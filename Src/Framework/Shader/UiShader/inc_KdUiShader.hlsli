#ifndef INC_KD_UI_SHADER_HLSLI
#define INC_KD_UI_SHADER_HLSLI

#include "inc_KdHudCommon.hlsli"

//===================================================
//
// UI共通部品 シェーダ 種別定義
//
//===================================================

// 種別。C++側の KdUiShader::Kind と必ず一致させること
#define UI_KIND_BUTTON			0	// ボタン(細長い平行四辺形)
#define UI_KIND_BUTTON_ROUND	1	// ボタン(角丸)
#define UI_KIND_BUTTON_TAB		2	// ボタン(札)
#define UI_KIND_BUTTON_PLATE	3	// ボタン(帳票の欄。リザルト向け)
#define UI_KIND_VEIL		4	// 暗幕(画面を沈める下地)
#define UI_KIND_PANEL		5	// 枠(項目をまとめる区画)
#define UI_KIND_LABEL		6	// 見出しの語
#define UI_KIND_HINT		7	// 操作ヒント(アイコン + 短い語)

//===================================================
// 語の受け渡し
//===================================================
// ボタンや見出しの語は「文字コード」で渡す。
//   Params.xyzw = 1〜4文字目
//   Extra.xy    = 5〜6文字目
//   0=A .. 25=Z、負で「その桁は無し」。
//
// enum で語を持つと、語を足すたびに共通シェーダを触ることになる。
// コードで渡せば、C++側で文字列を変換するだけで済む。
#define UI_WORD_MAX			6

// Params/Extra から語を取り出す。
void UiDecodeWord(float4 _params, float4 _extra, out int _codes[8], out int _count)
{
	[unroll]
	for (int i = 0; i < 8; ++i)
		_codes[i] = -1;

	_codes[0] = (int) _params.x;
	_codes[1] = (int) _params.y;
	_codes[2] = (int) _params.z;
	_codes[3] = (int) _params.w;
	_codes[4] = (int) _extra.x;
	_codes[5] = (int) _extra.y;

	// 負が出たらそこで打ち切る。途中に穴が空くことは想定しない。
	_count = 0;
	[unroll]
	for (int k = 0; k < UI_WORD_MAX; ++k)
	{
		if (_codes[k] >= 0)
			_count = k + 1;
	}
}

//===================================================
// ボタンの状態
//===================================================
void UiDecodeState(float _packed, out float _focus, out float _press, out float _ghost)
{
	_ghost = saturate(-_packed);

	const float pos = max(_packed, 0.0f);
	_focus = step(1.5f, pos);
	_press = saturate(pos - _focus * 2.0f);
}

//===================================================
// ボタンの先頭アイコン
//===================================================
// SubColor.a / Extra.z の整数部に「形 * UI_ICON_STEP + (文字コード + 1)」。
// ★C++側の KdUiShader::BtnIcon / kIconStep と一致させること。
#define UI_ICON_NONE		0
#define UI_ICON_FACE		1	// フェイスボタン(丸に文字)
#define UI_ICON_ENTER		2	// Enter(キーキャップ + 矢印)
#define UI_ICON_SPACE		3	// Space(横長のキーキャップ)
#define UI_ICON_KEYCAP		4	// 任意の1文字のキーキャップ
#define UI_ICON_SHOULDER	5	// LB/RB
#define UI_ICON_TRIGGER		6	// LT/RT

#define UI_ICON_STEP		32

void UiDecodeIcon(float _packed, out int _shape, out int _code)
{
	const int v = (int) _packed;

	_shape = v / UI_ICON_STEP;
	_code = (v % UI_ICON_STEP) - 1; // -1 で文字なし
}

//===================================================
// ボタンの形
//===================================================
// UI_KIND_BUTTON からの差で判定する。
#define UI_BTN_SLIM			0	// 細長い平行四辺形。輪郭が荒れる。タイトル向け。
#define UI_BTN_ROUND		1	// 角丸。設定画面などの落ち着いた場面向け。
#define UI_BTN_TAB			2	// 上辺だけ角を残した札。並べて使う。
#define UI_BTN_PLATE		3	// 帳票の欄。左端に帯、下に罫線。リザルト向け。

#endif // INC_KD_UI_SHADER_HLSLI
