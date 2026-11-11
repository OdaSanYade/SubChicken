#ifndef INC_KD_HUD_FONT_HLSLI
#define INC_KD_HUD_FONT_HLSLI

#include "inc_KdHudSdf.hlsli"

//===================================================
//
// HUD用 文字SDF
//
// 汎用のフォントテクスチャは持たず、必要な文字だけを線で組む。
// テクスチャが要らないので解像度に依存せず、
// 拡大しても線幅が一定に保たれる。
//
//  1. 数字(7セグメント)
//  2. 英字(A〜Z)
//  3. ラベル(最大3文字)
//  4. 単語(COMBO)
//
// 文字を足したい時はこのファイルだけを見ればよい。
//
//===================================================


//===================================================
// 1. 数字
//===================================================
float sdRingOct(float2 p, float w, float h, float c)
{
	float d = sdSegment(p, float2(-w + c, -h), float2(w - c, -h));
	d = min(d, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
	d = min(d, sdSegment(p, float2(w, -h + c), float2(w, h - c)));
	d = min(d, sdSegment(p, float2(w, h - c), float2(w - c, h)));
	d = min(d, sdSegment(p, float2(w - c, h), float2(-w + c, h)));
	d = min(d, sdSegment(p, float2(-w + c, h), float2(-w, h - c)));
	d = min(d, sdSegment(p, float2(-w, h - c), float2(-w, -h + c)));
	d = min(d, sdSegment(p, float2(-w, -h + c), float2(-w + c, -h)));
	return d;
}

float sdDigit(float2 p, int d, float w, float h, float th)
{
	// 角の落とし量。小さいほど角張る。
	// 丸いと事務的な字面になるので、角は最小限だけ落とす。
	const float c = min(w, h) * 0.10f;

	const float2 tl = float2(-w, -h), tr = float2(w, -h);
	const float2 bl = float2(-w, h), br = float2(w, h);
	const float2 ml = float2(-w, 0), mr = float2(w, 0);

	float dist = 1e9f;
	const int n = clamp(d, 0, 9);

	if (n == 0)
	{
		// 上下を尖らせた六角形。O との区別も付きやすい。
		dist = sdSegment(p, float2(-w, -h + c * 2.0f), float2(0, -h));
		dist = min(dist, sdSegment(p, float2(0, -h), float2(w, -h + c * 2.0f)));
		dist = min(dist, sdSegment(p, float2(w, -h + c * 2.0f), float2(w, h - c * 2.0f)));
		dist = min(dist, sdSegment(p, float2(w, h - c * 2.0f), float2(0, h)));
		dist = min(dist, sdSegment(p, float2(0, h), float2(-w, h - c * 2.0f)));
		dist = min(dist, sdSegment(p, float2(-w, h - c * 2.0f), float2(-w, -h + c * 2.0f)));
	}
	else if (n == 1)
	{
		// 縦棒 + 左上の旗 + 台。棒だけだと他の字と紛れる。
		// 旗は水平に折って角を立てる。
		dist = sdSegment(p, float2(0, -h), float2(0, h));
		dist = min(dist, sdSegment(p, float2(-w * 0.7f, -h + h * 0.30f), float2(-w * 0.7f, -h)));
		dist = min(dist, sdSegment(p, float2(-w * 0.7f, -h), float2(0, -h)));
		dist = min(dist, sdSegment(p, float2(-w * 0.7f, h), float2(w * 0.7f, h)));
	}
	else if (n == 2)
	{
		// 上辺 → 右の縦 → 斜め → 下辺。角を立てて機械的な字面にする。
		dist = sdSegment(p, float2(-w + c, -h), float2(w - c, -h));
		dist = min(dist, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		dist = min(dist, sdSegment(p, float2(w, -h + c), float2(w, -h * 0.05f)));
		dist = min(dist, sdSegment(p, float2(w, -h * 0.05f), float2(-w + c * 0.3f, h - c)));
		dist = min(dist, sdSegment(p, float2(-w + c * 0.3f, h - c), float2(-w, h)));
		dist = min(dist, sdSegment(p, bl, br));
	}
	else if (n == 3)
	{
		dist = sdSegment(p, float2(-w + c, -h), float2(w - c, -h));
		dist = min(dist, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		dist = min(dist, sdSegment(p, float2(w, -h + c), float2(w, -c * 0.3f)));
		dist = min(dist, sdSegment(p, float2(w, -c * 0.3f), float2(w - c * 0.6f, 0)));
		dist = min(dist, sdSegment(p, float2(w - c * 0.6f, 0), float2(-w * 0.45f, 0)));
		dist = min(dist, sdSegment(p, float2(w - c * 0.6f, 0), float2(w, c * 0.3f)));
		dist = min(dist, sdSegment(p, float2(w, c * 0.3f), float2(w, h - c)));
		dist = min(dist, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		dist = min(dist, sdSegment(p, float2(w - c, h), float2(-w + c, h)));
	}
	else if (n == 4)
	{
		dist = sdSegment(p, float2(w * 0.35f, -h), float2(-w, h * 0.18f));
		dist = min(dist, sdSegment(p, float2(-w, h * 0.18f), float2(w, h * 0.18f)));
		dist = min(dist, sdSegment(p, float2(w * 0.35f, -h), float2(w * 0.35f, h)));
	}
	else if (n == 5)
	{
		dist = sdSegment(p, tr, tl);
		dist = min(dist, sdSegment(p, tl, float2(-w, -c * 0.1f)));
		dist = min(dist, sdSegment(p, float2(-w, -c * 0.1f), float2(w - c, -c * 0.1f)));
		dist = min(dist, sdSegment(p, float2(w - c, -c * 0.1f), float2(w, -c * 0.1f + c)));
		dist = min(dist, sdSegment(p, float2(w, -c * 0.1f + c), float2(w, h - c)));
		dist = min(dist, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		dist = min(dist, sdSegment(p, float2(w - c, h), float2(-w + c, h)));
	}
	else if (n == 6)
	{
		dist = sdSegment(p, float2(w - c * 0.5f, -h), float2(-w + c, -h));
		dist = min(dist, sdSegment(p, float2(-w + c, -h), float2(-w, -h + c)));
		dist = min(dist, sdSegment(p, float2(-w, -h + c), float2(-w, h - c)));
		dist = min(dist, sdSegment(p, float2(-w, h - c), float2(-w + c, h)));
		dist = min(dist, sdSegment(p, float2(-w + c, h), float2(w - c, h)));
		dist = min(dist, sdSegment(p, float2(w - c, h), float2(w, h - c)));
		dist = min(dist, sdSegment(p, float2(w, h - c), float2(w, c * 0.4f)));
		dist = min(dist, sdSegment(p, float2(w, c * 0.4f), float2(w - c, 0)));
		dist = min(dist, sdSegment(p, float2(w - c, 0), float2(-w, 0)));
	}
	else if (n == 7)
	{
		dist = sdSegment(p, tl, tr);
		dist = min(dist, sdSegment(p, tr, float2(-w * 0.25f, h)));
		// 横棒を1本入れて 1 との差を作る
		dist = min(dist, sdSegment(p, float2(-w * 0.5f, 0), float2(w * 0.45f, 0)));
	}
	else if (n == 8)
	{
		dist = sdRingOct(p, w, h, c);
		dist = min(dist, sdSegment(p, float2(-w + c * 0.4f, 0), float2(w - c * 0.4f, 0)));
	}
	else
	{
		// 9
		dist = sdSegment(p, float2(w - c, -h), float2(-w + c, -h));
		dist = min(dist, sdSegment(p, float2(-w + c, -h), float2(-w, -h + c)));
		dist = min(dist, sdSegment(p, float2(-w, -h + c), float2(-w, -c * 0.4f)));
		dist = min(dist, sdSegment(p, float2(-w, -c * 0.4f), float2(-w + c, 0)));
		dist = min(dist, sdSegment(p, float2(-w + c, 0), float2(w, 0)));
		dist = min(dist, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		dist = min(dist, sdSegment(p, float2(w, -h + c), float2(w, h - c)));
		dist = min(dist, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		dist = min(dist, sdSegment(p, float2(w - c, h), float2(-w + c, h)));
	}

	return dist - th;
}

// 整数を右詰めで描く
float sdNumber(float2 p, int value, float digitW, float digitH, float th, float space)
{
	value = clamp(value, 0, 999);

	const int digits = (value >= 100) ? 3 : (value >= 10) ? 2 : 1;
	const float pitch = digitW * 2.0f + space;

	float d = 1e9f;

	[unroll]
	for (int i = 0; i < 3; ++i)
	{
		if (i < digits)
		{
			int div = (i == 0) ? 1 : (i == 1) ? 10 : 100;
			int digit = (value / div) % 10;
			d = min(d, sdDigit(p - float2(-(float) i * pitch, 0), digit, digitW, digitH, th));
		}
	}
	return d;
}

// パーセント記号
float sdPercent(float2 p, float w, float h, float th)
{
	const float r = min(w, h) * 0.36f;

	// 斜線
	float d = sdSegment(p, float2(-w * 0.60f, h), float2(w * 0.60f, -h)) - th;

	// 左上と右下の四角。丸より角張って数字と揃う。
	d = min(d, abs(sdBox(p - float2(-w * 0.50f, -h + r), float2(r, r))) - th * 0.85f);
	d = min(d, abs(sdBox(p - float2(w * 0.50f, h - r), float2(r, r))) - th * 0.85f);

	return d;
}

//---------------------------------------------------
// 詰め組み
//---------------------------------------------------
float NumberSpan(int value, float w, float space)
{
	value = clamp(value, 0, 999);
	const int digits = (value >= 100) ? 3 : (value >= 10) ? 2 : 1;
	return (float) digits * w * 2.0f + (float) (digits - 1) * space;
}

// 左端 _left から右へ詰めて整数を描く。
float sdNumberAt(float2 p, int value, float _left, float w, float h, float th, float space)
{
	value = clamp(value, 0, 999);

	// sdNumber は一の位を基準に左へ伸びるので、一の位の中心を求める。
	const float span = NumberSpan(value, w, space);
	const float onesCx = _left + span - w;

	return sdNumber(p - float2(onesCx, 0.0f), value, w, h, th, space);
}

// 「現在 / 最大」を1つの塊として詰めて描く。
//  _h     : 字の半高
//  _align : -1=左詰め 0=中央 1=右詰め(基準は p=0)
float sdRatioText(float2 p, int _now, int _max, float _h, float _align)
{
	const float w = _h * 0.56f; // 字の半幅
	const float th = _h * 0.20f; // 線の太さ
	const float space = w * 0.55f; // 字間

	const float sw = _h * 0.34f; // 斜線の半幅
	const float gap = w * 0.70f; // 数字と斜線の間

	const float nowSpan = NumberSpan(_now, w, space);
	const float maxSpan = NumberSpan(_max, w, space);
	const float total = nowSpan + gap + sw * 2.0f + gap + maxSpan;

	// 揃え方から左端を決める
	float left;
	if (_align < -0.5f)
		left = 0.0f;
	else if (_align > 0.5f)
		left = -total;
	else
		left = -total * 0.5f;

	float d = sdNumberAt(p, _now, left, w, _h, th, space);

	const float slashCx = left + nowSpan + gap + sw;
	d = min(d, sdSegment(p,
		float2(slashCx - sw * 0.55f, _h * 0.92f),
		float2(slashCx + sw * 0.55f, -_h * 0.92f)) - th * 0.55f);

	d = min(d, sdNumberAt(p, _max, slashCx + sw + gap, w, _h, th, space));

	return d;
}

// 「値 + %」を1つの塊として詰めて描く。
float sdPercentText(float2 p, int _value, float _h, float _align)
{
	const float w = _h * 0.56f;
	const float th = _h * 0.20f;
	const float space = w * 0.55f;

	const float pw = _h * 0.40f; // 記号の半幅
	const float gap = w * 0.60f;

	const float numSpan = NumberSpan(_value, w, space);
	const float total = numSpan + gap + pw * 2.0f;

	float left;
	if (_align < -0.5f)
		left = 0.0f;
	else if (_align > 0.5f)
		left = -total;
	else
		left = -total * 0.5f;

	float d = sdNumberAt(p, _value, left, w, _h, th, space);

	d = min(d, sdPercent(p - float2(left + numSpan + gap + pw, 0.0f),
						 pw, _h * 0.88f, th * 0.85f));

	return d;
}

// 指定した矩形に収まる大きさで整数を描く
//  _align  : -1=左詰め 0=中央 1=右詰め
float sdNumberFit(float2 p, int value, float2 _half,
	float _thR, float _spaceR, float _align)
{
	value = clamp(value, 0, 999);
	const int digits = (value >= 100) ? 3 : (value >= 10) ? 2 : 1;
	const float dn = (float) digits;

	// 総幅 = dn*2w + (dn-1)*space,  space = 2w*_spaceR
	const float unit = dn * 2.0f + (dn - 1.0f) * 2.0f * _spaceR;
	const float wFromWidth = (_half.x * 2.0f) / max(unit, 1e-4f);

	// 字の縦横比。細くなりすぎないよう高さからも上限をかける。
	const float wFromHeight = _half.y * 0.62f;

	const float w = min(wFromWidth, wFromHeight);
	const float h = _half.y;
	const float space = w * 2.0f * _spaceR;
	const float pitch = w * 2.0f + space;
	const float total = dn * 2.0f * w + (dn - 1.0f) * space;

	// sdNumber は一の位を基準に左へ伸びるので、揃え方から基準点を出す。
	float anchor;
	if (_align < -0.5f)
		anchor = -_half.x + total - w; // 左詰め
	else if (_align > 0.5f)
		anchor = _half.x - w; // 右詰め
	else
		anchor = total * 0.5f - w; // 中央

	return sdNumber(p - float2(anchor, 0.0f), value, w, h, h * _thR, space);
}

// 数字の総幅(基準点から左へ伸びる量)
float NumberWidth(int value, float digitW, float space)
{
	value = clamp(value, 0, 999);
	const int digits = (value >= 100) ? 3 : (value >= 10) ? 2 : 1;
	return (digitW * 2.0f + space) * (float) (digits - 1) + digitW * 2.0f;
}

//===================================================
// 2. 英字(A〜Z)
//===================================================
float sdLetter(float2 p, int code, float w, float h, float th)
{
	float d = 1e9f;

	// 基準点
	const float2 tl = float2(-w, -h), tm = float2(0, -h), tr = float2(w, -h);
	const float2 ml = float2(-w, 0), mm = float2(0, 0), mr = float2(w, 0);
	const float2 bl = float2(-w, h), bm = float2(0, h), br = float2(w, h);

	// 角を落とす量。DやJの丸みを直線で表現する。
	const float c = min(w, h) * 0.45f;

	if (code == 0)			// A
	{
		d = min(d, sdSegment(p, bl, tm));
		d = min(d, sdSegment(p, br, tm));
		d = min(d, sdSegment(p, float2(-w * 0.55f, h * 0.2f), float2(w * 0.55f, h * 0.2f)));
	}
	else if (code == 1)		// B
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tl, float2(w * 0.6f, -h)));
		d = min(d, sdSegment(p, ml, float2(w * 0.7f, 0)));
		d = min(d, sdSegment(p, bl, float2(w * 0.6f, h)));
		d = min(d, sdSegment(p, float2(w * 0.6f, -h), float2(w, -h + c)));
		d = min(d, sdSegment(p, float2(w, -h + c), float2(w, -c * 0.4f)));
		d = min(d, sdSegment(p, float2(w, -c * 0.4f), float2(w * 0.7f, 0)));
		d = min(d, sdSegment(p, float2(w * 0.7f, 0), float2(w, c * 0.4f)));
		d = min(d, sdSegment(p, float2(w, c * 0.4f), float2(w, h - c)));
		d = min(d, sdSegment(p, float2(w, h - c), float2(w * 0.6f, h)));
	}
	else if (code == 2)		// C
	{
		d = min(d, sdSegment(p, float2(w, -h), float2(-w + c, -h)));
		d = min(d, sdSegment(p, float2(-w + c, -h), float2(-w, -h + c)));
		d = min(d, sdSegment(p, float2(-w, -h + c), float2(-w, h - c)));
		d = min(d, sdSegment(p, float2(-w, h - c), float2(-w + c, h)));
		d = min(d, sdSegment(p, float2(-w + c, h), float2(w, h)));
	}
	else if (code == 3)		// D
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tl, float2(w - c, -h)));
		d = min(d, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		d = min(d, sdSegment(p, float2(w, -h + c), float2(w, h - c)));
		d = min(d, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		d = min(d, sdSegment(p, float2(w - c, h), bl));
	}
	else if (code == 4)		// E
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tl, tr));
		d = min(d, sdSegment(p, ml, float2(w * 0.7f, 0)));
		d = min(d, sdSegment(p, bl, br));
	}
	else if (code == 5)		// F
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tl, tr));
		d = min(d, sdSegment(p, ml, float2(w * 0.7f, 0)));
	}
	else if (code == 6)		// G
	{
		d = min(d, sdSegment(p, float2(w, -h), float2(-w + c, -h)));
		d = min(d, sdSegment(p, float2(-w + c, -h), float2(-w, -h + c)));
		d = min(d, sdSegment(p, float2(-w, -h + c), float2(-w, h - c)));
		d = min(d, sdSegment(p, float2(-w, h - c), float2(-w + c, h)));
		d = min(d, sdSegment(p, float2(-w + c, h), float2(w, h)));
		d = min(d, sdSegment(p, float2(w, h), float2(w, h * 0.05f)));
		d = min(d, sdSegment(p, float2(w, h * 0.05f), float2(w * 0.2f, h * 0.05f)));
	}
	else if (code == 7)		// H
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tr, br));
		d = min(d, sdSegment(p, ml, mr));
	}
	else if (code == 8)		// I
	{
		d = min(d, sdSegment(p, tm, bm));
		d = min(d, sdSegment(p, float2(-w * 0.6f, -h), float2(w * 0.6f, -h)));
		d = min(d, sdSegment(p, float2(-w * 0.6f, h), float2(w * 0.6f, h)));
	}
	else if (code == 9)		// J
	{
		d = min(d, sdSegment(p, float2(-w * 0.2f, -h), float2(w, -h)));
		d = min(d, sdSegment(p, float2(w * 0.6f, -h), float2(w * 0.6f, h - c)));
		d = min(d, sdSegment(p, float2(w * 0.6f, h - c), float2(w * 0.6f - c, h)));
		d = min(d, sdSegment(p, float2(w * 0.6f - c, h), float2(-w + c, h)));
		d = min(d, sdSegment(p, float2(-w + c, h), float2(-w, h - c)));
	}
	else if (code == 10)	// K
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, ml, tr));
		d = min(d, sdSegment(p, ml, br));
	}
	else if (code == 11)	// L
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, bl, br));
	}
	else if (code == 12)	// M
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tr, br));
		d = min(d, sdSegment(p, tl, float2(0, h * 0.1f)));
		d = min(d, sdSegment(p, tr, float2(0, h * 0.1f)));
	}
	else if (code == 13)	// N
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tr, br));
		d = min(d, sdSegment(p, tl, br));
	}
	else if (code == 14)	// O
	{
		d = min(d, sdSegment(p, float2(-w + c, -h), float2(w - c, -h)));
		d = min(d, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		d = min(d, sdSegment(p, float2(w, -h + c), float2(w, h - c)));
		d = min(d, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		d = min(d, sdSegment(p, float2(w - c, h), float2(-w + c, h)));
		d = min(d, sdSegment(p, float2(-w + c, h), float2(-w, h - c)));
		d = min(d, sdSegment(p, float2(-w, h - c), float2(-w, -h + c)));
		d = min(d, sdSegment(p, float2(-w, -h + c), float2(-w + c, -h)));
	}
	else if (code == 15)	// P
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tl, float2(w - c, -h)));
		d = min(d, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		d = min(d, sdSegment(p, float2(w, -h + c), float2(w, -c * 0.2f)));
		d = min(d, sdSegment(p, float2(w, -c * 0.2f), float2(w - c, 0)));
		d = min(d, sdSegment(p, float2(w - c, 0), ml));
	}
	else if (code == 16)	// Q
	{
		d = min(d, sdSegment(p, float2(-w + c, -h), float2(w - c, -h)));
		d = min(d, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		d = min(d, sdSegment(p, float2(w, -h + c), float2(w, h - c)));
		d = min(d, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		d = min(d, sdSegment(p, float2(w - c, h), float2(-w + c, h)));
		d = min(d, sdSegment(p, float2(-w + c, h), float2(-w, h - c)));
		d = min(d, sdSegment(p, float2(-w, h - c), float2(-w, -h + c)));
		d = min(d, sdSegment(p, float2(-w, -h + c), float2(-w + c, -h)));
		d = min(d, sdSegment(p, float2(w * 0.2f, h * 0.35f), float2(w * 1.05f, h * 1.1f)));
	}
	else if (code == 17)	// R
	{
		d = min(d, sdSegment(p, tl, bl));
		d = min(d, sdSegment(p, tl, float2(w - c, -h)));
		d = min(d, sdSegment(p, float2(w - c, -h), float2(w, -h + c)));
		d = min(d, sdSegment(p, float2(w, -h + c), float2(w, -c * 0.2f)));
		d = min(d, sdSegment(p, float2(w, -c * 0.2f), float2(w - c, 0)));
		d = min(d, sdSegment(p, float2(w - c, 0), ml));
		d = min(d, sdSegment(p, float2(-w * 0.1f, 0), br));
	}
	else if (code == 18)	// S
	{
		d = min(d, sdSegment(p, float2(w, -h), float2(-w + c, -h)));
		d = min(d, sdSegment(p, float2(-w + c, -h), float2(-w, -h + c)));
		d = min(d, sdSegment(p, float2(-w, -h + c), float2(-w, -c * 0.3f)));
		d = min(d, sdSegment(p, float2(-w, -c * 0.3f), float2(-w + c, 0)));
		d = min(d, sdSegment(p, float2(-w + c, 0), float2(w - c, 0)));
		d = min(d, sdSegment(p, float2(w - c, 0), float2(w, c * 0.3f)));
		d = min(d, sdSegment(p, float2(w, c * 0.3f), float2(w, h - c)));
		d = min(d, sdSegment(p, float2(w, h - c), float2(w - c, h)));
		d = min(d, sdSegment(p, float2(w - c, h), float2(-w, h)));
	}
	else if (code == 19)	// T
	{
		d = min(d, sdSegment(p, tl, tr));
		d = min(d, sdSegment(p, tm, bm));
	}
	else if (code == 20)	// U
	{
		d = min(d, sdSegment(p, tl, float2(-w, h - c)));
		d = min(d, sdSegment(p, float2(-w, h - c), float2(-w + c, h)));
		d = min(d, sdSegment(p, float2(-w + c, h), float2(w - c, h)));
		d = min(d, sdSegment(p, float2(w - c, h), float2(w, h - c)));
		d = min(d, sdSegment(p, float2(w, h - c), tr));
	}
	else if (code == 21)	// V
	{
		d = min(d, sdSegment(p, tl, bm));
		d = min(d, sdSegment(p, tr, bm));
	}
	else if (code == 22)	// W
	{
		d = min(d, sdSegment(p, tl, float2(-w * 0.5f, h)));
		d = min(d, sdSegment(p, float2(-w * 0.5f, h), float2(0, -h * 0.15f)));
		d = min(d, sdSegment(p, float2(0, -h * 0.15f), float2(w * 0.5f, h)));
		d = min(d, sdSegment(p, float2(w * 0.5f, h), tr));
	}
	else if (code == 23)	// X
	{
		d = min(d, sdSegment(p, tl, br));
		d = min(d, sdSegment(p, tr, bl));
	}
	else if (code == 24)	// Y
	{
		d = min(d, sdSegment(p, tl, mm));
		d = min(d, sdSegment(p, tr, mm));
		d = min(d, sdSegment(p, mm, bm));
	}
	else if (code == 25)	// Z
	{
		d = min(d, sdSegment(p, tl, tr));
		d = min(d, sdSegment(p, tr, bl));
		d = min(d, sdSegment(p, bl, br));
	}
	else
	{
		return 1e9f;
	}

	return d - th;
}

//===================================================
// 3. ラベル
//===================================================
// 最大3文字の並び。code が負の要素は描かない。
// 中央揃えで、実際に入っている文字数に応じて詰める。
float sdLabel3(float2 p, int3 codes, float w, float h, float th, float space)
{
	int count = 0;
	if (codes.x >= 0)
		count++;
	if (codes.y >= 0)
		count++;
	if (codes.z >= 0)
		count++;
	if (count == 0)
		return 1e9f;

	const float pitch = w * 2.0f + space;
	const float startX = -pitch * (count - 1) * 0.5f;

	float d = 1e9f;
	int slot = 0;

	if (codes.x >= 0)
	{
		d = min(d, sdLetter(p - float2(startX + pitch * slot, 0), codes.x, w, h, th));
		slot++;
	}
	if (codes.y >= 0)
	{
		d = min(d, sdLetter(p - float2(startX + pitch * slot, 0), codes.y, w, h, th));
		slot++;
	}
	if (codes.z >= 0)
	{
		d = min(d, sdLetter(p - float2(startX + pitch * slot, 0), codes.z, w, h, th));
	}

	return d;
}


//===================================================
// 4. 単語
//===================================================

// 感嘆符。英字と同じ線の太さ・字高で組み、字面を揃える。
// 別の作り方をすると、そこだけ浮いて手作り感が出る。
float sdExclaim(float2 p, float w, float h, float th)
{
	// 棒。上が太く下が細い方が字らしくなる。
	float d = sdSegment(p, float2(0.0f, -h), float2(0.0f, h * 0.30f)) - th;

	// 点
	d = min(d, sdCircle(p - float2(0.0f, h * 0.80f), th * 1.20f));

	return d;
}

// "HIT!!" の5文字。
// 汎用フォントは持たず、この単語だけを描く割り切り。
// 基準点は文字列の左端・縦中央。
float sdHitText(float2 p, float _h)
{
	const float h = _h * 0.5f;
	const float w = _h * 0.32f;
	const float th = _h * 0.10f;
	const float pitch = _h * 0.86f;

	// H I T
	float d = sdLetter(p, 7, w, h, th);
	d = min(d, sdLetter(p - float2(pitch, 0.0f), 8, w, h, th));
	d = min(d, sdLetter(p - float2(pitch * 2.0f, 0.0f), 19, w, h, th));

	// !!
	// 文字より少し詰めて置く。単語の一部として読ませたい。
	const float bx = pitch * 2.0f + _h * 0.72f;
	d = min(d, sdExclaim(p - float2(bx, 0.0f), w, h, th));
	d = min(d, sdExclaim(p - float2(bx + _h * 0.40f, 0.0f), w, h, th));

	return d;
}

// 任意個数の文字列。codes が負の要素はそこで打ち切る。
// 左端が基準。ラベルのように中央揃えにしないのは、
// 語の長さが変わっても書き出し位置を揃えたいため。
float sdWord(float2 p, int codes[8], int count, float w, float h, float th, float space)
{
	const float pitch = w * 2.0f + space;

	float d = 1e9f;

	[unroll]
	for (int i = 0; i < 8; ++i)
	{
		if (i >= count)
			continue;
		if (codes[i] < 0)
			continue;

		d = min(d, sdLetter(p - float2(pitch * (float) i, 0.0f), codes[i], w, h, th));
	}
	return d;
}

// 連続数の格を示す語。
// 数字だけでは「今どれくらい凄いのか」が伝わらない。
// 略さずに書く。3文字に詰めると意味が読み取れず、記号と変わらなくなる。
//  rank : 0=なし 1=NICE 2=GREAT 3=SUPER 4=PERFECT
float sdRankWord(float2 p, int rank, float w, float h, float th, float space)
{
	// 0=A .. 25=Z
	int codes[8];
	int count = 0;

	[unroll]
	for (int k = 0; k < 8; ++k)
		codes[k] = -1;

	if (rank == 1)			// NICE
	{
		codes[0] = 13;
		codes[1] = 8;
		codes[2] = 2;
		codes[3] = 4;
		count = 4;
	}
	else if (rank == 2)		// GREAT
	{
		codes[0] = 6;
		codes[1] = 17;
		codes[2] = 4;
		codes[3] = 0;
		codes[4] = 19;
		count = 5;
	}
	else if (rank == 3)		// SUPER
	{
		codes[0] = 18;
		codes[1] = 20;
		codes[2] = 15;
		codes[3] = 4;
		codes[4] = 17;
		count = 5;
	}
	else if (rank == 4)		// PERFECT
	{
		codes[0] = 15;
		codes[1] = 4;
		codes[2] = 17;
		codes[3] = 5;
		codes[4] = 4;
		codes[5] = 2;
		codes[6] = 19;
		count = 7;
	}
	else
	{
		return 1e9f;
	}

	return sdWord(p, codes, count, w, h, th, space);
}

// ランク語の幅(基準点からの右端)
float RankWordWidth(int rank, float w, float space)
{
	const float pitch = w * 2.0f + space;

	int n = 0;
	if (rank == 1)
		n = 4;
	else if (rank == 2)
		n = 5;
	else if (rank == 3)
		n = 5;
	else if (rank == 4)
		n = 7;
	else
		return 0.0f;

	return pitch * (float) (n - 1) + w * 2.0f;
}

//---------------------------------------------------
// リザルト用の語
//---------------------------------------------------
// 汎用フォントは持たないので、必要な語だけを個別に用意する。
// 語を増やすときはここへ足す。

// 語の全幅。中央揃えの基準に使う。
float WordSpan(int _count, float w, float space)
{
	return (float) _count * w * 2.0f + (float) (_count - 1) * space;
}

// 左端 _left から右へ詰めて語を描く。
float sdWordAt(float2 p, int codes[8], int _count, float _left,
	float w, float h, float th, float space)
{
	const float pitch = w * 2.2f + space;

	float d = 1e9f;

	[unroll]
	for (int i = 0; i < 8; ++i)
	{
		if (i >= _count)
			continue;
		if (codes[i] < 0)
			continue;

		d = min(d, sdLetter(p - float2(_left + w + pitch * (float) i, 0.0f),
							codes[i], w, h, th));
	}
	return d;
}

// 中央揃えで語を描く。
// ボタンや見出しの語はこれ1つで足りる。
// 語ごとに関数を作ると、語を足すたびにシェーダを触ることになる。
float sdWordCentered(float2 p, int codes[8], int _count, float _h)
{
	const float w = _h * 0.56f;
	const float th = _h * 0.20f;
	const float space = w * 0.60f;

	return sdWordAt(p, codes, _count, -WordSpan(_count, w, space) * 0.5f,
					w, _h, th, space);
}

// "CLEAR"
float sdClearText(float2 p, float _h)
{
	int c[8];
	[unroll]
	for (int i = 0; i < 8; ++i)
		c[i] = -1;

	c[0] = 2;
	c[1] = 11;
	c[2] = 4;
	c[3] = 0;
	c[4] = 17; // C L E A R
	return sdWordCentered(p, c, 5, _h);
}

// "TIME" / "HIT" / "LIFE" / "RANK"。リザルトの項目名。
//  _kind : 0=TIME 1=HIT 2=LIFE 3=RANK
float sdStatLabel(float2 p, int _kind, float _h)
{
	int c[8];
	[unroll]
	for (int i = 0; i < 8; ++i)
		c[i] = -1;

	int n = 0;

	if (_kind == 0)			// HOME
	{
		c[0] = 7;
		c[1] = 14;
		c[2] = 12;
		c[3] = 4;
		n = 4;
	}
	else if (_kind == 1)	// TIME
	{
		c[0] = 19;
		c[1] = 8;
		c[2] = 12;
		c[3] = 4;
		n = 4;
	}
	else if (_kind == 2)	// LIFE
	{
		c[0] = 11;
		c[1] = 8;
		c[2] = 5;
		c[3] = 4;
		n = 4;
	}
	else // RANK
	{
		c[0] = 17;
		c[1] = 0;
		c[2] = 13;
		c[3] = 10;
		n = 4;
	}

	// 項目名は左揃え。値が右に来るので、左端を固定して縦に並べる。
	const float w = _h * 0.56f;
	const float th = _h * 0.20f;
	const float space = w * 0.60f;

	return sdWordAt(p, c, n, 0.0f, w, _h, th, space);
}

// "THE CHICKEN DIED..." を1行で描く。
//
// 2行に折ると視線が2回動いて、終わった実感が途切れる。
// 1行で読み切らせた方が、言葉としてそのまま入る。
//
// 語間は字間より広く取る。同じだと1つの長い語に見える。
float sdDeadText(float2 p, float _h)
{
	const float w = _h * 0.56f;
	const float th = _h * 0.20f;
	const float space = w * 0.60f; // 字間
	const float wordGap = w * 1.60f; // 語間

	const float pitch = w * 2.0f + space;

	// 各語の幅
	const float wTHE = 3.0f * w * 2.0f + 2.0f * space;
	const float wCHICKEN = 7.0f * w * 2.0f + 6.0f * space;
	const float wDIED = 4.0f * w * 2.0f + 3.0f * space;

	// 三点リーダのぶんも幅に含める
	const float dotStep = w * 1.05f;
	const float wDots = dotStep * 3.0f;

	const float total = wTHE + wordGap + wCHICKEN + wordGap + wDIED + wDots;
	float left = -total * 0.5f;

	int c[8];

	//---- THE ----
	[unroll]
	for (int i = 0; i < 8; ++i)
		c[i] = -1;
	c[0] = 19;
	c[1] = 7;
	c[2] = 4;

	float d = sdWordAt(p, c, 4, left, w, _h, th, space);
	left += wTHE + wordGap;

	//---- CHICKEN ----
	[unroll]
	for (int i = 0; i < 8; ++i)
		c[i] = -1;
	c[0] = 2;
	c[1] = 7;
	c[2] = 8;
	c[3] = 2;
	c[4] = 10;
	c[5] = 4;
	c[6] = 13;

	d = min(d, sdWordAt(p, c, 8, left, w, _h, th, space));
	left += wCHICKEN + wordGap;

	//---- DIED ----
	[unroll]
	for (int i = 0; i < 8; ++i)
		c[i] = -1;
	c[0] = 3;
	c[1] = 8;
	c[2] = 4;
	c[3] = 3;

	d = min(d, sdWordAt(p, c, 4, left, w, _h, th, space));
	left += wDIED;

	//---- 余韻の三点 ----
	[unroll]
	for (int k = 0; k < 3; ++k)
	{
		const float2 o = float2(left + dotStep * ((float) k + 0.6f), _h * 0.78f);
		d = min(d, sdCircle(p - o, th * 1.05f));
	}

	return d;
}

// ランクの1文字
float sdRankLetter(float2 p, int _rank, float _h)
{
	static const int kTable[5] = { 3, 2, 1, 0, 18 }; // D C B A S

	return sdLetter(p, kTable[clamp(_rank, 0, 4)],
					_h * 0.62f, _h, _h * 0.19f);
}

// キーの語
int KeyWordCodes(int _kind, out int _codes[8])
{
	[unroll]
	for (int i = 0; i < 8; ++i)
		_codes[i] = -1;

	int n = 0;

	if (_kind == 0) // SHIFT
	{
		_codes[0] = 18;
		_codes[1] = 7;
		_codes[2] = 8;
		_codes[3] = 5;
		_codes[4] = 19;
		n = 5;
	}
	else if (_kind == 1) // CTRL
	{
		_codes[0] = 2;
		_codes[1] = 19;
		_codes[2] = 17;
		_codes[3] = 11;
		n = 4;
	}
	else if (_kind == 2) // ALT
	{
		_codes[0] = 0;
		_codes[1] = 11;
		_codes[2] = 19;
		n = 3;
	}
	else if (_kind == 3) // SPACE
	{
		_codes[0] = 18;
		_codes[1] = 15;
		_codes[2] = 0;
		_codes[3] = 2;
		_codes[4] = 4;
		n = 5;
	}
	else if (_kind == 4) // ENTER
	{
		_codes[0] = 4;
		_codes[1] = 13;
		_codes[2] = 19;
		_codes[3] = 4;
		_codes[4] = 17;
		n = 5;
	}
	else if (_kind == 5) // TAB
	{
		_codes[0] = 19;
		_codes[1] = 0;
		_codes[2] = 1;
		n = 3;
	}
	else if (_kind == 6) // ESC
	{
		_codes[0] = 4;
		_codes[1] = 18;
		_codes[2] = 2;
		n = 3;
	}

	return n;
}

// 語の全幅。sdWordAt の送り(pitch)と同じ式で出す。
// ここがずれると、キーキャップの幅が語に合わなくなる。
float KeyWordSpan(int _count, float w, float space)
{
	const float pitch = w * 2.2f + space;
	return (float) (_count - 1) * pitch + w * 2.0f;
}

// "HIT!!" の右端(基準点からの距離)
float HitTextWidth(float _h)
{
	return _h * 0.86f * 2.0f + _h * 0.72f + _h * 0.40f + _h * 0.10f;
}

#endif // INC_KD_HUD_FONT_HLSLI
