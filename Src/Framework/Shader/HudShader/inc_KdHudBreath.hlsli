#include "inc_KdHudShader.hlsli"
#include "inc_KdHudSdf.hlsli"

#ifndef INC_KD_HUD_BREATH_HLSLI
#define INC_KD_HUD_BREATH_HLSLI

//===================================================
//
// 肺活量ゲージ(必殺技ゲージ)
//
// 夜明けを呼ぶ鳴き声のための息。
// 溜まる = 吸う / 満タン = 息を止める / 発動 = 吐き切る。
// 演出はこの3語に沿わせる。迷ったらここへ戻ること。
//
//===================================================

//---------------------------------------------------
// 寸法
//---------------------------------------------------
// 必殺技ゲージなので、HUDの中で一番読める大きさを与える。
// 太くするのではなく「大きく薄く」。太いリングは中の余白を潰す。

#define HUD_BREATH_RING    0.78f  // b.y に対する弧の半径
#define HUD_BREATH_TH      0.22f  // ringR に対する弧の太さ
#define HUD_BREATH_ICON    0.46f  // ringR に対するアイコンの大きさ
#define HUD_BREATH_CX      1.00f  // 弧の中心を左端からどれだけ離すか(b.y 倍)
#define HUD_BREATH_GAP     1.42f  // 弧の中心から HPバー左端まで(ringR 倍)

// ★大きくしすぎるとクアッドの外に出て枠が切れる。
#define HUD_BREATH_FRAME   1.12f

#define HUD_BREATH_SWELL   0.12f  // 満タン直後の膨らみ(ringR 倍)

// 入力記号。弧の内側の下。
// ★内径は ringR*0.89、アイコン下端は ringR*0.28 付近。その間に置く。
#define HUD_BREATH_MARK_Y  0.60f
#define HUD_BREATH_MARK_S  0.17f

// 必殺技の入力。0:Enter 1:Shift 2:マウス
// ★C++ の入力設定と対で直すこと。
#define HUD_BREATH_INPUT   1

#define HUD_BREATH_INTAKE  8      // 吸気粒の数
#define HUD_BREATH_STACK   6      // 羽根の積層の最大枚数

// 加算光の上限。満タンと発動が重なると白飛びして形が消える。
#define HUD_BREATH_GLOWMAX 1.75f

// 満タンの鳴き姿。アイコン(中央のニワトリ)と、内側・外側の脈動が同じ時計で動く。
#define HUD_BREATH_CRY_RATE  0.55f // 1秒あたりに鳴く回数
// ★音の弧が内壁(ringR*0.89)に触れない上限。大きくすると弧が輪へ食い込む。
#define HUD_BREATH_CRY_ICON  0.66f // ringR に対する鳴き姿の大きさ
#define HUD_BREATH_CRY_SHIFT 0.10f // 音の弧を含めた重心を中央へ戻す量(ringR 倍)

// オーラ(弧の外側)
#define HUD_BREATH_EMBER   10     // 火の粉の数

//---------------------------------------------------
// 色
//---------------------------------------------------
static const float3 kBreathFill = float3(0.42f, 0.82f, 0.95f); // 溜まっている間
static const float3 kBreathFull = float3(1.00f, 0.55f, 0.14f); // 満タン
static const float3 kBreathCold = float3(0.38f, 0.33f, 0.28f); // クールダウン
static const float3 kBreathLost = float3(1.00f, 0.45f, 0.30f); // 消費した区間

//---------------------------------------------------
// 入力記号
//---------------------------------------------------
// 「光っている」だけでは撃てることが伝わらない。
// 押す装置の形そのものを出すと、読む前に分かる。
float sdBreathInput(float2 _q, float _s)
{
#if HUD_BREATH_INPUT == 0
	return sdEnterMark(_q, _s, _s * 0.14f);
#elif HUD_BREATH_INPUT == 1
	return sdShiftMark(_q - float2(0.0f, -_s * 0.06f), _s * 0.78f);
#else
	return sdMouseBody(_q, float2(_s * 0.62f, _s * 0.88f));
#endif
}

