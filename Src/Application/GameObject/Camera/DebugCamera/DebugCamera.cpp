#include "DebugCamera.h"
#include "../CameraManager.h"

namespace
{
	// これより短いベクトルは「向きが無い」とみなす(長さの2乗)
	constexpr float kTinyLengthSq = 1e-8f;

	// GetAsyncKeyState の戻り値で「今押されている」を表すビット
	constexpr short kKeyDownBit = static_cast<short>(0x8000);

	// yaw/pitchから前方ベクトル(CameraComponentのOrbitと同一規約)
	inline Math::Vector3 ForwardFromYawPitch(float _yaw, float _pitch)
	{
		return {
			cosf(_pitch) * sinf(_yaw),
			sinf(_pitch),
			cosf(_pitch) * cosf(_yaw)
		};
	}
}

//====================================================
// 初期化
//====================================================
void DebugCamera::Init()
{
	Tuning::Load(m_tuning, CameraTuningKey::kFile, "DebugCamera");
	m_position = m_tuning.startPosition;
	m_defaultFovDeg = m_tuning.defaultFovDeg;
	m_fovMinDeg = m_tuning.fovMinDeg;
	m_fovMaxDeg = m_tuning.fovMaxDeg;
	m_moveSpeed = m_tuning.moveSpeed;
	m_boostScale = m_tuning.boostScale;
	m_lookSpeed = m_tuning.lookSpeed;
	m_wheelStep = m_tuning.wheelStep;
	m_pitchLimit = m_tuning.pitchLimit;
	m_mouseSensitivity = m_tuning.mouseSensitivity;

	auto& cam = m_cm.get<CameraComponent>(m_entity);

	// フリーフライはManual運用。初期姿勢・FOVを反映。
	m_fovDeg = m_defaultFovDeg;
	cam.SetFovY(m_fovDeg);
	m_fovDirty = false;

	Math::Vector3 fwd = ForwardFromYawPitch(m_yaw, m_pitch);
	cam.SetLookAt(m_position, m_position + fwd, Math::Vector3::Up);
}

//====================================================
// 更新(入力→姿勢)
//====================================================
void DebugCamera::Update()
{
	if (!IsActive()) return; // 非アクティブ時は入力を無視

	// --- モード切替(カメラ操作 ⇔ カーソル操作) ---
	if (KdInputActionSystem::Instance().IsPress("MouseLockToggle"))
	{
		CameraManager::Instance().ToggleDebugMouseLock();
	}

	KdMouseLock& mouse = KdMouseLock::Instance();

	// --- カーソルモード: カメラは一切動かさない ---
	// ImGuiの入力欄に打ったWASDやスクロールでカメラが動かないよう、入力ごと捨てる
	if (!mouse.IsLocked())
	{
		mouse.ConsumeMoveDelta();
		mouse.ConsumeWheelDelta();
		return;
	}

	auto& cam = m_cm.get<CameraComponent>(m_entity);
	const float dt = static_cast<float>(Time::Instance().GetUnScaledDeltaTime());

	// --- 視点回転: スティック(既存Orbitと同様に正規化して一定レート) ---
	Math::Vector2 look = ReadLookAxis();
	if (look.LengthSquared() > 0.0f) look.Normalize();
	m_yaw += look.x * m_lookSpeed;
	m_pitch += look.y * m_lookSpeed;

	// --- 視点回転: マウス(dtは掛けない) ---
	const Math::Vector2 mouseDelta = mouse.ConsumeMoveDelta();
	m_yaw += mouseDelta.x * m_mouseSensitivity;
	m_pitch -= mouseDelta.y * m_mouseSensitivity;

	m_pitch = std::clamp(m_pitch, -m_pitchLimit, m_pitchLimit);

	// --- 基底ベクトル ---
	Math::Vector3 forward = ForwardFromYawPitch(m_yaw, m_pitch);
	Math::Vector3 right = Math::Vector3::Up.Cross(forward); // 常に水平(ストレイフが上下にぶれない)
	if (right.LengthSquared() < kTinyLengthSq) right = Math::Vector3::Right;
	right.Normalize();

	// --- 移動(前後左右=視点基準 / 上下=ワールドUp) ---
	Math::Vector3 mv = ReadMoveInput(); // x=右, y=上, z=前
	Math::Vector3 vel =
		right * mv.x +
		Math::Vector3::Up * mv.y +
		forward * mv.z;
	if (vel.LengthSquared() > kTinyLengthSq)
	{
		vel.Normalize(); // 斜め入力で速くならないよう正規化
		float speed = m_moveSpeed * (IsSpeedBoost() ? m_boostScale : 1.0f);
		m_position += vel * (speed * dt);
	}

	// --- FOV(ホイール増減 / 中クリックでデフォルト) ---
	float wheel = ReadWheelDelta();
	if (wheel != 0.0f)
	{
		// 符号を逆にしたい場合は下の + を - にするだけ
		m_fovDeg = std::clamp(m_fovDeg + wheel * m_wheelStep, m_fovMinDeg, m_fovMaxDeg);
		m_fovDirty = true;
	}
	if (IsResetFovPressed())
	{
		m_fovDeg = m_defaultFovDeg;
		m_fovDirty = true;
	}
	if (m_fovDirty)
	{
		cam.SetFovY(m_fovDeg);
		m_fovDirty = false;
	}

	// --- 姿勢反映(Manual) ---
	cam.SetLookAt(m_position, m_position + forward, Math::Vector3::Up);
}

