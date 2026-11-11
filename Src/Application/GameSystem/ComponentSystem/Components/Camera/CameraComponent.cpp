#include "CameraComponent.h"

namespace
{
	// 1D value noise: 滑らかで連続なノイズ([-1,1]付近)。乱数ジッタより自然な揺れになる。
	// 符号なし演算で桁あふれのUBを回避(UBSanitizerクリーン)。
	inline float Hash1(int n)
	{
		uint32_t un = static_cast<uint32_t>(n);
		un = (un << 13) ^ un;
		uint32_t m = (un * (un * un * 15731u + 789221u) + 1376312589u) & 0x7fffffffu;
		return 1.0f - static_cast<float>(m) / 1073741824.0f; // [-1,1]
	}
	inline float ValueNoise1D(float x)
	{
		int   i = static_cast<int>(floorf(x));
		float f = x - static_cast<float>(i);
		float u = f * f * (3.0f - 2.0f * f); // smoothstep
		return Hash1(i) * (1.0f - u) + Hash1(i + 1) * u;
	}
	// フレームレート非依存の指数減衰係数
	inline float DampK(float lambda, float dt) { return 1.0f - expf(-lambda * dt); }
}

//====================================================
// 射影
//====================================================
void CameraComponent::SetPerspective(float _fovDeg, float _nearZ, float _farZ, float _aspect)
{
	m_fovDeg = _fovDeg; m_nearZ = _nearZ; m_farZ = _farZ; m_aspect = _aspect;
	m_fovLerping = false;
	m_projDirty = true;
}

void CameraComponent::ApplyProjection(KdCamera& _kd)
{
	// aspect自動(0以下)はバックバッファ依存のため毎回作り直す(リサイズ追従)
	if (m_projDirty || m_aspect <= 0.0f)
	{
		// KdCamera::SetProjectionMatrix(fov, maxRange, minRange, aspect) の順に注意
		_kd.SetProjectionMatrix(m_fovDeg, m_farZ, m_nearZ, m_aspect);
		m_projDirty = false;
	}
}

//====================================================
// ビュー(姿勢)
//====================================================
Math::Matrix CameraComponent::BuildLookMatrix(const Math::Vector3& _eye, const Math::Vector3& _target, const Math::Vector3& _up)
{
	Math::Vector3 forward = _target - _eye;
	if (forward.LengthSquared() < 1e-8f) forward = Math::Vector3::Backward;
	forward.Normalize();

	Math::Vector3 right = _up.Cross(forward);
	if (right.LengthSquared() < 1e-8f) right = Math::Vector3::Right;
	right.Normalize();

	Math::Vector3 up = forward.Cross(right);

	Math::Matrix m = Math::Matrix::Identity;
	m.Right(right);
	m.Up(up);
	m.Backward(forward);   // GetForward()==Backward()の既存仕様に合わせる
	m.Translation(_eye);
	return m;
}

void CameraComponent::SetCameraMatrix(const Math::Matrix& _cameraWorld)
{
	m_mode = Mode::Manual;
	m_baseWorld = _cameraWorld;
	m_finalWorld = _cameraWorld; // Update前でも取得できるよう即反映
}

void CameraComponent::SetLookAt(const Math::Vector3& _eye, const Math::Vector3& _target, const Math::Vector3& _up)
{
	m_mode = Mode::Manual;
	m_baseWorld = BuildLookMatrix(_eye, _target, _up);
	m_finalWorld = m_baseWorld;
}

//====================================================
// 追従(Follow)
//====================================================
void CameraComponent::SetFollow(const Math::Vector3& _eye, const Math::Vector3& _lookTarget)
{
	m_mode = Mode::Follow;
	m_desiredEye = _eye;
	m_desiredLook = _lookTarget;
}

//====================================================
// 軌道(Orbit)
//====================================================
void CameraComponent::SetOrbit(const Math::Vector3& _pivot, float _yawRad, float _pitchRad, float _distance)
{
	m_mode = Mode::Orbit;
	m_pivot = _pivot;
	m_yaw = _yawRad;
	m_pitch = std::clamp(_pitchRad, m_pitchMin, m_pitchMax);
	m_distance = std::clamp(_distance, m_distMin, m_distMax);
}

