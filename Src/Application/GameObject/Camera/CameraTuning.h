#pragma once
#include "../Config/Tuning.h"

//==========================================================
// カメラの調整値
// 中身は Asset/Data/Config/CameraTuning@CameraTuning_js.json
// (ResourceConfig の configFiles に "CameraTuning" で登録)
//==========================================================
namespace CameraTuningKey
{
	inline constexpr const char* kFile = "CameraTuning";
}

//------------------------------------------------------
// CameraBase(全カメラ共通)
//------------------------------------------------------
struct CameraBaseTuning
{
	// マウス。1カウントあたりの回転量(rad)。800DPIで約17cm動かすと一周
	float mouseSensitivity = 0.0036f;
	bool  mouseInvertY = false;

	// スティックの効き方。中央付近をこの割合まで弱め、端で最大にする
	float lookCurveBase = 0.35f;

	// 追従ピボット(縦ならし)
	Math::Vector3 pivotOffset = { 0.0f, 1.6f, 0.0f };
	float pivotMaxGap = 1.2_m;			// 離れすぎたら引き戻す距離
	float pivotMaxSpeed = 14.0_mps;		// 1秒に動ける上限
	float pivotRiseLambda = 16.0f;		// 上りの追従(速め。遅いとキャラが画面下へ潜る)
	float pivotFallLambda = 10.0f;		// 下りの追従(遅め。落下中の画が落ち着く)

	// 遮蔽
	float occlusionProbeRadius = 0.35_m;
	float occlusionMinDistance = 2.0_m;
	float occlusionIgnoreDistance = 0.8f;
	float occlusionMaxPullSpeed = 25.0f;

	// 視点操作(rad/秒)
	float yawSpeed = 3.0f;
	float pitchSpeed = 2.0f;
	float lookDeadZone = 0.15f;

	// フォーカス演出(ひよこを見せる寄り)
	float focusPitchMin = -1.5f;		// ひよこが上下どちらにいても向けるように広げる
	float focusPitchMax = 0.6f;
	float focusTurnLambda = 12.0f;		// 旋回の速さ
	float focusAimTolerance = 0.03f;	// これ以下の角度差で「向き終わった」とみなす(rad)
	float focusTurnTimeout = 0.5f;		// 向き終わらなくてもズームへ移る時間
	float focusZoomFovLambda = 5.0f;	// ズームに入ったときの画角の追従

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("mouseSensitivity", mouseSensitivity);
		v("mouseInvertY", mouseInvertY);
		v("lookCurveBase", lookCurveBase);
		v("pivotOffset", pivotOffset);
		v("pivotMaxGap", pivotMaxGap, Unit::Meter);
		v("pivotMaxSpeed", pivotMaxSpeed, Unit::MeterPerSec);
		v("pivotRiseLambda", pivotRiseLambda);
		v("pivotFallLambda", pivotFallLambda);
		v("occlusionProbeRadius", occlusionProbeRadius, Unit::Meter);
		v("occlusionMinDistance", occlusionMinDistance, Unit::Meter);
		v("occlusionIgnoreDistance", occlusionIgnoreDistance);
		v("occlusionMaxPullSpeed", occlusionMaxPullSpeed);
		v("yawSpeed", yawSpeed);
		v("pitchSpeed", pitchSpeed);
		v("lookDeadZone", lookDeadZone);
		v("focusPitchMin", focusPitchMin);
		v("focusPitchMax", focusPitchMax);
		v("focusTurnLambda", focusTurnLambda);
		v("focusAimTolerance", focusAimTolerance);
		v("focusTurnTimeout", focusTurnTimeout);
		v("focusZoomFovLambda", focusZoomFovLambda);
	}
};

//------------------------------------------------------
// EntityCamera(ゲーム中のプレイヤー追従カメラ)
//------------------------------------------------------
struct EntityCameraTuning
{
	// 初期の軌道
	Math::Vector3 orbitPivot = { -1.0_m, 3.0_m, 6.0_m };
	float orbitYaw = 0.0f;
	float orbitPitch = 0.3f;
	float orbitDistance = 30.0_m;

	float pitchMin = -1.1f;
	float pitchMax = -0.25f;
	float distanceMin = 0.5_m;
	float distanceMax = 40.0_m;

