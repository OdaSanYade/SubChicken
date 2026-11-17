#include "Player.h"
#include "../../Camera/CameraBase.h"
#include "../../../GameSystem/StateMachine/StateMachineManager.h"
#include "State/PlayerStateSetup.h"
#include "../../../GameSystem/Combat/CombatFx.h"
#include "../../../GameSystem/Combat/ComboTracker.h"
#include "../../../GameSystem/Combat/BreathGauge.h"

#include "../../../Inc/Math/Damp.h"

using namespace StateLiterals;

namespace
{
	// これより短い距離・ベクトルは「向きが無い」とみなす(0 割り防止)
	constexpr float kTinyLength = 0.0001f;

	// 時間で割る処理の下限
	constexpr float kMinDuration = 0.01f;
}

Player::~Player()
{
	if (m_canCryEffectID != -1)
	{
		GPUParticleSystem::Instance().StopEffect(m_canCryEffectID);
		m_canCryEffectID = -1;
	}
}

// ===========================================================
// 初期化
// ===========================================================
void Player::Init()
{
	Tuning::Load(m_tuning, PlayerTuningKey::kFile);
	const PlayerTuning& t = m_tuning;

	// ---- モデル ----
	auto model = RES_MGR.GetModelList()->GetModel(t.modelName);
	const ModelId modelId = RES_MGR.GetModelList()->GetModelId(t.modelName);
	const WorkId wid = RES_MGR.GetWorkPool()->Create(model, modelId);

	m_cm.add<RenderComponent>(m_entity,
		{ RenderKind::Work, wid,
		RES_MGR.GetWorkPool()->GetGeneration(wid) });

	auto& gc = m_cm.add<GhostTrailComponent>(m_entity);
	gc.mode = GhostMode::XRay;

	m_cm.add<TrailComponent>(m_entity);

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(t.bodyScale);
	// ---- ワールド縮尺を反映 ----
	GameScale::ApplyToTransform(tc);
	tc.SetRotationEuler(t.spawnRotationDeg);
	tc.SetPosition(t.spawnPosition);

	// ---- コリジョン ----
	if (auto sp = m_wpWorld.lock())
	{
		//通常判定
		Collision::CollisionComponent init;
		init.shape = Collision::Shape::MakeCapsule(t.capsuleRadius, t.capsuleHeight, Collision::CapsulePivot::Bottom);
		init.type = Collision::ObjectType::Player;
		init.blocksCapsule = true;
		init.inverseMass = 0.0f;	// 0 = 押されない(敵やひよこに押し負けない)
		m_handle = sp->CreateCollision(init);

		if (auto* col = sp->GetCollision(m_handle))
		{
			GameScale::ApplyToMoveConfig(col->moveConfig, m_tuning.stepHeight, m_tuning.stepSnap);
		}

		//被弾判定
		Collision::CombatComponent initCombat;
		initCombat.team = Collision::Team::Player;
		initCombat.hittable = true;
		m_combatHandle = sp->CreateCombat(initCombat);
	}


	m_isGrounded = false;

	auto& rc = m_cm.get<RenderComponent>(m_entity);
	SetMaterial(rc, t.materialName.c_str());

	auto& lc = m_cm.add<LightComponent>(m_entity);
	lc.enabled = false;

	//ポストエフェクト発動権
	m_cm.add<PostEffectComponent>(m_entity);

	// 割合表示のために初期値を控えておく
	m_maxHp = std::max(m_cm.get<Game::Health>(m_entity).hp, 1.0f);
	m_hpDisplay = m_maxHp;

	// 他すべての初期化後にステートマシンを初期化
	SetupPlayerStates();

	auto* sys = SM_MGR.Get("Player");
	if (!sys)
	{
		assert(false && "Player: ステート定義が作れていません");
		return;
	}

	// アニメーション名 → 実データ の解決関数
	std::weak_ptr<IObject> weak = weak_from_this();
	sys->SetAnimResolver([weak](const char* name) -> std::shared_ptr<KdAnimationData>
		{
			auto self = weak.lock();
			if (!self) return nullptr;
			auto* work = static_cast<Player&>(*self).GetWork();
			return work ? work->GetAnimation(name) : nullptr;
		});

	m_cm.add<StateMachineComponent>(m_entity, SM_MGR.MakeComponent("Player"));

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	sys->Update(sm, 0.0f, this, m_entity.index);
	auto resolver = [this](const char* name) -> std::shared_ptr<KdAnimationData>
		{
			auto* work = GetWork();
			return work ? work->GetAnimation(name) : nullptr;
		};
	sys->SyncAnimation(sm, m_chickenAnim, resolver);

	SetKeyAction();
}

// ===========================================================
// 入力
// ===========================================================
void Player::SetKeyAction()
{
	auto& input = KdInputActionSystem::Instance();

	// 入力アクション名 → 立てるフラグ
	struct Binding { const char* action; uint16_t flag; };
	static constexpr Binding kBindings[] = {
		{ "Cry",         TestEntityFlag::kCry },
		{ "Dodge",       TestEntityFlag::kDodgeRequested },
		{ "Attack",      TestEntityFlag::kAttackRequested },
		{ "HeavyAttack", TestEntityFlag::kHeavyRequested },
	};

	for (const auto& b : kBindings)
	{
		const uint16_t flag = b.flag;
		input.AddListener(b.action, KdInputEventType::Press,
			MakeSafeCallback<Player>([flag](Player& self) { self.SetFlag(flag); }));
	}
}

