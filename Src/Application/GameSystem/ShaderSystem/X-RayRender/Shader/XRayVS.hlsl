#include "inc_XRay.hlsli"

struct BoneMatrix
{
	row_major float4x4 m;
};
StructuredBuffer<BoneMatrix> g_bones : register(t1);

struct VS_IN
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	uint4 skinIndex : SKININDEX;
	float4 skinWeight : SKINWEIGHT;
};

VS_OUT main(VS_IN In)
{
	float3 localPos = In.pos;
	float3 localNormal = In.normal;

	if (g_boneOffset >= 0)
	{
		float wsum = In.skinWeight.x + In.skinWeight.y
                   + In.skinWeight.z + In.skinWeight.w;
		if (wsum > 0.001f)
		{
			uint bo = (uint) g_boneOffset;
			float4x4 skin =
                  g_bones[bo + In.skinIndex.x].m * In.skinWeight.x
                + g_bones[bo + In.skinIndex.y].m * In.skinWeight.y
                + g_bones[bo + In.skinIndex.z].m * In.skinWeight.z
                + g_bones[bo + In.skinIndex.w].m * In.skinWeight.w;
			localPos = mul(float4(localPos, 1.0f), skin).xyz;
			localNormal = mul(localNormal, (float3x3) skin);
		}
	}

	VS_OUT o;
	o.pos = mul(float4(localPos, 1.0), g_wvp);
	float3 wp = mul(float4(localPos, 1.0), g_world).xyz;
	o.worldPos = wp;
	o.normal = normalize(mul(localNormal, (float3x3) g_world));
	o.dist = length(g_camPos - wp);
	o.clip = o.pos;
	return o;
}
