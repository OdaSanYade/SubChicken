#include "EnemyBase.h"
#include "../../../../GameSystem/StateMachine/StateMachineManager.h"
#include "../../../../GameSystem/Combat/CombatFx.h"
#include "../../../../GameSystem/Combat/AttackTag.h"

#include "../../../../Inc/Math/Damp.h"

namespace {
	// 攻撃タグ → 体幹の表のどの行を使うか。値そのものは EnemyTuning.json の "poise"
	const EnemyTuning::Poise& FindPoise(uint32_t _tag)
	{
		const auto& table = EnemyTuning::Shared().poise;

		struct Row { uint32_t tag; const EnemyTuning::Poise* poise; };
		const Row rows[] = {
			{ PlayerAttackTag::kNormal,   &table.normal },
			{ PlayerAttackTag::kCombo3,   &table.combo3 },
			{ PlayerAttackTag::kCounter,  &table.counter },
			{ PlayerAttackTag::kFinisher, &table.finisher },
			{ PlayerAttackTag::kHeavy,    &table.heavy },
			{ PlayerAttackTag::kSmash,    &table.smash },
		};

		for (const auto& row : rows)
		{
			if (row.tag == _tag) return *row.poise;
		}
		return table.other;
	}

	// 被弾リアクションの段階。EnemyStateSetup の TryReaction が
	// 1:Stagger 2:Blow 3:Launch 4:Down の順で受け取る
	namespace Reaction
	{
		constexpr int kBlow = 2;
		constexpr int kLaunch = 3;
		constexpr int kDown = 4;
	}

	// これより遅いノックバックは止まっているとみなす(速さの2乗)
	constexpr float kStopSpeedSq = 0.0001f;

	// 割合を割り算に使うときに 0 と 1 ちょうどを避ける範囲
	constexpr float kRatioMin = 0.01f;
	constexpr float kRatioMax = 0.99f;

	// 減速距離が 0 になって割り算が壊れないようにする下限
	constexpr float kMinBrakeDistance = 0.001f;
}

EnemyBase::~EnemyBase()
{
	if (GetEffectID() != -1) GPUParticleSystem::Instance().StopEffect(GetEffectID());
}

// ===========================================================
// アニメーション
// ===========================================================
std::shared_ptr<KdAnimationData> EnemyBase::ResolveAnimation(const char* _name) const
{
	auto* work = GetWork();
	auto anim = work ? work->GetAnimation(_name) : nullptr;

#ifdef _DEBUG
	if (!anim && _name && _name[0] != '\0')
	{
		OutputDebugStringA("[Enemy] animation not found: ");
		OutputDebugStringA(_name);
		OutputDebugStringA("\n");
	}
#endif

	return anim;
}

void EnemyBase::AdvanceAnim()
{
	auto* sys = SM_MGR.Get(m_stateSystemName);
	auto* work = GetWork();
	if (!sys || !work) return;

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	sys->AdvanceAnimation(sm, m_enemyAnim, work->WorkNodes());
}

// ===========================================================
// 初期化
// ===========================================================
void EnemyBase::Init()
{
	// ---- 種類ごとのパラメータ ----
	// コードの MakeSpec() が既定値。EnemyTuning.json の "types" に同じ種類名があれば上書きする
	m_spec = MakeSpec();
	{
		const nlohmann::json types = Tuning::LoadSection(EnemyTuningKey::kFile, "types");
		const Tuning::Reader reader(types);
		reader(m_spec.typeName, m_spec);
	}
	ApplySpec();

	SetRenderModel();

	// ---- コリジョン ----
	if (auto sp = m_wpWorld.lock()) {
		//通常判定
		Collision::CollisionComponent init;
		init.shape = Collision::Shape::MakeCapsule(
			m_spec.capsuleRadius,
			m_spec.capsuleHeight * T().motion.capsuleHeightScale,
			Collision::CapsulePivot::Bottom);
		init.type = Collision::ObjectType::Enemy;
		init.blocksCapsule = true;
		init.inverseMass = 1.0f;
		m_handle = sp->CreateCollision(init);

		//被弾判定
		Collision::CombatComponent initCombat;
		initCombat.team = Collision::Team::Enemy;
		initCombat.hittable = true;
		m_combatHandle = sp->CreateCombat(initCombat);
	}

	// ---- ステートマシン ----
	// 定義の登録は同名なら既存を返すため、何体生成しても二重登録にならない。
	SetupStates();

	m_cm.add<StateMachineComponent>(m_entity, SM_MGR.MakeComponent(m_stateSystemName));
	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	if (auto* sys = SM_MGR.Get(sm))
	{
		auto resolver = [this](const char* name) -> std::shared_ptr<KdAnimationData>
			{
				return ResolveAnimation(name);
			};
		sys->SyncAnimation(sm, m_enemyAnim, resolver);
		sys->Update(sm, 0.0f, this, m_entity.index);
	}


	// 個体差。全員が同じ間合い・同じ間隔で動くと機械的に見える
	m_attackBias = KdRandom::GetFloat(0.0f, T().motion.attackBiasMax);
	m_slotDistanceBias = KdRandom::GetFloat(T().motion.slotBiasMin, T().motion.slotBiasMax) + m_spec.slotDistanceOffset;

	// HPは IChara の既定値を種類ごとの値で上書きする
	auto& hc = m_cm.get<Game::Health>(m_entity);
	hc.hp = m_spec.hp;
	m_maxHp = std::max(hc.hp, 1.0f);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	GameScale::ApplyToTransform(tc);

	// 体格差
	tc.SetScale(tc.GetScale() * m_spec.bodyScale);

	m_cm.add<LightComponent>(m_entity, { LightType::Point });
	auto& lc = m_cm.get<LightComponent>(m_entity);
	lc.enabled = false;
	lc.power = T().light.power;
	lc.range = T().light.range;
	lc.color = T().light.color;

	auto& rc = m_cm.get<RenderComponent>(m_entity);
	SetMaterial(rc, m_spec.defaultMaterial);
}

void EnemyBase::SetRenderModel()
{
	// ---- モデル ----
	auto model = RES_MGR.GetModelList()->GetModel(m_spec.modelName);
	ModelId id = RES_MGR.GetModelList()->GetModelId(m_spec.modelName);

	WorkId wid = RES_MGR.GetWorkPool()->Create(model, id);

	m_cm.add<RenderComponent>(m_entity,
		{ RenderKind::Work, wid,
		RES_MGR.GetWorkPool()->GetGeneration(wid) });
}

void EnemyBase::ApplySpec()
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	hc.hp = m_spec.hp;
	hc.maxHp = hc.hp;

	m_eyeHeight = m_spec.eyeHeight;
	m_sightOffset = m_spec.sightOffset;
	m_sightRadius = m_spec.sightRadius;
	m_sightHeight = m_spec.sightHeight;
	m_loseHysteresis = m_spec.loseHysteresis;

	m_poiseMax = m_spec.poiseMax;
	m_poiseDecay = m_spec.poiseDecay;

	m_knockbackDecay = m_spec.knockbackDecay;
}

