#ifndef INC_KD_CREDIT_SHADER_HLSLI
#define INC_KD_CREDIT_SHADER_HLSLI

//===================================================
//
// クレジット画面 共通定義
//
//===================================================

// 背景(全画面)
struct VSOutput
{
	float4 Pos : SV_POSITION;
	float2 UV : TEXCOORD0; // 0〜1、左上原点
};

// C++側の KdCreditShader::cbCredit と1バイトも違わないこと(48バイト)
cbuffer cbCredit : register(b0)
{
	float2 g_screenSize; //  8 : ビューポート(ピクセル)
	float g_time; // 12 : 経過秒(実時間)
	float g_appear; // 16 : 登場(0→1)。閉じる時は 1→0

	float g_scroll; // 20 : 本文のスクロール量(基準解像度px)
	float g_uiScale; // 24 : 基準解像度(1920x1080)からの倍率
	float2 g_origin; // 32 : 本文の原点(画面px)。x=中央、y=スクロール0での上端

	float2 g_shadowUV; // 40 : 影のずらし量(アトラスのUV)。ドット字なら1ドット
	uint g_instBase; // 44 : 描く範囲の先頭の行。SV_InstanceID は常に0から始まるので足す
	float g_fade; // 48 : 画面上下で薄れる幅(画面高に対する比)
};

//===================================================
// 本文の文字
//===================================================
// C++側の KdCreditShader::TextInst と1バイトも違わないこと(64バイト)
struct CreditTextInst
{
	float4 Rect; // x(中央から), y(上端), w, h  : 基準解像度px
	float4 UV; // u0, v0, u1, v1
	float4 Color;
	float4 Params; // x=縁取りの太さ(距離場の値) y=光の強さ
};

struct TextVSOutput
{
	float4 Pos : SV_POSITION;
	float2 UV : TEXCOORD0;
	nointerpolation float4 UVRect : TEXCOORD1; // 影のサンプルを字の中に留める
	nointerpolation float4 Color : COLOR0;
	nointerpolation float4 Params : TEXCOORD2;
};

#endif // INC_KD_CREDIT_SHADER_HLSLI
