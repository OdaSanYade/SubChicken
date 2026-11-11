#pragma once
#include "../IObject.h"
#include "CameraTuning.h"

//==========================================================
// CameraBase
// 追従・視点操作・遮蔽・シェイク・フォーカス演出をまとめたカメラの土台。
// 調整値は CameraTuning.json の "CameraBase" 区画から読む(コンストラクタで1回)。
// 派生カメラは Init で Set〜 を呼んで自分用の値に上書きする。
//==========================================================
class CameraBase : public IObject
{
public:

	CameraBase(kd::ecs::ComponentManager& cm, const std::shared_ptr<CollisionWorld>& world);
	virtual ~CameraBase() override = default;

	virtual void Update() override;
	virtual void PostUpdate() override;

	const std::shared_ptr<KdCamera>& GetCamera() { return m_camera; }
	virtual std::string GetID() override { return "Camera"; }
	void SetTarget(const kd::ecs::Entity& _target)
	{
		m_targetEntity = _target;
		m_hasTarget = true;
		ResetPivot();	// 別対象へ乗り換えた瞬間に補間が走らないようにする
	}

	//==================== ターゲットプル用(CameraComponent::UpdateTargetPullへ渡す敵候補) ====================
	void SetPullCandidatesProvider(std::function<std::vector<CameraComponent::PullCandidate>()> _provider) { m_pullCandidatesProvider = std::move(_provider); }

	//==================== 遮蔽プローブ ====================
	void SetOcclusionProbe(float _radius, float _minDistance)
	{
		m_occlusionProbeRadius = _radius;
		m_occlusionMinDistance = _minDistance;
	}

	//==================== マウス視点 ====================
	// 1カウントあたりの回転量(rad)。dtは掛けない(マウスは「動かした量=回る量」)
	void SetMouseSensitivity(float _radPerCount) { m_mouseSensitivity = _radPerCount; }
	void SetMouseInvertY(bool _invert) { m_mouseInvertY = _invert; }


	//==================== 視点操作 ====================
	// 角速度はrad/秒で持つ(フレームレート非依存)
	void SetLookSpeed(float _yawRadPerSec, float _pitchRadPerSec)
	{
		m_yawSpeed = _yawRadPerSec; m_pitchSpeed = _pitchRadPerSec;
	}
	void SetLookDeadZone(float _dz) { m_lookDeadZone = _dz; }
	
	//==================== 追従ピボットのオフセット ====================
	void SetPivotOffset(const Math::Vector3& _offset) { m_pivotOffset = _offset; }

	// 遮蔽ヒットのうち、これより近いものは「始点がジオメトリにめり込んでいる」
	// とみなして無視する。壁に張り付いた時の暴発も同時に防げる。
	void SetOcclusionResponse(float _ignoreDistance, float _maxPullSpeed)
	{
		m_occlusionIgnoreDistance = _ignoreDistance;
		m_occlusionMaxPullSpeed = _maxPullSpeed;
	}

	//==================== 演出フック(プレイヤー等から叩く) ====================
	void SetTargetFovY(float _fovDeg, float _lambda = 10.0f)
	{
		m_cm.get<CameraComponent>(m_entity).SetTargetFovY(_fovDeg, _lambda);
	}
	float GetFovY() { return m_cm.get<CameraComponent>(m_entity).GetFovY(); }

	bool GetSoftLockTargetPosition(Math::Vector3& _out)
	{
		auto& cam = m_cm.get<CameraComponent>(m_entity);
		if (!cam.IsTargetPullActive()) return false;
		_out = cam.GetTargetPullTargetPosition();
		return true;
	}

	void AddTrauma(float _amount) { m_cm.get<CameraComponent>(m_entity).AddTrauma(_amount); }

	//==================== アクティブ制御(CameraManager用) ====================
	void SetActive(bool _active) { m_active = _active; }
	bool IsActive() const { return m_active; }

	Math::Matrix GetResolvedWorldMatrix();
	void SnapToTarget();

	virtual void SyncPoseFromWorldMatrix(const Math::Matrix& /*_world*/) {}

	// ---- HUD用(ワールド→画面の変換に使う) ----
	Math::Matrix GetViewMatrix() { return m_cm.get<CameraComponent>(m_entity).GetResolvedViewMatrix(); }
	float GetNearZ() { return m_cm.get<CameraComponent>(m_entity).GetNearZ(); }
	float GetFarZ() { return m_cm.get<CameraComponent>(m_entity).GetFarZ(); }

