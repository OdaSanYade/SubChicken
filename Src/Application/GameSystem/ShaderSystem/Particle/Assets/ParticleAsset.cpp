#include "ParticleAsset.h"

bool ParticleAsset::LoadFromString(const std::string& jsonStr)
{
	try
	{
		nlohmann::json j = nlohmann::json::parse(jsonStr);

		name = j.value("name", name);

		// texture ブロック
		if (j.contains("texture"))
		{
			auto& t = j["texture"];
			textureType = t.value("type", textureType);
			effectName = t.value("effectName", effectName);
			imagePath = t.value("imagePath", imagePath);
		}

		// params ブロック
		if (j.contains("params"))
		{
			auto& p = j["params"];

			// ---- 基本 ----
			gravity = p.value("gravity", gravity);
			initialSpeed = p.value("initialSpeed", initialSpeed);
			lifeMin = p.value("lifeMin", lifeMin);
			lifeMax = p.value("lifeMax", lifeMax);
			spread = p.value("spread", spread);
			size = p.value("size", size);
			colorR = p.value("colorR", colorR);
			colorG = p.value("colorG", colorG);
			colorB = p.value("colorB", colorB);
			activeCount = p.value("activeCount", activeCount);

			// ---- 動き ----
			cUpward = p.value("cUpward", cUpward);
			cRadial = p.value("cRadial", cRadial);
			cTangent = p.value("cTangent", cTangent);
			cSwirl = p.value("cSwirl", cSwirl);
			cSphere = p.value("cSphere", cSphere);
			cDrag = p.value("cDrag", cDrag);
			cTurbulence = p.value("cTurbulence", cTurbulence);
			cSizeGrow = p.value("cSizeGrow", cSizeGrow);
			cSeek = p.value("cSeek", cSeek);
			cWave = p.value("cWave", cWave);
			cSpiral = p.value("cSpiral", cSpiral);
			cConverge = p.value("cConverge", cConverge);
			targetX = p.value("targetX", targetX);
			targetY = p.value("targetY", targetY);
			targetZ = p.value("targetZ", targetZ);

			sizeBegin = p.value("sizeBegin", sizeBegin);
			sizeMid = p.value("sizeMid", sizeMid);
			sizeEnd = p.value("sizeEnd", sizeEnd);
			speedRandom = p.value("speedRandom", speedRandom);
			fadeIn = p.value("fadeIn", fadeIn);
			fadeOut = p.value("fadeOut", fadeOut);
			rotSpeedMin = p.value("rotSpeedMin", rotSpeedMin);
			rotSpeedMax = p.value("rotSpeedMax", rotSpeedMax);
			curlStrength = p.value("curlStrength", curlStrength);
			curlScale = p.value("curlScale", curlScale);
			curlSpeed = p.value("curlSpeed", curlSpeed);
			velColorScale = p.value("velColorScale", velColorScale);
			velSizeScale = p.value("velSizeScale", velSizeScale);

			sizeCurveOn = p.value("sizeCurveOn", sizeCurveOn);
			if (p.contains("sizeCurve") && p["sizeCurve"].is_array())
			{
				const auto& arr = p["sizeCurve"];
				for (size_t i = 0; i < arr.size() && i < (size_t)kMaxCurvePoints; ++i)
					sizeCurve[i] = arr[i].get<float>();
			}
			sizeCurvePointCount = p.value("sizeCurvePointCount", sizeCurvePointCount);
			sizeCurveInterp = p.value("sizeCurveInterp", sizeCurveInterp);
			alphaCurveOn = p.value("alphaCurveOn", alphaCurveOn);
			if (p.contains("alphaCurve") && p["alphaCurve"].is_array())
			{
				const auto& arr = p["alphaCurve"];
				for (size_t i = 0; i < arr.size() && i < (size_t)kMaxCurvePoints; ++i)
					alphaCurve[i] = arr[i].get<float>();
			}
			alphaCurvePointCount = p.value("alphaCurvePointCount", alphaCurvePointCount);
			alphaCurveInterp = p.value("alphaCurveInterp", alphaCurveInterp);

			stretchOn = p.value("stretchOn", stretchOn);
			stretchScale = p.value("stretchScale", stretchScale);
			shapeType = p.value("shapeType", shapeType);
			shapeRadius = p.value("shapeRadius", shapeRadius);
			shapeHeight = p.value("shapeHeight", shapeHeight);
			shapeSurfaceOnly = p.value("shapeSurfaceOnly", shapeSurfaceOnly);

			formOn = p.value("formOn", formOn);
			formSides = p.value("formSides", formSides);
			formRound = p.value("formRound", formRound);
			formSpike = p.value("formSpike", formSpike);
			formAspect = p.value("formAspect", formAspect);
			formTwist = p.value("formTwist", formTwist);
			formEdge = p.value("formEdge", formEdge);
			formHollow = p.value("formHollow", formHollow);
			formRotate = p.value("formRotate", formRotate);
			formShear = p.value("formShear", formShear);
			formRipple = p.value("formRipple", formRipple);
			formRippleFreq = p.value("formRippleFreq", formRippleFreq);
			formBulge = p.value("formBulge", formBulge);
			formArc = p.value("formArc", formArc);
			formDissolve = p.value("formDissolve", formDissolve);
			formCore = p.value("formCore", formCore);

			// ---- Trail ----
			trailOn = p.value("trailOn", trailOn);
			trailFade = p.value("trailFade", trailFade);
			trailWidth = p.value("trailWidth", trailWidth);
			trailMode = p.value("trailMode", trailMode);

			// ---- Color ----
			colorMode = p.value("colorMode", colorMode);
			hueJitter = p.value("hueJitter", hueJitter);
			satJitter = p.value("satJitter", satJitter);
			valJitter = p.value("valJitter", valJitter);
			sizeRandom = p.value("sizeRandom", sizeRandom);
			colorKeyCount = p.value("colorKeyCount", colorKeyCount);
			if (p.contains("colorKeys") && p["colorKeys"].is_array())
			{
				const auto& arr = p["colorKeys"];
				for (size_t i = 0; i < arr.size() && i < 16; ++i)
					colorKeys[i] = arr[i].get<float>();
			}

			// ---- Simulation ----
			simSpace = p.value("simSpace", simSpace);
			inheritVel = p.value("inheritVel", inheritVel);

			// ---- Timing ----
			delay = p.value("delay", delay);
			duration = p.value("duration", duration);
			looping = p.value("looping", looping);
			prewarm = p.value("prewarm", prewarm);

			// 壊れた値の矯正（古いアセット・手書きJSON対策）
			// ここを通さないと、0 のまま GPU に渡って粒が見えなくなる
			if (lifeMin > lifeMax) lifeMax = lifeMin;
			if (rotSpeedMin > rotSpeedMax) rotSpeedMax = rotSpeedMin;
			if (sizeBegin == 0.0f && sizeMid == 0.0f && sizeEnd == 0.0f)
			{
				sizeBegin = 1.0f; sizeMid = 1.0f; sizeEnd = 1.0f;
			}
			if (curlScale <= 0.0f) curlScale = 0.5f;
			if (formAspect <= 0.0f) formAspect = 1.0f;
			if (formEdge <= 0.0f) formEdge = 0.15f;
			if (formBulge <= 0.0f) formBulge = 1.0f;
			if (formArc <= 0.0f) formArc = 1.0f;
			if (formRippleFreq < 1.0f) formRippleFreq = 6.0f;
			if (colorKeyCount < 2 || colorKeyCount > 4)
			{
				// グラデーション未設定なら基本色から2キーで作る
				colorKeyCount = 2;
				colorKeys[0] = colorR; colorKeys[1] = colorG; colorKeys[2] = colorB; colorKeys[3] = 0.0f;
				colorKeys[4] = colorR; colorKeys[5] = colorG; colorKeys[6] = colorB; colorKeys[7] = 1.0f;
			}
			if (sizeCurvePointCount < 2 || sizeCurvePointCount > kMaxCurvePoints) sizeCurvePointCount = 3;
			if (alphaCurvePointCount < 2 || alphaCurvePointCount > kMaxCurvePoints) alphaCurvePointCount = 4;
			if (sizeCurveInterp < 0 || sizeCurveInterp > 1) sizeCurveInterp = 0;
			if (alphaCurveInterp < 0 || alphaCurveInterp > 1) alphaCurveInterp = 0;
			if (delay < 0.0f) delay = 0.0f;
			if (duration < 0.0f) duration = 0.0f;
			if (prewarm < 0.0f) prewarm = 0.0f;
		}
		return true;
	}
	catch (...) { return false; }
}

