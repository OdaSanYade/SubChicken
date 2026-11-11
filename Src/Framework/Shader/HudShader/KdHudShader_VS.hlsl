#include "../UiShader/inc_KdHudCommon.hlsli"

//===================================================
//
// HUD系 共通 頂点シェーダ
//
// 戦闘HUD・UI共通部品・リザルトの3つで使い回す。
// 種別ごとの違いはピクセルシェーダ側にあり、頂点の組み方は同じ。
//
// ★このファイルを変えたら、参照している全ての .cpp を
//   再コンパイルすること(.shaderInc の依存はビルド系が見ていない)。
//
//===================================================

StructuredBuffer<HudInstance> g_instances : register(t0);

// トライアングルストリップ用の並び。
// 頂点バッファを持たずにIDから組むので、IASetVertexBuffersは不要。
static const float2 kCorner[4] =
{
	float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)
};

VSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
	HudInstance inst = g_instances[iid];

	const float2 local = kCorner[vid] * inst.HalfSize;

	// SDFは回転前の座標系で計算するため、Localには回転前の値を渡し、
	// 画面上の頂点位置だけを回転させる。
	float2 placed = local;
	if (abs(inst.Tilt) > 1e-4f)
	{
		const float s = sin(inst.Tilt);
		const float c = cos(inst.Tilt);
		placed = float2(local.x * c - local.y * s,
						local.x * s + local.y * c);
	}

	const float2 screen = inst.Center + placed;

	VSOutput Out;

	// 画面ピクセル → NDC。Yは下向きなので反転する。
	Out.Pos = float4(
		screen.x / g_screenSize.x * 2.0f - 1.0f,
		1.0f - screen.y / g_screenSize.y * 2.0f,
		0.0f, 1.0f);

	Out.Local = local;
	Out.HalfSize = inst.HalfSize;
	Out.Color = inst.Color;
	Out.SubColor = inst.SubColor;
	Out.Params = inst.Params;
	Out.Extra = inst.Extra;
	Out.Kind = inst.Kind;

	return Out;
}
