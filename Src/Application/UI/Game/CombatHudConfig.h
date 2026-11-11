#pragma once

// ===========================================================
// CombatHudConfig
//
// HUDの調整値を1箇所に集め、JSONから読み込む。
//
// 【なぜ必要か】
// 色を0.05動かすたびにビルド→起動→戦闘まで進む、を繰り返していると
// 調整の回転数が上がらない。JsonResourceManagerの監視スレッドに乗せて
// ホットリロードできるようにすると、実行したまま数値を詰められる。
//
// 【方針】
// ・全ての値に既定値を持たせる。JSONが無くても、項目が欠けても動く。
// ・座標とサイズは基準解像度(1920x1080)でのピクセル値。
//   実際の描画では画面サイズに応じたスケールを掛ける。
// ・パースはメインスレッドで行う。監視スレッドのコールバックから
//   直接書き換えると、描画中の値が途中で入れ替わる。
// ===========================================================
struct CombatHudConfig
{
	using json = nlohmann::json;

	//------------------------------------------
	// 基準解像度
	//------------------------------------------
	// ここに書いた解像度でのピクセル値として全ての寸法を扱う。
	// 実解像度との比でスケールするので、どの画面でも同じ見た目になる。
	float baseWidth = 1920.0f;
	float baseHeight = 1080.0f;

	// 画面端の安全余白(%)。テレビ出力を想定して確保する。
	float safeAreaPercent = 0.035f;

	//------------------------------------------
	// 注目度
	//------------------------------------------
	struct Fade
	{
		float idleAlphaHp = 0.58f;		// HPの平常時の不透明度
		float idleAlphaOther = 0.40f;	// その他の平常時
		float holdHp = 2.4f;			// 変化後に濃いままでいる時間
		float holdBreath = 1.1f;
		float holdCount = 0.9f;
	} fade;

	//------------------------------------------
	// プレイヤーの生命線
	//------------------------------------------
	struct Health
	{
		float widthRatio = 0.20f;	// 画面幅に対する比
		float halfHeight = 34.0f;
		float marginX = 0.05f;		// 画面幅に対する比
		float marginY = 62.0f;		// 画面下からの距離
		float dangerLine = 0.35f;	// これを下回ると危険域
		float lastStandLine = 0.20f;	// 最後の1目盛り。別格の警告に切り替わる。
		float shockScale = 0.7f;	// 被弾時の反応の強さ(揺れ・亀裂・赤み)
		float numShowSec = 1.6f;	// 数値を出しておく時間
	} health;

	//------------------------------------------
	// 敵の頭上ゲージ
	//------------------------------------------
	struct EnemyBar
	{
		float halfW = 26.0f;	// 敵より目立たせない。ロック中だけ大きくする。
		float halfH = 11.0f;
		float offsetY = 10.0f;		// 敵の上どれだけに出すか
		float lockedGap = 6.0f;		// ロック中はマーカーを避けてさらに上へ
		float range = 45.0f;		// これより遠い敵には出さない
		float minAlpha = 0.55f;		// 遠くても読める下限
		float minSpacing = 46.0f;	// これより近い位置に重なったら間引く
		int   maxVisible = 8;		// 同時に出す上限。多いと画面が埋まる。
		float lockedScale = 1.45f;	// ロック中の敵は大きく出す
		float offCenterFade = 0.45f;	// 画面中心から離れた敵をどこまで薄めるか
	} enemyBar;

	//------------------------------------------
	// ロックマーカー
	//------------------------------------------
	// 大きさは refDistance の位置で refSize になるよう決める。
	// ★refDistance はワールド座標。縮尺を変えたらここも合わせる。
	struct Lock
	{
		float refDistance = 8.0f;	// 基準の距離
		float refSize = 22.0f;		// その距離での半径(px)
		float minRadius = 10.0f;
		float maxRadius = 30.0f;
	} lock;

	//------------------------------------------
	// 攻撃予兆
	//------------------------------------------
	struct Warn
	{
		float radius = 16.0f;
		float offsetY = 30.0f;
		float quadScale = 3.2f;		// 衝撃波が伸びるぶんの余白
		float edgeMargin = 48.0f;	// 画面端インジケータの余白
	} warn;

