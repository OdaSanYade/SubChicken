#include "inc_KdUiShader.hlsli"
#include "../HudShader/inc_KdHudSdf.hlsli"
#include "../HudShader/inc_KdHudFont.hlsli"

//===================================================
//
// UI共通部品 ピクセルシェーダ
//
// 画面をまたいで使い回す部品。
// 語は文字コードで受け取るので、語を足すのにここを触る必要は無い。
//
//===================================================

// ボタンの記号。決定に使うキーを形で示す。
// ボタンとヒントの両方で使うので、関数へ切り出しておく。
//  _code : 0=A..25=Z、負で文字なし。
float sdBtnIcon(float2 p, int _shape, int _code, float s)
{
	if (_shape == UI_ICON_FACE)
	{
		// 丸の中に文字。Xboxのフェイスボタンに合わせる。
		float d = abs(sdCircle(p, s)) - HUD_LINE_BASE * 0.55f;

		if (_code >= 0)
			d = min(d, sdLetter(p, _code, s * 0.34f, s * 0.46f, s * 0.13f));

		return d;
	}

	if (_shape == UI_ICON_ENTER)
	{
		// キーキャップに矢印
		float d = abs(sdRoundBox(p, float2(s * 1.05f, s * 0.86f), s * 0.22f))
				- HUD_LINE_BASE * 0.55f;
		return min(d, sdEnterMark(p, s * 0.72f, s * 0.11f));
	}

	if (_shape == UI_ICON_SPACE)
	{
		// 横長のキーキャップ。中に1本引いてスペースバーに見せる。
		// 枠だけだと、ただの細長い箱と区別が付かない。
		float d = abs(sdRoundBox(p, float2(s * 1.5f, s * 0.62f), s * 0.20f))
				- HUD_LINE_BASE * 0.55f;
		return min(d, sdBox(p - float2(0.0f, s * 0.18f),
							float2(s * 0.90f, HUD_LINE_THIN * 0.5f)));
	}

	if (_shape == UI_ICON_SHOULDER)
	{
		// 横長の角丸に「LB」「RB」
		float d = abs(sdRoundBox(p - float2(0.0f, s * 0.10f),
								 float2(s * 1.10f, s * 0.52f), s * 0.22f))
				- HUD_LINE_BASE * 0.55f;

		d = min(d, sdLabel3(p - float2(0.0f, s * 0.10f), int3(_code, 1, -1),
							s * 0.26f, s * 0.34f, s * 0.10f, s * 0.30f));
		return d;
	}

	if (_shape == UI_ICON_TRIGGER)
	{
		// 上が丸い縦長。引く形に見せる。
		float d = abs(sdRoundBox(p - float2(0.0f, s * 0.18f),
								 float2(s * 0.72f, s * 0.56f), s * 0.20f))
				- HUD_LINE_BASE * 0.55f;

		d = min(d, abs(sdEllipse(p - float2(0.0f, -s * 0.38f),
								 float2(s * 0.72f, s * 0.44f))) - HUD_LINE_BASE * 0.5f);

		d = min(d, sdLabel3(p - float2(0.0f, s * 0.20f), int3(_code, 19, -1),
							s * 0.24f, s * 0.32f, s * 0.095f, s * 0.28f));
		return d;
	}

	// キーキャップ
	float d = abs(sdRoundBox(p, float2(s * 0.92f, s * 0.86f), s * 0.18f))
			- HUD_LINE_BASE * 0.55f;

	if (_code >= 0)
		d = min(d, sdLetter(p, _code, s * 0.30f, s * 0.44f, s * 0.12f));

	return d;
}

