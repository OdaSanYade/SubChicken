#ifndef INC_KD_HUD_SDF_HLSLI
#define INC_KD_HUD_SDF_HLSLI

//===================================================
//
// HUD用 SDFライブラリ
//
//===================================================

#define HUD_PI 3.14159265f

//===================================================
// 1. 基本プリミティブ
//===================================================
float sdCircle(float2 p, float r)
{
	return length(p) - r;
}

float sdBox(float2 p, float2 b)
{
	float2 d = abs(p) - b;
	return length(max(d, 0.0f)) + min(max(d.x, d.y), 0.0f);
}

float sdRoundBox(float2 p, float2 b, float r)
{
	return sdBox(p, max(b - r, 0.0f)) - r;
}

float sdSegment(float2 p, float2 a, float2 b)
{
	float2 pa = p - a;
	float2 ba = b - a;
	float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6f));
	return length(pa - ba * h);
}

float sdTriangle(float2 p, float2 p0, float2 p1, float2 p2)
{
	float2 e0 = p1 - p0, v0 = p - p0;
	float2 e1 = p2 - p1, v1 = p - p1;
	float2 e2 = p0 - p2, v2 = p - p2;

	float2 pq0 = v0 - e0 * saturate(dot(v0, e0) / dot(e0, e0));
	float2 pq1 = v1 - e1 * saturate(dot(v1, e1) / dot(e1, e1));
	float2 pq2 = v2 - e2 * saturate(dot(v2, e2) / dot(e2, e2));

	float s = sign(e0.x * e2.y - e0.y * e2.x);
	float2 d = min(min(
		float2(dot(pq0, pq0), s * (v0.x * e0.y - v0.y * e0.x)),
		float2(dot(pq1, pq1), s * (v1.x * e1.y - v1.y * e1.x))),
		float2(dot(pq2, pq2), s * (v2.x * e2.y - v2.y * e2.x)));

	return -sqrt(d.x) * sign(d.y);
}

// 楕円の近似SDF。真の距離ではないが、輪郭の描画には十分。
float sdEllipse(float2 p, float2 r)
{
	float k = min(r.x, r.y);
	return (length(p / r) - 1.0f) * k;
}

//===================================================
// 2. 補助
//===================================================
float2 Rotate(float2 p, float a)
{
	float s = sin(a);
	float c = cos(a);
	return float2(p.x * c - p.y * s, p.x * s + p.y * c);
}

// 距離 → 被覆率。画面の微分で1px幅にならすので、
// 拡大縮小しても線の見え方が変わらない。
float Cover(float d)
{
	float w = max(fwidth(d), 1e-5f);
	return 1.0f - smoothstep(-w, w, d);
}

// 二重の縁取り。
//  x : 内側の暗い層。明るい背景で輪郭を保つ。
//  y : 外側の明るいハロー。暗い背景で輪郭を保つ。
// どちらの背景でも消えないよう、必ず対で使う。
float2 OutlineDual(float _d, float _dark, float _halo)
{
	const float body = Cover(_d);
	const float inner = Cover(_d - _dark);
	const float outer = Cover(_d - _dark - _halo);
	return float2(inner - body, outer - inner);
}

float Stroke(float _d, float _th)
{
	return Cover(abs(_d) - _th * 0.5f);
}

float Hash11(float x)
{
	return frac(sin(x * 127.1f) * 43758.5453f);
}

float Hash21(float2 p)
{
	return frac(sin(dot(p, float2(127.1f, 311.7f))) * 43758.5453f);
}

