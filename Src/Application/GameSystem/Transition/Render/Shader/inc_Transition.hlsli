cbuffer cbTransition : register(b0)
{
	float g_time; 
	float g_progress;
	float g_switchPoint;
	float g_padding;
};

struct VSOutput
{
	float4 pos : SV_POSITION;
	float2 uv : TEXCOORD0;
};
