#include "inc_KdResultShader.hlsli"
#include "../HudShader/inc_KdHudSdf.hlsli"
#include "../HudShader/inc_KdHudFont.hlsli"

float4 DrawHeadline(VSOutput In)
{
	const int kind = (int) In.Params.x;
	const float appear = saturate(In.Params.y);

	const float2 b = In.HalfSize;

	float2 p = In.Local;
	float d;
	float h;

	if (kind == 0)
	{
		//------------------------------------------
		// CLEAR : 押印
		//------------------------------------------
		// 大きく飛び込んできて、行き過ぎてから止まる。
		const float k = EaseOutBack(appear);

		p /= (2.4f - k * 1.4f); // 大きい状態から縮んで着地
		p = Rotate(p, (1.0f - k) * 0.18f); // 傾きも戻る

		h = b.y * 0.52f;
		d = sdClearText(p, h);
	}
	else
	{
		//------------------------------------------
		// DIED : 沈む
		//------------------------------------------。
		const float k = 1.0f - (1.0f - appear) * (1.0f - appear);

		p.y -= (1.0f - k) * b.y * 0.85f;
		
		p.y -= sin(g_time * 0.35f) * b.y * 0.02f * k;

		h = b.y * 0.30f;
		d = sdDeadText(p, h);
	}

	const float body = Cover(d);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK * 1.2f, HUD_EDGE_HALO * 1.4f);

	//------------------------------------------
	// 下線
	//------------------------------------------
	// 見出しの下に1本引く。帳票の見出しらしくなる。
	float rule = 0.0f;
	{
		const float w = b.x * (0.30f + appear * 0.52f);
		rule = Cover(sdBox(In.Local - float2(0.0f, b.y * 0.86f),
						   float2(w, HUD_LINE_THIN * 0.7f)));

		// 両端の鋲
		rule = max(rule, Cover(sdRivet(In.Local - float2(-w, b.y * 0.86f), b.y * 0.05f)));
		rule = max(rule, Cover(sdRivet(In.Local - float2(w, b.y * 0.86f), b.y * 0.05f)));
	}

	//------------------------------------------
	// 敗北の演出
	//------------------------------------------
	float stain = 0.0f;
	float feather = 0.0f;

	if (kind != 0)
	{
		// 滲み。文字の下端から下へ、ノイズで縁を崩しながら広がる。
		{
			const float2 q = In.Local / b;

			const float grow = smoothstep(0.0f, 1.0f, appear) * 0.55f
							 + sin(g_time * 0.22f) * 0.04f;

			const float n = Noise21(q * float2(3.4f, 1.8f)) * 0.30f;
			const float edgeY = 0.18f + grow + n;

			stain = smoothstep(edgeY, edgeY - 0.30f, q.y)
				  * smoothstep(0.10f, 0.30f, q.y)
				  * smoothstep(1.2f, 0.55f, abs(q.x));
		}

		// 落ちる羽根。上から力なく、揺れながら降りる。
		[unroll]
		for (int i = 0; i < 5; ++i)
		{
			const float seed = Hash11((float) i * 4.7f);
			const float life = frac(g_time * 0.11f + seed);

			const float2 o = float2((seed - 0.5f) * b.x * 1.7f
									+ sin(life * 5.0f + seed * 6.0f) * b.y * 0.5f,
									-b.y * 1.4f + life * b.y * 3.4f);

			float2 q = In.Local - o;
			q = Rotate(q, life * 2.6f + seed * 6.28f);

			const float s = b.y * (0.10f + seed * 0.05f);

			feather = max(feather, Cover(sdSmallFeather(q, s * 2.2f, s * 0.5f))
								 * (1.0f - life) * appear);
		}
	}

	//------------------------------------------
	// 光の走り
	//------------------------------------------
	// 勝ちだけ。負けた画面で光らせても嘘になる。
	float sweep = 0.0f;
	if (kind == 0)
	{
		const float ph = frac(g_time * 0.30f);
		sweep = body * exp(-abs(p.x - lerp(-b.x, b.x, ph)) * 0.06f)
			  * smoothstep(1.0f, 0.75f, ph);
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	float3 col = In.Color.rgb;
	col = lerp(col, float3(0.30f, 0.03f, 0.03f), stain);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), max(edge.y * 0.5f, sweep));
	col = lerp(col, In.SubColor.rgb, rule * 0.6f);
	col = lerp(col, float3(0.62f, 0.56f, 0.52f), feather);

	float a = edge.x * 0.85f;
	a = max(a, edge.y * 0.30f);
	a = max(a, body);
	a = max(a, sweep * 0.7f);
	a = max(a, rule * 0.45f);
	a = max(a, stain * 0.42f);
	a = max(a, feather * 0.55f);

	return float4(col, a * In.Color.a * smoothstep(0.0f, 0.30f, appear));
}

