#pragma once
#include "../CameraBase.h"

// ===========================================================
// TitleCamera
//
// タイトル画面のカメラ。
//
// CameraBase::Update() は「プレイヤーを追い、入力で回す」前提なので、
// ここでは呼ばずに軌道を自分で決める。
// PostUpdate() は遮蔽・シェイク・GPU転送をまとめているのでそのまま使う。
//
// 【方針】
// ゆっくり動かし続ける。止まった絵はスクリーンショットに見えるが、
// 動いていると「いつでも遊べる状態」に見える。
//
// ただし速く動かすと、UIを読んでいる間に画が変わって落ち着かない。
// 一周に1分近くかけるくらいで丁度いい。
// ===========================================================
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

	// 調整値(CameraTuning.json の "TitleCamera" 区画)
	TitleCameraTuning m_tuning;
};