//====================================================
// 反映(オクルージョン無し=壁抜けOK)
//====================================================
void DebugCamera::PostUpdate()
{
	if (!IsActive()) return;

	auto& cam = m_cm.get<CameraComponent>(m_entity);

	// rayを渡さない=オクルージョン無効。
	// 入力側(Update)と同じく実時間で進める。前はここだけゲーム内時間だったので、
	// スローやヒットストップ中にデバッグカメラの追従まで止まっていた
	cam.Update(static_cast<float>(Time::Instance().GetUnScaledDeltaTime()));
	cam.ApplyToKdCamera(*m_camera);
}

//====================================================
// 有効化時に描画カメラの姿勢を引き継ぐ
//====================================================
void DebugCamera::SyncPoseFromWorldMatrix(const Math::Matrix& _world)
{
	m_position = _world.Translation();

	Math::Vector3 fwd = _world.Backward(); // GetForward()==Backward()仕様
	if (fwd.LengthSquared() < kTinyLengthSq) fwd = Math::Vector3::Backward;
	fwd.Normalize();

	m_yaw = atan2f(fwd.x, fwd.z);
	m_pitch = std::clamp(asinf(std::clamp(fwd.y, -1.0f, 1.0f)), -m_pitchLimit, m_pitchLimit);

	// ゲーム中に溜まったホイール等を持ち込まない(切り替えた瞬間にFOVが跳ねるのを防ぐ)
	KdMouseLock::Instance().ConsumeMoveDelta();
	KdMouseLock::Instance().ConsumeWheelDelta();

	auto& cam = m_cm.get<CameraComponent>(m_entity);
	cam.SetLookAt(m_position, m_position + ForwardFromYawPitch(m_yaw, m_pitch), Math::Vector3::Up);
}

//====================================================
// 入力(環境依存箇所)
//====================================================
Math::Vector2 DebugCamera::ReadLookAxis() const
{
	return KdInputActionSystem::Instance().GetAxis("Look");
}

Math::Vector3 DebugCamera::ReadMoveInput() const
{
	// 生キー入力(デバッグ用途のためアクション割り当てに依存しない)
	// アクションに寄せたい場合は GetAxis("Move") 等へ置換可
	auto down = [](int vk) -> bool { return (GetAsyncKeyState(vk) & kKeyDownBit) != 0; };

	Math::Vector3 mv = Math::Vector3::Zero;
	if (down('W'))mv.z += 1.0f;
	if (down('S')) mv.z -= 1.0f;
	if (down('D')) mv.x += 1.0f;
	if (down('A')) mv.x -= 1.0f;
	if (down('E')) mv.y += 1.0f; // 上昇
	if (down('Q')) mv.y -= 1.0f; // 下降
	return mv;
}

bool DebugCamera::IsSpeedBoost() const
{
	return (GetAsyncKeyState(VK_SHIFT) & kKeyDownBit) != 0;
}

float DebugCamera::ReadWheelDelta()
{
	// 上回転で正・下回転で負(ノッチ数)
	return KdMouseLock::Instance().ConsumeWheelDelta();
}

bool DebugCamera::IsResetFovPressed()
{
	// ホイールボタン(中クリック)の押下エッジ。VK_MBUTTONは生入力で取得可。
	bool now = (GetAsyncKeyState(VK_MBUTTON) & kKeyDownBit) != 0;
	bool pressed = now && !m_prevResetDown;
	m_prevResetDown = now;
	return pressed;
}