	//------------------------------------------
	// 方位計
	//------------------------------------------
	struct Compass
	{
		float radius = 145.0f;
		float range = 60.0f;		// 近さの基準距離
		int   maxMarkers = 3;		// 同時に出す数。多いと画面端が埋まる。
	} compass;

	//------------------------------------------
	// 敵の残数
	//------------------------------------------
	struct Count
	{
		float posY = 14.0f;		// スコアの下にどれだけ離すか
		float dotWidth = 13.0f;
		float maxWidthRatio = 0.34f;
		float halfH = 8.0f;
		int   maxDots = 20;
		int   urgentCount = 3;		// これ以下は常に濃く出す
	} count;

	//------------------------------------------
	// 撃破数と経過時間
	//------------------------------------------
	struct Score
	{
		float halfW = 138.0f;	// 記号・数字・総数が並ぶ幅
		float halfH = 54.0f;	// 2段(撃破数 / 経過時間)ぶんの高さ
		float marginX = 8.0f;
		float marginY = 8.0f;
		float idleAlpha = 0.95f;	// 平常時の不透明度。薄すぎると読めない。
	} score;

	//------------------------------------------
	// コンボ
	//------------------------------------------
	struct Combo
	{
		float posXRatio = 0.78f;
		float posYRatio = 0.28f;
		float halfW = 150.0f;	// 上段にランク語、下段に数字とHIT!!
		float halfH = 52.0f;	// 横に広げず縦に積んで、視界を塞がない
		float popSec = 0.30f;
	} combo;

	//------------------------------------------
	// ダメージ表示
	//------------------------------------------
	struct Damage
	{
		float halfW = 40.0f;
		float halfH = 44.0f;
		float lifeSec = 0.9f;
		int   maxCount = 24;
	} damage;

	//------------------------------------------
	// 点表示(空中攻撃の残りなど)
	//------------------------------------------
	struct Pips
	{
		float halfW = 34.0f;
		float halfH = 9.0f;
		float offsetY = 108.0f;		// HPの上にどれだけ離して置くか
	} pips;

	//------------------------------------------
	// 操作ガイド
	//------------------------------------------
	struct Guide
	{
		float cardW = 88.0f;
		float cardH = 98.0f;
		float gap = 8.0f;
		int   cols = 2;
		float rippleSec = 0.32f;
	} guide;

	//------------------------------------------
	// 演出
	//------------------------------------------
	struct Fx
	{
		float breakFlashSec = 0.55f;	// 体幹を崩した瞬間の閃光
		float deathBurstSec = 0.7f;		// 撃破時にゲージが爆散する時間
		float counterFlashSec = 0.65f;	// カウンター成立の閃光
		float victoryDipSec = 1.2f;		// 殲滅時に操作ガイドが沈む時間
		float victoryDipAmount = 0.75f;	// どこまで沈めるか(1で完全に消える)

		float comboDropSec = 1.10f;		// コンボが途切れて落ちる時間
		float comboWarnLine = 0.35f;	// 猶予がこれを切ると崩れ始める
		float healFlashSec = 0.7f;		// 回復の光が走る時間
		float idleSleepSec = 8.0f;		// 戦闘が途切れてからHUDが眠るまで
		float idleSleepAlpha = 0.35f;	// 眠っている間の不透明度
		float deathCollapseSec = 1.4f;	// 死亡時にHUDが崩れる時間
		float desatOnCounter = 0.55f;	// カウンター受付中に彩度を落とす量
		float mergeRadius = 42.0f;		// ダメージ数値を合流させる距離(px)
		float mergeSec = 0.28f;			// 合流を受け付ける時間
	} fx;

	//------------------------------------------
	// 表示要素のON/OFF
	//------------------------------------------
	// 撮影やデバッグで要素を切れるようにする。
	// ポートフォリオの動画を撮るときに効く。
	struct Visible
	{
		bool health = true;
		bool enemyBar = true;
		bool warn = true;
		bool lock = true;
		bool counterRing = true;
		bool focus = true;
		bool compass = true;
		bool count = false;			// 卵の残数。左上の「3/20」と情報が重複する。
		bool combo = true;
		bool damage = true;
		bool guide = true;
		bool wave = true;
		bool pips = true;
		bool score = true;			// 撃破数と経過時間
	} visible;

