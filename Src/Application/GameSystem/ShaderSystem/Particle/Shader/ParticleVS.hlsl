#include "inc_Particle.hlsli"

StructuredBuffer<Particle> g_particles : register(t0);
StructuredBuffer<EmitterData> g_emitters : register(t1);
StructuredBuffer<uint> g_activeList : register(t3); // 描くエミッタ番号の一覧

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
	float viewDepth : VIEWDEPTH; // ★粒のビュー空間Z（カメラからの距離）
	float4 clipPos : CLIPPOS; // ★クリップ座標（画面UV計算用）
	nointerpolation float4 form0 : FORM0; // sides, round, spike, aspect
	nointerpolation float4 form1 : FORM1; // twist, edge, hollow, on
	nointerpolation float4 form2 : FORM2; // rotate, shear, ripple, rippleFreq
	nointerpolation float4 form3 : FORM3; // bulge, arc, dissolve, core
};

VS_OUT main(uint id : SV_VertexID)
{
	// 一覧の何番目のエミッタの何番目の粒か → 粒バッファの位置
	uint slotIdx = id / 6;
	uint corner = id % 6;
	uint ppe = (uint) g_particlesPerEmitter;
	uint emitterId = g_activeList[slotIdx / ppe];
	uint particleIdx = emitterId * ppe + (slotIdx % ppe);

	Particle p = g_particles[particleIdx];

	VS_OUT o;
	if (p.life <= 0.0)
	{
        // ★全メンバを初期化（未初期化の出力を残さない）
		o.pos = float4(0, 0, -10, 1);
		o.uv = float2(0, 0);
		o.color = float4(0, 0, 0, 0);
		o.texIndex = 0;
		o.viewDepth = 0.0;
		o.clipPos = float4(0, 0, -10, 1);
		o.form0 = float4(0, 0, 0, 1);
		o.form1 = float4(0, 0, 0, 0);
		o.form2 = float4(0, 0, 0, 0);
		o.form3 = float4(1, 1, 0, 0);
		return o;
	}

	EmitterData e = g_emitters[emitterId];

	float total = max(p.age + p.life, 0.0001);
	float tNorm = saturate(p.age / total);

    // ★2 サイズカーブ（3キー：begin → mid → end）
    //    ゼロ初期化のエミッタで粒が消えないよう安全弁
	float curve;
	if (e.sizeCurveOn != 0)
	{
		// ★Phase8c カーブが有効ならこちらを使う
		curve = EvalCurve8(e.sizeCurve, e.sizeCurvePointCount, e.sizeCurveInterp, tNorm);
	}
	else
	{
		float3 keys = float3(e.sizeBegin, e.sizeMid, e.sizeEnd);
		if (dot(keys, float3(1, 1, 1)) <= 0.0001)
			keys = float3(1, 1, 1);

		curve = (tNorm < 0.5)
			? lerp(keys.x, keys.y, tNorm * 2.0)
			: lerp(keys.y, keys.z, (tNorm - 0.5) * 2.0);
	}

	float size = e.size * curve;
	size += e.cSizeGrow * p.age * 0.1;

    // ★11 速度連動（速い粒ほど大きく／明るく）
	float speed = length(p.vel);
	size *= lerp(1.0 - saturate(e.sizeRandom), 1.0, p.seed); // ★2 粒ごとのばらつき
	size *= 1.0 + speed * e.velSizeScale;
	size *= (e.emitScale > 0.0001) ? e.emitScale : 1.0;
	if (size < 0.001)
		size = 0.001;

	float2 offsets[6] =
	{
		float2(-1, -1), float2(1, -1), float2(-1, 1),
        float2(1, -1), float2(1, 1), float2(-1, 1)
	};
	float2 uvs[6] =
	{
		float2(0, 1), float2(1, 1), float2(0, 0),
        float2(1, 1), float2(1, 0), float2(0, 0)
	};
	float2 off = offsets[corner] * size;

	float3 camRight = float3(g_view._11, g_view._21, g_view._31);
	float3 camUp = float3(g_view._12, g_view._22, g_view._32);

	float2 quad;
	if (e.stretchOn != 0)
	{
        // ★5 速度方向へ引き伸ばす（回転は無視される）
		float3 vdir = (speed > 0.0001) ? (p.vel / speed) : camUp;

        // 速度をビルボード平面へ射影
		float2 sv = float2(dot(vdir, camRight), dot(vdir, camUp));
		float svLen = length(sv);
		sv = (svLen > 0.0001) ? (sv / svLen) : float2(0, 1);
		float2 sp = float2(-sv.y, sv.x); // 垂直方向

		float stretch = 1.0 + speed * e.stretchScale;
		quad = sp * off.x + sv * (off.y * stretch);
	}
	else
	{
        // ★4 通常の回転ビルボード
		float sn, cs;
		sincos(p.rot, sn, cs);
		quad = float2(off.x * cs - off.y * sn,
                      off.x * sn + off.y * cs);
	}

	float3 basePos = (e.simSpace != 0) ? (p.pos + e.pos) : p.pos; // ★4
	float3 worldPos = basePos + camRight * quad.x + camUp * quad.y;

	o.texIndex = e.textureIndex;
	o.pos = mul(float4(worldPos, 1.0), g_viewProj);
	o.viewDepth = length(g_camPos - worldPos);
	o.clipPos = o.pos;
	o.uv = uvs[corner];

	o.color = p.color;
	o.color.rgb *= 1.0 + speed * e.velColorScale; // ★11

	o.form0 = float4(e.formSides, e.formRound, e.formSpike, e.formAspect);
	o.form1 = float4(e.formTwist, e.formEdge, e.formHollow, (float) e.formOn);
	o.form2 = float4(e.formRotate, e.formShear, e.formRipple, e.formRippleFreq);
	o.form3 = float4(e.formBulge, e.formArc, e.formDissolve, e.formCore);
	return o;
}
