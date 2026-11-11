#pragma once
#include "../../../Config/Tuning.h"

//==========================================================
// 敵の調整値
// 中身は Asset/Data/Config/EnemyTuning@EnemyTuning_js.json
//
//   "EnemyBase"    … 全種類で共通の値(このファイルの EnemyTuning)
//   "types"        … 種類ごとの EnemySpec の上書き("Normal" / "Heavy" / "Rusher")
//   "EnemyManager" … 部隊・陣形・連鎖など(EnemyManager.h の EnemyManagerTuning)
//   "states"       … ステート用の値(EnemyStateSetup.cpp の EnemyStateTuning)
//
// ステートの params は EnemyStates.json 側
//==========================================================
namespace EnemyTuningKey
{
	inline constexpr const char* kFile = "EnemyTuning";
	inline constexpr const char* kStatesFile = "EnemyStates";
}

struct EnemyTuning
{
	//------------------------------------------------------
	// 攻撃タグごとの体幹ダメージ
	//------------------------------------------------------
	struct Poise
	{
		float poise = 0.0f;		// 体幹へのダメージ
		float stagger = 0.0f;	// 軽怯みの硬直時間
		float breakGain = 0.0f;	// 硬化中に溜まるブレイク値

		template<class V> void Visit(V& v)
		{
			v("poise", poise);
			v("stagger", stagger);
			v("breakGain", breakGain);
		}
	};
	struct PoiseTable
	{
		Poise normal   = { 20.0f, 0.20f, 12.0f };
		Poise combo3   = { 60.0f, 0.35f, 34.0f };
		Poise counter  = { 55.0f, 0.35f, 30.0f };
		Poise finisher = { 120.0f, 0.45f, 55.0f };
		Poise heavy    = { 45.0f, 0.30f, 26.0f };
		Poise smash    = { 90.0f, 0.45f, 50.0f };
		Poise other    = { 15.0f, 0.18f, 10.0f };	// 表に無いタグ

		template<class V> void Visit(V& v)
		{
			v("normal", normal);
			v("combo3", combo3);
			v("counter", counter);
			v("finisher", finisher);
			v("heavy", heavy);
			v("smash", smash);
			v("other", other);
		}
	} poise;

	//------------------------------------------------------
	// 被弾
	//------------------------------------------------------
	struct Hit
	{
		float frozenDamageMul = 3.0f;		// 硬化中の被ダメージ倍率
		int   frozenExtraHitstop = 3;		// 硬化中に当てたときに足すヒットストップ(フレーム)
		float breakThreshold = 70.0f;		// ブレイク値がこれを超えると砕ける
		float launchThreshold = 200.0f;		// 上向きの勢いがこれ以上なら打ち上げ扱い
		float breakVulnerableSec = 0.85f;	// 体幹ブレイク後の無防備な時間
		float staggerBlowSec = 0.6f;		// 体幹ブレイク(吹き飛び)の硬直
		float staggerLaunchSec = 0.9f;		// 体幹ブレイク(打ち上げ)の硬直
		float knockbackHorizontalMul = 3.0f;	// 受けたノックバックの横方向への倍率
		float knockbackTimeLight = 0.12f;	// ノックバック中とみなす時間(崩していない)
		float knockbackTimeHeavy = 0.4f;	// 同(崩した)
		float launchMinUp = 30.0f;			// 崩したときに最低限浮かせる勢い
		float launchGravityScale = 0.45f;	// 打ち上げ中の重力の弱さ
		float traumaPerDamage = 100.0f;		// このダメージでシェイクが1
		float traumaMax = 0.6f;
		std::string effect = "Hit";
		std::string frozenEffect = "FrozenHit";
		std::string sound = "attack";