	//------------------------------------------
	// 配色
	//------------------------------------------
	// ニワトリの3系統(鶏冠の赤 / くちばしの黄 / 羽根のクリーム)を基調にする。
	struct Palette
	{
		Math::Vector4 life = { 0.40f, 0.80f, 0.46f, 1.00f };	// HP(緑)
		Math::Vector4 lifeDark = { 0.05f, 0.07f, 0.06f, 0.86f };
		Math::Vector4 beak = { 0.92f, 0.76f, 0.32f, 1.00f };
		Math::Vector4 plume = { 0.93f, 0.90f, 0.82f, 0.95f };
		Math::Vector4 plumeDark = { 0.62f, 0.58f, 0.52f, 0.70f };	// 割れた卵の輪郭
		Math::Vector4 enemyHp = { 1.00f, 0.20f, 0.18f, 1.00f };
		Math::Vector4 enemyBg = { 0.02f, 0.02f, 0.03f, 0.96f };
		Math::Vector4 alert = { 0.94f, 0.40f, 0.28f, 0.95f };
		Math::Vector4 alertHi = { 1.00f, 0.96f, 0.86f, 1.00f };
		Math::Vector4 counter = { 0.58f, 0.86f, 0.96f, 1.00f };
		Math::Vector4 counterBg = { 0.46f, 0.80f, 0.94f, 0.26f };
		// 常に出ているものなので彩度を落とす。鮮やかだと視界を奪う。
		Math::Vector4 compass = { 0.62f, 0.26f, 0.24f, 0.70f };
		Math::Vector4 compassHi = { 0.82f, 0.38f, 0.30f, 0.85f };
		Math::Vector4 comboHot = { 0.96f, 0.58f, 0.26f, 1.00f };

		// 撃破数と経過時間。主と副で色を分ける。
		// 全部同じ白だと、どこが本命の数字か分からなくなる。
		Math::Vector4 score = { 1.00f, 0.86f, 0.42f, 1.00f };	// 撃破数(くちばしの黄)
		Math::Vector4 scoreSub = { 0.72f, 0.82f, 0.92f, 1.00f };	// 総数と時間(青灰)
		Math::Vector4 damage = { 0.96f, 0.93f, 0.86f, 1.00f };
		Math::Vector4 damageHot = { 1.00f, 0.66f, 0.24f, 1.00f };

		// ゲームパッドのボタン色。実機の表記に合わせる。
		Math::Vector4 padA = { 0.42f, 0.72f, 0.30f, 1.00f };
		Math::Vector4 padB = { 0.84f, 0.24f, 0.22f, 1.00f };
		Math::Vector4 padX = { 0.20f, 0.46f, 0.82f, 1.00f };
		Math::Vector4 padY = { 0.92f, 0.72f, 0.18f, 1.00f };
		Math::Vector4 padNeutral = { 0.30f, 0.32f, 0.36f, 1.00f };
		Math::Vector4 keycap = { 0.26f, 0.27f, 0.31f, 1.00f };
	} color;

