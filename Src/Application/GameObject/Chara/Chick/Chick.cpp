#include "Chick.h"
#include "../../../GameSystem/StateMachine/StateMachineManager.h"
#include "../../../Inc/Math/Damp.h"

// ===========================================================
// 初期化
// ===========================================================
void Chick::Init()
{
	const ChickTuning& t = m_tuning;

	// ---- モデル ----
	auto model = RES_MGR.GetModelList()->GetModel(t.modelName);
	const ModelId id = RES_MGR.GetModelList()->GetModelId(t.modelName);
	const WorkId wid = RES_MGR.GetWorkPool()->Create(model, id);

	m_cm.add<RenderComponent>(m_entity,
		{ RenderKind::Work, wid,
		RES_MGR.GetWorkPool()->GetGeneration(wid) });

	// ---- コリジョン ----
	if (auto sp = m_wpWorld.lock())
	{
		Collision::CollisionComponent init;
		init.shape = Collision::Shape::MakeCapsule(
			t.capsuleRadius, t.capsuleHeight, Collision::CapsulePivot::Bottom);
		init.type = Collision::ObjectType::Player;
		init.blocksCapsule = true;
		init.inverseMass = t.inverseMass;
		m_handle = sp->CreateCollision(init);

		Collision::CombatComponent initCombat;
		initCombat.team = Collision::Team::Player;
		initCombat.hittable = true;
		m_combatHandle = sp->CreateCombat(initCombat);
	}

	m_isActive = true;

	// Health は IChara のコンストラクタで追加済み。add し直さずに値だけ入れる
	ResetHealth(t.maxHp);

	// ---- ステートマシン ----
	SetupChickStates();

	m_cm.add<StateMachineComponent>(m_entity, SM_MGR.MakeComponent(kStateSystemName));

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	if (auto* sys = SM_MGR.Get(sm))
	{
		auto resolver = [this](const char* name) -> std::shared_ptr<KdAnimationData>
			{
				auto* work = GetWork();
				return work ? work->GetAnimation(name) : nullptr;
			};
		sys->SyncAnimation(sm, m_anim, resolver);
		sys->Update(sm, 0.0f, this, m_entity.index);
	}

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	GameScale::ApplyToTransform(tc);
	tc.SetScale(tc.GetScale() * t.bodyScale);
	m_baseScale = tc.GetScale();

	auto& rc = m_cm.get<RenderComponent>(m_entity);
	SetMaterial(rc, t.materialName.c_str());

	auto& gc = m_cm.add<GhostTrailComponent>(m_entity);
	gc.recording = true;
	gc.mode = GhostMode::XRay;

	auto& lc = m_cm.add<LightComponent>(m_entity);
	lc.range = t.lightRange;
	lc.power = t.lightPower;
	lc.offset = t.lightOffset;
	lc.enabled = true;

	// 配置前に呼ばれた場合の保険
	m_homePos = tc.GetPosition();
}

// ===========================================================
// 更新
// ===========================================================
void Chick::Update()
{
	const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

	auto sp = m_wpWorld.lock();
	if (!sp) return;

	// ステートは常に回す。倒れている間も Down ステートが時間を進めるため。
	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	if (auto* sys = SM_MGR.Get(sm))
	{
		auto resolver = [this](const char* name) -> std::shared_ptr<KdAnimationData>
			{
				auto* work = GetWork();
				return work ? work->GetAnimation(name) : nullptr;
			};
		sys->SyncAnimation(sm, m_anim, resolver);
		sys->Update(sm, dt, this, m_entity.index);
	}

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	auto* col = sp->GetCollision(m_handle);
	if (!col) return;	// コリジョンが作れていない(ワールドが無かった等)

	// 咥えられている間は運び手に位置を預ける。
	// 自前の移動量を出すと、口元へ置いた座標と引っ張り合う。
	if (IsCarried())
	{
		col->center = tc.GetPosition();
		col->moveVector = Math::Vector3::Zero;
		return;
	}

	col->center = tc.GetPosition();
	col->moveVector = m_move;

	// 倒れている間は殴られない。判定も出さない。
	if (auto* combat = sp->GetCombat(m_combatHandle); combat && IsActing())
	{
		const Math::Vector3 mid = col->GetCapsuleMid();
		combat->hurtVolume.p0 = mid - Math::Vector3::Up * col->shape.segHalf;
		combat->hurtVolume.p1 = mid + Math::Vector3::Up * col->shape.segHalf;
		combat->hurtVolume.radius = col->shape.radius;
	}
}

