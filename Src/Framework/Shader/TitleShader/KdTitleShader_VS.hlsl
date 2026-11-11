#include "inc_KdTitleShader.hlsli"

// トライアングルストリップ用の並び。
// 板1枚なので頂点バッファは持たず、IDから組む。
static const float2 kCorner[4] =
{
	float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)
};

VSOutput main(uint vid : SV_VertexID)
{
	const float2 c = kCorner[vid];

	//------------------------------------------
	// 登場
	//------------------------------------------
	// 下から突き上がって、行き過ぎてから止まる。
	// 叩きつけた質感のロゴなので、動きも一度過剰に振る。
	float2 offset = float2(0.0f, 0.0f);
	float2 scale = float2(1.0f, 1.0f);

	{
		const float t = saturate(g_appear);

		// EaseOutBack
		const float s = 1.70158f;
		const float u = t - 1.0f;
		const float back = 1.0f + u * u * ((s + 1.0f) * u + s);

		offset.y = (1.0f - back) * g_halfSize.y * 1.30f;

		// 着地の瞬間だけ横に潰れる。踏ん張った形になる。
		const float squash = sin(saturate(t) * TITLE_PI) * 0.06f;
		scale = float2(1.0f + squash, 1.0f - squash);
	}

	//------------------------------------------
	// 呼吸
	//------------------------------------------
	// 既定は0。陽炎と重なると酔うので、使うときだけ入れる。
	scale *= 1.0f + sin(g_time * 0.5f) * 0.02f * g_breath;

	//------------------------------------------
	// 揺れ
	//------------------------------------------
	// 選択が動いた時などに一時的に震える。減衰は呼び出し側で持つ。
	offset.x += sin(g_time * 42.0f) * g_shake * g_halfSize.y * 0.05f;
	offset.y += cos(g_time * 37.0f) * g_shake * g_halfSize.y * 0.03f;

	const float2 screen = g_center + offset + c * g_halfSize * scale;

	VSOutput Out;

	// 画面ピクセル → NDC。Yは下向きなので反転する。
	Out.Pos = float4(
		screen.x / g_screenSize.x * 2.0f - 1.0f,
		1.0f - screen.y / g_screenSize.y * 2.0f,
		0.0f, 1.0f);

	Out.UV = c * 0.5f + 0.5f;

	return Out;
}