// ===========================================================
// 更新
// ===========================================================
void Player::Update()
{
	const float deltaTime = static_cast<float>(Time::Instance().GetDeltaTime());

	//移動しているか
	if (GetMoveInput().LengthSquared() > 0.0f) 
	{
		SetFlag(TestEntityFlag::kDashing);
	}
	else 
	{
		ClearFlag(TestEntityFlag::kDashing);
	}

	UpdateTimers(deltaTime);

	//Hpの白ゲージ表示はダメージを受けたときにゆっくり減らす
	{
		const float ratio = GetHealthRatio();
		const float lag = (m_damageShock > m_tuning.hpLagHoldShock) ? 0.0f : m_tuning.hpLagLambda;
		m_hpDisplay = Damp::Value(m_hpDisplay, ratio * m_maxHp, lag, deltaTime);
	}

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	auto* sys = SM_MGR.Get(sm);
	if (!sys) return;

	// ステート更新
	sys->Update(sm, deltaTime, this, m_entity.index);

	// 遷移していればアニメーションを差し替える
	auto resolver = [this](const char* name) -> std::shared_ptr<KdAnimationData>
		{
			auto* work = GetWork();
			return work ? work->GetAnimation(name) : nullptr;
		};
	sys->SyncAnimation(sm, m_chickenAnim, resolver);

	// 移動量をコリジョンへ渡す
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	if (auto sp = m_wpWorld.lock())
	{
		//通常判定
		if (auto* col = sp->GetCollision(m_handle))
		{
			col->center = tc.GetPosition();
			col->moveVector = m_move;
			m_dir = m_move;

			//体当たり判定提出
			if (auto* combat = sp->GetCombat(m_combatHandle))
			{
				const Math::Vector3 mid = col->GetCapsuleMid();
				combat->hurtVolume.p0 = mid - Math::Vector3::Up * col->shape.segHalf;
				combat->hurtVolume.p1 = mid + Math::Vector3::Up * col->shape.segHalf;
				combat->hurtVolume.radius = col->shape.radius;
			}
		}
	}

	// アニメーションを進める + イベントをコンポーネントへ移送
	if (auto* work = GetWork())
	{
		sys->AdvanceAnimation(sm, m_chickenAnim, work->WorkNodes());
	}
}

void Player::UpdateTimers(float deltaTime)
{
	if (m_dodgeCooldown > 0.0f) m_dodgeCooldown -= deltaTime;
	if (m_jumpLockTime > 0.0f)  m_jumpLockTime -= deltaTime;

	// 回避猶予は回避モーションと歩調を合わせたいのでゲーム内時間で減らす
	if (m_invincibleTime > 0.0f) m_invincibleTime -= deltaTime;

	// 被弾後の無敵
	if (m_hitInvincibleTime > 0.0f) m_hitInvincibleTime -= deltaTime;

	// キャンセル猶予はステートを抜ければ自然に閉じる
	if (m_cancelWindow > 0.0f) m_cancelWindow -= deltaTime;

	// コヨーテ猶予もゲーム内時間
	if (m_coyoteTime > 0.0f) m_coyoteTime -= deltaTime;

	// カウンター受付は実時間
	if (m_counterWindow > 0.0f)
	{
		m_counterWindow -= CombatFx::Instance().RealDelta();
		if (m_counterWindow <= 0.0f) EndPerfectDodge();
	}

	// 鳴き声の波紋
	if (m_cryWaveTime > 0.0f)
	{
		m_cryWaveTime -= deltaTime;

		// 開き切った視野角をゆっくり戻す
		if (m_cryWasFull)
		{
			const float t = 1.0f - SafeRatio(m_cryWaveTime, m_tuning.cryWaveSec);
			SetCameraFovY(std::lerp(m_tuning.cryBurstFovY, m_tuning.normalFovY, t), m_tuning.cryRecoverFovLambda);
		}

		if (m_cryWaveTime <= 0.0f)
		{
			if (m_cryChargePushed) { PopCameraFocus(); m_cryChargePushed = false; }
			m_cryFovPushed = false;
		}
	}

	// UIの揺れ。ダメージの強さに応じて揺れる。
	if (m_damageShock > 0.0f)
		m_damageShock = std::max(0.0f, m_damageShock - CombatFx::Instance().RealDelta() / std::max(m_tuning.damageShockSec, kMinDuration));
}

