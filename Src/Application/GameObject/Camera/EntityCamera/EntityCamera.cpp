#include "EntityCamera.h"
#include "../CameraManager.h"

EntityCamera::~EntityCamera()
{
	if (m_effectID != -1)
	{
		GPUParticleSystem::Instance().StopEffect(m_effectID);
		m_effectID = -1;
	}
}

void EntityCamera::Init()
{
	Tuning::Load(m_tuning, CameraTuningKey::kFile, "EntityCamera");
	const EntityCameraTuning& t = m_tuning;

	auto& cam = m_cm.get<CameraComponent>(m_entity);

	// ---- 軌道 ----
	cam.SetOrbit(t.orbitPivot, t.orbitYaw, t.orbitPitch, t.orbitDistance);
	cam.SetOrbitPitchLimit(t.pitchMin, t.pitchMax);
	cam.SetOrbitDistanceLimit(t.distanceMin, t.distanceMax);
	SetPivotOffset(Math::Vector3(0.0f, t.pivotHeight, 0.0f));

	// ---- 遮蔽 ----
	SetOcclusionResponse(t.occlusionIgnoreDistance, t.occlusionMaxPullSpeed);
	cam.EnableOcclusion(true);
	// padding は長さ。pull / push は追従レート(1/秒)なので縮尺とは無関係
	cam.SetOcclusionParams(t.occlusionPadding, t.occlusionPullRate, t.occlusionPushRate);
	SetOcclusionProbe(t.occlusionProbeRadius, t.occlusionMinDistance);

	// ---- 視点操作(rad/秒は縮尺と無関係) ----
	SetLookSpeed(t.yawSpeed, t.pitchSpeed);
	SetLookDeadZone(t.lookDeadZone);

	// ---- フレーミング ----
	cam.EnableFraming(true);
	cam.SetFraming(t.framingDeadZone, t.framingLookAhead, t.framingFollowRate, t.framingFollowRate2);

	// ---- 敵への吸着 ----
	cam.SetTargetPull(true);
	cam.SetTargetPullParams(DirectX::XMConvertToRadians(t.pullAngleDeg), t.pullRate,
		DirectX::XMConvertToRadians(t.pullInnerAngleDeg));
	cam.SetTargetPullStickiness(DirectX::XMConvertToRadians(t.pullStickAngleDeg), t.pullStickStrength);
	cam.SetTargetPullMaxDistance(t.pullMaxDistance);
	cam.SetTargetPullHeightSafety(t.pullHeightSafety);

	//空間パーティクル
	m_effectID =
		GPUParticleSystem::Instance().SpawnEffect(t.dustEffect, cam.GetWorldPosition());

	CameraManager::Instance().SetGameMouseLocked(true);
}

void EntityCamera::Update()
{
	CameraBase::Update();
	
	auto& cam = m_cm.get<CameraComponent>(m_entity);
	GPUParticleSystem::Instance().SetEffectPos(m_effectID, cam.GetWorldPosition());
}