// ボタン。
//
// 状態は3つ。どれも見た目で区別が付くようにする。
//   通常   : 沈んだ色。輪郭だけ。
//   ホバー : 枠が太く広がり、中が塗られ、左右に矢印が出る
//   押下   : 沈み込んで白く飛び、外へ輪が抜ける
//
// 常時のアニメーションはホバー中だけに絞る。
// 全部が動いていると、どれが選ばれているのか分からなくなる。
//
// Params : 語の1〜4文字目
// Extra  : x,y=語の5〜6文字目 z=選択中(+2)と押下(0〜1) w=出現(0→1)
// Tilt   : 使わない。共有の頂点シェーダがクアッドを回すため。
float4 DrawButton(VSOutput In)
{
	const float2 b = In.HalfSize;

	int codes[8];
	int count;
	UiDecodeWord(In.Params, In.Extra, codes, count);

	float focus;
	float press;
	float ghost;
	UiDecodeState(In.Extra.z, focus, press, ghost);

	const float appear = saturate(In.Extra.w);

	// SubColor.a の整数部がアイコンの番号、小数部が本来のアルファ。
	// 空きチャンネルが無いので、2値を1つに畳んで運ぶ。
	// ★C++側と対で扱うこと。
	int iconShape;
	int iconCode;
	UiDecodeIcon(In.SubColor.a, iconShape, iconCode);
	
	const float subAlpha = frac(In.SubColor.a);

	// 形。Kind で分ける。
	const int shape = (int) (In.Kind - UI_KIND_BUTTON);

	//------------------------------------------
	// 動き
	//------------------------------------------
	float2 p = In.Local;

	// 下から現れる
	p.y -= (1.0f - EaseOutBack(appear)) * b.y * 1.4f;

	// ホバー中はゆっくり息をする。選ばれているものだけが動く。
	if (focus > 0.5f)
	{
		p /= (1.0f + sin(g_time * 2.1f) * 0.022f);

		// わずかに前へ出る
		p.x += sin(g_time * 1.3f) * b.y * 0.03f;
	}

	// 選択が離れた直後は、少し遅れて元へ戻る。
	// 尾を引くと、どこから来たのかが動きで読める。
	if (ghost > 0.001f)
		p /= (1.0f + ghost * 0.04f);

	// 押した瞬間は沈む
	p /= (1.0f - press * 0.12f);
	p.y -= press * b.y * 0.10f;

	//------------------------------------------
	// 枠
	//------------------------------------------
	float2 sp = p;
	float2 fb;
	float corner;

	// ★枠はクアッドの内側に収める。
	//   傾き(sp.y * 0.14)と、外へ出る矢印(fb.x + s * 1.5)のぶんを
	//   見込んでおかないと、選択した瞬間に左右が切れる。
	if (shape == UI_BTN_SLIM)
	{
		// 右上がりに傾ける。傾けるのは枠だけで、文字は水平に保つ。
		sp.x += sp.y * 0.14f;
		fb = b * float2(0.82f, 0.62f) * (1.0f + focus * 0.05f);
		corner = b.y * 0.14f;
	}
	else if (shape == UI_BTN_ROUND)
	{
		fb = b * float2(0.82f, 0.68f) * (1.0f + focus * 0.05f);
		corner = b.y * 0.34f;
	}
	else if (shape == UI_BTN_PLATE)
	{
		// 帳票の欄。傾けず、角も立てる。
		// リザルトは「記録が並ぶ紙」の質感が合うので、
		// タイトルの筆致とは意図して別の見た目にする。
		fb = b * float2(0.86f, 0.60f) * (1.0f + focus * 0.03f);
		corner = b.y * 0.04f;
	}
	else
	{
		fb = b * float2(0.84f, 0.64f) * (1.0f + focus * 0.05f);
		corner = b.y * 0.12f;
	}

	float boxD = sdRoundBox(sp, fb, corner);

	//------------------------------------------
	// 輪郭を荒らす
	//------------------------------------------
	// タイトルのロゴが筆で叩きつけた質感なので、そこに並ぶボタンも
	// 整った矩形にしない。距離場をノイズでずらすとかすれた縁になる。
	//
	// リザルト(PLATE)には掛けない。あちらは帳票の質感なので、
	// 荒らすと「記録」に見えなくなる。
	//
	// 動かさないのが要点。時間で揺らすと安っぽく見えるうえ、
	// 常時動くものが増えて選択中の項目が埋もれる。
	if (shape == UI_BTN_SLIM)
	{
		const float2 n = sp / max(b.y, 1e-3f);

		// 粗い荒れと細かい荒れを重ねる。1種類だと規則的に見える。
		const float rough = (Noise21(n * 2.6f) - 0.5f) * 2.0f
						  + (Noise21(n * 7.4f) - 0.5f) * 0.8f;

		boxD += rough * b.y * 0.055f;
	}

	//------------------------------------------
	// 影
	//------------------------------------------
	// 下へずらした同じ形を薄く敷く。
	// 背景から浮いて見えるので、明るい場所でも枠が沈まない。
	// 本体と同じ距離場を下へずらすだけ。
	// 別に計算すると荒れの位置がずれて、影が輪郭からはみ出す。
	const float shadow = Cover(boxD - b.y * 0.10f)
					   * (0.20f + focus * 0.10f)
					   * (1.0f - Cover(boxD));

	// 選択中は枠が太くなる。
	// 色だけで示すと、どちらが選ばれているか一瞬迷う。
	const float frame = Stroke(boxD, HUD_LINE_BASE * (1.0f + focus * 1.3f));

	// 選択中は中を塗る。塗りがあると「今ここ」が面で伝わる。
	// 離れた直後は塗りが残って薄れていく。
	const float fill = Cover(boxD) * (0.08f + focus * 0.26f + ghost * 0.20f);

	// 塗りの中を光がゆっくり流れる。ホバー中だけ。
	float flow = 0.0f;
	if (focus > 0.5f)
	{
		const float v = frac((sp.x + fb.x) / (fb.x * 0.7f) - g_time * 0.35f);
		flow = Cover(boxD) * smoothstep(0.55f, 0.95f, v) * 0.30f;
	}

	// 選択が移ってきた瞬間、色が上から下へ流れ落ちる。
	// 「今ここへ来た」が動きで伝わる。
	// Params は語で埋まっているので、移動量は focus の立ち上がりから作る。
	float sweepIn = 0.0f;
	if (focus > 0.5f)
	{
		// enterT は C++側で 1→0 に減衰させて Extra.z の小数部で運ぶ…と
		// したいが押下と衝突する。ここでは focus の脈で代用する。
		const float ph = frac(g_time * 0.8f);
		sweepIn = Cover(boxD) * exp(-abs((sp.y + fb.y) / (fb.y * 2.0f) - ph) * 6.0f) * 0.18f;
	}

	// 四隅の鉤。選択中だけ伸びる。
	float tick = Cover(sdCornerTick(sp, fb * 1.05f,
									b.y * (0.14f + focus * 0.22f), HUD_LINE_THIN));

	// 帳票は鉤の代わりに、左端の帯と下の罫線で示す。
	// 角を飾ると装飾に見えるが、罫線なら「記入欄」に見える。
	if (shape == UI_BTN_PLATE)
	{
		// 左端の帯。選択中は太くなる。
		const float bw = b.y * (0.09f + focus * 0.10f);
		tick = Cover(sdBox(sp - float2(-fb.x + bw, 0.0f), float2(bw, fb.y)));

		// 下の罫線。欄の底を示す。
		tick = max(tick, Cover(sdBox(sp - float2(0.0f, fb.y),
									 float2(fb.x, HUD_LINE_THIN * 0.6f))));

		// 右端の短い切り欠き。紙を切り出した縁に見せる。
		tick = max(tick, Cover(sdBox(sp - float2(fb.x, -fb.y * 0.55f),
									 float2(b.y * 0.10f, HUD_LINE_THIN * 0.6f))));
	}

	//------------------------------------------
	// 文字
	//------------------------------------------
	// 枠は傾けても文字は水平のまま。傾けると読みにくくなる。
	const float h = b.y * (0.34f + focus * 0.02f);

	// アイコンを置くぶん、語は右へ寄せる。
	// 中央のままだとアイコンと重なる。
	const float iconS = b.y * 0.36f;
	const float iconX = -fb.x + iconS * 1.9f;
	const float textShift = (iconShape != UI_ICON_NONE) ? (iconS * 1.1f) : 0.0f;

	const float textD = sdWordCentered(p - float2(textShift, 0.0f), codes, count, h);
	const float text = Cover(textD);

	//------------------------------------------
	// 先頭のアイコン
	//------------------------------------------
	// 「押せば進む」を言葉なしで伝える。
	// 選択中だけ出す。全部に付けると、どれが押せるのか分からなくなる。
	float iconFill = 0.0f;
	float iconRim = 0.0f;

	if (iconShape != UI_ICON_NONE && focus > 0.5f)
	{
		// 押される直前を示すので、ゆっくり脈打たせる
		const float beat = 1.0f + sin(g_time * 2.8f) * 0.05f;
		const float2 q = (p - float2(iconX, 0.0f)) / beat;

		const float d = sdBtnIcon(q, iconShape, iconCode, iconS);

		iconFill = Cover(d);
		iconRim = Stroke(d, HUD_LINE_THIN * 1.4f);
	}

	// 選択中は文字を光がなぞる
	float shine = 0.0f;
	if (focus > 0.5f)
	{
		const float ph = frac(g_time * 0.45f);
		shine = text * exp(-abs(p.x - lerp(-fb.x, fb.x, ph)) * 0.055f)
			  * smoothstep(1.0f, 0.72f, ph);
	}

	//------------------------------------------
	// 選択中の矢印
	//------------------------------------------
	// 左右から挟んで「ここ」を指す。跳ねる動きで目を引く。
	float caret = 0.0f;
	if (focus > 0.5f)
	{
		// 内へ寄っては戻る。単純な上下より方向が伝わる。
		const float bob = (0.5f + sin(g_time * 3.6f) * 0.5f) * b.y * 0.16f;
		const float s = b.y * 0.26f;

		[unroll]
		for (int i = 0; i < 2; ++i)
		{
			const float side = (i == 0) ? -1.0f : 1.0f;
			const float2 q = sp - float2(side * (fb.x + s * 1.5f - bob), 0.0f);

			caret = max(caret, Cover(sdTriangle(float2(q.x * -side, q.y),
				float2(s * 0.85f, 0.0f),
				float2(-s * 0.55f, s * 0.80f),
				float2(-s * 0.55f, -s * 0.80f))));
		}
	}

	//------------------------------------------
	// 押した瞬間
	//------------------------------------------
	// 枠から外へ輪が2枚抜ける。1枚だと押した実感が薄い。
	float burst = 0.0f;
	if (press > 0.001f)
	{
		const float k = 1.0f - press;

		burst = Cover(abs(boxD - k * b.y * 1.1f) - HUD_LINE_BASE * 0.8f) * press;
		burst = max(burst,
			Cover(abs(boxD - k * b.y * 0.6f) - HUD_LINE_BASE * 0.5f) * press * 0.7f);
	}

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float2 edge = max(OutlineDual(textD, HUD_EDGE_DARK, HUD_EDGE_HALO),
							OutlineDual(boxD, HUD_EDGE_DARK * 0.8f, HUD_EDGE_HALO * 0.7f));

	// 選択中は主色、そうでなければ副色。
	// 副色のアルファはアイコン番号と畳んであるので、小数部だけを使う。
	// 離れた直後は色も残す。急に沈むと、移動が瞬間移動に見える。
	const float warm = saturate(focus + ghost * 0.7f);

	const float3 base = lerp(In.SubColor.rgb, In.Color.rgb, warm);
	const float baseAlpha = lerp(subAlpha, In.Color.a, warm);

	float3 col = base;
	col = lerp(col, float3(0.01f, 0.01f, 0.02f), shadow);
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1),
			   max(max(edge.y * 0.45f, burst), max(press * text, shine)));

	float a = edge.x * 0.72f;
	a = max(a, edge.y * 0.26f);
	a = max(a, shadow);
	a = max(a, frame * (0.55f + focus * 0.45f + ghost * 0.30f));
	a = max(a, fill);
	a = max(a, flow);
	a = max(a, sweepIn);
	a = max(a, tick * (0.35f + focus * 0.5f));
	a = max(a, text * (0.78f + focus * 0.22f));
	a = max(a, iconFill * 0.92f);
	a = max(a, iconRim * 0.6f);
	a = max(a, shine * 0.8f);
	a = max(a, caret * 0.95f);
	a = max(a, burst * 0.9f);

	return float4(col, a * baseAlpha * smoothstep(0.0f, 0.30f, appear));
}

