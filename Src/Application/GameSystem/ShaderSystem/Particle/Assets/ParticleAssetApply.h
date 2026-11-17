#pragma once
#include "ParticleAsset.h"

template<typename E>
void ApplyAssetToEmitter(const ParticleAsset& a, E& e)
{
	// ---- 基本 ----
	e.color = { a.colorR, a.colorG, a.colorB };
	e.gravity = a.gravity;
	e.initialSpeed = a.initialSpeed;
	e.spread = a.spread;
	e.lifeMin = a.lifeMin;
	e.lifeMax = a.lifeMax;
	e.size = a.size;

	// ---- 動き（力係数）----
	e.cUpward = a.cUpward;
	e.cRadial = a.cRadial;
	e.cTangent = a.cTangent;
	e.cSwirl = a.cSwirl;
	e.cSphere = a.cSphere;
	e.cDrag = a.cDrag;
	e.cTurbulence = a.cTurbulence;
	e.cSizeGrow = a.cSizeGrow;
	e.cSeek = a.cSeek;
	e.cWave = a.cWave;
	e.cSpiral = a.cSpiral;
	e.cConverge = a.cConverge;
	e.targetPos = { a.targetX, a.targetY, a.targetZ };

	// ---- Phase1（カーブ・回転・カールノイズ・速度連動）----
	e.sizeBegin = a.sizeBegin;
	e.sizeMid = a.sizeMid;
	e.sizeEnd = a.sizeEnd;
	e.speedRandom = a.speedRandom;
	e.fadeIn = a.fadeIn;
	e.fadeOut = a.fadeOut;
	e.rotSpeedMin = a.rotSpeedMin;
	e.rotSpeedMax = a.rotSpeedMax;
	e.curlStrength = a.curlStrength;
	e.curlScale = a.curlScale;
	e.curlSpeed = a.curlSpeed;
	e.velColorScale = a.velColorScale;
	e.velSizeScale = a.velSizeScale;

	// ---- Phase8c（サイズ／アルファのカーブエディタ）----
	e.sizeCurveOn = a.sizeCurveOn;
	for (int i = 0; i < ParticleAsset::kMaxCurvePoints; ++i) e.sizeCurve[i] = a.sizeCurve[i];
	e.sizeCurvePointCount = a.sizeCurvePointCount;
	e.sizeCurveInterp = a.sizeCurveInterp;
	e.alphaCurveOn = a.alphaCurveOn;
	for (int i = 0; i < ParticleAsset::kMaxCurvePoints; ++i) e.alphaCurve[i] = a.alphaCurve[i];
	e.alphaCurvePointCount = a.alphaCurvePointCount;
	e.alphaCurveInterp = a.alphaCurveInterp;

	// ---- Phase2a（ストレッチ・発生形状）----
	e.stretchOn = a.stretchOn;
	e.stretchScale = a.stretchScale;
	e.shapeType = a.shapeType;
	e.shapeRadius = a.shapeRadius;
	e.shapeHeight = a.shapeHeight;
	e.shapeSurfaceOnly = a.shapeSurfaceOnly;

	// ---- Form（粒そのものの形）----
	e.formOn = a.formOn;
	e.formSides = a.formSides;
	e.formRound = a.formRound;
	e.formSpike = a.formSpike;
	e.formAspect = a.formAspect;
	e.formTwist = a.formTwist;
	e.formEdge = a.formEdge;
	e.formHollow = a.formHollow;
	e.formRotate = a.formRotate;
	e.formShear = a.formShear;
	e.formRipple = a.formRipple;
	e.formRippleFreq = a.formRippleFreq;
	e.formBulge = a.formBulge;
	e.formArc = a.formArc;
	e.formDissolve = a.formDissolve;
	e.formCore = a.formCore;

	// ---- Trail（残像 / リボン）----
	e.trailOn = a.trailOn;
	e.trailFade = a.trailFade;
	e.trailWidth = a.trailWidth;
	e.trailMode = a.trailMode;

	// ---- Color（グラデーション＋ばらつき）----
	e.colorMode = a.colorMode;
	e.hueJitter = a.hueJitter;
	e.satJitter = a.satJitter;
	e.valJitter = a.valJitter;
	e.sizeRandom = a.sizeRandom;
	e.colorKeyCount = a.colorKeyCount;
	e.colorKey0 = { a.colorKeys[0],  a.colorKeys[1],  a.colorKeys[2],  a.colorKeys[3] };
	e.colorKey1 = { a.colorKeys[4],  a.colorKeys[5],  a.colorKeys[6],  a.colorKeys[7] };
	e.colorKey2 = { a.colorKeys[8],  a.colorKeys[9],  a.colorKeys[10], a.colorKeys[11] };
	e.colorKey3 = { a.colorKeys[12], a.colorKeys[13], a.colorKeys[14], a.colorKeys[15] };

	// ---- Simulation ----
	e.simSpace = a.simSpace;
	e.inheritVel = a.inheritVel;

	// ---- Timing（GPUでは読まないが、値は保持しておく）----
	e.delay = a.delay;
	e.duration = a.duration;
	e.looping = a.looping;
	e.prewarm = a.prewarm;

	// ---- パディング ----
	e._pad1 = e._pad2 = e._pad3 = e._pad4 = 0.0f;
	e._pad8 = e._pad9 = e._pad10 = 0.0f;
	e._padD = e._padE = 0.0f;
}

