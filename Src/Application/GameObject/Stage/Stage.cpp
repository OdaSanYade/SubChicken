#include "Stage.h"

void Stage::Init()
{
	Tuning::Load(m_tuning, StageTuningKey::kFile, "Stage");

	// ---- モデル ----
	m_spModel = RES_MGR.GetModelList()->GetModel(m_tuning.modelName);
	const ModelId id = RES_MGR.GetModelList()->GetModelId(m_tuning.modelName);

	// 静的モデルなので Work は作らない(世代番号は使わないので 0)
	constexpr uint32_t kNoGeneration = 0;
	m_cm.add<RenderComponent>(m_entity, { RenderKind::Static, id, kNoGeneration });

	// ---- 置き方 ----
	// 位置と大きさは縮尺前の値で入れてから、まとめて縮尺を掛ける
	auto& tc = m_cm.get<TransformComponent>(m_entity);
	tc.SetScale(m_tuning.scale);
	tc.SetPosition(m_tuning.position);
	GameScale::ApplyToTransform(tc);

	// ---- ステージ全体を照らすライト ----
	auto& lc = m_cm.add<LightComponent>(m_entity);
	lc.enabled = true;
	lc.color = m_tuning.lightColor;
	lc.power = m_tuning.lightPower;
	lc.offset = m_tuning.lightOffset;
}

StageData Stage::GetStageData()
{
	return StageData{
		m_spModel,
		m_cm.get<TransformComponent>(m_entity).GetWorldMatrix()
	};
}
