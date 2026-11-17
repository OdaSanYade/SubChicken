#include "inc_KdHudShader.hlsli"
#include "inc_KdHudSdf.hlsli"
#include "inc_KdHudFont.hlsli"
#include "inc_KdHudBreath.hlsli"

//===================================================
// 種別ごとの描画
//===================================================
float4 DrawLock(VSOutput In)
{
	const float R = min(In.HalfSize.x, In.HalfSize.y) * 0.9f;
	const float len = R * 0.62f;
	const float wid = R * 0.16f;
	// 0で閉じ切り、1で開き切り。捕捉で開き、外すと閉じる。
	const float open = saturate(In.Params.x);
	const float inRange = saturate(In.Params.y); // 踏み込みが届くか

	float2 p = Rotate(In.Local, In.Params.z);

	// 開閉
	const float spread = lerp(0.05f, 0.42f, EaseOutBack(open));
	const float scale = lerp(0.45f, 1.0f, open);

	float d = 1e9f;
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		const float a = HUD_PI * 0.5f * (float) i + HUD_PI * 0.25f;
		float2 q = Rotate(p, -a);
		q.x -= R * spread;
		d = min(d, sdFeather(q, len * scale, wid * scale));
	}

	const float feather = Cover(d);
	const float outline = Stroke(d, HUD_LINE_BASE);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK, HUD_EDGE_HALO);

	// 中心の照準。十字と点。
	const float cross = Cover(min(
		sdBox(In.Local, float2(R * 0.16f, HUD_LINE_THIN)),
		sdBox(In.Local, float2(HUD_LINE_THIN, R * 0.16f))));
	const float core = Cover(sdCircle(In.Local, 2.4f));

	// 羽根がゆっくり呼吸する
	const float breathe = 0.85f + sin(g_time * 0.7f) * 0.15f;
	// 中心の点は明滅。狙いが定まっている印。
	const float blink = 0.72f + sin(g_time * 2.9f) * 0.28f;

	// 開き切る手前だけ外周を光が一周する
	float sweep = 0.0f;
	if (open > 0.05f && open < 0.999f)
	{
		float t = atan2(In.Local.x, -In.Local.y) / (HUD_PI * 2.0f);
		t = (t < 0.0f) ? t + 1.0f : t;
		sweep = Cover(abs(length(In.Local) - R * 1.02f) - HUD_LINE_THIN)
			  * exp(-frac(t - open) * 10.0f) * (1.0f - open);
	}

	// 射程外は色が沈み、羽根が薄くなる。
	// 踏み込みが届かない相手をロックしていると、押した時に空振りする。
	const float3 baseCol = lerp(float3(0.55f, 0.55f, 0.58f), In.Color.rgb, inRange);

	float3 col = baseCol;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, In.SubColor.rgb, max(core, max(cross * 0.6f, edge.y * 0.6f)));
	col = lerp(col, baseCol * 1.4f, outline * 0.6f);

	float a = edge.x * 0.62f;
	a = max(a, edge.y * 0.24f);
	a = max(a, feather * 0.45f * breathe);
	a = max(a, outline * 0.95f);
	a = max(a, cross * 0.5f);
	a = max(a, core * blink);
	a = max(a, sweep * 0.85f);

	// 射程外は全体を薄くする
	a *= lerp(0.45f, 1.0f, inRange);

	// 閉じ切ると消える
	a *= smoothstep(0.0f, 0.25f, open);

	return float4(col, a * In.Color.a);
}

// 敵のHP/体幹ゲージ
float4 DrawEnemyBar(VSOutput In)
{
	// クアッドは数値を置くぶん広げてある。帯の寸法へ戻す
	const float2 b = In.HalfSize / float2(HUD_ENEMYBAR_EXPAND_X, HUD_ENEMYBAR_EXPAND_Y);

	const float hp = saturate(In.Params.x);
	const float poise = saturate(In.Params.y);
	const float special = saturate(In.Params.z);
	const float breakFlash = saturate(In.Params.w); // 崩した瞬間(1→0)

	const float locked = saturate(In.Extra.x); // ロック中
	
	const float stagger = saturate(In.Extra.y); // 硬直・ダウンの残り(0〜1)
	const float death = saturate(In.Extra.z); // 撃破の爆散(1→0)

	// 帯はクアッドの下寄せ。上に空いた領域へ数値を置く。
	float2 P = In.Local - float2(0.0f, In.HalfSize.y - b.y * 1.20f);

	// 凍結中はゆっくり上下する。止まっていることが動きで伝わる。
	P.y -= special * sin(g_time * 1.3f) * b.y * 0.12f;

	// 崩した瞬間はゲージ本体が一度縮んで戻る
	P *= (1.0f + breakFlash * 0.22f);

	// 爆散中は破片が外まで飛ぶので、本体側を縮めて余白を作る。
	P *= (1.0f + saturate(In.Extra.z) * 0.9f);

	//------------------------------------------
	// 帯の形
	//------------------------------------------
	const float hpY = -b.y * 0.30f;
	const float hpH = b.y * 0.34f;
	const float2 hbp = SkewBar(P - float2(0.0f, hpY), hpH);
	const float hpBoxD = sdBox(hbp, float2(b.x * 0.86f, hpH));

	const float pY = b.y * 0.42f;
	const float pH = b.y * 0.22f;
	const float2 pbp = SkewBar(P - float2(0.0f, pY), pH);
	const float pBoxD = sdBox(pbp, float2(b.x * 0.86f, pH));

	//------------------------------------------
	// 二重縁取り
	//------------------------------------------
	const float2 hpEdge = OutlineDual(hpBoxD, HUD_EDGE_DARK, HUD_EDGE_HALO);
	const float2 pEdge = OutlineDual(pBoxD, HUD_EDGE_DARK * 0.75f, HUD_EDGE_HALO)
					   * step(0.01f, poise);
	const float2 edge = max(hpEdge, pEdge);

	//------------------------------------------
	// 枠 : 両端の鉤と鋲
	//------------------------------------------
	const float tx = b.x * 0.92f;
	const float ty0 = hpY - hpH * 1.5f;
	const float ty1 = pY + pH * 1.7f;

	float ticks = Cover(sdSegment(P, float2(-tx, ty0), float2(-tx, ty1)) - HUD_LINE_BASE * 0.6f);
	ticks = max(ticks, Cover(sdSegment(P, float2(tx, ty0), float2(tx, ty1)) - HUD_LINE_BASE * 0.6f));
	ticks = max(ticks, Cover(sdSegment(P,
		float2(-tx, ty0), float2(-tx + b.x * 0.22f, ty0)) - HUD_LINE_BASE * 0.6f));
	ticks = max(ticks, Cover(sdSegment(P,
		float2(tx, ty0), float2(tx - b.x * 0.22f, ty0)) - HUD_LINE_BASE * 0.6f));

	float rivet = Cover(sdRivet(P - float2(-tx, ty0), b.y * 0.11f));
	rivet = max(rivet, Cover(sdRivet(P - float2(tx, ty0), b.y * 0.11f)));

	const float2 tickEdge = OutlineDual(
		sdSegment(P, float2(-tx, ty0), float2(-tx, ty1)) - HUD_LINE_BASE * 0.6f,
		HUD_EDGE_DARK * 0.75f, HUD_EDGE_HALO * 0.6f);

	//------------------------------------------
	// 中身
	//------------------------------------------
	const float hpBg = Cover(hpBoxD);
	const float hpX = -b.x * 0.86f + b.x * 1.72f * hp;
	const float hpFill = Cover(max(hpBoxD, hbp.x - hpX));
	const float hpHead = hpBg * exp(-abs(hbp.x - hpX) * 0.7f);

	// HPにも分節を入れる。あと何発耐えるかが読める。
	const float hpNotch = hpBg * smoothstep(0.035f, 0.0f,
		abs(frac((hbp.x + b.x * 0.86f) / (b.x * 1.72f) * 5.0f) - 0.5f) - 0.465f);

	const float pBg = Cover(pBoxD) * step(0.01f, poise);
	const float pX = -b.x * 0.86f + b.x * 1.72f * poise;
	const float pFill = Cover(max(pBoxD, pbp.x - pX)) * step(0.01f, poise);

	// 先端の光。溜まっている方向が見える。
	const float pHead = pBg * exp(-abs(pbp.x - pX) * 0.7f);

	// 体幹の目盛り。4分割して量を読めるようにする。
	const float pNotch = pBg * smoothstep(0.04f, 0.0f,
		abs(frac((pbp.x + b.x * 0.86f) / (b.x * 1.72f) * 4.0f) - 0.5f) - 0.46f);

	//------------------------------------------
	// 撃破の爆散
	//------------------------------------------
	float burst = 0.0f;
	float burstWhite = 0.0f;

	if (death > 0.001f)
	{
		const float k = 1.0f - death; // 0(撃破の瞬間)→1(消える)

		// 帯を縦に割った破片。上へ飛んで落ちる。
		[unroll]
		for (int i = 0; i < 7; ++i)
		{
			const float fx = (-3.0f + (float) i) * b.x * 0.245f;
			const float sp = Hash11((float) i * 3.3f);
			const float side = (fx >= 0.0f) ? 1.0f : -1.0f;

			// 放物線。横へ散りながら落下する。
			const float2 o = float2(side * k * b.x * (0.6f + sp * 1.1f),
									-k * b.y * 1.6f + k * k * b.y * 4.2f);

			// 飛びながら回る
			float2 q = Rotate(P - float2(fx, hpY) - o, k * (sp - 0.5f) * 7.0f);

			burst = min(burst, 0.0f); // 初期化代わり
			const float pd = sdBox(q, float2(b.x * 0.105f, hpH * 0.95f));
			burst = max(burst, Cover(pd) * death);
		}

		// 撃破の瞬間だけ白く飛ぶ
		burstWhite = Cover(hpBoxD - (1.0f - k) * b.y * 0.6f)
				   * smoothstep(0.6f, 1.0f, death);

		// 外へ抜ける衝撃の輪
		burst = max(burst,
			Cover(abs(length(P - float2(0.0f, hpY)) - b.x * (0.15f + k * 1.15f))
				- HUD_LINE_BOLD * 0.6f) * death * 0.8f);
	}

	// 爆散中は通常のゲージを消す。破片と二重に出ると何が起きたか読めない。
	const float alive = 1.0f - death;

	//------------------------------------------
	// ロック中の強調
	//------------------------------------------
	float lockFrame = 0.0f;
	if (locked > 0.5f)
	{
		const float2 fb = float2(b.x * 0.98f, b.y * 0.92f);
		lockFrame = Stroke(sdBox(P, fb), HUD_LINE_BASE);

		// 四隅の鉤を伸ばす
		lockFrame = max(lockFrame,
			Cover(sdCornerTick(P, fb, b.y * 0.42f, HUD_LINE_BASE * 0.6f)));
	}

	//------------------------------------------
	// 硬直中の印
	//------------------------------------------
	float stunMark = 0.0f;
	if (stagger > 0.001f)
	{
		const float sp = g_time * (1.0f + stagger * 2.0f);

		[unroll]
		for (int i = 0; i < 3; ++i)
		{
			const float a2 = sp + (float) i * (HUD_PI * 2.0f / 3.0f);
			const float2 o = float2(cos(a2) * b.x * 0.30f, sin(a2) * b.y * 0.22f);
			stunMark = max(stunMark,
				Cover(sdCircle(P - float2(0.0f, -b.y * 1.15f) - o,
							   b.y * 0.13f * (0.4f + stagger * 0.6f))));
		}
	}

	//------------------------------------------
	// 崩壊間近の警告
	//------------------------------------------
	float alert = 0.0f;
	if (poise > 0.8f)
	{
		const float t = (poise - 0.8f) / 0.2f;
		const float blink = 0.5f + sin(g_time * (7.0f + t * 9.0f)) * 0.5f;
		alert = Stroke(pBoxD, HUD_LINE_BOLD + t * 2.0f) * blink * t;
	}

	//------------------------------------------
	// 崩した瞬間
	//------------------------------------------
	float shatter = 0.0f;
	float shatterWhite = 0.0f;

	if (breakFlash > 0.001f)
	{
		const float k = breakFlash; // 1→0
		const float grow = 1.0f - k; // 0→1

		// ゲージ全体が一瞬白く膨らむ
		const float swell = Cover(pBoxD - k * b.y * 0.55f);
		shatterWhite = swell * k;

		// 縦に裂ける。割れ目が広がっていく。
		[unroll]
		for (int i = 0; i < 5; ++i)
		{
			const float x = (-2.0f + (float) i) * b.x * 0.34f;
			float2 cq = pbp - float2(x, 0.0f);
			cq.x += sin(cq.y * 2.4f) * 2.2f;
			shatter = max(shatter,
				Cover(sdBox(cq, float2(HUD_LINE_BOLD * (0.4f + grow * 0.9f), pH * 1.4f))) * k);
		}

		// 外へ飛ぶ破片。大きく遠くまで散らす。
		[unroll]
		for (int j = 0; j < 8; ++j)
		{
			const float a2 = Hash11((float) j * 3.1f) * 6.283f;
			const float sp = 0.6f + Hash11((float) j * 7.7f) * 0.8f;
			const float2 o = float2(cos(a2), sin(a2) * 0.7f) * grow * b.x * 1.1f * sp;

			shatter = max(shatter,
				Cover(sdRivet(P - float2(0.0f, pY) - o, b.y * 0.17f * k)) * k);
		}

		// 外へ抜ける衝撃の輪
		const float ring = Cover(abs(length(P - float2(0.0f, pY))
								- b.x * (0.2f + grow * 1.0f)) - HUD_LINE_BOLD * 0.7f) * k;
		shatter = max(shatter, ring);
	}

	//------------------------------------------
	// 特殊状態(凍結)
	//------------------------------------------
	float frost = 0.0f;
	if (special > 0.01f)
	{
		// 残り時間が減るほど結晶が短くなる
		const float grow = (0.7f + sin(g_time * 1.9f) * 0.3f) * (0.35f + special * 0.65f);
		[unroll]
		for (int i = 0; i < 6; ++i)
		{
			const float fx = (-2.5f + (float) i) * b.x * 0.3f;
			const float fy = (fmod((float) i, 2.0f) < 0.5f) ? -b.y : b.y;
			float2 q = Rotate(P - float2(fx, fy), 0.6f + (float) i);
			frost = max(frost, Cover(sdBox(q,
				float2(HUD_LINE_THIN, b.y * 0.34f * special * grow))));
		}
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float3 poiseCol = (poise > 0.8f)
		? lerp(float3(1.00f, 0.86f, 0.30f), float3(1, 1, 1), (poise - 0.8f) / 0.2f)
		: float3(1.00f, 0.62f, 0.06f);

	// 凍結中はHPが氷色へ寄る
	const float3 iceCol = float3(0.42f, 0.82f, 1.00f);
	const float3 hpCol = lerp(In.Color.rgb, iceCol, special * 0.85f);

	float3 col = In.SubColor.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, hpCol, hpFill);
	col += hpCol * hpHead * 0.8f;
	col = lerp(col, In.SubColor.rgb * 0.35f, hpNotch * hpFill * 0.7f);
	col = lerp(col, poiseCol, pFill);
	col += poiseCol * pHead * 0.7f;
	col = lerp(col, In.SubColor.rgb * 0.4f, pNotch * pFill * 0.7f);
	col = lerp(col, iceCol, frost * 0.9f);
	col = lerp(col, float3(1, 1, 1), max(max(alert, max(shatter, shatterWhite)), edge.y * 0.5f));
	col = lerp(col, In.Color.rgb * 0.9f, max(ticks, rivet) * 0.5f);
	col = lerp(col, float3(1.00f, 0.92f, 0.55f), lockFrame);
	col = lerp(col, float3(1.00f, 0.95f, 0.70f), stunMark);
	col = lerp(col, float3(1, 1, 1), max(burstWhite, burst * 0.35f));
	
	float a = 0.0f;

	// 生きている間の表示。爆散中は消す。
	{
		float live = edge.x * 0.95f;
		live = max(live, edge.y * 0.34f);
		live = max(live, max(tickEdge.x, tickEdge.y * 0.4f) * 0.8f);
		live = max(live, hpBg * In.SubColor.a);
		live = max(live, hpFill * In.Color.a);
		live = max(live, hpHead * 0.85f);
		live = max(live, pBg * In.SubColor.a * 0.9f);
		live = max(live, pFill); // 体幹は色そのままの濃さで出す
		live = max(live, pHead * 0.85f);
		live = max(live, ticks * 0.8f);
		live = max(live, rivet * 0.9f);
		live = max(live, alert * In.Color.a);
		live = max(live, shatter);
		live = max(live, shatterWhite);
		live = max(live, frost * 0.9f);
		live = max(live, lockFrame * 0.9f);
		live = max(live, stunMark * 0.95f);

		a = live * alive;
	}

	// 爆散
	a = max(a, burst);
	a = max(a, burstWhite);

	return float4(col, a);
}