	bool GetSoftLockInfo(std::uint64_t& _outId, Math::Vector3& _outPos)
	{
		auto& cam = m_cm.get<CameraComponent>(m_entity);
		if (!cam.IsTargetPullActive()) return false;

		_outId = cam.GetTargetPullLockedId();
		_outPos = cam.GetTargetPullTargetPosition();
		return true;
	}

	void SetPivotVerticalDamp(float _riseLambda, float _fallLambda,
		float _maxGap, float _maxSpeed)
	{
		m_pivotRiseLambda = _riseLambda;
		m_pivotFallLambda = _fallLambda;
		m_pivotMaxGap = _maxGap;
		m_pivotMaxSpeed = _maxSpeed;
	}

	//==================== フォーカス演出 ====================
	void StartFocus(const Math::Vector3& _pos, float _distance, float _fovDeg,
		float _posLambda = 6.0f, float _distLambda = 5.0f);
	float GetFocusZoomElapsed() const
	{
		return (m_focusActive && m_focusPhase == FocusPhase::Zoom) ? m_focusPhaseTime : 0.0f;
	}
	bool IsFocusing() const { return m_focusActive; }

protected:

	// 生の追従座標を、縦だけならしたピボットへ変換する
	Math::Vector3 ResolvePivot(const Math::Vector3& _rawPos, float _dt);
	void ResetPivot() { m_pivotInitialized = false; m_occInitialized = false; }

	std::shared_ptr<KdCamera> m_camera;
	kd::ecs::Entity m_targetEntity;
	bool            m_hasTarget = false;	// SetTarget 前に Update が来ても空のエンティティを読まないように
	std::function<std::vector<CameraComponent::PullCandidate>()> m_pullCandidatesProvider;

	bool m_active = true;

	// 共通の調整値(フォーカス演出やスティックの効き方はここから直接読む)
	CameraBaseTuning m_baseTuning;

	// マウス視点(値はコンストラクタで m_baseTuning から入る)
	float m_mouseSensitivity = 0.0f;	// rad/カウント
	bool  m_mouseInvertY = false;

	// フォーカス演出
	void UpdateFocus(CameraComponent& _cam, float _dt);

	enum class FocusPhase { Turn, Zoom };
	Math::Vector3 m_currentPivot = Math::Vector3::Zero;	
	bool          m_focusActive = false;
	FocusPhase    m_focusPhase = FocusPhase::Turn;
	float         m_focusPhaseTime = 0.0f;
	Math::Vector3 m_focusPos = Math::Vector3::Zero;
	Math::Vector3 m_focusEye = Math::Vector3::Zero;		// 旋回中に固定するカメラ位置
	Math::Vector3 m_focusPivot = Math::Vector3::Zero;
	// 下の4つは StartFocus で必ず入れ直す
	float         m_focusDistance = 0.0f;
	float         m_focusFovDeg = 0.0f;
	float         m_focusPosLambda = 0.0f;
	float         m_focusDistLambda = 0.0f;

	// ---- ここから下はコンストラクタで m_baseTuning から入る ----

	// 遮蔽プローブ
	float m_occlusionProbeRadius = 0.0f;
	float m_occlusionMinDistance = 0.0f;

	// 視点操作
	float m_yawSpeed = 0.0f;   // rad/秒
	float m_pitchSpeed = 0.0f; // rad/秒
	float m_lookDeadZone = 0.0f;

	// 追従ピボット(縦ならし)
	float m_pivotY = 0.0f;
	bool  m_pivotInitialized = false;
	float m_pivotMaxGap = 0.0f;
	float m_pivotMaxSpeed = 0.0f;
	float m_pivotRiseLambda = 0.0f;
	float m_pivotFallLambda = 0.0f;

	Math::Vector3 m_pivotOffset = Math::Vector3::Zero;
	float m_occlusionIgnoreDistance = 0.0f;
	float m_occlusionMaxPullSpeed = 0.0f;	// ※今は遮蔽処理の中で使っていない(解説mdの要確認リスト参照)
	float m_occDistance = 0.0f;
	bool  m_occInitialized = false;
};