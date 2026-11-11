RWStructuredBuffer<uint> g_count : register(u0);
RWStructuredBuffer<uint> g_cursor : register(u1);
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
	if (id.x >= (uint) g_typeCount)
		return;
	g_count[id.x] = 0;
	g_cursor[id.x] = 0;
}