// 攻撃予兆
float4 DrawWarn(VSOutput In)
{
	const float R = In.Params.y;
	const float prog = saturate(In.Params.x);
	const float flash = saturate(In.Params.z);

	// 攻撃の種別。0=通常(回避可能) 1=回避不能。
	// 敵の種類が増えたとき、リングの見た目で読み分けられるようにしておく。
	const float unblockable = saturate(In.Params.w);

	// 攻撃力の目安(0〜1)。リングの太さで示す。
	// 数値を出すより、太さの違いの方が一瞬で読める。
	const float power = saturate(In.Extra.x);

	const float dist = length(In.Local);

	//------------------------------------------
	// 外枠 : 常に出ている的
	//------------------------------------------
	const float baseD = abs(dist - R) - HUD_LINE_BASE * 0.5f;
	const float baseRing = Cover(baseD);
	const float2 edge = OutlineDual(baseD, HUD_EDGE_DARK, HUD_EDGE_HALO);

	// 四隅の鉤。的であることを強調する。
	float corner = 0.0f;
	{
		float t = atan2(In.Local.x, -In.Local.y) / (HUD_PI * 2.0f);
		t = (t < 0.0f) ? t + 1.0f : t;

		float wedge = 0.0f;
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			const float ca = (float) i * 0.25f;
			wedge = max(wedge, smoothstep(0.055f, 0.022f, abs(frac(t - ca + 0.5f) - 0.5f)));
		}
		corner = Cover(abs(dist - R * 1.28f) - HUD_LINE_BASE) * wedge;
	}

	//------------------------------------------
	// 収束リング : 溜めの進捗
	//------------------------------------------
	const float closeR = lerp(R * 1.9f, R * 0.26f, prog);
	float closing = Cover(abs(dist - closeR)
					- (HUD_LINE_BOLD * (0.5f + power * 0.8f) + prog * 1.8f));

	// 回避不能は破線にする。色ではなく線種で分けると、
	// 色覚に頼らずに読めるうえ、背景色にも影響されない。
	if (unblockable > 0.5f)
	{
		float t2 = atan2(In.Local.x, -In.Local.y) / (HUD_PI * 2.0f);
		t2 = (t2 < 0.0f) ? t2 + 1.0f : t2;

		const float dash2 = frac(t2 * 12.0f);
		closing *= step(0.42f, dash2);

		// 内側にもう1本。二重線で危険を示す。
		closing = max(closing,
			Cover(abs(dist - closeR * 0.78f) - HUD_LINE_BASE * 0.6f) * 0.8f);
	}

	// 収束リングの内側を薄く塗る。溜まっている量が面でも読める。
	const float charge = Cover(dist - closeR) * (0.10f + prog * 0.22f);

	// 溜め切り直前は強く明滅する
	const float imminent = (prog > 0.75f)
		? (0.5f + sin(g_time * 13.0f) * 0.5f) * (prog - 0.75f) / 0.25f : 0.0f;

	//------------------------------------------
	// 発生の閃光
	//------------------------------------------
	float spark = 0.0f;
	if (flash > 0.001f)
	{
		// 立ち上がりが鋭く、尾を引いて消える
		const float k = flash * flash;

		// 中心の閃光。一瞬だけ強く光って縮む。
		spark = Cover(sdCircle(In.Local, R * (0.15f + k * 0.55f))) * k;

		// 抜けていく衝撃波。2枚を時間差で送る。
		spark = max(spark, Cover(abs(dist - R * (0.5f + (1.0f - flash) * 2.8f))
							   - HUD_LINE_BOLD * 0.8f) * flash);
		spark = max(spark, Cover(abs(dist - R * (0.5f + (1.0f - flash) * 1.9f))
							   - HUD_LINE_BASE) * flash * 0.7f);
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	float3 col = In.Color.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, In.SubColor.rgb, max(max(imminent * 0.7f, spark), edge.y * 0.5f));

	float a = edge.x * 0.85f;
	a = max(a, edge.y * 0.28f);
	a = max(a, baseRing * In.Color.a * 0.75f);
	a = max(a, corner * In.Color.a * 0.8f);
	a = max(a, charge * In.Color.a);
	a = max(a, closing * In.Color.a * (0.85f + imminent * 0.15f));
	a = max(a, spark);

	return float4(col, a);
}

// ひよこマーカーに添えるアイコン。向きは回さず常に正立。
// Params : x=左右の向き(-1で左向き) w=1(アイコンモード)
float4 DrawChickIcon(VSOutput In)
{
	float2 p = In.Local;
	p.x *= (In.Params.x < 0.0f) ? -1.0f : 1.0f;

	const float r = min(In.HalfSize.x, In.HalfSize.y) * 0.68f;
	const float2 q = ChickSpace(p, r);
	const float d = sdChick(p, r);

	const float body = Cover(d);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK, HUD_EDGE_HALO);

	const float u = r * CHICK_SCALE;
	const float beak = Cover(sdBeak(q) * u);
	const float eye = Cover((length(q - float2(0.32f, -0.50f)) - 0.085f) * u);

	float3 col = In.Color.rgb + saturate(-p.y / r) * 0.25f;
	col = lerp(col, float3(1.00f, 0.55f, 0.10f), beak);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), eye);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);

	float a = edge.x * 0.7f;
	a = max(a, edge.y * 0.24f);
	a = max(a, body * In.Color.a);

	return float4(col, a);
}

// 画面端の攻撃予兆インジケータ。+X方向を向いた三角を回転させる。
// Params : y=縁の太さ z=向き
float4 DrawArrow(VSOutput In)
{
	if (In.Params.w > 0.5f)
		return DrawChickIcon(In);

	float2 p = Rotate(In.Local, -In.Params.z);

	// 外向きに脈打つ
	p.x -= sin(g_time * 3.7f) * 2.2f;

	const float s = min(In.HalfSize.x, In.HalfSize.y) * 0.9f;
	const float d = sdTriangle(p,
		float2(s, 0.0f),
		float2(-s * 0.55f, s * 0.72f),
		float2(-s * 0.55f, -s * 0.72f));

	const float body = Cover(d);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK, HUD_EDGE_HALO);

	// 尾を引く線
	const float tail = Cover(sdSegment(p,
		float2(-s * 0.7f, 0.0f), float2(-s * 1.5f, 0.0f)) - HUD_LINE_BASE * 0.8f);

	float3 col = In.Color.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), edge.y * 0.5f);

	float a = edge.x * 0.8f;
	a = max(a, edge.y * 0.3f);
	a = max(a, body * In.Color.a);
	a = max(a, tail * In.Color.a * 0.5f);

	return float4(col, a);
}

// カウンター受付。Params.x の割合だけ円弧を描く。
// Params : x=残り割合 y=線の太さ
float4 DrawRing(VSOutput In)
{
	const float R = min(In.HalfSize.x, In.HalfSize.y) * 0.88f;
	const float th = In.Params.y;

	const float dist = length(In.Local);
	const float base = Cover(abs(dist - R) - th * 0.25f);
	const float arcD = abs(dist - R) - th * 0.5f;
	const float arc = Cover(arcD);

	// 真上を0とした時計回りの角度[0,1)
	float t = atan2(In.Local.x, -In.Local.y) / (HUD_PI * 2.0f);
	t = (t < 0.0f) ? t + 1.0f : t;

	const float fill = saturate(In.Params.x);
	const float mask = smoothstep(fill + 0.006f, fill - 0.006f, t);

	// 残量の先端に光の粒。時間が流れていることが見える。
	const float head = arc * exp(-abs(t - fill) * 60.0f);

	// 内側に細い補助円。残りが減るほど内へ縮む。
	const float inner = Cover(abs(dist - R * (0.55f + fill * 0.25f)) - HUD_LINE_THIN) * 0.35f;

	const float2 edge = OutlineDual(arcD, HUD_EDGE_DARK, HUD_EDGE_HALO) * mask;

	float3 col = In.SubColor.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, In.Color.rgb, mask);
	col += In.Color.rgb * head;

	float a = edge.x * 0.6f;
	a = max(a, edge.y * 0.22f);
	a = max(a, base * In.SubColor.a);
	a = max(a, arc * mask * In.Color.a);
	a = max(a, head * 0.9f);
	a = max(a, inner * In.SubColor.a);

	return float4(col, a);
}

