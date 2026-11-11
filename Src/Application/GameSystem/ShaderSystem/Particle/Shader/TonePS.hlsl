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
float3 LinearToSRGB(float3 c)
{
	float3 lo = c * 12.92;
	float3 hi = pow(max(c, 0.0001), 1.0 / 2.4) * 1.055 - 0.055;
	return (c <= 0.0031308) ? lo : hi;
}

struct VSOut
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
};
float4 main(VSOut i) : SV_Target
{
	float3 hdr = g_hdrTex.Sample(g_sampler, i.uv).rgb * g_exposure;
	float3 mapped = ACESFilm(hdr);
	mapped = LinearToSRGB(mapped);
	return float4(mapped, 1.0);
}
