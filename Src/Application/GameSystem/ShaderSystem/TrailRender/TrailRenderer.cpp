#include "TrailRenderer.h"
#include "Shader/TrailPS.shaderInc"
#include "Shader/TrailVS.shaderInc"

namespace {
	struct TrailCB
	{
		DirectX::XMFLOAT4X4 viewProj;
		DirectX::XMFLOAT3   camPos;
		float               time;
	};
	static const int kMaxTrailVerts = 65536;  

	// 可変点数の線形補間
	float EvalCurveLinear(const float* s, int count, float t)
	{
		if (count < 2) return (count == 1) ? s[0] : 0.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;

		float f = t * (float)(count - 1);
		int i = (int)f;
		if (i > count - 2) i = count - 2;
		return s[i] + (s[i + 1] - s[i]) * (f - (float)i);
	}

	// Catmull-Rom（端は自分を折り返して使う）
	float EvalCurveSmooth(const float* s, int count, float t)
	{
		if (count < 2) return (count == 1) ? s[0] : 0.0f;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;

		float f = t * (float)(count - 1);
		int i = (int)f;
		if (i > count - 2) i = count - 2;
		float local = f - (float)i;

		auto at = [&](int idx) -> float
			{
				if (idx < 0) idx = 0;
				if (idx > count - 1) idx = count - 1;
				return s[idx];
			};

		float p0 = at(i - 1), p1 = at(i), p2 = at(i + 1), p3 = at(i + 2);
		float t2 = local * local, t3 = t2 * local;
		return 0.5f * ((2.0f * p1) +
			(-p0 + p2) * local +
			(2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
			(-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	}

	// ---- CPU側 RGB<->HSV（hueJitter等の色ずらし用）----
	void RGBtoHSV(float r, float g, float b, float& h, float& s, float& v)
	{
		float mx = r; if (g > mx) mx = g; if (b > mx) mx = b;
		float mn = r; if (g < mn) mn = g; if (b < mn) mn = b;
		v = mx;
		float d = mx - mn;
		s = (mx <= 1e-6f) ? 0.0f : (d / mx);

		if (d <= 1e-6f) { h = 0.0f; return; }

		if (mx == r)      h = fmodf((g - b) / d, 6.0f);
		else if (mx == g) h = (b - r) / d + 2.0f;
		else              h = (r - g) / d + 4.0f;

		h /= 6.0f;
		if (h < 0.0f) h += 1.0f;
	}

	void HSVtoRGB(float h, float s, float v, float& r, float& g, float& b)
	{
		h -= floorf(h);   // 0〜1へ巻き戻す
		float i = floorf(h * 6.0f);
		float f = h * 6.0f - i;
		float p = v * (1.0f - s);
		float q = v * (1.0f - f * s);
		float t = v * (1.0f - (1.0f - f) * s);

		int k = ((int)i) % 6;
		if (k < 0) k += 6;

		static const int kHsvTable[6][3] = {
			{ 0, 1, 2 }, // k=0: r=v g=t b=p
			{ 3, 0, 2 }, // k=1: r=q g=v b=p
			{ 2, 0, 1 }, // k=2: r=p g=v b=t
			{ 2, 3, 0 }, // k=3: r=p g=q b=v
			{ 1, 2, 0 }, // k=4: r=t g=p b=v
			{ 0, 2, 3 }, // k=5: r=v g=p b=q
		};
		const float comp[4] = { v, t, p, q };
		r = comp[kHsvTable[k][0]];
		g = comp[kHsvTable[k][1]];
		b = comp[kHsvTable[k][2]];
	}

	// 0〜1 の疑似乱数（決定的。seed が同じなら常に同じ値）
	float HashSeed(float x)
	{
		float s = sinf(x * 12.9898f) * 43758.5453f;
		return s - floorf(s);
	}

	float Frac(float x) { return x - floorf(x); }

	float Hash13(const DirectX::XMFLOAT3& p0)
	{
		DirectX::XMFLOAT3 p = {
			Frac(p0.x * 0.3183099f + 0.1f),
			Frac(p0.y * 0.3183099f + 0.1f),
			Frac(p0.z * 0.3183099f + 0.1f) };
		p.x *= 17.0f; p.y *= 17.0f; p.z *= 17.0f;
		return Frac(p.x * p.y * p.z * (p.x + p.y + p.z));
	}

	float VNoise(const DirectX::XMFLOAT3& x)
	{
		DirectX::XMFLOAT3 i = { floorf(x.x), floorf(x.y), floorf(x.z) };
		DirectX::XMFLOAT3 f = { x.x - i.x, x.y - i.y, x.z - i.z };
		f.x = f.x * f.x * (3.0f - 2.0f * f.x);
		f.y = f.y * f.y * (3.0f - 2.0f * f.y);
		f.z = f.z * f.z * (3.0f - 2.0f * f.z);

		auto n = [&](float dx, float dy, float dz)
			{ return Hash13({ i.x + dx, i.y + dy, i.z + dz }); };

		float n000 = n(0, 0, 0), n100 = n(1, 0, 0);
		float n010 = n(0, 1, 0), n110 = n(1, 1, 0);
		float n001 = n(0, 0, 1), n101 = n(1, 0, 1);
		float n011 = n(0, 1, 1), n111 = n(1, 1, 1);

		float nx00 = n000 + (n100 - n000) * f.x;
		float nx10 = n010 + (n110 - n010) * f.x;
		float nx01 = n001 + (n101 - n001) * f.x;
		float nx11 = n011 + (n111 - n011) * f.x;
		float nxy0 = nx00 + (nx10 - nx00) * f.y;
		float nxy1 = nx01 + (nx11 - nx01) * f.y;
		return (nxy0 + (nxy1 - nxy0) * f.z) * 2.0f - 1.0f;
	}

	DirectX::XMFLOAT3 Potential(const DirectX::XMFLOAT3& p)
	{
		return {
			VNoise(p),
			VNoise({ p.x + 31.41f, p.y + 17.23f, p.z + 47.11f }),
			VNoise({ p.x + 11.73f, p.y + 63.31f, p.z + 29.97f }) };
	}

	DirectX::XMFLOAT3 CurlNoiseCPU(const DirectX::XMFLOAT3& p)
	{
		const float e = 0.15f;
		DirectX::XMFLOAT3 px0 = Potential({ p.x - e, p.y, p.z });
		DirectX::XMFLOAT3 px1 = Potential({ p.x + e, p.y, p.z });
		DirectX::XMFLOAT3 py0 = Potential({ p.x, p.y - e, p.z });
		DirectX::XMFLOAT3 py1 = Potential({ p.x, p.y + e, p.z });
		DirectX::XMFLOAT3 pz0 = Potential({ p.x, p.y, p.z - e });
		DirectX::XMFLOAT3 pz1 = Potential({ p.x, p.y, p.z + e });

		const float inv2e = 1.0f / (2.0f * e);
		return {
			((py1.z - py0.z) - (pz1.y - pz0.y)) * inv2e,
			((pz1.x - pz0.x) - (px1.z - px0.z)) * inv2e,
			((px1.y - px0.y) - (py1.x - py0.x)) * inv2e };
	}

	DirectX::XMFLOAT3 CurlNoiseFBM(const DirectX::XMFLOAT3& p, int octaves)
	{
		if (octaves < 1) octaves = 1;
		if (octaves > 4) octaves = 4;

		DirectX::XMFLOAT3 sum = { 0.0f, 0.0f, 0.0f };
		float amp = 1.0f, freq = 1.0f, ampSum = 0.0f;
		for (int o = 0; o < octaves; ++o)
		{
			const DirectX::XMFLOAT3 c = CurlNoiseCPU({ p.x * freq, p.y * freq, p.z * freq });
			sum.x += c.x * amp; sum.y += c.y * amp; sum.z += c.z * amp;
			ampSum += amp;
			amp *= 0.5f;
			freq *= 2.0f;
		}
		if (ampSum > 1e-5f) { sum.x /= ampSum; sum.y /= ampSum; sum.z /= ampSum; }
		return sum;
	}

	// 分岐トレイル用：seedから決定的な単位方向ベクトルを作る（面白い機能3）
	DirectX::XMFLOAT3 RandomDir(float seed)
	{
		float u1 = HashSeed(seed * 3.107f + 0.37f);
		float u2 = HashSeed(seed * 7.919f + 1.913f);
		float z = u2 * 2.0f - 1.0f;
		float r = sqrtf((1.0f - z * z) > 0.0f ? (1.0f - z * z) : 0.0f);
		float theta = u1 * 6.2831853f;
		return { r * cosf(theta), r * sinf(theta), z };
	}
}

//==============================================================================
// 見た目パラメータ（.trail.json）。TrailRenderer の中だけで使う
//==============================================================================
struct TrailRenderer::Asset
{
	std::string name;

	// 帯に沿った倍率
	static const int kMaxCurvePoints = 16;
	float widthCurve[kMaxCurvePoints] = {
		1.0f, 1.0f, 0.95f, 0.85f, 0.7f, 0.5f, 0.3f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	float alphaCurve[kMaxCurvePoints] = {
		1.0f, 1.0f, 0.9f, 0.75f, 0.55f, 0.35f, 0.15f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	int   curvePointCount = 8;
	int   curveInterp = 0;       // 0=Linear 1=Smooth

	int   colorKeyCount = 2;
	// rgb + 位置(0〜1) を4キー分
	float colorKeys[16] = {
		1.0f, 1.0f, 1.0f, 0.0f,
		0.5f, 0.7f, 1.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f };
	// 帯の1点ごとに色をランダムにずらす（0で従来どおり無効）
	float hueJitter = 0.0f, satJitter = 0.0f, valJitter = 0.0f;

	float intensity = 1.0f;      // 全体の明るさ（HDR前提なので1超えも可）
	int   additive = 1;          // 1=加算 0=アルファ
	int   textureIndex = -1;     // -1 = 手続き（テクスチャを使わない）
	float edgeSoft = 0.3f;       // 幅方向のぼかし。0で硬い縁、1で光の帯
	float edgeCore = 0.0f;       // 中心線を明るくする
	int   uvMode = 0;            // 0=帯全体に引き伸ばす 1=長さでタイル
	float uvTiling = 1.0f;
	float uvScroll = 0.0f;
	int smoothing = 2;

	// ---- 質感（以前はこの下のPS内固定値だった6定数）----
	float fadePow = 1.5f;
	float coreWidth = 0.35f;
	float coreWhite = 0.6f;
	float coreBoost = 4.0f;
	float rootBoost = 2.0f;
	float depthFade = 0.5f;

	// ---- ノイズ変位（面白い機能2）----
	float noiseStrength = 0.0f;
	float noiseScale = 0.5f;
	float noiseSpeed = 1.0f;

	// ---- 分岐トレイル（面白い機能3）----
	int   branchOn = 0;
	float branchChance = 0.15f;
	float branchLength = 0.3f;
	float branchWidth = 0.05f;
	int   branchSegments = 3;

	// ---- ノイズ拡張（フェーズ3）----
	int   noiseOctaves = 1;
	float uvNoiseStrength = 0.0f;
	float uvNoiseScale = 3.0f;
	float uvNoiseSpeed = 1.0f;
	float dissolveAmount = 0.0f;
	float dissolveNoiseScale = 4.0f;
	float dissolveEdgeWidth = 0.15f;
	float edgeNoiseStrength = 0.0f;
	float edgeNoiseScale = 5.0f;

	// ---- 色拡張（フェーズ3）----
	float hueSpeed = 0.0f;
	float speedColorAmount = 0.0f;
	float speedColorMax = 3.0f;
	DirectX::XMFLOAT3 speedColorHot = { 1.0f, 1.0f, 1.0f };
	float ageDesaturate = 0.0f;
	float tempAmount = 0.0f;
	DirectX::XMFLOAT3 tempHotColor = { 1.0f, 0.9f, 0.6f };
	DirectX::XMFLOAT3 tempCoolColor = { 0.3f, 0.4f, 1.0f };

	// ---- パターン（フェーズ3）----
	float pulseAmp = 0.0f;
	float pulseSpeed = 4.0f;
	float pulseWidth = 0.2f;
	float dashDuty = 1.0f;
	float dashFreq = 8.0f;
	float specBoost = 0.0f;
	float specWidth = 0.08f;
	float specSpeed = 1.5f;

	// ---- 特殊質感（フェーズ3）----
	float cellAmount = 0.0f;
	float cellScale = 6.0f;
	float cellEdgeWidth = 0.08f;
	float cellGlow = 1.5f;
	float weaveStrength = 0.0f;
	float weaveScale = 10.0f;
	float electricChance = 0.0f;
	float electricScale = 3.0f;
	int   crowdCount = 1;
	float crowdSpacing = 0.15f;
	float crowdJitter = 0.0f;

	// ---- トレイル→パーティクル発生 ----
	int         particleOn = 0;
	std::string particleEffect;
	float       particleChance = 0.3f;

	float EvalWidth(float t) const
	{
		return (curveInterp != 0)
			? EvalCurveSmooth(widthCurve, curvePointCount, t)
			: EvalCurveLinear(widthCurve, curvePointCount, t);
	}
	float EvalAlpha(float t) const
	{
		return (curveInterp != 0)
			? EvalCurveSmooth(alphaCurve, curvePointCount, t)
			: EvalCurveLinear(alphaCurve, curvePointCount, t);
	}

	// seed を変えると hueJitter/satJitter/valJitter ぶんだけ色がずれる。
	// seed=0 かつジッター全て0なら従来と完全に同じ結果になる。
	void EvalColor(float t, float out[3], float seed = 0.0f) const
	{
		int n = colorKeyCount;
		if (n < 2) n = 2;
		if (n > 4) n = 4;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;

		out[0] = colorKeys[0]; out[1] = colorKeys[1]; out[2] = colorKeys[2];

		for (int i = 0; i + 1 < n; ++i)
		{
			const float* k0 = colorKeys + i * 4;
			const float* k1 = colorKeys + (i + 1) * 4;
			if (t < k0[3]) continue;

			float span = k1[3] - k0[3];
			if (span < 1e-4f) span = 1e-4f;
			float f = (t - k0[3]) / span;
			if (f < 0.0f) f = 0.0f;
			if (f > 1.0f) f = 1.0f;

			out[0] = k0[0] + (k1[0] - k0[0]) * f;
			out[1] = k0[1] + (k1[1] - k0[1]) * f;
			out[2] = k0[2] + (k1[2] - k0[2]) * f;
		}

		if (hueJitter > 0.0001f || satJitter > 0.0001f || valJitter > 0.0001f)
		{
			float h, s, v;
			RGBtoHSV(out[0], out[1], out[2], h, s, v);

			h += (HashSeed(seed * 7.31f) - 0.5f) * hueJitter;
			s += (HashSeed(seed * 13.77f) - 0.5f) * satJitter;
			v += (HashSeed(seed * 19.13f) - 0.5f) * valJitter;
			if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
			if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;

			HSVtoRGB(h, s, v, out[0], out[1], out[2]);
		}
	}

	bool FromJson(const nlohmann::json& j)
	{
		try
		{
			name = j.value("name", name);
			if (!j.contains("params")) return true;
			const auto& p = j["params"];

			intensity = p.value("intensity", intensity);
			additive = p.value("additive", additive);
			textureIndex = p.value("textureIndex", textureIndex);
			edgeSoft = p.value("edgeSoft", edgeSoft);
			edgeCore = p.value("edgeCore", edgeCore);
			uvMode = p.value("uvMode", uvMode);
			uvTiling = p.value("uvTiling", uvTiling);
			uvScroll = p.value("uvScroll", uvScroll);
			colorKeyCount = p.value("colorKeyCount", colorKeyCount);
			smoothing = p.value("smoothing", smoothing);

			curvePointCount = p.value("curvePointCount", curvePointCount);
			curveInterp = p.value("curveInterp", curveInterp);

			hueJitter = p.value("hueJitter", hueJitter);
			satJitter = p.value("satJitter", satJitter);
			valJitter = p.value("valJitter", valJitter);

			fadePow = p.value("fadePow", fadePow);
			coreWidth = p.value("coreWidth", coreWidth);
			coreWhite = p.value("coreWhite", coreWhite);
			coreBoost = p.value("coreBoost", coreBoost);
			rootBoost = p.value("rootBoost", rootBoost);
			depthFade = p.value("depthFade", depthFade);

			noiseStrength = p.value("noiseStrength", noiseStrength);
			noiseScale = p.value("noiseScale", noiseScale);
			noiseSpeed = p.value("noiseSpeed", noiseSpeed);

			branchOn = p.value("branchOn", branchOn);
			branchChance = p.value("branchChance", branchChance);
			branchLength = p.value("branchLength", branchLength);
			branchWidth = p.value("branchWidth", branchWidth);
			branchSegments = p.value("branchSegments", branchSegments);

			particleOn = p.value("particleOn", particleOn);
			particleEffect = p.value("particleEffect", particleEffect);
			particleChance = p.value("particleChance", particleChance);

			noiseOctaves = p.value("noiseOctaves", noiseOctaves);
			uvNoiseStrength = p.value("uvNoiseStrength", uvNoiseStrength);
			uvNoiseScale = p.value("uvNoiseScale", uvNoiseScale);
			uvNoiseSpeed = p.value("uvNoiseSpeed", uvNoiseSpeed);
			dissolveAmount = p.value("dissolveAmount", dissolveAmount);
			dissolveNoiseScale = p.value("dissolveNoiseScale", dissolveNoiseScale);
			dissolveEdgeWidth = p.value("dissolveEdgeWidth", dissolveEdgeWidth);
			edgeNoiseStrength = p.value("edgeNoiseStrength", edgeNoiseStrength);
			edgeNoiseScale = p.value("edgeNoiseScale", edgeNoiseScale);

			hueSpeed = p.value("hueSpeed", hueSpeed);
			speedColorAmount = p.value("speedColorAmount", speedColorAmount);
			speedColorMax = p.value("speedColorMax", speedColorMax);
			ageDesaturate = p.value("ageDesaturate", ageDesaturate);
			tempAmount = p.value("tempAmount", tempAmount);

			pulseAmp = p.value("pulseAmp", pulseAmp);
			pulseSpeed = p.value("pulseSpeed", pulseSpeed);
			pulseWidth = p.value("pulseWidth", pulseWidth);
			dashDuty = p.value("dashDuty", dashDuty);
			dashFreq = p.value("dashFreq", dashFreq);
			specBoost = p.value("specBoost", specBoost);
			specWidth = p.value("specWidth", specWidth);
			specSpeed = p.value("specSpeed", specSpeed);

			cellAmount = p.value("cellAmount", cellAmount);
			cellScale = p.value("cellScale", cellScale);
			cellEdgeWidth = p.value("cellEdgeWidth", cellEdgeWidth);
			cellGlow = p.value("cellGlow", cellGlow);
			weaveStrength = p.value("weaveStrength", weaveStrength);
			weaveScale = p.value("weaveScale", weaveScale);
			electricChance = p.value("electricChance", electricChance);
			electricScale = p.value("electricScale", electricScale);
			crowdCount = p.value("crowdCount", crowdCount);
			crowdSpacing = p.value("crowdSpacing", crowdSpacing);
			crowdJitter = p.value("crowdJitter", crowdJitter);

			auto readArr = [&](const char* key, float* dst, int count)
				{
					if (!p.contains(key) || !p[key].is_array()) return;
					const auto& a = p[key];
					for (int i = 0; i < count && i < (int)a.size(); ++i)
						dst[i] = a[i].get<float>();
				};
			// kMaxCurvePoints(16)まで読む。旧アセット（8要素）はそのまま8点として読める
			readArr("widthCurve", widthCurve, kMaxCurvePoints);
			readArr("alphaCurve", alphaCurve, kMaxCurvePoints);
			readArr("colorKeys", colorKeys, 16);

			auto readVec3 = [&](const char* key, DirectX::XMFLOAT3& dst)
				{
					if (!p.contains(key) || !p[key].is_array() || p[key].size() < 3) return;
					const auto& a = p[key];
					dst.x = a[0].get<float>();
					dst.y = a[1].get<float>();
					dst.z = a[2].get<float>();
				};
			readVec3("speedColorHot", speedColorHot);
			readVec3("tempHotColor", tempHotColor);
			readVec3("tempCoolColor", tempCoolColor);

			if (uvTiling <= 0.0f) uvTiling = 1.0f;
			if (edgeSoft < 0.0f) edgeSoft = 0.0f;
			if (colorKeyCount < 2 || colorKeyCount > 4) colorKeyCount = 2;

			if (curvePointCount < 2) curvePointCount = 2;
			if (curvePointCount > kMaxCurvePoints) curvePointCount = kMaxCurvePoints;
			if (curveInterp != 0 && curveInterp != 1) curveInterp = 0;

			if (hueJitter < 0.0f) hueJitter = 0.0f; if (hueJitter > 1.0f) hueJitter = 1.0f;
			if (satJitter < 0.0f) satJitter = 0.0f; if (satJitter > 1.0f) satJitter = 1.0f;
			if (valJitter < 0.0f) valJitter = 0.0f; if (valJitter > 1.0f) valJitter = 1.0f;

			if (fadePow <= 0.0f) fadePow = 1.5f;
			if (coreWidth < 0.0f) coreWidth = 0.0f; if (coreWidth > 1.0f) coreWidth = 1.0f;
			if (coreWhite < 0.0f) coreWhite = 0.0f; if (coreWhite > 1.0f) coreWhite = 1.0f;
			if (coreBoost < 0.0f) coreBoost = 0.0f;
			if (rootBoost < 0.0f) rootBoost = 0.0f;
			if (depthFade <= 0.0f) depthFade = 0.5f;

			if (noiseStrength < 0.0f) noiseStrength = 0.0f;
			if (noiseScale < 0.0f) noiseScale = 0.0f;

			if (branchOn != 0 && branchOn != 1) branchOn = 0;
			if (branchChance < 0.0f) branchChance = 0.0f; if (branchChance > 1.0f) branchChance = 1.0f;
			if (branchLength < 0.0f) branchLength = 0.0f;
			if (branchWidth < 0.0f) branchWidth = 0.0f;
			if (branchSegments < 1) branchSegments = 1;
			if (branchSegments > 8) branchSegments = 8;

			if (particleOn != 0 && particleOn != 1) particleOn = 0;
			if (particleChance < 0.0f) particleChance = 0.0f; if (particleChance > 1.0f) particleChance = 1.0f;

			if (noiseOctaves < 1) noiseOctaves = 1; if (noiseOctaves > 4) noiseOctaves = 4;
			if (uvNoiseStrength < 0.0f) uvNoiseStrength = 0.0f;
			if (uvNoiseScale < 0.0f) uvNoiseScale = 0.0f;
			if (dissolveAmount < 0.0f) dissolveAmount = 0.0f; if (dissolveAmount > 1.0f) dissolveAmount = 1.0f;
			if (dissolveNoiseScale < 0.0f) dissolveNoiseScale = 0.0f;
			if (dissolveEdgeWidth < 0.001f) dissolveEdgeWidth = 0.001f;
			if (edgeNoiseStrength < 0.0f) edgeNoiseStrength = 0.0f;
			if (edgeNoiseScale < 0.0f) edgeNoiseScale = 0.0f;

			if (speedColorAmount < 0.0f) speedColorAmount = 0.0f; if (speedColorAmount > 1.0f) speedColorAmount = 1.0f;
			if (speedColorMax <= 0.0f) speedColorMax = 3.0f;
			if (ageDesaturate < 0.0f) ageDesaturate = 0.0f; if (ageDesaturate > 1.0f) ageDesaturate = 1.0f;
			if (tempAmount < 0.0f) tempAmount = 0.0f; if (tempAmount > 1.0f) tempAmount = 1.0f;

			if (pulseAmp < 0.0f) pulseAmp = 0.0f;
			if (pulseWidth < 0.01f) pulseWidth = 0.01f; if (pulseWidth > 1.0f) pulseWidth = 1.0f;
			if (dashDuty < 0.02f) dashDuty = 0.02f; if (dashDuty > 1.0f) dashDuty = 1.0f;
			if (dashFreq < 0.0f) dashFreq = 0.0f;
			if (specBoost < 0.0f) specBoost = 0.0f;
			if (specWidth < 0.001f) specWidth = 0.001f; if (specWidth > 1.0f) specWidth = 1.0f;

			if (cellAmount < 0.0f) cellAmount = 0.0f; if (cellAmount > 1.0f) cellAmount = 1.0f;
			if (cellScale < 0.01f) cellScale = 0.01f;
			if (cellEdgeWidth < 0.001f) cellEdgeWidth = 0.001f; if (cellEdgeWidth > 1.0f) cellEdgeWidth = 1.0f;
			if (cellGlow < 0.0f) cellGlow = 0.0f;
			if (weaveStrength < 0.0f) weaveStrength = 0.0f; if (weaveStrength > 1.0f) weaveStrength = 1.0f;
			if (weaveScale < 0.01f) weaveScale = 0.01f;
			if (electricChance < 0.0f) electricChance = 0.0f; if (electricChance > 1.0f) electricChance = 1.0f;
			if (electricScale < 0.01f) electricScale = 0.01f;
			if (crowdCount < 1) crowdCount = 1; if (crowdCount > 8) crowdCount = 8;
			if (crowdSpacing < 0.0f) crowdSpacing = 0.0f;
			if (crowdJitter < 0.0f) crowdJitter = 0.0f;
			return true;
		}
		catch (...) { return false; }
	}
};

//==============================================================================
// シェーダ
//==============================================================================
bool TrailRenderer::Init()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	m_assets = std::make_shared<std::vector<Asset>>();

	if (FAILED(device->CreateVertexShader(
		TrailVS, sizeof(TrailVS), nullptr, &m_vs)))
		return false;

	D3D11_INPUT_ELEMENT_DESC layout[] = {
		{ "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "COLOR",      0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 20, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "PARAMINDEX", 0, DXGI_FORMAT_R32_UINT,           0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	if (FAILED(device->CreateInputLayout(layout, (UINT)_countof(layout),
		TrailVS, sizeof(TrailVS), &m_layout)))
		return false;

	// ---- 見た目パラメータ（トレイルごと、VS が番号で引く）----
	{
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = sizeof(ParamSet) * kMaxParamSets;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		d.StructureByteStride = sizeof(ParamSet);
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_paramBuffer))) return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = DXGI_FORMAT_UNKNOWN;
		sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sd.Buffer.NumElements = kMaxParamSets;
		if (FAILED(device->CreateShaderResourceView(m_paramBuffer.Get(), &sd, &m_paramSRV))) return false;
	}

