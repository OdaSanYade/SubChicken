#pragma once

class PhaseShader;
class PhaseSystem
{
public:

	enum class State : int
	{
		Prepare = 0,
		Wave,
		Finished,		
	};

	struct Hooks
	{
		std::function<void(int)>	   SpawnEnemy;
		std::function<bool()>          PlayerAlive;
	};

	Hooks& GetHooks() { return m_hooks; }

	// 初期化
	void Init();

	// シーンの開始時に1回。
	void Reset();

	// 毎フレーム1回
	void Update();

	// フェーズUI描画
	void Draw();

	// フェーズの経過時間
	float GetElapsed()    const { return m_elapsed; }


private:

	void EnterPhase(int _index);

	std::string PhaseNames = "PhaseFIRST";
	std::shared_ptr<KdTexture> m_phaseTex;
	std::shared_ptr<PhaseShader> m_phaseShader;

	static constexpr float kPrepareSec = 2.0f;
	static constexpr float kInterludeSec = 3.5f;
	static constexpr float kPhaseHealRate = 0.25f;
	static constexpr float kClearDelaySec = 1.2f;

	Hooks   m_hooks;
	State   m_state = State::Prepare;

	int   m_phaseIndex = 0;
	float m_stateTimer = 0.0f;
	float m_elapsed = 0.0f;

private:
	PhaseSystem() = default;
	~PhaseSystem() = default;
public:
	static PhaseSystem& Instance()
	{
		static PhaseSystem instance;
		return instance;
	}
};

#define PHASE PhaseSystem::Instance()