// ===========================================================
// 更新
// ===========================================================
void EnemyBase::Update()
{
	const float deltaTime = static_cast<float>(Time::Instance().GetDeltaTime());

	m_poise = std::max(0.0f, m_poise - m_poiseDecay * deltaTime);
	if (m_strikeFlash > 0.0f)
		m_strikeFlash = std::max(0.0f, m_strikeFlash - CombatFx::Instance().RealDelta() / T().hud.strikeFlashSec);

	if (m_breakFlash > 0.0f)
		m_breakFlash = std::max(0.0f, m_breakFlash - CombatFx::Instance().RealDelta() / T().hud.breakFlashSec);

	if (m_tokenCooldown > 0.0f) m_tokenCooldown -= deltaTime;
	if (m_callCooldown > 0.0f) m_callCooldown -= deltaTime;
	if (m_fleeCooldown > 0.0f) m_fleeCooldown -= deltaTime;
	if (m_carryCooldown > 0.0f) m_carryCooldown -= deltaTime;

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	auto* sys = SM_MGR.Get(sm);
	if (!sys) return;

	auto resolver = [this](const char* name) -> std::shared_ptr<KdAnimationData>
		{
			return ResolveAnimation(name);
		};
	sys->SyncAnimation(sm, m_enemyAnim, resolver);

	// ステート更新(索敵・移動・遷移判定は全てここで行われる)
	sys->Update(sm, deltaTime, this, m_entity.index);

	// 遷移が起きた場合はここで合わせ直す。
	// やらないと PostUpdate の AdvanceAnimation が「遷移前のクリップ」を
	// 1フレーム進めてしまい、被弾や死亡のように Forced が連続する所で絵が飛ぶ。
	sys->SyncAnimation(sm, m_enemyAnim, resolver);

	// コリジョンへ提出
	auto sp = m_wpWorld.lock();
	if (!sp)return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	//現在地と移動量提出
	if (auto* col = sp->GetCollision(m_handle))
	{
		col->center = tc.GetPosition();
		col->moveVector = m_move;

		//当たり判定提出
		if (auto* combat = sp->GetCombat(m_combatHandle))
		{
			const Math::Vector3 mid = col->GetCapsuleMid();
			combat->hurtVolume.p0 = mid - Math::Vector3::Up * col->shape.segHalf;
			combat->hurtVolume.p1 = mid + Math::Vector3::Up * col->shape.segHalf;
			combat->hurtVolume.radius = col->shape.radius;
		}
	}
}

void EnemyBase::PostUpdate()
{
	// 走り去り中は当たり判定を通さない。被弾も着地も見ずに進むだけ。
	if (m_isLeaving)
	{
		auto& tcLeave = m_cm.get<TransformComponent>(m_entity);
		tcLeave.SetPosition(tcLeave.GetPosition() + m_move);
		AdvanceAnim();
		return;
	}

	//当たり判定確定
	auto sp = m_wpWorld.lock();
	if (!sp)
	{
		// ワールドが無くても絵は進める
		AdvanceAnim();
		return;
	}

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	auto* col = sp->GetCollision(m_handle);

	if (!col)
	{
		// コリジョンが見つからない場合、Transformを更新
		tc.SetPosition(tc.GetPosition() + m_move);
		return;
	}

	tc.SetPosition(col->result.position);

	if (m_isDead)
	{
		auto& sm = m_cm.get<StateMachineComponent>(m_entity);

		if (!sm.HasTag(EnemyTag::kBurst)) ApplyCorpsePose();

		if (m_deathFlash > 0.0f)
			m_deathFlash = std::max(0.0f, m_deathFlash - CombatFx::Instance().RealDelta() / T().death.flashSec);

		if (!sm.HasTag(EnemyTag::kDie))
			TickCorpse(Time::Instance().GetDeltaTime());

		// 死体でもアニメは進める。ここで return すると
		// 倒れるモーションが1フレーム目で止まったまま沈んでいく。
		AdvanceAnim();
		return;
	}

	if (col->result.grounded && !m_isKnockback)
	{
		// 「今フレーム接地」かつ「前フレームは空中だった」時だけ着地扱い
		if (!m_wasGroundedLastFrame) SetFlag(EnemyFlag::kLanded);

		m_isGrounded = true;
		m_gravity = 1.0f;
		m_jumpMoment = 0.0f;
	}
	else if (!col->result.grounded)
	{
		m_isGrounded = false;
	}
	m_wasGroundedLastFrame = col->result.grounded;

	//被弾判定回収
	auto* combat = sp->GetCombat(m_combatHandle);
	if (!combat)
	{
		AdvanceAnim();
		return;
	}
	for (auto& hit : combat->received)
	{
		// 無敵
		if (IsInvincible()) continue;

		// 死体は殴られない。
		if (m_isDead) continue;

		const bool frozen = IsFrozen();
		const auto& poise = FindPoise(hit.tag);

		// 硬化中の被弾
		float damage = hit.damage;
		if (frozen)
		{
			damage *= T().hit.frozenDamageMul;
			m_frozenDamage += damage;

			GPUParticleSystem::Instance().PlayEffect(T().hit.frozenEffect, GetPosition());

			// ブレイク値
			m_breakGauge += poise.breakGain;

			if (m_breakGauge >= T().hit.breakThreshold && !HasFlag(EnemyFlag::kShatterRequested))
			{
				auto& tcS = m_cm.get<TransformComponent>(m_entity);
				m_shatterOrigin = tcS.GetPosition() + Math::Vector3(0.0f, m_eyeHeight, 0.0f);
				SetFlag(EnemyFlag::kShatterRequested);
			}
		}

		// ---- 体幹を削る ----
		// 硬化中は体幹を無視して確定でブレイクする。
		m_poise += frozen ? m_poiseMax : poise.poise;

		// ブレイクするまでは怯ませない
		int   level = 0;
		float duration = 0.0f;

		if (m_poise >= m_poiseMax)
		{
			m_poise = 0.0f;
			m_breakFlash = 1.0f;

			// 崩れた拍子に口が開く。HUDの体幹ゲージがそのまま「取り返すまで」になる。
			ReleaseCarryByBreak();

			const bool launch = (hit.knockback.y >= T().hit.launchThreshold);
			level = launch ? Reaction::kLaunch : Reaction::kBlow;
			duration = (launch ? T().hit.staggerLaunchSec : T().hit.staggerBlowSec) + T().hit.breakVulnerableSec;
		}

		// ---- ノックバック ----
		// ※直前で崩れたときに m_poise を 0 に戻しているので、ここは常に false になる。
		//   スーパーアーマー中は崩しても押されない動きになっているが、
		//   挙動を変えないよう元のまま残している(要確認リスト参照)
		const bool poiseBroken = (m_poise >= m_poiseMax);
		if (!frozen && (!m_superArmor || poiseBroken))
		{
			// 崩していない間は押されるだけ。足は止めない。
			const float horizontal = T().hit.knockbackHorizontalMul;
			m_knockbackVelocity = { hit.knockback.x * horizontal, 0.0f, hit.knockback.z * horizontal };
			m_isKnockback = true;
			m_knockbackTimer = (level >= Reaction::kBlow) ? T().hit.knockbackTimeHeavy : T().hit.knockbackTimeLight;

			if (level >= Reaction::kBlow)
			{
				m_jumpMoment = std::max(hit.knockback.y, T().hit.launchMinUp);
				m_gravity = 0.0f;
				m_isGrounded = false;
				m_gravityScale = (level >= Reaction::kLaunch) ? T().hit.launchGravityScale : 1.0f;

				col->moveConfig.stepHeight = 0.0f;
				col->moveConfig.snapDistance = 0.0f;

				Math::Vector3 from = -m_knockbackVelocity;
				from.y = 0.0f;
				if (from.LengthSquared() > 0.0f)
				{
					from.Normalize();
					m_lastKnownPos = tc.GetPosition() + from * m_sightOffset;
				}

				RequestReaction(level, duration);
			}
		}

		GPUParticleSystem::Instance().PlayEffect(T().hit.effect, GetPosition());
		AudioManager::Instance().Play(T().hit.sound);

		//Hp減少
		auto& hc = m_cm.get<Game::Health>(m_entity);
		const float before = hc.hp;
		hc.hp -= damage;
		if (hc.hp <= 0.0f)
			hc.hp = 0.0f;


		const bool lethal = (before > 0.0f && hc.hp <= 0.0f);
		if (lethal)
		{
			// 硬化中に倒し切った場合も、溜めたぶんは周囲へ返す。
			if (frozen) ComputeShatterTransfer(true);
			Die(hit.knockback, damage - before);
		}

		// ---- 演出通知 ----
		CombatFx::HitEvent ev;
		ev.position = tc.GetPosition() + Math::Vector3(0.0f, m_eyeHeight, 0.0f);
		ev.knockback = hit.knockback;
		ev.damage = damage;
		ev.tag = hit.tag;
		ev.attackId = hit.attackId;
		ev.hitstopFrames = frozen ? hit.hitstopFrames + T().hit.frozenExtraHitstop : hit.hitstopFrames;
		ev.lethal = lethal;
		ev.attackerType = hit.attackerType;
		CombatFx::Instance().Notify(ev);

		CombatFx::Instance().AddTrauma(std::min(damage / std::max(T().hit.traumaPerDamage, 1.0f), T().hit.traumaMax));
	}

	// 壁に当たったら完全ゼロにせず軽く跳ね返す
	if (m_isKnockback && col->result.hitWall)
	{
		const float speed = m_knockbackVelocity.Length();

		Math::Vector3 n = col->result.wallNormal;
		n.y = 0.0f;
		if (n.LengthSquared() > 0.0f)
		{
			n.Normalize();
			const float into = m_knockbackVelocity.Dot(n);
			if (into < 0.0f)
				m_knockbackVelocity -= n * into * T().wall.bounce;
		}

		if (speed >= T().wall.slamSpeed)
		{
			auto& hc = m_cm.get<Game::Health>(m_entity);
			const float before = hc.hp;
			hc.hp -= T().wall.slamDamage;

			// 壁で倒した場合も、必ず通常の撃破と同じ経路を通す
			if (before > 0.0f && hc.hp <= 0.0f)
			{
				Math::Vector3 launch = m_knockbackVelocity * T().wall.killLaunchRatio;
				launch.y = T().wall.killLaunchUp;
				Die(launch, T().wall.slamDamage - before);

				// とどめ演出
				CombatFx::HitEvent ev;
				ev.position = tc.GetPosition() + Math::Vector3(0.0f, m_eyeHeight, 0.0f);
				ev.knockback = launch;
				ev.damage = T().wall.slamDamage;
				ev.tag = PlayerAttackTag::kNormal;
				ev.lethal = true;
				ev.attackerType = Collision::ObjectType::PlayerAttack;
				CombatFx::Instance().Notify(ev);

				m_knockbackVelocity *= T().wall.speedKeep;
			}
			else
			{
				m_knockbackVelocity *= T().wall.speedKeep;
				RequestReaction(Reaction::kDown, T().wall.reactionSec);
			}

			CombatFx::Instance().RequestHitStop(T().wall.hitstop);
			CombatFx::Instance().AddTrauma(T().wall.trauma);
		}
	}

	// 一定時間で通常の段差設定に戻す
	if (m_isKnockback)
	{
		m_knockbackTimer -= Time::Instance().GetDeltaTime();
		if (m_knockbackTimer <= 0.0f)
		{
			col->moveConfig.stepHeight = T().motion.knockbackStepHeight;
			col->moveConfig.snapDistance = T().motion.knockbackSnapDist;
			m_isKnockback = false;
		}
	}

	//デバッグキー敵死ぬ
	if (GetAsyncKeyState('B') & 0x8000)
	{
		auto& hc = m_cm.get<Game::Health>(m_entity);
		hc.hp = 0;
		Die(Math::Vector3::Zero, 1000);
	}

	AdvanceAnim();
}