	// ---- PS ----
	if (FAILED(device->CreatePixelShader(
		TrailPS, sizeof(TrailPS), nullptr, &m_ps)))
		return false;

	// ---- 動的頂点バッファ ----
	m_maxVerts = kMaxTrailVerts;
	{
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = sizeof(Vertex) * m_maxVerts;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_vb))) return false;
	}

	// ---- 定数バッファ ----
	{
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = sizeof(TrailCB);
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_cb))) return false;
	}

	// ---- ブレンド（PS がプリマルチプライドで返す）----
	{
		D3D11_BLEND_DESC d = {};
		d.RenderTarget[0].BlendEnable = TRUE;
		d.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
		d.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		d.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		d.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
		d.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		d.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&d, &m_additive))) return false;

		d.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		d.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		if (FAILED(device->CreateBlendState(&d, &m_alphaBlend))) return false;
	}

	// ---- 深度: テストなし・書き込みなし（GBufferCで自前遮蔽するため）----
	{
		D3D11_DEPTH_STENCIL_DESC d = {};
		d.DepthEnable = FALSE;
		d.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		if (FAILED(device->CreateDepthStencilState(&d, &m_depthState))) return false;
	}

	// ---- ラスタライザ: 両面 ----
	{
		D3D11_RASTERIZER_DESC d = {};
		d.FillMode = D3D11_FILL_SOLID;
		d.CullMode = D3D11_CULL_NONE;
		d.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&d, &m_raster))) return false;
	}

	// ---- サンプラー ----
	{
		D3D11_SAMPLER_DESC d = {};
		d.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		d.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		d.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;   // 長さ方向はタイルできるように
		d.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		d.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&d, &m_sampler))) return false;
	}

	return true;
}

