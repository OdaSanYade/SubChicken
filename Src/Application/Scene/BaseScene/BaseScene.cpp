#include "BaseScene.h"

//描画システム
#include "../../GameSystem/ShaderSystem/RenderHelper.h"
#include "../../GameSystem/ShaderSystem/ShaderRender.h"
#include "../../GameObject/Camera/CameraBase.h"

BaseScene::BaseScene()
{
	Init();
}

void BaseScene::PreUpdate()
{
	ObjectManager::Instance().PreUpdateAll();
}

void BaseScene::Update()
{
	ObjectManager::Instance().UpdateAll();

	if (auto sp = ObjectManager::Instance().GetWeak<CameraBase>(m_cameraName).lock()) {
		auto camera = sp->GetCamera();
		m_shaderRender->Update(camera->GetCameraMatrix().Translation());
	}
}

void BaseScene::PostUpdate()
{
	ObjectManager::Instance().PostUpdateAll();
}

void BaseScene::PreDraw()
{
}

void BaseScene::Draw()
{
	//シェーダ描画
	Render::UpdateAll(cm, Time::Instance().GetDeltaTime(), *m_shaderRender);
	if (auto sp = ObjectManager::Instance().GetWeak<CameraBase>(m_cameraName).lock()) {
		auto camera = sp->GetCamera();
		m_shaderRender->Render(camera);
	}
}

void BaseScene::DrawSprite()
{
}

void BaseScene::DrawDebug()
{
	m_shaderRender->DrawDebug();
}

void BaseScene::Enter()
{
	AudioManager::Instance().SetMasterVolume(0.01f);
	AudioManager::Instance().SetBgmVolume(0.8f);
	AudioManager::Instance().SetSeVolume(1.6f);
}

void BaseScene::Exit()
{
	GPUParticleSystem::Instance().StopAllEffects();
	Time::Instance().SetTimeScale(1.0f);
	ObjectManager::Instance().UnRegister();
	KdInputActionSystem::Instance().ClearListeners();
	m_bgm->FadeOut(1.0f);
}

void BaseScene::Event()
{
}

void BaseScene::Init()
{
	// 起動時のロードタスクで生成済みのものを受け取る
	m_shaderRender = ShaderRenderSystem::Get();
}