Math::Vector3 EnemyBase::GetPosition() const
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	return tc.GetPosition();
}

KdModelWork* EnemyBase::GetWork() const
{
	if (m_cm.has<RenderComponent>(m_entity)) {
		auto& wp = m_cm.get<RenderComponent>(m_entity);
		return RES_MGR.GetWorkPool()->Get(wp.assetId, wp.generation);
	}
	else
	{
		return nullptr;
	}
}

// ===========================================================
// 索敵
// ===========================================================
Math::Vector3 EnemyBase::CalcSightCenter()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 eye = tc.GetPosition();
	eye.y += m_eyeHeight;

	//視線方向へ押し出した点が円の中心
	return eye + GetFacing() * m_sightOffset;
}

bool EnemyBase::IsTargetInSight()
{
	if (!m_hasTarget) return false;
	return CanSee(m_targetPos);
}

bool EnemyBase::CanSee(const Math::Vector3& _pos)
{
	Math::Vector3 diff = _pos - CalcSightCenter();

	if (std::fabs(diff.y) > m_sightHeight) return false;
	diff.y = 0.0f;

	float radius = m_sightRadius;

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	if (sm.HasTag(EnemyTag::kChase)) radius += m_loseHysteresis;

	return diff.LengthSquared() <= radius * radius;
}

bool EnemyBase::IsNear(const Math::Vector3& _pos, float _radius) const
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 diff = _pos - tc.GetPosition();
	if (std::fabs(diff.y) > m_sightHeight) return false;

	diff.y = 0.0f;
	return diff.LengthSquared() <= _radius * _radius;
}

float EnemyBase::GetTargetDistance()
{
	if (!m_hasTarget)return FLT_MAX;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 diff = m_targetPos - tc.GetPosition();
	diff.y = 0.0f;
	return diff.Length();
}

// ===========================================================
// 向き
// ===========================================================
Math::Vector3 EnemyBase::GetFacing()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 facing = tc.GetForward() * kFacingSign;
	facing.y = 0.0f;
	if (facing.LengthSquared() > 0.0f) facing.Normalize();
	return facing;
}

void EnemyBase::ApplyRotation(float _dt)
{
	if (m_rotateDir.LengthSquared() <= 0.0f)return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// TestEntity::ApplyRotation と同じ規約で目標姿勢を作る
	const float targetAngle = atan2f(
		m_rotateDir.x * kFacingSign,
		m_rotateDir.z * kFacingSign);

	const Math::Quaternion targetRot =
		Math::Quaternion::CreateFromAxisAngle(Math::Vector3::Up, targetAngle);

	tc.SetRotation(Damp::Rotation(tc.GetRotation(), targetRot, m_turnRate, _dt));
}

void EnemyBase::FaceTarget(float _turnRate)
{
	if (!m_hasTarget)return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 toTarget = m_targetPos - tc.GetPosition();
	toTarget.y = 0.0f;
	if (toTarget.LengthSquared() <= 0.0f)return;

	toTarget.Normalize();
	m_rotateDir = toTarget;
	m_turnRate = _turnRate;
}

void EnemyBase::RefreshWanderDir()
{
	Math::Vector3 dir(
		KdRandom::GetFloat(-1.0f, 1.0f),
		0.0f,
		KdRandom::GetFloat(-1.0f, 1.0f));

	if (dir.LengthSquared() > 0.0f)
	{
		dir.Normalize();
		m_rotateDir = dir;
	}
	m_wanderTimer = KdRandom::GetFloat(T().motion.wanderSecMin, T().motion.wanderSecMax);
}

void EnemyBase::BeginLookAround()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	//見失った方向を見回しの基準にする
	Math::Vector3 lost = m_lastKnownPos - tc.GetPosition();
	lost.y = 0.0f;
	if (lost.LengthSquared() <= 0.0f) lost = GetFacing();

	if (lost.LengthSquared() > 0.0f)
	{
		lost.Normalize();
		m_lostDir = lost;
	}

	m_rotateDir = m_lostDir;
	m_lookTimer = 0.0f;
}

void EnemyBase::RefreshLookAroundDir()
{
	m_rotateDir = RotateYaw(m_lostDir, KdRandom::GetFloat(-T().motion.lookAroundDeg, T().motion.lookAroundDeg));
	m_lookTimer = KdRandom::GetFloat(T().motion.lookSecMin, T().motion.lookSecMax);
}

Math::Vector3 EnemyBase::RotateYaw(const Math::Vector3& _dir, float _deg)
{
	const float rad = DirectX::XMConvertToRadians(_deg);
	const float s = std::sin(rad);
	const float c = std::cos(rad);
	return Math::Vector3(_dir.x * c + _dir.z * s, 0.0f, -_dir.x * s + _dir.z * c);
}

