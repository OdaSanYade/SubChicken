#include "inc_Instance.hlsli"

struct BoneMatrix
{
	row_major float4x4 m;
};

StructuredBuffer<InstanceData> g_visible : register(t10);
StructuredBuffer<uint> g_offset : register(t11);
StructuredBuffer<BoneMatrix> g_bones : register(t12);

cbuffer cbTransform : register(b1)
{
	row_major float4x4 g_mWVP;
	row_major float4x4 g_mWorldView;
	row_major float4x4 g_mWorld;
};
cbuffer cbDraw : register(b7)
{
	uint g_meshID;
	uint3 _drawPad;
};

struct VS_IN
{
	float3 pos : POSITION;
	float2 uv : TEXCOORD0;
	float3 normal : NORMAL;
	float3 tangent : TANGENT;
	uint4 skinIndex : SKININDEX;
	float4 skinWeight : SKINWEIGHT;
};
struct PS_IN
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
	float3 normal : NORMAL;
	float3 viewNormal : NORMAL1;
	float3 worldNormal : NORMAL2;
	float3 worldPos : POSITION1;
	float3 worldTangent : TANGENT;
	nointerpolation uint instID : INSTANCEID;
};

PS_IN main(VS_IN In, uint instID : SV_InstanceID)
{
	PS_IN Out;
	uint idx = g_offset[g_meshID] + instID;
	InstanceData inst = g_visible[idx];

	float3 localPos = In.pos;
	float3 localNormal = In.normal;
	float3 localTangent = In.tangent;

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
			localNormal = mul(localNormal, (float3x3) skin);
			localTangent = mul(localTangent, (float3x3) skin);
		}
	}

	float4 wp4 = mul(float4(localPos, 1.0f), inst.world);
	Out.pos = mul(wp4, g_mWVP);
	Out.uv = In.uv;
	Out.normal = localNormal;
	float3 wN = normalize(mul(localNormal, (float3x3) inst.world));
	Out.worldNormal = wN;
	Out.viewNormal = normalize(mul(wN, (float3x3) g_mWorldView));
	Out.worldPos = wp4.xyz;
	Out.worldTangent = normalize(mul(localTangent, (float3x3) inst.world));
	Out.instID = inst.paramOffset;
	return Out;
}
