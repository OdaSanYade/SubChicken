#include "TitleScene.h"
#include "../../UI/Title/TitleHud.h"

//カメラ
#include "../../GameObject/Camera/CameraManager.h"
#include "../../GameObject/Camera/TitleCamera/TitleCamera.h"

//ゲームオブジェクト
#include "../../GameObject/Stage/Stage.h"

#include "../SceneManager.h"
#include "../../main.h"

void TitleScene::Update()
{

	BaseScene::Update();

	m_collisionWorld->Update();

	//------------------------------------------
	// クレジット
	//------------------------------------------
	// 開いている間はタイトルの入力を止める。
	// 閉じ切ったら、ロゴの登場からやり直してカーソルを Credit に戻す。
	if (m_mode == Mode::Credit)
	{
		m_credit.Update();

		if (m_credit.IsFinished())
		{
			m_mode = Mode::Menu;
			TITLE_UI.Resume(TitleHud::ChoiceCredit);
		}
		return;
	}

	TITLE_UI.Update();

	// ---- 遷移 ----
	// ロゴが剥がれ切るのを待ってから次へ行く。
	if (!m_leaving) return;

	m_leaveTimer += static_cast<float>(Time::Instance().GetDeltaTime());
	if (m_leaveTimer < kLeaveSec) return;

	Leave(m_choice);
}

void TitleScene::Leave(TitleHud::Choice _c)
{
	if (_c == TitleHud::ChoiceCredit)
	{
		// シーンは変えない。剥がれたロゴの跡に夜空の幕を下ろす。
		m_leaving = false;
		m_mode = Mode::Credit;
		m_credit.Open();
	}
	else if (_c == TitleHud::ChoiceStart)
	{
		SceneManager::Instance().SetNextScene(SceneManager::SceneType::Game);
	}
	else
	{
		Application::Instance().End();
	}
}

void TitleScene::DrawSprite()
{
	if (m_mode == Mode::Credit)
		m_credit.Draw();
	else
		TITLE_UI.Draw();
}

void TitleScene::DrawDebug()
{
	BaseScene::DrawDebug();
}

void TitleScene::Enter()
{
	BaseScene::Enter();

	auto& om = ObjectManager::Instance();

	//コリジョンシステム
	m_collisionWorld = std::make_shared<CollisionWorld>();

	//オブジェクト生成
	auto camera = std::make_shared<TitleCamera>(cm, m_collisionWorld);
	om.Register(camera);

	auto stage = std::make_shared<Stage>(cm, m_collisionWorld);
	om.Register(stage);

	camera->SetPivot(Math::Vector3(8.0f,-0.5f,-4.0f));

	CameraManager::Instance().RegisterGameCamera(camera);
	m_cameraName = "TitleCamera";

	m_leaving = false;
	m_leaveTimer = 0.0f;
	m_mode = Mode::Menu;

	TITLE_UI.Init();
	TITLE_UI.SetDecideCallback(
		[this](TitleHud::Choice _c)
		{
			m_leaving = true;
			m_leaveTimer = 0.0f;
			m_choice = _c;
		});

	m_credit.Init();

	//ステージのあたり判定登録
	if (auto sp = om.GetWeak<Stage>("Stage").lock()) {
		const StageData data = sp->GetStageData();
		m_collisionWorld->SetStage(data.m_model, data.m_mat,
			[](std::string_view nodeName) -> uint32_t { return Collision::Tag_None; });
	}

	m_bgm = AudioManager::Instance().Play("Title");
	if (m_bgm) m_bgm->FadeIn(0.5f);
}

void TitleScene::Exit()
{
	BaseScene::Exit();
	TITLE_UI.Release();
	m_credit.Release();
}

void TitleScene::Init()
{
}