// ===========================================================
// ひよこの連れ去り
// ===========================================================
void EnemyBase::BeginCarry(int _chickId)
{
	if (m_isDead || !m_isActive) return;

	m_carryChickId = _chickId;

	// 咥えたら戦わない。殴りながら運ばれると避けようが無くなる。
	ReleaseAttackToken();
	ClearApproachPoint();

	SetFlag(EnemyFlag::kCarryStarted);
}

void EnemyBase::EndCarry()
{
	if (!IsCarrying()) return;

	m_carryChickId = -1;
	m_carryCooldown = T().carry.cooldown;

	// 離した瞬間に Carry へ入り直さないよう、予約も捨てる
	ClearFlag(EnemyFlag::kCarryStarted);
}

void EnemyBase::ReleaseCarryByBreak()
{
	if (!IsCarrying()) return;

	// 走り去り中は判定を切っているので通常は来ないが、念のため弾く
	if (m_isLeaving) return;

	// 口を開く。ひよこ側は次の確認で「離された」と知り、足元に降りる。
	// 崩れの怯み(吹き飛び/打ち上げ)は体幹ブレイク側がそのまま出す。
	EndCarry();

	// 取り返した手応え
	GPUParticleSystem::Instance().PlayEffect(T().carry.releaseEffect, GetPosition());
	CombatFx::Instance().RequestHitStop(T().carry.releaseHitstop);
	CombatFx::Instance().AddTrauma(T().carry.releaseTrauma);
}

Math::Vector3 EnemyBase::GetMouthPosition()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// ---- 保険 ----
	const float scale = m_spec.bodyScale;
	const Math::Vector3 fallback = tc.GetPosition()
		+ GetFacing() * (m_spec.mouthOffset * scale)
		+ Math::Vector3::Up * ((m_eyeHeight + m_spec.mouthHeight) * scale);

	if (m_mouthNodeMissing || !m_spec.mouthNode) return fallback;

	auto* work = GetWork();
	if (!work) return fallback;

	// アニメーションを進めた直後はノード行列が未計算のことがある
	if (work->NeedCalcNodeMatrices()) work->CalcNodeMatrices();

	const auto* node = work->FindNode(m_spec.mouthNode);
	if (!node)
	{
		// 一度見つからなければこのモデルには無い。毎フレーム総当たりしない。
		m_mouthNodeMissing = true;
		return fallback;
	}

	return (node->m_worldTransform * tc.GetWorldMatrix()).Translation();
}

// ===========================================================
// 走り去り
// ===========================================================
void EnemyBase::BeginLeave()
{
	if (m_isLeaving) return;

	m_isLeaving = true;
	m_leaveTimer = 0.0f;
	m_leaveScale = 1.0f;

	// 溜めや予告の途中で入っても元の大きさから縮める
	CaptureBaseScale();
	EndWindup();
	EndAttackNotice();

	ReleaseAttackToken();
	ClearApproachPoint();

	m_knockbackVelocity = Math::Vector3::Zero;
	StopMotion();

	// 壊さずに止める。ハンドルを生かしておけば、片付けの順番に左右されない。
	if (auto sp = m_wpWorld.lock())
	{
		// 押し合いもステージとの当たりも切る。壁を抜けて走り去れるようにする。
		if (auto* col = sp->GetCollision(m_handle)) col->enabled = false;

		// 殴られない。走り去り中に倒されてひよこを落とすことはない。
		if (auto* combat = sp->GetCombat(m_combatHandle))
		{
			combat->hittable = false;
			combat->received.clear();
		}
	}
}

bool EnemyBase::TickLeave(float _dt)
{
	if (!m_isLeaving) return false;
	if (m_isVanished) return true;

	m_leaveTimer += _dt;
	const float p = std::clamp(m_leaveTimer / T().carry.leaveSec, 0.0f, 1.0f);

	// 走り出しはゆっくり、最後に一気に縮む
	m_leaveScale = 1.0f - p * p;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_baseScale * std::max(m_leaveScale, kLeaveMinScale));

	if (p < 1.0f) return false;

	m_isVanished = true;
	return true;
}

// ===========================================================
// 仲間を呼ぶ
// ===========================================================
void EnemyBase::BeginCall()
{
	// 実際の配布はマネージャの担当。ここは「呼んだ」事実だけを立てる。
	SetFlag(EnemyFlag::kCallRequested);
	m_callCooldown = m_spec.callInterval;
}

bool EnemyBase::ReceiveCall(const Math::Vector3& _pos)
{
	if (m_isDead || !m_isActive) return false;

	// 硬化中や怯み中は聞こえない。動けない敵に予約を残しても嘘になる。
	if (IsFrozen() || m_staggerTimer > 0.0f) return false;

	// 既に自力で見えている敵に呼びかけても意味が無い
	if (IsTargetInSight()) return false;

	// 「最後に見た位置」を上書きする。捜索も見回しもこの点を基準にする。
	m_lastKnownPos = _pos;
	SetFlag(EnemyFlag::kCallReceived);
	return true;
}

void EnemyBase::ApplyCallPose(float _t01)
{
	CaptureBaseScale();

	// 一度伸び上がって戻る。攻撃の溜め(潰れ)とは逆向きにして、
	// 遠目でも「殴ってくる」のか「呼んでいる」のかを見分けられるようにする。
	const float t = std::clamp(_t01, 0.0f, 1.0f);
	const float s = std::sin(t * DirectX::XM_PI) * T().pose.callStretch;

	Math::Vector3 scale = m_baseScale;
	scale.y *= (1.0f + s);
	scale.x *= (1.0f - s * T().pose.callNarrow);
	scale.z *= (1.0f - s * T().pose.callNarrow);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(scale);
}

void EnemyBase::EndCallPose()
{
	if (!m_scaleCaptured) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_baseScale);
}

// ===========================================================
// 士気
// ===========================================================
bool EnemyBase::WantsToFlee()
{
	if (m_spec.fleeMorale <= 0.0f) return false;
	if (m_isDead || !m_isActive) return false;
	if (m_fleeCooldown > 0.0f) return false;

	if (m_morale > m_spec.fleeMorale) return false;

	// 群れが薄いだけでは折れない
	return GetHealthRatio() <= m_spec.fleeHealth;
}

void EnemyBase::FaceAwayFromTarget(float _turnRate)
{
	if (!m_hasTarget) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 away = tc.GetPosition() - m_targetPos;
	away.y = 0.0f;
	if (away.LengthSquared() <= 0.0f) return;

	away.Normalize();
	m_rotateDir = away;
	m_turnRate = _turnRate;
}

void EnemyBase::FaceDirection(const Math::Vector3& _dir, float _turnRate)
{
	Math::Vector3 dir = _dir;
	dir.y = 0.0f;
	if (dir.LengthSquared() <= 0.0f) return;

	dir.Normalize();
	m_rotateDir = dir;
	m_turnRate = _turnRate;
}

// ===========================================================
// 音楽との同期
// ===========================================================
bool EnemyBase::IsAttackBeatReady() const
{
	auto& audio = AudioManager::Instance();

	// 曲が鳴っていない/テンポ未設定なら従来通り即座に通す。
	// 音を後から差し替えても挙動が壊れないようにするため。
	if (!audio.HasMusicClock()) return true;

	return audio.IsNearBeat(T().squad.beatWindow, T().squad.beatDivision);
}

// ===========================================================
// デバッグ表示
// ===========================================================
const char* EnemyBase::GetStateLabel()
{
	// 上から順に見て最初に一致したものを返す。分岐ではなくテーブル引き。
	// 強い状態(死/硬化/被弾)を先に置くこと。
	struct Entry { unsigned int tag; const char* name; };
	static const Entry kTable[] = {
		{ EnemyTag::kDie,    "Die"    },
		{ EnemyTag::kBurst,  "Burst"  },
		{ EnemyTag::kFrozen, "Frozen" },
		{ EnemyTag::kCarry,  "Carry"  },
		{ EnemyTag::kDamage, "Damage" },
		{ EnemyTag::kFlee,   "Flee"   },
		{ EnemyTag::kNotice, "Notice" },
		{ EnemyTag::kAttack, "Attack" },
		{ EnemyTag::kChase,  "Chase"  },
		{ EnemyTag::kAlert,  "Alert"  },
		{ EnemyTag::kNormal, "Normal" },
	};

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	for (const auto& e : kTable)
	{
		if (sm.HasTag(e.tag)) return e.name;
	}
	return "-";
}

