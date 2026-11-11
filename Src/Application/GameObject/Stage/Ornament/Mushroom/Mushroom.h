#pragma once
#include "../../../IObject.h"
#include "../../Stage.h"	// StageTuningKey

// キノコ1本の見た目と光り方(StageTuning.json の "Mushroom" 区画)
struct MushroomTuning
{
	std::string modelName = "Mushroom";
	std::string materialName = "MushroomDefault";
	float bodyScale = 2.5f;

	// 接地。既定でわずかに埋める。ぴったり0だと斜面で足の縁が浮いて見える
	float groundOffset = -0.05_m;
	float groundRayUp = 30.0_m;			// この高さから下へ撃つ(埋まっていても拾えるように)
	float groundRayLength = 100.0_m;

	// ライト。生成直後の値(実際の明るさと範囲はマネージャが配置ごとに入れ直す)
	Math::Vector3 lightColor = { 0.05f, 0.1f, 0.3f };
	float lightRange = 0.1f;
	float lightPower = 0.01f;
	Math::Vector3 lightOffset = { 0.0f, 2.0f, 0.0f };

	// SetLightPower が呼ばれなかったときの明滅の中心
	float defaultGlowPower = 1.6f;

	// 明滅。周期の違う2つの波を重ねる。1つだけだと機械的な点滅に見える
	float pulseSpeedA = 1.7f;
	float pulseSpeedB = 0.9f;
	float pulseDepth = 0.18f;			// 光量の揺れ幅

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("modelName", modelName);
		v("materialName", materialName);
		v("bodyScale", bodyScale);
		v("groundOffset", groundOffset, Unit::Meter);
		v("groundRayUp", groundRayUp, Unit::Meter);
		v("groundRayLength", groundRayLength, Unit::Meter);
		v("lightColor", lightColor);
		v("lightRange", lightRange);
		v("lightPower", lightPower);
		v("lightOffset", lightOffset);
		v("defaultGlowPower", defaultGlowPower);
		v("pulseSpeedA", pulseSpeedA);
		v("pulseSpeedB", pulseSpeedB);
		v("pulseDepth", pulseDepth);
	}
};

// 光るだけの装飾物。当たり判定もステートも持たない。
// 位置と見た目の個体差はマネージャが生成時に決める。
class Mushroom : public IObject
{
public:

	using IObject::IObject;

	Mushroom() = default;
	~Mushroom() = default;

	void Init() override;
	void Update() override;

	std::string GetID() override { return "Mushroom"; }

	// コンポーネントの中身への参照を返すと、ECS の再配置で宙に浮くので値で返す
	Math::Vector3 GetPosition() const;
	void SetPosition(const Math::Vector3& _pos);

	// 見た目の個体差。Init の後に呼ぶ。
	void SetBodyScale(float _scale);
	void SetYaw(float _radian);

	//------------------------------------------
	// 接地
	//------------------------------------------
	// 今のXZで地面を拾い直して足を着ける。大きさを変えた後に呼ぶこと。
	void SnapToGround();

	// 地面へのめり込ませ量。マイナスで埋まる。斜面で足元が浮く時に使う。
	void SetGroundOffset(float _offset) { m_groundOffset = _offset; SnapToGround(); }

	// 調整値を読む。Init より前に1回だけ呼ばれる(マネージャが全員ぶんまとめて読んで渡す)
	void SetTuning(const MushroomTuning& _tuning) { m_tuning = _tuning; m_groundOffset = _tuning.groundOffset; }
	float GetGroundOffset() const { return m_groundOffset; }

	//------------------------------------------
	// ライト
	//------------------------------------------
	// 明るさ。明滅はこの値を中心に揺れる。
	void  SetLightPower(float _power);
	float GetLightPower() const { return m_lightPower; }

	void  SetLightRange(float _range);

	// 明滅の位相。群生させた時に全部が同じ呼吸をしないようずらす。
	void SetGlowPhase(float _phase) { m_glowPhase = _phase; }

	void SetLightEnable(bool _on);

private:

	MushroomTuning m_tuning;

	float m_glowPhase = 0.0f;
	float m_timer = 0.0f;
	float m_bodyScale = 1.0f;

	float m_lightPower = 0.0f;		// 明滅の中心になる明るさ(SetLightPower で入る)
	float m_groundOffset = 0.0f;	// SetTuning / SetGroundOffset で入る
};
