#include "Mushroom.h"

void Mushroom::Init()
{
	const MushroomTuning& t = m_tuning;

	// ---- モデル ----
	const ModelId id = RES_MGR.GetModelList()->GetModelId(t.modelName);

	// Work を使わない共有モデルなので世代番号は 0
	constexpr uint32_t kNoGeneration = 0;
	m_cm.add<RenderComponent>(m_entity, { RenderKind::Model, id, kNoGeneration });

	auto& rc = m_cm.get<RenderComponent>(m_entity);
	SetMaterial(rc, t.materialName.c_str());

	// ---- 大きさ ----
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	GameScale::ApplyToTransform(tc);
	tc.SetScale(tc.GetScale() * t.bodyScale);

	// ---- ライト ----
	auto& lc = m_cm.add<LightComponent>(m_entity);
	lc.color = t.lightColor;
	lc.range = t.lightRange;
	lc.power = t.lightPower;
	lc.offset = t.lightOffset;
	lc.enabled = true;

	// 明滅の中心。マネージャが配置ごとの値で SetLightPower し直す
	m_lightPower = t.defaultGlowPower;

	// 個体ごとに開始位置をずらす。全員が同じ瞬間に一番明るくならないように。
	m_glowPhase = KdRandom::GetFloat(0.0f, DirectX::XM_2PI);
}

void Mushroom::Update()
{
	m_timer += static_cast<float>(Time::Instance().GetDeltaTime());

	if (!m_cm.has<LightComponent>(m_entity)) return;

	auto& lc = m_cm.get<LightComponent>(m_entity);
	if (!lc.enabled) return;

	const float t = m_timer + m_glowPhase;

	// 2つの波の平均。振れ幅は ±pulseDepth に収まる。
	const float wave =
		(std::sin(t * m_tuning.pulseSpeedA) + std::sin(t * m_tuning.pulseSpeedB)) * 0.5f;

	lc.power = m_lightPower * (1.0f + m_tuning.pulseDepth * wave);
}

// ===========================================================
// 接地
// ===========================================================
void Mushroom::SnapToGround()
{
	auto world = m_wpWorld.lock();
	if (!world) return;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	const Math::Vector3 pos = tc.GetPosition();

	// 今いる高さからではなく、十分上から落とす。
	// 足元から撃つと、既に地面へ埋まっている時に何も拾えない。
	const Math::Vector3 from = pos + Math::Vector3::Up * m_tuning.groundRayUp;
	const Math::Vector3 to = from - Math::Vector3::Up * m_tuning.groundRayLength;

	const auto hit = world->RaycastStage(from, to);
	if (!hit.isHit) return;	// 床が無い場所では動かさない

	tc.SetPosition(Math::Vector3(pos.x, hit.hitpos.y + m_groundOffset, pos.z));
}

Math::Vector3 Mushroom::GetPosition() const
{
	return m_cm.get<TransformComponent>(m_entity).GetPosition();
}

void Mushroom::SetPosition(const Math::Vector3& _pos)
{
	m_cm.get<TransformComponent>(m_entity).SetPosition(_pos);
}

void Mushroom::SetBodyScale(float _scale)
{
	m_bodyScale = _scale;

	auto& tc = m_cm.get<TransformComponent>(m_entity);
	GameScale::ApplyToTransform(tc);
	tc.SetScale(tc.GetScale() * m_tuning.bodyScale * _scale);

	// 原点が足元でないモデルだと、拡縮で足が地面から離れる
	SnapToGround();
}

void Mushroom::SetYaw(float _radian)
{
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetRotation(
		Math::Quaternion::CreateFromAxisAngle(Math::Vector3::Up, _radian));
}

// ===========================================================
// ライト
// ===========================================================
void Mushroom::SetLightPower(float _power)
{
	m_lightPower = std::max(_power, 0.0f);

	if (!m_cm.has<LightComponent>(m_entity)) return;
	m_cm.get<LightComponent>(m_entity).power = m_lightPower;
}

void Mushroom::SetLightRange(float _range)
{
	if (!m_cm.has<LightComponent>(m_entity)) return;
	m_cm.get<LightComponent>(m_entity).range = std::max(_range, 0.0f);
}

void Mushroom::SetLightEnable(bool _on)
{
	if (!m_cm.has<LightComponent>(m_entity)) return;
	m_cm.get<LightComponent>(m_entity).enabled = _on;
}