// ===========================================================
// 捜索
// ===========================================================
void EnemyBase::FaceLastKnown(float _turnRate)
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 to = m_lastKnownPos - tc.GetPosition();
	to.y = 0.0f;
	if (to.LengthSquared() <= 0.0f) return;

	to.Normalize();
	m_rotateDir = to;
	m_turnRate = _turnRate;
}

float EnemyBase::GetLastKnownDistance()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 diff = m_lastKnownPos - tc.GetPosition();
	diff.y = 0.0f;
	return diff.Length();
}

// ===========================================================
// タイマー
// ===========================================================
bool EnemyBase::TickWanderTimer(float _dt)
{
	m_wanderTimer -= _dt;
	return (m_wanderTimer <= 0.0f);
}

bool EnemyBase::TickLookTimer(float _dt)
{
	m_lookTimer -= _dt;
	return (m_lookTimer <= 0.0f);
}

// ===========================================================
// 移動
// ===========================================================
void EnemyBase::MoveAlong(const Math::Vector3& _dir, float _dt, float _speed, bool _applyGravity)
{
	Math::Vector3 dir = _dir;
	dir.y = 0.0f;
	if (dir.LengthSquared() > 0.0f) dir.Normalize();

	if (_applyGravity)
	{
		//重力(接地中は地面に張り付かせる最小限の下向きの力)
		float vertical = -T().motion.groundStick;
		if (!m_isGrounded)
		{
			if (m_gravity <= 0.0f) m_gravity = 1.0f;
			m_gravity *= m_gravityPow;
			m_gravity = std::min(m_gravity, m_gravityMax);

			m_jumpMoment *= T().motion.jumpDecay;
			if (m_jumpMoment < T().motion.jumpCutoff) m_jumpMoment = 0.0f;
			vertical = m_jumpMoment - m_gravity * m_gravityScale;
			m_gravityScale = 1.0f;
		}

		m_move = (dir * _speed + Math::Vector3::Up * vertical) * _dt
			+ m_knockbackVelocity * T().motion.knockbackScale * _dt;
	}
	else
	{
		// 重力なし。座標は PostUpdate でまとめて動かす。ここで動かすと2重に進む。
		m_move = dir * _speed * _dt;
	}
	//ノックバックは指数減衰で自然に収まる
	m_knockbackVelocity *= std::exp(-m_knockbackDecay * _dt);
	if (m_knockbackVelocity.LengthSquared() < kStopSpeedSq)
		m_knockbackVelocity = Math::Vector3::Zero;
}

void EnemyBase::ApplyMotion(float _dt, float _speed)
{
	ApplyRotation(_dt);
	MoveAlong(GetFacing(), _dt, _speed);
}

void EnemyBase::ApplyChaseMotion(float _dt, float _speed, bool _applyGravity)
{
	ApplyRotation(_dt);

	// 旋回を待たずに詰められるよう、進行方向は「向き」ではなく「相手の方向」
	MoveAlong(m_rotateDir, _dt, _speed, _applyGravity);
}

void EnemyBase::ApplyApproachMotion(float _dt, float _speed)
{
	// 持ち場が配られていなければ従来通り相手へ直進する
	if (!m_hasApproachPoint)
	{
		ApplyChaseMotion(_dt, _speed);
		return;
	}

	ApplyRotation(_dt);	// 向きは m_rotateDir(＝相手の方向)のまま

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 to = m_approachPoint - tc.GetPosition();
	to.y = 0.0f;

	const float dist = to.Length();
	if (dist <= T().squad.approachSlack)
	{
		// 足は止めるが、重力とノックバック減衰は進める
		MoveAlong(Math::Vector3::Zero, _dt, 0.0f);
		return;
	}

	to /= dist;

	// 持ち場の手前で減速する
	const float brake = std::max(T().squad.approachSlack * T().squad.approachBrakeRatio, kMinBrakeDistance);
	const float speed = _speed * std::min(dist / brake, 1.0f);
	MoveAlong(to, _dt, speed);
}

float EnemyBase::GetApproachDistance()
{
	if (!m_hasApproachPoint) return GetTargetDistance();

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 diff = m_approachPoint - tc.GetPosition();
	diff.y = 0.0f;
	return diff.Length();
}

bool EnemyBase::IsAtApproachPoint() const
{
	// 持ち場が無い間は「着いていない」。追いかけ続ける。
	if (!m_hasApproachPoint) return false;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 diff = m_approachPoint - tc.GetPosition();
	diff.y = 0.0f;
	return diff.LengthSquared() <= T().squad.approachSlack * T().squad.approachSlack;
}

bool EnemyBase::IsOffApproachPoint() const
{
	// 持ち場が無い間は追跡扱い。ApplyApproachMotion の分岐と揃える。
	if (!m_hasApproachPoint) return true;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 diff = m_approachPoint - tc.GetPosition();
	diff.y = 0.0f;
	return diff.LengthSquared() > T().squad.approachLeave * T().squad.approachLeave;
}

// ===========================================================
// 攻撃前の予備動作
// ===========================================================
void EnemyBase::BeginWindup()
{
	CaptureBaseScale();
	m_windupActive = true;
}

void EnemyBase::ApplyWindupPose(float _t01)
{
	if (!m_windupActive) return;

	const float t = std::clamp(_t01, 0.0f, 1.0f);
	m_windupProgress = t;

	// 前半で潰れ、後半で伸び上がる。
	// 潰れ切る手前で一番遅くなるようにして、溜めの「間」を作る。
	const EnemyTuning::Pose& pose = T().pose;
	const float split = std::clamp(pose.windupSplit, kRatioMin, kRatioMax);	// 0 と 1 は割り算が壊れるので避ける
	const float s = (t < split)
		? pose.windupSquash * Damp::EaseOut(t / split)
		: -pose.windupStretch * ((t - split) / (1.0f - split));

	Math::Vector3 scale = m_baseScale;
	scale.y *= (1.0f - s);
	scale.x *= (1.0f + s * pose.windupWiden);
	scale.z *= (1.0f + s * pose.windupWiden);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(scale);
}

void EnemyBase::EndWindup()
{
	if (!m_windupActive) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_baseScale);
	m_windupActive = false;
	m_windupProgress = 0.0f;
	m_strikeFlash = 0.0f;
}

// ===========================================================
// 被弾リアクション
// ===========================================================
void EnemyBase::RequestReaction(int _level, float _duration)
{
	if (m_staggerTimer > 0.0f && _level < static_cast<int>(m_reactionLevel))
	{
		m_staggerTimer = std::max(m_staggerTimer, _duration);
		m_staggerDuration = std::max(m_staggerDuration, m_staggerTimer);
		SetFlag(EnemyFlag::kReactionRequested);
		return;
	}

	m_reactionLevel = static_cast<uint8_t>(_level);
	m_staggerDuration = _duration;
	m_staggerTimer = _duration;
	SetFlag(EnemyFlag::kReactionRequested);
}

int EnemyBase::ConsumeReactionLevel()
{
	if (!ConsumeFlag(EnemyFlag::kReactionRequested)) return 0;
	return static_cast<int>(m_reactionLevel);
}

bool EnemyBase::TickStagger(float _dt)
{
	if (m_staggerTimer <= 0.0f) return true;

	m_staggerTimer -= _dt;
	return (m_staggerTimer <= 0.0f);
}

float EnemyBase::GetStaggerProgress() const
{
	if (m_staggerDuration <= 0.0f) return 1.0f;
	return std::clamp(1.0f - (m_staggerTimer / m_staggerDuration), 0.0f, 1.0f);
}

