#include "inc_Transition.hlsli"

Texture2D g_prevTex : register(t0);
Texture2D g_transTex: register(t1);
SamplerState g_samp : register(s0);

// ===================================================================
// 調整用パラメータ
// ===================================================================
static const float3 COVER_COLOR = float3(0.0, 0.0, 0.0); // 幕の色
static const float ASPECT = 16.0 / 9.0;

// --- 幕の縁：大きなうねり ---
static const float WAVE_AMP = 0.06;
static const float WAVE_FREQ = 1.5;
static const float WAVE_SPEED = 2.0;

// --- 幕の縁：中くらいの揺らぎ ---
static const float NOISE_AMP = 0.05;
static const float NOISE_SCALE = 3.0;
static const float NOISE_SPEED = 0.6;

// --- 幕の縁：細かいザラつき ---
static const float GRAIN_AMP = 0.03;
static const float GRAIN_SCALE = 18.0;
static const float GRAIN_SPEED = 0.3;

// --- 光に抗う幕（画面の明るさで縁の進み方を変える）---
static const float LUMA_AMP = 0.12; // 明暗による縁のズレ量（0で無効）
static const float LUMA_BLUR = 0.012; // 明るさを測る範囲（ドット単位のチラつき防止）
static const float LUMA_LOW = 0.10; // この明るさ以下は「最も早く呑まれる」
static const float LUMA_HIGH = 0.85; // この明るさ以上は「最後まで抵抗する」

static const float EDGE_SOFTNESS = 0.15; // 境界の幅（ハーフトーン帯）

// --- ハーフトーン ---
static const float HALFTONE_AMOUNT = 1.0; // 0でなめらかな境界、1で網点
static const float HALFTONE_SCALE = 40.0;
static const float HALFTONE_AA = 0.06;

// --- 飛沫 ---
static const float SPLAT_SCALE = 14.0;
static const float SPLAT_DENSITY = 0.35;
static const float SPLAT_RANGE = 0.25;
static const float SPLAT_RADIUS_MIN = 0.10;
static const float SPLAT_RADIUS_MAX = 0.35;

// --- 液体の滴り ---
static const float DRIP_COUNT = 24.0;
static const float DRIP_DENSITY = 0.5;
static const float DRIP_LENGTH = 0.25;
static const float DRIP_WIDTH_MIN = 0.15;
static const float DRIP_WIDTH_MAX = 0.40;
static const float DRIP_WOBBLE_SPEED = 1.5;

// --- 画面の歪み ---
static const float WARP_WIDTH = 0.35;
static const float WARP_STRENGTH = 0.04;
static const float WARP_NOISE_SCALE = 4.0;
static const float WARP_NOISE_SPEED = 0.8;
static const float PULL_STRENGTH = 0.06;
static const float CHROMA_SHIFT = 0.35;

// --- スメア ／ 吸い込みズーム ---
static const int SMEAR_SAMPLES = 8;
static const float SMEAR_LENGTH = 0.08;
static const float ZOOM_STRENGTH = 0.10;
static const float ZOOM_BLUR = 0.06;

// 縁が本来の位置から最大どれだけズレるか（明暗によるズレも含む）
static const float EDGE_RANGE = WAVE_AMP + NOISE_AMP + GRAIN_AMP + LUMA_AMP;
static const float TWO_PI = 6.28318530718;
static const float PI = 3.14159265359;