// 暗幕。画面を沈めて、上に置くものを読ませる。
// 同じ明るさのまま情報を重ねると、どちらも読まれない。
// Params : x=濃さ(0→1)
float4 DrawVeil(VSOutput In)
{
	const float t = saturate(In.Params.x);

	const float2 b = In.HalfSize;
	const float2 q = In.Local / b;

	// 中央ほど薄い。周縁から閉じてくる。
	const float r = length(q * float2(1.0f, 1.4f));
	const float vign = smoothstep(0.20f, 1.15f, r);

	// 閉じる動き。上下から幕が寄る。
	const float closeY = smoothstep(t * 1.6f, t * 1.6f - 0.35f, abs(q.y));

	float a = saturate(0.55f + vign * 0.45f) * t;
	a = max(a, closeY * t * 0.85f);

	// 走査線。静止した黒だけだと画面が死んで見える。
	const float scan = 0.5f + sin(In.Local.y * 0.55f + g_time * 1.1f) * 0.5f;
	a *= 0.94f + scan * 0.06f;

	return float4(In.Color.rgb, saturate(a) * In.Color.a);
}

// 枠。項目をまとめる区画。
// 全周を囲まず、右上がりに傾けて四隅の鉤で示す。
// 囲い切ると窮屈になるので、右端は開けておく。
// Params : x=出現(0→1) y=傾き
float4 DrawPanel(VSOutput In)
{
	const float appear = saturate(In.Params.x);
	const float skew = In.Params.y;

	const float2 b = In.HalfSize;

	// 左から開く
	float2 p = In.Local;
	p.x -= (1.0f - EaseOutBack(appear)) * b.x * 0.35f;

	float2 sp = p;
	sp.x += sp.y * skew;

	const float2 fb = b * float2(0.98f, 0.96f);

	//------------------------------------------
	// 枠線
	//------------------------------------------
	float frame = Cover(sdSegment(sp, float2(-fb.x, -fb.y), float2(fb.x * 0.74f, -fb.y))
					  - HUD_LINE_THIN * 0.6f);

	frame = max(frame, Cover(sdSegment(sp, float2(-fb.x, fb.y), float2(fb.x * 0.86f, fb.y))
					  - HUD_LINE_THIN * 0.6f));

	frame = max(frame, Cover(sdSegment(sp, float2(-fb.x, -fb.y), float2(-fb.x, fb.y))
					  - HUD_LINE_THIN * 0.6f));

	// 右上と右下の跳ね。閉じずに開けておく。
	frame = max(frame, Cover(sdSegment(sp,
		float2(fb.x * 0.74f, -fb.y), float2(fb.x * 0.91f, -fb.y + b.y * 0.22f))
		- HUD_LINE_THIN * 0.6f));

	frame = max(frame, Cover(sdSegment(sp,
		float2(fb.x * 0.86f, fb.y), float2(fb.x * 1.00f, fb.y - b.y * 0.22f))
		- HUD_LINE_THIN * 0.6f));

	//------------------------------------------
	// 左端の帯
	//------------------------------------------
	// 区画の始まりを面で示す。中を光がゆっくり昇る。
	float accent;
	{
		accent = Cover(sdBox(sp - float2(-fb.x + b.y * 0.07f, 0.0f),
							 float2(b.y * 0.07f, fb.y * 0.90f)));

		const float ph = frac(g_time * 0.22f);
		accent *= 0.55f + exp(-abs(sp.y - lerp(fb.y, -fb.y, ph)) * 0.05f) * 0.75f;
	}

	const float tick = Cover(sdCornerTick(sp, fb, b.y * 0.16f, HUD_LINE_THIN));

	//------------------------------------------
	// 合成
	//------------------------------------------
	float3 col = In.Color.rgb;

	float a = frame * 0.42f;
	a = max(a, accent * 0.55f);
	a = max(a, tick * 0.55f);

	// 中を薄く塗る。背景が透けたままだと区画に見えない。
	a = max(a, Cover(sdRoundBox(sp, fb, b.y * 0.10f)) * In.SubColor.a);

	return float4(col, a * In.Color.a * smoothstep(0.0f, 0.30f, appear));
}

