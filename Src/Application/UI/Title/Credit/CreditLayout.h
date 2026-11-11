#pragma once
#include "CreditData.h"

//===================================================
//
// クレジットの組版
//
// CreditData を「何を・どの大きさで・どこに置くか」の行の並びにする。
// 座標は基準解像度(1920x1080)のピクセル。y は上から下へ増える。
// x は画面中央からの距離で持つ(実際の幅は文字を焼いてから決まる)。
//
// 文字の描き方(SDF フォント)には依存しない。
// 書体を差し替えても、この並びはそのまま使える。
//
//===================================================

enum class CreditStyle : uint8_t
{
	Title,		// ゲーム名
	Sub,		// CREDITS
	Heading,	// 区画の見出し
	Name,		// 1行の名前
	AssetTitle,	// 素材名
	AssetInfo,	// 作者 / ライセンス
	Note,		// 備考
	PairKey,	// 左右組みの左
	PairValue,	// 左右組みの右
	Footer,		// 締め
	Count,
};

enum class CreditAlign : uint8_t
{
	Center,
	Right,		// 中央より左に置き、右端を揃える
	Left,		// 中央より右に置き、左端を揃える
};

struct CreditStyleDef
{
	float size;				// 字の大きさ(基準解像度px)
	Math::Vector4 color;
	float advance;			// この行の後に送る量
	float outlineEm;		// 縁取りの太さ(em)。1/16 で16ドット字の1ドット
	float glow;				// 光の強さ(0で無し)
};

// 見た目の表。調整はここだけ見ればよい。
//
// ★字の大きさは16の倍数にする。
//   書体が16ドットのドット字なので、16の倍数だと1ドットが整数の画素になり、
//   ドットの幅が揃う。半端な大きさだと、太いドットと細いドットが混ざって汚く見える。
//
// 色はタイトルのボタン(選択中の金)と揃え、情報の重さで明るさを落とす。
// 光は表題・見出し・締めだけ。全部が光ると、どこが区切りか分からなくなる。
inline const CreditStyleDef& GetCreditStyle(CreditStyle _s)
{
	constexpr float kDot = 1.0f / 16.0f;

	static const CreditStyleDef kTable[] = {
		{ 80.0f, { 1.00f, 0.86f, 0.36f, 1.00f }, 104.0f, kDot, 1.00f },	// Title
		{ 32.0f, { 0.82f, 0.80f, 0.76f, 0.90f },  44.0f, kDot, 0.00f },	// Sub
		{ 48.0f, { 0.98f, 0.78f, 0.42f, 1.00f },  64.0f, kDot, 0.60f },	// Heading
		{ 32.0f, { 0.96f, 0.94f, 0.90f, 1.00f },  48.0f, kDot, 0.00f },	// Name
		{ 32.0f, { 0.96f, 0.94f, 0.90f, 1.00f },  40.0f, kDot, 0.00f },	// AssetTitle
		{ 16.0f, { 0.80f, 0.78f, 0.74f, 1.00f },  24.0f, kDot, 0.00f },	// AssetInfo
		{ 16.0f, { 0.66f, 0.64f, 0.60f, 0.95f },  24.0f, kDot, 0.00f },	// Note
		{ 32.0f, { 0.74f, 0.72f, 0.68f, 0.95f },  48.0f, kDot, 0.00f },	// PairKey
		{ 32.0f, { 0.96f, 0.94f, 0.90f, 1.00f },  48.0f, kDot, 0.00f },	// PairValue
		{ 64.0f, { 1.00f, 0.86f, 0.36f, 1.00f },  80.0f, kDot, 1.00f },	// Footer
	};
	static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(CreditStyle::Count),
		"CreditStyle と表の数を合わせること");

	return kTable[static_cast<size_t>(_s)];
}

struct CreditLine
{
	std::wstring text;
	CreditStyle  style = CreditStyle::Name;
	CreditAlign  align = CreditAlign::Center;
	float y = 0.0f;		// 行の上端
	float h = 0.0f;		// 行の箱の高さ(字の大きさ × kRowFactor)
};

struct CreditLayout
{
	std::vector<CreditLine> lines;	// y の昇順
	float totalHeight = 0.0f;
	int   footerIndex = -1;			// 締めの行。止める位置の基準
};

