#pragma once

// 共有フルスクリーンVS
static const char* kVSCode = R"SH(
struct VS_OUT
{
	float4 pos:SV_Position;
	float2 uv:TEXCOORD0;
};
VS_OUT main(uint id:SV_VertexID)
{
	VS_OUT o;
	float2 uv=float2((id<<1)&2,id&2);
	o.uv=uv;
	o.pos=float4(uv*float2(2,-2)+float2(-1,1),0,1);
	return o;
}
)SH";

static const char* kPSCode = R"SH(
TextureCube g_cubemap : register(t0);
SamplerState g_sampler : register(s0);

cbuffer cbRaymarchCamera : register(b3)
{
    float3 g_camPos;
    float  _camPad;
    row_major float4x4 g_invViewProj;
	row_major float4x4 g_viewProj; 
};

struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float4 main(PS_IN In) : SV_Target0
{
    // UVから、クリップ空間の座標を作る
    float2 ndc = In.uv * 2.0 - 1.0;
    ndc.y = -ndc.y;   // Y反転（UVは上が0、NDCは上が+1）

    // 遠クリップ面の点を、ワールド空間に逆変換
    float4 farPos = mul(float4(ndc, 1.0, 1.0), g_invViewProj);
    farPos /= farPos.w;

    // 視線方向 = カメラから、その点へ
    float3 dir = normalize(farPos.xyz - g_camPos);

    // その方向で、キューブマップをサンプル
    float3 col = g_cubemap.Sample(g_sampler, dir).rgb;
    return float4(col, 1.0);
}
)SH";

static const char* kIrradiancePS = R"SH(
TextureCube g_envMap : register(t0);
SamplerState g_sampler : register(s0);
cbuffer cbFace : register(b0)
{
    row_major float4x4 g_faceMat;
}
struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PS_IN In) : SV_Target0
{
    float2 ndc = In.uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 localDir = normalize(float3(ndc, 1.0));
    float3 N = normalize(mul(localDir, (float3x3)g_faceMat));
    float3 up = abs(N.y) < 0.999 ? float3(0,1,0) : float3(1,0,0);
    float3 right = normalize(cross(up, N));
    up = cross(N, right);
    float3 irradiance = float3(0,0,0);
    float nrSamples = 0.0;
    float step = 0.025;
    for (float phi = 0.0; phi < 6.28318; phi += step)
    {
        for (float theta = 0.0; theta < 1.5708; theta += step)
        {
            float3 tangent = float3(sin(theta)*cos(phi), sin(theta)*sin(phi), cos(theta));
            float3 sampleVec = tangent.x * right + tangent.y * up + tangent.z * N;
            irradiance += g_envMap.Sample(g_sampler, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    irradiance = 3.14159265 * irradiance / nrSamples;
    return float4(irradiance, 1.0);
}
)SH";

static const char* kPrefilterPS = R"SH(
TextureCube g_envMap : register(t0);
SamplerState g_sampler : register(s0);
cbuffer cbPrefilter : register(b0)
{
    row_major float4x4 g_faceMat;
    float g_roughness;
    float3 _pad;
}
struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint N) { return float2(float(i)/float(N), RadicalInverse_VdC(i)); }

float3 ImportanceGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159265 * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
    float3 H = float3(cos(phi)*sinTheta, sin(phi)*sinTheta, cosTheta);
    float3 up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

float4 main(PS_IN In) : SV_Target0
{
    float2 ndc = In.uv * 2.0 - 1.0;
    ndc.y = -ndc.y;
    float3 N = normalize(mul(normalize(float3(ndc, 1.0)), (float3x3)g_faceMat));
    float3 V = N;

    const uint SAMPLE_COUNT = 256u;
    float3 prefiltered = float3(0,0,0);
    float totalWeight = 0.0;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceGGX(Xi, N, g_roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            prefiltered += g_envMap.Sample(g_sampler, L).rgb * NdotL;
            totalWeight += NdotL;
        }
    }
    prefiltered = prefiltered / max(totalWeight, 0.001);
    return float4(prefiltered, 1.0);
}
)SH";

static const char* kBRDFLUTPS = R"SH(
struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}
float2 Hammersley(uint i, uint N) { return float2(float(i)/float(N), RadicalInverse_VdC(i)); }

float3 ImportanceGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * 3.14159265 * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);
    float3 H = float3(cos(phi)*sinTheta, sin(phi)*sinTheta, cosTheta);
    float3 up = abs(N.z) < 0.999 ? float3(0,0,1) : float3(1,0,0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);
    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}
float G_SchlickGGX_IBL(float NdotV, float roughness)
{
    float k = (roughness * roughness) / 2.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}
float G_Smith_IBL(float NdotV, float NdotL, float roughness)
{
    return G_SchlickGGX_IBL(NdotV, roughness) * G_SchlickGGX_IBL(NdotL, roughness);
}

float2 main(PS_IN In) : SV_Target0
{
    float NdotV = In.uv.x;
    float roughness = In.uv.y;
    NdotV = max(NdotV, 0.001);

    float3 V = float3(sqrt(1.0 - NdotV*NdotV), 0.0, NdotV);
    float3 N = float3(0, 0, 1);

    float A = 0.0;
    float B = 0.0;
    const uint SAMPLE_COUNT = 1024u;
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceGGX(Xi, N, roughness);
        float3 L = normalize(2.0 * dot(V, H) * H - V);
        float NdotL = max(L.z, 0.0);
        float NdotH = max(H.z, 0.0);
        float VdotH = max(dot(V, H), 0.0);
        if (NdotL > 0.0)
        {
            float G = G_Smith_IBL(NdotV, NdotL, roughness);
            float G_Vis = (G * VdotH) / (NdotH * NdotV);
            float Fc = pow(1.0 - VdotH, 5.0);
            A += (1.0 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / float(SAMPLE_COUNT);
}
)SH";

static const char* kShow2DPS = R"SH(
Texture2D g_tex : register(t0);
SamplerState g_sampler : register(s0);
struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PS_IN In) : SV_Target0
{
    float d = g_tex.Sample(g_sampler, In.uv).r;
	// 深度は 0.9〜1.0 に固まりがちなので、その範囲を拡大
	float enhanced = (d - 0.9) * 10.0;   // 0.9〜1.0 を 0〜1 に
	
	return float4(enhanced, enhanced, enhanced, 1.0);
}
)SH";

static const char* kShadowVS = R"SH(
cbuffer cbShadow : register(b0)
{
    row_major float4x4 g_lightWVP;
};
struct VS_IN  { float3 pos : POSITION; float2 uv : TEXCOORD0; float3 normal : NORMAL; };
struct VS_OUT { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

VS_OUT main(VS_IN In)
{
    VS_OUT o;
    o.pos = mul(float4(In.pos, 1.0), g_lightWVP);
    o.uv  = In.uv;
    return o;
}
)SH";

static const char* kShadowAlphaPS = R"SH(
Texture2D    g_baseColor : register(t0);
SamplerState g_sampler   : register(s0);

cbuffer cbShadowClip : register(b1)
{
    float  g_alphaThreshold;
    float3 _clipPad;
};

struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };

void main(PS_IN In)
{
    float a = g_baseColor.Sample(g_sampler, In.uv).a;
    clip(a - g_alphaThreshold);
}
)SH";