#include "TrailAsset.h"

//------------------------------------------------------------------------------
// カーブ評価（可変点数対応）
//------------------------------------------------------------------------------
namespace
{
	// 線形補間（従来の EvalCurve8 を可変点数化したもの）
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

	// ---- CPU側 RGB<->HSV（hueJitter等の色ずらし用。HLSL側の rgb2hsv/hsv2rgb と同じ式）----
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

		// k(0〜5) → (r,g,b) を {v,t,p,q} のどれにするか、をテーブルで引く
		// index: 0=v 1=t 2=p 3=q
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
}

float TrailAsset::EvalWidth(float t) const
{
	float v = (curveInterp != 0)
		? EvalCurveSmooth(widthCurve, curvePointCount, t)
		: EvalCurveLinear(widthCurve, curvePointCount, t);
	return v * widthScale;
}

float TrailAsset::EvalAlpha(float t) const
{
	return (curveInterp != 0)
		? EvalCurveSmooth(alphaCurve, curvePointCount, t)
		: EvalCurveLinear(alphaCurve, curvePointCount, t);
}

void TrailAsset::EvalColor(float t, float outRGB[3], float seed) const
{
	int n = colorKeyCount;
	if (n < 2) n = 2;
	if (n > 4) n = 4;
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	float rgb[3] = { colorKeys[0], colorKeys[1], colorKeys[2] };

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

		rgb[0] = k0[0] + (k1[0] - k0[0]) * f;
		rgb[1] = k0[1] + (k1[1] - k0[1]) * f;
		rgb[2] = k0[2] + (k1[2] - k0[2]) * f;
	}

	if (hueJitter > 0.0001f || satJitter > 0.0001f || valJitter > 0.0001f)
	{
		float h, s, v;
		RGBtoHSV(rgb[0], rgb[1], rgb[2], h, s, v);

		h += (HashSeed(seed * 7.31f) - 0.5f) * hueJitter;
		s += (HashSeed(seed * 13.77f) - 0.5f) * satJitter;
		v += (HashSeed(seed * 19.13f) - 0.5f) * valJitter;
		if (s < 0.0f) s = 0.0f; if (s > 1.0f) s = 1.0f;
		if (v < 0.0f) v = 0.0f; if (v > 1.0f) v = 1.0f;

		HSVtoRGB(h, s, v, rgb[0], rgb[1], rgb[2]);
	}

	outRGB[0] = rgb[0]; outRGB[1] = rgb[1]; outRGB[2] = rgb[2];
}

