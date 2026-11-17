#pragma once

struct ParticleAsset
{
	std::string name = "particle";

	// テクスチャの決め方
	std::string textureType = "builtin"; 
	std::string effectName = "";
	std::string imagePath = "";

	// ---- 基本 ----
	float gravity = 1.5f;
	float initialSpeed = 1.0f;
	float lifeMin = 1.0f;
	float lifeMax = 3.0f;
	float spread = 0.5f;
	float size = 0.1f;
	float colorR = 1.0f;
	float colorG = 0.5f;
	float colorB = 0.1f;
	int   activeCount = 1024;

	// ---- 動き（力係数） ----
	float cUpward = 1.0f;
	float cRadial = 0.0f;
	float cTangent = 0.0f;
	float cSwirl = 0.0f;
	float cSphere = 0.0f;
	float cDrag = 0.0f;
	float cTurbulence = 0.0f;
	float cSizeGrow = 0.0f;
	float cSeek = 0.0f, cWave = 0.0f, cSpiral = 0.0f, cConverge = 0.0f;
	float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;

	// ---- カーブ・回転・カールノイズ・速度連動・初速ランダム ----
	float sizeBegin = 1.0f, sizeMid = 1.0f, sizeEnd = 1.0f;
	float speedRandom = 0.0f;
	float fadeIn = 0.0f, fadeOut = 0.0f;
	float rotSpeedMin = 0.0f, rotSpeedMax = 0.0f;
	float curlStrength = 0.0f, curlScale = 0.5f, curlSpeed = 0.0f;
	float velColorScale = 0.0f, velSizeScale = 0.0f;

	// ---- サイズ／アルファのカーブ ----
	static const int kMaxCurvePoints = 8;
	int   sizeCurveOn = 0;
	float sizeCurve[kMaxCurvePoints] = { 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	int   sizeCurvePointCount = 3;
	int   sizeCurveInterp = 0;
	int   alphaCurveOn = 0;
	float alphaCurve[kMaxCurvePoints] = { 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	int   alphaCurvePointCount = 4;
	int   alphaCurveInterp = 0;

	// ---- ストレッチビルボード・発生形状 ----
	int   stretchOn = 0;
	float stretchScale = 0.5f;
	int   shapeType = 0;
	float shapeRadius = 0.0f;
	float shapeHeight = 0.0f;
	int   shapeSurfaceOnly = 0;

	// ---- Form ----
	int   formOn = 0;
	float formSides = 0.0f;
	float formRound = 0.0f;
	float formSpike = 0.0f;
	float formAspect = 1.0f;
	float formTwist = 0.0f;
	float formEdge = 0.15f;
	float formHollow = 0.0f;
	float formRotate = 0.0f;
	float formShear = 0.0f;
	float formRipple = 0.0f;
	float formRippleFreq = 6.0f;
	float formBulge = 1.0f;
	float formArc = 1.0f;
	float formDissolve = 0.0f;
	float formCore = 0.0f;

	// ---- Trail ----
	int   trailOn = 0;
	float trailFade = 0.6f;
	float trailWidth = 0.3f;
	int   trailMode = 0;

	// ---- Color ----
	int   colorMode = 0;
	float hueJitter = 0.0f, satJitter = 0.0f, valJitter = 0.0f;
	float sizeRandom = 0.0f;
	int   colorKeyCount = 2;
	// rgb + 位置 を4キー分
	float colorKeys[16] = {
		1.0f, 0.5f, 0.1f, 0.0f,
		1.0f, 0.5f, 0.1f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f };

	// ---- Simulation ----
	int   simSpace = 0;
	float inheritVel = 0.0f;

	// ---- Timing ----
	float delay = 0.0f;
	float duration = 0.0f;    
	int   looping = 1;
	float prewarm = 0.0f;

	bool LoadFromFile(const std::string& path);
	bool LoadFromString(const std::string& jsonStr);
	std::string ToJsonString() const;
};
