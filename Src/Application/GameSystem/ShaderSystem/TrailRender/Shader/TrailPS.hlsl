Texture2D g_gbufferC : register(t0);
Texture2D g_tex0 : register(t1);
Texture2D g_tex1 : register(t2);
Texture2D g_tex2 : register(t3);
Texture2D g_tex3 : register(t4);
Texture2D g_tex4 : register(t5);
Texture2D g_tex5 : register(t6);
Texture2D g_tex6 : register(t7);
Texture2D g_tex7 : register(t8);
SamplerState g_sampler : register(s0);

cbuffer CB : register(b0)
{
	row_major float4x4 g_viewProj;
	float3 g_camPos;
	float g_time;
};

float Hash21(float2 p)
{
	p = frac(p * float2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return frac(p.x * p.y);
}
float2 Hash22(float2 p)
{
	return float2(Hash21(p), Hash21(p + 17.31));
}
float ValueNoise2D(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);
	float a = Hash21(i);
	float b = Hash21(i + float2(1, 0));
	float c = Hash21(i + float2(0, 1));
	float d = Hash21(i + float2(1, 1));
	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}
// 簡易ボロノイ（F2-F1）。0に近いほどセルの境目（ひび割れ/結晶/溶岩の輪郭に流用）
float VoronoiEdge(float2 p)
{
	float2 ip = floor(p);
	float2 fp = frac(p);
	float d1 = 8.0, d2 = 8.0;
	for (int y = -1; y <= 1; ++y)
	{
		for (int x = -1; x <= 1; ++x)
		{
			float2 g = float2((float) x, (float) y);
			float2 o = Hash22(ip + g);
			float2 r = g + o - fp;
			float d = dot(r, r);
			if (d < d1)
			{
				d2 = d1;
				d1 = d;
			}
			else if (d < d2)
			{
				d2 = d;
			}
		}
	}
	return sqrt(d2) - sqrt(d1);
}

struct VS_OUT
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
	float4 col : COLOR;
	float dist : DIST;
	float4 clip : CLIPPOS;
	nointerpolation float4 prm : TEXCOORD1;
	nointerpolation float4 prm2 : TEXCOORD2;
	nointerpolation float2 prm3 : TEXCOORD3;
	nointerpolation float4 prm4 : TEXCOORD4;
	nointerpolation float4 prm5 : TEXCOORD5;
	nointerpolation float4 prm6 : TEXCOORD6;
	nointerpolation float2 prm7 : TEXCOORD7;
};

