Texture2D g_hdrTex : register(t0);
SamplerState g_sampler : register(s0);
cbuffer CB : register(b0)
{
	float g_exposure;
	float3 _pad;
}

float3 ACESFilm(float3 x)
{
	const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
	return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

struct VSOut
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
};
float4 main(VSOut i) : SV_Target
{
	float4 hdr = g_hdrTex.Sample(g_sampler, i.uv);
	if (hdr.a <= 0.0001)
		discard; // パーティクルが無いピクセルは触らない

	float3 mapped = ACESFilm(hdr.rgb * g_exposure);
	return float4(mapped, saturate(hdr.a));
}