// 値ノイズ。滑らかに繋がる乱数。
// かすれや荒れた輪郭を作るのに使う。
float Noise21(float2 p)
{
	const float2 i = floor(p);
	const float2 f = frac(p);
	const float2 u = f * f * (3.0f - 2.0f * f);

	const float a = Hash21(i);
	const float b = Hash21(i + float2(1, 0));
	const float c = Hash21(i + float2(0, 1));
	const float d = Hash21(i + float2(1, 1));

	return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 0→1→0 の山。
float Bump(float t)
{
	return sin(saturate(t) * HUD_PI);
}

// 行き過ぎてから戻る。跳ねの表現に使う。
float EaseOutBack(float t)
{
	const float s = 1.70158f;
	const float u = saturate(t) - 1.0f;
	return 1.0f + u * u * ((s + 1.0f) * u + s);
}

// 減衰する振動。値が変わった瞬間の跳ねに使う。
float Elastic(float t, float freq, float decay)
{
	return sin(t * freq) * exp(-t * decay);
}

// 円弧。_from 〜 _to の角度範囲(rad, -PI〜PI)だけを残す。
// 文字のカーブを作るのに多用する。
float sdArc(float2 p, float r, float th, float from, float to)
{
	const float ring = abs(length(p) - r) - th;

	const float mid = (from + to) * 0.5f;
	const float halfSpan = abs(to - from) * 0.5f;

	// 角度差を±πへ畳む。
	// ★whileで畳むとNaNが来た時に無限ループになるので必ずfloorで一度に処理する。
	float diff = atan2(p.y, p.x) - mid;
	diff = diff - HUD_PI * 2.0f * floor(diff / (HUD_PI * 2.0f) + 0.5f);

	return max(ring, (abs(diff) - halfSpan) * max(r, 1.0f));
}

//===================================================
// 3. モチーフ
//===================================================

// 羽根。根元が細く、中ほどで最も太く、先端へ向けて鋭く絞る紡錘。
// 先端方向は +X。
float sdFeather(float2 p, float len, float width)
{
	float t = saturate(p.x / max(len, 1e-4f));
	float w = max(width * sin(t * HUD_PI) * (1.0f - t * 0.35f), 0.5f);
	float curve = sin(t * HUD_PI) * width * 0.35f;

	float2 q = float2(p.x, p.y - curve);
	float dx = abs(q.x - len * 0.5f) - len * 0.5f;
	float dy = abs(q.y) - w;

	return length(max(float2(dx, dy), 0.0f)) + min(max(dx, dy), 0.0f);
}

// 小さな羽根。装飾や粒子として使う単純化版。
float sdSmallFeather(float2 p, float len, float wid)
{
	float t = saturate((p.x + len * 0.5f) / max(len, 1e-4f));
	float w = max(wid * sin(t * HUD_PI), 0.4f);
	float2 q = float2(p.x, p.y - sin(t * HUD_PI) * wid * 0.4f);
	float dx = abs(q.x) - len * 0.5f;
	float dy = abs(q.y) - w;
	return length(max(float2(dx, dy), 0.0f)) + min(max(dx, dy), 0.0f);
}

// ニワトリの横向きシルエット。
float sdChicken(float2 p, float s)
{
	float d = sdEllipse(p - float2(-s * 0.05f, s * 0.22f), float2(s * 0.62f, s * 0.51f));

	d = min(d, sdCircle(p - float2(s * 0.42f, -s * 0.35f), s * 0.32f));
	d = min(d, sdSegment(p, float2(s * 0.15f, s * 0.05f),
						   float2(s * 0.4f, -s * 0.3f)) - s * 0.16f);

	// 鶏冠
	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		float x = s * (0.28f + (float) i * 0.15f);
		float h = (i == 1) ? s * 0.17f : s * 0.13f;
		d = min(d, sdCircle(p - float2(x, -s * 0.62f), h));
	}

	// くちばし
	d = min(d, sdTriangle(p,
		float2(s * 0.68f, -s * 0.38f),
		float2(s * 1.02f, -s * 0.28f),
		float2(s * 0.68f, -s * 0.18f)));

	// 肉垂
	d = min(d, sdCircle(p - float2(s * 0.62f, -s * 0.12f), s * 0.11f));

	// 尾
	d = min(d, sdTriangle(p,
		float2(-s * 0.5f, s * 0.05f),
		float2(-s * 1.0f, -s * 0.55f),
		float2(-s * 0.45f, -s * 0.3f)));

	return d;
}

// ---------------------------------------------------------------
// ひよこ SDF(単位空間: 幅約2、右向き。Y下向きなので頭は -Y 側)
// ---------------------------------------------------------------
static const float CHICK_SCALE = 0.85f; // 全体の大きさ調整(卵より大きく見えるなら下げる)

float ChickSmin(float a, float b, float k)
{
	const float h = saturate(0.5f + 0.5f * (b - a) / k);
	return lerp(b, a, h) - k * h * (1.0f - h);
}