void EnemyBase::ApplyHitPose()
{
	CaptureBaseScale();

	// 当たった瞬間が最大の潰れ、硬直の終わりに向けて戻る。
	// 2乗で立ち上がりを鋭くすると衝撃が乗る。
	const float p = 1.0f - GetStaggerProgress();
	const float s = T().pose.hitSquash * p * p;

	Math::Vector3 scale = m_baseScale;
	scale.y *= (1.0f - s);
	scale.x *= (1.0f + s * T().pose.hitWiden);
	scale.z *= (1.0f + s * T().pose.hitWiden);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(scale);
}

void EnemyBase::EndHitPose()
{
	if (!m_scaleCaptured) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_baseScale);
}

void EnemyBase::CaptureBaseScale()
{
	if (m_scaleCaptured) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	m_baseScale = tc.GetScale();
	m_scaleCaptured = true;
}

// ===========================================================
// 攻撃
// ===========================================================
void EnemyBase::BeginAttack()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 踏み込んだ瞬間の方向で固定する。
	// 振り中も追尾させると回避不能な攻撃になってしまう。
	Math::Vector3 dir = m_targetPos - tc.GetPosition();
	dir.y = 0.0f;
	if (dir.LengthSquared() > 0.0f)
	{
		dir.Normalize();
		m_attackDir = dir;
	}
	else
	{
		m_attackDir = GetFacing();
	}

	m_rotateDir = m_attackDir;

	// IChara::StartAttack()。attackId が進むことで
	// 判定を毎フレーム出しても1回の攻撃で1度しか当たらない。
	StartAttack();
	++m_attackChain;

	m_strikeFlash = 1.0f;
	CombatFx::Instance().AddTrauma(T().pose.windupTrauma);
}

void EnemyBase::ReleaseAttackToken()
{
	// 持っていた場合だけ冷却する。持っていない敵が何度呼んでも副作用が無いようにする。
	if (m_hasAttackToken) m_tokenCooldown = T().squad.tokenCooldown;

	m_hasAttackToken = false;
	m_wantsAttack = false;
	m_attackRole = AttackRole::None;
	m_attackChain = 0;
}

void EnemyBase::FaceAttackDir(float _turnRate)
{
	if (m_attackDir.LengthSquared() <= 0.0f)return;

	m_rotateDir = m_attackDir;
	m_turnRate = _turnRate;
}

void EnemyBase::SpawnAttackHitbox(float _damage, float _reach, float _radius, float _knockback)
{
	auto sp = m_wpWorld.lock();
	if (!sp)return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 体格に合わせて上げる
	Math::Vector3 origin = tc.GetPosition();
	origin.y += m_eyeHeight;

	const Math::Vector3 dir =
		(m_attackDir.LengthSquared() > 0.0f) ? m_attackDir : GetFacing();

	Collision::HitData data;
	data.damage = _damage;
	data.knockback = dir * _knockback + Math::Vector3(0.0f, _knockback * 0.5f, 0.0f);
	data.hitstopFrames = T().attack.hitstopFrames;
	data.attackId = m_currentAttackId;
	data.tag = 0;
	data.attackerType = Collision::ObjectType::EnemyAttack;

	// ---- 判定 ----
	const int steps = (std::fabs(m_swing.yawDeg) >= T().attack.sweepYawMin) ? T().attack.swingSubSteps : 1;

	for (int i = 0; i < steps; ++i)
	{
		// 刻みの位置 0〜1。1回だけなら真ん中(正面)
		constexpr float kCenter = 0.5f;
		const float p = (steps <= 1) ? kCenter
			: static_cast<float>(i) / static_cast<float>(steps - 1);

		const float yaw = m_swing.yawDeg * (p - kCenter);
		const Math::Vector3 d = (steps <= 1) ? dir
			: Math::Vector3::Transform(dir,
				Math::Matrix::CreateRotationY(DirectX::XMConvertToRadians(yaw)));

		Collision::HitVolume vol;
		vol.p0 = origin;
		vol.p1 = origin + d * _reach;
		vol.radius = _radius;
		sp->SubmitHitbox(vol, data, Collision::Team::Enemy, m_combatHandle);
	}
}

// ===========================================================
// 硬化(鳴き声)
// ===========================================================
void EnemyBase::Freeze(float _duration)
{
	// 走り去り中は固めない。固めるとひよこを離してしまう。
	if (m_isDead || m_isLeaving) return;

	// 既に硬化しているかどうかで扱いを変える
	const bool already = IsFrozen();

	// 解除を個体ごとにずらす
	const float stagger = KdRandom::GetFloat(0.0f, T().shatter.freezeStaggerMax);

	m_freezeTimer = std::max(m_freezeTimer, _duration + stagger);
	m_freezeDuration = std::max(m_freezeDuration, m_freezeTimer);

	m_knockbackVelocity = Math::Vector3::Zero;
	StopMotion();
	ReleaseAttackToken();
	EndCarry();

	if (already) return;

	SetFlag(EnemyFlag::kFreezeRequested);

	m_frozenDamage = 0.0f;
	m_breakGauge = 0.0f;
	m_shatterTransfer = 0.0f;
	m_chainDepth = 0;
	m_shatterFromChain = false;
}

bool EnemyBase::TickFreeze(float _dt)
{
	if (m_freezeTimer <= 0.0f) return true;

	m_freezeTimer -= _dt;
	return (m_freezeTimer <= 0.0f);
}

float EnemyBase::GetFreezeProgress() const
{
	if (m_freezeDuration <= 0.0f) return 1.0f;
	return std::clamp(1.0f - (m_freezeTimer / m_freezeDuration), 0.0f, 1.0f);
}

float EnemyBase::GetBreakRatio() const
{
	return std::clamp(m_breakGauge / T().hit.breakThreshold, 0.0f, 1.0f);
}

// 周囲へ配る量を確定させ、マネージャへ通知する。
void EnemyBase::ComputeShatterTransfer(bool _broken)
{
	if (!m_shatterFromChain)
	{
		const float rate = _broken ? T().shatter.breakTransferRate : T().shatter.timeoutTransferRate;
		m_shatterTransfer = std::min(m_frozenDamage * rate, T().shatter.transferMax);
		m_chainDepth = 0;
	}

	SetFlag(EnemyFlag::kShatterBurst);
}

void EnemyBase::ApplyFrozenPose()
{
	CaptureBaseScale();

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 固まった瞬間に一度だけ縦へ伸びて、そのまま静止する。
	// 解ける直前に細かく震わせて「もうすぐ動く」を伝える。
	const float p = GetFreezeProgress();

	const EnemyTuning::Pose& pose = T().pose;

	float stretch = 0.0f;
	if (p < pose.freezeStretchRatio)	// 硬化の瞬間
		stretch = (1.0f - p / pose.freezeStretchRatio) * pose.freezeStretch;

	float shiver = 0.0f;
	if (p > pose.freezeShiverFrom && pose.freezeShiverFrom < 1.0f)
	{
		const float t = (p - pose.freezeShiverFrom) / (1.0f - pose.freezeShiverFrom);
		shiver = std::sin(m_freezeTimer * pose.freezeShiverSpeed) * pose.freezeShiver * t;
	}

	Math::Vector3 scale = m_baseScale;
	scale.y *= (1.0f + stretch);
	scale.x *= (1.0f - stretch * pose.freezeNarrow + shiver);
	scale.z *= (1.0f - stretch * pose.freezeNarrow - shiver);
	tc.SetScale(scale);
}

void EnemyBase::EndFrozenPose()
{
	m_freezeTimer = 0.0f;
	m_freezeDuration = 0.0f;

	if (!m_scaleCaptured) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_baseScale);
}

