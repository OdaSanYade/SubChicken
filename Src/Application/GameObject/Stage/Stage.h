#pragma once
#include "../IObject.h"
#include "../Config/Tuning.h"

// ステージ関連(Stage / Mushroom / MushroomManager)の調整値ファイル
namespace StageTuningKey
{
	inline constexpr const char* kFile = "StageTuning";
}

// ステージ本体の置き方と、ステージに付けるライト
// (StageTuning.json の "Stage" 区画)
struct StageTuning
{
	std::string   modelName = "Stage";
	Math::Vector3 scale = { 5.0f, 5.0f, 5.0f };
	Math::Vector3 position = { 10.0f, -6.0f, 5.0f };	// GameScale を掛ける前の値

	Math::Vector3 lightColor = { 0.3f, 0.2f, 0.0f };
	float         lightPower = 200.0f;
	Math::Vector3 lightOffset = { 3.0f, 2.0f, -3.5f };

	template<class V> void Visit(V& v)
	{
		v("modelName", modelName);
		v("scale", scale);
		v("position", position);
		v("lightColor", lightColor);
		v("lightPower", lightPower);
		v("lightOffset", lightOffset);
	}
};

struct StageData
{
	std::shared_ptr<KdModelData> m_model;
	Math::Matrix m_mat;
};

class Stage : public IObject 
{
public:

	using IObject::IObject;

	Stage() = default;
	~Stage()override = default;

	void Init()override;

	StageData GetStageData();

	std::string GetID()override { return "Stage"; }

private:
	
	StageTuning m_tuning;
	std::shared_ptr<KdModelData> m_spModel;

};