//---------------------------------------------------
// オーラの揺らぎ
//---------------------------------------------------
// 外へ流れる値ノイズ。方向ベクトルで引くので、角度の継ぎ目(真上)が出ない。
// 流れ続けると一方向へ引き伸ばされるので、位相のずれた2層を交互に見せて巻き戻す。
float AuraNoise(float2 _dir, float _rr)
{
	const float ph0 = frac(g_time * 0.45f);
	const float ph1 = frac(ph0 + 0.5f);
	const float w0 = 1.0f - abs(ph0 * 2.0f - 1.0f); // 巻き戻る瞬間は重み0

	const float s = 2.2f + _rr * 1.5f;
	const float n0 = Noise21(_dir * (s - ph0 * 1.2f));
	const float n1 = Noise21(_dir * (s - ph1 * 1.2f) + 7.3f);
	return lerp(n1, n0, w0);
}

//---------------------------------------------------
// 層
//---------------------------------------------------
// 計算と色の載せ方を混ぜない。
// 混ぜると、層を1つ足すたびに合成順が崩れて前の演出が沈む。
struct BreathArc
{
	float2 lp; // 弧のローカル座標。アイコンが流用する
	float dist;
	float t; // 角度 0..1 (真上が0、時計回り)
	float sd; // 弧本体の符号付き距離。縁取り用
	float near; // 満タンへの近さ
	float hold; // 息を止めている度合い
	float live; // 常時表示の生存度。発動中は0へ落ちる
	float full;
	float cd; // クールダウン

	float ring;
	float base;
	float fill;
	float lost;
	float head;
	float plume;
	float tick;
	float stack; // 羽根の積層
	float intake; // 吸気粒

	float travel;
	float burst;
	float feather;
	float wing;
	float mark;
	float plate;

	float shotRing; // 発動 : 収束する輪
	float shotWave; // 発動 : 外へ抜ける波
	float shotRay; // 発動 : 放射する線
	float shot; // 発動位相 0..1

	float br; // 充填量 0..1

	// 鳴き姿の時計。アイコンと内外の脈動で共有する
	float cryOpen; // くちばしの開き 0..1(常時進む)
	float cryWave; // 音の弧の位相
	float cry; // 満タン中だけの開き。脈動に使う

	// 内側(肺)
	float inner; // 内壁より内側
	float rim; // 内壁への近さ
	float swirl; // 空気の渦
	float flood; // 満タンで中心から満ちる
	float front; // 満ちる縁の光
	float ray; // 内側の放射光
	float core; // 胸の奥の熱

	// 外側(オーラ)
	float aura; // 炎
	float halo; // 広がる光
	float ember; // 火の粉
};