// 凸三角形の符号付き距離(内側は正確、外側はやや過小評価。縁取り用途なら十分)
float sdTri3(float2 p, float2 a, float2 b, float2 c)
{
	const float2 e0 = b - a, e1 = c - b, e2 = a - c;
	const float s = ((e0.x * (c.y - a.y) - e0.y * (c.x - a.x)) > 0.0f) ? 1.0f : -1.0f;
	const float2 n0 = s * normalize(float2(e0.y, -e0.x));
	const float2 n1 = s * normalize(float2(e1.y, -e1.x));
	const float2 n2 = s * normalize(float2(e2.y, -e2.x));
	return max(dot(p - a, n0), max(dot(p - b, n1), dot(p - c, n2)));
}

// 単位空間へ変換(シルエット全体の中心を原点に合わせる)
float2 ChickSpace(float2 p, float r)
{
	float2 q = p / (r * CHICK_SCALE);
	q.y -= -0.115f;
	return q;
}

float sdChickBody(float2 q)
{
	float d = sdCircle(q - float2(0.00f, 0.20f), 0.75f); // 胴体
	d = ChickSmin(d, sdCircle(q - float2(0.18f, -0.42f), 0.50f), 0.25f); // 頭(滑らかに結合)
	d = min(d, sdTri3(q, float2(-0.55f, -0.05f), float2(-0.55f, 0.35f), float2(-0.95f, -0.10f))); // 尾
	d = min(d, sdTri3(q, float2(-0.02f, -0.86f), float2(0.26f, -0.86f), float2(0.10f, -1.18f))); // アホ毛
	return d;
}

float sdBeak(float2 q)
{
	return sdTri3(q, float2(0.60f, -0.56f), float2(0.60f, -0.28f), float2(1.00f, -0.42f));
}

// ×印(撃破時の目)
float sdXMark(float2 q, float2 c, float s, float w)
{
	const float2 a = abs(q - c);
	return max(abs(a.x - a.y) * 0.7071f - w, max(a.x, a.y) - s);
}

// 外形(胴体+くちばし)。距離はローカル単位(px相当)に戻して返す
float sdChick(float2 p, float r)
{
	const float2 q = ChickSpace(p, r);
	return min(sdChickBody(q), sdBeak(q)) * r * CHICK_SCALE;
}

// 卵。
//
// 単純に上を細めた円だと、丸に近くて卵に見えない。
// 縦に伸ばしたうえで、上半分をさらに絞る。
//
//  縦横比は 1 : 1.35 前後。これより細いと繭に、太いと球に見える。
float sdEgg(float2 p, float r)
{
	// まず縦長にする
	float2 q = float2(p.x, p.y / 1.35f);

	// 上ほど細くする。下は丸いまま。
	// 係数を上げすぎると涙滴になるので、0.28 前後に留める。
	const float taper = 1.0f - saturate(-q.y / r) * 0.28f;
	q.x /= max(taper, 0.35f);

	return (length(q) - r) * 0.8f;
}

// 卵の割れ目。
float sdCrack(float2 p, float r, float seed)
{
	float d = 1e9f;

	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		float a = seed + (float) i * 2.1f;
		float2 s = float2(cos(a), sin(a)) * r * 0.15f;
		float2 e = float2(cos(a + 0.9f), sin(a + 0.9f)) * r * 0.95f;
		d = min(d, sdSegment(p, s, e));
	}
	return d - 0.9f;
}

// 時計。経過時間の意味を記号で伝える。
// 文字で書くより、形の方が言語に依存せず一瞬で読める。
//  _mm : 長針の角度(rad)  _ss : 短針の角度(rad)
float sdClockIcon(float2 p, float s, float _mm, float _ss)
{
	// 外周
	float d = abs(sdCircle(p, s)) - s * 0.13f;

	// 12時・3時・6時・9時の目盛り
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		const float a = HUD_PI * 0.5f * (float) i;
		const float2 dir = float2(cos(a), sin(a));
		d = min(d, sdSegment(p, dir * s * 0.62f, dir * s * 0.82f) - s * 0.075f);
	}

	// 針。分針は長く、秒針は短く。
	d = min(d, sdSegment(p, float2(0, 0),
		float2(sin(_mm), -cos(_mm)) * s * 0.55f) - s * 0.09f);
	d = min(d, sdSegment(p, float2(0, 0),
		float2(sin(_ss), -cos(_ss)) * s * 0.38f) - s * 0.07f);

	// 軸
	d = min(d, sdCircle(p, s * 0.10f));

	return d;
}