// 成績の1行
float4 DrawStat(VSOutput In)
{
	const int kind = (int) In.Params.x;
	const float appear = saturate(In.Params.y);
	const int valA = (int) In.Params.z;
	const int valB = (int) In.Params.w;

	const float perfect = saturate(In.Extra.x);

	const float2 b = In.HalfSize;

	// 左から滑り込む
	float2 p = In.Local;
	p.x -= (1.0f - EaseOutBack(appear)) * b.x * 0.65f;

	// 出切った直後だけ一度弾む
	{
		const float land = saturate((appear - 0.80f) / 0.20f);
		p /= (1.0f + Bump(land) * 0.06f);
	}

	const float h = b.y * 0.52f;

	//------------------------------------------
	// 帯
	//------------------------------------------
	float plate = 0.0f;
	float plateEdge = 0.0f;
	float plateHead = 0.0f;

	{
		const float2 pb = float2(b.x * 0.99f, b.y * 0.74f);

		// 左端だけ斜めに切る。右は水平のまま。
		float2 q = p;
		q.x += q.y * 0.10f;

		const float d = sdBox(q, pb);

		// 出現に合わせて左から伸びる
		const float grow = -pb.x + pb.x * 2.0f * smoothstep(0.0f, 0.85f, appear);
		const float mask = step(q.x, grow);

		plate = Cover(d) * mask;

		// 上下の罫線
		plateEdge = Cover(abs(abs(q.y) - pb.y) - HUD_LINE_THIN * 0.55f) * mask;

		// 伸びる先端の光
		plateHead = Cover(d) * exp(-abs(q.x - grow) * 0.14f)
				  * (1.0f - smoothstep(0.75f, 1.0f, appear));
	}

	//------------------------------------------
	// 左端の印
	//------------------------------------------
	// 帯の頭に小さな菱形
	const float bullet = Cover(sdRivet(p - float2(-b.x * 0.96f, 0.0f), b.y * 0.10f))
					   * smoothstep(0.15f, 0.45f, appear);

	//------------------------------------------
	// 項目名(左)
	//------------------------------------------
	const float labelD = sdStatLabel(p - float2(-b.x * 0.86f, 0.0f), kind, h * 0.72f);

	//------------------------------------------
	// 値(右)
	//------------------------------------------
	float valueD = 1e9f;

	if (kind == RES_STAT_TIME)
	{
		// 分:秒。秒は必ず2桁。
		const float w = h * 0.56f;
		const float th = h * 0.20f;
		const float space = w * 0.42f;
		const float pitch = w * 2.0f + space;

		const float right = b.x * 0.94f;

		valueD = sdNumberAt(p, valB % 10, right - w * 2.0f, w, h, th, space);
		valueD = min(valueD, sdNumberAt(p, valB / 10, right - w * 2.0f - pitch,
										w, h, th, space));

		const float colonX = right - w * 2.0f - pitch - w * 1.4f;
		valueD = min(valueD, sdCircle(p - float2(colonX, -h * 0.34f), th * 1.1f));
		valueD = min(valueD, sdCircle(p - float2(colonX, h * 0.34f), th * 1.1f));

		valueD = min(valueD, sdNumberAt(p, valA, colonX - w * 1.4f - NumberSpan(valA, w, space),
										w, h, th, space));
	}
	else if (kind == RES_STAT_LIFE)
	{
		valueD = sdPercentText(p - float2(b.x * 0.94f, 0.0f), valA, h, 1.0f);
	}
	else
	{
		const float w = h * 0.56f;
		valueD = sdNumberAt(p, valA, b.x * 0.94f - NumberSpan(valA, w, w * 0.55f),
							w, h, h * 0.20f, w * 0.55f);
	}

	//------------------------------------------
	// 満点の印
	//------------------------------------------
	// ノーダメージや目標タイム内。項目単位の達成が見えると次の目標になる。
	float star = 0.0f;
	if (perfect > 0.5f)
	{
		float2 q = p - float2(b.x * 1.04f, 0.0f);
		q = Rotate(q, g_time * 0.9f);

		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			const float a2 = HUD_PI * 0.5f * (float) i;
			star = max(star, Cover(sdEllipse(Rotate(q, a2),
											 float2(h * 0.10f, h * 0.42f))));
		}
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float label = Cover(labelD);
	const float value = Cover(valueD);

	const float2 edge = max(OutlineDual(labelD, HUD_EDGE_DARK, HUD_EDGE_HALO * 0.8f),
							OutlineDual(valueD, HUD_EDGE_DARK, HUD_EDGE_HALO * 0.8f));

	// 項目名は値より控えめだが、読めなくては意味が無い
	float3 col = In.SubColor.rgb;
	col = lerp(col, float3(0.8f, 0.75f, 0.5f), plate * 0.85f);
	col = lerp(col, In.SubColor.rgb, max(plateEdge, bullet));
	col += In.Color.rgb * plateHead * 0.55f;
	col = lerp(col, float3(0.2f, 0.2f, 0.2f), edge.x);
	col = lerp(col, In.Color.rgb, value);
	col = lerp(col, float3(1.0f, 0.86f, 0.42f), star);

	// 帯を最初に置く。文字より下に来るので値を小さく取る。
	float a = plate * kStatPlateAlpha;
	a = max(a, plateEdge * 0.42f);
	a = max(a, plateHead * 0.55f);
	a = max(a, bullet * 0.75f);
	a = max(a, edge.x * 0.75f);
	a = max(a, edge.y * 0.26f);
	a = max(a, label);
	a = max(a, value);
	a = max(a, star * 0.95f);

	return float4(col, a * In.Color.a * smoothstep(0.0f, 0.30f, appear));
}

