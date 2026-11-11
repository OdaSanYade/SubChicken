struct VSOutput
{
	float4 Pos : SV_POSITION;
	float2 UV : TEXCOORD0;
	float Speed : TEXCOORD1; 
	float Squash : TEXCOORD2; 
};

cbuffer cbPhase : register(b0)
{
	float g_time;
	float g_progress; 
	float g_phase; 
	float g_pad;

	float2 g_screenSize;
	float2 g_halfSize;

	float2 g_center;
	float2 g_uvRange; 
};