// ===================================================================
// ノイズ
// ===================================================================
float Hash21(float2 p)
{
	p = frac(p * float2(123.34, 456.21));
	p += dot(p, p + 45.32);
	return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
	float2 i = floor(p);
	float2 f = frac(p);
	f = f * f * (3.0 - 2.0 * f);

	float a = Hash21(i);
	float b = Hash21(i + float2(1.0, 0.0));
	float c = Hash21(i + float2(0.0, 1.0));
	float d = Hash21(i + float2(1.0, 1.0));

	return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float Fbm(float2 p)
{
	float value = 0.0;
	float amp = 0.5;

    [unroll]
	for (int i = 0; i < 4; ++i)
	{
		value += ValueNoise(p) * amp;
		p *= 2.0;
		amp *= 0.5;
	}

	return value; // 0 〜 0.9375
}

float SignedFbm(float2 p)
{
	return (Fbm(p) - 0.5) * 2.0;
}

// ===================================================================
// 幕の縁
// ===================================================================
// ノイズによるズレ（絶対値は WAVE_AMP + NOISE_AMP + GRAIN_AMP 以下）
float CalcEdgeOffset(float2 uv, float seed)
{
	float phase = uv.y * WAVE_FREQ * TWO_PI + g_time * WAVE_SPEED + seed;
	float wave = (sin(phase) * 0.7 + sin(phase * 2.3 + 1.7) * 0.3) * WAVE_AMP;

	float noise = SignedFbm(float2(uv.y * NOISE_SCALE + seed, g_time * NOISE_SPEED)) * NOISE_AMP;

	float2 grainUV = uv * float2(ASPECT, 1.0) * GRAIN_SCALE + seed + g_time * GRAIN_SPEED;
	float grain = SignedFbm(grainUV) * GRAIN_AMP;

	return wave + noise + grain;
}

// 画面の明るさによるズレ（-LUMA_AMP 〜 +LUMA_AMP、明るいほど正）
float CalcLumaShift(float2 uv)
{
	const float3 LUMA_WEIGHT = float3(0.299, 0.587, 0.114);
	float2 d = float2(LUMA_BLUR / ASPECT, LUMA_BLUR);

    // 十字5点の平均で、細かい模様に縁が引っかからないようにする
	float lum = dot(g_prevTex.Sample(g_samp, uv).rgb, LUMA_WEIGHT)
              + dot(g_prevTex.Sample(g_samp, saturate(uv + float2(d.x, 0.0))).rgb, LUMA_WEIGHT)
              + dot(g_prevTex.Sample(g_samp, saturate(uv + float2(-d.x, 0.0))).rgb, LUMA_WEIGHT)
              + dot(g_prevTex.Sample(g_samp, saturate(uv + float2(0.0, d.y))).rgb, LUMA_WEIGHT)
              + dot(g_prevTex.Sample(g_samp, saturate(uv + float2(0.0, -d.y))).rgb, LUMA_WEIGHT);
	lum *= 0.2;

	return (smoothstep(LUMA_LOW, LUMA_HIGH, lum) * 2.0 - 1.0) * LUMA_AMP;
}

// 縁の座標
float CalcEdgeCoord(float2 uv, float seed, float edgeSign)
{
	return (1.0 - uv.x) + CalcEdgeOffset(uv, seed) + CalcLumaShift(uv) * edgeSign;
}

// ===================================================================
// ハーフトーン（a=0で何もなし、a=1で必ず全面）
// ===================================================================
float Halftone(float2 uv, float a)
{
	float2 p = uv * float2(ASPECT, 1.0) * HALFTONE_SCALE;
	p = float2(p.x - p.y, p.x + p.y) * 0.70710678;

	float dist = length(frac(p) - 0.5);
	float radius = sqrt(saturate(a)) * (0.70710678 + HALFTONE_AA);
	float dots = saturate((radius - dist) / HALFTONE_AA);

	return lerp(a, dots, HALFTONE_AMOUNT);
}

// ===================================================================
// 飛沫
// ===================================================================
float CalcSplat(float2 uv, float seed, float edgePos, float edgeSign, float edgeSeed, float weight)
{
	float2 scale = float2(ASPECT, 1.0) * SPLAT_SCALE;
	float2 p = uv * scale;
	float2 cell = floor(p);
	float2 f = p - cell;

	float hExist = Hash21(cell + seed);
	float hRad = Hash21(cell + seed + 11.1);
	float hRange = Hash21(cell + seed + 51.9);
	float2 hPos = float2(Hash21(cell + seed + 23.7), Hash21(cell + seed + 37.3));

	float2 center = SPLAT_RADIUS_MAX + (1.0 - 2.0 * SPLAT_RADIUS_MAX) * hPos;
	float radius = lerp(SPLAT_RADIUS_MIN, SPLAT_RADIUS_MAX, hRad) * weight;

    // 粒の中心で縁までの距離を測る（粒が欠けない）
	float2 centerUV = (cell + center) / scale;
	float gap = (CalcEdgeCoord(centerUV, edgeSeed, edgeSign) - edgePos) * edgeSign;

	float visible = step(gap, hRange * SPLAT_RANGE) * step(hExist, SPLAT_DENSITY);
	float dotA = saturate((radius - length(f - center)) / 0.05);

	return dotA * visible;
}

// ===================================================================
// 液体の滴り
// ===================================================================
float CalcDrip(float2 uv, float gap, float seed, float weight)
{
	float band = uv.y * DRIP_COUNT;
	float id = floor(band);
	float v = frac(band) - 0.5;

	float hExist = Hash21(float2(id, seed));
	float hLen = Hash21(float2(id, seed + 7.3));
	float hWid = Hash21(float2(id, seed + 13.1));

	float wobble = 0.8 + 0.2 * sin(g_time * DRIP_WOBBLE_SPEED + hLen * TWO_PI);
	float len = max(hLen * DRIP_LENGTH * wobble * weight, 1e-4);

	float along = saturate(gap / len);
	float width = lerp(DRIP_WIDTH_MIN, DRIP_WIDTH_MAX, hWid) * sqrt(1.0 - along);
	float body = saturate((width - abs(v)) / 0.05);

	return body
         * step(0.0, gap) * step(gap, len)
         * step(hExist, DRIP_DENSITY)
         * step(1e-4, weight);
}

// ===================================================================
// 歪み
// ===================================================================
float CalcWarpInfluence(float gap, float phaseWeight)
{
	float inf = saturate(1.0 - max(gap, 0.0) / WARP_WIDTH);
	return inf * inf * phaseWeight;
}

float2 MirrorUV(float2 uv)
{
	return 1.0 - abs(1.0 - abs(uv));
}

// ===================================================================
// ピクセルシェーダ
// ===================================================================
float4 PSMain(VSOutput In) : SV_TARGET
{
    // ---- 進行（線形）----
	float isOpening = step(g_switchPoint, g_progress);
	float closeT = saturate(g_progress / max(g_switchPoint, 1e-4));
	float openT = saturate((g_progress - g_switchPoint) / max(1.0 - g_switchPoint, 1e-4));

    // 各フェーズの中盤で1、開始・終了で0（演出のポップ防止）
	float closeW = max(sin(PI * closeT), 0.0);
	float openW = max(sin(PI * openT), 0.0);

	float front = lerp(-EDGE_RANGE, 1.0 + EDGE_RANGE + EDGE_SOFTNESS, closeT);
	float tail = lerp(-EDGE_RANGE - EDGE_SOFTNESS, 1.0 + EDGE_RANGE, openT);

    // ---- 縁（ノイズ＋明暗）----
	float x = 1.0 - In.uv.x;
	float lumaShift = CalcLumaShift(In.uv);
	float frontEdge = x + CalcEdgeOffset(In.uv, 0.0) + lumaShift;
	float tailEdge = x + CalcEdgeOffset(In.uv, 17.3) - lumaShift;

    // 見えている側で正になる、縁までの距離
	float frontGap = frontEdge - front;
	float tailGap = tail - tailEdge;

    // ---- 幕本体（ハーフトーン）----
	float alphaFront = saturate(-frontGap / EDGE_SOFTNESS);
	float alphaTail = saturate(-tailGap / EDGE_SOFTNESS);
	float curtain = Halftone(In.uv, alphaFront * alphaTail);

    // ---- 滴り ----
	float dripOffset = EDGE_SOFTNESS * 0.5;
	float drip = max(CalcDrip(In.uv, frontGap + dripOffset, 0.0, closeW),
                     CalcDrip(In.uv, tailGap + dripOffset, 41.0, openW));

    // ---- 飛沫 ----
	float splat = max(CalcSplat(In.uv, 0.0, front, 1.0, 0.0, closeW),
                      CalcSplat(In.uv, 73.0, tail, -1.0, 17.3, openW));

	float coverAlpha = max(curtain, max(drip, splat));

    // ---- 歪み ----
	float frontInf = CalcWarpInfluence(frontGap, closeW);
	float tailInf = CalcWarpInfluence(tailGap, openW);
	float warp = frontInf + tailInf;

	float2 hazeUV = In.uv * float2(ASPECT, 1.0) * WARP_NOISE_SCALE + g_time * WARP_NOISE_SPEED;
	float2 haze = float2(SignedFbm(hazeUV), SignedFbm(hazeUV + 31.7));

	float2 offset = haze * WARP_STRENGTH * warp;
	offset.x += (tailInf - frontInf) * PULL_STRENGTH;

    // ---- スメア ／ 吸い込みズーム ----
	float2 smearDir = float2(tailInf - frontInf, 0.0) * SMEAR_LENGTH;
	float edgeX = lerp(1.0 - front, 1.0 - tail, isOpening);
	float2 zoomCenter = float2(saturate(edgeX), 0.5);
	float zoomW = closeW + openW;
	float zoomBase = ZOOM_STRENGTH * zoomW;
	float zoomBlur = ZOOM_BLUR * zoomW;

    // ---- サンプリング（スメア＋放射ブラー＋色ズレ）----
	float3 scene = float3(0.0, 0.0, 0.0);
	float stepDiv = (float) max(SMEAR_SAMPLES - 1, 1);

    [unroll]
	for (int i = 0; i < SMEAR_SAMPLES; ++i)
	{
		float t = (float) i / stepDiv;
		float scale = 1.0 + zoomBase + zoomBlur * t;
		float2 baseUV = zoomCenter + (In.uv - zoomCenter) * scale + smearDir * t;

		scene.r += g_prevTex.Sample(g_samp, MirrorUV(baseUV + offset * (1.0 + CHROMA_SHIFT))).r;
		scene.g += g_prevTex.Sample(g_samp, MirrorUV(baseUV + offset)).g;
		scene.b += g_prevTex.Sample(g_samp, MirrorUV(baseUV + offset * (1.0 - CHROMA_SHIFT))).b;
	}
	scene /= (float) SMEAR_SAMPLES;

	float3 color = lerp(scene, COVER_COLOR, coverAlpha);
	if (dot(color.rgb, 1) <= 0)
	{
		float3 transColor = g_transTex.Sample(g_samp, MirrorUV(In.uv + offset * (1.0 + CHROMA_SHIFT))) * 0.05f;
		return float4(transColor, 1.0f);
	}
	return float4(color,1.0f);

}