void CameraComponent::AddOrbitAngles(float _yawRad, float _pitchRad)
{
	m_mode = Mode::Orbit;
	m_yaw += _yawRad;
	m_pitch = std::clamp(m_pitch + _pitchRad, m_pitchMin, m_pitchMax);
}

void CameraComponent::AddOrbitDistance(float _delta)
{
	m_distance = std::clamp(m_distance + _delta, m_distMin, m_distMax);
}

//====================================================
// ターゲットプル(センタリングアシスト/ソフトロック)
//====================================================
void CameraComponent::SetTargetPullParams(float _maxAngleRad, float _strengthLambda, float _innerAngleRad)
{
	m_targetPullMaxAngle = _maxAngleRad;
	m_targetPullInnerAngle = (std::min)(_innerAngleRad, _maxAngleRad);
	m_targetPullLambda = _strengthLambda;
}

void CameraComponent::UpdateTargetPull(float _dt, const std::vector<PullCandidate>& _candidates, float _playerInputMagnitude)
{
	m_targetPullActive = false;

	// ホールドタイマーは有効/無効に関わらず進める(再有効化時に前の残り時間を持ち越さないため)
	if (m_targetPullHoldTimer > 0.0f) m_targetPullHoldTimer -= _dt;

	if (!m_targetPullEnabled || m_mode != Mode::Orbit || _candidates.empty())
	{
		m_targetPullLocked = false;
		return;
	}

	// 現在の視線方向(m_yaw/m_pitchから直接算出。既存Orbit更新と同じ規約)
	Math::Vector3 curFwd = {
		cosf(m_pitch) * sinf(m_yaw),
		sinf(m_pitch),
		cosf(m_pitch) * cosf(m_yaw)
	};

	// eye位置の近似: pivotそのものではなく、今フレームのyaw/pitch/distanceから
	// 「Update()が今から計算するはずのeye位置」を先読みして使う(1フレーム遅れなし)。
	// オクルージョンで距離が詰まっている場合は前フレームの詰め済み距離を使うとより正確。
	float approxDist = (m_occlusionEnabled && m_occInit) ? m_curOcclusionDist : m_distance;
	Math::Vector3 origin = m_pivot - curFwd * approxDist;

	// 全候補の角度を評価しつつ、現在ロック中の対象の角度も同時に拾う
	float bestAngle = m_targetPullMaxAngle;
	Math::Vector3 bestDir = curFwd;
	Math::Vector3 bestPos = Math::Vector3::Zero;
	float bestDist = 0.0f;
	std::uint64_t bestId = 0;
	bool foundBest = false;

	bool lockedStillPresent = false;
	float lockedAngle = 0.0f;
	Math::Vector3 lockedDir = curFwd;
	Math::Vector3 lockedPos = Math::Vector3::Zero;
	float lockedDist = 0.0f;

	for (const auto& candidate : _candidates)
	{
		const std::uint64_t id = candidate.first;
		const Math::Vector3& pos = candidate.second;

		Math::Vector3 toTarget = pos - origin;
		float dist = toTarget.Length();
		if (dist < 1e-3f) continue;

		if (dist > m_targetPullMaxDistance) continue;

		Math::Vector3 dir = toTarget / dist;

		float angle = acosf(std::clamp(curFwd.Dot(dir), -1.0f, 1.0f));

		if (m_targetPullLocked && id == m_targetPullLockedId)
		{
			lockedStillPresent = true;
			lockedAngle = angle;
			lockedDir = dir;
			lockedPos = pos;
			lockedDist = dist;
		}

		if (angle < bestAngle)
		{
			bestAngle = angle;
			bestDir = dir;
			bestPos = pos;
			bestDist = dist;
			bestId = id;
			foundBest = true;
		}
	}

	// ロック解除判定: 対象が候補から消えた、またはmaxAngleを超えて離れた場合は即解除
	// (角度超過は本来なら乗り換え判定の中で自然に処理されるが、代わりの候補が
	//  1つも無い場合にロックが居座り続けてしまうため、ここで明示的に切る)
	if (m_targetPullLocked && (!lockedStillPresent || lockedAngle > m_targetPullMaxAngle))
	{
		m_targetPullLocked = false;
	}

	if (m_targetPullLocked)
	{
		bool holdActive = m_targetPullHoldTimer > 0.0f;
		bool challengerIsClearlyBetter = foundBest && bestId != m_targetPullLockedId
			&& (lockedAngle - bestAngle) > m_targetPullSwitchMargin;

		if (holdActive || !challengerIsClearlyBetter)
		{
			// 乗り換えの条件を満たさない間は、現在ロック中の対象を使い続ける
			bestId = m_targetPullLockedId;
			bestDir = lockedDir;
			bestAngle = lockedAngle;
			bestPos = lockedPos;
			bestDist = lockedDist;
			foundBest = true;
		}
		else
		{
			// 明確に有利な候補が現れた → 乗り換え、ホールドタイマーをリセット
			m_targetPullLockedId = bestId;
			m_targetPullHoldTimer = m_targetPullMinHoldTime;
		}
	}
	else if (foundBest)
	{
		// 新規ロック
		m_targetPullLocked = true;
		m_targetPullLockedId = bestId;
		m_targetPullHoldTimer = m_targetPullMinHoldTime;
	}

	if (!foundBest) return;

	m_targetPullActive = true;
	m_targetPullTargetPos = bestPos;
	m_targetPullTargetDistance = bestDist;
	m_targetPullTargetAngle = bestAngle;

	float targetYaw = atan2f(bestDir.x, bestDir.z);
	float targetPitch = asinf(std::clamp(bestDir.y, -1.0f, 1.0f));

	// 地面貫通/上空突き抜け防止:
	// eye.y = pivot.y - sin(pitch)*distance という式から、今のdistanceでeyeが
	// 許容範囲(pivotからmin〜maxの高さ)に収まる最大/最小pitchを逆算してクランプする。
	// 固定角度でクランプすると距離が大きい時だけ地面に刺さるので、必ず距離込みで計算すること。
	if (approxDist > 1e-4f)
	{
		float maxPitchAllowed = asinf(std::clamp(-m_targetPullMinEyeHeightOffset / approxDist, -1.0f, 1.0f));
		float minPitchAllowed = asinf(std::clamp(-m_targetPullMaxEyeHeightOffset / approxDist, -1.0f, 1.0f));
		if (minPitchAllowed > maxPitchAllowed)
		{
			float tmp = minPitchAllowed;
			minPitchAllowed = maxPitchAllowed;
			maxPitchAllowed = tmp;
		}
		targetPitch = std::clamp(targetPitch, minPitchAllowed, maxPitchAllowed);
	}

	// 中心に近いほど強く(inner角度以内は最大強度、maxAngleに近づくほど弱まる)
	float falloff = 1.0f;
	if (m_targetPullMaxAngle > m_targetPullInnerAngle)
	{
		falloff = 1.0f - std::clamp(
			(bestAngle - m_targetPullInnerAngle) / (m_targetPullMaxAngle - m_targetPullInnerAngle),
			0.0f, 1.0f);
	}

	// プレイヤーが能動的に操作中なら引き寄せを弱める(押し付け感の防止)
	float inputFactor = 1.0f - std::clamp(_playerInputMagnitude, 0.0f, 1.0f) * m_targetPullInputSuppression;

	float k = DampK(m_targetPullLambda * falloff * inputFactor, _dt);

	// yawは±πを跨ぐ差分を最短方向へ補正してから寄せる
	constexpr float kPi = 3.14159265358979323846f;
	float yawDiff = targetYaw - m_yaw;
	while (yawDiff > kPi)  yawDiff -= kPi * 2.0f;
	while (yawDiff < -kPi) yawDiff += kPi * 2.0f;

	m_yaw += yawDiff * k;
	m_pitch = std::clamp(m_pitch + (targetPitch - m_pitch) * k, m_pitchMin, m_pitchMax);
}

