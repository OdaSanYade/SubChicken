#include "inc_KdRankShader.hlsli"
#include "../../HudShader/inc_KdHudSdf.hlsli"

//===================================================
//
// ランクの絵 ピクセルシェーダ
//
//===================================================

//===================================================
// テクスチャの中身を動かす
//===================================================
float2 AnimateRankUV(float2 _uv, int _rank, float _t, float _idle)
{
	float2 uv = _uv;

	if (_idle <= 0.0f)
		return uv;

	if (_rank == RANK_S)
	{
		// 炎に炙られて揺らぐ。上ほど強く。
		const float w = smoothstep(1.0f, -0.6f, uv.y);

		uv.x += sin(uv.y * 9.0f + _t * 3.4f) * 0.022f * w * _idle;
		uv.y += sin(uv.x * 7.0f + _t * 2.6f) * 0.010f * w * _idle;
	}
	else if (_rank == RANK_A)
	{
		// 弾む勢いで縦に伸び縮みする。
		const float squash = sin(_t * 3.45f) * 0.045f * _idle;

		uv.y /= (1.0f + squash);
		uv.x /= (1.0f - squash * 0.6f);
	}
	else if (_rank == RANK_B)
	{
		// 水面のようにゆっくり波打つ。
		uv.x += sin(uv.y * 4.0f + _t * 1.5f) * 0.014f * _idle;
	}
	else if (_rank == RANK_C)
	{
		// ごく浅い呼吸。動いているかどうかの境目に留める。
		uv /= (1.0f + sin(_t * 1.1f) * 0.012f * _idle);
	}
	else
	{
		// 重さでわずかに縦へ潰れる。動きも遅い。
		uv.y /= (1.0f - 0.030f * _idle);
		uv.x += sin(uv.y * 3.0f + _t * 0.8f) * 0.008f * _idle;
	}

	return uv;
}

float4 main(VSOutput In) : SV_Target0
{
	const int rank = (int) g_rank;

	// 殻が割れてから出てくる。割れる前は完全に隠す。
	const float pop = saturate((g_hatch - RANK_BREAK_AT)
							 / (RANK_BREAK_END - RANK_BREAK_AT));

	if (pop <= 0.001f)
		discard;

	const float idle = saturate((g_hatch - RANK_BREAK_END) / 0.28f);

	//------------------------------------------
	// 絵の位置
	//------------------------------------------
	// 板は効果のぶん広く取ってあるので、絵は中央の一部に描く。
	// 板いっぱいに貼ると、動かした瞬間に縁で切れる。
	float2 uv = In.Local / RANK_TEX_FIT;

	// テクスチャそのものを動かす。
	// 板ごと動かすだけだと「絵が移動している」だけに見えるので、
	// 絵の中身も歪ませて生き物らしくする。
	uv = AnimateRankUV(uv, rank, g_time, idle);

	// 範囲外は描かない
	if (abs(uv.x) > 1.0f || abs(uv.y) > 1.0f)
		discard;

	float4 col = g_rankTex.Sample(g_samp, uv * 0.5f + 0.5f);

	// 中心からの距離。周りの効果に使う。
	// 板を広く取ってあるので、絵の外側にも余白がある。
	const float dist = length(In.Local) / RANK_TEX_FIT;

	//------------------------------------------
	// 出た瞬間の白飛び
	//------------------------------------------
	// 殻を割って出てきたので、一瞬だけ光を纏う。
	// これが無いと、絵がただ現れただけに見える。
	{
		const float k = 1.0f - pop;
		col.rgb = lerp(col.rgb, float3(1, 1, 1), k * k * 0.85f);
	}

	//------------------------------------------
	// ランクごとの効果
	//------------------------------------------
	float glow = 0.0f;
	float3 glowCol = float3(1, 1, 1);

	if (rank == RANK_S)
	{
		glowCol = float3(1.00f, 0.72f, 0.24f);

		// 立ち上る炎。下から上へノイズを流す。
		{
			const float2 q = (In.Local / RANK_TEX_FIT) * float2(1.6f, 1.0f);
			const float n = Noise21(q * 2.4f + float2(0.0f, -g_time * 1.6f));

			// 下ほど強く、上へ行くほど細る
			const float rise = smoothstep(1.0f, -0.4f, In.Local.y / RANK_TEX_FIT);
			glow = saturate(n * 1.6f - 0.7f) * rise * 0.9f;
		}

		// 外へ伸びる光条。ゆっくり回る。
		{
			float ang = atan2(In.Local.y, In.Local.x) + g_time * 0.25f;
			const float ray = pow(abs(sin(ang * 6.0f)), 6.0f);

			glow = max(glow, ray * smoothstep(0.55f, 1.15f, dist)
							   * smoothstep(1.45f, 1.0f, dist) * 0.8f);
		}
	}
	else if (rank == RANK_A)
	{
		glowCol = float3(1.00f, 0.84f, 0.42f);

		// 温かい光が周りを回る
		float ang = atan2(In.Local.y, In.Local.x) / (RANK_PI * 2.0f);
		ang = (ang < 0.0f) ? ang + 1.0f : ang;

		glow = exp(-frac(ang - frac(g_time * 0.3f)) * 5.0f)
			 * smoothstep(0.55f, 1.0f, dist)
			 * smoothstep(1.35f, 0.95f, dist) * 0.7f;
	}
	else if (rank == RANK_B)
	{
		glowCol = float3(0.52f, 0.78f, 1.00f);

		// 涼しい輪郭光。動かさず、静かに。
		glow = smoothstep(0.70f, 1.02f, dist)
			 * smoothstep(1.25f, 0.95f, dist) * 0.45f;
	}
	else if (rank == RANK_D)
	{
		// 色を沈める。上位と同じ鮮やかさだと、悪い結果に見えない。
		const float lum = dot(col.rgb, float3(0.299f, 0.587f, 0.114f));
		col.rgb = lerp(col.rgb, lum.xxx, 0.35f);
		col.rgb *= 0.85f;

		// 灰が落ちる。上から下へゆっくり。
		[unroll]
		for (int i = 0; i < 5; ++i)
		{
			const float seed = Hash11((float) i * 3.7f);
			const float life = frac(g_time * 0.14f + seed);

			const float2 o = float2((seed - 0.5f) * 1.7f
									+ sin(life * 4.0f + seed * 6.0f) * 0.12f,
									-1.2f + life * 2.6f);

			glow = max(glow, smoothstep(0.06f, 0.0f,
									   length(In.Local / RANK_TEX_FIT - o))
							* (1.0f - life) * 0.5f);
		}

		glowCol = float3(0.62f, 0.60f, 0.58f);
	}

	// C は何もしない。全部が光ると、光っていること自体が意味を失う。

	//------------------------------------------
	// 合成
	//------------------------------------------
	glow *= idle;

	col.rgb += glowCol * glow * col.a;

	// 絵の外にも光を出す。輪郭の外が暗いままだと、板に貼った絵に見える。
	const float outside = saturate(glow * (1.0f - col.a));

	col.rgb = lerp(col.rgb, glowCol, outside);
	col.a = max(col.a, outside * 0.55f);

	col *= g_tint;

	// 出現中は薄い
	col.a *= smoothstep(0.0f, 0.25f, pop);

	if (col.a < 0.004f)
		discard;

	return col;
}