// 鳴き声の波紋。中心から広がって消える。
// Params : x=広がり(0→1) y=線の太さ
float4 DrawWave(VSOutput In)
{
	const float R = min(In.HalfSize.x, In.HalfSize.y) * saturate(In.Params.x);
	const float th = In.Params.y * (1.0f - In.Params.x * 0.6f);

	const float ring = Cover(abs(sdCircle(In.Local, R)) - th * 0.5f);
	const float inner = Cover(sdCircle(In.Local, R)) * 0.12f;
	const float fade = 1.0f - saturate(In.Params.x);

	// 追従する2本目の波。厚みが出る。
	const float ring2 = Cover(abs(sdCircle(In.Local, R * 0.78f)) - th * 0.3f) * 0.55f;

	float4 col;
	col.rgb = In.Color.rgb;
	col.a = max(max(ring, ring2), inner) * In.Color.a * fade;
	return col;
}

// プレイヤーの生命線。
// アイコンのニワトリを肺活量の弧が囲み、その右にHPバーが伸びる。
// 1匹の生き物が体力と息の両方を持っている、という読み方になる。
// Params : x=HP y=HP遅延 z=肺活量 w=肺活量遅延
// Extra  : x=被弾衝撃 y=HPの実数値 z=肺活量の消費強度 w=数値の表示量
float4 DrawHealth(VSOutput In)
{
	const float by = In.HalfSize.y / (1.0f + HUD_HEALTH_PAD_Y);
	const float2 b = float2(In.HalfSize.x - by * HUD_HEALTH_PAD_X, by);

	const float hp = saturate(In.Params.x);
	const float hpLag = saturate(In.Params.y);
	const float br = saturate(In.Params.z);
	const float brLag = saturate(In.Params.w);

	const float shock = In.Extra.x;

	// Extra.y は「HPの実数値 + 数値の表示量(小数部)」を詰めてある。
	// チャンネルを1つ節約するための細工。C++側と対で扱うこと。
	const int hpValue = (int) floor(In.Extra.y);
	const float showNum = saturate(frac(In.Extra.y) * 1.001f);

	// Extra.z は正で肺活量の消費、負でHPの回復。
	// 符号で分けて、チャンネルを増やさずに2つの一発演出を運ぶ。
	const float spend = saturate(In.Extra.z);
	const float heal = saturate(-In.Extra.z);

	const float breathFlash = saturate(In.Extra.w); // 満タンになった瞬間の一発光

	// ★閾値は config の health.dangerLine と対で直すこと。
	const float danger = saturate((0.35f - hp) / 0.35f);
	const float full = step(1.0f, br);

	// 満タンになってからの落ち着き具合。0で満タンの瞬間、1で静止。
	// breathFlash の減衰をそのまま流用してチャンネルを節約している。
	// ★膨らみの長さを別に決めたくなったら、C++から専用の値を渡すこと。
	const float settle = full * saturate(1.0f - breathFlash);

	//------------------------------------------
	// 揺れ
	//------------------------------------------
	float2 P = In.Local;

	// 被弾。減衰する振動で弾かれる。
	// 振幅は控えめにする。大きく動かすと残量が読めなくなり、
	// 一番読みたい瞬間に一番読めないUIになる。
	// 強さの調整は config の health.shockScale で行う。
	if (shock > 0.001f)
	{
		const float age = 1.0f - shock;
		P.x -= Elastic(age, 40.0f, 8.0f) * shock * 3.2f;
		P.y -= Elastic(age, 33.0f, 8.0f) * shock * 1.4f;
	}

	// 危険域。ゆっくり大きく揺らす。
	if (danger > 0.01f)
	{
		P.x += sin(g_time * 2.3f) * danger * 1.6f;
		P.y += cos(g_time * 1.7f) * danger * 1.0f;
	}

	// 最後の1目盛りは全体が心拍で拡縮する。
	// 位置ではなく大きさを変えると、視界の端でも気付ける。
	{
		const float ls = smoothstep(0.22f, 0.10f, hp);
		if (ls > 0.01f)
		{
			const float ph = frac(g_time * 1.25f);
			const float beat2 = exp(-ph * 9.0f) + exp(-max(ph - 0.20f, 0.0f) * 9.0f) * 0.7f;
			const float amp = HUD_HEALTH_BEAT * b.y / max(b.x, 1e-3f);
			P /= (1.0f + saturate(beat2) * ls * amp);
		}
	}

	//------------------------------------------
	// レイアウト
	//------------------------------------------
	const float ringR = b.y * HUD_BREATH_RING;
	const float arcTh = ringR * HUD_BREATH_TH;
	const float barH = b.y * 0.26f;

	const float2 ic = float2(-b.x + b.y * HUD_BREATH_CX, 0.0f);

	const float barL = ic.x + ringR * HUD_BREATH_GAP;
	const float barHalfW = (b.x * 0.97f - barL) * 0.5f;
	const float barCx = barL + barHalfW;

	// 弧の層を先に組む。アイコンがローカル座標を流用する。
	const BreathArc arc = BuildBreathArc(P, ic, ringR, arcTh, br, brLag,
										 settle, 0);

	//------------------------------------------
	// アイコン(ニワトリ)
	//------------------------------------------
	float beat = 1.0f;
	if (danger > 0.01f)
	{
		// 心拍のリズム。速すぎると点滅に見えるので脈として読める速さに留める。
		const float ph = frac(g_time * (0.55f + danger * 0.25f));
		const float p1 = exp(-ph * 11.0f);
		const float p2 = exp(-max(ph - 0.22f, 0.0f) * 11.0f) * 0.55f;
		beat = 1.0f + (p1 + p2) * danger * 0.2f;
	}

	// 被弾で縮んでから跳ね返る
	beat *= 1.0f - shock * 0.18f + Elastic(1.0f - shock, 18.0f, 5.0f) * shock * 0.10f;
	beat *= 1.0f + full * (0.5f + sin(g_time * 2.9f) * 0.5f) * 0.07f;

	const float bob = sin(g_time * 1.3f) * b.y * 0.06f;
	const float lean = full * sin(g_time * 2.9f) * 0.06f;

	float2 ip = (arc.lp - float2(0.0f, bob)) / beat;
	ip = Rotate(ip, lean);

	// 満タンで鳴き姿へ切り替える。操作ガイドの鳴き声アイコンと同じSDFなので見た目が揃う。
	// 切り替えの間は2つの距離場を混ぜる。一瞬なので形の崩れは目に残らない。
	const float cryK = arc.full * smoothstep(0.04f, 0.30f, settle);
	const float chickD = lerp(sdChicken(ip, ringR * HUD_BREATH_ICON),
							  sdIconCry(ip + float2(ringR * HUD_BREATH_CRY_SHIFT, 0.0f),
										ringR * HUD_BREATH_CRY_ICON, arc.cryOpen, arc.cryWave),
							  cryK);
	const float icon = Cover(chickD);
	const float iconLine = Stroke(chickD, HUD_LINE_BASE);

	// 時々まばたきする
	float blink = 0.0f;
	{
		const float ph = frac(g_time * 0.25f);
		if (ph < 0.03f)
		{
			blink = Cover(sdBox(ip - float2(ringR * 0.24f, -ringR * 0.20f),
								float2(ringR * 0.06f, ringR * 0.012f)));
		}
	}

	// 鳴いている間は頭の位置が変わるので、まばたきは出さない。
	blink *= 1.0f - cryK;

	//------------------------------------------
	// HPバー
	//------------------------------------------
	const float2 bp = P - float2(barCx, 0.0f);
	const float2 bb = float2(barHalfW, barH);

	const float barD = sdSweptBar(bp, bb, 1.6f);
	const float bg = Cover(barD);

	const float fillX = -bb.x + bb.x * 2.0f * hp;
	const float lagX = -bb.x + bb.x * 2.0f * hpLag;

	const float fill = Cover(max(barD, bp.x - fillX));
	const float lag = Cover(max(barD, bp.x - lagX));
	const float lagOnly = saturate(lag - fill);

	// 減りかけの帯は端がほつれる。
	// ちらつきが速いと目障りなので、書き換えは秒5回程度に留める。
	float fray = 1.0f;
	if (lagOnly > 0.0f)
		fray = 0.82f + Hash11(floor(bp.y * 0.4f) + floor(g_time * 5.0f)) * 0.18f;

	// 分節(5分割)
	const float segW = bb.x * 2.0f / 5.0f;
	const float notch = Cover(abs(fmod(bp.x + bb.x, segW)) - HUD_LINE_THIN);

	// 1目盛り減ると、その位置に波紋が走る
	const float segX = -bb.x + ceil(hp * 5.0f) / 5.0f * bb.x * 2.0f;
	const float segPulse = exp(-abs(bp.x - segX) * 0.14f) * lagOnly * bg;

	// 羽根の模様が流れる
	float plume;
	{
		float2 q = bp;
		q.x = fmod(q.x + bb.x + g_time * 4.0f, 16.0f) - 8.0f;
		plume = Cover(sdSmallFeather(q, 9.0f, 2.2f)) * 0.16f;
	}

	// 危険域は斜線が入る。
	// 緑と赤の区別が付きにくい人でも、模様の有無で状態が読める。
	// 色に頼らない手掛かりを1つ持たせておく。
	float hatch = 0.0f;
	if (danger > 0.01f)
	{
		const float v = frac((bp.x + bp.y * 1.6f) / 9.0f);
		hatch = step(0.55f, v) * fill * danger;
	}

	const float edgeGlow = exp(-abs(bp.x - fillX) * 0.35f) * fill * (1.0f + shock * 2.5f);
	const float lagEdge = exp(-abs(bp.x - lagX) * 0.5f) * lagOnly;
	const float outline = Stroke(barD, HUD_LINE_BASE);

	// 最後の1目盛り。危険域とは別格の扱いにする。
	// 「まだ少しある」と「あと一撃で死ぬ」は伝わり方が違っていい。
	// ★閾値は config の health.lastStandLine と対で直すこと。
	const float lastStand = smoothstep(0.22f, 0.10f, hp);

	// 心拍のように2回打つ。均一な明滅より切迫感が出る。
	float lastPulse = 0.0f;
	{
		const float ph = frac(g_time * 1.25f);
		lastPulse = exp(-ph * 9.0f) + exp(-max(ph - 0.20f, 0.0f) * 9.0f) * 0.7f;
		lastPulse = saturate(lastPulse) * lastStand;
	}

	// 空になった部分が赤く満ちる。
	// 色を足すだけでは背景に紛れるので、面で埋めて形を変える。
	const float emptyFill = saturate(bg - fill) * lastStand
						  * (0.35f + lastPulse * 0.65f);

	// 5秒に1度、光が右へ走り抜ける
	float sheen;
	{
		const float ph = frac(g_time * 0.2f);
		sheen = exp(-abs(bp.x - (-bb.x + ph * bb.x * 3.2f)) * 0.20f)
			  * fill * smoothstep(0.55f, 0.25f, ph);
	}

	// 被弾した瞬間、帯に亀裂が走る
	float crack = 0.0f;
	if (shock > 0.45f)
	{
		const float k = (shock - 0.45f) / 0.55f;
		[unroll]
		for (int i = 0; i < 3; ++i)
		{
			const float sx = fillX - Hash11((float) i * 5.1f) * bb.x * 0.5f;
			float2 q = bp - float2(sx, 0.0f);
			q.x += sin(q.y * 1.8f) * 2.5f;
			crack = max(crack, Cover(sdBox(q, float2(HUD_LINE_THIN * 0.8f, bb.y))) * k);
		}
	}

	// 足跡
	float feet;
	{
		float2 q = P - float2(barCx, barH * 2.1f);
		const float st = 22.0f;
		const float idx = floor((q.x + bb.x) / st);
		q.x = fmod(q.x + bb.x, st) - st * 0.5f;
		q.y += (fmod(idx, 2.0f) < 0.5f) ? -1.5f : 1.5f;
		feet = Cover(sdFootPrint(Rotate(q, 1.57f), barH * 0.42f));
		feet *= step(idx * st / (bb.x * 2.0f), hp);
	}

	//------------------------------------------
	// HPの割合
	//------------------------------------------
	// 帯の右上、クアッドの内側に置く。
	//
	// ★横へ逃がすとクアッドの外に出て、まるごと切れる。
	//   帯は既に右端いっぱいまで伸びているので、空きは上下にしかない。
	//
	// 数字と記号は詰め組みで1つの塊にする。
	// 位置を別々に決めると、桁が変わったときに必ず重なる。
	//
	// 数値は遅れて追う値(hpLag)から作る。
	// 実測値から作ると被弾の瞬間に飛んで、減る過程が見えない。
	float num = 0.0f;
	float2 numEdge = float2(0.0f, 0.0f);
	{
		const float glyphH = barH * 0.92f;
		const float rightIn = barCx + bb.x - glyphH * 0.35f;
		const float cellY = -barH * 2.35f;

		// 被弾で弾んでから戻る
		const float k = 1.0f + shock * 0.22f
					  + Elastic(1.0f - shock, 20.0f, 6.0f) * shock * 0.12f;

		const float numD = sdPercentText((P - float2(rightIn, cellY)) / k,
										 hpValue, glyphH, 1.0f);

		num = Cover(numD);
		numEdge = OutlineDual(numD, HUD_EDGE_DARK, HUD_EDGE_HALO);
	}

	//------------------------------------------
	// 枠 : 情報の範囲を示す線と鋲
	//------------------------------------------
	// 全周を囲まず、下辺と両端の立ち上がりだけ。
	// 囲うと窮屈になり、面を敷くのと変わらなくなる。
	const float fy = b.y * 0.86f;
	const float fx0 = ic.x - ringR * HUD_BREATH_FRAME;
	const float fx1 = b.x * 0.98f;

	const float baseLineD = sdSegment(P, float2(fx0, fy), float2(fx1, fy)) - HUD_LINE_THIN;

	float frame = Cover(baseLineD);
	frame = max(frame, Cover(sdSegment(P,
		float2(fx0, fy), float2(fx0, fy - b.y * 0.32f)) - HUD_LINE_THIN));
	frame = max(frame, Cover(sdSegment(P,
		float2(fx1, fy), float2(fx1, fy - b.y * 0.32f)) - HUD_LINE_THIN));
	frame = max(frame, Cover(sdSegment(P,
		float2(fx1, fy), float2(fx1 + b.y * 0.3f, fy - b.y * 0.5f)) - HUD_LINE_THIN));

	float rivets = Cover(sdRivet(P - float2(fx0, fy), b.y * 0.075f));
	rivets = max(rivets, Cover(sdRivet(P - float2(fx1, fy), b.y * 0.075f)));

	// 目盛り。下辺に沿って短い縦線を刻む。
	// 線1本だけだと素っ気ないので、刻みを入れて版面らしくする。
	float ticks = 0.0f;
	{
		const float span = fx1 - fx0;
		const float step2 = span / 12.0f;
		const float lx = fmod(P.x - fx0, step2);
		const float inBand = step(fx0, P.x) * step(P.x, fx1);

		// 3本に1本だけ長くする。等間隔より読みやすい。
		const float idx = floor((P.x - fx0) / step2);
		const float len2 = (fmod(idx, 3.0f) < 0.5f) ? b.y * 0.16f : b.y * 0.09f;

		ticks = Cover(sdBox(float2(min(lx, step2 - lx), P.y - fy - len2 * 0.5f),
							float2(HUD_LINE_THIN * 0.55f, len2 * 0.5f))) * inBand;
	}

	// 左端の斜線。区画の始まりを示す。
	float lead = 0.0f;
	{
		[unroll]
		for (int i = 0; i < 3; ++i)
		{
			const float x = fx0 + (float) i * b.y * 0.16f;
			lead = max(lead, Cover(sdSegment(P,
				float2(x, fy), float2(x + b.y * 0.20f, fy - b.y * 0.26f))
				- HUD_LINE_THIN * 0.55f));
		}
	}

	// 下辺を光が左から右へ舐める。常時の生気。
	// 必殺技が溜まると、その光が弧から流れ出たように強く速くなる。
	float frameSweep;
	{
		const float sp = 0.18f + arc.hold * 0.10f;
		const float ph = frac(g_time * sp);
		frameSweep = Cover(baseLineD) * exp(-abs(P.x - lerp(fx0, fx1, ph)) * 0.08f)
				   * smoothstep(1.0f, 0.75f, ph) * (1.0f + arc.hold * 0.8f);
	}

	//------------------------------------------
	// 二重縁取り
	//------------------------------------------
	const float2 barEdge = OutlineDual(barD, HUD_EDGE_DARK, HUD_EDGE_HALO);
	const float2 arcEdge = OutlineDual(arc.sd, HUD_EDGE_DARK * 0.9f, HUD_EDGE_HALO);
	const float2 iconEdge = OutlineDual(chickD, HUD_EDGE_DARK, HUD_EDGE_HALO);
	const float2 frameEdge = OutlineDual(baseLineD, HUD_EDGE_DARK * 0.8f, HUD_EDGE_HALO * 0.6f);

	const float2 edge = max(max(barEdge, arcEdge), max(iconEdge, numEdge));

	//------------------------------------------
	// 合成
	//------------------------------------------
	// HPは緑。危険域で赤へ寄る。
	const float3 hpCol = lerp(In.Color.rgb, float3(1.0f, 0.26f, 0.22f), danger);

	float3 col = In.SubColor.rgb;

	// 縁取り(暗い層を先に敷く)
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);

	// HP
	col = lerp(col, float3(1.0f, 0.96f, 0.92f) * fray, lagOnly);
	col = lerp(col, hpCol, fill);
	col += hpCol * plume * fill;
	col += hpCol * edgeGlow * 0.8f;
	col += float3(1, 1, 1) * (lagEdge * 0.9f + sheen * 0.45f);
	col += float3(1, 0.95f, 0.9f) * segPulse * 0.8f;
	col = lerp(col, In.SubColor.rgb * 0.45f, notch * fill * 0.75f);
	col = lerp(col, In.SubColor.rgb * 0.6f, hatch * 0.55f);
	col = lerp(col, float3(1, 1, 1), crack * 0.8f);

	// 肺活量
	// col は HP の上・アイコンの下。α はこの時点では受け皿に貯めておき、
	// アルファ区間でまとめて合流させる。
	float breathA = 0.0f;
	ComposeBreathArc(arc, In.Color, In.SubColor, spend, col, breathA);

	// アイコン。発動中は弧と一緒に引っ込める。
	// 撃っている最中に鳥が残ると、何が飛んだのか分からなくなる。
	col = lerp(col, hpCol * 1.6f, icon * arc.live);
	col = lerp(col, In.SubColor.rgb, blink * arc.live);
	col = lerp(col, float3(1, 1, 1), iconLine * arc.live * 0.4f);

	// 数値。帯の上に置くので明るく。危険域では色が寄る。
	col = lerp(col, lerp(float3(1, 1, 1), hpCol, danger * 0.6f), num);

	// 枠
	col = lerp(col, In.Color.rgb * 0.9f, max(max(frame, rivets), max(ticks, lead)) * 0.7f);
	col += In.Color.rgb * frameSweep * 0.8f;

	// ハローは最後に薄く乗せる
	col = lerp(col, float3(1, 1, 1), edge.y * 0.45f);

	col += hpCol * danger * 0.5f;
	col += float3(1, 0.85f, 0.85f) * shock * 0.35f;

	// 最後の1目盛り。空の部分が赤く満ち、帯全体が脈打つ。
	col = lerp(col, float3(0.85f, 0.10f, 0.08f), emptyFill);
	col += float3(1.0f, 0.30f, 0.24f) * lastPulse * 0.9f;

	// 回復。緑の光が帯を左から右へ満たす。
	// 減る演出だけ作ると、回復が「何も起きていない」ように見える。
	if (heal > 0.001f)
	{
		const float x = -bb.x + (1.0f - heal) * bb.x * 2.4f;
		const float wave = exp(-abs(bp.x - x) * 0.16f) * bg * heal;

		col = lerp(col, float3(0.60f, 1.00f, 0.55f), saturate(wave * 1.4f));
	}

	// 肺活量の一発演出。縁取りとハローより後に載せる。
	// 一発ものが線の下に潜ると、一番見せたい瞬間に見えなくなる。
	float shotA = 0.0f;
	ComposeBreathOneShot(arc, ringR, br, spend, breathFlash, col, shotA);

	//------------------------------------------
	// アルファ
	//------------------------------------------
	float a = edge.x * 0.75f;
	a = max(a, edge.y * 0.26f);
	a = max(a, max(frameEdge.x * 0.55f, frameEdge.y * 0.2f));

	// HPバー
	a = max(a, bg * In.SubColor.a * 0.5f);
	a = max(a, fill * In.Color.a);
	a = max(a, lagOnly * 0.95f * fray);
	a = max(a, outline * bg * 0.55f);
	a = max(a, crack * 0.85f);
	a = max(a, feet * 0.35f);

	// 肺活量の弧
	a = max(a, breathA);
	a = max(a, shotA);

	// アイコン・数値・枠
	a = max(a, icon * In.Color.a * arc.live);
	a = max(a, iconLine * 0.85f * arc.live);
	a = max(a, num);
	a = max(a, max(numEdge.x, numEdge.y * 0.4f) * 0.7f);
	a = max(a, frame * (0.32f + danger * 0.3f + shock * 0.4f
					  + lastPulse * 0.6f + arc.hold * 0.35f));
	a = max(a, emptyFill * 0.9f);

	// 回復
	if (heal > 0.001f)
	{
		const float x = -bb.x + (1.0f - heal) * bb.x * 2.4f;
		a = max(a, exp(-abs(bp.x - x) * 0.16f) * bg * heal * 0.9f);
	}

	a = max(a, rivets * (0.55f + full * 0.3f));
	a = max(a, ticks * 0.28f);
	a = max(a, lead * 0.35f);
	a = max(a, frameSweep * 0.7f);

	return float4(col, a);
}

