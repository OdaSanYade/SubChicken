#include "inc_KdCreditShader.hlsli"

//===================================================
//
// クレジット本文 ピクセルシェーダ(SDF)
//
// アトラスは距離場(R8)。0.5 がちょうど輪郭、内側ほど大きい。
// 同じ1枚から「塗り・縁取り・影・光」をしきい値を変えて取り出す。
//
// 重ね順(下から)
//   光   : 見出しと表題だけ。縁取りのさらに外へ滲む
//   影   : 右下へずらした縁取りの形
//   縁取り : 輪郭の外へ1ドット。星や天の川の上でも字が沈まない
//   塗り
//
//===================================================

Texture2D g_atlas : register(t0);
SamplerState g_samp : register(s0);

// 縁取りと影の色。焦げ茶寄りの黒にすると、真っ黒より背景の夜空に馴染む。
static const float3 kEdgeColor = float3(0.06f, 0.04f, 0.03f);

// 上に重ねる(ストレートアルファ)
float4 Over(float4 _top, float4 _bottom)
{
	const float a = _top.a + _bottom.a * (1.0f - _top.a);
	const float3 rgb = (_top.rgb * _top.a + _bottom.rgb * _bottom.a * (1.0f - _top.a)) / max(a, 1e-5f);
	return float4(rgb, a);
}

float4 main(TextVSOutput In) : SV_Target0
{
	const float sd = g_atlas.Sample(g_samp, In.UV).r;

	// 画面上での距離場の変化量。拡大しても縮小しても、縁の滲みが約1画素に保たれる。
	// 0.75 倍で少し締める。ドット字なので甘いと角がぼやける。
	const float w = max(fwidth(sd), 1e-4f) * 0.75f;

	const float edgeT = 0.5f - In.Params.x;	// 縁取りの外側のしきい値

	const float fill = smoothstep(0.5f - w, 0.5f + w, sd);
	const float edge = smoothstep(edgeT - w, edgeT + w, sd);

	//------------------------------------------
	// 影
	//------------------------------------------
	// 字の範囲の外を拾わないように留める。隣の字が影に混ざるのを防ぐ。
	float shadow;
	{
		const float2 suv = clamp(In.UV - g_shadowUV, In.UVRect.xy, In.UVRect.zw);
		const float sds = g_atlas.Sample(g_samp, suv).r;
		shadow = smoothstep(edgeT - w, edgeT + w, sds);
	}

	//------------------------------------------
	// 光
	//------------------------------------------
	// 縁取りの外から距離場の端まで。ゆっくり脈打たせる。
	// 字ごとに位相をずらすと、光が字の上を渡っていくように見える。
	float glow = 0.0f;
	if (In.Params.y > 0.001f)
	{
		const float pulse = 0.75f + 0.25f * sin(g_time * 1.6f - In.Pos.x * 0.004f);
		glow = smoothstep(0.0f, edgeT, sd) * In.Params.y * pulse * 0.55f;
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	float4 col = float4(In.Color.rgb, glow);
	col = Over(float4(kEdgeColor, shadow * 0.60f), col);
	col = Over(float4(kEdgeColor, edge), col);
	col = Over(float4(In.Color.rgb, fill), col);

	//------------------------------------------
	// 画面の上下で薄れる
	//------------------------------------------
	// 端で急に切れると、流れているのではなく「切り抜かれている」ように見える。
	{
		const float y = In.Pos.y / max(g_screenSize.y, 1.0f);
		const float f = max(g_fade, 1e-3f);
		col.a *= smoothstep(0.0f, f, y) * smoothstep(1.0f, 1.0f - f, y);
	}

	col.a *= In.Color.a * smoothstep(0.0f, 1.0f, g_appear);

	if (col.a < 0.004f)
		discard;

	return col;
}