// 見出しの語。ボタンの枠を持たない文字だけの表示。
// Params : 語の1〜4文字目
// Extra  : x,y=語の5〜6文字目 z=出現(0→1)
// ★Tilt は使わない。共有の頂点シェーダがクアッドを回すため。
float4 DrawLabel(VSOutput In)
{
	const float2 b = In.HalfSize;

	int codes[8];
	int count;
	UiDecodeWord(In.Params, In.Extra, codes, count);

	const float appear = saturate(In.Extra.z);

	// 下から浮き上がって、行き過ぎてから止まる
	float2 p = In.Local;
	p.y -= (1.0f - EaseOutBack(appear)) * b.y * 0.55f;

	const float d = sdWordCentered(p, codes, count, b.y * 0.62f);

	const float body = Cover(d);
	const float2 edge = OutlineDual(d, HUD_EDGE_DARK * 1.2f, HUD_EDGE_HALO * 1.3f);

	// 文字の中を光が横切る。止まった画面に動きを残す。
	float sweep;
	{
		const float ph = frac(g_time * 0.30f);
		sweep = body * exp(-abs(p.x - lerp(-b.x, b.x, ph)) * 0.06f)
			  * smoothstep(1.0f, 0.75f, ph);
	}

	float3 col = In.Color.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), max(edge.y * 0.5f, sweep));

	float a = edge.x * 0.85f;
	a = max(a, edge.y * 0.30f);
	a = max(a, body);
	a = max(a, sweep * 0.7f);

	return float4(col, a * In.Color.a * smoothstep(0.0f, 0.35f, appear));
}

