#include "inc_KdCreditShader.hlsli"

//===================================================
//
// クレジット画面 頂点シェーダ(全画面)
//
// 画面を覆う大きな三角形を1枚出す。
// 四角形2枚だと対角線上のピクセルが2回塗られるので、三角形1枚にする。
// 頂点バッファは持たず、IDから組む。
//
//===================================================

VSOutput main(uint vid : SV_VertexID)
{
	// vid : 0 → (0,0)  1 → (2,0)  2 → (0,2)
	const float2 uv = float2((float) ((vid << 1) & 2), (float) (vid & 2));

	VSOutput Out;

	// UV(下向き) → NDC(上向き)
	Out.Pos = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, 0.0f, 1.0f);
	Out.UV = uv;

	return Out;
}
