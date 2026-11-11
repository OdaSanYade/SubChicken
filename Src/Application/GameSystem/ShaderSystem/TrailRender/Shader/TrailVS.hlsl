cbuffer CB : register(b0)
{
	row_major float4x4 g_viewProj;
	float3 g_camPos;
	float g_time;
};

// トレイル1本ぶんの見た目パラメータ（TrailRenderer::ParamSet と同じ並び）
struct TrailParams
{
	float4 prm;
	float4 prm2;
	float4 prm3; // xy のみ使用
	float4 prm4;
	float4 prm5;
	float4 prm6;
	float4 prm7; // xy のみ使用
};
StructuredBuffer<TrailParams> g_params : register(t0);

struct VS_IN
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float4 col : COLOR;
	uint pidx : PARAMINDEX;
};
struct VS_OUT
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
	float4 col : COLOR;
	float dist : DIST;
	float4 clip : CLIPPOS;
	nointerpolation float4 prm : TEXCOORD1;
	nointerpolation float4 prm2 : TEXCOORD2;
	nointerpolation float2 prm3 : TEXCOORD3;
	nointerpolation float4 prm4 : TEXCOORD4;
	nointerpolation float4 prm5 : TEXCOORD5;
	nointerpolation float4 prm6 : TEXCOORD6;
	nointerpolation float2 prm7 : TEXCOORD7;
};
VS_OUT main(VS_IN In)
{
	TrailParams P = g_params[In.pidx];

	VS_OUT o;
	o.pos = mul(float4(In.pos, 1.0), g_viewProj);
	o.uv = In.uv;
	o.col = In.col;
	o.dist = length(g_camPos - In.pos);
	o.clip = o.pos;
	o.prm = P.prm;
	o.prm2 = P.prm2;
	o.prm3 = P.prm3.xy;
	o.prm4 = P.prm4;
	o.prm5 = P.prm5;
	o.prm6 = P.prm6;
	o.prm7 = P.prm7.xy;
	return o;
}