		template<class V> void Visit(V& v)
		{
			v("frozenDamageMul", frozenDamageMul);
			v("frozenExtraHitstop", frozenExtraHitstop);
			v("breakThreshold", breakThreshold);
			v("launchThreshold", launchThreshold);
			v("breakVulnerableSec", breakVulnerableSec);
			v("staggerBlowSec", staggerBlowSec);
			v("staggerLaunchSec", staggerLaunchSec);
			v("knockbackHorizontalMul", knockbackHorizontalMul);
			v("knockbackTimeLight", knockbackTimeLight);
			v("knockbackTimeHeavy", knockbackTimeHeavy);
			v("launchMinUp", launchMinUp);
			v("launchGravityScale", launchGravityScale);
			v("traumaPerDamage", traumaPerDamage);
			v("traumaMax", traumaMax);
			v("effect", effect);
			v("frozenEffect", frozenEffect);
			v("sound", sound);
		}
	} hit;

	//------------------------------------------------------
	// 壁への叩きつけ
	//------------------------------------------------------
	struct Wall
	{
		float slamSpeed = 25.0f;		// これ以上の速さで壁に当たると叩きつけ
		float slamDamage = 8.0f;
		float bounce = 1.3f;			// 壁に向かう成分を何倍で跳ね返すか(1で止まる)
		float speedKeep = 0.3f;			// 叩きつけた後に残る勢い
		float killLaunchRatio = 0.5f;	// 壁で倒したときの吹っ飛び(残った勢いに対する比)
		float killLaunchUp = 60.0f;
		float reactionSec = 1.1f;		// 生き残ったときのダウン時間
		float hitstop = 0.12f;
		float trauma = 0.5f;

		template<class V> void Visit(V& v)
		{
			v("slamSpeed", slamSpeed);
			v("slamDamage", slamDamage);
			v("bounce", bounce);
			v("speedKeep", speedKeep);
			v("killLaunchRatio", killLaunchRatio);
			v("killLaunchUp", killLaunchUp);
			v("reactionSec", reactionSec);
			v("hitstop", hitstop);
			v("trauma", trauma);
		}
	} wall;

	//------------------------------------------------------
	// 移動
	//------------------------------------------------------
	struct Motion
	{
		float capsuleHeightScale = 50.0f;	// カプセルの高さに掛ける倍率(元コードの値。要確認リスト参照)
		float groundStick = 0.1f;			// 接地中の下向きの力
		float jumpDecay = 0.98f;			// 打ち上げの勢いの毎フレームの減り方
		float jumpCutoff = 0.01f;			// これ以下の勢いは0にする
		float knockbackScale = 1.0f;		// ノックバック移動量の倍率
		float knockbackStepHeight = 0.4f;	// 吹き飛びから戻したときの段差(通常値)
		float knockbackSnapDist = 0.35f;
		float wanderSecMin = 3.0f;			// うろつきの向きを変えるまで
		float wanderSecMax = 5.0f;
		float lookSecMin = 0.4f;			// 見回しの首振りの間隔
		float lookSecMax = 0.8f;
		float lookAroundDeg = 70.0f;		// 見回しの振り幅(度)
		float attackBiasMax = 0.6f;			// 攻撃間隔の個体差の上限
		float slotBiasMin = -1.2_m;			// 間合いの個体差
		float slotBiasMax = 1.5_m;

		template<class V> void Visit(V& v)
		{
			using Tuning::Unit;
			v("capsuleHeightScale", capsuleHeightScale);
			v("groundStick", groundStick);
			v("jumpDecay", jumpDecay);
			v("jumpCutoff", jumpCutoff);
			v("knockbackScale", knockbackScale);
			v("knockbackStepHeight", knockbackStepHeight);
			v("knockbackSnapDist", knockbackSnapDist);
			v("wanderSecMin", wanderSecMin);
			v("wanderSecMax", wanderSecMax);
			v("lookSecMin", lookSecMin);
			v("lookSecMax", lookSecMax);
			v("lookAroundDeg", lookAroundDeg);
			v("attackBiasMax", attackBiasMax);
			v("slotBiasMin", slotBiasMin, Unit::Meter);
			v("slotBiasMax", slotBiasMax, Unit::Meter);
		}
	} motion;

	//------------------------------------------------------
	// 目のライト(硬化などで点く)
	//------------------------------------------------------
	struct Light
	{
		float power = 1.0f;
		float range = 10.0f;
		Math::Vector3 color = { 0.6f, 0.2f, 0.8f };