void Player::PostUpdate()
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	auto sp = m_wpWorld.lock();
	if (!sp) return;

	auto* col = sp->GetCollision(m_handle);
	auto* combat = sp->GetCombat(m_combatHandle);
	if (!col || !combat) return;	// コリジョンが作れていない(ワールドが無かった等)

	tc.SetPosition(col->result.position);

	// 落下速度を覚えておく
	if (!m_isGrounded) m_fallSpeed = std::max(m_fallSpeed, m_gravity);

	//被弾判定回収
	for (auto& hit : combat->received)
	{
		// ジャスト回避
		if (m_invincibleTime > 0.0f)
		{
			TriggerPerfectDodge(hit.knockback);
			continue;
		}

		// 被弾後の無敵
		if (m_hitInvincibleTime > 0.0f) continue;

		m_hitInvincibleTime = m_tuning.hitInvincibleTime;
		ComboTracker::Instance().Break();

		const float extraShock = std::min(hit.damage / std::max(m_tuning.damageShockPerDamage, 1.0f), m_tuning.damageShockExtraMax);
		m_damageShock = std::min(1.0f, m_damageShock + m_tuning.damageShockBase + extraShock);

		// よろけ
		m_hurtLevel = (hit.damage >= m_tuning.hurtHeavyDamage) ? 1 : 0;

		m_hurtVelocity = { hit.knockback.x, 0.0f, hit.knockback.z };

		// 踏み込みや回避の途中でも必ず中断させる。
		m_lungeActive = false;
		ClearComboQueue();
		LockCancel();

		SetFlag(TestEntityFlag::kHurt);

		//Hp減少
		auto& hc = m_cm.get<Game::Health>(m_entity);
		hc.hp -= hit.damage;
		if (hc.hp <= 0.0f)
		{
			SetFlag(TestEntityFlag::kDead);
		}

		// 演出通知
		CombatFx::HitEvent ev;
		ev.position = tc.GetPosition() + Math::Vector3(0.0f, m_tuning.hitFxHeight, 0.0f);
		ev.knockback = hit.knockback;
		ev.tag = hit.tag;
		ev.attackId = hit.attackId;
		ev.damage = hit.damage;
		ev.hitstopFrames = hit.hitstopFrames;
		ev.attackerType = hit.attackerType;
		CombatFx::Instance().Notify(ev);

		// 被弾は与ダメージより強く止める
		CombatFx::Instance().RequestHitStop(m_tuning.damagedStopSec, m_tuning.damagedStopFade);
		CombatFx::Instance().AddTrauma(m_tuning.damagedTrauma * (1.0f + m_hurtLevel * m_tuning.damagedTraumaPerLevel));
		const Math::Vector3& vib = m_tuning.damagedVibration;
		CombatFx::Instance().AddVibration(vib.x, vib.y, vib.z);
	}

	// 自分の攻撃が当たったかを拾う
	for (const auto& ev : CombatFx::Instance().Events())
	{
		if (ev.attackerType != Collision::ObjectType::PlayerAttack) continue;
		if (ev.attackId != m_currentAttackId) continue;

		m_attackLanded = true;
		if (ev.tag == PlayerAttackTag::kCounter) SetFlag(TestEntityFlag::kCounterHit);
	}
}

// ===========================================================
// 移動
// ===========================================================
Math::Vector2 Player::GetMoveInput() const
{
	return KdInputActionSystem::Instance().GetAxis("Move");
}

Math::Vector3 Player::GetCameraForwardFlat() const
{
	Math::Vector3 forward = Math::Vector3::Forward;

	if (auto sp = m_wpCamera.lock())
		forward = sp->GetCamera()->GetCameraMatrix().Backward();

	forward.y = 0.0f;
	if (forward.LengthSquared() > 0.0f) forward.Normalize();
	return forward;
}

Math::Vector3 Player::MakeMoveDir(const Math::Vector2& input) 
{
	Math::Vector2 move = input;
	if (move.LengthSquared() > 0.0f) move.Normalize();

	Math::Vector3 forward = GetCameraForwardFlat();
	Math::Vector3 right = Math::Vector3::Up.Cross(forward);
	if (right.LengthSquared() > 0.0f) right.Normalize();

	Math::Vector3 moveDir = right * move.x + forward * move.y;
	if (moveDir.LengthSquared() > 0.0f)
	{
		moveDir.Normalize();
		// 攻撃時の対象選択に使う。スティックを倒している方向の敵を優先させる。
		m_lastAimDir = moveDir;
	}
	return moveDir;
}
void Player::ApplyGroundedMotion(float deltaTime, float speed)
{
	const Math::Vector3 moveDir = MakeMoveDir(GetMoveInput());
	SetStepClimb(true);

	// 目標速度へ寄せる
	const float lambda = (speed > m_currentSpeed) ? m_tuning.speedLambda : m_tuning.stopLambda;
	m_currentSpeed = Damp::Value(m_currentSpeed, speed, lambda, deltaTime);

	// ダッシュに入った瞬間だけ前へ押し出す。
	// 補間だけだと「速くなった」瞬間が分からないので、頭に段差を作る。
	const bool dashing = IsDashing();
	if (dashing && !m_prevDashing && moveDir.LengthSquared() > 0.0f)
		m_currentSpeed += m_tuning.dashKickSpeed;
	m_prevDashing = dashing;

	// 接地中も地面に張り付かせるための最小限の下向きの力
	const float stickForce = std::max(m_tuning.groundStickMin, m_currentSpeed * m_tuning.groundStickSlope);
	const Math::Vector3 upMoment = -stickForce * Math::Vector3::Up;

	// 入力を離した後も、残った速度のぶんだけ滑って止まる
	const Math::Vector3 dir = (moveDir.LengthSquared() > 0.0f) ? moveDir : m_lastMoveDir;

	m_move = (dir * m_currentSpeed + upMoment) * deltaTime;

	ApplyRotation(moveDir, deltaTime);
	ApplyLandingPose();	// 着地の潰しを戻す
}

void Player::ApplyDodgeMotion(float deltaTime)
{
	SetStepClimb(true);
	m_move = m_dodgeDir * m_dodgeSpeed * deltaTime;
}