void Chick::PostUpdate()
{
	auto sp = m_wpWorld.lock();
	if (!sp) return;

	auto* col = sp->GetCollision(m_handle);
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	if (!col)
	{
		AdvanceAnim();
		return;
	}

	// 咥えられている間は位置の決定権がマネージャにある
	if (IsCarried())
	{
		col->center = tc.GetPosition();
		AdvanceAnim();
		return;
	}

	tc.SetPosition(col->result.position);

	m_isGrounded = col->result.grounded;
	if (m_isGrounded) m_gravity = 1.0f;

	//------------------------------------------
	// 被弾判定回収
	//------------------------------------------
	auto* combat = sp->GetCombat(m_combatHandle);
	if (IsActing() && combat)
	{
		auto& hc = m_cm.get<Game::Health>(m_entity);


		//　デバッグ強制ダウン状態
		if(GetAsyncKeyState('C') & 0x8000)
		{
			hc.hp = 0.0f;
			BeginDown();
		}

		for (auto& hit : combat->received)
		{
			hc.hp -= hit.damage;
			BeginHurt();

			if (hc.hp > 0.0f) continue;

			hc.hp = 0.0f;
			BeginDown();	// 死なない
			break;
		}
	}

	AdvanceAnim();
}

void Chick::AdvanceAnim()
{
	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	auto* sys = SM_MGR.Get(sm);
	auto* work = GetWork();
	if (!sys || !work) return;

	sys->AdvanceAnimation(sm, m_anim, work->WorkNodes());
}

// ===========================================================
// ダウン / 連れ去り
// ===========================================================
void Chick::SetVulnerable(bool _on)
{
	auto sp = m_wpWorld.lock();
	if (!sp) return;

	// 倒れている間は的から外す。追い討ちで永遠に起きられなくなるのを防ぐ。
	if (auto* combat = sp->GetCombat(m_combatHandle)) combat->hittable = _on;

	// 押し返しも切る。倒れた体が壁になると、運び手も自分も詰まる。
	if (auto* col = sp->GetCollision(m_handle)) col->blocksCapsule = _on;
}

void Chick::BeginDown()
{
	if (m_isDowned) return;

	m_isDowned = true;
	m_downTimer = m_tuning.downSec;

	StopMotion();
	ClearSlot();
	SetVulnerable(false);

	// 絵と遷移はステート側に任せる。ここは事実だけを立てる。
	SetFlag(ChickFlag::kDownRequested);
}

void Chick::BeginHurt()
{
	if (m_isHurt)return;
	m_isHurt = true;
	m_hurtTimer = m_tuning.hurtSec;

	SetFlag(ChickFlag::kHurt);
}

void Chick::BeginCarried(int _carrierId)
{
	m_carrierId = _carrierId;

	StopMotion();
	ClearSlot();
	SetVulnerable(false);

	SetFlag(ChickFlag::kCarryRequested);
}

void Chick::SetCarriedPosition(const Math::Vector3& _pos)
{
	if (!IsCarried()) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetPosition(_pos);

	// 運ばれている間もコリジョンを連れて行く
	if (auto sp = m_wpWorld.lock())
	{
		if (auto* col = sp->GetCollision(m_handle))
		{
			col->center = _pos;
			col->result.position = _pos;
		}
	}
}

void Chick::EndCarried(const Math::Vector3& _pos)
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetPosition(_pos);

	// コリジョン側の解決済み位置も合わせる
	if (auto sp = m_wpWorld.lock())
	{
		if (auto* col = sp->GetCollision(m_handle))
		{
			col->center = _pos;
			col->result.position = _pos;
			col->moveVector = Math::Vector3::Zero;
		}
	}
	m_carrierId = -1;

	Revive();
}

void Chick::SetCarriedScale(float _scale, bool _leaving)
{
	m_carryLeaving = _leaving;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_baseScale * std::max(_scale, kMinScale));
}

