#include "inc_KdCreditShader.hlsli"
#include "../HudShader/inc_KdHudSdf.hlsli"

//===================================================
//
// クレジット画面 背景 ピクセルシェーダ
//
// 夜明け前の農場の空。
// タイトルの3D画面の上に重ねるので、完全には塗り潰さない。
// 奥にうっすら農場が残ると、「同じ世界の中で幕が下りた」と読める。
//
//===================================================

// 星の層。
// 画面をマス目に切り、マスごとに1つ星を置くかどうかを乱数で決める。
//  _px      : 基準解像度のピクセル座標
//  _cell    : マスの大きさ(px)
//  _density : 星を置くマスの割合(0〜1)
//  _seed    : 層ごとにずらす乱数の種
float StarLayer(float2 _px, float _cell, float _density, float _seed)
{
	const float2 g = _px / _cell;
	const float2 id = floor(g);
	const float2 f = frac(g);

	const float h = Hash21(id + _seed);
	if (h > _density)
		return 0.0f;

	// マスの端に寄りすぎると、光が隣のマスで切れる。中ほどに置く。
	const float2 pos = float2(Hash21(id + _seed + 17.3f),
							  Hash21(id + _seed + 41.7f)) * 0.7f + 0.15f;

	const float r = length((f - pos) * _cell);
	const float size = lerp(0.7f, 1.8f, Hash21(id + _seed + 5.1f));

	// またたき。星ごとに速さと位相をずらす。揃うと点滅に見える。
	const float k = h / max(_density, 1e-3f);
	const float tw = 0.55f + 0.45f * sin(g_time * (0.8f + k * 2.6f) + k * 61.0f);

	// 芯と滲み。芯だけだと解像度が低い時に消える。
	const float core = exp(-(r * r) / (size * size));
	const float halo = exp(-r / (size * 4.0f)) * 0.15f;

	return (core + halo) * tw;
}

float4 main(VSOutput In) : SV_Target0
{
	const float2 uv = In.UV;

	// 基準解像度の座標。解像度が変わっても星の密度と大きさが変わらない。
	const float2 bp = In.Pos.xy / max(g_uiScale, 1e-3f);

	//------------------------------------------
	// 空のグラデーション
	//------------------------------------------
	// 上は深い藍、下は黒に近い茶。地面の側を暖色にすると夜の農場に見える。
	const float3 kTop = float3(0.035f, 0.055f, 0.130f);
	const float3 kMid = float3(0.050f, 0.050f, 0.100f);
	const float3 kBottom = float3(0.030f, 0.020f, 0.020f);

	float3 col = lerp(kTop, kMid, smoothstep(0.0f, 0.55f, uv.y));
	col = lerp(col, kBottom, smoothstep(0.50f, 1.00f, uv.y));

	//------------------------------------------
	// 地平の灯り
	//------------------------------------------
	// 画面の底だけ、ランプの火のような橙をにじませる。
	// 夜明け(タイトルロゴの朝日)へ向かう途中の時間帯に見せる。
	{
		const float h = (1.0f - uv.y) * 4.5f;
		const float horizon = exp(-h * h);
		col += float3(0.20f, 0.11f, 0.04f) * horizon * 0.35f;
	}

	//------------------------------------------
	// 天の川の帯
	//------------------------------------------
	// 右下がりの帯をゆっくり揺らす。止まった空は書き割りに見える。
	{
		const float center = 0.32f - (uv.x - 0.5f) * 0.18f
						   + sin(uv.x * 2.3f + g_time * 0.05f) * 0.03f;
		const float axis = uv.y - center;

		const float n = Noise21(bp * 0.004f + float2(g_time * 0.010f, 0.0f)) * 0.6f
					  + Noise21(bp * 0.013f - float2(0.0f, g_time * 0.008f)) * 0.4f;

		const float band = exp(-axis * axis * 55.0f) * (0.35f + n * 0.65f);
		col += float3(0.10f, 0.12f, 0.20f) * band * 0.55f;
	}

	//------------------------------------------
	// 星
	//------------------------------------------
	// 本文のスクロールに少しだけ付いてくる。層ごとに追従量を変えて奥行きを出す。
	float star = 0.0f;
	star += StarLayer(bp + float2(0.0f, g_scroll * 0.06f + g_time * 1.5f), 46.0f, 0.18f, 0.0f);
	star += StarLayer(bp + float2(0.0f, g_scroll * 0.15f + g_time * 3.0f), 90.0f, 0.35f, 11.0f) * 1.3f;

	// 下ほど減らす。地平の近くは灯りに負けて見えない。
	star *= smoothstep(1.0f, 0.55f, uv.y);
	col += float3(1.00f, 0.95f, 0.85f) * star;

	//------------------------------------------
	// 周縁を沈める
	//------------------------------------------
	// 本文は中央を流れるので、端の情報を落として視線を中へ寄せる。
	{
		const float aspect = g_screenSize.x / max(g_screenSize.y, 1.0f);
		const float2 q = (uv - 0.5f) * float2(aspect, 1.0f);
		const float vign = smoothstep(0.35f, 1.05f, length(q));
		col *= 1.0f - vign * 0.55f;
	}

	//------------------------------------------
	// 登場
	//------------------------------------------
	// 上から幕が下りる。上端が少しだけ先に濃くなる。
	const float t = saturate(g_appear * 1.25f - uv.y * 0.25f);

	// 0.96 : 奥の農場をわずかに残す
	return float4(col, smoothstep(0.0f, 1.0f, t) * 0.96f);
}