//====================================================
// ルックアヘッド + デッドゾーン
//====================================================
void CameraComponent::SetFraming(const Math::Vector3& _deadZoneHalf, float _lookAheadTime,
	float _lookAheadMax, float _velSmoothLambda)
{
	m_deadZoneHalf = _deadZoneHalf;
	m_lookAheadTime = _lookAheadTime;
	m_lookAheadMax = _lookAheadMax;
	m_velSmoothLambda = _velSmoothLambda;
	m_framingEnabled = true;
}

// 追従点の前処理: デッドゾーンで箱の縁に押し、速度で先読みオフセットを足す
Math::Vector3 CameraComponent::ProcessFraming(const Math::Vector3& _raw, float _dt)
{
	if (!m_framingEnabled) return _raw;

	if (!m_framingInit)
	{
		m_framedTarget = _raw;
		m_prevRawTarget = _raw;
		m_smoothVel = Math::Vector3::Zero;
		m_framingInit = true;
		return _raw;
	}

	// 速度推定(平滑化)
	if (_dt > 1e-6f)
	{
		Math::Vector3 inst = (_raw - m_prevRawTarget) / _dt;
		m_smoothVel = Math::Vector3::Lerp(m_smoothVel, inst, DampK(m_velSmoothLambda, _dt));
	}
	m_prevRawTarget = _raw;

	// デッドゾーン(軸ごとに箱の縁で押される)
	Math::Vector3 d = _raw - m_framedTarget;
	if (d.x > m_deadZoneHalf.x) m_framedTarget.x = _raw.x - m_deadZoneHalf.x;
	else if (d.x < -m_deadZoneHalf.x) m_framedTarget.x = _raw.x + m_deadZoneHalf.x;
	if (d.y > m_deadZoneHalf.y) m_framedTarget.y = _raw.y - m_deadZoneHalf.y;
	else if (d.y < -m_deadZoneHalf.y) m_framedTarget.y = _raw.y + m_deadZoneHalf.y;
	if (d.z > m_deadZoneHalf.z) m_framedTarget.z = _raw.z - m_deadZoneHalf.z;
	else if (d.z < -m_deadZoneHalf.z) m_framedTarget.z = _raw.z + m_deadZoneHalf.z;

	// ルックアヘッド(最大距離でクランプ)
	Math::Vector3 ahead = m_smoothVel * m_lookAheadTime;
	float len = ahead.Length();
	if (len > m_lookAheadMax && len > 1e-6f) ahead *= (m_lookAheadMax / len);

	return m_framedTarget + ahead;
}