void Chick::Vanish()
{
	if (m_isVanished) return;

	m_isVanished = true;
	m_isActive = false;		// 死んだ扱い。IsAlive も false になる
	m_carryLeaving = false;

	SetVulnerable(false);

	// 片付くまでの間に何かを押したり拾ったりしないようにする
	if (auto sp = m_wpWorld.lock())
	{
		if (auto* col = sp->GetCollision(m_handle)) col->enabled = false;
	}

	if (m_cm.has<LightComponent>(m_entity)) m_cm.get<LightComponent>(m_entity).enabled = false;
}

void Chick::Revive()
{
	auto& hc = m_cm.get<Game::Health>(m_entity);
	hc.hp = m_tuning.maxHp;

	m_isDowned = false;
	m_carrierId = -1;
	m_downTimer = 0.0f;

	// 落下の勢いを持ち越さない
	m_gravity = 1.0f;
	StopMotion();

	SetVulnerable(true);

	// 走り去りの途中で離された場合に備えて大きさを戻す
	SetCarriedScale(1.0f, false);

	// 倒れた/咥えられたの予約が残っていると、起きた直後に戻されてしまう
	ClearFlag(ChickFlag::kDownRequested);
	ClearFlag(ChickFlag::kCarryRequested);
	ClearFlag(ChickFlag::kHurt);
}

void Chick::RecoverHurt()
{
	m_isHurt = false;
	m_hurtTimer = 0.0f;
	ClearFlag(ChickFlag::kHurt);
}

bool Chick::TickDown(float _dt)
{
	if (!m_isDowned) return false;
	if (m_downTimer <= 0.0f) return true;

	m_downTimer -= _dt;
	return (m_downTimer <= 0.0f);
}

float Chick::GetDownRatio() const
{
	if (m_tuning.downSec <= 0.0f) return 1.0f;
	return std::clamp(1.0f - (m_downTimer / m_tuning.downSec), 0.0f, 1.0f);
}

bool Chick::TickHurt(float _dt)
{
	if (!m_isHurt) return false;
	if (m_hurtTimer <= 0.0f) return true;

	m_hurtTimer -= _dt;
	return (m_hurtTimer <= 0.0f);
}

float Chick::GetHealthRatio() const
{
	if (m_tuning.maxHp <= 0.0f) return 0.0f;
	auto& hc = m_cm.get<Game::Health>(m_entity);
	return std::clamp(hc.hp / m_tuning.maxHp, 0.0f, 1.0f);
}

Math::Vector3 Chick::GetPosition() const
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	return tc.GetPosition();
}

KdModelWork* Chick::GetWork() const
{
	if (!m_cm.has<RenderComponent>(m_entity)) return nullptr;

	auto& wp = m_cm.get<RenderComponent>(m_entity);
	return RES_MGR.GetWorkPool()->Get(wp.assetId, wp.generation);
}

// ===========================================================
// 距離
// ===========================================================
namespace {
	float DistXZ(const Math::Vector3& _a, const Math::Vector3& _b)
	{
		Math::Vector3 d = _a - _b;
		d.y = 0.0f;
		return d.Length();
	}
}

float Chick::GetTargetDistance() const
{
	if (!m_hasTarget) return FLT_MAX;
	return DistXZ(m_targetPos, GetPosition());
}

float Chick::GetHomeDistance() const
{
	return DistXZ(m_homePos, GetPosition());
}

float Chick::GetSlotDistance() const
{
	if (!m_hasSlot) return FLT_MAX;
	return DistXZ(m_slotPos, GetPosition());
}

// ===========================================================
// 向き
// ===========================================================
Math::Vector3 Chick::GetFacing() const
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 facing = tc.GetForward() * kFacingSign;
	facing.y = 0.0f;
	if (facing.LengthSquared() > 0.0f) facing.Normalize();
	return facing;
}