// ===========================================================
// 死
// ===========================================================
void EnemyBase::Die(const Math::Vector3& _knockback, float _excess)
{
	if (m_isDead) return;

	m_isDead = true;
	m_corpseTimer = T().death.corpseStaySec;
	m_deathFlash = 1.0f;

	// 演出と死体の進行は Die ステートに任せる。
	// ここは「死んだ」という事実だけを立てて、絵はステート側へ寄せる。
	SetFlag(EnemyFlag::kDeathRequested);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	m_deathPosition = tc.GetPosition();

	// 超過ダメージのぶんだけ吹っ飛びを増幅する。
	// 「ちょうど倒した」より「大きく上回って倒した」方が派手になる。
	m_overkillScale = std::min(1.0f + std::max(_excess, 0.0f) / std::max(T().death.overkillPerDamage, 1.0f), T().death.overkillMax);

	m_knockbackVelocity = Math::Vector3(_knockback.x, 0.0f, _knockback.z) * m_overkillScale;
	m_jumpMoment = std::max(_knockback.y, T().death.minLaunchUp) * m_overkillScale;
	m_gravity = 0.0f;
	m_gravityScale = 1.0f;
	m_isGrounded = false;

	// 被弾判定を降ろす。死体に判定が残るとコンボが不自然に伸びる。
	if (auto sp = m_wpWorld.lock())
	{
		if (auto* combat = sp->GetCombat(m_combatHandle))
			combat->hittable = false;

		// 死体に押されないようにする。壁役として残ると戦線が歪む。
		// 死体で足を止めたい場合はこの2行を消せば元の挙動に戻る。
		if (auto* col = sp->GetCollision(m_handle))
			col->blocksCapsule = false;
	}

	// 攻撃権を握ったまま死ぬと、他の敵に順番が回らなくなる
	ReleaseAttackToken();
	ClearApproachPoint();

	EndCarry();
}

void EnemyBase::BurstFromFreeze(bool _broken)
{
	ComputeShatterTransfer(_broken);

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 真上寄りに大きく飛ばす。氷が砕けて弾かれる絵にする。
	Math::Vector3 dir = tc.GetPosition() - m_shatterOrigin;
	dir.y = 0.0f;
	if (dir.LengthSquared() <= 0.0f) dir = -GetFacing();
	dir.Normalize();

	// 時間切れは「勝手に解けた」なので、絵も手応えも一段落とす。
	// ここで差をつけないと、ブレイクを狙う理由がなくなる。
	const EnemyTuning::Shatter& sh = T().shatter;
	const float power = _broken ? 1.0f : sh.timeoutPower;

	m_knockbackVelocity = dir * sh.burstSpeed * power;
	m_jumpMoment = sh.burstUp * power;
	m_gravity = 0.0f;
	m_gravityScale = sh.burstGravityScale;
	m_isGrounded = false;
	m_isKnockback = true;
	m_knockbackTimer = sh.burstKnockbackSec;

	RequestReaction(Reaction::kBlow, _broken ? sh.reactionBrokenSec : sh.reactionTimeoutSec);

	CombatFx::Instance().RequestHitStop(_broken ? sh.hitstopBroken : sh.hitstopTimeout);
	CombatFx::Instance().AddTrauma(sh.trauma * power);
	CombatFx::Instance().AddVibration(sh.vibration.x * power, sh.vibration.y, sh.vibration.z);

	m_frozenDamage = 0.0f;
	m_breakGauge = 0.0f;
	m_shatterFromChain = false;
}

// ===========================================================
// 攻撃通知
// ===========================================================
void EnemyBase::BeginAttackNotice()
{
	if (m_noticeActive) return;	// 冪等。毎フレーム呼ばれても増やさない

	m_noticeActive = true;
	m_noticeProgress = 0.0f;

	auto& rc = m_cm.get<RenderComponent>(m_entity);

	if (m_spec.noticeMaterial)
	{
		SetMaterial(rc, m_spec.noticeMaterial);
		SetParam(rc, 0, 0.0f);
	}

	if (m_spec.noticeEffect)
	{
		// 硬化の常時エフェクト(m_effectID)とは別のハンドルで持つ。
		// 硬化中に通知が乗っても互いを消し合わないようにするため。
		m_noticeEffectId = GPUParticleSystem::Instance().SpawnEffect(
			m_spec.noticeEffect, GetNoticeOrigin());
	}
}

void EnemyBase::TickAttackNotice(float _progress01)
{
	if (!m_noticeActive) return;

	m_noticeProgress = std::clamp(_progress01, 0.0f, 1.0f);

	// 当たる瞬間へ向けて強くなる。マテリアル側で点滅なり発光なりに使う。
	if (m_spec.noticeMaterial)
	{
		auto& rc = m_cm.get<RenderComponent>(m_entity);
		SetParam(rc, 0, m_noticeProgress);
	}

	if (m_noticeEffectId >= 0)
		GPUParticleSystem::Instance().SetEffectPos(m_noticeEffectId, GetNoticeOrigin());
}

void EnemyBase::EndAttackNotice()
{
	if (!m_noticeActive) return;

	m_noticeActive = false;
	m_noticeProgress = 0.0f;

	if (m_spec.noticeMaterial)
	{
		auto& rc = m_cm.get<RenderComponent>(m_entity);
		SetMaterial(rc, m_spec.defaultMaterial);
	}

	if (m_noticeEffectId >= 0)
	{
		GPUParticleSystem::Instance().StopEffect(m_noticeEffectId);
		m_noticeEffectId = -1;
	}
}

Math::Vector3 EnemyBase::GetNoticeOrigin()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 体格に合わせて上げる。足元だと大きい敵で埋まる。
	return tc.GetPosition() + Math::Vector3(0.0f, m_eyeHeight + 1.0_m, 0.0f);
}

// ===========================================================
// 断末魔
// ===========================================================
void EnemyBase::ApplyFusePose(float _t01)
{
	CaptureBaseScale();

	// 全方向に膨らむ。潰れ(被弾)とも伸び(呼び声)とも違う形にして、
	// 「今から破裂する」を一目で分からせる。
	const float t = std::clamp(_t01, 0.0f, 1.0f);
	const EnemyTuning::Pose& pose = T().pose;
	const float s = t * t * pose.fuseSwell;

	// 終盤だけ細かく震わせて、爆発の瞬間を予告する
	const float shiverFrom = std::min(pose.fuseShiverFrom, kRatioMax);
	const float shiver = (t > shiverFrom)
		? std::sin(t * pose.fuseShiverSpeed) * pose.fuseShiver * ((t - shiverFrom) / (1.0f - shiverFrom)) : 0.0f;

	Math::Vector3 scale = m_baseScale;
	scale.x *= (1.0f + s + shiver);
	scale.y *= (1.0f + s - shiver);
	scale.z *= (1.0f + s + shiver);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(scale);
}

void EnemyBase::SpawnDeathBurst()
{
	auto sp = m_wpWorld.lock();
	if (!sp) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// attackId を進める。判定を数フレーム出しても1回しか当たらない。
	StartAttack();

	Math::Vector3 origin = tc.GetPosition();
	origin.y += m_eyeHeight;

	// 球で出す。放射状の爆風なので方向を持たせない。
	Collision::HitVolume vol;
	vol.p0 = origin;
	vol.p1 = origin;
	vol.radius = m_spec.burstRadius;

	Collision::HitData data;
	data.damage = m_spec.burstDamage;
	// 打ち上げ主体にする。放射方向は当てる側では決められないため、
	// 上へ飛ばして「巻き込まれた」感触に寄せる。
	data.knockback = Math::Vector3(0.0f, m_spec.burstKnockback, 0.0f);
	data.hitstopFrames = 6;
	data.attackId = m_currentAttackId;
	data.tag = 0;
	data.attackerType = Collision::ObjectType::EnemyAttack;

	sp->SubmitHitbox(vol, data, Collision::Team::Enemy, m_combatHandle);

	GPUParticleSystem::Instance().PlayEffect("Destroy", origin);
	GPUParticleSystem::Instance().PlayEffect("Blow", origin);

	// 破裂した勢いで自分も跳ねる。死体のなぎ倒しにも繋がる。
	m_knockbackVelocity = -GetFacing() * T().death.burstSelfKnockback;
	m_jumpMoment = T().death.burstSelfUp;
	m_gravity = 0.0f;
	m_isGrounded = false;
}

