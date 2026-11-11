#include "inc_KdRankShader.hlsli"

// トライアングルストリップ用の並び。
// 板1枚なので頂点バッファは持たず、IDから組む。
static const float2 kCorner[4] =
{
	float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)
};

// 行き過ぎてから戻る。飛び出しの表現に使う。
float EaseOutBack(float t)
{
	const float s = 2.10158f;
	const float u = saturate(t) - 1.0f;
	return 1.0f + u * u * ((s + 1.0f) * u + s);
}

VSOutput main(uint vid : SV_VertexID)
{
	const float2 c = kCorner[vid];

	const int rank = (int) g_rank;

	//------------------------------------------
	// 飛び出し
	//------------------------------------------
	// 殻が割れてから出てくる。
	// 割れる前に動き出すと、中身が先に見えてしまう。
	const float pop = saturate((g_hatch - RANK_BREAK_AT)
							 / (RANK_BREAK_END - RANK_BREAK_AT));

	// 上へ跳ねて落ちる。飛び出した勢いを残す。
	float2 offset = float2(0.0f, -EaseOutBack(pop) * g_halfSize.y * 0.35f
							   + pop * pop * g_halfSize.y * 0.35f);

	float2 scale = float2(EaseOutBack(pop), EaseOutBack(pop));
	float rot = (1.0f - pop) * 0.6f;

	//------------------------------------------
	// 定着後の常時の動き
	//------------------------------------------
	// ランクごとに変える。同じ動きだと色違いにしか見えない。
	const float idle = saturate((g_hatch - RANK_BREAK_END) / 0.28f);

	if (idle > 0.0f)
	{
		const float t = g_time;

		if (rank == RANK_S)
		{
			// 大きく脈打ち、ゆっくり傾く。最上位なので最も派手に。
			scale *= 1.0f + sin(t * 2.3f) * 0.055f * idle;
			rot += sin(t * 0.9f) * 0.05f * idle;
			offset.y -= (sin(t * 1.7f) * 0.5f + 0.5f) * g_halfSize.y * 0.05f * idle;
		}
		else if (rank == RANK_A)
		{
			// 弾む。着地のたびに横へ潰れる。
			const float ph = frac(t * 0.55f);
			const float hop = abs(sin(ph * RANK_PI));

			offset.y -= hop * g_halfSize.y * 0.09f * idle;

			const float squash = (1.0f - hop) * 0.05f * idle;
			scale *= float2(1.0f + squash, 1.0f - squash);
		}
		else if (rank == RANK_B)
		{
			// 左右に揺れる。落ち着いた動き。
			rot += sin(t * 1.4f) * 0.055f * idle;
			offset.x += sin(t * 1.4f) * g_halfSize.x * 0.02f * idle;
		}
		else if (rank == RANK_C)
		{
			// ゆっくり呼吸するだけ。
			scale *= 1.0f + sin(t * 1.0f) * 0.022f * idle;
		}
		else
		{
			// D は重い。少しうなだれて、たまに揺れる。
			rot += 0.10f * idle + sin(t * 0.7f) * 0.02f * idle;
			offset.y += g_halfSize.y * 0.03f * idle;
		}
	}

	//------------------------------------------
	// 頂点
	//------------------------------------------
	float2 local = c * g_halfSize * scale;

	// 回転は中心を軸に
	{
		const float s = sin(rot);
		const float co = cos(rot);
		local = float2(local.x * co - local.y * s, local.x * s + local.y * co);
	}

	const float2 screen = g_center + offset + local;

	VSOutput Out;

	// 画面ピクセル → NDC。Yは下向きなので反転する。
	Out.Pos = float4(
		screen.x / g_screenSize.x * 2.0f - 1.0f,
		1.0f - screen.y / g_screenSize.y * 2.0f,
		0.0f, 1.0f);

	Out.UV = c * 0.5f + 0.5f;
	Out.Local = c;

	return Out;
}
