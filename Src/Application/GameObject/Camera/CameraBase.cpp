#include "CameraBase.h"
#include "../../GameSystem/Combat/CombatFx.h"

namespace
{
	// 1フレームとして扱う時間の上限。ロード直後などの巨大なdtで一気に回るのを防ぐ
	constexpr double kMaxDeltaTime = 1.0 / 30.0;

	// 0 割りを避けるための小さな値
	constexpr float kEpsilon = 1e-4f;

	inline float ClampedDeltaTime()
	{
		return static_cast<float>(std::min(Time::Instance().GetUnScaledDeltaTime(), kMaxDeltaTime));
	}

	// スティック入力の効き方。
	// デッドゾーンを抜けた量 a に対して a * (base + (1-base) * a)。中央付近をゆるく、端で最大
	inline float ShapeLookInput(float _magnitude, float _deadZone, float _curveBase)
	{
		if (_magnitude <= _deadZone) return 0.0f;
		float a = (_magnitude - _deadZone) / std::max(1.0f - _deadZone, kEpsilon);
		a = std::clamp(a, 0.0f, 1.0f);
		return a * (_curveBase + (1.0f - _curveBase) * a);
	}

	// 指数補間
	inline float DampTo(float _now, float _target, float _lambda, float _dt)
	{
		return _target + (_now - _target) * std::exp(-_lambda * _dt);
	}
}

CameraBase::CameraBase(kd::ecs::ComponentManager& cm, const std::shared_ptr<CollisionWorld>& world)
	: IObject(cm, world)
{
	m_cm.add<CameraComponent>(m_entity, {});
	m_camera = std::make_shared<KdCamera>();

	// 共通の調整値。派生の Init で Set〜 を呼べば上書きできる
	Tuning::Load(m_baseTuning, CameraTuningKey::kFile, "CameraBase");

	m_mouseSensitivity = m_baseTuning.mouseSensitivity;
	m_mouseInvertY = m_baseTuning.mouseInvertY;

	m_pivotOffset = m_baseTuning.pivotOffset;
	m_pivotMaxGap = m_baseTuning.pivotMaxGap;
	m_pivotMaxSpeed = m_baseTuning.pivotMaxSpeed;
	m_pivotRiseLambda = m_baseTuning.pivotRiseLambda;
	m_pivotFallLambda = m_baseTuning.pivotFallLambda;

	m_occlusionProbeRadius = m_baseTuning.occlusionProbeRadius;
	m_occlusionMinDistance = m_baseTuning.occlusionMinDistance;
	m_occlusionIgnoreDistance = m_baseTuning.occlusionIgnoreDistance;
	m_occlusionMaxPullSpeed = m_baseTuning.occlusionMaxPullSpeed;

	m_yawSpeed = m_baseTuning.yawSpeed;
	m_pitchSpeed = m_baseTuning.pitchSpeed;
	m_lookDeadZone = m_baseTuning.lookDeadZone;
}

void CameraBase::Update()
{
	if (!m_active) return;

	auto& cam = m_cm.get<CameraComponent>(m_entity);
	const float dt = ClampedDeltaTime();

	// マウス移動量はフォーカス演出中も必ず取り出す(溜めたまま戻ると視点が跳ねる)
	const Math::Vector2 mouseDelta = KdMouseLock::Instance().ConsumeMoveDelta();

	// フォーカス演出中は通常の入力・追従を行わない
	if (m_focusActive) { UpdateFocus(cam, dt); return; }

	// 追う相手がまだ決まっていない。空のエンティティから位置を読まないように止める
	if (!m_hasTarget || !m_cm.has<TransformComponent>(m_targetEntity)) return;

	// ---- 入力(スティック) ----
	const Math::Vector2 rawLook = KdInputActionSystem::Instance().GetAxis("Look");
	const float         rawMag = rawLook.Length();
	const Math::Vector2 lookDir = (rawMag > kEpsilon) ? rawLook / rawMag : Math::Vector2::Zero;
	const float         lookAmount = ShapeLookInput(rawMag, m_lookDeadZone, m_baseTuning.lookCurveBase); // 0..1

	// ---- ターゲット位置 ----
	const Math::Vector3 raw =
		m_cm.get<TransformComponent>(m_targetEntity).GetPosition() + m_pivotOffset;
	m_currentPivot = ResolvePivot(raw, dt);
	cam.SetOrbitPivot(m_currentPivot);

	// ---- 視点回転: スティック(rad/秒 × dt) ----
	cam.AddOrbitAngles(lookDir.x * lookAmount * m_yawSpeed * dt,
		lookDir.y * lookAmount * m_pitchSpeed * dt);

	// ---- 視点回転: マウス(移動量 × 感度。dtは掛けない) ----
	// 画面座標は下が+なので、上に動かしたら見上げるよう符号を反転する
	const float mouseYaw = mouseDelta.x * m_mouseSensitivity;
	const float mousePitch = -mouseDelta.y * m_mouseSensitivity * (m_mouseInvertY ? -1.0f : 1.0f);
	cam.AddOrbitAngles(mouseYaw, mousePitch);

	// ---- ターゲットプル ----
	// マウス操作の強さを「スティック全開時の角速度に対する比」で0..1に揃え、
	// 能動的に振っている間は吸着を弱める
	float mouseAmount = 0.0f;
	constexpr float kTiny = 1e-6f;
	if (dt > kTiny && m_yawSpeed > kTiny)
	{
		const float mouseRadPerSec = std::sqrt(mouseYaw * mouseYaw + mousePitch * mousePitch) / dt;
		mouseAmount = std::clamp(mouseRadPerSec / m_yawSpeed, 0.0f, 1.0f);
	}

	if (m_pullCandidatesProvider)
	{
		cam.UpdateTargetPull(dt, m_pullCandidatesProvider(), std::max(lookAmount, mouseAmount));
	}
}