bool ParticleAsset::LoadFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::stringstream ss; ss << f.rdbuf();
	return LoadFromString(ss.str());
}

std::string ParticleAsset::ToJsonString() const
{
	nlohmann::json j;
	j["name"] = name;

	j["texture"]["type"] = textureType;
	j["texture"]["effectName"] = effectName;
	j["texture"]["imagePath"] = imagePath;

	auto& p = j["params"];

	// ---- 基本 ----
	p["gravity"] = gravity;
	p["initialSpeed"] = initialSpeed;
	p["lifeMin"] = lifeMin;
	p["lifeMax"] = lifeMax;
	p["spread"] = spread;
	p["size"] = size;
	p["colorR"] = colorR;
	p["colorG"] = colorG;
	p["colorB"] = colorB;
	p["activeCount"] = activeCount;

	// ---- 動き ----
	p["cUpward"] = cUpward;
	p["cRadial"] = cRadial;
	p["cTangent"] = cTangent;
	p["cSwirl"] = cSwirl;
	p["cSphere"] = cSphere;
	p["cDrag"] = cDrag;
	p["cTurbulence"] = cTurbulence;
	p["cSizeGrow"] = cSizeGrow;
	p["cSeek"] = cSeek;
	p["cWave"] = cWave;
	p["cSpiral"] = cSpiral;
	p["cConverge"] = cConverge;
	p["targetX"] = targetX;
	p["targetY"] = targetY;
	p["targetZ"] = targetZ;

	// ---- Phase1 ----
	p["sizeBegin"] = sizeBegin;
	p["sizeMid"] = sizeMid;
	p["sizeEnd"] = sizeEnd;
	p["speedRandom"] = speedRandom;
	p["fadeIn"] = fadeIn;
	p["fadeOut"] = fadeOut;
	p["rotSpeedMin"] = rotSpeedMin;
	p["rotSpeedMax"] = rotSpeedMax;
	p["curlStrength"] = curlStrength;
	p["curlScale"] = curlScale;
	p["curlSpeed"] = curlSpeed;
	p["velColorScale"] = velColorScale;
	p["velSizeScale"] = velSizeScale;

	// ---- Phase8c ----
	p["sizeCurveOn"] = sizeCurveOn;
	p["sizeCurve"] = nlohmann::json::array();
	for (int i = 0; i < kMaxCurvePoints; ++i) p["sizeCurve"].push_back(sizeCurve[i]);
	p["sizeCurvePointCount"] = sizeCurvePointCount;
	p["sizeCurveInterp"] = sizeCurveInterp;
	p["alphaCurveOn"] = alphaCurveOn;
	p["alphaCurve"] = nlohmann::json::array();
	for (int i = 0; i < kMaxCurvePoints; ++i) p["alphaCurve"].push_back(alphaCurve[i]);
	p["alphaCurvePointCount"] = alphaCurvePointCount;
	p["alphaCurveInterp"] = alphaCurveInterp;

	// ---- Phase2a ----
	p["stretchOn"] = stretchOn;
	p["stretchScale"] = stretchScale;
	p["shapeType"] = shapeType;
	p["shapeRadius"] = shapeRadius;
	p["shapeHeight"] = shapeHeight;
	p["shapeSurfaceOnly"] = shapeSurfaceOnly;

	// ---- Form ----
	p["formOn"] = formOn;
	p["formSides"] = formSides;
	p["formRound"] = formRound;
	p["formSpike"] = formSpike;
	p["formAspect"] = formAspect;
	p["formTwist"] = formTwist;
	p["formEdge"] = formEdge;
	p["formHollow"] = formHollow;
	p["formRotate"] = formRotate;
	p["formShear"] = formShear;
	p["formRipple"] = formRipple;
	p["formRippleFreq"] = formRippleFreq;
	p["formBulge"] = formBulge;
	p["formArc"] = formArc;
	p["formDissolve"] = formDissolve;
	p["formCore"] = formCore;

	// ---- Trail ----
	p["trailOn"] = trailOn;
	p["trailFade"] = trailFade;
	p["trailWidth"] = trailWidth;
	p["trailMode"] = trailMode;

	// ---- Color ----
	p["colorMode"] = colorMode;
	p["hueJitter"] = hueJitter;
	p["satJitter"] = satJitter;
	p["valJitter"] = valJitter;
	p["sizeRandom"] = sizeRandom;
	p["colorKeyCount"] = colorKeyCount;
	p["colorKeys"] = nlohmann::json::array();
	for (int i = 0; i < 16; ++i) p["colorKeys"].push_back(colorKeys[i]);

	// ---- Simulation ----
	p["simSpace"] = simSpace;
	p["inheritVel"] = inheritVel;

	// ---- Timing ----
	p["delay"] = delay;
	p["duration"] = duration;
	p["looping"] = looping;
	p["prewarm"] = prewarm;

	return j.dump(2);
}
