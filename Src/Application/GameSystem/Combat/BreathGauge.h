#pragma once
#include "../../Inc/Math/Damp.h"	
#include "CombatFx.h"

class BreathGauge
{
public:

	static constexpr float kMax = 100.0f;
	static constexpr float kShortCost = 0.0f;	
	static constexpr float kFullCost = kMax;
	static constexpr float kFullEpsilon = 0.5f;
	static constexpr float kHeavyCost = 4.0f;

	void Update()
	{
		const float dt = CombatFx::Instance().RealDelta();

		// 命中で溜める
		for (const auto& ev : CombatFx::Instance().Events())
		{
			if (ev.attackerType != Collision::ObjectType::PlayerAttack) continue;
			Add(ev.damage * kGainPerDamage);
		}

		m_value = std::min(m_value + kIdleRegen * dt, kMax);

		// 表示用の追従
		if (m_display > m_value)
		{
			m_display = std::max(m_value, m_display - kDrainPerSec * dt);
		}
		else
		{
			m_display = Damp::Value(m_display, m_value, kDisplayLambda, dt);
		}
		m_flash = std::max(0.0f, m_flash - dt / kFlashSec);
	}

	//==================== 回避による回復 ====================
	void OnDodge() { Add(kDodgeGain); }
	void OnPerfect() { Add(kPerfectGain); }

	bool CanShort() const { return m_value >= kShortCost; }
	bool CanFull()  const { return m_value >= kFullCost; }

	// 消費できたら true、足りなければ何もせず false
	bool Consume(float _cost)
	{

		if (_cost >= kFullCost)
		{
			if (!IsFull()) return false;

			m_value = 0.0f;
			m_flash = 1.0f;
			return true;
		}

		if (m_value + kFullEpsilon < _cost) return false;

		m_value = std::max(0.0f, m_value - _cost);
		m_flash = 1.0f;
		return true;
	}

	void Reset() { m_value = 0.0f; m_display = 0.0f; m_flash = 0.0f; }

	float GetRatio()        const { return std::clamp(m_value / kMax, 0.0f, 1.0f); }
	float GetDisplayRatio() const { return std::clamp(m_display / kMax, 0.0f, 1.0f); }
	float GetShortRatio()   const { return 0.0f; }
	float GetFlash()        const { return m_flash; }
	void Drain(float _amount) { m_value = std::max(0.0f, m_value - _amount); }
	bool IsFull() const { return m_value >= kMax - kFullEpsilon; }

private:

	void Add(float _amount)
	{
		const float before = m_value;
		m_value = std::min(m_value + _amount, kMax);

		// 満タンになった瞬間だけ光らせる
		if (before < kMax && m_value >= kMax) m_flash = 1.0f;
	}

	static constexpr float kDisplayLambda = 9.0f;
	static constexpr float kFlashSec = 0.4f;
	static constexpr float kGainPerDamage = 0.06f;	// ダメージ1につき溜まる量
	static constexpr float kIdleRegen = 0.0f;	// 毎秒の自然回復
	static constexpr float kDodgeGain = 0.0f;	// 通常の回避
	static constexpr float kPerfectGain = 15.0f;	// ジャスト回避
	static constexpr float kDrainPerSec = 55.0f;

	float m_value = 0.0f;
	float m_display = 0.0f;
	float m_flash = 0.0f;

private:
	BreathGauge() = default;
	~BreathGauge() = default;
public:
	static BreathGauge& Instance()
	{
		static BreathGauge instance;
		return instance;
	}
};