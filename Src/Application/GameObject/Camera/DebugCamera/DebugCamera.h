#pragma once
#include "../CameraBase.h"

//==========================================================
// DebugCamera
// デバッグ用のフリーフライカメラ。WASD+QE で移動、ホイールで画角。
// 初期値は CameraTuning.json の "DebugCamera" 区画。Set〜 で実行中に変えられる。
//==========================================================
class DebugCamera : public CameraBase
{
public:

	using CameraBase::CameraBase;

	DebugCamera() = default;
	~DebugCamera() override = default;

	void Init() override;
	void Update() override;
	void PostUpdate() override;

	std::string GetID() override { return "DebugCamera"; }

	// 有効化時に現在の描画カメラ姿勢を引き継ぐ(CameraManagerが呼ぶ)
	void SyncPoseFromWorldMatrix(const Math::Matrix& _world) override;

	// パラメータ調整
	void SetMoveSpeed(float _unitsPerSec) { m_moveSpeed = _unitsPerSec; }
	void SetBoostScale(float _scale) { m_boostScale = _scale; }
	void SetLookSpeed(float _radPerFrame) { m_lookSpeed = _radPerFrame; }
	void SetDefaultFovY(float _deg) { m_defaultFovDeg = _deg; }
	void SetWheelStep(float _degPerNotch) { m_wheelStep = _degPerNotch; }
	void SetMouseSensitivity(float _radPerCount) { m_mouseSensitivity = _radPerCount; }

private:

	// 入力(環境依存箇所をここに集約)
	Math::Vector2 ReadLookAxis()  const; // 視点回転(Look軸)
	Math::Vector3 ReadMoveInput() const; // x=右, y=上, z=前 の[-1,1]
	bool          IsSpeedBoost()  const; // Shift
	float         ReadWheelDelta();       // ホイール回転量(上+/下-) ※要確認
	bool          IsResetFovPressed();    // ホイールボタン押下エッジ

private:

	// 初期値(Init で JSON から読む)
	DebugCameraTuning m_tuning;

	// 姿勢(このクラスが保持)
	Math::Vector3 m_position = Math::Vector3::Zero;
	float m_yaw = 0.0f; // ラジアン
	float m_pitch = 0.0f; // ラジアン

	// FOV
	float m_fovDeg = 0.0f;
	float m_defaultFovDeg = 0.0f;
	bool  m_fovDirty = true;

	// パラメータ(Init で m_tuning から入る。Set〜 で上書きできる)
	float m_moveSpeed = 0.0f;   // units/sec
	float m_boostScale = 1.0f;  // Shift時の倍率
	float m_lookSpeed = 0.0f;   // rad/frame
	float m_wheelStep = 0.0f;   // 1ノッチあたりのFOV変化(deg)
	float m_fovMinDeg = 0.0f;
	float m_fovMaxDeg = 0.0f;
	float m_pitchLimit = 0.0f;
	float m_mouseSensitivity = 0.0f;
	// 中クリックのエッジ検出
	bool  m_prevResetDown = false;
};