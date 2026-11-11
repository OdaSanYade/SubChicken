// ============================================
// パーティクル共通定義（CS / 本体VS / トレイルVS で共有）
// ※C++ 側 GPUParticleSystem.h の GPUParticle / EmitterData と同じ並びにすること
//   （C++ 側は static_assert で並びを確かめている）
// ============================================
struct Particle
{
	float3 pos;
	float life;
	float3 vel;
	float age;
	float4 color;
	float rot;
	float rotSpeed;
	float seed;
	float _padP;
};
struct EmitterData
{
	float3 pos;
	int active;
	float3 color;
	float gravity;
	float initialSpeed;
	float spread;
	float lifeMin;
	float lifeMax;
	int mode;
	int emitTrigger;
	float size;
	int typeId;
	int textureIndex;
	float cUpward;
	float cRadial;
	float cTangent;
	float cSwirl;
	float cSphere;
	float cDrag;
	float cTurbulence;
	float cSizeGrow;
	float cSeek;
	float cWave;
	float cSpiral;
	float cConverge;
	float emitScale;
	float _pad1;
	float _pad2;
	float3 targetPos;
	float _pad3;

	float sizeBegin;
	float sizeMid;
	float sizeEnd;
	float speedRandom;
	float fadeIn;
	float fadeOut;
	float rotSpeedMin;
	float rotSpeedMax;
	float curlStrength;
	float curlScale;
	float curlSpeed;
	float velColorScale;
	float velSizeScale;
	int stretchOn;
	float stretchScale;
	int shapeType;
	float shapeRadius;
	float shapeHeight;
	int shapeSurfaceOnly;
	float _pad4;
	int formOn;
	float formSides;
	float formRound;
	float formSpike;
	float formAspect;
	float formTwist;
	float formEdge;
	float formHollow;
	int trailOn;
	float trailFade;
	float trailWidth;
	int trailMode;
	float formRotate;
	float formShear;
	float formRipple;
	float formRippleFreq;
	float formBulge;
	float formArc;
	float formDissolve;
	float formCore;
	int colorMode;
	float hueJitter;
	float satJitter;
	float valJitter;
	float sizeRandom;
	int colorKeyCount;
	int emitBase;
	int emitCount;
	float4 colorKey0;
	float4 colorKey1;
	float4 colorKey2;
	float4 colorKey3;
	int simSpace;
	float inheritVel;
	float _pad8;
	float _pad9;
	float3 emitterVel;
	float _pad10;
	float delay;
	float duration;
	int looping;
	float prewarm;
	float3 emitAxisX;
	float _padA;
	float3 emitAxisY;
	float _padB;
	float3 emitAxisZ;
	float _padC;

	// Phase8c（サイズ／アルファのカーブ。エディタと同じ）
	int sizeCurveOn;
	float sizeCurve[8];
	int sizeCurvePointCount;
	int sizeCurveInterp;
	float _padD;
	int alphaCurveOn;
	float alphaCurve[8];
	int alphaCurvePointCount;
	int alphaCurveInterp;
	float _padE;
};

// ★Phase8c カーブ（8点。Linear/Catmull-Rom。エディタと同じ式）
float CatmullRom8(float p0, float p1, float p2, float p3, float t)
{
	float t2 = t * t;
	float t3 = t2 * t;
	return 0.5 * ((2.0 * p1) + (-p0 + p2) * t
		+ (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2
		+ (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3);
}

float EvalCurve8(float curve[8], int count, int interp, float t)
{
	int c = clamp(count, 2, 8);
	t = saturate(t);
	float seg = t * (float) (c - 1);
	int i = clamp((int) floor(seg), 0, c - 2);
	float f = seg - (float) i;

	if (interp == 1)
	{
		int i0 = max(i - 1, 0);
		int i2 = min(i + 1, c - 1);
		int i3 = min(i + 2, c - 1);
		return CatmullRom8(curve[i0], curve[i], curve[i2], curve[i3], f);
	}
	return lerp(curve[i], curve[i + 1], f);
}