	//===================================================
	// JSONからの読み込み
	//===================================================
	// 項目が欠けていても既定値のままにする。
	// 調整したい値だけを書いた小さなJSONで運用できる。
	void Load(const json& _j)
	{
		ReadF(_j, "baseWidth", baseWidth);
		ReadF(_j, "baseHeight", baseHeight);
		ReadF(_j, "safeAreaPercent", safeAreaPercent);

		if (const json* o = Sub(_j, "fade"))
		{
			ReadF(*o, "idleAlphaHp", fade.idleAlphaHp);
			ReadF(*o, "idleAlphaOther", fade.idleAlphaOther);
			ReadF(*o, "holdHp", fade.holdHp);
			ReadF(*o, "holdBreath", fade.holdBreath);
			ReadF(*o, "holdCount", fade.holdCount);
		}

		if (const json* o = Sub(_j, "health"))
		{
			ReadF(*o, "widthRatio", health.widthRatio);
			ReadF(*o, "halfHeight", health.halfHeight);
			ReadF(*o, "marginX", health.marginX);
			ReadF(*o, "marginY", health.marginY);
			ReadF(*o, "dangerLine", health.dangerLine);
			ReadF(*o, "lastStandLine", health.lastStandLine);
			ReadF(*o, "shockScale", health.shockScale);
			ReadF(*o, "numShowSec", health.numShowSec);
		}

		if (const json* o = Sub(_j, "enemyBar"))
		{
			ReadF(*o, "halfW", enemyBar.halfW);
			ReadF(*o, "halfH", enemyBar.halfH);
			ReadF(*o, "offsetY", enemyBar.offsetY);
			ReadF(*o, "lockedGap", enemyBar.lockedGap);
			ReadF(*o, "range", enemyBar.range);
			ReadF(*o, "minAlpha", enemyBar.minAlpha);
			ReadF(*o, "minSpacing", enemyBar.minSpacing);
			ReadI(*o, "maxVisible", enemyBar.maxVisible);
			ReadF(*o, "lockedScale", enemyBar.lockedScale);
			ReadF(*o, "offCenterFade", enemyBar.offCenterFade);
		}

		if (const json* o = Sub(_j, "lock"))
		{
			ReadF(*o, "refDistance", lock.refDistance);
			ReadF(*o, "refSize", lock.refSize);
			ReadF(*o, "minRadius", lock.minRadius);
			ReadF(*o, "maxRadius", lock.maxRadius);
		}

		if (const json* o = Sub(_j, "warn"))
		{
			ReadF(*o, "radius", warn.radius);
			ReadF(*o, "offsetY", warn.offsetY);
			ReadF(*o, "quadScale", warn.quadScale);
			ReadF(*o, "edgeMargin", warn.edgeMargin);
		}

		if (const json* o = Sub(_j, "compass"))
		{
			ReadF(*o, "radius", compass.radius);
			ReadF(*o, "range", compass.range);
			ReadI(*o, "maxMarkers", compass.maxMarkers);
		}

		if (const json* o = Sub(_j, "count"))
		{
			ReadF(*o, "posY", count.posY);
			ReadF(*o, "dotWidth", count.dotWidth);
			ReadF(*o, "maxWidthRatio", count.maxWidthRatio);
			ReadF(*o, "halfH", count.halfH);
			ReadI(*o, "maxDots", count.maxDots);
			ReadI(*o, "urgentCount", count.urgentCount);
		}

		if (const json* o = Sub(_j, "score"))
		{
			ReadF(*o, "halfW", score.halfW);
			ReadF(*o, "halfH", score.halfH);
			ReadF(*o, "marginX", score.marginX);
			ReadF(*o, "marginY", score.marginY);
			ReadF(*o, "idleAlpha", score.idleAlpha);
		}

		if (const json* o = Sub(_j, "combo"))
		{
			ReadF(*o, "posXRatio", combo.posXRatio);
			ReadF(*o, "posYRatio", combo.posYRatio);
			ReadF(*o, "halfW", combo.halfW);
			ReadF(*o, "halfH", combo.halfH);
			ReadF(*o, "popSec", combo.popSec);
		}

		if (const json* o = Sub(_j, "damage"))
		{
			ReadF(*o, "halfW", damage.halfW);
			ReadF(*o, "halfH", damage.halfH);
			ReadF(*o, "lifeSec", damage.lifeSec);
			ReadI(*o, "maxCount", damage.maxCount);
		}

		if (const json* o = Sub(_j, "pips"))
		{
			ReadF(*o, "halfW", pips.halfW);
			ReadF(*o, "halfH", pips.halfH);
			ReadF(*o, "offsetY", pips.offsetY);
		}

		if (const json* o = Sub(_j, "guide"))
		{
			ReadF(*o, "cardW", guide.cardW);
			ReadF(*o, "cardH", guide.cardH);
			ReadF(*o, "gap", guide.gap);
			ReadI(*o, "cols", guide.cols);
			ReadF(*o, "rippleSec", guide.rippleSec);
		}

		if (const json* o = Sub(_j, "fx"))
		{
			ReadF(*o, "breakFlashSec", fx.breakFlashSec);
			ReadF(*o, "deathBurstSec", fx.deathBurstSec);
			ReadF(*o, "comboDropSec", fx.comboDropSec);
			ReadF(*o, "comboWarnLine", fx.comboWarnLine);
			ReadF(*o, "healFlashSec", fx.healFlashSec);
			ReadF(*o, "idleSleepSec", fx.idleSleepSec);
			ReadF(*o, "idleSleepAlpha", fx.idleSleepAlpha);
			ReadF(*o, "deathCollapseSec", fx.deathCollapseSec);
			ReadF(*o, "desatOnCounter", fx.desatOnCounter);
			ReadF(*o, "mergeRadius", fx.mergeRadius);
			ReadF(*o, "mergeSec", fx.mergeSec);
			ReadF(*o, "counterFlashSec", fx.counterFlashSec);
			ReadF(*o, "victoryDipSec", fx.victoryDipSec);
			ReadF(*o, "victoryDipAmount", fx.victoryDipAmount);
		}

		if (const json* o = Sub(_j, "visible"))
		{
			ReadB(*o, "health", visible.health);
			ReadB(*o, "enemyBar", visible.enemyBar);
			ReadB(*o, "warn", visible.warn);
			ReadB(*o, "lock", visible.lock);
			ReadB(*o, "counterRing", visible.counterRing);
			ReadB(*o, "focus", visible.focus);
			ReadB(*o, "compass", visible.compass);
			ReadB(*o, "count", visible.count);
			ReadB(*o, "combo", visible.combo);
			ReadB(*o, "damage", visible.damage);
			ReadB(*o, "guide", visible.guide);
			ReadB(*o, "wave", visible.wave);
			ReadB(*o, "pips", visible.pips);
			ReadB(*o, "score", visible.score);
		}

		if (const json* o = Sub(_j, "color"))
		{
			ReadC(*o, "life", color.life);
			ReadC(*o, "lifeDark", color.lifeDark);
			ReadC(*o, "beak", color.beak);
			ReadC(*o, "plume", color.plume);
			ReadC(*o, "plumeDark", color.plumeDark);
			ReadC(*o, "enemyHp", color.enemyHp);
			ReadC(*o, "enemyBg", color.enemyBg);
			ReadC(*o, "alert", color.alert);
			ReadC(*o, "alertHi", color.alertHi);
			ReadC(*o, "counter", color.counter);
			ReadC(*o, "counterBg", color.counterBg);
			ReadC(*o, "compass", color.compass);
			ReadC(*o, "compassHi", color.compassHi);
			ReadC(*o, "comboHot", color.comboHot);
			ReadC(*o, "score", color.score);
			ReadC(*o, "scoreSub", color.scoreSub);
			ReadC(*o, "damage", color.damage);
			ReadC(*o, "damageHot", color.damageHot);
			ReadC(*o, "padA", color.padA);
			ReadC(*o, "padB", color.padB);
			ReadC(*o, "padX", color.padX);
			ReadC(*o, "padY", color.padY);
			ReadC(*o, "padNeutral", color.padNeutral);
			ReadC(*o, "keycap", color.keycap);
		}
	}

private:

	static const json* Sub(const json& _j, const char* _key)
	{
		auto it = _j.find(_key);
		return (it != _j.end() && it->is_object()) ? &(*it) : nullptr;
	}

	static void ReadF(const json& _j, const char* _key, float& _out)
	{
		auto it = _j.find(_key);
		if (it != _j.end() && it->is_number()) _out = it->get<float>();
	}

	static void ReadB(const json& _j, const char* _key, bool& _out)
	{
		auto it = _j.find(_key);
		if (it != _j.end() && it->is_boolean()) _out = it->get<bool>();
	}

	static void ReadI(const json& _j, const char* _key, int& _out)
	{
		auto it = _j.find(_key);
		if (it != _j.end() && it->is_number_integer()) _out = it->get<int>();
	}

	// 色は [r, g, b, a] の配列で書く。aは省略可。
	static void ReadC(const json& _j, const char* _key, Math::Vector4& _out)
	{
		auto it = _j.find(_key);
		if (it == _j.end() || !it->is_array()) return;

		const auto& a = *it;
		if (a.size() >= 3)
		{
			_out.x = a[0].get<float>();
			_out.y = a[1].get<float>();
			_out.z = a[2].get<float>();
		}
		if (a.size() >= 4) _out.w = a[3].get<float>();
	}
};