struct DrawTypeInfo
{
	uint meshID;
	uint indexCount;
	uint startIndex;
	uint _pad;
};
StructuredBuffer<uint> g_count : register(t0);
StructuredBuffer<DrawTypeInfo> g_drawTypes : register(t1);
RWStructuredBuffer<uint> g_offset : register(u0);
RWByteAddressBuffer g_args : register(u1);
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
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    // 種別ごとの開始位置（累積和）
	uint sum = 0;
	for (int t = 0; t < g_typeCount; ++t)
	{
		g_offset[t] = sum;
		sum += g_count[t];
	}
    // drawType ごとの Indirect 引数（5 uint = 20バイト）
	for (int d = 0; d < g_drawTypeCount; ++d)
	{
		DrawTypeInfo dt = g_drawTypes[d];
		uint base = d * 20;
		g_args.Store(base + 0, dt.indexCount); // IndexCountPerInstance
		g_args.Store(base + 4, g_count[dt.meshID]); // InstanceCount（GPUが決める）
		g_args.Store(base + 8, dt.startIndex); // StartIndexLocation
		g_args.Store(base + 12, 0); // BaseVertexLocation
		g_args.Store(base + 16, 0); // StartInstanceLocation（VS側でoffset加算）
	}
}