// 敵の残数 = 卵。生存は無傷、撃破は割れた殻。
// Params : x=生存数 y=総数 z=直近で減ったか w=未使用
float4 DrawCount(VSOutput In)
{
	const float total = max(In.Params.y, 1.0f);
	const float alive = In.Params.x;
	const float killT = In.Params.z;

	const float2 b = In.HalfSize;

	const float rowH = b.y;
	const float rowY = 0.0f;

	const float slotW = b.x * 2.0f / total;
	const float r = min(slotW * 0.36f, rowH * 0.85f);

	const float idxF = floor((In.Local.x + b.x) / slotW);
	const float cx = -b.x + slotW * (idxF + 0.5f);

	const float seed = idxF * 1.7f;
	const float tilt = sin(seed * 1.6f) * 0.18f + sin(g_time * 0.53f + seed) * 0.045f;

	float2 p = In.Local - float2(cx, rowY);

	const bool isAlive = (idxF < alive);
	if (isAlive)
		p.y -= sin(g_time * 1.1f + idxF * 0.7f) * b.y * 0.10f;

	p = Rotate(p, tilt);

	const bool isJustKilled = (abs(idxF - alive) < 0.5f) && (killT > 0.001f);

	// 倒された直後は弾ける
	if (isJustKilled)
	{
		p /= (1.0f + killT * 0.45f);
		p.y += killT * killT * 5.0f;
	}

	// 撃破されたひよこは下へ落ちる
	float shellFall = 0.0f;
	float shellSpin = 0.0f;
	if (!isAlive)
	{
		const float age = saturate((alive - idxF) * 0.35f + (1.0f - killT) * 0.2f);
		shellFall = age * age * rowH * 1.6f;
		shellSpin = age * 0.9f * ((fmod(idxF, 2.0f) < 0.5f) ? 1.0f : -1.0f);
	}

	const float2 sp2 = Rotate(In.Local - float2(cx, rowY + shellFall), tilt + shellSpin);
	const float2 cp = isAlive ? p : sp2;
	const float2 q = ChickSpace(cp, r);

	const float chickD = sdChick(cp, r);
	const float body = Cover(chickD);
	const float shell = Stroke(chickD, HUD_LINE_BASE);
	const float2 edge = OutlineDual(chickD, HUD_EDGE_DARK * 0.8f, HUD_EDGE_HALO);

	// 目・くちばし・×目
	const float u = r * CHICK_SCALE;
	const float beak = Cover(sdBeak(q) * u);
	const float eye = Cover((length(q - float2(0.32f, -0.50f)) - 0.085f) * u);
	const float xEye = Cover(sdXMark(q, float2(0.32f, -0.50f), 0.16f, 0.035f) * u);

	float3 col;
	float a = edge.x * 0.7f;
	a = max(a, edge.y * 0.24f);

	if (isAlive)
	{
		// 生存は明るく塗る。撃破との明度差を大きく取らないと残数が読めない。
		col = In.Color.rgb + saturate(-p.y / r) * 0.40f;
		col = lerp(col, float3(1.00f, 0.55f, 0.10f), beak); // くちばしはオレンジ
		col = lerp(col, float3(0.02f, 0.02f, 0.03f), eye); // 目
		col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);

		a = max(a, body * (0.95f + sin(g_time * 1.1f + seed) * 0.05f));
	}
	else
	{
		col = lerp(In.SubColor.rgb, float3(0.02f, 0.02f, 0.03f), edge.x);

		const float fade = 1.0f - saturate(shellFall / (rowH * 1.6f));

		// 撃破は輪郭と×目だけ。塗りをほぼ落として生存との差をはっきりさせる。
		a = max(a, max(shell, xEye * 0.85f) * fade * 0.55f);
		a = max(a, body * 0.06f * fade);

		if (isJustKilled)
		{
			col += float3(1, 1, 1) * killT * 1.5f;
			a = max(a, body * killT * 0.8f);

			[unroll]
			for (int i = 0; i < 5; ++i)
			{
				const float ang = Hash11(seed + (float) i) * 6.283f;
				const float dist = r * (0.5f + (1.0f - killT) * 2.4f);
				a = max(a, Cover(sdCircle(p - float2(cos(ang), sin(ang)) * dist,
										  r * 0.13f * killT)) * killT);
			}
		}
	}

	return float4(col, a * In.Color.a);
}

