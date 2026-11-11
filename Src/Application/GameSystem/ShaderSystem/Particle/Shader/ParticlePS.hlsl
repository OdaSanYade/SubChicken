Texture2D g_tex0 : register(t0);
Texture2D g_tex1 : register(t1);
Texture2D g_tex2 : register(t2);
Texture2D g_tex3 : register(t3);
Texture2D g_tex4 : register(t4);
Texture2D g_tex5 : register(t5);
Texture2D g_tex6 : register(t6);
Texture2D g_tex7 : register(t7);
Texture2D g_gbufferC : register(t8);
SamplerState g_sampler : register(s0);

cbuffer CB : register(b0)
{
	float4x4 g_viewProj;
	float4x4 g_view;
	float g_size;
	int g_particlesPerEmitter;
	float g_nearZ;
	float g_farZ;
	float3 g_camPos;
	float _pad;
};

struct VS_OUT
{
	float4 pos : SV_Position;
	float2 uv : TEXCOORD0;
	float4 color : COLOR;
	nointerpolation int texIndex : TEXINDEX;
	float viewDepth : VIEWDEPTH;
	float4 clipPos : CLIPPOS;
	nointerpolation float4 form0 : FORM0;
	nointerpolation float4 form1 : FORM1;
	nointerpolation float4 form2 : FORM2;
	nointerpolation float4 form3 : FORM3;
};

float hash12(float2 p)
{
	float3 p3 = frac(float3(p.xyx) * 0.1031);
	p3 += dot(p3, p3.yzx + 33.33);
	return frac((p3.x + p3.y) * p3.z);
}

float vnoise2(float2 x)
{
	float2 i = floor(x);
	float2 f = frac(x);
	f = f * f * (3.0 - 2.0 * f);
	return lerp(lerp(hash12(i + float2(0, 0)), hash12(i + float2(1, 0)), f.x),
                lerp(hash12(i + float2(0, 1)), hash12(i + float2(1, 1)), f.x), f.y);
}

// 手続き形状：多角形 + 丸み + 星化 + アスペクト + ねじれ + 中空
//              + 回転 + シアー + 波打ち + ふくらみ + 扇形 + 溶解 + コア
float FormAlpha(float2 uv, float4 f0, float4 f1, float4 f2, float4 f3)
{
	float sides = f0.x;
	float roundAmt = saturate(f0.y);
	float spike = saturate(f0.z);
	float aspect = max(f0.w, 0.01);
	float twist = f1.x;
	float edge = max(f1.y, 0.001);
	float hollow = saturate(f1.z);

	float rotAng = f2.x;
	float shear = f2.y;
	float ripple = f2.z;
	float rippleFrq = max(f2.w, 1.0);
	float bulge = f3.x;
	float arc = saturate(f3.y);
	float dissolve = saturate(f3.z);
	float core = f3.w;

	float2 p = uv * 2.0 - 1.0;

    // 形そのものを回す（粒の回転とは別。扇形の向き決めにも使う）
	float rs, rc;
	sincos(rotAng, rs, rc);
	p = float2(p.x * rc - p.y * rs, p.x * rs + p.y * rc);

    // シアー：平行四辺形にする
	p.x += p.y * shear;

	p.x /= aspect;
	p.y *= aspect;

	float r = length(p);
	if (r < 1e-5)
		return saturate(1.0 + core);

	float a = atan2(p.y, p.x) + twist * r;

	float rp = 1.0;
	if (sides >= 2.5)
	{
		float seg = 6.2831853 / sides;
		float h = fmod(a + 6.2831853 * 8.0, seg) - seg * 0.5;
		float rMid = cos(seg * 0.5);
		float base = rMid / max(cos(h), 1e-4);

        // ふくらみ／へこみ：1.0で直線、小で外へ張り、大で内へ凹む
		if (abs(bulge - 1.0) > 0.001)
		{
			float u = saturate(abs(h) / (seg * 0.5));
			base = lerp(rMid, base, pow(u, max(bulge, 0.01)));
		}

		rp = lerp(base, 1.0, roundAmt);

		if (spike > 0.0001)
		{
			float tv = 1.0 - abs(h) / (seg * 0.5);
			rp *= lerp(1.0 - spike, 1.0, tv);
		}
	}

    // 輪郭の波打ち：花びら・歯車・ギザギザ
	if (abs(ripple) > 0.0001)
		rp *= 1.0 + sin(a * rippleFrq) * ripple;

	float d = r / max(rp, 1e-4);

	float alpha = 1.0 - smoothstep(1.0 - edge, 1.0, d);

	if (hollow > 0.0001)
		alpha *= smoothstep(1.0 - hollow - edge, 1.0 - hollow, d);

    // 扇形に切り取る（1.0で全周。Rotate で向きを決める）
	if (arc < 0.999)
	{
		float an = abs(atan2(p.y, p.x));
		float halfArc = arc * 3.1415927;
		alpha *= 1.0 - smoothstep(halfArc - 0.08, halfArc, an);
	}

    // 溶解：ノイズで食い散らかす
	if (dissolve > 0.0001)
	{
		float n = vnoise2(uv * 9.0);
		alpha *= smoothstep(dissolve, dissolve + 0.18, n);
	}

    // 中心の明るいコア
	if (core > 0.0001)
		alpha += (1.0 - smoothstep(0.0, 0.55, d)) * core;

	return saturate(alpha);
}

float4 main(VS_OUT i) : SV_Target
{
	float4 t;
	if (i.texIndex < 0)
	{
        // ★8 リボン：幅方向にソフトなフォールオフ（form1.y = Edge Soft）
		float across = 1.0 - abs(i.uv.x * 2.0 - 1.0);
		float soft = max(i.form1.y, 0.02);
		t = float4(1, 1, 1, smoothstep(0.0, soft, across));
	}
	else if (i.form1.w > 0.5)
	{
        // 手続き形状（テクスチャを使わない）
		t = float4(1, 1, 1, FormAlpha(i.uv, i.form0, i.form1, i.form2, i.form3));
	}
	else
	{
		int ti = i.texIndex;
		if (ti == 1)
			t = g_tex1.Sample(g_sampler, i.uv);
		else if (ti == 2)
			t = g_tex2.Sample(g_sampler, i.uv);
		else if (ti == 3)
			t = g_tex3.Sample(g_sampler, i.uv);
		else if (ti == 4)
			t = g_tex4.Sample(g_sampler, i.uv);
		else if (ti == 5)
			t = g_tex5.Sample(g_sampler, i.uv);
		else if (ti == 6)
			t = g_tex6.Sample(g_sampler, i.uv);
		else if (ti == 7)
			t = g_tex7.Sample(g_sampler, i.uv);
		else
			t = g_tex0.Sample(g_sampler, i.uv);
	}
	float4 col = i.color * t;

	float2 screenUV = i.clipPos.xy / i.clipPos.w;
	screenUV = screenUV * float2(0.5, -0.5) + 0.5;

    // GBufferC から worldPos → カメラ距離
	float3 sceneWorldPos = g_gbufferC.Sample(g_sampler, screenUV).xyz;
	float sceneDist = length(g_camPos - sceneWorldPos);
    // 背景（worldPos≈0）は遠方扱い（隠さない）
	if (length(sceneWorldPos) < 0.001)
		sceneDist = 10000.0;

    // パーティクルのカメラ距離（VSで計算済み）
	float particleDist = i.viewDepth;

	float fadeRange = 0.5;
	float fade = saturate((sceneDist - particleDist) / fadeRange);
	col.a *= fade;
	return col;
}
