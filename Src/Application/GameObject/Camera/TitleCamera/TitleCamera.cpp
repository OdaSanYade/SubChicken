#include "TitleCamera.h"

namespace
{
	// 1フレームとして扱う時間の上限。ロード直後などの巨大なdtで一気に回るのを防ぐ
	constexpr double kMaxDeltaTime = 1.0 / 30.0;

	inline float ClampedDeltaTime()
	{
		return static_cast<float>(std::min(Time::Instance().GetDeltaTime(), kMaxDeltaTime));
	}

	// sin に渡す角度。「1秒あたり何周するか」で持っている速度を角速度へ直す
	inline float Wave(float _time, float _cyclesPerSec)
	{
		return std::sin(_time * _cyclesPerSec * DirectX::XM_2PI);
	}
}

void TitleCamera::Init()
{
	Tuning::Load(m_tuning, CameraTuningKey::kFile, "TitleCamera");
	const TitleCameraTuning& t = m_tuning;

	auto& cam = m_cm.get<CameraComponent>(m_entity);

	cam.SetOrbit(m_pivot, t.yaw, t.basePitch, t.baseDistance);
	cam.SetPerspective(t.fovBase, t.nearZ, t.farZ);

	// タイトルでは使わないものを明示的に切る
	cam.SetTargetPull(false);
	cam.EnableFraming(false);

	// 遮蔽だけは残す
	cam.EnableOcclusion(true);
	cam.SetOcclusionParams(t.occlusionPadding, t.occlusionPullRate, t.occlusionPushRate);

	SetOcclusionProbe(t.occlusionProbeRadius, t.occlusionMinDistance);
	SetOcclusionResponse(t.occlusionIgnoreDistance, t.occlusionMaxPullSpeed);

	cam.SnapToTarget();
}

void TitleCamera::Update()
{
	if (!m_active) return;

	auto& cam = m_cm.get<CameraComponent>(m_entity);
	const TitleCameraTuning& t = m_tuning;
	const float dt = ClampedDeltaTime();

	m_time += dt;

	//------------------------------------------
	// 常時の動き
	//------------------------------------------
	// ゆっくり回り続ける。止まった絵はスクリーンショットに見える。
	// (今は向きを固定している。回すなら t.yawSpeed * dt を向きに足す)

	// 上下と前後の揺れ。周期をずらして、往復が同期しないようにする。
	const float bob = Wave(m_time, t.bobSpeed) * t.bobAmplitude;
	const float zoom = Wave(m_time, t.zoomSpeed) * t.zoomAmplitude;

	float distance = t.baseDistance + zoom;
	float fov = t.fovBase + Wave(m_time, t.fovSpeed) * t.fovAmplitude;

	//------------------------------------------
	// 決定後
	//------------------------------------------
	// ゆっくり寄りながら画角を狭める。戦闘へ意識が向く。
	if (m_leaving)
	{
		m_leaveT = std::min(1.0f, m_leaveT + dt / std::max(t.leaveSec, dt));

		// 終盤ほど速く寄る。等速だと「引っ張られている」感じが出ない。
		const float ease = m_leaveT * m_leaveT;

		distance = std::lerp(distance, m_tuning.leaveDistance, ease);
		fov = std::lerp(fov, m_tuning.leaveFov, ease);
	}

	//------------------------------------------
	// 反映
	//------------------------------------------
	cam.SetOrbit(m_pivot + Math::Vector3(0.0f, bob, 0.0f), t.yaw, t.basePitch, distance);

	// 画角は補間で渡す。直接入れると微小な差が毎フレーム跳ねて見える。
	cam.SetTargetFovY(fov, t.fovLambda);
}