void CameraBase::PostUpdate()
{
	if (!m_active) return;

	auto& cam = m_cm.get<CameraComponent>(m_entity);
	const float dt = ClampedDeltaTime();

	if (const float trauma = CombatFx::Instance().ConsumeTrauma(); trauma > 0.0f)
		cam.AddTrauma(trauma);

	// 振動の消費
	float vl = 0.0f, vr = 0.0f, vs = 0.0f;
	if (CombatFx::Instance().ConsumeVibration(vl, vr, vs))
		KdInputActionSystem::Instance().SetVibration(0, vl, vr, vs);

	//if (auto sp = m_wpWorld.lock())
	//{
	//	const float probeR = m_occlusionProbeRadius;
	//	const float minDist = m_occlusionMinDistance;
	//	const float ignore = m_occlusionIgnoreDistance;

	//	float& occDist = m_occDistance;
	//	bool& occInit = m_occInitialized;

	//	// 遮蔽なしとみなす誤差
	//	constexpr float kNoHitSlack = 1e-3f;

	//	cam.Update(dt, [&](const Math::Vector3& from, const Math::Vector3& to, float& outDist) -> bool
	//		{
	//			const float dist = (to - from).Length();
	//			if (dist < kEpsilon) return false;

	//			float best = dist;
	//			bool  anyHit = false;

	//			// 芯のレイ。正確で速い
	//			if (const auto ray = sp->RaycastStage(from, to); ray.isHit && ray.distance < best)
	//			{
	//				best = ray.distance; anyHit = true;
	//			}

	//			// 球スイープ
	//			if (const auto sw = sp->SphereCastStage(from, to, probeR); sw.isHit && sw.distance < best)
	//			{
	//				best = sw.distance; anyHit = true;
	//			}

	//			// 始点が地面や壁に接触していると距離ほぼ0で返る
	//			if (anyHit && best <= ignore) anyHit = false;

	//			const float target = anyHit ? best : dist;

	//			// ※元は「詰める側だけ速度制限をかける」つもりだったようだが、
	//			//   初回もそれ以降も同じ代入になっていて制限は掛かっていない。
	//			//   挙動を変えないよう、そのまま目標値を入れている(要確認リスト参照)
	//			occDist = target;
	//			occInit = true;

	//			if (occDist >= dist - kNoHitSlack) return false;	// 遮蔽なし

	//			// 壁に密着してもpivotまでは潰さない
	//			outDist = std::max(occDist, minDist);
	//			return true;
	//		});
	//}
	cam.Update(dt);
	

	cam.ApplyToKdCamera(*m_camera);
	m_camera->SetToShader();
}

// ====================================================
//  フォーカス演出
// ====================================================
void CameraBase::StartFocus(const Math::Vector3& _pos, float _distance, float _fovDeg,
	float _posLambda, float _distLambda)
{
	auto& cam = m_cm.get<CameraComponent>(m_entity);

	m_focusActive = true;
	m_focusPhase = FocusPhase::Turn;
	m_focusPhaseTime = 0.0f;
	m_focusPos = _pos;
	m_focusEye = cam.GetWorldPosition();	// 旋回中はこの位置から動かさない
	m_focusPivot = m_currentPivot;
	m_focusDistance = _distance;
	m_focusFovDeg = _fovDeg;
	m_focusPosLambda = _posLambda;
	m_focusDistLambda = _distLambda;

	cam.SetTargetPull(false);
	cam.EnableFraming(false);
	cam.SetOrbitPitchLimit(m_baseTuning.focusPitchMin, m_baseTuning.focusPitchMax);	// ひよこが上下どちらにいても向けるように
}