//==============================================================================
// アセット
//==============================================================================
bool TrailRenderer::LoadAsset(const std::string& path, const std::string& key)
{
	std::ifstream f(path);
	if (!f) return false;

	nlohmann::json j;
	try { f >> j; }
	catch (...) { return false; }

	Asset a;
	if (!a.FromJson(j)) return false;
	a.name = key;  

	const TrailId id = MakeTrailId(key);

	auto it = m_idToIndex.find(id);
	if (it != m_idToIndex.end())
	{
		(*m_assets)[it->second] = a;  
		return true;
	}

	m_idToIndex[id] = static_cast<int>(m_assets->size());
	m_keyToId[key] = id;
	m_assets->push_back(a);
	return true;
}

int TrailRenderer::LoadAssetFolder(const std::string& folder, bool recursive)
{
	namespace fs = std::filesystem;

	std::error_code ec;
	fs::path base(folder);
	if (!fs::exists(base, ec) || !fs::is_directory(base, ec))
	{
		OutputDebugStringA(("[trail] not a directory: " + folder + "\n").c_str());
		return 0;
	}

	int loaded = 0;
	auto tryLoad = [&](const fs::path& p)
		{
			std::string s = p.string();
			if (s.size() < 11) return;
			if (s.substr(s.size() - 11) != ".trail.json") return;

			// sword.trail.json → sword
			std::string key = p.stem().string();
			size_t dot = key.find(".trail");
			if (dot != std::string::npos) key = key.substr(0, dot);

			// サブフォルダにあるなら相対パスを前置する
			std::error_code rc;
			auto rel = fs::relative(p.parent_path(), base, rc);
			if (!rc && !rel.empty() && rel != ".")
				key = rel.generic_string() + "/" + key;

			if (LoadAsset(s, key))
			{
				++loaded;
				OutputDebugStringA(("[trail] " + key + "\n").c_str());
			}
			else
			{
				OutputDebugStringA(("[trail] failed: " + s + "\n").c_str());
			}
		};

	if (recursive)
	{
		for (const auto& e : fs::recursive_directory_iterator(base, ec))
		{
			if (ec) break;
			if (e.is_regular_file(ec)) tryLoad(e.path());
		}
	}
	else
	{
		for (const auto& e : fs::directory_iterator(base, ec))
		{
			if (ec) break;
			if (e.is_regular_file(ec)) tryLoad(e.path());
		}
	}
	return loaded;
}