	float pivotHeight = 0.6_m;

	// 遮蔽
	float occlusionIgnoreDistance = 0.5_m;
	float occlusionMaxPullSpeed = 25.0_mps;
	float occlusionPadding = 0.35_m;		// 壁との隙間(長さ)
	float occlusionPullRate = 25.0f;		// 壁に寄るときの追従レート(1/秒)
	float occlusionPushRate = 3.0f;			// 壁から戻るときの追従レート(1/秒)
	float occlusionProbeRadius = 2.5_m;
	float occlusionMinDistance = 1.0_m;

	// 視点操作(rad/秒。縮尺とは無関係)
	float yawSpeed = 8.0f;
	float pitchSpeed = 5.0f;
	float lookDeadZone = 0.15f;

	// フレーミング(CameraComponent::SetFraming の引数の順)
	Math::Vector3 framingDeadZone = { 0.3_m, 0.8_m, 0.3_m };	// デッドゾーン(半径)
	float framingLookAhead = 0.18f;		// ルックアヘッド係数
	float framingFollowRate = 4.0f;		// 追従レート
	float framingFollowRate2 = 6.0f;	// 4番目の引数(元コードでは無名だった)

	// ターゲットプル(敵への吸着)
	float pullAngleDeg = 18.0f;
	float pullRate = 15.0f;
	float pullInnerAngleDeg = 10.0f;
	float pullStickAngleDeg = 4.6f;
	float pullStickStrength = 0.35f;
	float pullMaxDistance = 60.0_m;
	float pullHeightSafety = 2.0_m;

	// ダッシュ中の画角
	float dashFov = 72.0f;
	float dashFovLambda = 6.0f;
	float normalFov = 60.0f;
	float normalFovLambda = 8.0f;

	// 空間に漂う塵のパーティクル
	std::string dustEffect = "Dust";

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("orbitPivot", orbitPivot, Unit::Meter);
		v("orbitYaw", orbitYaw);
		v("orbitPitch", orbitPitch);
		v("orbitDistance", orbitDistance, Unit::Meter);
		v("pitchMin", pitchMin);
		v("pitchMax", pitchMax);
		v("distanceMin", distanceMin, Unit::Meter);
		v("distanceMax", distanceMax, Unit::Meter);
		v("pivotHeight", pivotHeight, Unit::Meter);
		v("occlusionIgnoreDistance", occlusionIgnoreDistance, Unit::Meter);
		v("occlusionMaxPullSpeed", occlusionMaxPullSpeed, Unit::MeterPerSec);
		v("occlusionPadding", occlusionPadding, Unit::Meter);
		v("occlusionPullRate", occlusionPullRate);
		v("occlusionPushRate", occlusionPushRate);
		v("occlusionProbeRadius", occlusionProbeRadius, Unit::Meter);
		v("occlusionMinDistance", occlusionMinDistance, Unit::Meter);
		v("yawSpeed", yawSpeed);
		v("pitchSpeed", pitchSpeed);
		v("lookDeadZone", lookDeadZone);
		v("framingDeadZone", framingDeadZone, Unit::Meter);
		v("framingLookAhead", framingLookAhead);
		v("framingFollowRate", framingFollowRate);
		v("framingFollowRate2", framingFollowRate2);
		v("pullAngleDeg", pullAngleDeg);
		v("pullRate", pullRate);
		v("pullInnerAngleDeg", pullInnerAngleDeg);
		v("pullStickAngleDeg", pullStickAngleDeg);
		v("pullStickStrength", pullStickStrength);
		v("pullMaxDistance", pullMaxDistance, Unit::Meter);
		v("pullHeightSafety", pullHeightSafety, Unit::Meter);
		v("dashFov", dashFov);
		v("dashFovLambda", dashFovLambda);
		v("normalFov", normalFov);
		v("normalFovLambda", normalFovLambda);
		v("dustEffect", dustEffect);
	}
};

