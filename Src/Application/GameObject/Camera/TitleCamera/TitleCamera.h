#pragma once
#include "../CameraBase.h"

class TitleCamera : public CameraBase
{
public:

	using CameraBase::CameraBase;

	TitleCamera() = default;
	~TitleCamera() override = default;

	void Init() override;
	void Update() override;

	std::string GetID() override { return "TitleCamera"; }

	// 見回す中心。寝ているニワトリの位置を渡す。
	void SetPivot(const Math::Vector3& _p) { m_pivot = _p; }

	// 決定した瞬間に呼ぶ。ゆっくり寄って、そのまま戦闘へ繋ぐ。
	void BeginLeave() { m_leaving = true; }

private:

	Math::Vector3 m_pivot = Math::Vector3::Zero;

	float m_time = 0.0f;

	bool  m_leaving = false;
	float m_leaveT = 0.0f;

	TitleCameraTuning m_tuning;
};