// 撃破の印。倒した数の意味を記号で伝える。
// 卵だと「残っている数」に見えるので、討伐の印を別に用意する。
// 交差した2本の線(バツ)と、それを囲む欠けた輪。
//  _punch : 0で静止、1で弾ける
float sdKillMark(float2 p, float s, float _punch)
{
	const float th = s * 0.15f;

	// 中央のバツ。討ち取った印。
	float d = sdSegment(p, float2(-s * 0.42f, -s * 0.42f),
						   float2(s * 0.42f, s * 0.42f)) - th;
	d = min(d, sdSegment(p, float2(s * 0.42f, -s * 0.42f),
						   float2(-s * 0.42f, s * 0.42f)) - th);

	// 囲む輪。上下を欠けさせて的に見せる。
	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		const float a = HUD_PI * 0.5f * (float) i + HUD_PI * 0.25f;
		d = min(d, sdArc(p, s * (1.0f + _punch * 0.35f), th * 0.55f,
						 a - 0.42f, a + 0.42f));
	}
	return d;
}

// 割れかけの卵。撃破数の意味を記号で伝える。
// 残数表示の卵と同じ意匠にして、対応が読み取れるようにする。
//  _crack : 0で無傷、1で割れ目が入る
float sdEggIcon(float2 p, float s, float _crack)
{
	float d = sdEgg(p, s);

	if (_crack > 0.01f)
	{
		// 中央を横切るジグザグの割れ目
		float2 q = p;
		q.y += sin(q.x / max(s, 1e-3f) * 6.0f) * s * 0.12f;
		d = max(d, -(abs(q.y) - s * 0.09f * _crack));
	}
	return d;
}

// 足跡。装飾用の小さな三本指。
float sdFootPrint(float2 p, float s)
{
	float d = 1e9f;
	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		float a = -HUD_PI * 0.5f + ((float) i - 1.0f) * 0.55f;
		d = min(d, sdSegment(p, float2(0, 0), float2(cos(a), sin(a)) * s) - s * 0.16f);
	}
	d = min(d, sdSegment(p, float2(0, 0), float2(0, s * 0.5f)) - s * 0.14f);
	return d;
}

// 右端が羽根状に跳ね上がる帯。HP用。
float sdSweptBar(float2 p, float2 b, float sweep)
{
	float d = sdBox(p, b);

	float2 q = p - float2(b.x, -b.y * 0.3f);
	float tip = sdTriangle(q,
		float2(0.0f, -b.y * 0.7f),
		float2(b.y * sweep, -b.y * 2.2f),
		float2(0.0f, b.y * 1.3f));

	return min(d, tip);
}

// 菱形の鋲(リベット)。枠の要所に打つ装飾。
float sdRivet(float2 p, float s)
{
	return sdBox(Rotate(p, HUD_PI * 0.25f), float2(s, s));
}

// Enterの矢印。左下へ折れて戻る線。
// キーボードでの決定を、文字ではなく形で示す。
float sdEnterMark(float2 p, float s, float th)
{
	// 右上から下へ
	float d = sdSegment(p, float2(s * 0.55f, -s * 0.55f),
						   float2(s * 0.55f, s * 0.15f)) - th;

	// 左へ
	d = min(d, sdSegment(p, float2(s * 0.55f, s * 0.15f),
						   float2(-s * 0.25f, s * 0.15f)) - th);

	// 矢尻
	d = min(d, sdTriangle(p - float2(-s * 0.45f, s * 0.15f),
		float2(0.0f, 0.0f),
		float2(s * 0.36f, -s * 0.34f),
		float2(s * 0.36f, s * 0.34f)));

	return d;
}

// マウス本体。
//
// 「LMB」の3文字だけでは、キーボードのキーと区別が付かない。
// 形そのものを描けば、どの装置のどこを押すのかが一目で読める。
//
//  _b : 半サイズ。上(-Y)が指を置く面。
float sdMouseBody(float2 p, float2 _b)
{
	// 上ほど細める。手のひらに沿う輪郭になり、ただの角丸矩形と区別が付く。
	// 絞りすぎると洋梨に見えるので 0.16 前後に留める。
	const float t = saturate((_b.y - p.y) / max(_b.y * 2.0f, 1e-4f)); // 0=下 1=上
	const float2 q = float2(p.x / max(1.0f - t * t * 0.16f, 0.4f), p.y);

	return sdRoundBox(q, _b, _b.x * 0.62f);
}

