#pragma once
#include "../../UI/Title/TitleHud.h"
#include "../../UI/Title/CreditHud.h"
#include"../BaseScene/BaseScene.h"

class TitleScene : public BaseScene
{
public :

	TitleScene()  { Init(); }
	~TitleScene()override {}

	void Update()override;

	void DrawSprite()override;
	void DrawDebug() override;

	void Enter() override;
	void Exit() override;

private :

	void Init()  override;

	// 決定した項目ごとの行き先
	void Leave(TitleHud::Choice _c);

	std::shared_ptr<CollisionWorld> m_collisionWorld;

	// タイトルの中で開く画面。シーンは切り替えず、3Dの農場とBGMを残したまま重ねる。
	enum class Mode { Menu, Credit };
	Mode m_mode = Mode::Menu;

	CreditHud m_credit;

	//ロゴアニメーション
	bool  m_leaving = false;
	float m_leaveTimer = 0.0f;
	TitleHud::Choice m_choice = TitleHud::ChoiceStart;

	static constexpr float kLeaveSec = 0.7f;
};
