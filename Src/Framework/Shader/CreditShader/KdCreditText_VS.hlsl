#include "inc_KdCreditShader.hlsli"

//===================================================
//
// クレジット本文 頂点シェーダ
//
// 文字の並びは組版の時に一度だけ作って、GPUに置いたままにする。
// 毎フレーム変わるのはスクロール量だけなので、定数バッファで渡して
// ここで画面の位置を出す。
//
//===================================================

StructuredBuffer<CreditTextInst> g_lines : register(t1);

// トライアングルストリップ用の並び(左上原点)
static const float2 kCorner[4] =
{
	float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)
};

TextVSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	const CreditTextInst inst = g_lines[g_instBase + iid];
	const float2 c = kCorner[vid];

	// 左上を画素に揃える。
	// ドット字を16の倍数の大きさで出すと、1ドットがちょうど整数の画素になる。
	// 板の左上を画素に合わせれば、ドットの境目も画素の境目に揃う。
	float2 tl = g_origin + (inst.Rect.xy - float2(0.0f, g_scroll)) * g_uiScale;
	tl = floor(tl + 0.5f);

	const float2 px = tl + c * inst.Rect.zw * g_uiScale;

	TextVSOutput Out;

	// 画面ピクセル → NDC。Yは下向きなので反転する。
	Out.Pos = float4(px.x / g_screenSize.x * 2.0f - 1.0f,
					 1.0f - px.y / g_screenSize.y * 2.0f,
					 0.0f, 1.0f);

	Out.UV = lerp(inst.UV.xy, inst.UV.zw, c);
	Out.UVRect = inst.UV;
	Out.Color = inst.Color;
	Out.Params = inst.Params;

	return Out;
}
