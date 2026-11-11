#include "inc_Instance.hlsli"

struct BoneMatrix
{
	row_major float4x4 m;
};

StructuredBuffer<InstanceData> g_visible : register(t10);
StructuredBuffer<uint> g_offset : register(t11);
StructuredBuffer<BoneMatrix> g_bones : register(t12);

cbuffer cbShadow : register(b0)
{
	row_major float4x4 g_mLightViewProj;
};
cbuffer cbDraw : register(b7)
{
	uint g_meshID;
	uint3 _drawPad;
};

struct VS_OUT
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
};

struct VS_IN
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	uint4 skinIndex : SKININDEX;
	float4 skinWeight : SKINWEIGHT;
};

VS_OUT main(VS_IN In, uint instID : SV_InstanceID)
{
	uint idx = g_offset[g_meshID] + instID;
	InstanceData inst = g_visible[idx];

	float3 localPos = In.pos;
	if (inst.isSkinned != 0)
	{
		float wsum = In.skinWeight.x + In.skinWeight.y
                   + In.skinWeight.z + In.skinWeight.w;
		if (wsum > 0.001f)
		{
			uint bo = inst.boneOffset;
			float4x4 skin =
                  g_bones[bo + In.skinIndex.x].m * In.skinWeight.x
                + g_bones[bo + In.skinIndex.y].m * In.skinWeight.y
                + g_bones[bo + In.skinIndex.z].m * In.skinWeight.z
                + g_bones[bo + In.skinIndex.w].m * In.skinWeight.w;
			localPos = mul(float4(localPos, 1.0f), skin).xyz;
		}
	}
	float4 wp = mul(float4(localPos, 1.0f), inst.world);
	VS_OUT result;
	result.pos = mul(wp, g_mLightViewProj);
	result.uv = In.uv;
	return result;
}