void Chick::ApplyRotation(float _dt)
{
	if (m_rotateDir.LengthSquared() <= 0.0f) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	const float targetAngle = atan2f(
		m_rotateDir.x * kFacingSign,
		m_rotateDir.z * kFacingSign);

	const Math::Quaternion targetRot =
		Math::Quaternion::CreateFromAxisAngle(Math::Vector3::Up, targetAngle);

	tc.SetRotation(Damp::Rotation(tc.GetRotation(), targetRot, m_turnRate, _dt));
}

// ===========================================================
// 移動
// ===========================================================
// 残り距離から速度の倍率(0〜1)を出す。ブレーキ距離が 0 なら減速しない
float Chick::BrakeRatio(float _remain) const
{
	if (m_tuning.brakeDistance <= 0.0f) return 1.0f;
	return std::min(_remain / m_tuning.brakeDistance, 1.0f);
}

void Chick::MoveAlong(const Math::Vector3& _dir, float _dt, float _speed)
{
	Math::Vector3 dir = _dir;
	dir.y = 0.0f;
	if (dir.LengthSquared() > 0.0f) dir.Normalize();

	// 重力(接地中は地面に張り付かせる最小限の下向きの力)
	float vertical = -m_tuning.groundStick;
	if (!m_isGrounded)
	{
		if (m_gravity <= 0.0f) m_gravity = 1.0f;
		m_gravity *= m_gravityPow;
		m_gravity = std::min(m_gravity, m_gravityMax);
		vertical = -m_gravity;
	}

	m_move = (dir * _speed + Math::Vector3::Up * vertical) * _dt;
}

void Chick::ApplyMotion(float _dt, float _speed)
{
	ApplyRotation(_dt);
	MoveAlong(GetFacing(), _dt, _speed);
}

void Chick::MoveTo(const Math::Vector3& _dest, float _dt, float _speed, float _turnRate)
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 to = _dest - tc.GetPosition();
	to.y = 0.0f;

	const float dist = to.Length();
	if (dist <= kArriveEpsilon)
	{
		// 足は止めるが、重力は進める
		MoveAlong(Math::Vector3::Zero, _dt, 0.0f);
		return;
	}

	to /= dist;
	m_rotateDir = to;
	m_turnRate = _turnRate;

	ApplyRotation(_dt);

	// 旋回を待たずに歩き出す。ブレーキ距離の内側では距離に比例して減速する
	const float speed = _speed * BrakeRatio(dist);
	MoveAlong(to, _dt, speed);
}

void Chick::FollowTarget(float _dt, float _speed, float _turnRate, float _keepDistance)
{
	if (!m_hasTarget)
	{
		ApplyMotion(_dt, 0.0f);
		return;
	}

	auto& tc = m_cm.get<TransformComponent>(m_entity);

	Math::Vector3 to = m_targetPos - tc.GetPosition();
	to.y = 0.0f;

	const float dist = to.Length();
	if (dist > kArriveEpsilon)
	{
		to /= dist;
		m_rotateDir = to;		// 止まっている間も相手を向き続ける
		m_turnRate = _turnRate;
	}

	ApplyRotation(_dt);

	// 間合いの内側では足を止める
	const float over = dist - _keepDistance;
	const float speed = (over <= 0.0f) ? 0.0f : _speed * BrakeRatio(over);

	MoveAlong(to, _dt, speed);
}

// ===========================================================
// デバッグ表示
// ===========================================================
const char* Chick::GetStateLabel() const
{
	// 上から順に見て最初に一致したものを返す。
	// 強い状態(連れ去り/ダウン)を先に置くこと。
	struct Entry { unsigned int tag; const char* name; };
	static const Entry kTable[] = {
		{ ChickTag::kCarried, "Carried" },
		{ ChickTag::kDown,    "Down"    },
		{ ChickTag::kEscape,  "Escape"  },
		{ ChickTag::kFollow,  "Follow"  },
		{ ChickTag::kIdle,    "Idle"    },
	};

	if (!m_cm.has<StateMachineComponent>(m_entity)) return "-";

	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	for (const auto& e : kTable)
	{
		if (sm.HasTag(e.tag)) return e.name;
	}
	return "-";
}

void Chick::DrawDebugImGui()
{
	auto& sm = m_cm.get<StateMachineComponent>(m_entity);
	if (auto* sys = SM_MGR.Get(sm)) sys->DrawImGui(sm, m_entity.index);
}