		template<class V> void Visit(V& v)
		{
			v("power", power);
			v("range", range);
			v("color", color);
		}
	} light;

	//------------------------------------------------------
	// 姿勢の潰し・伸ばし
	// 「縦に s 潰したら横に s × widen 広げる」の形が多い
	//------------------------------------------------------
	struct Pose
	{
		float callStretch = 0.22f;		// 呼び声の伸び上がり
		float callNarrow = 0.4f;
		float windupSquash = 0.5f;		// 溜めの縦の潰れ量
		float windupStretch = 0.35f;	// 発射直前の伸び量
		float windupSplit = 0.6f;		// 溜めのこの割合までが潰れ、残りが伸び
		float windupWiden = 0.7f;
		float windupTrauma = 0.15f;		// 溜め切りで入れるシェイク
		float hitSquash = 0.35f;		// 被弾時の潰し量
		float hitWiden = 0.6f;
		float strikeStretch = 0.35f;	// 攻撃方向への伸び
		float strikeNarrowX = 0.5f;
		float strikeNarrowY = 0.3f;
		float freezeStretch = 0.12f;	// 固まった瞬間の伸び
		float freezeStretchRatio = 0.15f;	// 硬化時間のこの割合で伸びが戻る
		float freezeNarrow = 0.5f;
		float freezeShiver = 0.02f;		// 解ける直前の震え
		float freezeShiverFrom = 0.75f;	// 硬化時間のこの割合から震える
		float freezeShiverSpeed = 60.0f;
		float fuseSwell = 0.45f;		// 断末魔の膨らみ
		float fuseShiver = 0.05f;
		float fuseShiverFrom = 0.6f;
		float fuseShiverSpeed = 90.0f;
		float corpseWiden = 0.7f;
		float corpseSinkDepth = 4.0f;	// 最後に沈む深さ
		float corpseSinkShrink = 0.4f;	// 沈みながら縮む割合

		template<class V> void Visit(V& v)
		{
			v("callStretch", callStretch);
			v("callNarrow", callNarrow);
			v("windupSquash", windupSquash);
			v("windupStretch", windupStretch);
			v("windupSplit", windupSplit);
			v("windupWiden", windupWiden);
			v("windupTrauma", windupTrauma);
			v("hitSquash", hitSquash);
			v("hitWiden", hitWiden);
			v("strikeStretch", strikeStretch);
			v("strikeNarrowX", strikeNarrowX);
			v("strikeNarrowY", strikeNarrowY);
			v("freezeStretch", freezeStretch);
			v("freezeStretchRatio", freezeStretchRatio);
			v("freezeNarrow", freezeNarrow);
			v("freezeShiver", freezeShiver);
			v("freezeShiverFrom", freezeShiverFrom);
			v("freezeShiverSpeed", freezeShiverSpeed);
			v("fuseSwell", fuseSwell);
			v("fuseShiver", fuseShiver);
			v("fuseShiverFrom", fuseShiverFrom);
			v("fuseShiverSpeed", fuseShiverSpeed);
			v("corpseWiden", corpseWiden);
			v("corpseSinkDepth", corpseSinkDepth);
			v("corpseSinkShrink", corpseSinkShrink);
		}
	} pose;

	//------------------------------------------------------
	// 攻撃判定
	//------------------------------------------------------
	struct Attack
	{
		int   swingSubSteps = 4;		// 扇の判定で1フレームを刻む数
		float sweepYawMin = 40.0f;		// これ未満の振れ幅は扇にしない
		int   hitstopFrames = 4;		// 敵の攻撃が当たったときのヒットストップ

		template<class V> void Visit(V& v)
		{
			v("swingSubSteps", swingSubSteps);
			v("sweepYawMin", sweepYawMin);
			v("hitstopFrames", hitstopFrames);
		}
	} attack;

	//------------------------------------------------------
	// ひよこの連れ去り
	//------------------------------------------------------
	struct Carry
	{
		float cooldown = 3.0f;			// 離してから咥え直せるまで
		float leaveSec = 2.5f;			// 走り出してから消えるまで
		std::string releaseEffect = "Blow";
		float releaseHitstop = 0.10f;	// 取り返した手応え
		float releaseTrauma = 0.4f;