// ランクの殻
float4 DrawRank(VSOutput In)
{
	const int rank = (int) In.Params.x;
	const float t = saturate(In.Params.y);

	const float2 b = In.HalfSize;
	const float R = min(b.x, b.y);

	// 段取りの各区間を 0〜1 に正規化しておく。
	// 生の t で毎回 smoothstep を書くと、境目を直すのが面倒になる。
	const float tGather = saturate(t / 0.25f);
	const float tShake = saturate((t - 0.25f) / 0.20f);
	const float tCrack = saturate((t - 0.45f) / 0.13f);
	const float tBreak = saturate((t - 0.58f) / 0.14f);
	const float tAfter = saturate((t - 0.72f) / 0.28f);

	//------------------------------------------
	// 集まる光
	//------------------------------------------
	float spark = 0.0f;
	if (tGather < 1.0f)
	{
		[unroll]
		for (int i = 0; i < 12; ++i)
		{
			const float seed = Hash11((float) i * 5.3f);
			const float a2 = seed * 6.283f;

			const float dist = (1.0f - tGather) * R * (1.3f + seed * 1.5f);
			const float2 o = float2(cos(a2), sin(a2)) * dist;

			spark = max(spark, Cover(sdCircle(In.Local - o,
											  R * 0.055f * (1.0f - tGather * 0.6f))));
		}
		spark *= (1.0f - tGather);
	}

	//------------------------------------------
	// 卵
	//------------------------------------------
	float2 p = In.Local;
	{
		const float wob = tShake * (1.0f - tBreak);

		p.x -= sin(g_time * 17.0f) * R * 0.045f * wob;
		p = Rotate(p, sin(g_time * 13.0f) * 0.06f * wob);
	}

	const float eggR = R * 0.62f * tGather;
	const float eggD = sdEgg(p, eggR);

	// 殻は塗らずに輪郭だけ。中が透けている方が「割れる前」に見える。
	const float shell = Stroke(eggD, HUD_LINE_BOLD * 0.9f);
	const float inner = Cover(eggD) * 0.10f;

	//------------------------------------------
	// ひび
	//------------------------------------------
	float crack = 0.0f;
	if (tCrack > 0.0f)
	{
		// 主線。下端から上へ、蛇行しながら伸びる。
		const float grow = eggR * 1.9f * smoothstep(0.0f, 0.85f, tCrack);
		const float bottom = eggR * 1.15f;

		{
			float2 q = p;

			// 蛇行。直線だと割れ目に見えない。
			q.x -= sin(q.y * 11.0f) * eggR * 0.10f;

			crack = Cover(sdSegment(q, float2(0.0f, bottom),
									   float2(0.0f, bottom - grow))
						- HUD_LINE_BASE * 0.55f);
		}

		// 枝。主線の途中から左右へ分かれる。
		[unroll]
		for (int i = 0; i < 4; ++i)
		{
			const float seed = Hash11((float) i * 3.1f);

			// 分岐点。下ほど早く出る。
			const float at = 0.25f + seed * 0.55f;
			const float startY = bottom - grow * at;

			// 主線が到達してから伸び始める
			const float bt = saturate((grow / (eggR * 1.9f) - at) * 3.0f);
			if (bt <= 0.0f)
				continue;

			const float side = (fmod((float) i, 2.0f) < 0.5f) ? 1.0f : -1.0f;
			const float len = eggR * (0.30f + seed * 0.35f) * bt;

			const float2 dir = float2(side * 0.85f, -0.52f - seed * 0.3f);

			crack = max(crack, Cover(sdSegment(p,
				float2(0.0f, startY),
				float2(0.0f, startY) + dir * len) - HUD_LINE_BASE * 0.40f));
		}

		// ひびは卵の内側だけ
		crack *= Cover(eggD);
	}

	//------------------------------------------
	// 割れて飛ぶ殻
	//------------------------------------------
	float pieces = 0.0f;
	if (tBreak > 0.0f)
	{
		const float k = tBreak;

		[unroll]
		for (int i = 0; i < 7; ++i)
		{
			const float seed = Hash11((float) i * 4.7f);

			// 下から裂けたので、殻は左右と下へ開いて飛ぶ。
			// 全方位へ均等に飛ばすと、爆発したように見える。
			const float a2 = (seed - 0.5f) * 2.4f
						   + ((fmod((float) i, 2.0f) < 0.5f) ? 0.0f : HUD_PI);

			const float2 dir = float2(cos(a2), abs(sin(a2)) * 0.7f + 0.3f);
			const float2 o = dir * k * R * (0.9f + seed * 1.1f)
						   + float2(0.0f, -k * R * 0.35f + k * k * R * 1.9f);

			float2 q = Rotate(In.Local - o, k * (seed - 0.5f) * 9.0f);

			// 破片は殻の一部なので、細長い弧にする。
			const float s = R * (0.10f + seed * 0.07f) * (1.0f - k * 0.35f);
			pieces = max(pieces, Cover(sdArc(q, s, s * 0.30f, -1.1f, 1.1f)));
		}
		pieces *= (1.0f - smoothstep(0.55f, 1.0f, k));
	}

	//------------------------------------------
	// 割れた瞬間の閃光
	//------------------------------------------
	float flash = 0.0f;
	if (tBreak > 0.0f && tBreak < 1.0f)
	{
		const float k = 1.0f - tBreak;

		// 中心が白く飛ぶ
		flash = Cover(sdCircle(In.Local, R * (0.2f + (1.0f - k) * 0.5f))) * k * k;

		// 外へ抜ける輪
		const float rr = R * (0.5f + (1.0f - k) * 1.5f);
		flash = max(flash, Cover(abs(length(In.Local) - rr) - HUD_LINE_BOLD * 0.8f) * k);
	}

	//------------------------------------------
	// 余韻
	//------------------------------------------
	float halo = 0.0f;
	float feather = 0.0f;

	if (tAfter > 0.0f)
	{
		// 囲みの輪
		const float ringD = abs(length(In.Local) - R * 0.92f) - HUD_LINE_BASE * 0.6f;
		halo = Cover(ringD) * 0.35f;

		// 上位ほど光が回る。良い結果ほど長く見せると、狙う動機になる。
		if (rank >= 3)
		{
			float ang = atan2(In.Local.x, -In.Local.y) / (HUD_PI * 2.0f);
			ang = (ang < 0.0f) ? ang + 1.0f : ang;

			halo = max(halo, Cover(ringD)
					 * exp(-frac(ang - frac(g_time * 0.35f)) * 7.0f));
		}

		// 舞う羽根。割れた殻から出た羽根、という見立て。
		[unroll]
		for (int i = 0; i < 6; ++i)
		{
			const float seed = Hash11((float) i * 6.1f);
			const float life = frac(g_time * 0.22f + seed);

			const float2 o = float2((seed - 0.5f) * R * 2.0f
									+ sin(life * 5.0f + seed * 6.0f) * R * 0.3f,
									-R * 0.9f + life * R * 2.4f);

			float2 q = Rotate(In.Local - o, life * 3.4f + seed * 6.28f);

			const float s = R * (0.07f + seed * 0.04f);
			feather = max(feather, Cover(sdSmallFeather(q, s * 2.2f, s * 0.5f))
								 * (1.0f - life));
		}

		halo *= tAfter;
		feather *= tAfter;
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float3 shellCol = float3(0.94f, 0.90f, 0.80f);

	float3 col = shellCol;
	col = lerp(col, In.Color.rgb, max(halo, spark));
	col = lerp(col, float3(1, 1, 1), max(flash, crack * 0.5f));
	col = lerp(col, float3(0.88f, 0.84f, 0.76f), feather);

	// 割れ切ったら殻は消す
	const float alive = 1.0f - smoothstep(0.0f, 0.6f, tBreak);

	float a = 0.0f;
	a = max(a, shell * alive);
	a = max(a, inner * alive);
	a = max(a, crack * alive);
	a = max(a, spark * 0.9f);
	a = max(a, pieces);
	a = max(a, flash);
	a = max(a, halo * 0.85f);
	a = max(a, feather * 0.8f);

	return float4(col, a * In.Color.a);
}

//===================================================
// 本体
//===================================================
float4 main(VSOutput In) : SV_Target0
{
	float4 col = float4(0, 0, 0, 0);

	if (In.Kind == RES_KIND_HEADLINE)
		col = DrawHeadline(In);
	else if (In.Kind == RES_KIND_STAT)
		col = DrawStat(In);
	else if (In.Kind == RES_KIND_RANK)
		col = DrawRank(In);

	// 完全に透明な部分は捨てる。クアッドの大半は空白なので効く。
	if (col.a < 0.004f)
		discard;

	return col;
}