void Player::SetStepClimb(bool _enable)
{
	if (m_stepClimbEnabled == _enable) return;
	m_stepClimbEnabled = _enable;

	auto sp = m_wpWorld.lock();
	if (!sp) return;

	auto* col = sp->GetCollision(m_handle);
	if (!col) return;

	if (_enable)
	{
		GameScale::ApplyToMoveConfig(col->moveConfig, m_tuning.stepHeight, m_tuning.stepSnap);
		return;
	}

	col->moveConfig.stepHeight = 0.0f;
	col->moveConfig.snapDistance = 0.0f;
}
void Player::ApplyRotation(const Math::Vector3& moveDir, float deltaTime)
{
	if (moveDir.LengthSquared() <= 0.0f) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// モデルが -Z 向きに作られているので反転して角度を出す
	const float targetAngle = atan2f(-moveDir.x, -moveDir.z);
	const Math::Quaternion targetRot =
		Math::Quaternion::CreateFromAxisAngle(Math::Vector3::Up, targetAngle);

	// 今の向きと目標の開きが大きいほど速く回す
	float lambda = m_tuning.rotSpeed;
	if (m_lastMoveDir.LengthSquared() > 0.0f)
	{
		if (m_lastMoveDir.Dot(moveDir) < m_tuning.turnDotThresh) lambda = m_tuning.rotSpeedTurn;
	}
	m_lastMoveDir = moveDir;

	tc.SetRotation(Damp::Rotation(tc.GetRotation(), targetRot, lambda, deltaTime));
}

void Player::FaceInstant(const Math::Vector3& _dir)
{
	if (_dir.LengthSquared() <= 0.0f) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const float angle = atan2f(-_dir.x, -_dir.z);
	tc.SetRotation(Math::Quaternion::CreateFromAxisAngle(Math::Vector3::Up, angle));
}

// ===========================================================
// 鳴き声
// ===========================================================
bool Player::CanCryShort() const { return BreathGauge::Instance().CanShort(); }
bool Player::CanCryFull()  const { return BreathGauge::Instance().CanFull(); }

void Player::ApplyCryCharge(float _t01)
{
	const float t = std::clamp(_t01, 0.0f, 1.0f);

	// 視野角を段階的に絞る。息を吸い込んでいる間、世界が狭くなっていく。
	const PlayerTuning& tune = m_tuning;
	if (!m_cryChargePushed)
	{
		PushCameraFocus(GetCameraFovY(), tune.cryChargePushLambda);
		m_cryChargePushed = true;
	}
	// 押した後は目標値だけを毎フレーム更新する(Push/Popは1回ずつ)
	const float fov = std::lerp(tune.normalFovY, tune.cryChargeFovY, t * t);
	SetCameraFovY(fov, tune.cryChargeFovLambdaMin + t * tune.cryChargeFovLambdaAdd);

	// 溜め切りに向けて振動を強めていく。手に「来る」感じを作る。
	// 開始点から 1 までを 0〜1 に引き伸ばして使う
	if (t > tune.cryVibrationStart && tune.cryVibrationStart < 1.0f)
	{
		const float p = (t - tune.cryVibrationStart) / (1.0f - tune.cryVibrationStart);
		CombatFx::Instance().AddVibration(p * tune.cryVibrationMax, p * tune.cryVibrationMax, tune.cryVibrationSec);
	}

	// 溜め切り直前は画面も揺らす
	if (t > tune.cryTraumaStart) CombatFx::Instance().AddTrauma((t - tune.cryTraumaStart) * tune.cryTraumaScale);
}

void Player::EmitCry(bool _full)
{
	const float cost = _full ? BreathGauge::kFullCost : BreathGauge::kShortCost;
	if (!BreathGauge::Instance().Consume(cost)) return;

	m_cryWasFull = _full;
	m_cryWaveTime = m_tuning.cryWaveSec;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	if (m_freezeFn)
	{
		// チョイ鳴きは目の前の1体だけ。全力鳴きは範囲内の全員。
		m_freezeFn(tc.GetPosition(),
			_full ? m_tuning.cryFullRadius : m_tuning.cryShortRadius,
			_full ? m_tuning.cryFullFreeze : m_tuning.cryShortFreeze,
			!_full);
	}

	CombatFx::Instance().RequestCryFx(_full);

	if (_full)
	{
		// ---- 解放 ----
		// 絞りきった視野角を一気に開く。溜めとの落差で衝撃を作る。
		SetCameraFovY(m_tuning.cryBurstFovY, m_tuning.cryBurstFovLambda);

		// 硬化中は画面全体が冷える
		m_cryFovPushed = true;
	}
}

// ===========================================================
// アクション
// ===========================================================
void Player::BeginDodge(float speed)
{
	// 受付中に再度回避したらスローは畳む
	EndPerfectDodge();

	m_dodgeCooldown = m_tuning.dodgeCooldown;
	m_dodgeSpeed = speed;

	m_dodgeDir = MakeMoveDir(GetMoveInput());

	// 出だしの数フレームだけ無敵。この間に殴られるとジャスト回避成立。
	m_invincibleTime = m_tuning.dodgeIFrameTime;

	// 入力が無ければ正面(カメラ前方)へ回避
	if (m_dodgeDir.LengthSquared() <= 0.0f) m_dodgeDir = GetCameraForwardFlat();

	// 回避方向を即座に向く(Slerpを待たない)
	FaceInstant(m_dodgeDir);

	// 避けること自体を報酬にする。連打で稼げないようクールダウンで抑える。
	BreathGauge::Instance().OnDodge();
}

void Player::EndDodge()
{
	m_dodgeSpeed = 0.0f;
}