void CameraBase::UpdateFocus(CameraComponent& _cam, float _dt)
{
	m_focusPhaseTime += _dt;

	//------------------------------------------
	// 旋回: カメラ位置は固定し、向きだけをひよこへ
	//------------------------------------------
	if (m_focusPhase == FocusPhase::Turn)
	{
		Math::Vector3 to = m_focusPos - m_focusEye;
		const float len = to.Length();
		if (len > kEpsilon)
		{
			to /= len;

			const float targetYaw = atan2f(to.x, to.z);
			const float targetPitch = asinf(std::clamp(to.y, -1.0f, 1.0f));

			// yawは±πを跨ぐ差分を最短方向へ
			float yawDiff = targetYaw - _cam.GetOrbitYaw();
			while (yawDiff > DirectX::XM_PI)  yawDiff -= DirectX::XM_2PI;
			while (yawDiff < -DirectX::XM_PI) yawDiff += DirectX::XM_2PI;
			const float pitchDiff = targetPitch - _cam.GetOrbitPitch();

			const float k = 1.0f - std::exp(-m_baseTuning.focusTurnLambda * _dt);
			const float yaw = _cam.GetOrbitYaw() + yawDiff * k;
			const float pitch = _cam.GetOrbitPitch() + pitchDiff * k;
			const float dist = _cam.GetOrbitDistance();

			// eye = pivot - fwd*dist が m_focusEye になるようにピボットを置く
			const Math::Vector3 fwd = {
				cosf(pitch) * sinf(yaw),
				sinf(pitch),
				cosf(pitch) * cosf(yaw) };
			m_focusPivot = m_focusEye + fwd * dist;
			_cam.SetOrbit(m_focusPivot, yaw, pitch, dist);

			const float tolerance = m_baseTuning.focusAimTolerance;
			const bool aimed = std::fabs(yawDiff) < tolerance && std::fabs(pitchDiff) < tolerance;
			if (!aimed && m_focusPhaseTime < m_baseTuning.focusTurnTimeout) return;
		}

		// 向き終わった → ズームへ
		m_focusPhase = FocusPhase::Zoom;
		m_focusPhaseTime = 0.0f;
		_cam.SetTargetFovY(m_focusFovDeg, m_baseTuning.focusZoomFovLambda);
		return;
	}

	//------------------------------------------
	// ズーム: ピボットを寄せ、距離を詰める
	//------------------------------------------
	m_focusPivot = Math::Vector3(
		DampTo(m_focusPivot.x, m_focusPos.x, m_focusPosLambda, _dt),
		DampTo(m_focusPivot.y, m_focusPos.y, m_focusPosLambda, _dt),
		DampTo(m_focusPivot.z, m_focusPos.z, m_focusPosLambda, _dt));
	_cam.SetOrbitPivot(m_focusPivot);

	const float k = 1.0f - std::exp(-m_focusDistLambda * _dt);
	_cam.AddOrbitDistance((m_focusDistance - _cam.GetOrbitDistance()) * k);
}

//====================================================
//  CameraManager用ヘルパ
//====================================================
Math::Matrix CameraBase::GetResolvedWorldMatrix()
{
	return m_cm.get<CameraComponent>(m_entity).GetResolvedWorldMatrix();
}

void CameraBase::SnapToTarget()
{
	ResetPivot();	// カット切り替えでならしを持ち越さない
	m_cm.get<CameraComponent>(m_entity).SnapToTarget();
}

Math::Vector3 CameraBase::ResolvePivot(const Math::Vector3& _rawPos, float _dt)
{
	if (!m_pivotInitialized)
	{
		m_pivotY = _rawPos.y;
		m_pivotInitialized = true;
		return _rawPos;
	}

	const float gap = _rawPos.y - m_pivotY;

	// 上りと下りで速度を分ける。
	// 上りを遅らせるとキャラが画面下へ潜るので速く、
	// 下りは遅らせた方が落下中の画が落ち着く。
	const float lambda = (gap > 0.0f) ? m_pivotRiseLambda : m_pivotFallLambda;

	float next = DampTo(m_pivotY, _rawPos.y, lambda, _dt);

	// 1フレームで動ける量に上限を設ける。
	// 着地の吸着でY座標が瞬間移動しても、ここで速度に化けない。
	const float maxStep = m_pivotMaxSpeed * _dt;
	next = std::clamp(next, m_pivotY - maxStep, m_pivotY + maxStep);

	// 離れすぎたら引き戻す。長い落下で画面外へ置き去りにしない。
	next = std::clamp(next, _rawPos.y - m_pivotMaxGap, _rawPos.y + m_pivotMaxGap);

	m_pivotY = next;

	// 横は即応。ならすと操作の手応えが落ちる。
	return Math::Vector3(_rawPos.x, m_pivotY, _rawPos.z);
}