template<typename E>
void ExtractAssetFromEmitter(const E& e, ParticleAsset& a)
{
	// ---- 表現が違うもの ----
	a.colorR = e.color.x;
	a.colorG = e.color.y;
	a.colorB = e.color.z;

	a.targetX = e.targetPos.x;
	a.targetY = e.targetPos.y;
	a.targetZ = e.targetPos.z;

	a.colorKeys[0] = e.colorKey0.x; a.colorKeys[1] = e.colorKey0.y;
	a.colorKeys[2] = e.colorKey0.z; a.colorKeys[3] = e.colorKey0.w;
	a.colorKeys[4] = e.colorKey1.x; a.colorKeys[5] = e.colorKey1.y;
	a.colorKeys[6] = e.colorKey1.z; a.colorKeys[7] = e.colorKey1.w;
	a.colorKeys[8] = e.colorKey2.x; a.colorKeys[9] = e.colorKey2.y;
	a.colorKeys[10] = e.colorKey2.z; a.colorKeys[11] = e.colorKey2.w;
	a.colorKeys[12] = e.colorKey3.x; a.colorKeys[13] = e.colorKey3.y;
	a.colorKeys[14] = e.colorKey3.z; a.colorKeys[15] = e.colorKey3.w;

	// ---- 同名のものをそのまま ----
	a.gravity = e.gravity;
	a.initialSpeed = e.initialSpeed;
	a.spread = e.spread;
	a.lifeMin = e.lifeMin;
	a.lifeMax = e.lifeMax;
	a.size = e.size;
	a.cUpward = e.cUpward;
	a.cRadial = e.cRadial;
	a.cTangent = e.cTangent;
	a.cSwirl = e.cSwirl;
	a.cSphere = e.cSphere;
	a.cDrag = e.cDrag;
	a.cTurbulence = e.cTurbulence;
	a.cSizeGrow = e.cSizeGrow;
	a.cSeek = e.cSeek;
	a.cWave = e.cWave;
	a.cSpiral = e.cSpiral;
	a.cConverge = e.cConverge;
	a.sizeBegin = e.sizeBegin;
	a.sizeMid = e.sizeMid;
	a.sizeEnd = e.sizeEnd;
	a.speedRandom = e.speedRandom;
	a.fadeIn = e.fadeIn;
	a.fadeOut = e.fadeOut;
	a.rotSpeedMin = e.rotSpeedMin;
	a.rotSpeedMax = e.rotSpeedMax;
	a.curlStrength = e.curlStrength;
	a.curlScale = e.curlScale;
	a.curlSpeed = e.curlSpeed;
	a.velColorScale = e.velColorScale;
	a.velSizeScale = e.velSizeScale;
	a.sizeCurveOn = e.sizeCurveOn;
	for (int i = 0; i < ParticleAsset::kMaxCurvePoints; ++i) a.sizeCurve[i] = e.sizeCurve[i];
	a.sizeCurvePointCount = e.sizeCurvePointCount;
	a.sizeCurveInterp = e.sizeCurveInterp;
	a.alphaCurveOn = e.alphaCurveOn;
	for (int i = 0; i < ParticleAsset::kMaxCurvePoints; ++i) a.alphaCurve[i] = e.alphaCurve[i];
	a.alphaCurvePointCount = e.alphaCurvePointCount;
	a.alphaCurveInterp = e.alphaCurveInterp;
	a.stretchOn = e.stretchOn;
	a.stretchScale = e.stretchScale;
	a.shapeType = e.shapeType;
	a.shapeRadius = e.shapeRadius;
	a.shapeHeight = e.shapeHeight;
	a.shapeSurfaceOnly = e.shapeSurfaceOnly;
	a.formOn = e.formOn;
	a.formSides = e.formSides;
	a.formRound = e.formRound;
	a.formSpike = e.formSpike;
	a.formAspect = e.formAspect;
	a.formTwist = e.formTwist;
	a.formEdge = e.formEdge;
	a.formHollow = e.formHollow;
	a.formRotate = e.formRotate;
	a.formShear = e.formShear;
	a.formRipple = e.formRipple;
	a.formRippleFreq = e.formRippleFreq;
	a.formBulge = e.formBulge;
	a.formArc = e.formArc;
	a.formDissolve = e.formDissolve;
	a.formCore = e.formCore;
	a.trailOn = e.trailOn;
	a.trailFade = e.trailFade;
	a.trailWidth = e.trailWidth;
	a.trailMode = e.trailMode;
	a.colorMode = e.colorMode;
	a.hueJitter = e.hueJitter;
	a.satJitter = e.satJitter;
	a.valJitter = e.valJitter;
	a.sizeRandom = e.sizeRandom;
	a.colorKeyCount = e.colorKeyCount;
	a.simSpace = e.simSpace;
	a.inheritVel = e.inheritVel;
	a.delay = e.delay;
	a.duration = e.duration;
	a.looping = e.looping;
	a.prewarm = e.prewarm;
}