// 画面中心を基準にした方位計。
//
// 戦闘中の視界を邪魔しないよう、出すのは矢印1つだけ。
// 弧や目盛りを重ねると情報が増えて、肝心の「どっちにいるか」が
// かえって読みにくくなる。
//
// 彩度と不透明度は低く保つ。常に出ているものなので、
// 視界の端で気付ける最低限の強さがあればいい。
// Params : x=方位(rad) y=距離の近さ z=脅威度 w=薄める量(遠い順に沈める)
float4 DrawCompass(VSOutput In)
{
	const float R = min(In.HalfSize.x, In.HalfSize.y) * 0.86f;
	const float ang = In.Params.x;
	const float near01 = saturate(In.Params.y);
	const float threat = saturate(In.Params.z);
	const float rank = saturate(In.Params.w); // 0が最寄り、1で最も沈む

	//------------------------------------------
	// 矢印
	//------------------------------------------
	const float2 dir = float2(cos(ang), sin(ang));
	const float2 p = Rotate(In.Local - dir * R, -ang);

	// 近いほど大きい。距離は形の大きさだけで伝える。
	const float s = 5.5f + near01 * 4.5f;

	const float d = sdTriangle(p,
		float2(s, 0.0f),
		float2(-s * 0.62f, s * 0.70f),
		float2(-s * 0.62f, -s * 0.70f));

	const float body = Cover(d);

	// 枠線。塗りだけだと背景に溶けるので、輪郭で形を保つ。
	const float edgeLine = Stroke(d, HUD_LINE_BASE * 0.9f);

	// 暗い縁取り。明るい背景でも輪郭が消えないように。
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK * 0.8f, HUD_EDGE_HALO * 0.6f);

	//------------------------------------------
	// 合成
	//------------------------------------------
	// 攻撃予兆がある敵はゆっくり明滅する。速いと目障りになる。
	const float blink = (threat > 0.01f)
		? 0.62f + (0.5f + sin(g_time * 3.1f) * 0.5f) * 0.38f : 1.0f;

	float3 col = lerp(In.Color.rgb, In.SubColor.rgb, threat);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);

	// 塗りは薄く、輪郭ははっきり。主張を抑えたまま形は読める。
	float a = body * 0.42f;
	a = max(a, edgeLine * 0.85f);
	a = max(a, edge.x * 0.5f);
	a = max(a, edge.y * 0.16f);

	// 近いほど濃く。2体目以降は順に沈める。
	a *= (0.40f + near01 * 0.60f) * (1.0f - rank * 0.55f);

	return float4(col, a * In.Color.a * blink);
}

// コンボ = 数字 + COMBO!! + 残り時間。
// Params : x=ヒット数 y=残り時間の割合 z=直近で増えたか w=ランク(0〜4)
float4 DrawCombo(VSOutput In)
{
	const float hits = In.Params.x;
	const float remain = saturate(In.Params.y);
	const float pop = saturate(In.Params.z);
	const float rank = In.Params.w;

	// Extra : x=途切れて落ちる量(0→1) y=累計ダメージ z=崩れ始め(0〜1)
	const float drop = saturate(In.Extra.x);
	const int totalDmg = (int) In.Extra.y;
	const float crumble = saturate(In.Extra.z);

	const float2 b = In.HalfSize;

	//------------------------------------------
	// 途切れた瞬間の落下
	//------------------------------------------
	// 数字が落ちて消える。静かに消えるだけだと、
	// 繋がっていたものが切れたことに気付けない。
	// 途切れる前から崩れ始める。
	// 切れてから動き出すと、見る前に消えてしまう。
	// 猶予が減るほど傾いて震え、切れた瞬間にそのまま落ちる。
	float2 D = In.Local;

	// 崩れ始め : 傾きと震え
	D = Rotate(D, crumble * 0.10f);
	D.x -= sin(g_time * 26.0f) * crumble * crumble * b.y * 0.06f;
	D.y -= crumble * crumble * b.y * 0.10f;

	// 落下 : クアッドの内側に収める。はみ出すと縁で切れる。
	D.y -= drop * drop * b.y * 0.72f;
	D = Rotate(D, drop * 0.34f);
	D /= (1.0f - drop * 0.22f);

	const float dropFade = 1.0f - drop;

	//------------------------------------------
	// 数字(左)
	//------------------------------------------
	// 増えた瞬間に跳ね、行き過ぎてから戻る。
	// 単調に縮むだけだと動きが死んで見える。
	const float scale = 1.0f + (EaseOutBack(pop) - pop) * 0.5f + pop * 0.26f;

	// 数字。矩形に収めるので、3桁になっても枠から出ない。
	const float2 numBox = float2(b.x * 0.26f, b.y * 0.28f);

	// 途切れる寸前は数字が震える
	float2 shake = float2(0.0f, 0.0f);
	{
		const float u = (remain < 0.30f) ? (1.0f - remain / 0.30f) : 0.0f;
		shake = float2(sin(g_time * 34.0f), cos(g_time * 29.0f)) * u * u * b.y * 0.09f;
	}

	// 数字は札の左。右にHIT!!が続くので、間を十分に空ける。
	// 詰めると「6HIT」と1つの語に見えてしまう。
	const float2 np = (D - float2(-b.x * 0.60f, b.y * 0.16f) - shake) / scale;

	const float numD = sdNumberFit(np, (int) hits, numBox, 0.20f, 0.30f, 1.0f);
	const float num = Cover(numD);
	const float numLine = Stroke(numD, HUD_LINE_THIN * 1.4f);

	//------------------------------------------
	// HIT!! の文字(数字の右)
	//------------------------------------------
	// 数字と釣り合う大きさにする。小さいと添え物に見えて読まれない。
	// 数字から十分に離す。詰めると「12HIT」と続けて読めてしまう。
	const float txtH = b.y * 0.38f;
	float2 tp = D - float2(-b.x * 0.02f, b.y * 0.18f);
	tp = Rotate(tp, Elastic(1.0f - pop, 24.0f, 6.0f) * pop * 0.08f);

	// 感嘆符も同じ字面で組んであるので、まとめて1つの単語として扱える。
	const float txtD = sdHitText(tp, txtH);
	const float txt = Cover(txtD);

	// 文字を左から右へ光が舐める
	float txtSweep;
	{
		const float ph = frac(g_time * 0.55f);
		const float x = lerp(-txtH * 0.5f, HitTextWidth(txtH) + txtH * 0.5f, ph);
		txtSweep = txt * exp(-abs(tp.x - x) * 0.12f) * smoothstep(1.0f, 0.7f, ph);
	}

	//------------------------------------------
	// ランク語(数字の前)
	//------------------------------------------
	// 数字だけでは「今どれくらい凄いのか」が伝わらない。
	// 数字より前に置くのは、先に格を読ませて数を後から確かめる順にするため。
	// ランク語は上段へ。横に並べると PERFECT で札が長くなりすぎ、
	// 画面の先頭を塞いでしまう。
	const float tagH = b.y * 0.34f;
	const float tw = tagH * 0.24f;
	const float tspace = tagH * 0.26f;

	const float tagLeft = -b.x * 0.92f;

	const float tagD = sdRankWord(D - float2(tagLeft, -b.y * 0.62f),
								  (int) rank, tw, tagH * 0.5f, tagH * 0.095f, tspace);
	const float tag = Cover(tagD);

	//------------------------------------------
	// 累計ダメージ(右下)
	//------------------------------------------
	// 札の中に収める。外へ出すと何の数字か分からなくなる。
	const float2 dmgBox = float2(b.x * 0.26f, b.y * 0.20f);
	const float dmgD = sdNumberFit(D - float2(b.x * 0.68f, -b.y * 0.56f),
								   totalDmg, dmgBox, 0.22f, 0.30f, 1.0f);
	const float dmgNum = Cover(dmgD);

	//------------------------------------------
	// 残り時間
	//------------------------------------------
	// 札の内側に収める。はみ出すと何の線か分からなくなる。
	const float2 gp = D - float2(0.0f, b.y * 0.80f);
	const float gaugeW = b.x * 0.76f;
	const float gbg = Cover(sdBox(gp, float2(gaugeW, HUD_LINE_BASE * 0.9f)));
	const float gfl = Cover(sdBox(gp - float2(-gaugeW + gaugeW * remain, 0.0f),
								   float2(gaugeW * remain, HUD_LINE_BASE * 0.9f)));

	const float gHead = gbg * exp(-abs(gp.x - (-gaugeW + gaugeW * 2.0f * remain)) * 0.35f);

	// 途切れる寸前。数字ごと揺れて明滅する。
	// 細い線1本の減りだけでは、繋ぎ直す判断に間に合わない。
	const float urgent = (remain < 0.30f)
		? (0.5f + sin(g_time * 11.0f) * 0.5f) * (1.0f - remain / 0.30f) : 0.0f;

	//------------------------------------------
	// 下線と鋲 : 囲わず1本引くだけ
	//------------------------------------------
	const float uy = b.y * 0.62f;
	const float ux0 = -b.x * 0.88f;
	const float ux1 = b.x * 0.88f;

	const float underD = sdSegment(D, float2(ux0, uy), float2(ux1, uy)) - HUD_LINE_THIN;
	float underline = Cover(underD);
	underline = max(underline, Cover(sdSegment(D,
		float2(ux0, uy), float2(ux0, uy - b.y * 0.4f)) - HUD_LINE_THIN));

	float uRivet = Cover(sdRivet(D - float2(ux0, uy), b.y * 0.07f));
	uRivet = max(uRivet, Cover(sdRivet(D - float2(ux1, uy), b.y * 0.07f)));

	//------------------------------------------
	// 二重縁取り
	//------------------------------------------
	const float2 edge = max(
		OutlineDual(numD, HUD_EDGE_DARK, HUD_EDGE_HALO),
		max(OutlineDual(txtD, HUD_EDGE_DARK * 0.8f, HUD_EDGE_HALO),
			OutlineDual(underD, HUD_EDGE_DARK * 0.7f, HUD_EDGE_HALO * 0.6f)));

	//------------------------------------------
	// 合成
	//------------------------------------------
	// ランクが上がるほど色が熱くなる
	const float3 rankCol = lerp(In.Color.rgb, In.SubColor.rgb, saturate(rank / 4.0f));

	float3 col = rankCol;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), max(numLine * 0.35f, edge.y * 0.45f));
	col += float3(1, 1, 1) * txtSweep * 0.5f;
	col += rankCol * (urgent * 0.9f + pop * 0.55f + gHead * 0.5f);
	col = lerp(col, float3(1.0f, 0.35f, 0.25f), urgent * 0.45f);
	col = lerp(col, rankCol * 0.85f, max(underline, uRivet) * 0.6f);
	col = lerp(col, float3(1, 1, 1), tag * 0.35f);

	float a = edge.x * 0.78f;
	a = max(a, edge.y * 0.26f);
	a = max(a, num * In.Color.a);
	a = max(a, numLine * In.Color.a * 0.75f);
	a = max(a, txt * In.Color.a * 0.9f);
	a = max(a, txtSweep * 0.6f);
	a = max(a, gbg * 0.25f);
	a = max(a, gfl * (0.75f + urgent * 0.25f));
	a = max(a, gHead * 0.6f);
	a = max(a, underline * (0.4f + pop * 0.4f));
	a = max(a, uRivet * (0.55f + pop * 0.35f));
	a = max(a, tag * 0.85f);
	a = max(a, dmgNum * 0.55f);

	// 落ちながら消える
	return float4(col, a * dropFade);
}

