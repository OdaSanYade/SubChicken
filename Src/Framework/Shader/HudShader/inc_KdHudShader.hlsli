#ifndef INC_KD_HUD_SHADER_HLSLI
#define INC_KD_HUD_SHADER_HLSLI

#include "../UiShader/inc_KdHudCommon.hlsli"

//===================================================
// 戦闘HUD シェーダ 種別定義
//===================================================

// 種別。C++側の KdHudShader::Kind と必ず一致させること。
#define HUD_KIND_LOCK		0	// ロックオンの羽根
#define HUD_KIND_GAUGE		1	// 敵の頭上ゲージ
#define HUD_KIND_WARN		2	// 攻撃予兆の収束リング
#define HUD_KIND_ARROW		3	// 画面端の予兆インジケータ
#define HUD_KIND_RING		4	// カウンター受付の円弧
#define HUD_KIND_WAVE		5	// 鳴き声の波紋
#define HUD_KIND_HEALTH		6	// プレイヤーの生命線(HP + 肺活量)
#define HUD_KIND_COUNT		7	// 敵の残数(卵)
#define HUD_KIND_COMPASS	8	// 画面外の敵の方位
#define HUD_KIND_COMBO		9	// コンボ
#define HUD_KIND_FOCUS		10	// ジャスト回避の集中線
#define HUD_KIND_DAMAGE		11	// ダメージの数値
#define HUD_KIND_GUIDE		12	// 操作ガイド
#define HUD_KIND_PIPS		13	// 点で数を示す汎用表示(空中攻撃の残りなど)
#define HUD_KIND_SCORE		14	// 撃破数と経過時間

//===================================================
// 敵ゲージのクアッド拡張
//===================================================
// 帯の上に「12/40」を置くための余白。
// クアッドをこの倍率で広げ、シェーダ側は同じ値で割って帯の寸法に戻す。
// 文字だけ大きくしようとしても、クアッドの外は描けない

#define HUD_HEALTH_PAD_X	0.75f
#define HUD_HEALTH_PAD_Y	0.45f

#define HUD_HEALTH_BEAT		0.15f

#define HUD_ENEMYBAR_EXPAND_X	1.45f
#define HUD_ENEMYBAR_EXPAND_Y	2.30f

// 敵ゲージの上に出す数値の大きさ
#define HUD_ENEMYBAR_TEXT	0.44f

//===================================================
// 操作ガイドのボタン形状。C++側の KdHudShader::BtnShape と一致させること。
//===================================================
#define HUD_BTN_FACE		0	// Xboxのフェイスボタン(A/B/X/Y)。丸に文字。
#define HUD_BTN_SHOULDER	1	// LB/RB。横長の角丸に文字。
#define HUD_BTN_TRIGGER		2	// LT/RT。上が細い台形に文字。
#define HUD_BTN_DPAD		3	// 十字キー
#define HUD_BTN_MENU		4	// Start/Back
#define HUD_BTN_KEYCAP		5	// キーボードのキーキャップ
#define HUD_BTN_MOUSE_L		6	// マウス左ボタン。本体の形 + 名称。
#define HUD_BTN_MOUSE_R		7	// マウス右ボタン
#define HUD_BTN_MOUSE_M		8	// マウス中ボタン(ホイール)
#define HUD_BTN_WORDKEY		9	// 語で書くキー(SHIFT/SPACEなど)

//===================================================
// 語で書くキーの種類。C++側の KdHudShader::KeyWord と一致させること。
//===================================================
#define HUD_WORD_SHIFT		0
#define HUD_WORD_CTRL		1
#define HUD_WORD_ALT		2
#define HUD_WORD_SPACE		3
#define HUD_WORD_ENTER		4
#define HUD_WORD_TAB		5
#define HUD_WORD_ESC		6

//===================================================
// 操作ガイドの動作アイコン
//===================================================
#define HUD_ACT_ATTACK		0	// 弱攻撃(小さく突く)
#define HUD_ACT_HEAVY		1	// 強攻撃(大きく突いて衝撃波)
#define HUD_ACT_DODGE		2	// 回避(丸まって転がる)
#define HUD_ACT_CRY			3	// 鳴き声(横顔と音)

#endif // INC_KD_HUD_SHADER_HLSLI
