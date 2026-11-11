#pragma once
#include "../../Config/Tuning.h"

//==========================================================
// プレイヤーの調整値
// 中身は Asset/Data/Config/PlayerTuning@PlayerTuning_js.json
// ステートごとの値(攻撃の判定や時間)は PlayerStates.json 側
//==========================================================
namespace PlayerTuningKey
{
	inline constexpr const char* kFile = "PlayerTuning";
	inline constexpr const char* kStatesFile = "PlayerStates";
}

struct PlayerTuning
{
	//---- 初期配置 / 見た目 ----
	std::string   modelName = "Player";
	std::string   materialName = "ChickenDefault";
	Math::Vector3 bodyScale = { 0.3f, 0.3f, 0.3f };
	Math::Vector3 spawnRotationDeg = { 0.0f, 180.0f, 0.0f };
	Math::Vector3 spawnPosition = { 8.0f, 0.0f, -114.0f };

	//---- コリジョン ----
	float capsuleRadius = 1.6_m;
	float capsuleHeight = 2.6_m;
	float stepHeight = 0.6f;		// 登れる段差(GameScale::ApplyToMoveConfig が縮尺を掛ける)
	float stepSnap = 0.5f;			// 下り段差へ吸い付く距離(同上)

	//---- 移動 ----
	float rotSpeed = 14.0f;			// 通常の旋回
	float rotSpeedTurn = 26.0f;		// 切り返し時の旋回
	float turnDotThresh = 0.2f;		// 入力方向との内積がこれ未満なら切り返し扱い
	float speedLambda = 14.0f;		// 速度の追従(加速の鈍さ)
	float stopLambda = 22.0f;		// 停止は加速より速く
	float dashKickSpeed = 12.0_mps;	// ダッシュ開始時の押し出し
	float groundStickMin = 12.0f;	// 接地中に地面へ張り付かせる最小の下向きの力
	float groundStickSlope = 0.7f;	// 速度に比例して足す張り付きの強さ
	float hurtGroundStick = 0.1f;	// よろけ中の張り付き

	//---- 回避 / ジャスト回避 ----
	float dodgeCooldown = 0.6f;
	float dodgeIFrameTime = 0.4f;	// 出だしの無敵。この間に殴られるとジャスト回避
	float counterWindow = 0.45f;	// カウンター受付(実時間)
	float slowScale = 0.15f;		// ジャスト回避中のタイムスケール
	float parryStopSec = 0.09f;		// 成立した瞬間の止め
	float parryTrauma = 0.4f;
	float focusFovY = 42.0f;		// 回避成功中の視野角
	float focusFovLambda = 18.0f;
	float parryBlurEase = 0.2f;		// 成立時のラジアルブラー
	float parryBlurStrength = 50.0f;

	//---- 攻撃 ----
	float attackOriginHeight = 2.0_m;	// 判定の根元の高さ
	float attackBladeLength = 3.0_m;	// 先端ノードが無いときの刃の長さ
	float lungeBodyMargin = 2.0_m;		// 踏み込みで相手の手前に残す余白
	float lungeMaxSpeed = 260.0_m;		// 踏み込みの速度上限(壁抜け防止)
	float aimConeDot = 0.35f;			// 入力方向の許容角(約70°)
	float aimPriorityBonus = 0.55f;		// 入力方向の敵を優先する度合い
	float counterAimDot = 0.5f;			// カウンターで「殴ってきた方向」とみなす角度(前後60°)
	float snapExtraRange = 3.5_m;		// 射程外でも吸い付く追加距離
	float snapFacingDot = 0.6f;			// 吸い付く相手の正面判定
	float snapPullRate = 12.0f;			// 吸い付きで1秒に詰める割合
	float knockbackLiftRatio = 0.35f;	// ノックバックの上向き成分(水平に対する比)
	float cancelHoldTime = 0.10f;		// キャンセル猶予の保持時間

	//---- 被弾 ----
	float hitInvincibleTime = 0.3f;
	float hurtLightSec = 0.30f;
	float hurtHeavySec = 0.55f;
	float hurtHeavyDamage = 25.0f;		// これ以上で吹き飛びのよろけ
	float hurtDecay = 6.0f;				// ノックバックの減衰
	float hurtSquashLight = 0.18f;		// よろけの潰れ量
	float hurtSquashHeavy = 0.35f;
	float damagedStopSec = 0.14f;		// 被弾のヒットストップ
	float damagedStopFade = 0.02f;
	float damagedTrauma = 0.5f;
	float damagedTraumaPerLevel = 0.6f;	// 吹き飛びのときに足すシェイクの割合
	Math::Vector3 damagedVibration = { 0.8f, 0.5f, 0.25f };	// 左, 右, 秒
	float hitFxHeight = 2.0_m;			// 被弾エフェクトを出す高さ

	//---- HP表示 ----
	float damageShockSec = 0.5f;		// UIの揺れが収まるまで
	float damageShockBase = 0.5f;		// 1発で必ず揺れる量
	float damageShockPerDamage = 40.0f;	// このダメージで追加分が最大
	float damageShockExtraMax = 0.5f;
	float hpLagLambda = 4.5f;			// 白ゲージが追いつく速さ
	float hpLagHoldShock = 0.6f;		// 揺れがこれより強い間は白ゲージを止める

	//---- 着地の潰し ----
	float landRecoverLambda = 12.0f;
	float landSquashWiden = 0.55f;		// 潰れたぶん横に広げる割合