TrailId TrailRenderer::GetTrailId(const std::string& key) const
{
	return MakeTrailId(key);
}

void TrailRenderer::ApplyAssetFromString(const std::string& jsonStr)
{
	nlohmann::json j;
	try { j = nlohmann::json::parse(jsonStr); }
	catch (...) { return; }

	Asset a;
	if (!a.FromJson(j) || a.name.empty()) return;

	// 登録キーを決める。フォルダから読んだものは "サブフォルダ/名前" で登録されているので、
	//  1) エディタが送ってきた "key"（フォルダからの相対パス）
	//  2) 名前そのもの
	//  3) 末尾が "/名前" のキー
	// の順に探す
	std::string key;
	if (j.contains("key") && j["key"].is_string())
		key = j["key"].get<std::string>();
	if (key.empty() || m_keyToId.find(key) == m_keyToId.end())
	{
		if (m_keyToId.find(a.name) != m_keyToId.end())
		{
			key = a.name;
		}
		else
		{
			const std::string tail = "/" + a.name;
			for (const auto& kv : m_keyToId)
			{
				const std::string& k = kv.first;
				if (k.size() > tail.size() &&
					k.compare(k.size() - tail.size(), tail.size(), tail) == 0)
				{
					key = k;
					break;
				}
			}
		}
	}
	if (key.empty()) key = a.name;
	a.name = key;

	const TrailId id = MakeTrailId(key);

	auto it = m_idToIndex.find(id);
	if (it != m_idToIndex.end())
	{
		// 動いているトレイルは次のフレームから新しい見た目になる
		(*m_assets)[it->second] = a;
		OutputDebugStringA(("[trail] live updated: " + key + "\n").c_str());
		return;
	}

	// エディタで新規に作ったアセットも、その場で参照できる
	m_idToIndex[id] = static_cast<int>(m_assets->size());
	m_keyToId[key] = id;
	m_assets->push_back(a);
	OutputDebugStringA(("[trail] live added: " + key + "\n").c_str());
}

