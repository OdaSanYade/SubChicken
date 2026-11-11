#include "inc_Transition.hlsli"

VSOutput VSMain(uint id : SV_VertexID)
{
	VSOutput Out;
 
	float2 uv = float2((id << 1) & 2, id & 2); // (0,0) (2,0) (0,2)
 
	Out.uv = uv;
	Out.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
 
	return Out;
}
