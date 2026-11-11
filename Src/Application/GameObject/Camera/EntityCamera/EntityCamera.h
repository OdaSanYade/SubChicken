#pragma once
#include "../CameraBase.h"

class EnemyManager;

//==========================================================
// EntityCamera
// ゲーム中のプレイヤー追従カメラ。
// 値は CameraTuning.json の "EntityCamera" 区画から Init で読む。
//==========================================================
class EntityCamera : public CameraBase 
{
public:

	using CameraBase::CameraBase;

	EntityCamera() = default;
	~EntityCamera() override;

	void Init() override;

	void Update() override;

	std::string GetID() override { return "EntityCamera"; }


private:

	void SetKeyAction();

	EntityCameraTuning m_tuning;
	int m_effectID = -1;
};