// ===========================================================
// 死体のなぎ倒し
// ===========================================================
bool EnemyBase::IsCorpseFlying() const
{
	if (!m_isDead) return false;
	if (m_corpseSlamCooldown > 0.0f) return false;

	// 沈み始めた死体は当たらない。見た目が地面に埋まっているため。
	if (m_corpseTimer <= T().death.corpseSinkSec) return false;

	return m_knockbackVelocity.LengthSquared() >= (T().corpseSlam.speed * T().corpseSlam.speed);
}

void EnemyBase::OnCorpseSlammed(int _count)
{
	// 巻き込むたびに勢いを失う。1発の吹き飛びで無限に薙ぎ払えないようにする。
	const float damp = std::pow(T().corpseSlam.damp, static_cast<float>(std::max(_count, 1)));
	m_knockbackVelocity *= damp;

	m_corpseSlamCooldown = T().corpseSlam.interval;
}

//============================================================
// 砕けに巻き込まれる
//============================================================
void EnemyBase::BlastByShatter(const Math::Vector3& _origin, float _damage, int _depth)
{
	if (m_isDead || !m_isActive || m_isLeaving) return;

	// 硬化中なら砕きの連鎖へ
	if (IsFrozen())
	{
		ChainShatter(_origin, _damage, _depth);
		return;
	}

	// 硬化していない敵はただ巻き込まれる
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 dir = tc.GetPosition() - _origin;
	dir.y = 0.0f;
	if (dir.LengthSquared() <= 0.0f) dir = -GetFacing();
	dir.Normalize();

	SlamByCorpse(_origin, dir * T().shatter.blastSpeed, _damage);
}

void EnemyBase::SlamByCorpse(const Math::Vector3& _origin, const Math::Vector3& _velocity, float _damage)
{
	if (m_isDead || !m_isActive || m_isLeaving) return;

	// 硬化中に轢かれたら砕きの経路へ流す。連鎖と同じ絵になる。
	if (IsFrozen())
	{
		ChainShatter(_origin, _damage);
		return;
	}

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 死体の進行方向ではなく「死体から自分へ」の向きに飛ばす。
	// 正面衝突でも横に弾かれるので、群れが割れて見える。
	Math::Vector3 dir = tc.GetPosition() - _origin;
	dir.y = 0.0f;
	if (dir.LengthSquared() <= 0.0f)
	{
		dir = _velocity;
		dir.y = 0.0f;
	}
	if (dir.LengthSquared() > 0.0f) dir.Normalize();

	const float power = _velocity.Length();
	Math::Vector3 knock = dir * power * T().corpseSlam.transfer;

	auto& hc = m_cm.get<Game::Health>(m_entity);
	const float before = hc.hp;
	hc.hp -= _damage;

	const bool lethal = (before > 0.0f && hc.hp <= 0.0f);

	// ---- 演出通知 ----
	// プレイヤーが作った状況なので、手柄はプレイヤーに寄せる。
	// こうしておくと EnemyManager のとどめ演出もそのまま乗る。
	CombatFx::HitEvent ev;
	ev.position = tc.GetPosition() + Math::Vector3(0.0f, m_eyeHeight, 0.0f);
	ev.knockback = knock;
	ev.damage = _damage;
	ev.tag = PlayerAttackTag::kHeavy;
	ev.attackId = 0;
	ev.hitstopFrames = T().corpseSlam.hitstopFrames;
	ev.lethal = lethal;
	ev.attackerType = Collision::ObjectType::PlayerAttack;
	CombatFx::Instance().Notify(ev);

	if (lethal)
	{
		knock.y = power * T().corpseSlam.lethalLift;
		Die(knock, _damage - before);
		return;
	}

	// 体幹を無視して確実に転ばせる。
	// ここで耐えられると「大量に巻き込んだのに何も起きない」絵になる。
	m_poise = 0.0f;
	m_breakFlash = 1.0f;

	m_knockbackVelocity = knock;
	m_jumpMoment = power * T().corpseSlam.lift;
	m_gravity = 0.0f;
	m_gravityScale = T().corpseSlam.gravityScale;
	m_isGrounded = false;
	m_isKnockback = true;
	m_knockbackTimer = T().corpseSlam.knockbackSec;

	if (auto sp = m_wpWorld.lock())
	{
		if (auto* col = sp->GetCollision(m_handle))
		{
			col->moveConfig.stepHeight = 0.0f;
			col->moveConfig.snapDistance = 0.0f;
		}
	}

	ReleaseAttackToken();
	RequestReaction(Reaction::kBlow, T().corpseSlam.reactionSec);
}

void EnemyBase::ChainShatter(const Math::Vector3& _origin, float _damage, int _depth)
{
	if (m_isDead || !IsFrozen()) return;

	m_shatterOrigin = _origin;
	m_chainDepth = _depth + 1;
	m_shatterFromChain = true;

	auto& hc = m_cm.get<Game::Health>(m_entity);
	const float before = hc.hp;
	hc.hp -= _damage;

	if (before > 0.0f && hc.hp <= 0.0f)
	{
		Math::Vector3 dir = GetPosition() - _origin;
		dir.y = 0.0f;
		Die(dir * T().shatter.chainKillPush + Math::Vector3(0.0f, T().shatter.chainKillUp, 0.0f), 0.0f);
		return;
	}

	m_shatterTransfer = (m_chainDepth < T().shatter.maxChainDepth) ? (_damage * T().shatter.chainFalloff) : 0.0f;

	SetFlag(EnemyFlag::kShatterRequested);
}

float EnemyBase::GetCorpseProgress() const
{
	if (T().death.corpseStaySec <= 0.0f) return 1.0f;
	return std::clamp(1.0f - (m_corpseTimer / T().death.corpseStaySec), 0.0f, 1.0f);
}

void EnemyBase::ApplyCorpsePose()
{
	CaptureBaseScale();

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	const float p = std::clamp(m_corpseTimer / T().death.corpseStaySec, 0.0f, 1.0f);
	const float squash = T().pose.hitSquash * p * p;

	Math::Vector3 scale = m_baseScale;
	scale.y *= (1.0f - squash);
	scale.x *= (1.0f + squash * T().pose.corpseWiden);
	scale.z *= (1.0f + squash * T().pose.corpseWiden);

	if (m_corpseTimer <= T().death.corpseSinkSec && T().death.corpseSinkSec > 0.0f)
	{
		const float sink = 1.0f - std::clamp(m_corpseTimer / T().death.corpseSinkSec, 0.0f, 1.0f);
		Math::Vector3 pos = tc.GetPosition();
		pos.y = m_deathPosition.y - sink * T().pose.corpseSinkDepth;
		tc.SetPosition(pos);

		scale *= (1.0f - sink * T().pose.corpseSinkShrink);
	}

	tc.SetScale(scale);
}

void EnemyBase::ApplyStrikePose(float _t01)
{
	CaptureBaseScale();

	const float t = std::clamp(_t01, 0.0f, 1.0f);

	const float s = T().pose.strikeStretch * (1.0f - Damp::EaseOut(t));

	Math::Vector3 scale = m_baseScale;
	scale.z *= (1.0f + s);
	scale.x *= (1.0f - s * T().pose.strikeNarrowX);
	scale.y *= (1.0f - s * T().pose.strikeNarrowY);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(scale);
}

// ===========================================================
// 補助
// ===========================================================
void EnemyBase::DrawDebugImGui()
{
	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	if (auto* sys = SM_MGR.Get(sm)) sys->DrawImGui(sm, m_entity.index);
}

float EnemyBase::GetHealthRatio()
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return std::clamp(hc.hp / m_maxHp, 0.0f, 1.0f);
}

float EnemyBase::GetHealthValue() const
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return hc.hp;
}

float EnemyBase::GetHealthMax() const
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return hc.maxHp;
}

bool EnemyBase::ConsumeDeathCount()
{
	if (!m_isDead) return false;
	if (HasFlag(EnemyFlag::kDeathCounted)) return false;

	SetFlag(EnemyFlag::kDeathCounted);
	return true;
}