// ===========================================================
// ジャスト回避
// ===========================================================
void Player::TriggerPerfectDodge(const Math::Vector3& _knockback)
{
	// ノックバックの逆向き ≒ 攻撃してきた敵がいる方向
	Math::Vector3 from = -_knockback;
	from.y = 0.0f;
	m_hasCounterDir = false;
	if (from.LengthSquared() > 0.0f)
	{
		from.Normalize();
		m_counterDir = from;
		m_hasCounterDir = true;
	}

	m_counterWindow = m_tuning.counterWindow;

	//全体的な色を変える
	auto& pe = m_cm.get<PostEffectComponent>(m_entity);
	pe.radialBlur.enabled = true;
	pe.radialBlur.easeDuration = m_tuning.parryBlurEase;
	pe.radialBlur.strength = m_tuning.parryBlurStrength;

	if (!m_slowActive)
	{
		m_slowActive = true;
		CombatFx::Instance().SetSlow(m_tuning.slowScale);
	}

	// 成立した瞬間だけ一瞬止める。スローへ入る前に「弾いた」感触を作る。
	CombatFx::Instance().RequestHitStop(m_tuning.parryStopSec);
	CombatFx::Instance().AddTrauma(m_tuning.parryTrauma);

	AudioManager::Instance().Play("just");

	// 視野角を狭めて「集中している」見え方にする
	if (!m_dodgeFovPushed)
	{
		PushCameraFocus(m_tuning.focusFovY, m_tuning.focusFovLambda);
		m_dodgeFovPushed = true;
	}

	// 直前に溜まっていた攻撃入力で即カウンターが暴発しないよう捨てる
	ClearFlag(TestEntityFlag::kAttackRequested);

	// ジャスト回避は大きく回復する。読み切った報酬として最大級に扱う。
	BreathGauge::Instance().OnPerfect();
}

void Player::EndPerfectDodge()
{
	m_counterWindow = 0.0f;

	auto& pe = m_cm.get<PostEffectComponent>(m_entity);
	pe.AllStopEnabled();

	if (m_slowActive)
	{
		m_slowActive = false;
		CombatFx::Instance().ClearSlow();
	}

	if (m_dodgeFovPushed)
	{
		PopCameraFocus();
		m_dodgeFovPushed = false;
	}
}

// ===========================================================
// 踏み込み(瞬間接近)
// ===========================================================
void Player::BeginLunge(const Math::Vector3& _targetPos, float _keepDistance,
	float _maxDistance, float _duration)
{
	m_lungeActive = false;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 to = _targetPos - tc.GetPosition();
	to.y = 0.0f;

	const float dist = to.Length();
	if (dist <= kTinyLength) return;
	to /= dist;

	// 相手の方は必ず向く(踏み込まない場合でも)
	FaceInstant(to);

	// 相手の手前で止まる。遠すぎる相手には踏み込まない。
	float keep = _keepDistance;
	if (auto sp = m_wpWorld.lock())
	{
		if (auto* col = sp->GetCollision(m_handle))
			keep = std::max(keep, col->shape.radius + m_tuning.lungeBodyMargin);
	}

	float travel = std::min(dist - keep, _maxDistance);
	if (travel <= 0.0f) return;

	// 速度上限で押さえる。短時間で長距離を詰めると壁を抜ける。
	// 0 秒にはしない(ApplyStepMotion で割り算するため)
	const float duration = std::max({ _duration, travel / std::max(m_tuning.lungeMaxSpeed, kTinyLength), kMinDuration });

	m_lungeDir = to;
	m_lungeTotal = travel;
	m_lungeDuration = duration;
	m_lungeElapsed = 0.0f;
	m_lungeMoved = 0.0f;
	m_lungeActive = true;
	SetStepClimb(false);
}

void Player::ApplyStepMotion(float _dt)
{
	const Math::Vector3 down = -m_tuning.groundStickMin * Math::Vector3::Up * _dt;
	

	if (!m_lungeActive)
	{
		// 踏み込みが終わったら段差登りを戻す
		SetStepClimb(true);
		m_move = down;
		return;
	}

	m_lungeElapsed += _dt;

	const float t01 = std::clamp(m_lungeElapsed / m_lungeDuration, 0.0f, 1.0f);

	// ease-out。等速だと重さが出ない。出だしを鋭く、着地で減速させる。
	const float progress = Damp::EaseOut(t01) * m_lungeTotal;
	const float step = progress - m_lungeMoved;
	m_lungeMoved = progress;

	if (t01 >= 1.0f) m_lungeActive = false;

	// m_move は「このフレームの移動量」なので dt は掛けない
	m_move = m_lungeDir * step + down;
}

// ===========================================================
// 攻撃の向き
// ===========================================================
Math::Vector3 Player::GetAttackDir() const
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 dir = -tc.GetForward();
	dir.y = 0.0f;
	if (dir.LengthSquared() <= kTinyLength) dir = GetCameraForwardFlat();
	dir.Normalize();
	return dir;
}

// ===========================================================
// 対象探索
// ===========================================================
bool Player::GetSoftLockPosition(Math::Vector3& _out)
{
	auto sp = m_wpCamera.lock();
	if (!sp) return false;
	return sp->GetSoftLockTargetPosition(_out);
}

bool Player::FindNearestEnemy(float _maxDist, Math::Vector3& _out)
{
	if (!m_enemyPositionProvider) return false;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const Math::Vector3 self = tc.GetPosition();

	// スティックを倒していればその方向、倒していなければカメラ前方を狙いとみなす。
	Math::Vector3 aim = MakeMoveDir(GetMoveInput());
	if (aim.LengthSquared() <= 0.0f) aim = m_lastAimDir;
	if (aim.LengthSquared() <= 0.0f) aim = GetCameraForwardFlat();

	float bestScore = FLT_MAX;
	bool found = false;

	for (const auto& pos : m_enemyPositionProvider())
	{
		Math::Vector3 d = pos - self;
		d.y = 0.0f;

		const float len = d.Length();
		if (len > _maxDist || len <= kTinyLength) continue;

		const Math::Vector3 n = d / len;
		const float dot = n.Dot(aim);

		// 狙っている方向にいる敵は、実距離より近いものとして評価する。
		// 「押した方向の敵を殴れる」は想像以上に効く。
		float score = len;
		if (dot >= m_tuning.aimConeDot) score *= (1.0f - m_tuning.aimPriorityBonus * dot);

		if (score < bestScore)
		{
			bestScore = score;
			_out = pos;
			found = true;
		}
	}
	return found;
}