//------------------------------------------------------------------------------
bool TrailAsset::LoadFromString(const std::string& jsonStr)
{
	try
	{
		nlohmann::json j = nlohmann::json::parse(jsonStr);
		name = j.value("name", name);

		if (j.contains("params"))
		{
			const auto& p = j["params"];

			lifetime = p.value("lifetime", lifetime);
			minDistance = p.value("minDistance", minDistance);
			maxNodes = p.value("maxNodes", maxNodes);
			smoothing = p.value("smoothing", smoothing);

			widthScale = p.value("widthScale", widthScale);
			intensity = p.value("intensity", intensity);
			additive = p.value("additive", additive);
			textureIndex = p.value("textureIndex", textureIndex);
			edgeSoft = p.value("edgeSoft", edgeSoft);
			edgeCore = p.value("edgeCore", edgeCore);
			uvMode = p.value("uvMode", uvMode);
			uvTiling = p.value("uvTiling", uvTiling);
			uvScroll = p.value("uvScroll", uvScroll);
			colorKeyCount = p.value("colorKeyCount", colorKeyCount);

			// ---- カーブ点数・補間モード ----
			curvePointCount = p.value("curvePointCount", curvePointCount);
			curveInterp = p.value("curveInterp", curveInterp);

			// ---- 色のばらつき ----
			hueJitter = p.value("hueJitter", hueJitter);
			satJitter = p.value("satJitter", satJitter);
			valJitter = p.value("valJitter", valJitter);

			// ---- 質感（旧: シェーダ内固定値）----
			fadePow = p.value("fadePow", fadePow);
			coreWidth = p.value("coreWidth", coreWidth);
			coreWhite = p.value("coreWhite", coreWhite);
			coreBoost = p.value("coreBoost", coreBoost);
			rootBoost = p.value("rootBoost", rootBoost);
			depthFade = p.value("depthFade", depthFade);

			// ---- ノイズ変位 ----
			noiseStrength = p.value("noiseStrength", noiseStrength);
			noiseScale = p.value("noiseScale", noiseScale);
			noiseSpeed = p.value("noiseSpeed", noiseSpeed);

			// ---- 分岐トレイル ----
			branchOn = p.value("branchOn", branchOn);
			branchChance = p.value("branchChance", branchChance);
			branchLength = p.value("branchLength", branchLength);
			branchWidth = p.value("branchWidth", branchWidth);
			branchSegments = p.value("branchSegments", branchSegments);

			// ---- トレイル→パーティクル発生 ----
			particleOn = p.value("particleOn", particleOn);
			particleEffect = p.value("particleEffect", particleEffect);
			particleChance = p.value("particleChance", particleChance);

			// ---- ノイズ拡張 ----
			noiseOctaves = p.value("noiseOctaves", noiseOctaves);
			uvNoiseStrength = p.value("uvNoiseStrength", uvNoiseStrength);
			uvNoiseScale = p.value("uvNoiseScale", uvNoiseScale);
			uvNoiseSpeed = p.value("uvNoiseSpeed", uvNoiseSpeed);
			dissolveAmount = p.value("dissolveAmount", dissolveAmount);
			dissolveNoiseScale = p.value("dissolveNoiseScale", dissolveNoiseScale);
			dissolveEdgeWidth = p.value("dissolveEdgeWidth", dissolveEdgeWidth);
			edgeNoiseStrength = p.value("edgeNoiseStrength", edgeNoiseStrength);
			edgeNoiseScale = p.value("edgeNoiseScale", edgeNoiseScale);

			// ---- 色拡張 ----
			hueSpeed = p.value("hueSpeed", hueSpeed);
			speedColorAmount = p.value("speedColorAmount", speedColorAmount);
			speedColorMax = p.value("speedColorMax", speedColorMax);
			ageDesaturate = p.value("ageDesaturate", ageDesaturate);
			tempAmount = p.value("tempAmount", tempAmount);

			// ---- パターン ----
			pulseAmp = p.value("pulseAmp", pulseAmp);
			pulseSpeed = p.value("pulseSpeed", pulseSpeed);
			pulseWidth = p.value("pulseWidth", pulseWidth);
			dashDuty = p.value("dashDuty", dashDuty);
			dashFreq = p.value("dashFreq", dashFreq);
			specBoost = p.value("specBoost", specBoost);
			specWidth = p.value("specWidth", specWidth);
			specSpeed = p.value("specSpeed", specSpeed);

			// ---- 特殊質感 ----
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

			// 壊れた値の矯正
			if (lifetime < 0.02f) lifetime = 0.02f;
			if (minDistance < 0.0f) minDistance = 0.0f;
			if (maxNodes < 4) maxNodes = 4;
			if (maxNodes > 256) maxNodes = 256;
			if (smoothing < 0) smoothing = 0;
			if (smoothing > 3) smoothing = 3;
			if (widthScale <= 0.0f) widthScale = 1.0f;
			if (uvTiling <= 0.0f) uvTiling = 1.0f;
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
		}
		return true;
	}
	catch (...) { return false; }
}

bool TrailAsset::LoadFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::stringstream ss; ss << f.rdbuf();
	return LoadFromString(ss.str());
}

