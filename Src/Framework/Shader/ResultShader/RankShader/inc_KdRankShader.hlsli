#ifndef INC_KD_RANK_SHADER_HLSLI
#define INC_KD_RANK_SHADER_HLSLI

//===================================================
//
// ランクの絵 シェーダ 共通定義
//
// 卵から出てきたランクのアイコンを描く。
// 殻はSDF(KdResultShader)側が受け持つので、ここは中身だけ。
//
// ランクごとに動きを変える。
// 同じ動きだと、SもDも「同じものの色違い」に見えてしまう。
//
//===================================================

struct VSOutput
{
	float4 Pos : SV_POSITION;
	float2 UV : TEXCOORD0;
	float2 Local : TEXCOORD1; // 中心からの正規化座標(-1〜1)
};

cbuffer cbRank : register(b0)
{
	float2 g_center; //  8 : 画面ピクセル座標(左上原点)
	float2 g_halfSize; // 16 : 半サイズ(ピクセル)

	float2 g_screenSize; // 24
	float g_time; // 28 : 経過秒
	float g_hatch; // 32 : 孵化の進み(0→1)。殻と同じ値を渡す。

	float4 g_tint; // 48

	float g_rank; // 52 : 0=D .. 4=S
	float g_pad0; // 56
	float g_pad1; // 60
	float g_pad2; // 64
};

Texture2D g_rankTex : register(t0);
SamplerState g_samp : register(s0);

#define RANK_PI 3.14159265f

//===================================================
// ランク
//===================================================
// ★C++側の ResultHud / BattleResult::Rank と一致させること。
#define RANK_D		0
#define RANK_C		1
#define RANK_B		2
#define RANK_A		3
#define RANK_S		4

//===================================================
// 段取り
//===================================================
// 殻が割れる前は見えない。割れてから飛び出す。
// ★KdResultShader_PS の DrawRank と同じ区切りにすること。
//   ずれると、殻が割れる前に中身が透けたり、間が空いたりする。
#define RANK_BREAK_AT	0.58f
#define RANK_BREAK_END	0.72f

//===================================================
// 板の余白
//===================================================
#define RANK_TEX_FIT	0.46f

#endif // INC_KD_RANK_SHADER_HLSLI