// ジャスト回避の集中状態。
// 画面中心から放射状の線が収束し、時が止まっていることを示す。
// Params : x=強度(0〜1) y=残り時間の割合
float4 DrawFocus(VSOutput In)
{
	const float power = saturate(In.Params.x);
	const float remain = saturate(In.Params.y);
	const float hitFlash = saturate(In.Params.z); // カウンター成立(1→0)

	if (power < 0.01f && hitFlash < 0.01f)
		return float4(0, 0, 0, 0);

	const float R = min(In.HalfSize.x, In.HalfSize.y);
	const float dist = length(In.Local) / R;
	const float t = atan2(In.Local.y, In.Local.x);

	// 集中線。角度ごとに長さと太さを変える。均一だと機械的に見える。
	const float seed = floor(t / (HUD_PI * 2.0f) * 48.0f);
	const float rnd = Hash11(seed * 7.3f);

	const float angFrac = frac(t / (HUD_PI * 2.0f) * 48.0f);
	const float ray = smoothstep(0.06f + rnd * 0.05f, 0.0f, abs(angFrac - 0.5f));

	const float inner = lerp(0.72f, 0.42f, 1.0f - remain) + rnd * 0.12f;
	const float radial = smoothstep(inner, inner + 0.22f, dist)
					   * smoothstep(1.15f, 0.92f, dist);

	const float rays = ray * radial * (0.75f + sin(g_time * 1.9f + seed * 1.7f) * 0.25f);

	// 周縁の光
	const float rim = smoothstep(0.55f, 1.0f, dist) * (0.4f + remain * 0.3f);

	// 中心の収束リング。残り時間に合わせて内へ縮む。
	const float ring = exp(-abs(dist - lerp(0.18f, 0.40f, remain)) * 42.0f);

	// 舞う羽根。集中している間だけ、ゆっくり降ってくる。
	float feathers = 0.0f;
	[unroll]
	for (int i = 0; i < 6; ++i)
	{
		const float fs = Hash11((float) i * 4.7f);
		const float ph = frac(g_time * 0.18f + fs);
		float2 fp = In.Local - float2((fs - 0.5f) * R * 1.6f, (ph - 0.5f) * R * 1.8f);
		fp = Rotate(fp, ph * 3.0f + fs * 6.283f);
		feathers = max(feathers,
			Cover(sdSmallFeather(fp, R * 0.05f, R * 0.011f)) * Bump(ph));
	}

	float a = (rays * 0.55f + rim * 0.35f + ring * 0.7f + feathers * 0.8f) * power;

	//------------------------------------------
	// カウンター成立の閃光
	//------------------------------------------
	// 画面の周縁が白く抜け、外へ輪が走る。
	// 集中線だけでは「決まった」瞬間が弱い。
	float3 col = In.Color.rgb;

	if (hitFlash > 0.001f)
	{
		const float k = hitFlash;

		// 周縁の白抜き
		const float edgeGlow = smoothstep(0.35f, 1.05f, dist) * k * k;

		// 外へ抜ける輪
		const float wave = exp(-abs(dist - (0.25f + (1.0f - k) * 0.85f)) * 22.0f) * k;

		col = lerp(col, float3(1, 1, 1), saturate(edgeGlow + wave));
		a = max(a, edgeGlow * 0.55f);
		a = max(a, wave * 0.85f);
	}

	return float4(col, a * In.Color.a);
}

// ダメージの数値。敵の位置から浮かび上がって消える。
// 与えている実感は、止めと吹っ飛びだけでは伝わり切らない。
//
// クアッドの下寄りから出て上へ抜けるので、
// 上昇量は必ず HalfSize.y の内側に収める(はみ出すと途中で切れる)。
// Params : x=ダメージ値 y=経過(0→1) z=強打の度合い w=横揺れの種
float4 DrawDamage(VSOutput In)
{
	const int value = (int) In.Params.x;
	const float age = saturate(In.Params.y);
	const float heavy = saturate(In.Params.z);
	const float seed = In.Params.w;

	// 自分が受けたダメージ。与えたものと混同すると、
	// 削られているのに攻撃が通っていると誤解する。
	const float self = saturate(In.Extra.x);

	const float2 b = In.HalfSize;

	//------------------------------------------
	// 動き
	//------------------------------------------
	// 出た瞬間に大きく跳ね、行き過ぎてから落ち着く。
	const float pop = 1.0f - age;
	const float birth = saturate(age * 5.0f);
	const float scale = (0.35f + EaseOutBack(birth) * 0.65f)
					  * (1.0f + heavy * 0.4f)
					  * (1.0f - age * 0.22f);

	// 与えたダメージは上へ、受けたダメージは下へ動かす。
	// 向きが逆なら、色を見なくても一瞬でどちらか分かる。
	const float dirY = (self > 0.5f) ? -1.0f : 1.0f;
	const float rise = (1.0f - (1.0f - age) * (1.0f - age)) * b.y * 0.72f * dirY;

	// 横へ流れる。個体ごとに向きと量を変える。
	const float side = (Hash11(seed * 13.7f) - 0.5f) * 2.0f;
	const float drift = side * b.x * 0.22f * age
					  + sin(seed * 6.283f + age * 5.0f) * b.x * 0.05f * (1.0f - age);

	// 出た直後だけ首を振る。減衰する振動。
	const float wobble = Elastic(age, 26.0f, 7.0f) * (0.16f + heavy * 0.12f);

	// 与ダメージは下寄りから上へ、被ダメージは上寄りから下へ。
	float2 p = In.Local - float2(drift, (self > 0.5f ? -b.y * 0.34f : b.y * 0.34f) - rise);
	p = Rotate(p, wobble);
	p /= max(scale, 1e-3f);

	//------------------------------------------
	// 数値
	//------------------------------------------
	// 敵より小さくする。数字が主役になると戦況が読めなくなる。
	const float2 box = float2(b.x * 0.62f, b.y * 0.28f * (1.0f + heavy * 0.18f));

	float d = sdNumberFit(p, value, box, 0.22f, 0.30f, 0.0f);

	// 被弾には下向きの三角を添える。数字の前に置いて「減った」を示す。
	if (self > 0.5f)
	{
		const float ms = box.y * 0.55f;
		d = min(d, sdTriangle(p - float2(-box.x * 0.85f, 0.0f),
			float2(0.0f, ms),
			float2(-ms * 0.62f, -ms * 0.55f),
			float2(ms * 0.62f, -ms * 0.55f)));
	}

	const float body = Cover(d);
	const float edgeLine = Stroke(d, HUD_LINE_THIN * 1.4f);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK, HUD_EDGE_HALO);

	//------------------------------------------
	// 残像
	//------------------------------------------
	// 少し下に薄い影を残す。上昇の軌跡が見えて速度感が出る。
	float ghost = 0.0f;
	{
		const float2 gp = (In.Local - float2(drift * 0.85f, b.y * 0.34f - rise * 0.82f))
						/ max(scale * 0.92f, 1e-3f);
		ghost = Cover(sdNumberFit(Rotate(gp, wobble * 0.7f), value, box, 0.22f, 0.30f, 0.0f))
			  * (1.0f - age) * 0.28f;
	}

	//------------------------------------------
	// 出現の閃光
	//------------------------------------------
	// 数値が現れる瞬間に、後ろで光が弾ける。
	float burst = 0.0f;
	if (birth < 1.0f)
	{
		const float k = 1.0f - birth;
		burst = Cover(abs(length(In.Local - float2(drift, b.y * 0.34f))
						- b.y * 0.12f * (1.0f + (1.0f - k) * 2.2f)) - 1.8f) * k;
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	// 終盤でフェードアウト
	const float fade = smoothstep(1.0f, 0.58f, age);

	// 強打は色が熱く、出た瞬間は白く飛ぶ
	const float3 base = lerp(In.Color.rgb, In.SubColor.rgb, heavy);
	float3 col = lerp(base, float3(1, 1, 1), saturate(pop * pop * 1.4f) * 0.7f);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), max(edge.y * 0.5f, burst));

	float a = body * In.Color.a;
	a = max(a, edgeLine * 0.8f);
	a = max(a, edge.x * 0.85f);
	a = max(a, edge.y * 0.3f);
	a = max(a, ghost);
	a = max(a, burst * 0.7f);

	return float4(col, a * fade);
}

