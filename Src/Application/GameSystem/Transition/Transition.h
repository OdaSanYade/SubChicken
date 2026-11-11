#pragma once
#include "Render/TransitionShader.h"

class Transition
{
public:

	// --- 初期化 ---
	bool Init();
	void Release();

	// --- カバートランジション開始 ---
	bool Start(float duration, std::function<void()> onSwitch, float switchPoint = 0.5f);

	// 遷移が完全に終了した時
	void SetOnComplete(std::function<void()> onComplete) { m_onComplete = onComplete; }

	// 毎フレーム呼び出す
	void Update(float deltaTime);

	// シーンの描画がすべて終わった後（バックバッファに一番手前として）呼ぶ
	void Draw();

	// 遷移を即座に中断する
	void Cancel();

	bool  IsTransitioning() const { return m_isTransitioning; }
	float GetProgress()     const { return m_progress; }

	// 0.0 = 画面が見えている / 1.0 = 完全に覆われている
	float GetCoverAmount()  const;

private:

	void ResetState();

	TransitionShader m_shader;
	std::shared_ptr<KdTexture> m_tex;

	bool  m_isTransitioning = false;
	float m_progress = 0.0f;	// 0.0〜1.0
	float m_duration = 1.0f;
	float m_switchPoint = 0.5f;
	bool  m_switchFired = false;

	std::function<void()> m_onSwitch = nullptr;
	std::function<void()> m_onComplete = nullptr;

private:
	Transition() = default;
	~Transition() = default;

	Transition(const Transition&) = delete;
	Transition& operator=(const Transition&) = delete;

public:
	static Transition& Instance()
	{
		static Transition instance;
		return instance;
	}
};

#define TRANSITION Transition::Instance()