//---------------------------------------------------
// 組み立て
//---------------------------------------------------
BreathArc BuildBreathArc(float2 _p, float2 _c, float _ringR, float _arcTh,
						 float _br, float _brLag, float _settle, float4 _ex2)
{
	BreathArc o = (BreathArc) 0;

	o.shot = saturate(_ex2.x);
	o.cd = saturate(_ex2.y);
	o.full = step(1.0f, _br);
	o.br = saturate(_br);

	// 鳴き姿の時計。開いて、閉じて、少し間を置く。
	// 止めずに常時進めておき、使うかどうかは満タン側で決める。
	o.cryOpen = Bump(saturate(frac(g_time * HUD_BREATH_CRY_RATE) * 2.2f));
	o.cryWave = g_time * HUD_BREATH_CRY_RATE * 1.6f;
	o.cry = o.cryOpen * o.full;

	const float intakeAmt = saturate(_ex2.z);

	// 満タンで息を止める。
	// 周りが全部動いている中でここだけ静まる方が、光らせるより目を引く。
	o.hold = o.full * saturate(_settle);

	// 発動中は常時表示を引っ込める。
	// 撃っている最中にゲージの読みを残すと、どちらを見るか分からなくなる。
	o.live = 1.0f - smoothstep(0.0f, 0.22f, o.shot);

	//-- 座標 ---------------------------------------
	// 呼吸。満タンへ向けて 0 へ寄せる。
	const float breathe = sin(g_time * 0.7f) * 0.018f * (1.0f - o.hold);

	// 満タンになった瞬間、一度だけ大きく膨らんでから戻る。
	const float swell = o.full * Bump(saturate(_settle)) * HUD_BREATH_SWELL;

	// 発動の出だしは中心へ吸い込まれる。吐き出す前に一度溜める。
	const float suck = smoothstep(0.0f, 0.18f, o.shot)
					 * (1.0f - smoothstep(0.18f, 0.42f, o.shot)) * 0.22f;

	o.lp = (_p - _c) / max(1.0f + breathe + swell - suck, 0.35f);
	o.dist = length(o.lp);

	const float ang = atan2(o.lp.x, -o.lp.y) / (HUD_PI * 2.0f);
	o.t = (ang < 0.0f) ? ang + 1.0f : ang;

	o.near = smoothstep(0.72f, 1.0f, _br);

	o.sd = abs(o.dist - _ringR) - _arcTh * 0.5f;
	o.ring = Cover(o.sd);

	//-- 下地 ---------------------------------------
	// 溜まっていない時でも動いていることが伝わる。
	// 満タンでは止め、クールダウンでは遅くする。
	{
		const float sp = 0.06f * (1.0f - o.hold) * lerp(1.0f, 0.25f, o.cd);
		const float dd = frac((o.t + g_time * sp) * 34.0f);
		const float dash = smoothstep(0.25f, 0.4f, dd) * smoothstep(0.95f, 0.8f, dd);
		const float amp = lerp(0.65f, 0.25f, o.cd); // 空の間は明暗差も縮める
		o.base = Cover(abs(o.dist - _ringR) - _arcTh * 0.30f) * (0.35f + dash * amp);
	}

	//-- 塗り ---------------------------------------
	o.fill = o.ring * smoothstep(_br + 0.006f, _br - 0.006f, o.t) * o.live;

	// 消費した区間。HPの白帯と同じ扱いで、失った量をはっきり見せる。
	// 弧の後退だけだと減ったことに気付けない。
	{
		const float lagF = o.ring * smoothstep(_brLag + 0.006f, _brLag - 0.006f, o.t);
		o.lost = saturate(lagF - o.fill) * o.live;
	}

	// 弧の中に羽根が流れる。帯と揃えた質感。満タンで止まる。
	{
		const float a2 = frac((o.t - g_time * 0.08f * (1.0f - o.hold)) * 16.0f);
		const float2 q = float2((a2 - 0.5f) * _arcTh * 3.0f, o.dist - _ringR);
		o.plume = Cover(sdSmallFeather(q, _arcTh * 1.6f, _arcTh * 0.28f)) * o.fill * 0.22f;
	}

	// 先端の光。彗星のように、進んだ側は短く、通った側へ長く引く。
	// 左右対称に光らせると、どちらへ溜まっているのかが読めない。
	o.head = o.ring * step(0.01f, _br) * o.live
		   * exp(-max(o.t - _br, 0.0f) * 60.0f)
		   * exp(-max(_br - o.t, 0.0f) * 25.0f);

	// 満タン位置(真上)の目盛り。近づくほど太く明るくする。
	o.tick = Cover(abs(o.dist - _ringR) - _arcTh * (0.85f + o.near * 0.40f))
		   * smoothstep(0.016f, 0.004f, min(o.t, 1.0f - o.t)) * o.live;

	//-- 羽根の積層 ---------------------------------
	// 溜まるほど弧の内側に羽根が重なって厚くなる。
	// 流れて消えるだけの模様と違い、量そのものが厚みとして残る。
	if (_br > 0.02f && o.live > 0.01f)
	{
		const float n = floor(_br * (float) HUD_BREATH_STACK + 0.001f);

		[unroll]
		for (int i = 0; i < HUD_BREATH_STACK; ++i)
		{
			if ((float) i >= n)
				continue;

			// 等間隔に置くと縞に見える。少しずらして重なりを作る。
			const float seed = Hash11((float) i * 7.3f);
			const float fa = ((float) i + 0.5f) / (float) HUD_BREATH_STACK;
			const float aa = fa * HUD_PI * 2.0f - HUD_PI * 0.5f;

			const float2 o2 = float2(cos(aa), sin(aa)) * (_ringR - _arcTh * 0.72f);
			float2 fq = Rotate(o.lp - o2, aa + HUD_PI * 0.5f + (seed - 0.5f) * 0.3f);

			o.stack = max(o.stack,
				Cover(sdSmallFeather(fq, _arcTh * 1.9f, _arcTh * 0.34f)));
		}
		o.stack *= o.live;
	}

	//-- 吸気粒 -------------------------------------
	// 外から弧へ空気が吸い込まれる。
	// ゲージが受け身で満ちるのではなく、能動的に奪っているように見せる。
	if (intakeAmt > 0.01f && o.live > 0.01f)
	{
		[unroll]
		for (int i = 0; i < HUD_BREATH_INTAKE; ++i)
		{
			const float seed = Hash11((float) i * 11.7f);
			const float ph = frac(g_time * (0.55f + seed * 0.35f) + seed);
			const float aa = seed * 6.283f + g_time * 0.12f;

			// 外から内へ。到着間際に細くして、吸い込まれて消える形にする。
			const float rr = lerp(_ringR * 1.62f, _ringR, ph);
			const float sz = _ringR * 0.030f * (1.0f - ph * 0.65f);

			const float2 pq = o.lp - float2(cos(aa), sin(aa)) * rr;

			o.intake = max(o.intake,
				Cover(sdSmallFeather(Rotate(pq, aa), sz * 3.4f, sz))
				* smoothstep(0.0f, 0.15f, ph));
		}
		o.intake *= intakeAmt * o.live;
	}

	//-- 満タン -------------------------------------
	// 止まるまでの名残。静止とともに消える。
	if (o.full > 0.01f && o.hold < 0.99f)
	{
		o.travel = exp(-frac(o.t - frac(g_time * 0.5f)) * 16.0f)
				 * o.ring * (1.0f - o.hold) * o.live;

		[unroll]
		for (int i = 0; i < 5; ++i)
		{
			const float ph = frac(g_time * 0.45f + (float) i * 0.2f);
			const float seed = Hash11((float) i * 3.7f);
			const float fa = seed * 6.283f + g_time * 0.2f;

			float2 fp = o.lp - float2(cos(fa), sin(fa)) * (_ringR + ph * _ringR * 0.55f);
			fp = Rotate(fp, ph * 4.0f + fa);

			o.feather = max(o.feather,
				Cover(sdSmallFeather(fp, _ringR * 0.30f, _ringR * 0.065f))
				* (1.0f - ph) * (1.0f - o.hold) * o.live);
		}
	}

	// 静止後も、ごく緩やかに外へ波が抜ける。
	// 完全に無音にすると、満タンのまま忘れられる。
	if (o.full > 0.01f && o.live > 0.01f)
	{
		// 鳴くたびに1回。口が開き切る少し前に出す。
		const float ph = frac(g_time * HUD_BREATH_CRY_RATE - 0.10f);
		o.burst = Cover(abs(o.dist - _ringR * (1.0f + ph * 0.85f)) - HUD_LINE_BASE)
				* (1.0f - ph) * o.live;
	}

	//-- 入力記号 -----------------------------------
	// 落ち着いてから出す。膨らんでいる最中に出すと、
	// どちらを見ればいいのか分からなくなる。
	if (o.full > 0.01f && o.live > 0.01f)
	{
		const float vis = smoothstep(0.20f, 0.75f, saturate(_settle));
		if (vis > 0.01f)
		{
			const float s = _ringR * HUD_BREATH_MARK_S;

			// 行き過ぎてから収まる。出現そのものを演出にする。
			const float k = max(EaseOutBack(vis), 0.05f);
			const float2 q = (o.lp - float2(0.0f, _ringR * HUD_BREATH_MARK_Y)) / k;

			o.plate = Cover(sdRoundBox(q, float2(s * 0.98f, s * 1.08f), s * 0.36f))
					* vis * o.live;
			o.mark = Cover(sdBreathInput(q, s)) * vis * o.live;
		}
	}

	//-- 内側 ---------------------------------------
	// 弧の内側は「肺」。溜まる間は内壁へ圧がかかり、
	// 満タンになると中心から熱が満ちて、息を止めたまま鳴く直前の張りつめた絵になる。
	// 層はアイコンより下に敷くので、ここで明るくしすぎるとニワトリが沈む。
	{
		const float innerR = _ringR - _arcTh * 0.5f;
		const float rn = o.dist / max(innerR, 1e-3f); // 0 中心 .. 1 内壁

		o.inner = Cover(o.dist - innerR) * o.live;
		o.rim = smoothstep(0.35f, 1.0f, rn);

		// 空気の渦。溜まるほど濃い。満タンでは息を止めてほぼ静止、クールダウンで鈍る。
		// 腕の数を整数にしてあるので、真上に継ぎ目は出ない。
		const float sp = lerp(1.1f, 0.2f, o.hold) * lerp(1.0f, 0.3f, o.cd);
		const float arm = 0.5f + 0.5f * sin((o.t * 3.0f + rn * 1.1f) * HUD_PI * 2.0f
										   - g_time * sp * 2.0f);
		o.swirl = arm * arm * smoothstep(0.25f, 0.95f, rn) * o.br * o.inner;

		// 満タンの瞬間、中心から縁へ向かって色が満ちる。
		// settle が 0 → 0.45 の間に広がり切る。以降は静止。
		const float fk = smoothstep(0.0f, 0.45f, saturate(_settle));
		const float fr = innerR * (0.02f + fk * 1.35f);

		o.flood = o.full * (1.0f - smoothstep(fr * 0.80f, fr, o.dist)) * o.inner;
		o.front = o.full * smoothstep(fr * 0.62f, fr * 0.85f, o.dist)
				* (1.0f - smoothstep(fr * 0.85f, fr, o.dist))
				* (1.0f - smoothstep(0.7f, 1.0f, fk)) * o.inner;

		// 満ちた後の内側。放射光は壁際、熱はアイコンの背後。
		// 12本は整数なので継ぎ目なし。ごくゆっくり回す。
		const float rs = frac(o.t * 12.0f + g_time * 0.03f);
		const float spoke = smoothstep(0.30f, 0.50f, rs) * smoothstep(0.70f, 0.50f, rs);

		o.ray = spoke * smoothstep(0.50f, 0.68f, rn)
			  * (1.0f - smoothstep(0.84f, 1.0f, rn)) * o.flood;
		o.core = exp(-rn * rn * 2.6f) * o.flood;
	}

	//-- オーラ -------------------------------------
	// 溜まった区間の弧から立ち上る。満タンで全周。
	// 発生する角度を充填と同じ範囲に限るので、炎の広がりでも「どこまで溜まったか」が読める。
	// ★右側(HPバー側)は切る。バーの左端は ringR*1.42。
	// ★外径は ringR*1.6 まで。クアッドの縦は ringR*1.86 しかなく、越えると端で切れる。
	if (o.br > 0.03f && o.live > 0.01f)
	{
		const float amt = (0.25f + 0.75f * o.br) * (1.0f + o.hold * 0.35f);

		// 先端が通った所だけ。満タンは継ぎ目でも欠けないよう全周に揃える。
		const float am = max(smoothstep(_br + 0.015f, _br - 0.03f, o.t), o.full);

		const float2 dir = o.lp / max(o.dist, 1e-3f);
		const float rr = o.dist / _ringR;

		const float outer = smoothstep(0.95f, 1.06f, rr);
		const float reach = (1.0f - smoothstep(1.34f, 1.60f, rr))
						  * (1.0f - smoothstep(1.12f, 1.40f, o.lp.x / _ringR));
		const float mask = outer * reach * am;

		// 炎。ノイズが高い方向ほど長く伸びる。
		const float n = AuraNoise(dir, rr);
		const float u = saturate((rr - 1.02f) / 0.52f);
		const float h = 0.16f + 0.84f * smoothstep(0.22f, 0.85f, n) * amt;

		o.aura = pow(saturate(1.0f - u / h), 1.6f) * mask * amt;

		// 広がる光。炎の根元をぼかして繋ぐ。
		o.halo = exp(-max(rr - 1.05f, 0.0f) * 3.4f) * mask * amt;

		// 火の粉。溜まった範囲のどこかから湧き、外へ抜けて消える。
		// 湧く場所は周期ごとに引き直す。固定すると同じ点から出続けて見える。
		[unroll]
		for (int i = 0; i < HUD_BREATH_EMBER; ++i)
		{
			const float seed = Hash11((float) i * 5.3f + 1.7f);
			const float clk = g_time * (0.32f + seed * 0.26f) + seed * 5.0f;
			const float ph = frac(clk);
			const float tt = Hash11((float) i * 9.1f + floor(clk) * 3.7f) * o.br;
			const float aa = tt * HUD_PI * 2.0f - HUD_PI * 0.5f;

			const float er = _ringR * (1.06f + ph * 0.50f);
			const float sz = _ringR * 0.032f * (1.0f - ph * 0.6f);

			const float2 pq = o.lp - float2(cos(aa), sin(aa)) * er;

			o.ember = max(o.ember,
				Cover(sdSmallFeather(Rotate(pq, aa), sz * 3.6f, sz))
				* smoothstep(0.0f, 0.12f, ph) * (1.0f - ph));
		}
		o.ember *= amt * reach;
	}

	//-- 発動 ---------------------------------------
	// 弧が中心へ解けて集まり、そこから外へ抜ける。
	// HUDの中に発射の起点を作ると、画面の演出と繋がって見える。
	if (o.shot > 0.001f)
	{
		// 収束。半径が縮むぶん、太さを増して質量を保つ。
		{
			const float k = smoothstep(0.0f, 0.34f, o.shot);
			const float rr = _ringR * (1.0f - k * 0.86f);
			const float th = _arcTh * (0.5f + k * 1.9f);
			o.shotRing = Cover(abs(o.dist - rr) - th * 0.5f)
					   * (1.0f - smoothstep(0.30f, 0.44f, o.shot));
		}

		// 爆発。外へ抜ける厚い波。
		if (o.shot > 0.26f)
		{
			const float k = smoothstep(0.26f, 1.0f, o.shot);
			const float rr = _ringR * (0.1f + k * 2.60f);
			const float th = HUD_LINE_BOLD * (2.6f - k * 2.0f);
			o.shotWave = Cover(abs(o.dist - rr) - th) * (1.0f - k);
		}

		// 放射する線。息が前へ抜ける鋭さ。
		if (o.shot > 0.24f && o.shot < 0.78f)
		{
			const float k = smoothstep(0.24f, 0.78f, o.shot);
			const float sp = frac(o.t * 12.0f);
			const float spoke = smoothstep(0.42f, 0.5f, sp) * smoothstep(0.58f, 0.5f, sp);

			o.shotRay = spoke
					  * smoothstep(_ringR * 0.1f, _ringR * (0.4f + k * 1.6f), o.dist)
					  * (1.0f - smoothstep(_ringR * (0.5f + k * 2.0f),
										   _ringR * (0.9f + k * 2.4f), o.dist))
					  * (1.0f - k);
		}
	}
	return o;
}