bool Player::FindEnemyAlongDir(const Math::Vector3& _dir, float _maxDist, Math::Vector3& _out)
{
	if (!m_enemyPositionProvider) return false;
	if (_dir.LengthSquared() <= 0.0f) return false;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const Math::Vector3 self = tc.GetPosition();

	// 前後60°以内(調整値)。これを外れた敵は「殴ってきた奴」とは見なさない。
	const float minDot = m_tuning.counterAimDot;

	float bestScore = FLT_MAX;
	bool found = false;

	for (const auto& pos : m_enemyPositionProvider())
	{
		Math::Vector3 d = pos - self;
		d.y = 0.0f;
		const float len = d.Length();
		if (len <= kTinyLength || len > _maxDist) continue;

		const Math::Vector3 n = d / len;
		const float dot = n.Dot(_dir);
		if (dot < minDot || dot <= 0.0f) continue;	// dot で割るので 0 以下は弾く

		// 方向が合っているほど「近い」とみなす
		const float score = len / dot;
		if (score < bestScore)
		{
			bestScore = score;
			_out = pos;
			found = true;
		}
	}
	return found;
}

// ===========================================================
// 攻撃
// ===========================================================
void Player::BeginAttackStep(bool _useSoftLock, float _keepDistance,
	float _maxDistance, float _duration)
{
	// attackId を進める。1回の攻撃で1度しか当たらなくなる。
	StartAttack();
	m_lungeActive = false;
	ClearComboQueue();
	m_hasPrevAttackOrigin = false;
	m_attackLanded = false;		// ヒット確認をこの一撃のぶんだけにする
	m_cancelWindow = 0.0f;		// 前の攻撃のキャンセル猶予を持ち越さない

	Math::Vector3 target;
	bool has = false;

	// 1段目は「今カメラが狙っている敵」を優先する
	if (_useSoftLock) has = GetSoftLockPosition(target);
	if (!has)         has = FindNearestEnemy(_maxDistance, target);

	if (!has)
	{
		// 相手がいなければカメラ前方を向いてその場で振る
		FaceInstant(GetCameraForwardFlat());
		return;
	}

	BeginLunge(target, _keepDistance, _maxDistance, _duration);
}

float Player::GetHurtDuration() const
{
	return (m_hurtLevel >= 1) ? m_tuning.hurtHeavySec : m_tuning.hurtLightSec;
}

void Player::ApplyHurtMotion(float _dt)
{
	// 押し出されながら減衰する。
	// 完全に止めるとその場で固まって、当たった向きが伝わらない。
	const float k = Damp::Factor(m_tuning.hurtDecay, _dt);
	m_hurtVelocity *= k;

	Math::Vector3 move = m_hurtVelocity * _dt;
	move.y = -m_tuning.hurtGroundStick * _dt;

	m_move = move;
}

void Player::ApplyHurtPose(float _t01)
{
	CaptureBaseScale();

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	// 当たった瞬間に潰れ、時間をかけて戻る。
	// 姿勢が変わらないと、止まっているだけに見えて被弾と分からない。
	const float t = std::clamp(_t01, 0.0f, 1.0f);
	const float squashMax = (m_hurtLevel >= 1) ? m_tuning.hurtSquashHeavy : m_tuning.hurtSquashLight;
	const float squash = (1.0f - t) * (1.0f - t) * squashMax;

	tc.SetScale(m_baseScale * Math::Vector3(1.0f + squash, 1.0f - squash, 1.0f + squash));
}

void Player::BeginCounterStep(float _keepDistance, float _maxDistance, float _duration)
{
	// カウンターが出た時点でスローと視野角は戻す
	EndPerfectDodge();

	StartAttack();
	m_lungeActive = false;
	ClearComboQueue();
	m_hasPrevAttackOrigin = false;
	m_attackLanded = false;		// ヒット確認をこの一撃のぶんだけにする
	m_cancelWindow = 0.0f;		// 前の攻撃のキャンセル猶予を持ち越さない

	Math::Vector3 target;
	bool has = false;

	// 殴ってきた方向の敵を優先。見つからなければ最寄り。
	if (m_hasCounterDir) has = FindEnemyAlongDir(m_counterDir, _maxDistance, target);
	if (!has)            has = FindNearestEnemy(_maxDistance, target);

	if (!has)
	{
		FaceInstant(m_hasCounterDir ? m_counterDir : GetCameraForwardFlat());
		return;
	}

	BeginLunge(target, _keepDistance, _maxDistance, _duration);
}

