#include "inc_PhaseShader.hlsli"

Texture2D g_tex : register(t0);
SamplerState g_samp : register(s0);

// フェーズごとの差し色。画像の差し色(無し→青→橙)に合わせる。
static const float3 kAccent[3] =
{
	float3(1.00f, 0.92f, 0.70f),
	float3(0.45f, 0.80f, 1.00f),
	float3(1.00f, 0.62f, 0.15f),
};

float4 PSMain(VSOutput In) : SV_Target0
{
	const float t = 1.0f - g_progress;
	const int idx = clamp((int) g_phase, 0, 2);
	const float3 accent = kAccent[idx];

	//------------------------------------------
	// 残像
	//------------------------------------------
	// 飛んでいる間だけ横へ引き伸ばす。
	// 位置が動くだけでは速さが伝わらない。
	float4 col = 0;
	const int kTaps = 6;
	const float smear = In.Speed * 0.11f;

	for (int i = 0; i < kTaps; ++i)
	{
		float k = (float) i / (kTaps - 1);
		float4 s = g_tex.Sample(g_samp, In.UV - float2(smear * k, 0.0f));
		s *= (1.0f - k * 0.8f);
		col = max(col, s);
	}

	//------------------------------------------
	// 潰れた瞬間の閃光
	//------------------------------------------
	// ぶつかって止まった一拍を、形だけでなく光でも出す。
	col.rgb = lerp(col.rgb, float3(1, 1, 1), In.Squash * 0.75f);
	col.rgb += accent * In.Squash * 0.8f;

	//------------------------------------------
	// 走る線
	//------------------------------------------
	// 潰れた瞬間だけ、横に短い線が走る。衝撃が外へ抜ける絵。
	if (In.Squash > 0.01f)
	{
		float y = frac(In.UV.y * 14.0f);
		float animeLine = smoothstep(0.46f, 0.5f, y) * smoothstep(0.54f, 0.5f, y);
		col.rgb += accent * animeLine * In.Squash * 1.2f;
		col.a = max(col.a, animeLine * In.Squash * 0.5f);
	}

	//------------------------------------------
	// 抜けるときのフェード
	//------------------------------------------
	// 入りは移動で見せているので触らない。
	const float fadeOut = 1.0f - saturate((t - 0.88f) / 0.12f);
	col.a *= fadeOut;

	if (col.a < 0.004f)
		discard;

	return col;
}