//---------------------------------------------------
// 合成 : 常時表示ぶん
//---------------------------------------------------
// col は HP の上・アイコンの下に載せる。
void ComposeBreathArc(BreathArc _arc, float4 _main, float4 _sub, float _spend,
					  inout float3 _col, inout float _a)
{
	// 溜まっている間と満タンで色を分ける。
	// クールダウン中は彩度を落とす。「空」を色でも伝える。
	float3 brCol = (_arc.full > 0.5f) ? kBreathFull : kBreathFill;
	brCol = lerp(brCol, kBreathCold, _arc.cd * 0.8f);

	const float spendPulse = 0.55f + sin(g_time * 9.0f) * 0.45f;

	// 加算光は一度貯めてから上限を掛ける。
	// 満タンと発動が重なると白飛びして形が消える。
	float3 glow = float3(0, 0, 0);

	// 内側に敷く色。満タンでは中心から橙が満ちていくので、弧全体の色とは別に決める。
	float3 inCol = lerp(kBreathFill, kBreathFull, _arc.flood);
	inCol = lerp(inCol, kBreathCold, _arc.cd * 0.8f);

	// 鳴くたびに内も外も脈打つ。アイコンの口の開きと同じ時計。
	const float throb = 1.0f + _arc.cry * 0.35f;
	const float inPulse = 0.72f + 0.28f * _arc.cryOpen;

	// オーラ。弧の外側なので最背面。
	// 根元は明るく、先へ行くほど弧の色に落ちる。
	{
		const float3 hot = lerp(brCol, float3(1.0f, 0.95f, 0.82f), 0.55f);
		const float aw = saturate(_arc.aura * throb);

		_col = lerp(_col, lerp(brCol, hot, smoothstep(0.5f, 1.0f, _arc.aura)),
					saturate(aw * 0.9f + _arc.halo * 0.25f));
		_col = lerp(_col, lerp(brCol, float3(1, 1, 1), 0.5f), _arc.ember);

		glow += brCol * (aw * 0.55f + _arc.halo * 0.45f * throb + _arc.ember * 0.9f);
	}

	// 内側。溜まる間は内壁の圧と薄い渦、満タンは満ちた熱と放射光。
	// アイコンはこの後で上に載るので、ここは「背景」として抑える。
	{
		const float press = _arc.inner * _arc.br * (0.08f + _arc.rim * _arc.rim * 0.42f);

		_col = lerp(_col, inCol * 0.42f, saturate(press + _arc.flood * 0.55f));
		_col = lerp(_col, inCol, _arc.swirl * 0.16f);

		glow += inCol * (_arc.swirl * 0.10f
					   + (_arc.ray * 0.30f + _arc.core * 0.45f) * inPulse
					   + _arc.front * 1.1f);
	}

	// 吸気粒。弧の外側なので先に敷く。
	_col = lerp(_col, lerp(brCol, float3(1, 1, 1), 0.35f), _arc.intake * 0.75f);

	// 翼
	_col = lerp(_col, kBreathFull * 0.85f, _arc.wing * 0.55f);
	glow += kBreathFull * _arc.wing * 0.35f;

	// 消費した区間は赤みを帯びて明滅する。減ったことに気付ける。
	_col = lerp(_col, lerp(_sub.rgb * 2.4f, kBreathLost, _spend * spendPulse), _arc.lost);

	// 塗りと、その内側の羽根の厚み
	_col = lerp(_col, brCol * 0.55f, _arc.stack * 0.5f);
	_col = lerp(_col, brCol, _arc.fill);
	glow += brCol * (_arc.plume + _arc.head * 0.9f + _arc.travel * 1.3f + _arc.burst * 0.8f);

	_col = lerp(_col, float3(1, 1, 1),
				max(_arc.tick * (0.35f + _arc.near * 0.45f), _arc.feather * 0.6f));

	// 入力記号。下敷きを暗く敷いてから白で抜く。
	// 弧の上に直接置くと、満タンの明るさに溶けて読めない。
	_col = lerp(_col, float3(0.03f, 0.03f, 0.04f), _arc.plate * 0.85f);
	_col = lerp(_col, float3(1, 1, 1), _arc.mark);
	glow += brCol * _arc.mark * (0.25f + sin(g_time * 3.0f) * 0.15f);

	_col += min(glow, HUD_BREATH_GLOWMAX);

	//-- アルファ -----------------------------------
	_a = max(_a, _arc.aura * 0.75f);
	_a = max(_a, _arc.halo * 0.32f);
	_a = max(_a, _arc.ember * 0.90f);
	_a = max(_a, _arc.inner * _arc.br * (0.10f + _arc.rim * 0.16f));
	_a = max(_a, _arc.flood * (0.36f + _arc.core * 0.18f));
	_a = max(_a, _arc.front * 0.80f);
	_a = max(_a, _arc.ray * 0.50f);

	_a = max(_a, _arc.base * _sub.a * 0.6f);
	_a = max(_a, _arc.intake * 0.55f);
	_a = max(_a, _arc.wing * 0.60f);
	_a = max(_a, _arc.stack * 0.45f);
	_a = max(_a, _arc.fill * _main.a);
	_a = max(_a, _arc.lost * (0.7f + _spend * 0.3f));
	_a = max(_a, _arc.head * 0.85f);
	_a = max(_a, _arc.tick * (0.5f + _arc.near * 0.45f));
	_a = max(_a, _arc.travel);
	_a = max(_a, _arc.burst * 0.7f);
	_a = max(_a, _arc.feather * 0.85f);
	_a = max(_a, _arc.plate * 0.70f);
	_a = max(_a, _arc.mark);
}