	//---- 鳴き声 ----
	float normalFovY = 60.0f;			// 普段の視野角(カメラが無いときの値と、戻す先)
	float cryChargeFovY = 34.0f;		// 溜め切りの視野角
	float cryBurstFovY = 86.0f;			// 解放の瞬間に一気に開く
	float cryChargePushLambda = 30.0f;
	float cryChargeFovLambdaMin = 16.0f;
	float cryChargeFovLambdaAdd = 14.0f;
	float cryBurstFovLambda = 42.0f;
	float cryRecoverFovLambda = 10.0f;
	float cryVibrationStart = 0.35f;	// 溜めのこの割合から振動を強めていく
	float cryVibrationMax = 0.55f;
	float cryVibrationSec = 0.1f;
	float cryTraumaStart = 0.8f;		// 溜め切り直前は画面も揺らす
	float cryTraumaScale = 1.2f;
	float cryShortRadius = 14.0_m;
	float cryFullRadius = 80.0_m;		// 事実上画面全体
	float cryShortFreeze = 1.0f;
	float cryFullFreeze = 5.0f;
	float cryWaveSec = 0.5f;

	//---- カメラ演出 ----
	float focusRestoreLambda = 10.0f;	// PopCameraFocus で戻すときの速さ

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("modelName", modelName);
		v("materialName", materialName);
		v("bodyScale", bodyScale);
		v("spawnRotationDeg", spawnRotationDeg);
		v("spawnPosition", spawnPosition);

		v("capsuleRadius", capsuleRadius, Unit::Meter);
		v("capsuleHeight", capsuleHeight, Unit::Meter);
		v("stepHeight", stepHeight);
		v("stepSnap", stepSnap);

		v("rotSpeed", rotSpeed);
		v("rotSpeedTurn", rotSpeedTurn);
		v("turnDotThresh", turnDotThresh);
		v("speedLambda", speedLambda);
		v("stopLambda", stopLambda);
		v("dashKickSpeed", dashKickSpeed, Unit::MeterPerSec);
		v("groundStickMin", groundStickMin);
		v("groundStickSlope", groundStickSlope);
		v("hurtGroundStick", hurtGroundStick);

		v("dodgeCooldown", dodgeCooldown);
		v("dodgeIFrameTime", dodgeIFrameTime);
		v("counterWindow", counterWindow);
		v("slowScale", slowScale);
		v("parryStopSec", parryStopSec);
		v("parryTrauma", parryTrauma);
		v("focusFovY", focusFovY);
		v("focusFovLambda", focusFovLambda);
		v("parryBlurEase", parryBlurEase);
		v("parryBlurStrength", parryBlurStrength);

		v("attackOriginHeight", attackOriginHeight, Unit::Meter);
		v("attackBladeLength", attackBladeLength, Unit::Meter);
		v("lungeBodyMargin", lungeBodyMargin, Unit::Meter);
		v("lungeMaxSpeed", lungeMaxSpeed, Unit::Meter);
		v("aimConeDot", aimConeDot);
		v("aimPriorityBonus", aimPriorityBonus);
		v("counterAimDot", counterAimDot);
		v("snapExtraRange", snapExtraRange, Unit::Meter);
		v("snapFacingDot", snapFacingDot);
		v("snapPullRate", snapPullRate);
		v("knockbackLiftRatio", knockbackLiftRatio);
		v("cancelHoldTime", cancelHoldTime);

		v("hitInvincibleTime", hitInvincibleTime);
		v("hurtLightSec", hurtLightSec);
		v("hurtHeavySec", hurtHeavySec);
		v("hurtHeavyDamage", hurtHeavyDamage);
		v("hurtDecay", hurtDecay);
		v("hurtSquashLight", hurtSquashLight);
		v("hurtSquashHeavy", hurtSquashHeavy);
		v("damagedStopSec", damagedStopSec);
		v("damagedStopFade", damagedStopFade);
		v("damagedTrauma", damagedTrauma);
		v("damagedTraumaPerLevel", damagedTraumaPerLevel);
		v("damagedVibration", damagedVibration);
		v("hitFxHeight", hitFxHeight, Unit::Meter);

		v("damageShockSec", damageShockSec);
		v("damageShockBase", damageShockBase);
		v("damageShockPerDamage", damageShockPerDamage);
		v("damageShockExtraMax", damageShockExtraMax);
		v("hpLagLambda", hpLagLambda);
		v("hpLagHoldShock", hpLagHoldShock);

		v("landRecoverLambda", landRecoverLambda);
		v("landSquashWiden", landSquashWiden);

		v("normalFovY", normalFovY);
		v("cryChargeFovY", cryChargeFovY);
		v("cryBurstFovY", cryBurstFovY);
		v("cryChargePushLambda", cryChargePushLambda);
		v("cryChargeFovLambdaMin", cryChargeFovLambdaMin);
		v("cryChargeFovLambdaAdd", cryChargeFovLambdaAdd);
		v("cryBurstFovLambda", cryBurstFovLambda);
		v("cryRecoverFovLambda", cryRecoverFovLambda);
		v("cryVibrationStart", cryVibrationStart);
		v("cryVibrationMax", cryVibrationMax);
		v("cryVibrationSec", cryVibrationSec);
		v("cryTraumaStart", cryTraumaStart);
		v("cryTraumaScale", cryTraumaScale);
		v("cryShortRadius", cryShortRadius, Unit::Meter);
		v("cryFullRadius", cryFullRadius, Unit::Meter);
		v("cryShortFreeze", cryShortFreeze);
		v("cryFullFreeze", cryFullFreeze);
		v("cryWaveSec", cryWaveSec);

		v("focusRestoreLambda", focusRestoreLambda);
	}
};
