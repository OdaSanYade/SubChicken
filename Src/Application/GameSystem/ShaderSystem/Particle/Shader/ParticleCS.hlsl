#include "inc_Particle.hlsli"

RWStructuredBuffer<Particle> g_particles : register(u0);
RWStructuredBuffer<float4> g_history : register(u1);
StructuredBuffer<EmitterData> g_emitters : register(t1);
Texture2D<float4> g_scenePos : register(t2); // GBufferC（xyz = ワールド座標、背景は 0）
StructuredBuffer<uint> g_activeList : register(t3); // 動かすエミッタ番号の一覧

cbuffer CB : register(b0)
{
	float g_dt; // 0
	float g_time; // 4
	int g_totalCount; // 8
	int g_particlesPerEmitter; // 12
	float4x4 g_viewProj; // 16〜79
	int g_screenW; // 80
	int g_screenH; // 84
	int g_collisionOn; // 88
	float g_nearZ; // 92
	float g_farZ; // 96
	float g_restitution; // 100
	int g_trailCursor; // 104
	int g_trailWrite; // 108
};

float LinearizeDepth(float d, float n, float f)
{
	return n * f / (f - d * (f - n));
}

float rand(float2 co)
{
	return frac(sin(dot(co, float2(12.9898, 78.233))) * 43758.5453);
}