//---------------------------------------------------
// 合成 : 一発の演出
//---------------------------------------------------
// 縁取りとハローより後に載せる。
// 一発ものが線の下に潜ると、一番見せたい瞬間に見えなくなる。
void ComposeBreathOneShot(BreathArc _arc, float _ringR, float _br,
						  float _spend, float _flash,
						  inout float3 _col, inout float _a)
{
	float3 glow = float3(0, 0, 0);

	// 肺活量を使い切った瞬間。弧が一周して消える。
	// 減り切ったことが弧の後退だけでは分かりにくい。
	if (_spend > 0.001f && _br < 0.03f && _arc.shot < 0.001f)
	{
		const float head2 = frac(1.0f - _spend);
		const float sweep = exp(-frac(_arc.t - head2) * 10.0f) * _arc.ring * _spend;

		_col = lerp(_col, kBreathLost, saturate(sweep));
		_a = max(_a, sweep);
	}

	// 満タンになった瞬間。弧が白く飛び、外へ衝撃波が抜ける。
	// 「満タンになった」を画面の端からでも分からせる一発。ここは控えめにしない。
	_col = lerp(_col, float3(1, 1, 1), saturate(_arc.ring * _flash * 1.6f));
	glow += kBreathFull * _flash * 1.7f;
	_a = max(_a, _arc.ring * _flash);

	if (_flash > 0.001f)
	{
		const float k = 1.0f - _flash; // 0 出だし → 1 終わり
		const float ds = _arc.dist / _ringR;

		// ★外径は ringR*1.65 まで。クアッドの縦は ringR*1.86 で、膨らみも掛かる。
		const float fade = 1.0f - smoothstep(1.50f, 1.72f, ds);

		// 3重の衝撃波。時間差で追わせ、先頭ほど速く太く、後続ほど細く短くする。
		// 出だしが最速になる減速曲線。一瞬で外へ抜け、その後はゆっくり余韻になる。
		float edge = 0.0f;
		float wake = 0.0f;

		[unroll]
		for (int i = 0; i < 3; ++i)
		{
			const float kd = saturate((k - (float) i * 0.10f) / (1.0f - (float) i * 0.10f));
			const float e = 1.0f - pow(1.0f - kd, 3.0f);
			const float amp = pow(1.0f - kd, 1.4f) * (1.0f - (float) i * 0.22f);

			const float rr = _ringR * (0.95f + e * (0.68f - (float) i * 0.07f));
			const float w = _ringR * (0.012f + 0.075f * (1.0f - kd) * (1.0f - kd));

			edge = max(edge, Cover(abs(_arc.dist - rr) - w) * amp);

			// 通り過ぎた後ろへ引く尾。内壁より内側(アイコンの上)へは入れない。
			wake = max(wake, exp(-max(rr - _arc.dist, 0.0f) / (_ringR * 0.30f))
						   * step(_arc.dist, rr + w)
						   * smoothstep(_ringR * 0.85f, _ringR * 1.0f, _arc.dist) * amp);
		}

		// 放射する刃。長さを1本ずつ変えて、整った星形にしない。
		const float sc = _arc.t * 18.0f;
		const float sp = frac(sc);
		const float spoke = smoothstep(0.38f, 0.50f, sp) * smoothstep(0.62f, 0.50f, sp);
		const float len = 0.5f + 0.5f * Hash11(floor(sc) * 3.1f);
		const float e0 = 1.0f - pow(1.0f - k, 3.0f);
		const float tip = 1.10f + len * 0.50f * e0;

		const float spike = spoke * smoothstep(1.04f, 1.12f, ds)
						  * (1.0f - smoothstep(tip * 0.80f, tip, ds))
						  * pow(_flash, 1.2f);

		edge *= fade;
		wake *= fade;
		const float sk = spike * fade;

		// 先頭は白、尾は弧の橙。
		_col = lerp(_col, float3(1, 1, 1), saturate(edge * 1.2f + sk * 0.8f));
		glow += kBreathFull * (edge * 1.3f + wake * 1.5f + sk * 1.2f);

		_a = max(_a, edge);
		_a = max(_a, wake * 0.75f);
		_a = max(_a, sk * 0.85f);
	}

	// 発動。収束して、外へ吐き切る。
	if (_arc.shot > 0.001f)
	{
		_col = lerp(_col, float3(1, 1, 1), saturate(_arc.shotRing));
		glow += kBreathFull * (_arc.shotRing * 1.3f
							 + _arc.shotWave * 1.5f
							 + _arc.shotRay * 0.9f);

		_a = max(_a, _arc.shotRing);
		_a = max(_a, _arc.shotWave * 0.92f);
		_a = max(_a, _arc.shotRay * 0.55f);
	}

	_col += min(glow, HUD_BREATH_GLOWMAX);
}

#endif // INC_KD_HUD_BREATH_HLSLI