float4 main(VS_OUT In) : SV_Target
{
	float4 col = In.col;

	float edgeSoft = max(In.prm.x, 0.01);
	float edgeCore = In.prm.y;
	int texIndex = (int) In.prm.z;

	float emissive = (In.prm.w <= 0.0) ? 1.0 : In.prm.w;

	float TRAIL_FADE_POW = In.prm2.x; // 大きいほど先端が早く消える
	float TRAIL_CORE_WIDTH = In.prm2.y; // 芯の幅 0..1（幅方向）
	float TRAIL_CORE_WHITE = In.prm2.z; // 芯の白熱化度 0..1
	float TRAIL_CORE_BOOST = In.prm2.w; // 芯のHDR倍率
	float TRAIL_ROOT_BOOST = In.prm3.x; // 根本の明るさ倍率
	float TRAIL_DEPTH_FADE = max(In.prm3.y, 0.01); // 深度遮蔽のフェード距離

	float uvNoiseStrength = In.prm4.x;
	float uvNoiseScale = In.prm4.y;
	float uvNoiseSpeed = In.prm4.z;
	float dissolveAmount = In.prm4.w;
	float dissolveNoiseScale = In.prm5.x;
	float dissolveEdgeWidth = max(In.prm5.y, 0.001);
	float edgeNoiseStrength = In.prm5.z;
	float edgeNoiseScale = In.prm5.w;
	float cellAmount = In.prm6.x;
	float cellScale = In.prm6.y;
	float cellEdgeWidth = max(In.prm6.z, 0.001);
	float cellGlow = In.prm6.w;
	float weaveStrength = In.prm7.x;
	float weaveScale = In.prm7.y;

    // ---- UV歪み（面白い機能8の簡易版：帯自体をUV歪みさせる）----
	float2 uv = In.uv;
	if (uvNoiseStrength > 0.0001)
	{
		float2 nOff = float2(
            ValueNoise2D(uv * uvNoiseScale + float2(g_time * uvNoiseSpeed, 0.0)) - 0.5,
            ValueNoise2D(uv * uvNoiseScale + float2(0.0, g_time * uvNoiseSpeed) + 91.7) - 0.5);
		uv += nOff * uvNoiseStrength;
	}

	float across = abs(uv.y * 2.0 - 1.0);
	float age = saturate(uv.x);

    // ---- 輪郭ノイズ（輪郭線だけをノイズで揺らす）----
	if (edgeNoiseStrength > 0.0001)
	{
		float en = ValueNoise2D(float2(uv.x * edgeNoiseScale, g_time * 0.7)) - 0.5;
		across = saturate(across + en * edgeNoiseStrength);
	}

	if (texIndex < 0)
	{
		col.a *= saturate(smoothstep(0.0, edgeSoft, 1.0 - across));
	}
	else
	{
		float4 t;
		if (texIndex == 1)
			t = g_tex1.Sample(g_sampler, uv);
		else if (texIndex == 2)
			t = g_tex2.Sample(g_sampler, uv);
		else if (texIndex == 3)
			t = g_tex3.Sample(g_sampler, uv);
		else if (texIndex == 4)
			t = g_tex4.Sample(g_sampler, uv);
		else if (texIndex == 5)
			t = g_tex5.Sample(g_sampler, uv);
		else if (texIndex == 6)
			t = g_tex6.Sample(g_sampler, uv);
		else if (texIndex == 7)
			t = g_tex7.Sample(g_sampler, uv);
		else
			t = g_tex0.Sample(g_sampler, uv);
		col *= t;
	}

	col.a *= pow(saturate(1.0 - age), TRAIL_FADE_POW);

	float coreMask = 1.0 - smoothstep(0.0, TRAIL_CORE_WIDTH, across);
	float core = saturate(coreMask * edgeCore);

	col.rgb = lerp(col.rgb, float3(1.0, 1.0, 1.0), core * TRAIL_CORE_WHITE);

	float boost = 1.0 + core * TRAIL_CORE_BOOST;
	boost *= lerp(TRAIL_ROOT_BOOST, 1.0, age);
	col.rgb *= boost * emissive;

    // ---- ディゾルブ（ノイズでジリジリと不規則にちぎれる）----
	if (dissolveAmount > 0.0001)
	{
		float dn = ValueNoise2D(uv * dissolveNoiseScale);
		float threshold = age * dissolveAmount;
		float mask = smoothstep(threshold - dissolveEdgeWidth, threshold + dissolveEdgeWidth, dn);
		col.a *= mask;
		float edgeGlow = 1.0 - smoothstep(threshold, threshold + dissolveEdgeWidth * 2.0, dn);
		col.rgb += edgeGlow * dissolveAmount * 0.6;
	}

    // ---- セル状パターン（ひび割れ/結晶/溶岩を共通のボロノイ境界で表現）----
	if (cellAmount > 0.0001)
	{
		float ve = VoronoiEdge(uv * cellScale);
		float edgeMask = 1.0 - smoothstep(0.0, cellEdgeWidth, ve);
		col.rgb += edgeMask * cellGlow * cellAmount;
		col.a = lerp(col.a, col.a * (0.3 + 0.7 * edgeMask), cellAmount * 0.5);
	}

    // ---- 織り目（横方向のクロスパターン）----
	if (weaveStrength > 0.0001)
	{
		float wv = sin(uv.x * weaveScale * 6.2831853) * sin(across * 3.14159265 * 3.0);
		col.rgb *= 1.0 - weaveStrength * 0.5 + weaveStrength * abs(wv);
	}

	float2 suv = In.clip.xy / In.clip.w;
	suv = suv * float2(0.5, -0.5) + 0.5;
	float3 sceneWP = g_gbufferC.Sample(g_sampler, suv).xyz;
	float sceneDist = (length(sceneWP) < 0.001) ? 10000.0 : length(g_camPos - sceneWP);
	col.a *= saturate((sceneDist - In.dist) / TRAIL_DEPTH_FADE);

	return float4(col.rgb * col.a, col.a);
}