		template<class V> void Visit(V& v)
		{
			v("cooldown", cooldown);
			v("leaveSec", leaveSec);
			v("releaseEffect", releaseEffect);
			v("releaseHitstop", releaseHitstop);
			v("releaseTrauma", releaseTrauma);
		}
	} carry;

	//------------------------------------------------------
	// 硬化 / ブレイク / 連鎖
	//------------------------------------------------------
	struct Shatter
	{
		float freezeStaggerMax = 1.4f;		// 解除タイミングのばらつき
		float breakTransferRate = 0.70f;	// ブレイク時に周囲へ渡す割合
		float timeoutTransferRate = 0.15f;	// 時間切れは申し訳程度
		float transferMax = 300.0f;			// 1回に配る上限(部隊即死の防止)
		float chainFalloff = 0.35f;			// 連鎖1段ごとの減衰
		int   maxChainDepth = 2;			// 連鎖の世代上限
		float blastSpeed = 50.0f;			// 巻き込まれた側へ渡る運動量
		float chainKillPush = 3.0f;			// 連鎖で倒れた敵を外へ押す強さ(発生源からの距離に掛ける)
		float chainKillUp = 120.0f;			// 連鎖で倒れた敵を打ち上げる勢い

		// 砕けて自分が吹き飛ぶとき
		float burstSpeed = 90.0f;
		float burstUp = 55.0f;
		float burstGravityScale = 0.6f;
		float burstKnockbackSec = 0.5f;
		float timeoutPower = 0.4f;			// 時間切れのときの勢い(ブレイクを1とした比)
		float reactionBrokenSec = 0.8f;
		float reactionTimeoutSec = 0.45f;
		float hitstopBroken = 0.14f;
		float hitstopTimeout = 0.04f;
		float trauma = 0.55f;
		Math::Vector3 vibration = { 0.9f, 0.8f, 0.25f };	// 左(勢いで弱まる), 右, 秒

		template<class V> void Visit(V& v)
		{
			v("freezeStaggerMax", freezeStaggerMax);
			v("breakTransferRate", breakTransferRate);
			v("timeoutTransferRate", timeoutTransferRate);
			v("transferMax", transferMax);
			v("chainFalloff", chainFalloff);
			v("maxChainDepth", maxChainDepth);
			v("blastSpeed", blastSpeed);
			v("chainKillPush", chainKillPush);
			v("chainKillUp", chainKillUp);
			v("burstSpeed", burstSpeed);
			v("burstUp", burstUp);
			v("burstGravityScale", burstGravityScale);
			v("burstKnockbackSec", burstKnockbackSec);
			v("timeoutPower", timeoutPower);
			v("reactionBrokenSec", reactionBrokenSec);
			v("reactionTimeoutSec", reactionTimeoutSec);
			v("hitstopBroken", hitstopBroken);
			v("hitstopTimeout", hitstopTimeout);
			v("trauma", trauma);
			v("vibration", vibration);
		}
	} shatter;

	//------------------------------------------------------
	// 死 / 死体
	//------------------------------------------------------
	struct Death
	{
		float corpseStaySec = 2.2f;		// 死体を残す時間
		float corpseSinkSec = 0.7f;		// 最後に沈む時間
		float overkillMax = 1.5f;		// 吹っ飛び倍率の上限
		float overkillPerDamage = 40.0f;	// 超過ダメージがこれだけあると倍率+1
		float minLaunchUp = 40.0f;		// 倒したときに最低限浮かせる勢い
		float burstSelfKnockback = 40.0f;	// 断末魔で自分が跳ねる勢い
		float burstSelfUp = 45.0f;
		float flashSec = 0.7f;			// 断末魔の閃光が消えるまで