//===================================================
// 組版
//===================================================
namespace CreditLayoutDetail
{
	// 行の箱の高さ。字の上下にかかる部分(ディセンダなど)を収める。
	constexpr float kRowFactor = 1.35f;

	// 間合い(基準解像度px)
	constexpr float kAfterTitle = 170.0f;
	constexpr float kAfterHeading = 14.0f;
	constexpr float kSectionGap = 120.0f;
	constexpr float kAssetGap = 22.0f;
	constexpr float kBeforeFooter = 140.0f;

	struct Ctx
	{
		CreditLayout& out;
		float y = 0.0f;
	};

	// 行を足す。空の文字列は足さないが、送りは行う(JSONで消した行の跡を残さない)。
	inline void AddLine(Ctx& _c, const std::wstring& _text, CreditStyle _s,
		CreditAlign _a = CreditAlign::Center, bool _advance = true)
	{
		const auto& def = GetCreditStyle(_s);

		if (!_text.empty())
		{
			CreditLine l;
			l.text = _text;
			l.style = _s;
			l.align = _a;
			l.y = _c.y;
			l.h = def.size * kRowFactor;
			_c.out.lines.push_back(std::move(l));
		}

		if (_advance) _c.y += def.advance;
	}

	inline void LayoutName(const CreditEntry& _e, Ctx& _c)
	{
		AddLine(_c, _e.text, CreditStyle::Name);
	}

	// 左右組み。2行を同じ高さに置き、送りは1回だけ。
	inline void LayoutPair(const CreditEntry& _e, Ctx& _c)
	{
		AddLine(_c, _e.name, CreditStyle::PairKey, CreditAlign::Right, false);
		AddLine(_c, _e.credit, CreditStyle::PairValue, CreditAlign::Left, true);
	}

	// 素材。CC BY の表示に要る「作品名・作者・ライセンス」を必ず同じ塊に置く。
	inline void LayoutAsset(const CreditEntry& _e, Ctx& _c)
	{
		AddLine(_c, _e.title, CreditStyle::AssetTitle);

		std::wstring info = _e.author;
		if (!info.empty() && !_e.license.empty()) info += L"  /  ";
		info += _e.license;
		AddLine(_c, info, CreditStyle::AssetInfo);

		if (!_e.note.empty()) AddLine(_c, _e.note, CreditStyle::Note);

		_c.y += kAssetGap;
	}

	inline void LayoutSpace(const CreditEntry& _e, Ctx& _c)
	{
		_c.y += _e.height;
	}

	// 種別 → 組み方。種別を足す時はここに1つ足す。
	using LayoutFn = void (*)(const CreditEntry&, Ctx&);

	inline LayoutFn GetLayoutFn(CreditEntryType _t)
	{
		static constexpr LayoutFn kTable[] = {
			&LayoutName,	// Name
			&LayoutPair,	// Pair
			&LayoutAsset,	// Asset
			&LayoutSpace,	// Space
		};
		static_assert(sizeof(kTable) / sizeof(kTable[0]) == static_cast<size_t>(CreditEntryType::Count),
			"CreditEntryType と表の数を合わせること");

		return kTable[static_cast<size_t>(_t)];
	}
}

inline CreditLayout BuildCreditLayout(const CreditData& _d)
{
	using namespace CreditLayoutDetail;

	CreditLayout out;
	Ctx c{ out };

	//------------------------------------------
	// 表題
	//------------------------------------------
	if (!_d.title.empty() || !_d.subTitle.empty())
	{
		AddLine(c, _d.title, CreditStyle::Title);
		AddLine(c, _d.subTitle, CreditStyle::Sub);
		c.y += kAfterTitle;
	}

	//------------------------------------------
	// 区画
	//------------------------------------------
	for (const auto& sec : _d.sections)
	{
		if (!sec.heading.empty())
		{
			AddLine(c, sec.heading, CreditStyle::Heading);
			c.y += kAfterHeading;
		}

		for (const auto& e : sec.entries)
			GetLayoutFn(e.type)(e, c);

		c.y += kSectionGap;
	}

	//------------------------------------------
	// 締め
	//------------------------------------------
	if (!_d.footer.empty())
	{
		c.y += kBeforeFooter;
		out.footerIndex = static_cast<int>(out.lines.size());
		AddLine(c, _d.footer, CreditStyle::Footer);
	}

	out.totalHeight = c.y;
	return out;
}
