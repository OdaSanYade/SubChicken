#include "inc_XRay.hlsli"

Texture2D g_bufferE : register(t0);

//　true...隠れていないほうを描画,　false...隠れている方を描画
static const bool kDrawWhereMasked = false;

static const float kBayer4x4[16] =
{
	0.0 / 16.0, 8.0 / 16.0, 2.0 / 16.0, 10.0 / 16.0,
	12.0 / 16.0, 4.0 / 16.0, 14.0 / 16.0, 6.0 / 16.0,
	3.0 / 16.0, 11.0 / 16.0, 1.0 / 16.0, 9.0 / 16.0,
	15.0 / 16.0, 7.0 / 16.0, 13.0 / 16.0, 5.0 / 16.0,
};

float4 main(VS_OUT In) : SV_Target
{
	// ---- マスク判定 (補間なし・ピクセル直読み) ----
	float maskA = g_bufferE.Load(int3(In.pos.xy, 0)).a;
	bool inMask = (maskA >= 0.999f);
	if (inMask != kDrawWhereMasked)
		discard;

	// ---- フェード用ディザ (g_color.a = 1 で全表示、0 で全消し) ----
	float fade = saturate(g_color.a);

	float wobble = sin(In.worldPos.y * 8.0 + g_time * 4.0) * 0.6
	             + sin(In.worldPos.x * 6.0 - g_time * 3.0) * 0.4;

	// uint2 へ負の float をキャストすると未定義なので int2 で扱う
	int2 pix = ((int2) (In.pos.xy + wobble)) & 3;
	float ditherValue = kBayer4x4[pix.y * 4 + pix.x];
	clip(fade - ditherValue);

	// ---- フレネルでリムを強調 ----
	float3 V = normalize(g_camPos - In.worldPos);
	float3 N = normalize(In.normal);
	float fres = pow(1.0 - saturate(dot(N, V)), 2.0);
	float intensity = 0.25 + fres * 1.5;

	// ---- 走査線 (ワールドY基準で上へ流れる) ----
	float scan = 0.85 + 0.15 * sin(In.worldPos.y * 40.0 - g_time * 6.0);

	float3 finalColor = lerp(g_color.rgb, g_rimColor.rgb, fres);
	finalColor = lerp(finalColor, float3(1, 1, 1), g_rimColor.a * 0.5);
	
	return float4(finalColor * scan, intensity);
}