// 操作ガイド。
//
// アイコンを大きく上に、キーの記号を小さく下に置く。
// 横並びより視線の移動が短く、アイコンに幅を全部使えるので形が読める。
// 操作を覚えたプレイヤーは上だけ見れば済む。
//
// アイコンは全てが常に動く。1つだけ動いていると、
// 動いていない方が「使えない」に見えてしまう。
//
// Params : x=ボタン形状 y,z,w=表示する文字(0=A..25=Z / 負で無し)
// Extra  : x=押下中(0/1) y=動作アイコン
//          z=状態(0=使えない 1=使える 2=最大) w=押した瞬間の波紋(1→0)
float4 DrawGuide(VSOutput In)
{
	const int shape = (int) In.Params.x;
	const int3 codes = int3((int) In.Params.y, (int) In.Params.z, (int) In.Params.w);

	const float pressed = saturate(In.Extra.x);
	const int action = (int) In.Extra.y;
	const float state = In.Extra.z;
	// Extra.w は正で押下の波紋、負で殲滅の余韻。
	const float ripple = saturate(In.Extra.w);
	const float victory = saturate(-In.Extra.w);

	const float active = saturate(state);
	const float charged = saturate(state - 1.0f);
	const float cooling = (state < 0.999f) ? state : -1.0f;

	const float2 b = In.HalfSize;

	//------------------------------------------
	// 殲滅の余韻
	//------------------------------------------
	const float sink = victory * victory;

	float2 L = In.Local;
	L.y -= sink * b.y * 0.55f;
	L /= (1.0f - sink * 0.28f);

	//------------------------------------------
	// レイアウト
	//------------------------------------------
	const float iconCY = -b.y * 0.26f;
	const float keyCY = b.y * 0.66f;

	const float isize = min(b.x, b.y) * 0.52f;
	const float s = b.y * 0.26f; // キーの記号の大きさ

	//------------------------------------------
	// 動作のアイコン(上・大きく)
	//------------------------------------------
	float2 r = L - float2(0.0f, iconCY);
	r /= (1.0f + ripple * 0.18f);

	float icon = 1e9f;

	if (action == HUD_ACT_ATTACK)
	{
		icon = sdIconLight(r, isize);
	}
	else if (action == HUD_ACT_HEAVY)
	{
		icon = sdIconHeavy(r, isize);
	}
	else if (action == HUD_ACT_DODGE)
	{
		icon = sdIconDodge(r, isize, 0.12f);
	}
	else
	{
		const float open = (charged > 0.5f)
			? Bump(saturate(frac(g_time * 0.7f) * 2.2f)) : 0.35f;
		const float wave = (charged > 0.5f) ? frac(g_time * 0.9f) : 0.0f;

		icon = sdIconCry(r, isize, open, wave);
	}

	const float iconFill = Cover(icon);
	const float iconLine = Stroke(icon, HUD_LINE_BASE);

	//------------------------------------------
	// クールダウンのリング
	//------------------------------------------
	float cool = 0.0f;
	if (cooling >= 0.0f)
	{
		const float rr = isize * 1.15f;
		const float ringD = abs(length(r) - rr) - HUD_LINE_BASE * 0.6f;

		float t = atan2(r.x, -r.y) / (HUD_PI * 2.0f);
		t = (t < 0.0f) ? t + 1.0f : t;

		const float base = Cover(ringD) * 0.22f;
		const float fill = Cover(ringD) * smoothstep(cooling + 0.01f, cooling - 0.01f, t);

		cool = max(base, fill);
	}

	//------------------------------------------
	// 最大状態の輪(鳴き声のみ)
	//------------------------------------------
	float aura = 0.0f;
	if (charged > 0.5f)
	{
		const float rr = isize * (1.18f + sin(g_time * 2.6f) * 0.06f);
		aura = Cover(abs(length(r) - rr) - HUD_LINE_THIN * 0.8f);

		const float ph = frac(g_time * 0.85f);
		aura = max(aura,
			Cover(abs(length(r) - isize * (1.10f + ph * 0.55f)) - HUD_LINE_THIN * 0.7f)
			* (1.0f - ph));
	}

	//------------------------------------------
	// キーの記号(下・小さく)
	//------------------------------------------
	float2 q = L - float2(0.0f, keyCY);

	// 押している間は沈む
	q /= (1.0f - pressed * 0.16f);
	q.y -= pressed * s * 0.14f;

	const bool isMouse = (shape == HUD_BTN_MOUSE_L)
					  || (shape == HUD_BTN_MOUSE_R)
					  || (shape == HUD_BTN_MOUSE_M);
	const bool isWord = (shape == HUD_BTN_WORDKEY);
	const bool isPadBtn = (shape <= HUD_BTN_MENU);

	// 文字の大きさ。パッドのボタンは大きく、キーボードとマウスは小さめ。
	const float lw = isPadBtn ? s * 0.30f : s * 0.22f;
	const float lh = isPadBtn ? s * 0.42f : s * 0.34f;

	float pad = 1e9f; // 下地(キーキャップ / マウス本体)
	float labelD = 1e9f; // 文字
	float hiliteD = 1e9f; // 押す場所。ここだけ明色で塗る。
	float glyphD = 1e9f; // 記号(Shiftの矢印)
	float seamD = 1e9f; // 継ぎ目の線

	if (isMouse)
	{
		//--------------------------------------
		// マウス : 本体の形 + 押す側の塗り + 名称
		//--------------------------------------
		const float2 mb = float2(s * 0.50f, s * 0.84f);
		const float2 mp = q - float2(-s * 1.35f, 0.0f);

		pad = sdMouseBody(mp, mb);

		// ボタン面と胴の境。ここより上(-Y)が左右のボタン。
		const float split = -mb.y * 0.10f;
		const float seam = mb.x * 0.07f;

		// 押すボタン側だけを塗る。左右どちらかが色で一目で分かる。
		if (shape == HUD_BTN_MOUSE_L)
		{
			hiliteD = max(pad, max(mp.x + seam, mp.y - split));
		}
		else if (shape == HUD_BTN_MOUSE_R)
		{
			hiliteD = max(pad, max(-mp.x + seam, mp.y - split));
		}
		else
		{
			// 中ボタンはホイールそのものを塗る
			hiliteD = sdRoundBox(mp - float2(0.0f, -mb.y * 0.44f),
								 float2(mb.x * 0.16f, mb.y * 0.22f), mb.x * 0.13f);
		}

		// 継ぎ目。縦1本で左右を割り、横1本でボタン面と胴を分ける。
		seamD = sdSegment(mp, float2(0.0f, -mb.y), float2(0.0f, split)) - mb.x * 0.06f;
		seamD = min(seamD,
			sdSegment(mp, float2(-mb.x, split), float2(mb.x, split)) - mb.x * 0.06f);

		// 本体の外へはみ出させない
		seamD = max(seamD, pad);

		// ホイール。中ボタン以外でも形の手掛かりとして線で描く。
		if (shape != HUD_BTN_MOUSE_M)
		{
			seamD = min(seamD, sdRoundBox(mp - float2(0.0f, -mb.y * 0.44f),
										  float2(mb.x * 0.14f, mb.y * 0.20f), mb.x * 0.12f));
		}

		// 名称は本体の右に添える。形と名前の両方で読ませる。
		labelD = sdLabel3(q - float2(s * 0.95f, 0.0f), codes, lw, lh, s * 0.085f, lw * 1.05f);
	}
	else if (isWord)
	{
		//--------------------------------------
		// 語で書くキー
		//--------------------------------------
		const int wordId = codes.x;

		int wc[8];
		const int wn = KeyWordCodes(wordId, wc);

		const float wh = s * 0.40f; // 字の半高
		const float ww = wh * 0.50f; // 字の半幅
		const float wsp = ww * 0.75f; // 字間

		const float span = KeyWordSpan(wn, ww, wsp);

		// Shiftだけは矢印を添える。記号があると語より先に読める。
		const float gw = (wordId == HUD_WORD_SHIFT) ? s * 0.70f : 0.0f;
		const float inner = gw + span;

		// キーキャップは中身の幅から決める。語が長くても枠から出ない。
		pad = sdRoundBox(q, float2(inner * 0.5f + s * 0.34f, s * 0.84f), s * 0.18f);

		const float left = -inner * 0.5f;

		if (gw > 0.0f)
			glyphD = sdShiftMark(q - float2(left + gw * 0.42f, 0.0f), s * 0.34f);

		labelD = sdWordAt(q - float2(left + gw, 0.0f), wc, wn,
						  0.0f, ww, wh, wh * 0.20f, wsp);
	}
	else
	{
		//--------------------------------------
		// パッドのボタンと通常のキーキャップ
		//--------------------------------------
		int len = 0;
		if (codes.x >= 0)
			len++;
		if (codes.y >= 0)
			len++;
		if (codes.z >= 0)
			len++;
		len = max(len, 1);

		if (shape == HUD_BTN_FACE)
		{
			pad = sdCircle(q, s * 0.90f);
		}
		else if (shape == HUD_BTN_SHOULDER)
		{
			pad = sdRoundBox(q - float2(0.0f, s * 0.12f),
							 float2(s * 1.05f, s * 0.50f), s * 0.22f);
			pad = min(pad, sdRoundBox(q - float2(0.0f, -s * 0.42f),
									  float2(s * 0.80f, s * 0.14f), s * 0.12f));
		}
		else if (shape == HUD_BTN_TRIGGER)
		{
			pad = sdRoundBox(q - float2(0.0f, s * 0.22f),
							 float2(s * 0.70f, s * 0.52f), s * 0.20f);
			pad = min(pad, sdEllipse(q - float2(0.0f, -s * 0.34f),
									 float2(s * 0.70f, s * 0.46f)));
		}
		else if (shape == HUD_BTN_DPAD)
		{
			pad = min(sdRoundBox(q, float2(s * 0.32f, s * 0.90f), s * 0.10f),
					  sdRoundBox(q, float2(s * 0.90f, s * 0.32f), s * 0.10f));
		}
		else if (shape == HUD_BTN_MENU)
		{
			pad = sdRoundBox(q, float2(s * 0.88f, s * 0.55f), s * 0.24f);
		}
		else
		{
			const float capW = s * (0.50f + 0.34f * (float) len);
			pad = sdRoundBox(q, float2(capW, s * 0.86f), s * 0.18f);
		}

		labelD = sdLabel3(q, codes, lw, lh, s * 0.085f, lw * 1.05f);
	}

	const float padFill = Cover(pad);
	const float padRim = Stroke(pad, HUD_LINE_BASE);
	const float label = Cover(labelD);
	const float hilite = Cover(hiliteD);
	const float glyph = Cover(glyphD);
	const float seamLine = Cover(seamD);

	// 押した瞬間の波紋
	float burst = 0.0f;
	if (ripple > 0.001f)
	{
		const float rr = s * (0.95f + (1.0f - ripple) * 1.0f);
		burst = Cover(abs(length(q) - rr) - HUD_LINE_BASE * 0.6f) * ripple;
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float2 edge = max(
		OutlineDual(min(min(pad, labelD), glyphD), HUD_EDGE_DARK * 0.9f, HUD_EDGE_HALO * 0.85f),
		OutlineDual(icon, HUD_EDGE_DARK * 0.9f, HUD_EDGE_HALO * 0.85f));

	const float dim = lerp(0.28f, 1.0f, active);

	const float3 hot = float3(1.00f, 0.55f, 0.14f);
	const float3 iconCol = lerp(In.Color.rgb, hot, charged);

	const float3 padCol = lerp(In.SubColor.rgb, In.SubColor.rgb * 2.0f + 0.28f, pressed);
	const float3 textCol = lerp(In.Color.rgb, float3(0.05f, 0.05f, 0.07f), pressed);

	// マウスの名称は本体の外に置くので、下地の色に合わせず明るいままにする。
	const float3 labelCol = isMouse ? In.Color.rgb : textCol;

	float3 col = iconCol;
	col = lerp(col, padCol, padFill);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);

	// 押す場所。明るく塗って「どこを押すのか」を色で示す。
	col = lerp(col, lerp(In.Color.rgb, float3(1, 1, 1), pressed), hilite);

	// 継ぎ目は下地より暗く。線で面が分かれて見える。
	col = lerp(col, padCol * 0.45f, seamLine);

	col = lerp(col, labelCol, label);
	col = lerp(col, textCol, glyph);
	col = lerp(col, float3(1, 1, 1), max(edge.y * 0.4f, burst));
	col = lerp(col, hot, aura * 0.8f);
	col = lerp(col, In.Color.rgb * 1.3f, cool * 0.6f);

	// 沈む瞬間に一度だけ白く飛ぶ
	col = lerp(col, float3(1, 1, 1), Bump(saturate(victory * 1.6f)) * 0.7f);

	float a = edge.x * 0.72f;
	a = max(a, edge.y * 0.24f);
	a = max(a, iconFill * (0.88f + pressed * 0.12f));
	a = max(a, iconLine * 0.75f);
	a = max(a, aura * 0.85f);
	a = max(a, cool * 0.9f);
	a = max(a, padFill * (0.5f + pressed * 0.45f));
	a = max(a, padRim * 0.9f);
	a = max(a, hilite * (0.85f + pressed * 0.15f));
	a = max(a, seamLine * 0.85f);
	a = max(a, label * 0.95f);
	a = max(a, glyph * 0.95f);
	a = max(a, burst * 0.8f);

	const float fade = 1.0f - sink;

	return float4(col, a * dim * fade * In.Color.a);
}

// 点で数を示す汎用表示。
// 空中攻撃の残り回数のように「あと何回できるか」を伝える。
// 数字より、点の数の方が一瞬で読める。
// Params : x=残り y=最大 z=直近で使ったか w=形(0=丸 1=羽根)
float4 DrawPips(VSOutput In)
{
	const float remain = In.Params.x;
	const float total = max(In.Params.y, 1.0f);
	const float used = saturate(In.Params.z);
	const float shape = In.Params.w;

	const float2 b = In.HalfSize;
	const float slotW = b.x * 2.0f / total;
	const float r = min(slotW * 0.30f, b.y * 0.80f);

	const float idxF = floor((In.Local.x + b.x) / slotW);
	const float cx = -b.x + slotW * (idxF + 0.5f);

	float2 p = In.Local - float2(cx, 0.0f);

	const bool has = (idxF < remain);

	// 使った直後の点は弾けて消える
	const bool justUsed = (abs(idxF - remain) < 0.5f) && (used > 0.001f);
	if (justUsed)
		p /= (1.0f + used * 0.6f);

	float d = (shape > 0.5f)
		? sdSmallFeather(Rotate(p, -0.5f), r * 2.2f, r * 0.5f)
		: sdCircle(p, r);

	const float body = Cover(d);
	const float ring = Stroke(d, HUD_LINE_BASE * 0.8f);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK * 0.75f, HUD_EDGE_HALO * 0.6f);

	float3 col = has ? In.Color.rgb : In.SubColor.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), justUsed ? used : 0.0f);

	float a = edge.x * 0.7f;
	a = max(a, edge.y * 0.22f);

	// 残っている点は塗り、使った点は輪郭だけ
	a = max(a, has ? (body * In.Color.a) : (ring * In.SubColor.a * 0.8f));

	if (justUsed)
		a = max(a, body * used);

	return float4(col, a);
}