// 操作ヒント。アイコンと短い語を並べる。
// 枠は持たない。画面の端に添えるものなので、囲むと主張が強すぎる。
//
// Params : 語の1〜4文字目
// Extra  : x,y=語の5〜6文字目 z=アイコン w=出現(0→1)
float4 DrawHint(VSOutput In)
{
	const float2 b = In.HalfSize;

	int codes[8];
	int count;
	UiDecodeWord(In.Params, In.Extra, codes, count);

	int iconShape;
	int iconCode;
	UiDecodeIcon(In.Extra.z, iconShape, iconCode);

	const float appear = saturate(In.Extra.w);

	// 下から静かに現れる。ボタンほど跳ねさせない。
	float2 p = In.Local;
	p.y -= (1.0f - appear) * b.y * 0.5f;

	//------------------------------------------
	// アイコン(左)
	//------------------------------------------
	const float s = b.y * 0.62f;
	const float iconX = -b.x + s * 1.7f;
	
	float iconD = 1e9f;
	if (iconShape != UI_ICON_NONE)
		iconD = sdBtnIcon(p - float2(iconX, 0.0f), iconShape, iconCode, s);

	//------------------------------------------
	// 語(右)
	//------------------------------------------
	// 左詰め。アイコンの右に続けて置く。
	const float h = b.y * 0.50f;
	const float w = h * 0.56f;
	const float space = w * 0.60f;

	const float textD = sdWordAt(p, codes, count,
								 iconX + s * 1.4f, w, h, h * 0.20f, space);

	//------------------------------------------
	// 合成
	//------------------------------------------
	const float iconFill = Cover(iconD);
	const float text = Cover(textD);

	const float2 edge = OutlineDual(min(iconD, textD),
									HUD_EDGE_DARK * 0.8f, HUD_EDGE_HALO * 0.7f);

	float3 col = In.Color.rgb;
	col = lerp(col, float3(0.02f, 0.02f, 0.03f), edge.x);
	col = lerp(col, float3(1, 1, 1), edge.y * 0.4f);

	float a = edge.x * 0.60f;
	a = max(a, edge.y * 0.20f);
	a = max(a, iconFill * 0.85f);
	a = max(a, text * 0.78f);

	return float4(col, a * In.Color.a * smoothstep(0.0f, 0.30f, appear));
}

//===================================================
// 本体
//===================================================
float4 main(VSOutput In) : SV_Target0
{
	// 種別ごとに分岐。nointerpolationで渡しているので
	// 1インスタンス内では必ず同じ経路を通る。
	float4 col = float4(0, 0, 0, 0);

	if (In.Kind <= UI_KIND_BUTTON_PLATE)
		col = DrawButton(In);
	else if (In.Kind == UI_KIND_VEIL)
		col = DrawVeil(In);
	else if (In.Kind == UI_KIND_PANEL)
		col = DrawPanel(In);
	else if (In.Kind == UI_KIND_LABEL)
		col = DrawLabel(In);
	else if (In.Kind == UI_KIND_HINT)
		col = DrawHint(In);

	// 完全に透明な部分は捨てる。クアッドの大半は空白なので効く。
	if (col.a < 0.004f)
		discard;

	return col;
}
