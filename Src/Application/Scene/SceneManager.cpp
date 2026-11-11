#include "SceneManager.h"
#include "../GameSystem/Transition/Transition.h"

#include "BaseScene/BaseScene.h"
#include "TitleScene/TitleScene.h"
#include "GameScene/GameScene.h"

SceneManager::SceneManager()
{
}
void SceneManager::Init()
{
	//シーンの登録
	m_sceneMap = {
		{SceneType::Title,std::make_shared<TitleScene>()},
		{SceneType::Game,std::make_shared<GameScene>()},
	};

	{
		bool ok = TRANSITION.Init();
		if (!ok) 
		{
			OutputDebugStringA("[Transition] ERROR_TRANSITION");
		}
	}

	// 開始シーンに切り替え
	ChangeScene();
}

void SceneManager::PreUpdate()
{
	// シーン切替
	if (m_isChangeRequest)ChangeScene();

	m_currentScene->PreUpdate();
}

void SceneManager::Update()
{
	m_currentScene->Update();
}

void SceneManager::PostUpdate()
{
	m_currentScene->PostUpdate();
	TRANSITION.Update(Time::Instance().GetUnScaledDeltaTime());
}

void SceneManager::PreDraw()
{
	m_currentScene->PreDraw();
}

void SceneManager::Draw()
{
	m_currentScene->Draw();
}

void SceneManager::DrawSprite()
{
	m_currentScene->DrawSprite();
	TRANSITION.Draw();
}

void SceneManager::DrawDebug()
{
	m_currentScene->DrawDebug();
}

void SceneManager::Release()
{
	if (m_currentScene)m_currentScene->Exit();
	m_currentScene = nullptr;
	m_sceneMap.clear();
}

void SceneManager::SetNextScene(SceneType _nextScene)
{
	TRANSITION.Start(1.0f, [this, _nextScene]()
		{
			m_nextSceneType = _nextScene;
			m_isChangeRequest = true;
		});
}

void SceneManager::ChangeScene()
{
	auto it = m_sceneMap.find(m_nextSceneType);
	if (it == m_sceneMap.end())
	{
		OutputDebugStringA("[SceneManager] 未登録のシーンが指定されました\n");
		m_isChangeRequest = false;
		return;
	}

	//次のシーンを作成し、現在のシーンにする
	//現在のシーンの後始末
	if (m_currentScene)m_currentScene->Exit();

	//シーンの変更＆初期化
	m_currentScene = m_sceneMap[m_nextSceneType];
	m_currentScene->Enter();

	m_nowSceneType = m_nextSceneType;

	m_isChangeRequest = false;
}