// 撃破数と経過時間。画面左上。
//
// 上下に2段。1段目が撃破数、2段目が経過時間。
// それぞれの左に記号を置いて、何の数字かを言葉なしで伝える。
//
// 数字は「収めたい矩形」を指定して sdNumberFit に任せる。
// 字高をクアッド比で決めると、桁が増えた瞬間に枠から出る。
//
// Params : x=撃破数 y=総数 z=撃破の跳ね(1→0) w=経過秒
// Extra  : x=秒の小数部(針の運びと明滅) y=進捗(0〜1)
float4 DrawScore(VSOutput In)
{
	const int killed = (int) In.Params.x;
	const int total = (int) max(In.Params.y, 1.0f);
	const float punch = saturate(In.Params.z);
	const int seconds = (int) In.Params.w;

	const float subSec = frac(In.Extra.x);
	const float progress = saturate(In.Extra.y);

	const float2 b = In.HalfSize;

	//------------------------------------------
	// 区画
	//------------------------------------------
	// 列の位置を先に決めて、1段目と2段目で同じ場所に揃える。
	// 段ごとに違う位置だと、数字がバラバラに置かれて見える。
	const float iconS = b.y * 0.15f;

	// 左端の帯と重ならないよう、少し内側へ寄せる。
	// 端に付けると装飾に埋もれて記号だと分からない。
	const float colIcon = -b.x + iconS * 3.0f;

	const float colMainR = -b.x * 0.16f; // 主の数字(撃破数 / 分)の右端
	const float colMark = -b.x * 0.04f; // 斜線とコロンの中心
	const float colSubR = b.x * 0.46f; // 補助(総数 / 秒)の右端

	const float row1Y = -b.y * 0.44f;
	const float barY = b.y * 0.06f;
	const float row2Y = b.y * 0.54f;

	const float rowH = b.y * 0.28f; // 1行の半高

	// 撃破の印。ひよこのシルエット(胴体+頭+くちばし)。
	// くちばしと目は色を塗り分けたいので、マスクを別に持つ。
	float eggIcon;
	float beakIcon;
	float eyeIcon;
	{
		float2 q = In.Local - float2(colIcon, row1Y);

		// 倒した瞬間に回りながら弾ける(従来どおり)
		q = Rotate(q, punch * 0.55f);
		q /= (1.0f + punch * 0.30f);

		const float r = iconS * 1.25f;
		const float u = r * CHICK_SCALE;
		const float2 cq = ChickSpace(q, r);

		eggIcon = Cover(min(sdChickBody(cq), sdBeak(cq)) * u);
		beakIcon = Cover(sdBeak(cq) * u);
		// 小さいアイコンでは目が潰れるので、HUD本体(0.085)より大きめにする
		eyeIcon = Cover((length(cq - float2(0.32f, -0.50f)) - 0.13f) * u);
	}

	// 撃破数。倒した瞬間に跳ねて、行き過ぎてから戻る。
	const float sc = 1.0f + (EaseOutBack(punch) - punch) * 0.7f + punch * 0.26f;

	const float2 numBox = float2(b.x * 0.20f, rowH);
	const float2 kp = (In.Local - float2(colMainR - numBox.x, row1Y + punch * rowH * 0.30f)) / sc;
	const float killD = sdNumberFit(kp, killed, numBox, 0.20f, 0.30f, 1.0f);

	// 斜線。数字と同じ字高に揃えると、記号として一体に見える。
	float subD = sdSegment(In.Local,
		float2(colMark - b.x * 0.04f, row1Y + rowH * 0.85f),
		float2(colMark + b.x * 0.04f, row1Y - rowH * 0.85f)) - rowH * 0.10f;

	// 総数。撃破数と同じ大きさで、右へ寄せすぎない。
	subD = min(subD, sdNumberFit(In.Local - float2(colSubR - numBox.x, row1Y),
								 total, numBox, 0.20f, 0.30f, 1.0f));

	//------------------------------------------
	// 進捗の帯(中段)
	//------------------------------------------
	const float barW = b.x * 0.92f;
	const float barH = HUD_LINE_BASE * 0.9f;

	const float2 gp = In.Local - float2(0.0f, barY);
	const float barBg = Cover(sdBox(gp, float2(barW, barH)));
	const float barFl = Cover(sdBox(gp - float2(-barW + barW * progress, 0.0f),
									float2(barW * progress, barH)));

	float barNotch = 0.0f;
	{
		const float seg = (barW * 2.0f) / (float) total;
		const float lx = fmod(gp.x + barW, seg);
		barNotch = barBg * smoothstep(1.5f, 0.0f, min(lx, seg - lx));
	}

	float barFlow = 0.0f;
	{
		const float v = frac((gp.x + barW) / 14.0f - g_time * 0.55f);
		barFlow = barFl * smoothstep(0.55f, 0.95f, v) * 0.35f;
	}

	const float headX = -barW + barW * 2.0f * progress;
	const float barHead = barBg * exp(-abs(gp.x - headX) * 0.28f) * (0.35f + punch);

	//------------------------------------------
	// 2段目 : 経過時間
	//------------------------------------------
	const int mm = min(seconds / 60, 99);
	const int ss = seconds % 60;

	float clockIcon;
	{
		const float2 q = In.Local - float2(colIcon, row2Y);
		const float sa = (float) ss / 60.0f * HUD_PI * 2.0f;
		const float ma = ((float) mm + (float) ss / 60.0f) / 60.0f * HUD_PI * 2.0f;

		clockIcon = Cover(sdClockIcon(q, iconS, ma, sa));
	}

	// 分は主の列、秒は補助の列。1段目と同じ場所に揃う。
	// 秒は十の位と一の位を別に置いて必ず2桁にする。
	const float2 dBox = float2(numBox.x * 0.46f, rowH * 0.94f);

	float timeD = sdNumberFit(In.Local - float2(colMainR - numBox.x, row2Y),
							  mm, numBox, 0.20f, 0.28f, 1.0f);

	timeD = min(timeD, sdNumberFit(In.Local - float2(colSubR - numBox.x * 1.5f, row2Y),
								   ss / 10, dBox, 0.20f, 0.28f, 0.0f));
	timeD = min(timeD, sdNumberFit(In.Local - float2(colSubR - numBox.x * 0.5f, row2Y),
								   ss % 10, dBox, 0.20f, 0.28f, 0.0f));

	// コロン。分と秒の間に十分な余白を取る。詰めると3桁の数字に見える。
	float colon = 0.0f;
	{
		const float2 cp = In.Local - float2(colMark, row2Y);
		colon = Cover(sdCircle(cp - float2(0.0f, -rowH * 0.36f), rowH * 0.12f));
		colon = max(colon, Cover(sdCircle(cp - float2(0.0f, rowH * 0.36f), rowH * 0.12f)));
		colon *= (subSec < 0.5f) ? 1.0f : 0.30f;
	}

	const float tick = exp(-subSec * 9.0f);

	//------------------------------------------
	// 装飾
	//------------------------------------------
	// 右上がりに傾いた版面。水平の矩形より画面に馴染む。
	// 情報の区画であることを、囲まずに示す。
	float2 SK = In.Local;
	SK.x += SK.y * 0.16f;

	// 外枠。右端は開けておく。閉じると窮屈になる。
	float panel = 0.0f;
	{
		const float2 pb = b * float2(0.99f, 0.97f);

		// 上辺は長めに引く。短いと右上の跳ねだけが浮いて見える。
		panel = Cover(sdSegment(SK, float2(-pb.x, -pb.y), float2(pb.x * 0.74f, -pb.y))
					- HUD_LINE_THIN * 0.6f);
		panel = max(panel, Cover(sdSegment(SK, float2(-pb.x, pb.y), float2(pb.x * 0.80f, pb.y))
					- HUD_LINE_THIN * 0.6f));
		panel = max(panel, Cover(sdSegment(SK, float2(-pb.x, -pb.y), float2(-pb.x, pb.y))
					- HUD_LINE_THIN * 0.6f));

		// 右上と右下の跳ね
		panel = max(panel, Cover(sdSegment(SK,
			float2(pb.x * 0.74f, -pb.y), float2(pb.x * 0.91f, -pb.y + b.y * 0.22f))
			- HUD_LINE_THIN * 0.6f));
		panel = max(panel, Cover(sdSegment(SK,
			float2(pb.x * 0.80f, pb.y), float2(pb.x * 0.97f, pb.y - b.y * 0.22f))
			- HUD_LINE_THIN * 0.6f));
	}

	// 左端の帯。区画の始まりを面で示す。
	float accent = 0.0f;
	{
		const float x = -b.x * 0.99f;
		accent = Cover(sdBox(SK - float2(x + b.y * 0.07f, 0.0f),
							 float2(b.y * 0.07f, b.y * 0.86f)));

		// 帯の中を光がゆっくり昇る
		const float ph = frac(g_time * 0.22f);
		accent *= 0.55f + exp(-abs(SK.y - lerp(b.y, -b.y, ph)) * 0.05f) * 0.75f;
	}

	// 記号と数字を結ぶレール
	float rail = 0.0f;
	{
		rail = Cover(sdSegment(In.Local,
			float2(colIcon + iconS * 1.25f, row1Y),
			float2(colMainR - numBox.x * 2.0f - b.x * 0.02f, row1Y)) - HUD_LINE_THIN * 0.5f);
		rail = max(rail, Cover(sdSegment(In.Local,
			float2(colIcon + iconS * 1.25f, row2Y),
			float2(colMainR - numBox.x * 2.0f - b.x * 0.02f, row2Y)) - HUD_LINE_THIN * 0.5f));
	}

	// 四隅の鉤。倒した瞬間に伸びる。
	const float corner = Cover(sdCornerTick(SK, b * float2(0.99f, 0.97f),
											b.y * (0.14f + punch * 0.10f), HUD_LINE_THIN));

	float sweep = 0.0f;
	{
		const float ph = frac(g_time * 0.17f);
		sweep = exp(-abs(In.Local.x - lerp(-b.x, b.x, ph)) * 0.10f)
			  * smoothstep(1.0f, 0.72f, ph);
	}

	float ring = 0.0f;
	if (punch > 0.001f)
	{
		const float rr = iconS * (1.2f + (1.0f - punch) * 2.6f);
		ring = Cover(abs(length(In.Local - float2(colIcon, row1Y)) - rr)
				   - HUD_LINE_BASE * 0.55f) * punch;
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float big = Cover(killD);
	const float sub = Cover(subD);
	const float tim = Cover(timeD);

	const float2 edge = max(OutlineDual(killD, HUD_EDGE_DARK, HUD_EDGE_HALO),
							OutlineDual(min(subD, timeD),
										HUD_EDGE_DARK * 0.85f, HUD_EDGE_HALO * 0.8f));

	// 役割ごとに色を変える。全部同じ白だと、どこが本命か分からない。
	const float3 mainCol = In.Color.rgb;
	const float3 subCol = In.SubColor.rgb;
	const float3 iconCol = lerp(In.SubColor.rgb, In.Color.rgb, 0.45f);

	float3 col = mainCol;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, subCol, max(sub, tim) * 0.85f);
	col = lerp(col, iconCol, max(eggIcon, clockIcon));
	col = lerp(col, float3(1.00f, 0.55f, 0.10f), beakIcon);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), eyeIcon);
	col = lerp(col, subCol * 0.8f, max(rail, panel) * 0.7f);
	col = lerp(col, mainCol, accent);
	col = lerp(col, float3(1, 1, 1), max(max(edge.y * 0.45f, punch * big), ring));
	col += mainCol * sweep * 0.35f;

	float a = edge.x * 0.92f;
	a = max(a, edge.y * 0.34f);
	a = max(a, big);
	a = max(a, sub);
	a = max(a, tim);
	a = max(a, colon);
	a = max(a, eggIcon * (0.90f + punch * 0.10f));
	a = max(a, clockIcon * (0.85f + tick * 0.15f));
	// 1体も倒していない間は下地を沈める。
	// 空の帯だけが長く伸びていると、何かの区切り線に見える。
	a = max(a, barBg * lerp(0.12f, 0.30f, saturate(progress * 4.0f)));
	a = max(a, barFl);
	a = max(a, barFlow);
	a = max(a, barNotch * 0.45f);
	a = max(a, barHead * 0.80f);
	a = max(a, corner * 0.60f);
	a = max(a, panel * 0.34f);
	a = max(a, accent * 0.55f);
	a = max(a, rail * 0.28f);
	a = max(a, sweep * 0.22f);
	a = max(a, ring * 0.85f);

	return float4(col, a * In.Color.a);
}

//===================================================
// 本体
//===================================================
float4 main(VSOutput In) : SV_Target0
{
	// 種別ごとに分岐。nointerpolationで渡しているので
	// 1インスタンス内では必ず同じ経路を通る。
	float4 col = float4(0, 0, 0, 0);

	if (In.Kind == HUD_KIND_LOCK)
		col = DrawLock(In);
	else if (In.Kind == HUD_KIND_GAUGE)
		col = DrawEnemyBar(In);
	else if (In.Kind == HUD_KIND_WARN)
		col = DrawWarn(In);
	else if (In.Kind == HUD_KIND_ARROW)
		col = DrawArrow(In);
	else if (In.Kind == HUD_KIND_RING)
		col = DrawRing(In);
	else if (In.Kind == HUD_KIND_WAVE)
		col = DrawWave(In);
	else if (In.Kind == HUD_KIND_HEALTH)
		col = DrawHealth(In);
	else if (In.Kind == HUD_KIND_COUNT)
		col = DrawCount(In);
	else if (In.Kind == HUD_KIND_COMPASS)
		col = DrawCompass(In);
	else if (In.Kind == HUD_KIND_COMBO)
		col = DrawCombo(In);
	else if (In.Kind == HUD_KIND_FOCUS)
		col = DrawFocus(In);
	else if (In.Kind == HUD_KIND_DAMAGE)
		col = DrawDamage(In);
	else if (In.Kind == HUD_KIND_GUIDE)
		col = DrawGuide(In);
	else if (In.Kind == HUD_KIND_PIPS)
		col = DrawPips(In);
	else if (In.Kind == HUD_KIND_SCORE)
		col = DrawScore(In);

	// 完全に透明な部分は捨てる。クアッドの大半は空白なので効く。
	if (col.a < 0.004f)
		discard;

	return col;
}
