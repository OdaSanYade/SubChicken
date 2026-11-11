#pragma once

#include "../../GameObject/ObjectManager.h"

static kd::ecs::Entity g_selected;

class ShaderRender;

class BaseScene
{
public :

	BaseScene();
	virtual ~BaseScene() {}

	virtual void PreUpdate();
	virtual void Update();
	virtual void PostUpdate();

	virtual void PreDraw();
	virtual void Draw();
	virtual void DrawSprite();
	virtual void DrawDebug();

	virtual void Enter();
	virtual void Exit();

protected :

	// 継承先シーンで必要ならオーバーライドする
	virtual void Event();
	virtual void Init();

	kd::ecs::ComponentManager cm;

	std::shared_ptr<ShaderRender> m_shaderRender;
	std::string m_cameraName = "";

	//BGM
	std::shared_ptr<SoundInstance> m_bgm;
};