std::string TrailAsset::ToJsonString() const
{
	nlohmann::json j;
	j["version"] = 2;   
	j["name"] = name;

	auto& p = j["params"];
	p["lifetime"] = lifetime;
	p["minDistance"] = minDistance;
	p["maxNodes"] = maxNodes;
	p["smoothing"] = smoothing;

	p["widthScale"] = widthScale;
	p["intensity"] = intensity;
	p["additive"] = additive;
	p["textureIndex"] = textureIndex;
	p["edgeSoft"] = edgeSoft;
	p["edgeCore"] = edgeCore;
	p["uvMode"] = uvMode;
	p["uvTiling"] = uvTiling;
	p["uvScroll"] = uvScroll;
	p["colorKeyCount"] = colorKeyCount;

	p["curvePointCount"] = curvePointCount;
	p["curveInterp"] = curveInterp;

	p["hueJitter"] = hueJitter;
	p["satJitter"] = satJitter;
	p["valJitter"] = valJitter;

	p["fadePow"] = fadePow;
	p["coreWidth"] = coreWidth;
	p["coreWhite"] = coreWhite;
	p["coreBoost"] = coreBoost;
	p["rootBoost"] = rootBoost;
	p["depthFade"] = depthFade;

	p["noiseStrength"] = noiseStrength;
	p["noiseScale"] = noiseScale;
	p["noiseSpeed"] = noiseSpeed;

	p["branchOn"] = branchOn;
	p["branchChance"] = branchChance;
	p["branchLength"] = branchLength;
	p["branchWidth"] = branchWidth;
	p["branchSegments"] = branchSegments;

	p["particleOn"] = particleOn;
	p["particleEffect"] = particleEffect;
	p["particleChance"] = particleChance;

	p["noiseOctaves"] = noiseOctaves;
	p["uvNoiseStrength"] = uvNoiseStrength;
	p["uvNoiseScale"] = uvNoiseScale;
	p["uvNoiseSpeed"] = uvNoiseSpeed;
	p["dissolveAmount"] = dissolveAmount;
	p["dissolveNoiseScale"] = dissolveNoiseScale;
	p["dissolveEdgeWidth"] = dissolveEdgeWidth;
	p["edgeNoiseStrength"] = edgeNoiseStrength;
	p["edgeNoiseScale"] = edgeNoiseScale;

	p["hueSpeed"] = hueSpeed;
	p["speedColorAmount"] = speedColorAmount;
	p["speedColorMax"] = speedColorMax;
	p["speedColorHot"] = { speedColorHot.x, speedColorHot.y, speedColorHot.z };
	p["ageDesaturate"] = ageDesaturate;
	p["tempAmount"] = tempAmount;
	p["tempHotColor"] = { tempHotColor.x, tempHotColor.y, tempHotColor.z };
	p["tempCoolColor"] = { tempCoolColor.x, tempCoolColor.y, tempCoolColor.z };

	p["pulseAmp"] = pulseAmp;
	p["pulseSpeed"] = pulseSpeed;
	p["pulseWidth"] = pulseWidth;
	p["dashDuty"] = dashDuty;
	p["dashFreq"] = dashFreq;
	p["specBoost"] = specBoost;
	p["specWidth"] = specWidth;
	p["specSpeed"] = specSpeed;

	p["cellAmount"] = cellAmount;
	p["cellScale"] = cellScale;
	p["cellEdgeWidth"] = cellEdgeWidth;
	p["cellGlow"] = cellGlow;
	p["weaveStrength"] = weaveStrength;
	p["weaveScale"] = weaveScale;
	p["electricChance"] = electricChance;
	p["electricScale"] = electricScale;
	p["crowdCount"] = crowdCount;
	p["crowdSpacing"] = crowdSpacing;
	p["crowdJitter"] = crowdJitter;

	p["widthCurve"] = nlohmann::json::array();
	for (int i = 0; i < kMaxCurvePoints; ++i) p["widthCurve"].push_back(widthCurve[i]);
	p["alphaCurve"] = nlohmann::json::array();
	for (int i = 0; i < kMaxCurvePoints; ++i) p["alphaCurve"].push_back(alphaCurve[i]);
	p["colorKeys"] = nlohmann::json::array();
	for (int i = 0; i < 16; ++i) p["colorKeys"].push_back(colorKeys[i]);

	return j.dump(2);
}

//------------------------------------------------------------------------------
// カーブプリセット
//------------------------------------------------------------------------------
const TrailCurvePreset kTrailCurvePresets[] =
{
	{
		"Default",
		{ 1.0f, 1.0f, 0.95f, 0.85f, 0.7f, 0.5f, 0.3f, 0.0f },
		{ 1.0f, 1.0f, 0.9f, 0.75f, 0.55f, 0.35f, 0.15f, 0.0f },
	},
	{
		"Taper (先細り)",
		{ 1.0f, 0.8f, 0.6f, 0.45f, 0.32f, 0.2f, 0.1f, 0.0f },
		{ 1.0f, 0.9f, 0.75f, 0.6f, 0.45f, 0.3f, 0.15f, 0.0f },
	},
	{
		"Constant (一定幅)",
		{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f, 0.8f, 0.5f, 0.25f, 0.0f },
	},
	{
		"Bulge (膨らんで消える)",
		{ 0.3f, 0.6f, 0.9f, 1.0f, 0.9f, 0.6f, 0.3f, 0.0f },
		{ 0.5f, 0.8f, 1.0f, 1.0f, 0.8f, 0.5f, 0.2f, 0.0f },
	},
	{
		"Comet (彗星)",
		{ 1.0f, 0.7f, 0.5f, 0.35f, 0.22f, 0.12f, 0.05f, 0.0f },
		{ 1.0f, 0.85f, 0.65f, 0.45f, 0.28f, 0.15f, 0.06f, 0.0f },
	},
	{
		"Flicker (電撃)",
		{ 1.0f, 0.5f, 0.9f, 0.4f, 0.8f, 0.3f, 0.6f, 0.0f },
		{ 1.0f, 0.6f, 0.9f, 0.5f, 0.8f, 0.4f, 0.6f, 0.0f },
	},
};

const int kTrailCurvePresetCount =
(int)(sizeof(kTrailCurvePresets) / sizeof(kTrailCurvePresets[0]));

void ApplyTrailCurvePreset(TrailAsset& asset, int presetIndex)
{
	if (presetIndex < 0 || presetIndex >= kTrailCurvePresetCount) return;

	const TrailCurvePreset& preset = kTrailCurvePresets[presetIndex];
	for (int i = 0; i < 8; ++i)
	{
		asset.widthCurve[i] = preset.width[i];
		asset.alphaCurve[i] = preset.alpha[i];
	}
	for (int i = 8; i < TrailAsset::kMaxCurvePoints; ++i)
	{
		asset.widthCurve[i] = 0.0f;
		asset.alphaCurve[i] = 0.0f;
	}
	asset.curvePointCount = 8;
}