float hash13(float3 p)
{
	p = frac(p * 0.3183099 + 0.1);
	p *= 17.0;
	return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float vnoise(float3 x)
{
	float3 i = floor(x);
	float3 f = frac(x);
	f = f * f * (3.0 - 2.0 * f);

	float n000 = hash13(i + float3(0, 0, 0));
	float n100 = hash13(i + float3(1, 0, 0));
	float n010 = hash13(i + float3(0, 1, 0));
	float n110 = hash13(i + float3(1, 1, 0));
	float n001 = hash13(i + float3(0, 0, 1));
	float n101 = hash13(i + float3(1, 0, 1));
	float n011 = hash13(i + float3(0, 1, 1));
	float n111 = hash13(i + float3(1, 1, 1));

	float nx00 = lerp(n000, n100, f.x);
	float nx10 = lerp(n010, n110, f.x);
	float nx01 = lerp(n001, n101, f.x);
	float nx11 = lerp(n011, n111, f.x);
	float nxy0 = lerp(nx00, nx10, f.y);
	float nxy1 = lerp(nx01, nx11, f.y);
	return lerp(nxy0, nxy1, f.z) * 2.0 - 1.0;
}

float3 potential(float3 p)
{
	return float3(
        vnoise(p),
        vnoise(p + float3(31.41, 17.23, 47.11)),
        vnoise(p + float3(11.73, 63.31, 29.97)));
}

float3 CurlNoise(float3 p)
{
	const float e = 0.15;
	float3 dx = float3(e, 0, 0);
	float3 dy = float3(0, e, 0);
	float3 dz = float3(0, 0, e);

	float3 px0 = potential(p - dx), px1 = potential(p + dx);
	float3 py0 = potential(p - dy), py1 = potential(p + dy);
	float3 pz0 = potential(p - dz), pz1 = potential(p + dz);

	float cx = (py1.z - py0.z) - (pz1.y - pz0.y);
	float cy = (pz1.x - pz0.x) - (px1.z - px0.z);
	float cz = (px1.y - px0.y) - (py1.x - py0.x);
	return float3(cx, cy, cz) / (2.0 * e);
}

// シェイプ内のランダムな1点（エミッタ原点からのオフセット）
float3 ShapeOffset(EmitterData e, float seed, float3 radialDir)
{
    // 0 = Point
	if (e.shapeType == 1)        // Sphere
	{
		float th = rand(float2(seed, 21.0)) * 6.2831853;
		float ph = acos(2.0 * rand(float2(seed, 22.0)) - 1.0);
		float3 dir = float3(sin(ph) * cos(th), cos(ph), sin(ph) * sin(th));
        // 体積内で均一にするには 1/3 乗。表面のみなら常に半径いっぱい
		float rr = (e.shapeSurfaceOnly != 0)
                 ? 1.0
                 : pow(max(rand(float2(seed, 23.0)), 0.0001), 1.0 / 3.0);
		return dir * e.shapeRadius * rr;
	}
	if (e.shapeType == 2)        // Box
	{
		return float3(
            (rand(float2(seed, 24.0)) - 0.5) * 2.0 * e.shapeRadius,
            (rand(float2(seed, 25.0)) - 0.5) * 2.0 * e.shapeHeight,
            (rand(float2(seed, 26.0)) - 0.5) * 2.0 * e.shapeRadius);
	}
	if (e.shapeType == 3)        // Circle（XZ平面）
	{
        // 面積で均一にするには sqrt
		float rr = (e.shapeSurfaceOnly != 0)
                 ? 1.0
                 : sqrt(rand(float2(seed, 27.0)));
		return radialDir * e.shapeRadius * rr;
	}
	if (e.shapeType == 4)        // Cone（底面の円。方向は upDir 側で決める）
	{
		return radialDir * e.shapeRadius * sqrt(rand(float2(seed, 28.0)));
	}
	return float3(0, 0, 0);
}

float3 rgb2hsv(float3 c)
{
	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
	float4 q1 = lerp(float4(c.bg, K.wz), float4(c.gb, K.xy), step(c.b, c.g));
	float4 q2 = lerp(float4(q1.xyw, c.r), float4(c.r, q1.yzx), step(q1.x, c.r));
	float d = q2.x - min(q2.w, q2.y);
	float eps = 1.0e-10;
	return float3(abs(q2.z + (q2.w - q2.y) / (6.0 * d + eps)), d / (q2.x + eps), q2.x);
}

float3 hsv2rgb(float3 c)
{
	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
	float3 q = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
	return c.z * lerp(K.xxx, saturate(q - K.xxx), c.y);
}

// 寿命 t(0〜1) におけるグラデーション色
float3 EvalGradient(EmitterData e, float t)
{
	float4 k[4] = { e.colorKey0, e.colorKey1, e.colorKey2, e.colorKey3 };
	int n = clamp(e.colorKeyCount, 2, 4);

	float3 c = k[0].rgb;
    [unroll]
	for (int i = 0; i < 3; ++i)
	{
		if (i + 1 >= n)
			continue;
		if (t >= k[i].w)
		{
			float f = saturate((t - k[i].w) / max(k[i + 1].w - k[i].w, 1e-4));
			c = lerp(k[i].rgb, k[i + 1].rgb, f);
		}
	}
	return c;
}

// 基本色 → グラデーション → 粒ごとのHSVずらし
float3 ResolveColor(EmitterData e, float t, float seed)
{
	float3 c = (e.colorMode != 0) ? EvalGradient(e, t) : e.color;

	if (e.hueJitter > 0.0001 || e.satJitter > 0.0001 || e.valJitter > 0.0001)
	{
		float3 hsv = rgb2hsv(c);
		hsv.x = frac(hsv.x + (frac(seed * 7.31) - 0.5) * e.hueJitter);
		hsv.y = saturate(hsv.y + (frac(seed * 13.77) - 0.5) * e.satJitter);
		hsv.z = saturate(hsv.z + (frac(seed * 19.13) - 0.5) * e.valJitter);
		c = hsv2rgb(hsv);
	}
	return c;
}

float3 ToEmitterSpace(EmitterData e, float3 v)
{
	return e.emitAxisX * v.x + e.emitAxisY * v.y + e.emitAxisZ * v.z;
}

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
	// g_totalCount = 一覧のエミッタ数 × 1エミッタの粒数
	uint t = id.x;
	if (t >= (uint) g_totalCount)
		return;

    // この粒が属するエミッタ（一覧から引く）
	uint ppe = (uint) g_particlesPerEmitter;
	uint emitterId = g_activeList[t / ppe];
	uint i = emitterId * ppe + (t % ppe);
	EmitterData e = g_emitters[emitterId];

	Particle p = g_particles[i];
	p.life -= g_dt;

	if (p.life <= 0.0)
	{
        // エミッタ無効／放出停止中（active==2）→ 新しい粒は作らない
		if (e.active != 1)
		{
			p.color.a = 0.0;
			g_particles[i] = p;
			return;
		}

        // ワンショットで未発火 → 死んだまま
		if (e.mode == 1 && e.emitTrigger == 0)
		{
			p.color.a = 0.0;
			g_particles[i] = p;
			return;
		}

        // ★17 バースト：emitBase から emitCount 個ぶんの窓だけ生成する
        //    窓を回していかないと、前のバーストの粒が生きていて出られなくなる
		if (e.mode == 2)
		{
			if (e.emitTrigger == 0)
			{
				p.color.a = 0.0;
				g_particles[i] = p;
				return;
			}
			int localIdx = (int) (i % (uint) g_particlesPerEmitter);
			int rel = (localIdx - e.emitBase + g_particlesPerEmitter * 4) % g_particlesPerEmitter;
			if (rel >= e.emitCount)
			{
				p.color.a = 0.0;
				g_particles[i] = p;
				return;
			}
		}

        // 継続リスポーンの暴発防止（確率の門）
        // ※ワンショットのときは門なしで一斉に出す
		float seed = (float) i + g_time;
		if (e.mode == 0)
		{
			float spawnChance = rand(float2(seed, 7.0));
			if (spawnChance > 0.05)
			{
				p.color.a = 0.0;
				g_particles[i] = p;
				return;
			}
		}

        // 生成
		float a = rand(float2(seed, 1.0)) * 6.2831853; // 方位角
		float r = rand(float2(seed, 2.0)); // 0〜1

		float3 radialDir = float3(cos(a), 0, sin(a));
		float3 tangentDir = float3(-sin(a), 0, cos(a));
		float3 upDir = float3(0, 1, 0);
		if (e.shapeType == 4)
			upDir = normalize(float3(0, 1, 0) + radialDir * e.shapeHeight);
		float3 randDir = float3(
            (rand(float2(seed, 4.0)) - 0.5),
            (rand(float2(seed, 5.0)) - 0.5),
            (rand(float2(seed, 6.0)) - 0.5));

        // ★球面放射（全方向。爆発）
		float theta = rand(float2(seed, 8.0)) * 6.2831853;
		float phi = acos(2.0 * rand(float2(seed, 9.0)) - 1.0); // 均一な球面
		float3 sphereDir = float3(
            sin(phi) * cos(theta),
            cos(phi),
            sin(phi) * sin(theta));

		float3 vel =
            upDir * e.cUpward +
            radialDir * e.cRadial * (0.5 + r) +
            tangentDir * e.cTangent +
            sphereDir * e.cSphere * (0.5 + r) + // ★球面
            randDir * e.spread;

		float spdMul = lerp(1.0 - saturate(e.speedRandom), 1.0, rand(float2(seed, 10.0)));

		float scale = (e.emitScale > 0.0001) ? e.emitScale : 1.0;

		float3 spawnOff = ToEmitterSpace(e, ShapeOffset(e, seed, radialDir)) * scale;
		p.pos = (e.simSpace != 0) ? spawnOff : (e.pos + spawnOff);

		p.vel = ToEmitterSpace(e, vel) * e.initialSpeed * spdMul * scale;
		p.vel += e.emitterVel * e.inheritVel;

		p.life = e.lifeMin + rand(float2(seed, 3.0)) * (e.lifeMax - e.lifeMin);
		p.age = 0.0;
		p.rot = rand(float2(seed, 11.0)) * 6.2831853;
		p.rotSpeed = lerp(e.rotSpeedMin, e.rotSpeedMax, rand(float2(seed, 12.0)));
		p.seed = rand(float2(seed, 13.0));
		p._padP = 0.0;

        // ★1/3 グラデーションの始点＋粒ごとのHSVずらし（seed 確定後に評価する）
		p.color = float4(ResolveColor(e, 0.0, p.seed), 1.0);

        // 履歴を発生位置で埋める（前の粒の残骸を引きずらない）
		for (int h = 0; h < 8; ++h)
			g_history[i * 8 + h] = float4(p.pos, 1.0);
	}
	else
	{
		// ★4 Local のとき、渦の中心と目標を相対系に読み替える
		float3 orgPos = (e.simSpace != 0) ? float3(0, 0, 0) : e.pos;
		float3 tgtPos = (e.simSpace != 0) ? (e.targetPos - e.pos) : e.targetPos;

		// 重力
		p.vel.y -= e.gravity * g_dt;

		// ★減速（空気抵抗）：速度に比例して減速
		p.vel -= p.vel * e.cDrag * g_dt;

		if (e.curlStrength != 0.0)
		{
			float3 np = p.pos * max(e.curlScale, 0.0001)
			          + float3(0.0, g_time * e.curlSpeed, 0.0);
			p.vel += CurlNoise(np) * e.curlStrength * g_dt;
		}

		// 渦の継続（既存）
		if (e.cSwirl != 0.0)
		{
			float2 toCenter = p.pos.xz - orgPos.xz;
			float dist = length(toCenter);
			if (dist > 0.001)
			{
				float2 tang = float2(-toCenter.y, toCenter.x) / dist;
				p.vel.xz += tang * e.cSwirl * g_dt;
			}
		}

		// ★乱流（時間と位置でゆらゆら）
		if (e.cTurbulence != 0.0)
		{
			float t = g_time + p.age;
			float3 turb = float3(
				sin(t * 2.3 + p.pos.y * 3.0),
				sin(t * 1.7 + p.pos.x * 3.0),
				sin(t * 2.1 + p.pos.z * 3.0));
			p.vel += turb * e.cTurbulence * g_dt;
		}

		// ★目標への引き寄せ（cSeek）
		if (e.cSeek != 0.0)
		{
			float3 toTarget = tgtPos - p.pos;
			float dist = length(toTarget);
			if (dist > 0.001)
				p.vel += (toTarget / dist) * e.cSeek * g_dt;
		}

        // ★波打ち（cWave）
		if (e.cWave != 0.0)
		{
			p.pos.y += sin(g_time * 3.0 + p.pos.x * 2.0) * e.cWave * g_dt;
		}

        // ★らせん（cSpiral）
		if (e.cSpiral != 0.0)
		{
			float ang = g_time * 2.0 + p.age * 5.0;
			p.vel.xz += float2(cos(ang), sin(ang)) * e.cSpiral * g_dt;
		}

        // ★収束（cConverge：近づくと減速して溜まる）
		if (e.cConverge != 0.0)
		{
			float3 toTarget = tgtPos - p.pos;
			float dist = length(toTarget);
			float slow = saturate(dist * 0.5); // 近いほど0
			p.vel *= lerp(1.0, slow, saturate(e.cConverge));
		}

		p.pos += p.vel * g_dt;
		p.age += g_dt;
		p.rot += p.rotSpeed * g_dt;

        // ★3 アルファカーブ（fadeIn/fadeOut は寿命に対する割合 0〜1）
        //    両方0のときは従来挙動（saturate(life)）にフォールバック
		float total = max(p.age + p.life, 0.0001);
		float tNorm = saturate(p.age / total);

        // ★1 寿命に沿って色を更新（毎フレーム基本色から作り直すので累積しない）
		p.color.rgb = ResolveColor(e, tNorm, p.seed);

		if (e.alphaCurveOn != 0)
		{
			// ★Phase8c カーブが有効なら fadeIn/fadeOut は無視してこちらを使う
			p.color.a = EvalCurve8(e.alphaCurve, e.alphaCurvePointCount, e.alphaCurveInterp, tNorm);
		}
		else
		{
			float aIn = (e.fadeIn > 0.0001) ? saturate(tNorm / e.fadeIn) : 1.0;
			float aOut = (e.fadeOut > 0.0001) ? saturate((1.0 - tNorm) / e.fadeOut) : 1.0;
			float useCurve = ((e.fadeIn > 0.0001) || (e.fadeOut > 0.0001)) ? 1.0 : 0.0;
			p.color.a = lerp(saturate(p.life), aIn * aOut, useCurve);
		}

        // 一定間隔で現在位置を履歴に記録
		if (g_trailWrite != 0)
			g_history[i * 8 + g_trailCursor] = float4(p.pos, 1.0);

		// ★深度を使った当たり判定（スクリーン空間）
		if (g_collisionOn != 0)
		{
			float3 wpos = (e.simSpace != 0) ? (p.pos + e.pos) : p.pos;
			float4 clip = mul(float4(wpos, 1.0), g_viewProj);
			if (clip.w > 0.0)
			{
				float2 ndc = clip.xy / clip.w;
				float2 uv = ndc * float2(0.5, -0.5) + 0.5;

				if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0)
				{
					float particleViewZ = clip.w; // 透視なら w ≒ ビューZ

					int2 px = int2(uv * float2(g_screenW, g_screenH));
					px = clamp(px, int2(0, 0), int2(g_screenW - 1, g_screenH - 1));
					float4 scene = g_scenePos.Load(int3(px, 0));

                    // 背景（ワールド座標が 0）は当たらない
					float sceneViewZ = mul(float4(scene.xyz, 1.0), g_viewProj).w;
					float penetration = particleViewZ - sceneViewZ;
                    // 面の少し奥に潜り込んだら衝突（薄い範囲だけ）
					if (dot(scene.xyz, scene.xyz) > 1e-8 && penetration > 0.0 && penetration < 2.0)
					{
                        // 速度を反転して減衰（簡易跳ね返り）
						p.vel = -p.vel * g_restitution;
                        // めり込みを戻す
						p.pos += p.vel * g_dt;
					}
				}
			}
		}
	}
	g_particles[i] = p;
}
