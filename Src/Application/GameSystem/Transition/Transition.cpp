#include "Transition.h"

namespace
{
	ID3D11Device* GetDevice() { return KdDirect3D::Instance().WorkDev(); }
	ID3D11DeviceContext* GetContext() { return KdDirect3D::Instance().WorkDevContext(); }
}

bool Transition::Init()
{
	m_tex = RES_MGR.GetTexList()->GetTex("Transition");
	return m_shader.Init();
}

void Transition::Release()
{
	ResetState();
	m_onComplete = nullptr;
	m_shader.Release();
}

bool Transition::Start(float duration, std::function<void()> onSwitch, float switchPoint)
{
	// 遷移中の再要求は無視する
	if (m_isTransitioning) return false;

	ResetState();

	m_onSwitch = onSwitch;
	m_switchPoint = std::clamp(switchPoint, 0.0f, 1.0f);
	m_duration = (duration > 0.0f) ? duration : 0.0001f;
	m_isTransitioning = true;

	return true;
}

void Transition::Update(float deltaTime)
{
	if (!m_isTransitioning) return;

	m_progress = std::min(m_progress + deltaTime / m_duration, 1.0f);

	// --- 画面を覆い切った瞬間に一度だけシーンを切り替える ---
	if (!m_switchFired && m_progress >= m_switchPoint)
	{
		m_switchFired = true;

		if (m_onSwitch)
		{
			auto cb = m_onSwitch;
			m_onSwitch = nullptr;
			cb();
		}
	}

	// --- 遷移完了 ---
	if (m_progress >= 1.0f)
	{
		m_isTransitioning = false;

		if (m_onComplete)
		{
			auto cb = m_onComplete;
			m_onComplete = nullptr;
			cb();
		}
	}
}

void Transition::Draw()
{
	if (!m_isTransitioning) return;

	// 覆い具合の計算はシェーダ側が持っているので、生の値をそのまま渡す
	m_shader.Draw(GetContext(),m_tex->WorkSRView(), m_progress, m_switchPoint);
}

void Transition::Cancel()
{
	ResetState();
}

void Transition::ResetState()
{
	m_isTransitioning = false;
	m_progress = 0.0f;
	m_switchFired = false;
	m_onSwitch = nullptr;
	
}