// Shiftの上向き矢印。
//
// 語だけだと他のキーと同じ字面に埋もれる。
// 記号を添えると、読む前に「このキーだ」と分かる。
float sdShiftMark(float2 p, float s)
{
	// 矢尻
	float d = sdTriangle(p,
		float2(0.0f, -s),
		float2(-s * 0.92f, -s * 0.05f),
		float2(s * 0.92f, -s * 0.05f));

	// 軸
	d = min(d, sdRoundBox(p - float2(0.0f, s * 0.42f),
						  float2(s * 0.34f, s * 0.47f), s * 0.08f));

	// 台座。押し上げる意味が出る。
	d = min(d, sdBox(p - float2(0.0f, s * 1.02f), float2(s * 0.62f, s * 0.10f)));

	return d;
}

// 四隅の鉤(かぎ)。矩形を囲まずに範囲を示す。
// 全周を線で囲むと窮屈になるので、角だけを示して抜けを作る。
//  _b   : 範囲の半サイズ
//  _len : 角から伸びる腕の長さ
//  _th  : 線の太さ
float sdCornerTick(float2 p, float2 _b, float _len, float _th)
{
	float d = 1e9f;

	[unroll]
	for (int i = 0; i < 4; ++i)
	{
		const float2 s = float2((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f);
		const float2 c = _b * s;

		d = min(d, sdSegment(p, c, c - float2(s.x * _len, 0.0f)));
		d = min(d, sdSegment(p, c, c - float2(0.0f, s.y * _len)));
	}
	return d - _th * 0.5f;
}

//===================================================
// 操作ガイドのアイコン
//===================================================
// 「何が起きるか」を一目で読ませるための図。
// 攻撃はニワトリらしく「くちばしで突く」形に統一し、
// 弱は火花、強は衝撃波で強弱を表す。
//
// 常時のアニメーションは付けない。戦闘中に視界の端で
// 動き続けるものがあると、そちらへ注意が引かれてしまう。
// 動くのは「押した瞬間」と「肺活量が最大の時」だけ。

// 突くくちばし。+X方向へ向く。
// _open : くちばしの開き(0で閉じ、1で全開)
float sdBeakStrike(float2 p, float s, float _open, float _bold)
{
	const float w = s * 0.62f * _bold; // 根元の太さ
	const float len = s * 1.05f; // 長さ
	const float gap = _open * s * 0.30f;

	// 上のくちばし
	float d = sdTriangle(p,
		float2(len, -gap * 0.35f),
		float2(-s * 0.30f, -w * 0.5f - gap * 0.5f),
		float2(-s * 0.30f, gap * 0.1f));

	// 下のくちばし。上より短くする。
	d = min(d, sdTriangle(p,
		float2(len * 0.86f, gap * 0.45f),
		float2(-s * 0.30f, w * 0.5f + gap * 0.5f),
		float2(-s * 0.30f, -gap * 0.1f)));

	// 頭側の丸み。くちばしだけだと何か分からない。
	d = min(d, sdCircle(p - float2(-s * 0.40f, 0.0f), w * 0.55f));

	// 鶏冠。小さくてもニワトリだと分かる決め手。
	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		const float x = -s * (0.52f - (float) i * 0.13f);
		const float h = (i == 1) ? w * 0.26f : w * 0.20f;
		d = min(d, sdCircle(p - float2(x, -w * 0.55f), h));
	}
	return d;
}

// 弱攻撃 : 小さく突いて、小さな火花が散る。
// 軽さは「小さい・細い・散るものが小さい」で表す。
float sdIconLight(float2 p, float s)
{
	// 本体は小さめ。強攻撃との差を大きさで付ける。
	float d = sdBeakStrike(p - float2(-s * 0.18f, 0.0f), s * 0.70f, 0.35f, 0.85f);

	// 先端の火花。短い線を放射状に3本。
	const float2 tip = float2(s * 0.58f, 0.0f);

	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		const float a = -0.62f + (float) i * 0.62f;
		const float2 dir = float2(cos(a), sin(a));

		d = min(d, sdSegment(p - tip, dir * s * 0.14f, dir * s * 0.34f) - s * 0.035f);
	}

	// さらに小さな粒。線だけだと寂しい。
	[unroll]
	for (int k = 0; k < 2; ++k)
	{
		const float a = -0.35f + (float) k * 0.70f;
		d = min(d, sdCircle(p - tip - float2(cos(a), sin(a)) * s * 0.46f, s * 0.045f));
	}
	return d;
}

