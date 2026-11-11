#include "inc_KdTitleShader.hlsli"
#include "../HudShader/inc_KdHudSdf.hlsli"

//===================================================
//
// タイトルロゴ ピクセルシェーダ
//
//===================================================

float4 main(VSOutput In) : SV_Target0
{
	float2 uv = In.UV;

	//------------------------------------------
	// 陽炎
	//------------------------------------------
	{
		const float weight = pow(saturate(1.0f - uv.y), 1.6f);

		const float w = sin(uv.x * 21.0f + g_time * 1.3f) * 0.5f
					  + sin(uv.x * 37.0f - g_time * 0.9f) * 0.5f;

		uv.y += w * 0.0022f * weight * g_heat;
		uv.x += w * 0.0008f * weight * g_heat;
	}

	//------------------------------------------
	// 縁の揺らぎ
	//------------------------------------------
	float2 waveUv = uv;
	{
		const float n = Noise21(uv * 9.0f + float2(0.0f, g_time * 0.35f));
		const float a = (n - 0.5f) * 2.0f;

		waveUv += float2(a, -a) * 0.0018f * g_edgeWave;
	}

	//------------------------------------------
	// サンプル
	//------------------------------------------
	float4 col = g_logo.Sample(g_samp, uv);
	const float4 wav = g_logo.Sample(g_samp, waveUv);

	// 輪郭の近さ
	float edge;
	{
		const float2 d = float2(0.0035f, 0.0035f);

		const float ax = abs(g_logo.Sample(g_samp, uv + float2(d.x, 0)).a
						   - g_logo.Sample(g_samp, uv - float2(d.x, 0)).a);
		const float ay = abs(g_logo.Sample(g_samp, uv + float2(0, d.y)).a
						   - g_logo.Sample(g_samp, uv - float2(0, d.y)).a);

		edge = saturate((ax + ay) * 2.2f);
	}
	col = lerp(col, wav, edge * saturate(g_edgeWave));

	//------------------------------------------
	// 光の走り
	//------------------------------------------
	{
		const float ph = frac(g_time * 0.17f);

		// ロゴは右上がりなので、帯も同じ角度にする
		const float axis = uv.x + uv.y * 0.35f;
		const float x = lerp(-0.45f, 1.55f, ph);

		float band = exp(-abs(axis - x) * 9.0f);
		band *= smoothstep(1.0f, 0.72f, ph); // 通り過ぎたら消える
		band *= saturate(col.a * 1.4f) * (1.0f - edge * 0.7f);

		col.rgb += band * g_sheen * 0.55f;
	}

	//------------------------------------------
	// 明滅
	//------------------------------------------
	// 朝日の側だけをゆっくり明るくする。
	// 一定の明るさで止まっていると、絵として死んで見える。
	{
		const float weight = pow(saturate(1.0f - uv.y), 2.0f)
						   * saturate(uv.x * 1.4f - 0.15f);

		const float pulse = 0.5f + sin(g_time * 0.55f) * 0.5f;
		col.rgb += weight * pulse * 0.13f * col.a;
	}

	//------------------------------------------
	// かすれの呼吸
	//------------------------------------------
	{
		const float n = Noise21(uv * 11.0f);
		const float breathe = sin(g_time * 0.42f + n * 6.2f) * 0.5f + 0.5f;
		
		const float faded = saturate(1.0f - abs(col.a - 0.5f) * 2.0f);

		col.a *= 1.0f - faded * breathe * 0.22f;
	}

	//------------------------------------------
	// 縁の光
	//------------------------------------------
	col.rgb += edge * (0.10f + sin(g_time * 0.8f) * 0.04f) * col.a;

	//------------------------------------------
	// 砂粒
	//------------------------------------------
	float dust = 0.0f;
	if (g_dust > 0.001f)
	{
		[unroll]
		for (int i = 0; i < 6; ++i)
		{
			const float seed = Hash21(float2((float) i * 7.3f, 3.1f));

			// 上へ流れて、上端で消えて下から出直す
			const float life = frac(g_time * 0.16f + seed);

			const float2 o = float2(0.12f + seed * 0.80f,
									0.95f - life * 1.10f);

			const float2 q = (uv - o) * float2(1.0f, 1.0f);
			const float r = 0.004f + seed * 0.003f;

			// 粒はロゴの上にある時だけ見える。
			const float onLogo = g_logo.Sample(g_samp, o).a;

			dust = max(dust, smoothstep(r, 0.0f, length(q))
							* (1.0f - life) * onLogo);
		}
	}

	//------------------------------------------
	// 剥がれて散る(決定時)
	//------------------------------------------
	if (g_dissolve > 0.001f)
	{
		const float n = Noise21(uv * 14.0f);
		
		const float bias = (uv.x * 0.6f + uv.y * 0.4f);
		const float t = saturate(g_dissolve * 1.8f - bias * 0.6f);

		col.a *= smoothstep(t, t - 0.22f, n);

		// 崩れる縁だけ明るく飛ぶ
		col.rgb += saturate(1.0f - abs(n - t) * 9.0f) * g_dissolve * 0.6f;
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	col.rgb *= g_tint.rgb;
	col.a *= g_tint.a;

	col.rgb += dust * 0.9f;
	col.a = max(col.a, dust * g_dust * 0.75f);

	// 登場中は薄い。突き上がる動きは頂点側で作っている。
	col.a *= smoothstep(0.0f, 0.30f, g_appear);
	
	col.a *= saturate(1.0f - g_dissolve);

	if (col.a < 0.004f)
		discard;

	return col;
}