bool Player::SnapToNearMiss(float _reach, float _radius, Math::Vector3& _outTarget)
{
	if (!m_enemyPositionProvider) return false;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const Math::Vector3 self = tc.GetPosition();
	const Math::Vector3 facing = -tc.GetForward();

	// 判定が届く距離 + 余裕。この帯にいる相手だけを引き寄せる。
	const float inner = _reach + _radius;
	const float outer = inner + m_tuning.snapExtraRange;

	float bestDist = FLT_MAX;
	bool found = false;

	for (const auto& pos : m_enemyPositionProvider())
	{
		Math::Vector3 d = pos - self;
		d.y = 0.0f;

		const float len = d.Length();
		if (len <= inner || len > outer) continue;	// 既に届く/遠すぎる

		// 正面にいる相手だけ。真横や背後を引き寄せると気持ち悪い。
		const Math::Vector3 n = d / len;
		if (n.Dot(facing) < m_tuning.snapFacingDot) continue;

		if (len < bestDist)
		{
			bestDist = len;
			_outTarget = pos;
			found = true;
		}
	}
	return found;
}

void Player::BeginAttackTrail(const AttackSwing& _swing)
{
	m_swing = _swing;
	m_swingPhase = 0.0f;

	if (!m_cm.has<TrailComponent>(m_entity)) return;

	auto& t = m_cm.get<TrailComponent>(m_entity);

	t.source = static_cast<int>(TrailSource::External);
	t.widthMode = static_cast<int>(TrailWidthMode::RootTip);
	t.lifeTime = _swing.lifeTime;
	t.widthScale = _swing.widthScale;
	t.SetAsset(_swing.asset);

	t.ResetPoints();
}

void Player::SpawnAttackHitbox(float _damage, float _reach, float _radius,
	float _knockback, int _hitstopFrames, uint32_t _tag, float _launch)
{
	auto sp = m_wpWorld.lock();
	if (!sp)return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

	// 吸い付き
	if (!m_lungeActive)
	{
		Math::Vector3 snapTarget;
		if (SnapToNearMiss(_reach, _radius, snapTarget))
		{
			Math::Vector3 to = snapTarget - tc.GetPosition();
			to.y = 0.0f;
			const float len = to.Length();
			if (len > kTinyLength)
			{
				to /= len;
				// 1フレームで詰める量は小さく抑える。瞬間移動に見えないように。
				const float pull = std::min(len - (_reach + _radius), m_tuning.snapExtraRange)
					* dt * m_tuning.snapPullRate;
				if (pull > 0.0f) m_move += to * pull;
			}
		}
	}

	// 振り抜きを進める
	m_swingPhase = std::min(
		(m_swingPhase + dt) / std::max(m_swing.swingSec, kMinDuration), 1.0f);

	// 根元は体、先端は刃先。上から下へ降りてくる。
	const Math::Vector3 origin = tc.GetPosition() + Math::Vector3(0.0f, m_tuning.attackOriginHeight, 0.0f);
	const Math::Vector3 swingDir = GetAttackDir();
	const Math::Vector3 tip = origin + swingDir * _reach;

	// ノックバックは水平方向で出す。向きが取れないとき(真上/真下を向いている)は攻撃方向で代用する
	Math::Vector3 dir = -tc.GetForward();
	dir.y = 0.0f;
	if (dir.LengthSquared() <= kTinyLength) dir = swingDir;
	dir.Normalize();

	Collision::HitVolume vol;
	vol.p0 = m_hasPrevAttackOrigin ? m_prevAttackOrigin : origin;
	vol.p1 = tip;
	vol.radius = _radius;

	m_prevAttackOrigin = origin;
	m_hasPrevAttackOrigin = true;

	// 軌跡
	if (m_cm.has<TrailComponent>(m_entity))
	{
		// 振りの進み 0〜1 を -0.5〜+0.5 にして、正面を中心に左右へ振る
		constexpr float kSwingCenter = 0.5f;
		const float yaw = m_swing.yawDeg * (m_swingPhase - kSwingCenter);
		const Math::Vector3 trailDir = Math::Vector3::Transform(swingDir,
			Math::Matrix::CreateRotationY(DirectX::XMConvertToRadians(yaw)));

		const float y = m_swing.topY + (m_swing.bottomY - m_swing.topY) * m_swingPhase;
		const Math::Vector3 off = Math::Vector3::Up * y;

		const Math::Vector3 bladeRoot = origin + off + trailDir * (_reach * m_swing.inner);
		const Math::Vector3 bladeTip = origin + off + trailDir * (_reach * m_swing.outer);
		m_cm.get<TrailComponent>(m_entity).Emit(bladeRoot, bladeTip);
	}

	Collision::HitData data;
	data.damage = _damage;
	data.knockback = dir * _knockback * _launch
		+ Math::Vector3(0.0f, _knockback * _launch * m_tuning.knockbackLiftRatio, 0.0f);
	data.hitstopFrames = _hitstopFrames;
	data.attackId = m_currentAttackId;
	data.tag = _tag;
	data.attackerType = Collision::ObjectType::PlayerAttack;

	sp->SubmitHitbox(vol, data, Collision::Team::Player, m_combatHandle);
}

void Player::BeginHeavyStep(bool _preferFrozen, float _keepDistance,
	float _maxDistance, float _duration)
{
	BreathGauge::Instance().Drain(BreathGauge::kHeavyCost);
	StartAttack();
	m_lungeActive = false;
	ClearComboQueue();
	m_hasPrevAttackOrigin = false;
	m_attackLanded = false;
	m_cancelWindow = 0.0f;

	Math::Vector3 target;
	bool has = false;

	// 固めた相手を優先。必殺技からの繋ぎを成立させる。
	if (_preferFrozen && m_frozenPositionProvider)
	{
		auto& tc = m_cm.get<TransformComponent>(m_entity);
		const Math::Vector3 self = tc.GetPosition();

		float best = _maxDistance * _maxDistance;
		for (const auto& pos : m_frozenPositionProvider())
		{
			Math::Vector3 d = pos - self;
			d.y = 0.0f;
			const float sq = d.LengthSquared();
			if (sq < best) { best = sq; target = pos; has = true; }
		}
	}

	if (!has) has = FindNearestEnemy(_maxDistance, target);

	if (!has)
	{
		FaceInstant(GetCameraForwardFlat());
		return;
	}

	BeginLunge(target, _keepDistance, _maxDistance, _duration);
}