// 強攻撃 : 大きく突いて、衝撃波が広がる。
// 重さは「大きい・太い・広がるものが大きい」で表す。
float sdIconHeavy(float2 p, float s)
{
	// 本体は大きく太い
	float d = sdBeakStrike(p - float2(-s * 0.34f, 0.0f), s * 0.92f, 0.55f, 1.35f);

	// 衝撃波。前方へ大きく広がる2枚の弧。
	const float2 tip = float2(s * 0.62f, 0.0f);

	[unroll]
	for (int i = 0; i < 2; ++i)
	{
		const float r = s * (0.34f + (float) i * 0.30f);
		const float th = s * (0.075f - (float) i * 0.022f);
		d = min(d, sdArc(p - tip, r, th, -1.05f, 1.05f));
	}

	// 割れ目のような放射線。衝撃の鋭さを出す。
	[unroll]
	for (int k = 0; k < 3; ++k)
	{
		const float a = -0.85f + (float) k * 0.85f;
		const float2 dir = float2(cos(a), sin(a));
		d = min(d, sdSegment(p - tip, dir * s * 0.70f, dir * s * 1.02f) - s * 0.05f);
	}
	return d;
}

// 回避 : 丸まって転がるニワトリと、後ろに残る軌跡。
// 矢印だけでは何が回っているのか読めないので、転がる本体を描く。
//  _roll : 転がりの位相。静止表示では固定値を渡す。
float sdIconDodge(float2 p, float s, float _roll)
{
	const float t = sin(_roll * HUD_PI * 2.0f);
	const float2 c = float2(t * s * 0.30f, 0.0f);

	// 丸まった体
	float d = sdCircle(p - c, s * 0.42f);

	// 回転する羽根の束。転がっていることが分かる。
	const float spin = _roll * HUD_PI * 4.0f;

	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		const float a = spin + (float) i * (HUD_PI * 2.0f / 3.0f);
		const float2 o = float2(cos(a), sin(a)) * s * 0.27f;
		d = min(d, sdEllipse(Rotate(p - c - o, a), float2(s * 0.05f, s * 0.17f)));
	}

	// 後ろに残る軌跡
	const float dir = (t >= 0.0f) ? -1.0f : 1.0f;

	[unroll]
	for (int k = 0; k < 3; ++k)
	{
		const float off = (float) (k + 1) * s * 0.26f;
		const float fade = 1.0f - (float) k * 0.28f;
		const float2 o = c + float2(dir * off, 0.0f);

		d = min(d, sdArc(p - o, s * 0.42f, s * 0.045f * fade,
						 (dir > 0.0f) ? -1.1f : (HUD_PI - 1.1f),
						 (dir > 0.0f) ? 1.1f : (HUD_PI + 1.1f)));
	}

	// 地面の線。転がる場が分かる。
	d = min(d, sdSegment(p, float2(-s * 0.88f, s * 0.66f),
						   float2(s * 0.88f, s * 0.66f)) - s * 0.035f);
	return d;
}

// 鳴き声 : ニワトリの横顔と、前方へ広がる音。
//  _open : くちばしの開き
//  _wave : 音の広がり。0で静止表示。
float sdIconCry(float2 p, float s, float _open, float _wave)
{
	// 本体。HPアイコンと同じシルエットを使って統一する。
	float2 q = p - float2(-s * 0.22f, s * 0.10f);
	q = Rotate(q, -_open * 0.14f);

	float d = sdChicken(q, s * 0.62f);

	// 開いたくちばし
	const float2 beak = float2(s * 0.20f, -s * 0.14f);
	d = min(d, sdTriangle(p - beak,
		float2(s * 0.30f, -_open * s * 0.10f),
		float2(-s * 0.02f, -s * 0.10f - _open * s * 0.12f),
		float2(-s * 0.02f, s * 0.02f)));

	// 前方へ広がる音の弧
	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		const float ph = frac(_wave + (float) i * 0.333f);
		const float r = s * (0.42f + ph * 0.62f);
		const float th = s * 0.055f * (1.0f - ph);

		if (th <= 0.0f)
			continue;

		d = min(d, sdArc(p - beak, r, th, -0.62f, 0.62f));
	}
	return d;
}

#endif // INC_KD_HUD_SDF_HLSLI
