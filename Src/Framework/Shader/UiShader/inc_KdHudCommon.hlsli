#ifndef INC_KD_HUD_COMMON_HLSLI
#define INC_KD_HUD_COMMON_HLSLI

//===================================================
//
// HUD系シェーダの共通定義
//
//===================================================

// 1マーカー分の情報。
// C++側の KdHudShader::Instance とバイト単位で一致させること(stride 96)
struct HudInstance
{
	float2 Center; //  8 : 画面ピクセル座標(左上原点)
	float2 HalfSize; // 16 : 半径相当(ピクセル)
	float4 Color; // 32 : 主色
	float4 SubColor; // 48 : 副色(背景・縁取り・強調)
	float4 Params; // 64 : 種別ごとの意味を持つ4値
	float4 Extra; // 80 : 種別ごとの追加4値
	float Tilt; // 84 : クアッドの傾き(rad)
	uint Kind; // 88
	uint2 Pad; // 96
};

struct VSOutput
{
	float4 Pos : SV_POSITION;
	float2 Local : TEXCOORD0; // 中心からのピクセル offset(回転前)

	// 補間する意味が無い値は nointerpolation で渡す
	nointerpolation float2 HalfSize : TEXCOORD1;
	nointerpolation float4 Color : COLOR0;
	nointerpolation float4 SubColor : COLOR1;
	nointerpolation float4 Params : TEXCOORD2;
	nointerpolation float4 Extra : TEXCOORD3;
	nointerpolation uint Kind : TEXCOORD4;
};

cbuffer cbHud : register(b0)
{
	float2 g_screenSize;
	float g_time; // 経過秒(実時間)。周期アニメーションの位相に使う。
	float g_hudPad;
};

//===================================================
// 共通の意匠パラメータ
//===================================================
// 帯の斜め切りの角度。UI全体で必ずこの値を使う。
#define HUD_SKEW		0.75f

// 二重縁取りの厚み(px)
#define HUD_EDGE_DARK	2.4f
#define HUD_EDGE_HALO	1.6f

// 線の太さの階層。重要度と太さを対応させる。
#define HUD_LINE_THIN	1.0f	// 補助線・目盛り
#define HUD_LINE_BASE	1.8f	// 枠・区画
#define HUD_LINE_BOLD	3.0f	// 警告・危険

// 帯の斜め切り。UI全体で角度を統一するためのヘルパ。
float2 SkewBar(float2 p, float halfH)
{
	return float2(p.x + p.y * HUD_SKEW, p.y);
}

#endif // INC_KD_HUD_COMMON_HLSLI
