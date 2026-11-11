#pragma once

#include "../../UI/Result/ResultHud.h"
#include"../BaseScene/BaseScene.h"

class ObjectManager;

class CollisionWorld;
class LineDebugDraw;

class GameScene : public BaseScene
{
public :

	GameScene()  { Init(); }
	~GameScene() override;

	void Update()override;

	void DrawSprite()override;
	void DrawDebug() override;

	void Enter() override;
	void Exit() override;

private:

	void Init()  override;

	void CheckBattleEnd();

	std::shared_ptr<CollisionWorld> m_collisionWorld;
	std::shared_ptr<LineDebugDraw>  m_lineDebug;

	//リザルト
	bool  m_leaving = false;
	float m_leaveTimer = 0.0f;
	ResultHud::Choice m_choice = ResultHud::ChoiceRetry;

	//フェーズ
	float m_phaseBanner = 0.0f;
	static constexpr float kPhaseBannerSec = 2.0f;
	static constexpr const char* kPhaseWords[] = { "FIRST", "SECOND", "FINAL" };

	float m_gameTime = 0.0f;

	bool m_battleEnded = false;
};