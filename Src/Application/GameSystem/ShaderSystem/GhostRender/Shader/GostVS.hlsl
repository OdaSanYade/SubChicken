struct BoneMatrix
{
	row_major float4x4 m;
};
StructuredBuffer<BoneMatrix> g_bones : register(t1);

cbuffer CB : register(b0)
{
	row_major float4x4 g_wvp;
	row_major float4x4 g_world;
	float4 g_color;
	float4 g_rimColor;
	float4 g_moveDirSpeed;
	float3 g_camPos;
	int g_boneOffset;
	float g_time;
	float3 _pad0;
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
struct VS_OUT
{
	float4 pos : SV_Position;
	float3 normal : NORMAL;
	float3 worldPos : POSITION1;
	float dist : DIST;
	float4 clip : CLIPPOS;
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

	float ageRatio = 1.0 - saturate(g_color.a);

    // ★寿命による収縮(溶けるような変形)
	const float kShrinkAmount = 0.08;
	localPos -= localNormal * (ageRatio * kShrinkAmount);

    // ★ヒット演出：法線方向に外側へ弾ける
	const float kBurstStrength = 0.35;
	localPos += localNormal * (g_rimColor.a * kBurstStrength);

    // ★進行方向への引き伸ばし
	if (g_moveDirSpeed.w > 0.0001)
	{
		float3x3 worldRot = (float3x3) g_world;
		float3x3 worldRotInv = transpose(worldRot); // 非一様スケール無し前提
		float3 localMoveDir = normalize(mul(g_moveDirSpeed.xyz, worldRotInv));
		const float kStretchStrength = 0.6;
		localPos += localMoveDir * dot(localPos, localMoveDir) * (g_moveDirSpeed.w * kStretchStrength);
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