//====================================================
// オクルージョン回避
//====================================================
void CameraComponent::SetOcclusionParams(float _padding, float _pullLambda, float _pushLambda)
{
	m_occlusionPadding = _padding;
	m_occlusionPullLambda = _pullLambda;
	m_occlusionPushLambda = _pushLambda;
}

//====================================================
// カメラシェイク
//====================================================
void CameraComponent::AddTrauma(float _amount)
{
	m_trauma += _amount;
	if (m_trauma > 1.0f) m_trauma = 1.0f;
	if (m_trauma < 0.0f) m_trauma = 0.0f;
}

void CameraComponent::SetShakeParams(float _maxAngleRad, float _maxOffset, float _maxRollRad, float _freq, float _decayPerSec)
{
	m_shakeMaxAngleRad = _maxAngleRad;
	m_shakeMaxOffset = _maxOffset;
	m_shakeMaxRollRad = _maxRollRad;
	m_shakeFreq = _freq;
	m_traumaDecay = _decayPerSec;
}

//====================================================
// 更新
//====================================================
void CameraComponent::Update(float _dt, const RayCastFn& _occlusionRay)
{
	if (_dt < 0.0f) _dt = 0.0f;

	// --- FOV補間 ---
	if (m_fovLerping)
	{
		m_fovDeg += (m_targetFovDeg - m_fovDeg) * DampK(m_fovLambda, _dt);
		if (fabsf(m_targetFovDeg - m_fovDeg) < 0.01f) { m_fovDeg = m_targetFovDeg; m_fovLerping = false; }
		m_projDirty = true;
	}

	// --- 軌道: yaw/pitch/distanceから目標eye・注視点を算出 ---
	if (m_mode == Mode::Orbit)
	{
		Math::Vector3 pivot = ProcessFraming(m_pivot, _dt);

		Math::Vector3 fwd = {
			cosf(m_pitch) * sinf(m_yaw),
			sinf(m_pitch),
			cosf(m_pitch) * cosf(m_yaw)
		};

		float useDist = m_distance;
		Math::Vector3 wantEye = pivot - fwd * m_distance;

		// pivot→望むeyeへレイを飛ばし、壁があれば距離を詰める(rayは引数注入)
		if (m_occlusionEnabled && _occlusionRay)
		{
			float hitDist = 0.0f;
			if (_occlusionRay(pivot, wantEye, hitDist))
			{
				float allowed = (std::max)(m_distMin, hitDist - m_occlusionPadding);
				useDist = (std::min)(useDist, allowed);
			}
			// 詰めるのは速く/戻すのは遅く(非対称スムージングでポップ防止)
			if (!m_occInit) { m_curOcclusionDist = useDist; m_occInit = true; }
			else
			{
				float lambda = (useDist < m_curOcclusionDist) ? m_occlusionPullLambda : m_occlusionPushLambda;
				m_curOcclusionDist += (useDist - m_curOcclusionDist) * DampK(lambda, _dt);
			}
			useDist = m_curOcclusionDist;
		}

		m_desiredEye = pivot - fwd * useDist;
		m_desiredLook = pivot;
	}

	// --- 追従: 注視点をルックアヘッド処理し、eyeも同じ量だけ平行移動 ---
	if (m_mode == Mode::Follow)
	{
		Math::Vector3 processed = ProcessFraming(m_desiredLook, _dt);
		Math::Vector3 delta = processed - m_desiredLook;
		m_desiredEye += delta;
		m_desiredLook = processed;
	}

	// --- 追従補間(Follow/Orbit共通) ---
	if (m_mode == Mode::Follow || m_mode == Mode::Orbit)
	{
		if (!m_smoothInit)
		{
			m_smoothEye = m_desiredEye;
			m_smoothLook = m_desiredLook;
			m_smoothInit = true;
		}
		else
		{
			m_smoothEye = Math::Vector3::Lerp(m_smoothEye, m_desiredEye, DampK(m_posLambda, _dt));
			m_smoothLook = Math::Vector3::Lerp(m_smoothLook, m_desiredLook, DampK(m_lookLambda, _dt));
		}
	}

	// --- シェイク ---
	m_shakeTime += _dt;
	if (m_trauma > 0.0f)
	{
		m_trauma -= m_traumaDecay * _dt;
		if (m_trauma < 0.0f) m_trauma = 0.0f;
	}
	{
		float shake = m_trauma * m_trauma;
		float t = m_shakeTime * m_shakeFreq;
		m_shYaw = m_shakeMaxAngleRad * shake * ValueNoise1D(t + 0.0f);
		m_shPitch = m_shakeMaxAngleRad * shake * ValueNoise1D(t + 17.3f);
		m_shRoll = m_shakeMaxRollRad * shake * ValueNoise1D(t + 41.7f);
		m_shOffX = m_shakeMaxOffset * shake * ValueNoise1D(t + 71.1f);
		m_shOffY = m_shakeMaxOffset * shake * ValueNoise1D(t + 101.9f);
	}

	RebuildFinalMatrix();
}

void CameraComponent::RebuildFinalMatrix()
{
	// 1) シェイク適用前の"素"のカメラ行列を決定
	switch (m_mode)
	{
	case Mode::Follow:
	case Mode::Orbit:
		m_baseWorld = BuildLookMatrix(m_smoothEye, m_smoothLook, Math::Vector3::Up);
		break;
	case Mode::Manual:
	default:
		// m_baseWorld は SetCameraMatrix/SetLookAt/FollowTransformMatrix で設定済みのものを使う
		break;
	}

	// 2) シェイクをカメラローカル空間で加算(回転+並進 → baseへ合成)
	Math::Matrix shake =
		Math::Matrix::CreateFromYawPitchRoll(m_shYaw, m_shPitch, m_shRoll) *
		Math::Matrix::CreateTranslation(m_shOffX, m_shOffY, 0.0f);

	m_finalWorld = shake * m_baseWorld;
}

//====================================================
// 反映(GPU転送は呼び出し側)
//====================================================
void CameraComponent::ApplyToKdCamera(KdCamera& _kd)
{
	_kd.SetCameraMatrix(m_finalWorld);
	ApplyProjection(_kd);
	_kd.SetFocus(m_focusDist, m_focusForeRange, m_focusBackRange);
}