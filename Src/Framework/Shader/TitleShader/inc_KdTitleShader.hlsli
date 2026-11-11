#ifndef INC_KD_TITLE_SHADER_HLSLI
#define INC_KD_TITLE_SHADER_HLSLI

struct VSOutput
{
	float4 Pos : SV_POSITION;
	float2 UV : TEXCOORD0;
};

cbuffer cbTitle : register(b0)
{
	float2 g_center; //  8 : 画面ピクセル座標(左上原点)
	float2 g_halfSize; // 16 : 半サイズ(ピクセル)

	float2 g_screenSize; // 24
	float g_time; // 28 : 経過秒(実時間)
	float g_appear; // 32 : 登場(0→1)

	float4 g_tint; // 48 : 全体の色と不透明度

	float g_heat; // 52 : 陽炎の強さ
	float g_sheen; // 56 : 光の走りの強さ
	float g_edgeWave; // 60 : 縁の揺らぎ
	float g_dust; // 64 : 砂粒の量

	float g_breath; // 68 : 呼吸(拡縮)の強さ。既定は0。
	float g_shake; // 72 : 一時的な揺れ(選択の移動など)
	float g_dissolve; // 76 : 剥がれて散る量(決定時)
	float g_titlePad; // 80
};

Texture2D g_logo : register(t0);
SamplerState g_samp : register(s0);

#define TITLE_PI 3.14159265f

#endif // INC_KD_TITLE_SHADER_HLSLI