std::vector<std::string> TrailRenderer::GetAssetNames() const
{
	std::vector<std::string> out;
	out.reserve(m_keyToId.size());
	for (const auto& kv : m_keyToId) out.push_back(kv.first);
	return out;
}

const TrailRenderer::Asset* TrailRenderer::FindAsset(TrailId id) const
{
	if (!m_assets) return nullptr;

	auto it = m_idToIndex.find(id);
	if (it == m_idToIndex.end()) return nullptr;   // kInvalidTrailId もここで弾かれる

	return &(*m_assets)[it->second];
}

// ★面白い機能10：トレイル→パーティクル発生。RenderHelper::CollectRenderSystemから
//   新しい点を打った瞬間だけ呼ばれる想定（描画のたびに呼ぶものではない）
bool TrailRenderer::GetParticleSpawn(TrailId id, std::string& outEffect, float& outChance) const
{
	const Asset* a = FindAsset(id);
	if (!a || a->particleOn == 0 || a->particleEffect.empty()) return false;

	outEffect = a->particleEffect;
	outChance = a->particleChance;
	return true;
}
//==============================================================================
// 収集
//   リングバッファの展開もここで行うので、呼ぶ側はコンポーネントを渡すだけでよい
//==============================================================================
void TrailRenderer::AddTrail(const TrailComponent& t, const DirectX::XMFLOAT3& camPos,
	float time)
{
	int count = t.count;
	if (count < 2) return;

	const int N = TrailComponent::kMaxPoints;
	const Asset* a = FindAsset(t.trailId);

	// アセットが無いときは従来どおりの見た目
	const float edgeSoft = a ? a->edgeSoft : 0.6667f;   // 従来の edge*1.5 相当
	const float edgeCore = a ? a->edgeCore : 0.0f;
	const float texIndex = a ? (float)a->textureIndex : -1.0f;
	const float intensity = a ? a->intensity : 1.0f;
	const float uvScroll = a ? a->uvScroll * time : 0.0f;
	const int   uvMode = a ? a->uvMode : 0;
	const float uvTiling = a ? a->uvTiling : 1.0f;

	const float fadePow = a ? a->fadePow : 1.5f;
	const float coreWidth = a ? a->coreWidth : 0.35f;
	const float coreWhite = a ? a->coreWhite : 0.6f;
	const float coreBoost = a ? a->coreBoost : 4.0f;
	const float rootBoost = a ? a->rootBoost : 2.0f;
	const float depthFade = a ? a->depthFade : 0.5f;

	const float noiseStrength = a ? a->noiseStrength : 0.0f;
	const float noiseScale = a ? a->noiseScale : 0.5f;
	const float noiseSpeed = a ? a->noiseSpeed : 1.0f;
	const int   noiseOctaves = (a && a->noiseOctaves > 0) ? a->noiseOctaves : 1;

	const bool  branchOn = a && a->branchOn != 0;
	const float branchChance = a ? a->branchChance : 0.0f;
	const float branchLength = a ? a->branchLength : 0.0f;
	const float branchWidth = a ? a->branchWidth : 0.0f;
	const int   branchSegments = (a && a->branchSegments > 0) ? a->branchSegments : 1;

	// ★フェーズ3：色拡張（時間巡回・速度連動・経年劣化・温度）
	const float hueSpeed = a ? a->hueSpeed : 0.0f;
	const float speedColorAmount = a ? a->speedColorAmount : 0.0f;
	const float speedColorMax = (a && a->speedColorMax > 0.0001f) ? a->speedColorMax : 3.0f;
	const DirectX::XMFLOAT3 speedColorHot = a ? a->speedColorHot : DirectX::XMFLOAT3{ 1,1,1 };
	const float ageDesaturate = a ? a->ageDesaturate : 0.0f;
	const float tempAmount = a ? a->tempAmount : 0.0f;
	const DirectX::XMFLOAT3 tempHotColor = a ? a->tempHotColor : DirectX::XMFLOAT3{ 1, 0.9f, 0.6f };
	const DirectX::XMFLOAT3 tempCoolColor = a ? a->tempCoolColor : DirectX::XMFLOAT3{ 0.3f, 0.4f, 1 };

	// ★フェーズ3：パターン（パルス波・破線・疑似鏡面）
	const float pulseAmp = a ? a->pulseAmp : 0.0f;
	const float pulseSpeed = a ? a->pulseSpeed : 4.0f;
	const float pulseWidth = (a && a->pulseWidth > 0.001f) ? a->pulseWidth : 0.2f;
	const float dashDuty = (a && a->dashDuty < 0.999f) ? a->dashDuty : 1.0f;
	const float dashFreq = a ? a->dashFreq : 8.0f;
	const float specBoost = a ? a->specBoost : 0.0f;
	const float specWidth = (a && a->specWidth > 0.001f) ? a->specWidth : 0.08f;
	const float specSpeed = a ? a->specSpeed : 1.5f;

	// ★フェーズ3：特殊質感（セル状パターン・織り目はPS側。ここは電撃間引き・群衆複製）
	const float electricChance = a ? a->electricChance : 0.0f;
	const float electricScale = (a && a->electricScale > 0.0f) ? a->electricScale : 3.0f;
	const int   crowdCount = (a && a->crowdCount > 1) ? a->crowdCount : 1;
	const float crowdSpacing = a ? a->crowdSpacing : 0.0f;
	const float crowdJitter = a ? a->crowdJitter : 0.0f;

	// ★フェーズ3：PSでピクセル単位評価が必要なものだけ頂点(prm4〜prm7)経由で渡す
	const DirectX::XMFLOAT4 prm4 = {
		a ? a->uvNoiseStrength : 0.0f, a ? a->uvNoiseScale : 3.0f,
		a ? a->uvNoiseSpeed : 1.0f,    a ? a->dissolveAmount : 0.0f };
	const DirectX::XMFLOAT4 prm5 = {
		a ? a->dissolveNoiseScale : 4.0f, a ? a->dissolveEdgeWidth : 0.15f,
		a ? a->edgeNoiseStrength : 0.0f,  a ? a->edgeNoiseScale : 5.0f };
	const DirectX::XMFLOAT4 prm6 = {
		a ? a->cellAmount : 0.0f, a ? a->cellScale : 6.0f,
		a ? a->cellEdgeWidth : 0.08f, a ? a->cellGlow : 1.5f };
	const DirectX::XMFLOAT2 prm7 = { a ? a->weaveStrength : 0.0f, a ? a->weaveScale : 10.0f };

	std::vector<Vertex>& out = (a && a->additive == 0) ? m_vertsAlpha : m_vertsAdd;

	const float life = (t.lifeTime > 0.001f) ? t.lifeTime : 1.0f;

	// リングバッファを新しい順に展開する
	std::vector<DirectX::XMFLOAT3> pts(count), left(count), right(count);
	std::vector<DirectX::XMFLOAT4> cols(count);
	std::vector<float> dist(count, 0.0f);

	for (int i = 0; i < count; ++i)
	{
		const int idx = (t.head - 1 - i + N * 2) % N;

		const Math::Vector3& P = t.points[idx];
		const Math::Vector3& U = t.ups[idx];
		pts[i] = { P.x, P.y, P.z };

		// 帯に沿った位置。0 = 最新 1 = 最古
		const float u = (float)i / (float)(count - 1);
		const float wCurve = a ? a->EvalWidth(u) : 1.0f;

		DirectX::XMVECTOR p = DirectX::XMLoadFloat3(&pts[i]);

		// ★面白い機能2：ノイズ変位。中心線をCurlNoiseで揺らす（粒子CSのCurlStrength相当）
		if (noiseStrength != 0.0f)
		{
			const DirectX::XMFLOAT3 np = {
				pts[i].x * noiseScale,
				pts[i].y * noiseScale + time * noiseSpeed,
				pts[i].z * noiseScale };
			const DirectX::XMFLOAT3 curl = CurlNoiseFBM(np, noiseOctaves);
			p = DirectX::XMVectorAdd(p, DirectX::XMVectorScale(
				DirectX::XMLoadFloat3(&curl), noiseStrength));
		}

		DirectX::XMVECTOR half;

		if (t.widthMode == (int)TrailWidthMode::RootTip)
		{
			// ups は半幅ベクトル。正規化しない
			DirectX::XMFLOAT3 hv = { U.x, U.y, U.z };
			half = DirectX::XMVectorScale(DirectX::XMLoadFloat3(&hv),
				wCurve * t.widthScale);

			// ---- 潰れ対策 ----
			const int pa = (i > 0) ? i - 1 : i;
			const int pb = (i < count - 1) ? i + 1 : i;
			const int ia = (t.head - 1 - pa + N * 2) % N;
			const int ib = (t.head - 1 - pb + N * 2) % N;

			DirectX::XMFLOAT3 A = { t.points[ib].x, t.points[ib].y, t.points[ib].z };
			DirectX::XMFLOAT3 B = { t.points[ia].x, t.points[ia].y, t.points[ia].z };

			DirectX::XMVECTOR tangent = DirectX::XMVectorSubtract(
				DirectX::XMLoadFloat3(&A), DirectX::XMLoadFloat3(&B));

			if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(tangent)) > 1e-8f)
			{
				tangent = DirectX::XMVector3Normalize(tangent);

				DirectX::XMVECTOR toCam = DirectX::XMVector3Normalize(
					DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&camPos), p));

				// 帯の法線。カメラとの向きで「どれだけ潰れているか」が出る
				DirectX::XMVECTOR n = DirectX::XMVector3Cross(tangent, half);
				if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n)) > 1e-8f)
				{
					n = DirectX::XMVector3Normalize(n);

					// 1 = 正面から見えている / 0 = 完全に潰れている
					const float faceness = fabsf(
						DirectX::XMVectorGetX(DirectX::XMVector3Dot(n, toCam)));

					const float kMinFace = 0.45f;	// これ以下から補正を効かせる
					if (faceness < kMinFace)
					{
						// 進行方向を軸に、カメラを向く姿勢へ回した半幅
						DirectX::XMVECTOR bill = DirectX::XMVector3Cross(tangent, toCam);
						if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(bill)) > 1e-8f)
						{
							bill = DirectX::XMVector3Normalize(bill);

							// 元の長さを保つ。太さは変えず向きだけ寄せる
							const float len = DirectX::XMVectorGetX(
								DirectX::XMVector3Length(half));

							// 向きが反転しないよう、鋭角側へ合わせる
							if (DirectX::XMVectorGetX(DirectX::XMVector3Dot(bill, half)) < 0.0f)
								bill = DirectX::XMVectorNegate(bill);

							const float k = 1.0f - (faceness / kMinFace);
							half = DirectX::XMVectorLerp(half,
								DirectX::XMVectorScale(bill, len), k);
						}
					}
				}
			}
		}
		else if (t.widthMode == (int)TrailWidthMode::UpVector)
		{
			DirectX::XMFLOAT3 sv = { U.x, U.y, U.z };
			DirectX::XMVECTOR side = DirectX::XMLoadFloat3(&sv);
			if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(side)) < 1e-8f)
				side = DirectX::XMVectorSet(0, 1, 0, 0);
			side = DirectX::XMVector3Normalize(side);
			half = DirectX::XMVectorScale(side, t.width * wCurve * t.widthScale * 0.5f);
		}
		else
		{
			// Billboard：進行方向とカメラ方向の外積
			const int pa = (i > 0) ? i - 1 : i;
			const int pb = (i < count - 1) ? i + 1 : i;
			const int ia = (t.head - 1 - pa + N * 2) % N;
			const int ib = (t.head - 1 - pb + N * 2) % N;

			DirectX::XMFLOAT3 A = { t.points[ib].x, t.points[ib].y, t.points[ib].z };
			DirectX::XMFLOAT3 B = { t.points[ia].x, t.points[ia].y, t.points[ia].z };

			DirectX::XMVECTOR dir = DirectX::XMVectorSubtract(
				DirectX::XMLoadFloat3(&A), DirectX::XMLoadFloat3(&B));
			if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(dir)) < 1e-8f)
				dir = DirectX::XMVectorSet(0, 0, 1, 0);
			dir = DirectX::XMVector3Normalize(dir);

			DirectX::XMVECTOR toCam = DirectX::XMVectorSubtract(
				DirectX::XMLoadFloat3(&camPos), p);
			DirectX::XMVECTOR side = DirectX::XMVector3Cross(dir, toCam);
			if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(side)) < 1e-8f)
				side = DirectX::XMVectorSet(1, 0, 0, 0);
			side = DirectX::XMVector3Normalize(side);
			half = DirectX::XMVectorScale(side, t.width * wCurve * t.widthScale * 0.5f);
		}

		DirectX::XMStoreFloat3(&left[i], DirectX::XMVectorSubtract(p, half));
		DirectX::XMStoreFloat3(&right[i], DirectX::XMVectorAdd(p, half));

		// UV用の累積距離
		float spd = 0.0f;
		if (i > 0)
		{
			float dx = pts[i].x - pts[i - 1].x;
			float dy = pts[i].y - pts[i - 1].y;
			float dz = pts[i].z - pts[i - 1].z;
			spd = sqrtf(dx * dx + dy * dy + dz * dz);
			dist[i] = dist[i - 1] + spd;
		}

		// 色とアルファ
		float fade = 1.0f - (t.ages[idx] / life);
		if (fade < 0.0f) fade = 0.0f;

		if (a)
		{
			float rgb[3];
			// seed=i：帯に沿った点ごとにhue/sat/valジッターをずらす（ジッターが全て0なら従来と同じ）
			a->EvalColor(u, rgb, (float)i);

			
			if (hueSpeed != 0.0f || ageDesaturate > 0.0f)
			{
				float h, s, v;
				RGBtoHSV(rgb[0], rgb[1], rgb[2], h, s, v);
				if (hueSpeed != 0.0f) h += hueSpeed * time;            
				if (ageDesaturate > 0.0f) s *= (1.0f - ageDesaturate * (1.0f - fade)); 
				HSVtoRGB(h, s, v, rgb[0], rgb[1], rgb[2]);
			}
			if (speedColorAmount > 0.0f)
			{
				float k = spd / speedColorMax;
				if (k > 1.0f) k = 1.0f;
				k *= speedColorAmount;
				rgb[0] += (speedColorHot.x - rgb[0]) * k;
				rgb[1] += (speedColorHot.y - rgb[1]) * k;
				rgb[2] += (speedColorHot.z - rgb[2]) * k;
			}
			if (tempAmount > 0.0f)
			{
				const float kHot = tempAmount * (1.0f - u);
				const float kCool = tempAmount * u;
				rgb[0] += (tempHotColor.x - rgb[0]) * kHot + (tempCoolColor.x - rgb[0]) * kCool;
				rgb[1] += (tempHotColor.y - rgb[1]) * kHot + (tempCoolColor.y - rgb[1]) * kCool;
				rgb[2] += (tempHotColor.z - rgb[2]) * kHot + (tempCoolColor.z - rgb[2]) * kCool;
			}

			float boost = 1.0f;
			if (pulseAmp > 0.0f)
			{
				float travel = time * pulseSpeed;
				travel -= floorf(travel);
				float d = u - travel;
				d -= floorf(d + 0.5f);   // -0.5〜0.5に巻き戻す
				boost += pulseAmp * expf(-(d * d) / (2.0f * pulseWidth * pulseWidth));
			}
			if (specBoost > 0.0f)
			{
				float travel2 = time * specSpeed;
				travel2 -= floorf(travel2);
				float d2 = u - travel2;
				d2 -= floorf(d2 + 0.5f);
				boost += specBoost * expf(-(d2 * d2) / (2.0f * specWidth * specWidth));
			}

			float alphaMul = 1.0f;
			if (dashDuty < 0.999f)
			{
				float phase = dist[i] * dashFreq;
				phase -= floorf(phase);
				if (phase >= dashDuty) alphaMul = 0.0f;
			}

			cols[i] = { rgb[0] * intensity * boost, rgb[1] * intensity * boost, rgb[2] * intensity * boost,
						t.color.w * fade * a->EvalAlpha(u) * alphaMul };
		}
		else
		{
			cols[i] = { t.color.x, t.color.y, t.color.z, t.color.w * fade };
		}
	}

	// 見た目パラメータはトレイルごとに1組（分岐は prm4〜7 を使わないので別の組）
	const DirectX::XMFLOAT4 prm = { edgeSoft, edgeCore, texIndex, 0.0f };
	const DirectX::XMFLOAT4 prm2 = { fadePow, coreWidth, coreWhite, coreBoost };
	const DirectX::XMFLOAT4 prm3 = { rootBoost, depthFade, 0.0f, 0.0f };

	if ((int)m_params.size() + 2 > kMaxParamSets)
	{
		if (m_droppedVerts == 0)
			OutputDebugStringA("[trail] param sets full. trails dropped\n");
		++m_droppedVerts;
		return;
	}
	const uint32_t ribbonParam = (uint32_t)m_params.size();
	m_params.push_back({ prm, prm2, prm3, prm4, prm5, prm6,
		DirectX::XMFLOAT4{ prm7.x, prm7.y, 0.0f, 0.0f } });
	const uint32_t branchParam = (uint32_t)m_params.size();
	m_params.push_back({ prm, prm2, prm3, {}, {}, {}, {} });

	//   分岐トレイル

	if (branchOn && branchChance > 0.0f && branchLength > 0.0f && branchWidth > 0.0f)
	{
		const float trailSeed = (float)(t.trailId % 100000) * 0.0131f + floorf(time * 6.0f) * 0.913f;

		for (int i = 0; i < count - 1; ++i)
		{
			const float roll = HashSeed((float)i * 12.37f + trailSeed);
			if (roll >= branchChance) continue;

			const float segLen = branchLength / (float)branchSegments;
			DirectX::XMVECTOR cur = DirectX::XMLoadFloat3(&pts[i]);
			const DirectX::XMFLOAT4 bcol = cols[i];

			for (int s = 0; s < branchSegments; ++s)
			{
				const float dseed = trailSeed + (float)i * 5.11f + (float)s * 2.73f;
				const DirectX::XMFLOAT3 dirF = RandomDir(dseed);
				const DirectX::XMVECTOR dir = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&dirF));
				const DirectX::XMVECTOR next = DirectX::XMVectorAdd(cur, DirectX::XMVectorScale(dir, segLen));

				const float w0 = branchWidth * (1.0f - (float)s / (float)branchSegments);
				const float w1 = branchWidth * (1.0f - (float)(s + 1) / (float)branchSegments);
				const float a0 = 1.0f - (float)s / (float)branchSegments;
				const float a1 = 1.0f - (float)(s + 1) / (float)branchSegments;

				DirectX::XMVECTOR toCam = DirectX::XMVector3Normalize(
					DirectX::XMVectorSubtract(DirectX::XMLoadFloat3(&camPos), cur));
				DirectX::XMVECTOR segDir = DirectX::XMVector3Normalize(
					DirectX::XMVectorSubtract(next, cur));
				DirectX::XMVECTOR side = DirectX::XMVector3Cross(segDir, toCam);
				if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(side)) < 1e-8f)
					side = DirectX::XMVectorSet(1, 0, 0, 0);
				side = DirectX::XMVector3Normalize(side);

				const DirectX::XMVECTOR h0 = DirectX::XMVectorScale(side, w0 * 0.5f);
				const DirectX::XMVECTOR h1 = DirectX::XMVectorScale(side, w1 * 0.5f);

				DirectX::XMFLOAT3 l0p, r0p, l1p, r1p;
				DirectX::XMStoreFloat3(&l0p, DirectX::XMVectorSubtract(cur, h0));
				DirectX::XMStoreFloat3(&r0p, DirectX::XMVectorAdd(cur, h0));
				DirectX::XMStoreFloat3(&l1p, DirectX::XMVectorSubtract(next, h1));
				DirectX::XMStoreFloat3(&r1p, DirectX::XMVectorAdd(next, h1));

				const DirectX::XMFLOAT4 col0 = { bcol.x, bcol.y, bcol.z, bcol.w * a0 };
				const DirectX::XMFLOAT4 col1 = { bcol.x, bcol.y, bcol.z, bcol.w * a1 };

				Vertex bl0{ l0p, { 0.0f, 0.0f }, col0, branchParam };
				Vertex br0{ r0p, { 0.0f, 1.0f }, col0, branchParam };
				Vertex bl1{ l1p, { 1.0f, 0.0f }, col1, branchParam };
				Vertex br1{ r1p, { 1.0f, 1.0f }, col1, branchParam };

				out.push_back(bl0); out.push_back(br0); out.push_back(bl1);
				out.push_back(br0); out.push_back(br1); out.push_back(bl1);

				cur = next;
			}
		}
	}

	// ---- 細分化 ----
	const int smoothing = a ? a->smoothing : 0;
	if (smoothing > 0 && count >= 3)
	{
		const int sub = smoothing + 1;	

		// 端点は自分を折り返して使う
		auto at = [&](const std::vector<DirectX::XMFLOAT3>& v, int i)
			{
				const int k = (i < 0) ? 0 : (i >= count ? count - 1 : i);
				return DirectX::XMLoadFloat3(&v[k]);
			};

		std::vector<DirectX::XMFLOAT3> nl, nr;
		std::vector<DirectX::XMFLOAT4> nc;
		nl.reserve(count * sub); nr.reserve(count * sub); nc.reserve(count * sub);

		for (int i = 0; i < count - 1; ++i)
		{
			const int steps = (i == count - 2) ? sub + 1 : sub;	// 末尾だけ終点も出す
			for (int s = 0; s < steps; ++s)
			{
				const float u = static_cast<float>(s) / static_cast<float>(sub);

				DirectX::XMFLOAT3 pl, pr;
				DirectX::XMStoreFloat3(&pl, DirectX::XMVectorCatmullRom(
					at(left, i - 1), at(left, i), at(left, i + 1), at(left, i + 2), u));
				DirectX::XMStoreFloat3(&pr, DirectX::XMVectorCatmullRom(
					at(right, i - 1), at(right, i), at(right, i + 1), at(right, i + 2), u));

				nl.push_back(pl);
				nr.push_back(pr);

				// 色は曲線にする必要が無いので線形で十分
				DirectX::XMFLOAT4 c;
				DirectX::XMStoreFloat4(&c, DirectX::XMVectorLerp(
					DirectX::XMLoadFloat4(&cols[i]),
					DirectX::XMLoadFloat4(&cols[i + 1]),
					u));
				nc.push_back(c);
			}
		}

		left.swap(nl);
		right.swap(nr);
		cols.swap(nc);
		count = static_cast<int>(left.size());

		// UV用の累積距離を引き直す
		dist.assign(count, 0.0f);
		for (int i = 1; i < count; ++i)
		{
			const float ax = (left[i].x + right[i].x) * 0.5f - (left[i - 1].x + right[i - 1].x) * 0.5f;
			const float ay = (left[i].y + right[i].y) * 0.5f - (left[i - 1].y + right[i - 1].y) * 0.5f;
			const float az = (left[i].z + right[i].z) * 0.5f - (left[i - 1].z + right[i - 1].z) * 0.5f;
			dist[i] = dist[i - 1] + sqrtf(ax * ax + ay * ay + az * az) * 0.5f;
		}
	}

	const float total = (dist[count - 1] > 1e-5f) ? dist[count - 1] : 1.0f;

	auto uOf = [&](int i)
		{
			if (uvMode == 0) return (float)i / (float)(count - 1) + uvScroll;
			return dist[i] / total * uvTiling + uvScroll;
		};

	const float trailSeed = (float)(t.trailId % 100000) * 0.0271f;
	auto emitRibbon = [&](float lateralOffset, float seedOfs)
		{
			for (int i = 0; i < count - 1; ++i)
			{
				if (electricChance > 0.0f)
				{
					const float roll = HashSeed((float)i * electricScale + trailSeed + seedOfs
						+ floorf(time * 8.0f) * 0.61f);
					if (roll < electricChance) continue;
				}

				const float u0 = uOf(i);
				const float u1 = uOf(i + 1);

				DirectX::XMFLOAT3 lp0 = left[i], rp0 = right[i];
				DirectX::XMFLOAT3 lp1 = left[i + 1], rp1 = right[i + 1];

				if (lateralOffset != 0.0f)
				{
					DirectX::XMVECTOR side0 = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(
						DirectX::XMLoadFloat3(&right[i]), DirectX::XMLoadFloat3(&left[i])));
					DirectX::XMVECTOR off0 = DirectX::XMVectorScale(side0, lateralOffset);
					DirectX::XMStoreFloat3(&lp0, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&left[i]), off0));
					DirectX::XMStoreFloat3(&rp0, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&right[i]), off0));

					DirectX::XMVECTOR side1 = DirectX::XMVector3Normalize(DirectX::XMVectorSubtract(
						DirectX::XMLoadFloat3(&right[i + 1]), DirectX::XMLoadFloat3(&left[i + 1])));
					DirectX::XMVECTOR off1 = DirectX::XMVectorScale(side1, lateralOffset);
					DirectX::XMStoreFloat3(&lp1, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&left[i + 1]), off1));
					DirectX::XMStoreFloat3(&rp1, DirectX::XMVectorAdd(DirectX::XMLoadFloat3(&right[i + 1]), off1));
				}

				Vertex l0{ lp0, { u0, 0.0f }, cols[i],     ribbonParam };
				Vertex r0{ rp0, { u0, 1.0f }, cols[i],     ribbonParam };
				Vertex l1{ lp1, { u1, 0.0f }, cols[i + 1], ribbonParam };
				Vertex r1{ rp1, { u1, 1.0f }, cols[i + 1], ribbonParam };

				out.push_back(l0); out.push_back(r0); out.push_back(l1);
				out.push_back(r0); out.push_back(r1); out.push_back(l1);
			}
		};

	if (crowdCount <= 1)
	{
		emitRibbon(0.0f, 0.0f);
	}
	else
	{
		const float center = (float)(crowdCount - 1) * 0.5f;
		for (int k = 0; k < crowdCount; ++k)
		{
			float off = ((float)k - center) * crowdSpacing;
			if (crowdJitter > 0.0f)
				off += (HashSeed(trailSeed + (float)k * 3.71f) - 0.5f) * crowdJitter;
			emitRibbon(off, (float)k * 17.23f);
		}
	}
}