//------------------------------------------------------
// TitleCamera(タイトル画面)
//------------------------------------------------------
struct TitleCameraTuning
{
	// 軌道。一周に約70秒。UIを読んでいる間に画が変わらない速さ
	float yawSpeed = 0.09f;			// rad/秒(今は回転を止めているので未使用)
	float yaw = -70.0f;				// 向き。元コードの値をそのまま(rad として渡している)
	float baseDistance = 12.0_m;
	float basePitch = 0.22f;

	// 上下の揺れ。呼吸のように、ごく浅く
	float bobAmplitude = 0.6_m;
	float bobSpeed = 0.31f;

	// 距離の揺れ。寄りと引きをゆっくり往復する
	float zoomAmplitude = 1.2_m;
	float zoomSpeed = 0.17f;

	// 画角も微かに動かす。距離だけだと機械的に見える
	float fovBase = 42.0f;
	float fovAmplitude = 1.6f;
	float fovSpeed = 0.23f;
	float fovLambda = 6.0f;

	// 決定後。ゆっくり寄りながら画角を狭める
	float leaveSec = 0.9f;
	float leaveDistance = 6.0_m;
	float leaveFov = 32.0f;

	// 射影
	float nearZ = 0.01_m;
	float farZ = 500.0_m;

	// 遮蔽
	float occlusionPadding = 0.35_m;
	float occlusionPullRate = 25.0f;
	float occlusionPushRate = 3.0f;
	float occlusionProbeRadius = 2.5_m;
	float occlusionMinDistance = 1.0_m;
	float occlusionIgnoreDistance = 0.8_m;
	float occlusionMaxPullSpeed = 25.0_mps;

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("yawSpeed", yawSpeed);
		v("yaw", yaw);
		v("baseDistance", baseDistance, Unit::Meter);
		v("basePitch", basePitch);
		v("bobAmplitude", bobAmplitude, Unit::Meter);
		v("bobSpeed", bobSpeed);
		v("zoomAmplitude", zoomAmplitude, Unit::Meter);
		v("zoomSpeed", zoomSpeed);
		v("fovBase", fovBase);
		v("fovAmplitude", fovAmplitude);
		v("fovSpeed", fovSpeed);
		v("fovLambda", fovLambda);
		v("leaveSec", leaveSec);
		v("leaveDistance", leaveDistance, Unit::Meter);
		v("leaveFov", leaveFov);
		v("nearZ", nearZ, Unit::Meter);
		v("farZ", farZ, Unit::Meter);
		v("occlusionPadding", occlusionPadding, Unit::Meter);
		v("occlusionPullRate", occlusionPullRate);
		v("occlusionPushRate", occlusionPushRate);
		v("occlusionProbeRadius", occlusionProbeRadius, Unit::Meter);
		v("occlusionMinDistance", occlusionMinDistance, Unit::Meter);
		v("occlusionIgnoreDistance", occlusionIgnoreDistance, Unit::Meter);
		v("occlusionMaxPullSpeed", occlusionMaxPullSpeed, Unit::MeterPerSec);
	}
};

//------------------------------------------------------
// DebugCamera(フリーフライ)
//------------------------------------------------------
struct DebugCameraTuning
{
	Math::Vector3 startPosition = { 0.0f, 3.0f, -10.0f };
	float defaultFovDeg = 60.0f;
	float fovMinDeg = 10.0f;
	float fovMaxDeg = 60.0f;
	float moveSpeed = 12.0f;		// units/sec
	float boostScale = 3.0f;		// Shift時の倍率
	float lookSpeed = 0.08f;		// rad/frame(既存Orbitカメラに合わせた感度)
	float wheelStep = 3.0f;			// 1ノッチあたりのFOV変化(deg)
	float pitchLimit = 1.53f;		// 約±87.7°(真上/真下でのフリップ防止)
	float mouseSensitivity = 0.0015f;

	template<class V> void Visit(V& v)
	{
		v("startPosition", startPosition);
		v("defaultFovDeg", defaultFovDeg);
		v("fovMinDeg", fovMinDeg);
		v("fovMaxDeg", fovMaxDeg);
		v("moveSpeed", moveSpeed);
		v("boostScale", boostScale);
		v("lookSpeed", lookSpeed);
		v("wheelStep", wheelStep);
		v("pitchLimit", pitchLimit);
		v("mouseSensitivity", mouseSensitivity);
	}
};
