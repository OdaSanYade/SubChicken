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
struct VS_OUT
{
	float4 pos : SV_Position;
	float3 normal : NORMAL;
	float3 worldPos : POSITION1;
	float dist : DIST;
	float4 clip : CLIPPOS;
};
