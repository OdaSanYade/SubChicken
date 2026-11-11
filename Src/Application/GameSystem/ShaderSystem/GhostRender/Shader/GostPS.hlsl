Texture2D g_gbufferC : register(t0);
SamplerState g_sampler : register(s0);
cbuffer CB : register(b0)
{
	row_major float4x4 g_wvp;
	row_major float4x4 g_world;
	float4 g_color;
	float4 g_rimColor;
	float4 g_moveDirSpeed;
	float3 g_camPos;
	int g_boneOffset;
	float g_time;
	float3 _pad0;
};
struct VS_OUT
{
	float4 pos : SV_Position;
	float3 normal : NORMAL;
	float3 worldPos : POSITION1;
	float dist : DIST;
	float4 clip : CLIPPOS;
};

static const float kBayer4x4[16] =
{
	0.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0,
    12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0, 6.0 / 16.0,
     3.0 / 16.0, 11.0 / 16.0, 1.0 / 16.0, 9.0 / 16.0,
    15.0 / 16.0, 7.0 / 16.0, 13.0 / 16.0, 5.0 / 16.0,
};

float3 RGBtoHSV(float3 c)
{
	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	float4 p = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
	float4 q = lerp(float4(p.xyw, c.r), float4(c.r, p.yzx), step(p.x, c.r));
	float d = q.x - min(q.w, q.y);
	float e = 1.0e-10;
	return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
float3 HSVtoRGB(float3 c)
{
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp(K.xxx, saturate(p - K.xxx), c.y);
}

float4 main(VS_OUT In) : SV_Target
{
	float fade = saturate(g_color.a);
	float ageRatio = 1.0 - fade;

    // ★寿命によるディザ抜き：50%抜けた状態からスタートして100%へ
	float ditherThreshold = lerp(0.5, 1.0, ageRatio);

    // ★簡易的な陽炎ゆらぎ(本格的な屈折ではなく抜けパターン/遮蔽サンプルを揺らす近似)
	float wobble = sin(In.worldPos.y * 8.0 + g_time * 4.0) * 0.6
                 + sin(In.worldPos.x * 6.0 - g_time * 3.0) * 0.4;

	uint2 pix = (uint2) (In.pos.xy + wobble) % 4;
	float ditherValue = kBayer4x4[pix.y * 4 + pix.x];
	clip(ditherValue - ditherThreshold);

	float2 suv = In.clip.xy / In.clip.w;
	suv = suv * float2(0.5, -0.5) + 0.5;
	suv.x += wobble * 0.0015;
	float3 sceneWP = g_gbufferC.Sample(g_sampler, suv).xyz;
	float sceneDist = (length(sceneWP) < 0.001) ? 10000.0 : length(g_camPos - sceneWP);
	clip(sceneDist - In.dist + 0.05);

	float3 V = normalize(g_camPos - In.worldPos);
	float3 N = normalize(In.normal);
	float fres = pow(1.0 - saturate(dot(N, V)), 2.0);
	float intensity = 0.25 + fres * 1.5;

	float3 baseHSV = RGBtoHSV(g_color.rgb);
	baseHSV.x = frac(baseHSV.x + ageRatio * 0.6);
	float3 baseColor = HSVtoRGB(baseHSV);

	float3 finalColor = lerp(baseColor, g_rimColor.rgb, fres);

	finalColor = lerp(finalColor, float3(1, 1, 1), g_rimColor.a * 0.5);

	return float4(finalColor * intensity, intensity);
}