//==============================================================================
// 描画
//==============================================================================
void TrailRenderer::DrawList(ID3D11DeviceContext* ctx, const std::vector<Vertex>& verts,
	ID3D11BlendState* blend)
{
	if (verts.empty()) return;

	int vertCount = (int)verts.size();
	if (vertCount > m_maxVerts)
	{
		if (m_droppedVerts == 0)
			OutputDebugStringA("[trail] vertex buffer full. trail vertices dropped\n");
		m_droppedVerts += vertCount - m_maxVerts;
		vertCount = m_maxVerts - (m_maxVerts % 3);   // 三角形の途中で切らない
	}

	D3D11_MAPPED_SUBRESOURCE mapped;
	if (FAILED(ctx->Map(m_vb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
	memcpy(mapped.pData, verts.data(), sizeof(Vertex) * vertCount);
	ctx->Unmap(m_vb.Get(), 0);

	float bf[4] = { 0, 0, 0, 0 };
	ctx->OMSetBlendState(blend, bf, 0xFFFFFFFF);

	UINT stride = sizeof(Vertex), offset = 0;
	ctx->IASetVertexBuffers(0, 1, m_vb.GetAddressOf(), &stride, &offset);
	ctx->Draw(vertCount, 0);
}

void TrailRenderer::Render(ID3D11RenderTargetView* rtv,
	const DirectX::XMMATRIX& viewProj,
	int width, int height,
	ID3D11ShaderResourceView* gbufferC,
	const DirectX::XMFLOAT3& camPos)
{
	if (!rtv) return;
	if (m_vertsAdd.empty() && m_vertsAlpha.empty()) return;

	ID3D11DeviceContext* ctx = KdDirect3D::Instance().WorkDevContext();

	// 定数バッファ
	TrailCB cb = {};
	DirectX::XMStoreFloat4x4(&cb.viewProj, viewProj);
	cb.camPos = camPos;
	cb.time = Time::Instance().GetTime();
	ctx->UpdateSubresource(m_cb.Get(), 0, nullptr, &cb, 0, 0);

	// ---- 退避 ----
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	ID3D11BlendState* oldBlend = nullptr;
	float oldBF[4] = {}; UINT oldMask = 0;
	ctx->OMGetBlendState(&oldBlend, oldBF, &oldMask);
	ID3D11RasterizerState* oldRS = nullptr;
	ctx->RSGetState(&oldRS);
	ID3D11DepthStencilState* oldDS = nullptr;
	UINT oldRef = 0;
	ctx->OMGetDepthStencilState(&oldDS, &oldRef);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	// ---- 設定 ----
	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	D3D11_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
	ctx->RSSetViewports(1, &vp);
	ctx->RSSetState(m_raster.Get());
	ctx->OMSetDepthStencilState(m_depthState.Get(), 0);

	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetInputLayout(m_layout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->PSSetShader(m_ps.Get(), nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);

	ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

	// 見た目パラメータ（VS の t0）
	if (!m_params.empty())
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(ctx->Map(m_paramBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, m_params.data(), sizeof(ParamSet) * m_params.size());
			ctx->Unmap(m_paramBuffer.Get(), 0);
		}
	}
	ctx->VSSetShaderResources(0, 1, m_paramSRV.GetAddressOf());

	// t0 = GBufferC、t1〜t8 = トレイル用テクスチャ
	ID3D11ShaderResourceView* srvs[1 + kMaxTextures] = {};
	srvs[0] = gbufferC;
	for (int i = 0; i < kMaxTextures; ++i) srvs[1 + i] = m_textureSlots[i];
	ctx->PSSetShaderResources(0, 1 + kMaxTextures, srvs);
	ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

	// ---- 描画（最大2ドローコール）----
	DrawList(ctx, m_vertsAlpha, m_alphaBlend.Get());
	DrawList(ctx, m_vertsAdd, m_additive.Get());

	// ---- 後始末 ----
	ID3D11ShaderResourceView* nullSRVs[1 + kMaxTextures] = {};
	ctx->PSSetShaderResources(0, 1 + kMaxTextures, nullSRVs);
	ctx->VSSetShaderResources(0, 1, nullSRVs);

	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) ctx->RSSetViewports(1, &oldVP);
	ctx->OMSetBlendState(oldBlend, oldBF, oldMask);
	ctx->RSSetState(oldRS);
	ctx->OMSetDepthStencilState(oldDS, oldRef);
	if (oldRTV)   oldRTV->Release();
	if (oldDSV)   oldDSV->Release();
	if (oldBlend) oldBlend->Release();
	if (oldRS)    oldRS->Release();
	if (oldDS)    oldDS->Release();
}