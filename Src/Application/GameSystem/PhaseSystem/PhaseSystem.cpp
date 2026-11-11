#include "PhaseSystem.h"
#include "Shader/PhaseShader.h"

void PhaseSystem::Init()
{
	m_phaseTex = RES_MGR.GetTexList()->GetTex(PhaseNames);
	

	m_phaseShader = std::make_shared<PhaseShader>();
	if (!m_phaseShader->Init()) 
	{
		OutputDebugStringA("[Phase] ShaderInitError");
	}
	if (m_phaseShader)
		m_phaseShader->SetPhaseNotice(0, m_phaseTex->WorkSRView());

	EnterPhase(0);
}

void PhaseSystem::Reset()
{
	m_state = State::Prepare;
	m_phaseIndex = 0;
	m_stateTimer = 0.0f;
	m_elapsed = 0.0f;
}

void PhaseSystem::Update()
{
	const float dt = Time::Instance().GetUnScaledDeltaTime();

	m_stateTimer += dt;
	if (m_state != State::Finished) m_elapsed += dt;
}

void PhaseSystem::Draw()
{
	if (!m_phaseShader)return;
	m_phaseShader->Draw();
}

void PhaseSystem::EnterPhase(int _index)
{
	m_phaseIndex = _index;

	m_state = State::Wave;
	m_stateTimer = 0.0f;

	if (m_hooks.SpawnEnemy) m_hooks.SpawnEnemy(m_phaseIndex);
}