#pragma once
#include "../CameraBase.h"

class EnemyManager;

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

	EntityCameraTuning m_tuning;
	int m_effectID = -1;
};