bool Player::GetAttackSegment(float _reach, Math::Vector3& _root, Math::Vector3& _tip) const
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const Math::Matrix world = tc.GetWorldMatrix();

	auto* work = GetWork();

	// アニメーションを進めた直後はノード行列が未計算のことがある
	if (work && work->NeedCalcNodeMatrices()) work->CalcNodeMatrices();

	if (work && !m_attackRootNode.empty())
	{
		if (const auto* rootNode = work->FindNode(m_attackRootNode))
		{
			// ノードの行列はモデル原点基準。エンティティのワールドを掛ける
			const Math::Matrix rootWorld = rootNode->m_worldTransform * world;
			_root = rootWorld.Translation();

			if (!m_attackTipNode.empty())
			{
				if (const auto* tipNode = work->FindNode(m_attackTipNode))
				{
					_tip = (tipNode->m_worldTransform * world).Translation();
					return true;
				}
			}

			// 先端のノードが無い場合。モデルのY軸を刃方向とみなす
			Math::Vector3 blade(rootWorld._21, rootWorld._22, rootWorld._23);
			blade.Normalize();
			_tip = _root + blade * m_tuning.attackBladeLength;
			return true;
		}
	}

	// フォールバック
	const Math::Vector3 origin = tc.GetPosition() + Math::Vector3(0.0f, m_tuning.attackOriginHeight, 0.0f);
	const Math::Vector3 dir = -tc.GetForward();

	_root = origin;
	_tip = origin + dir * _reach;
	return false;
}

void Player::SetAttackBones(const char* _rootNode, const char* _tipNode)
{
	m_attackRootNode = _rootNode ? _rootNode : "";
	m_attackTipNode = _tipNode ? _tipNode : "";
}

void Player::DumpNodeNames() const
{
	auto* work = GetWork();
	if (!work) return;

	OutputDebugStringA("---- node names ----\n");
	for (const auto& n : work->GetNodes())
	{
		OutputDebugStringA((n.m_name + "\n").c_str());
	}
}

// ===========================================================
// 着地
// ===========================================================
void Player::CaptureBaseScale()
{
	if (m_scaleCaptured) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	m_baseScale = tc.GetScale();
	m_scaleCaptured = true;
}

void Player::ApplyLandingPose()
{
	CaptureBaseScale();

	const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

	// 潰れは指数で戻す。十分小さくなったら 0 にして終わり
	constexpr float kSquashDone = 0.001f;
	m_landingSquash = Damp::Value(m_landingSquash, 0.0f, m_tuning.landRecoverLambda, dt);
	if (m_landingSquash < kSquashDone)
	{
		m_landingSquash = 0.0f;
		return;
	}

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	Math::Vector3 scale = m_baseScale;
	scale.y *= (1.0f - m_landingSquash);
	scale.x *= (1.0f + m_landingSquash * m_tuning.landSquashWiden);
	scale.z *= (1.0f + m_landingSquash * m_tuning.landSquashWiden);
	tc.SetScale(scale);
}

// ===========================================================
// カメラ
// ===========================================================
float Player::GetCameraFovY()
{
	auto sp = m_wpCamera.lock();
	return sp ? sp->GetFovY() : m_tuning.normalFovY;
}

void Player::SetCameraFovY(float _deg, float _lambda)
{
	if (auto sp = m_wpCamera.lock()) sp->SetTargetFovY(_deg, _lambda);
}

// ===========================================================
// カメラ演出
// ===========================================================
void Player::PushCameraFocus(float _fovDeg, float _lambda)
{
	// 一番外側で押したときの値だけを覚える
	if (m_fovDepth == 0) m_savedFovY = GetCameraFovY();
	++m_fovDepth;

	SetCameraFovY(_fovDeg, _lambda);
}

void Player::PopCameraFocus()
{
	if (m_fovDepth <= 0) return;

	--m_fovDepth;
	if (m_fovDepth == 0) SetCameraFovY(m_savedFovY, m_tuning.focusRestoreLambda);
}

void Player::AddCameraTrauma(float _amount)
{
	CombatFx::Instance().AddTrauma(_amount);
}

// ===========================================================
// 補助
// ===========================================================
KdModelWork* Player::GetWork() const
{
	if (!m_cm.has<RenderComponent>(m_entity)) return nullptr;
	auto& wp = m_cm.get<RenderComponent>(m_entity);
	return RES_MGR.GetWorkPool()->Get(wp.assetId, wp.generation);
}

float Player::GetHealthRatio()
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return std::clamp(hc.hp / m_maxHp, 0.0f, 1.0f);
}

float Player::GetHealthValue()
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return std::max(hc.hp, 0.0f);
}

bool Player::IsDead() const
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return hc.hp <= 0.0f;
}

void Player::DrawDebugImGui()
{
	auto& sm = m_cm.get<StateMachineComponent>(m_entity);

	// historyKey に entity を渡すと個体ごとに履歴が分かれる
	if (auto* sys = SM_MGR.Get(sm)) sys->DrawImGui(sm, m_entity.index);
}