		template<class V> void Visit(V& v)
		{
			v("corpseStaySec", corpseStaySec);
			v("corpseSinkSec", corpseSinkSec);
			v("overkillMax", overkillMax);
			v("overkillPerDamage", overkillPerDamage);
			v("minLaunchUp", minLaunchUp);
			v("burstSelfKnockback", burstSelfKnockback);
			v("burstSelfUp", burstSelfUp);
			v("flashSec", flashSec);
		}
	} death;

	//------------------------------------------------------
	// 死体のなぎ倒し
	//------------------------------------------------------
	struct CorpseSlam
	{
		float speed = 100.0f;			// これ以上の速度で飛んでいる間だけ当たる
		float interval = 0.12f;			// 巻き込んだ後の休み
		float damp = 0.65f;				// 巻き込むたびに勢いを失う
		float transfer = 0.85f;			// 轢かれた側へ渡る運動量の割合
		float lethalLift = 0.6f;		// 轢かれて倒れたときの浮き(勢いに対する比)
		float lift = 0.35f;				// 轢かれて生き残ったときの浮き
		float gravityScale = 0.7f;
		float knockbackSec = 0.4f;
		float reactionSec = 0.7f;
		int   hitstopFrames = 5;

		template<class V> void Visit(V& v)
		{
			v("speed", speed);
			v("interval", interval);
			v("damp", damp);
			v("transfer", transfer);
			v("lethalLift", lethalLift);
			v("lift", lift);
			v("gravityScale", gravityScale);
			v("knockbackSec", knockbackSec);
			v("reactionSec", reactionSec);
			v("hitstopFrames", hitstopFrames);
		}
	} corpseSlam;

	//------------------------------------------------------
	// 陣形 / 攻撃トークン / 士気 / 拍
	//------------------------------------------------------
	struct Squad
	{
		float approachSlack = 1.0_m;	// 持ち場に着いたとみなす距離
		float approachLeave = 3.0_m;	// ここまで離れたら歩き直す(ヒステリシス)
		float approachBrakeRatio = 3.0f;	// 持ち場の手前 approachSlack × この倍 から減速する
		float tokenCooldown = 1.0f;		// 返却後、再取得できるまでの時間
		int   maxAttackChain = 2;		// 連続攻撃の上限
		float fleeCooldown = 6.0f;		// 逃げ切った後、戦い直すまで
		float beatWindow = 0.035f;		// 拍とみなす前後の幅[秒]
		float beatDivision = 2.0f;		// 8分で刻む

		template<class V> void Visit(V& v)
		{
			using Tuning::Unit;
			v("approachSlack", approachSlack, Unit::Meter);
			v("approachLeave", approachLeave, Unit::Meter);
			v("approachBrakeRatio", approachBrakeRatio);
			v("tokenCooldown", tokenCooldown);
			v("maxAttackChain", maxAttackChain);
			v("fleeCooldown", fleeCooldown);
			v("beatWindow", beatWindow);
			v("beatDivision", beatDivision);
		}
	} squad;

	//------------------------------------------------------
	// HUD
	//------------------------------------------------------
	struct Hud
	{
		float strikeFlashSec = 0.28f;
		float breakFlashSec = 0.55f;

		template<class V> void Visit(V& v)
		{
			v("strikeFlashSec", strikeFlashSec);
			v("breakFlashSec", breakFlashSec);
		}
	} hud;

	template<class V> void Visit(V& v)
	{
		v("poise", poise);
		v("hit", hit);
		v("wall", wall);
		v("motion", motion);
		v("light", light);
		v("pose", pose);
		v("attack", attack);
		v("carry", carry);
		v("shatter", shatter);
		v("death", death);
		v("corpseSlam", corpseSlam);
		v("squad", squad);
		v("hud", hud);
	}

	//------------------------------------------------------
	// 全員で1つを共有する。敵は何十体も出るので、1体ずつ JSON を読み直さない。
	// EnemyManager::Init で Reload() して、シーンごとに読み直す
	//------------------------------------------------------
	static EnemyTuning& Shared()
	{
		static EnemyTuning instance;
		return instance;
	}

	static void Reload()
	{
		EnemyTuning fresh;
		Tuning::Load(fresh, EnemyTuningKey::kFile, "EnemyBase");
		Shared() = fresh;
	}
};
