#include "inc_Particle.hlsli"

StructuredBuffer<Particle> g_particles : register(t0);
StructuredBuffer<EmitterData> g_emitters : register(t1);
StructuredBuffer<float4> g_history : register(t2);
StructuredBuffer<uint> g_activeList : register(t3); // トレイルを描くエミッタ番号の一覧

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

cbuffer TrailCB : register(b1)
{
	int g_trailCursor;
	int _tp0;
	int _tp1;
	int _tp2;
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

#define TRAIL_POINTS 8

// 履歴スロット k の位置を取り出す（無効なら w<0.5）
float4 FetchHistory(uint particleIdx, int k, EmitterData e)
{
	int slot = ((int) g_trailCursor - k + TRAIL_POINTS * 4) % TRAIL_POINTS;
	float4 hp = g_history[particleIdx * TRAIL_POINTS + (uint) slot];
	if (e.simSpace != 0)
		hp.xyz += e.pos; // Local は相対で持っているのでワールドへ
	return hp;
}

VS_OUT main(uint id : SV_VertexID)
{
	uint quadIdx = id / 6;
	uint corner = id % 6;
	uint slotIdx = quadIdx / TRAIL_POINTS;
	uint k = quadIdx % TRAIL_POINTS; // 0 = 直近, 大きいほど古い

	// 一覧の何番目のエミッタの何番目の粒か → 粒バッファの位置
	uint ppe = (uint) g_particlesPerEmitter;
	uint emitterId = g_activeList[slotIdx / ppe];
	uint particleIdx = emitterId * ppe + (slotIdx % ppe);

    // 画面外へ退避した状態で全メンバを初期化
	VS_OUT o;
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

	Particle p = g_particles[particleIdx];
	if (p.life <= 0.0)
		return o;

	EmitterData e = g_emitters[emitterId];
	if (e.trailOn == 0)
		return o;

    // 本体VSと同じサイズカーブ
	float total = max(p.age + p.life, 0.0001);
	float tNorm = saturate(p.age / total);
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
		curve = (tNorm < 0.5) ? lerp(keys.x, keys.y, tNorm * 2.0)
			: lerp(keys.y, keys.z, (tNorm - 0.5) * 2.0);
	}
	float baseSize = e.size * curve * lerp(1.0 - saturate(e.sizeRandom), 1.0, p.seed);

	float3 camRight = float3(g_view._11, g_view._21, g_view._31);
	float3 camUp = float3(g_view._12, g_view._22, g_view._32);

	if (e.trailMode != 0)
	{
        //================ リボン ================
        // 履歴 k と k+1 をつなぐ帯。最後のスロットには相手がいないので捨てる
		if (k + 1 >= TRAIL_POINTS)
			return o;

		float4 h0 = FetchHistory(particleIdx, (int) k, e); // 新しい側
		float4 h1 = FetchHistory(particleIdx, (int) k + 1, e); // 古い側
		if (h0.w < 0.5 || h1.w < 0.5)
			return o;

		float3 seg = h1.xyz - h0.xyz;
		if (dot(seg, seg) < 1e-8)
			return o; // 止まっている粒はゼロ長になる

        // 帯の向きを画面平面へ射影し、その垂直方向を幅にする
		float2 d2 = float2(dot(seg, camRight), dot(seg, camUp));
		float dl = length(d2);
		d2 = (dl > 1e-4) ? (d2 / dl) : float2(0, 1);
		float2 n2 = float2(-d2.y, d2.x);

        // corner: .x = 0(新しい端) / 1(古い端),  .y = -1 / +1（帯の左右）
		float2 rc[6] =
		{
			float2(0, -1), float2(0, 1), float2(1, -1),
            float2(0, 1), float2(1, 1), float2(1, -1)
		};
		float2 c = rc[corner];

		float t0 = (float) k / (float) (TRAIL_POINTS - 1);
		float t1 = (float) (k + 1) / (float) (TRAIL_POINTS - 1);
		float tv = lerp(t0, t1, c.x); // 0=直近 1=最古

		float w = baseSize * lerp(1.0, saturate(e.trailWidth), tv);
		if (w < 0.001)
			w = 0.001;

		float3 basePos = lerp(h0.xyz, h1.xyz, c.x);
		float3 worldPos = basePos + (camRight * n2.x + camUp * n2.y) * (w * c.y);

		o.pos = mul(float4(worldPos, 1.0), g_viewProj);
		o.viewDepth = length(g_camPos - worldPos);
		o.clipPos = o.pos;
		o.uv = float2(c.y * 0.5 + 0.5, tv);
		o.texIndex = -1; // ★PSにリボンだと伝える
		o.form1 = float4(0, max(e.formEdge, 0.02), 0, 0); // y = 幅方向のぼかし
		o.color = p.color;
		o.color.a *= (1.0 - tv) * saturate(e.trailFade);
		return o;
	}

    //================ 残像（板を並べる従来方式） ================
	float4 hp = FetchHistory(particleIdx, (int) k, e);
	if (hp.w < 0.5)
		return o;

	float t = (float) k / (float) TRAIL_POINTS;

	float size = baseSize * lerp(1.0, saturate(e.trailWidth), t);
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

	float3 worldPos = hp.xyz + camRight * off.x + camUp * off.y;

	o.pos = mul(float4(worldPos, 1.0), g_viewProj);
	o.viewDepth = length(g_camPos - worldPos);
	o.clipPos = o.pos;
	o.uv = uvs[corner];
	o.texIndex = e.textureIndex;

	o.form0 = float4(e.formSides, e.formRound, e.formSpike, e.formAspect);
	o.form1 = float4(e.formTwist, e.formEdge, e.formHollow, (float) e.formOn);
	o.form2 = float4(e.formRotate, e.formShear, e.formRipple, e.formRippleFreq);
	o.form3 = float4(e.formBulge, e.formArc, e.formDissolve, e.formCore);

	o.color = p.color;
	o.color.a *= (1.0 - t) * saturate(e.trailFade);
	return o;
}
