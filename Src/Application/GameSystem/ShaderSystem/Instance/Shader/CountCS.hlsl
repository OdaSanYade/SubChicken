#include "inc_Instance.hlsli"

StructuredBuffer<InstanceData> g_all : register(t0);
RWStructuredBuffer<uint> g_count : register(u0);
cbuffer cbCull : register(b0)
{
	float4 g_planes[6];
	int g_instanceCount;
	int g_typeCount;
	int g_drawTypeCount;
	int _pad;
	float3 g_camPos;
	float g_maxDist;
};
[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	uint i = id.x;
	if (i >= (uint) g_instanceCount)
		return;
	InstanceData inst = g_all[i];
	float4 s = inst.boundingSphere;
    [unroll]
	for (int p = 0; p < 6; ++p)
	{
		float d = dot(g_planes[p].xyz, s.xyz) + g_planes[p].w;
		if (d < -s.w)
			return;
	}

	if (g_maxDist > 0.0)
	{
		float dist = length(s.xyz - g_camPos);
		if (dist - s.w > g_maxDist)
			return;
	}

	uint dummy;
	InterlockedAdd(g_count[inst.meshID], 1, dummy);
}
