#pragma once
#include "../Collision/CollisionWorld.h"


class CombatFx
{
public:

	struct HitEvent
	{
		Math::Vector3 position;
		Math::Vector3 knockback;
		float         damage = 0.0f;
		uint32_t      tag = 0;
		uint32_t      attackId = 0;
		int           hitstopFrames = 0;
		bool          lethal = false;
		Collision::ObjectType attackerType{};
	};

	// 毎フレーム1回、シーンの更新の一番先頭で呼ぶ
	void Update()
	{
		// 前フレーム分を読み取り用へ移す
		m_readEvents.swap(m_events);
		m_events.clear();

		const float real = RealDelta();

		if (m_stopTimer > 0.0f)
		{
			m_stopTimer -= real;
			if (m_stopTimer <= 0.0f)
			{
				m_stopTimer = 0.0f;
				m_stopScale = 1.0f;
			}
		}

		if (m_slowRecovering)
		{
			// 実時間で線形に戻す
			m_slowScale += real / kSlowRecoverSec;
			if (m_slowScale >= 1.0f)
			{
				m_slowScale = 1.0f;
				m_slowRecovering = false;
			}
		}

		// 撃破の余韻
		if (m_killSlowTimer > 0.0f)
		{
			m_killSlowTimer -= real;
			if (m_killSlowTimer <= 0.0f)
			{
				m_killSlowTimer = 0.0f;
				m_slowRecovering = true;
			}
			else if (!m_slowRecovering)
			{
				m_slowScale = kFinalKillSlowScale;
			}
		}


		ApplyScale();
	}

	//==================== 命中イベント ====================
	void Notify(const HitEvent& _ev)
	{
		m_events.push_back(_ev);

		if (_ev.hitstopFrames > 0)
		{
			const float sec = static_cast<float>(_ev.hitstopFrames) / 60.0f;
			RequestHitStop(sec, kHitStopScale);

			// 止まっている時間ぶん震える。重い攻撃ほど長く強く。
			const float power = std::min(0.3f + sec * 2.0f, 1.0f);
			AddVibration(power, power * 0.8f, sec + 0.05f);
		}
	}

	// 前フレームに起きたヒット
	const std::vector<HitEvent>& Events() const { return m_readEvents; }

	//==================== ヒットストップ ====================
	void RequestHitStop(float _duration, float _scale = kHitStopScale)
	{
		const float scale = std::max(_scale, kMinScale);

		if (_duration > m_stopTimer) m_stopTimer = _duration;
		if (scale < m_stopScale || m_stopTimer <= 0.0f) m_stopScale = scale;

		ApplyScale();
	}
	bool IsHitStopping() const { return m_stopTimer > 0.0f; }

	//==================== スロー(持続) ====================
	void SetSlow(float _scale)
	{
		m_slowScale = std::max(_scale, kMinScale);
		m_slowRecovering = false;
		ApplyScale();
	}
	// 一瞬で戻すと段差が出るので、kSlowRecoverSec かけて 1.0 へ戻す
	void ClearSlow()
	{
		if (m_slowScale >= 1.0f) return;
		m_slowRecovering = true;
	}
	bool IsSlowing() const { return m_slowScale < 1.0f; }

	//==================== カメラシェイク ====================
	void AddTrauma(float _amount) { m_trauma = std::max(m_trauma, _amount); }
	float ConsumeTrauma()
	{
		const float t = m_trauma;
		m_trauma = 0.0f;
		return t;
	}

	//==================== 実時間 ====================
	float RealDelta() const
	{
		return Time::Instance().GetUnScaledDeltaTime();
	}

	//==================== 振動 ====================
	void AddVibration(float _left, float _right, float _sec)
	{
		// 強いものが勝つ。弱い振動で上書きしない。
		if (_left > m_vibLeft)  m_vibLeft = _left;
		if (_right > m_vibRight) m_vibRight = _right;
		if (_sec > m_vibSec)   m_vibSec = _sec;
	}
	bool ConsumeVibration(float& _outLeft, float& _outRight, float& _outSec)
	{
		if (m_vibSec <= 0.0f) return false;

		_outLeft = m_vibLeft;
		_outRight = m_vibRight;
		_outSec = m_vibSec;

		m_vibLeft = m_vibRight = m_vibSec = 0.0f;
		return true;
	}

	//==================== 撃破 ====================
	void RequestKillFx(bool _isLast)
	{
		RequestHitStop(_isLast ? kFinalKillStopSec : kKillStopSec, 0.03f);
		AddTrauma(_isLast ? 0.8f : 0.45f);
		AddVibration(1.0f, 0.9f, _isLast ? 0.4f : 0.25f);

		// 殲滅時だけ、止めたあとに余韻のスローを残す
		if (_isLast) m_killSlowTimer = kFinalKillSlowSec;
	}

	bool IsKillSlowing() const { return m_killSlowTimer > 0.0f; }

	//==================== 鳴き声 ====================
	void RequestCryFx(bool _isFull)
	{
		RequestHitStop(_isFull ? 0.30f : 0.08f, 0.03f);
		AddTrauma(_isFull ? 0.9f : 0.25f);
		AddVibration(_isFull ? 1.0f : 0.5f, _isFull ? 1.0f : 0.4f, _isFull ? 0.5f : 0.15f);
	}

private:

	void ApplyScale()
	{
		const float scale = (m_stopTimer > 0.0f) ? m_stopScale : m_slowScale;
		if (scale == m_appliedScale) return;

		m_appliedScale = scale;
		Time::Instance().SetTimeScale(scale);
	}

	// 完全な0にはしない
	static constexpr float kMinScale = 0.02f;
	static constexpr float kHitStopScale = 0.05f;
	static constexpr float kSlowRecoverSec = 0.18f;	// スローから通常へ戻す時間
	static constexpr float kKillStopSec = 0.20f;	// 通常の撃破
	static constexpr float kFinalKillStopSec = 0.42f;	// 最後の1体
	static constexpr float kFinalKillSlowSec = 0.9f;	// 殲滅後の余韻
	static constexpr float kFinalKillSlowScale = 0.35f;

	std::vector<HitEvent> m_events;		// 今フレーム書き込み中
	std::vector<HitEvent> m_readEvents;	// 前フレーム分(読み取り用)

	float m_stopTimer = 0.0f;
	float m_stopScale = 1.0f;
	float m_slowScale = 1.0f;
	float m_appliedScale = 1.0f;
	float m_trauma = 0.0f;
	float m_vibLeft = 0.0f, m_vibRight = 0.0f, m_vibSec = 0.0f;
	float m_killSlowTimer = 0.0f;
	bool  m_slowRecovering = false;

private:
	CombatFx() = default;
	~CombatFx() = default;
public:
	static CombatFx& Instance()
	{
		static CombatFx instance;
		return instance;
	}
};