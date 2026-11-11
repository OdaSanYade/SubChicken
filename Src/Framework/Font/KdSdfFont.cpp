#include "Framework/KdFramework.h"

#include "KdSdfFont.h"

#include <cstdio>
#include <unordered_set>

namespace
{
	void FontLog(const std::string& _msg)
	{
		OutputDebugStringA(("[SdfFont] " + _msg + "\n").c_str());
	}

	std::string Narrow(const std::wstring& _w)
	{
		if (_w.empty()) return {};
		const int n = WideCharToMultiByte(CP_UTF8, 0, _w.data(), static_cast<int>(_w.size()),
			nullptr, 0, nullptr, nullptr);
		if (n <= 0) return {};
		std::string s(static_cast<size_t>(n), '\0');
		WideCharToMultiByte(CP_UTF8, 0, _w.data(), static_cast<int>(_w.size()), s.data(), n, nullptr, nullptr);
		return s;
	}

	std::string CodeText(char32_t _c)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "U+%04X", static_cast<unsigned>(_c));
		return buf;
	}

	// 1文字ぶんの取り出し結果
	struct RawGlyph
	{
		char32_t code = 0;
		bool hasImage = false;
		int  originX = 0, originY = 0;	// 黒い箱の左上(ベースライン基準、y上向き)
		int  boxW = 0, boxH = 0;
		int  advance = 0;
		std::vector<uint8_t> field;		// 距離場(余白込み)
		int  cellW = 0, cellH = 0;
	};
}

//===================================================
// TTF の読み込み
//===================================================
bool KdSdfFont::LoadFontFile(const std::wstring& _file)
{
	if (_file.empty()) return true;
	if (_file == m_loadedFile) return true;

	UnloadFontFile();

	// FR_PRIVATE : このプロセスだけ。PCにはインストールされない。
	if (AddFontResourceExW(_file.c_str(), FR_PRIVATE, nullptr) <= 0)
	{
		FontLog("cannot load font file " + Narrow(_file));
		return false;
	}

	m_loadedFile = _file;
	return true;
}

void KdSdfFont::UnloadFontFile()
{
	if (m_loadedFile.empty()) return;

	RemoveFontResourceExW(m_loadedFile.c_str(), FR_PRIVATE, nullptr);
	m_loadedFile.clear();
}

void KdSdfFont::Release()
{
	m_srv.Reset();
	m_tex.Reset();
	m_atlasW = 0;
	m_atlasH = 0;
	m_glyphs.clear();
	UnloadFontFile();
}

const KdSdfFont::Glyph* KdSdfFont::Find(char32_t _c) const
{
	auto it = m_glyphs.find(_c);
	if (it != m_glyphs.end()) return &it->second;

	it = m_glyphs.find(U'?');
	return (it != m_glyphs.end()) ? &it->second : nullptr;
}

//===================================================
// 焼く
//===================================================
bool KdSdfFont::Build(const Desc& _desc, const std::vector<char32_t>& _chars)
{
	// 書体ファイルは使い回す。アトラスだけ作り直す。
	m_srv.Reset();
	m_tex.Reset();
	m_atlasW = 0;
	m_atlasH = 0;
	m_glyphs.clear();

	m_desc = _desc;
	m_desc.emPx = std::clamp(m_desc.emPx, 8, 256);
	m_desc.spreadPx = std::clamp(m_desc.spreadPx, 1, m_desc.emPx);

	if (!m_desc.file.empty() && !LoadFontFile(m_desc.file))
	{
		// ファイルが読めなくても書体名で探しには行く。インストール済みなら出る。
		FontLog("fallback to installed font");
	}

	const int em = m_desc.emPx;
	const int spread = m_desc.spreadPx;

	HDC dc = CreateCompatibleDC(nullptr);
	if (!dc)
	{
		FontLog("CreateCompatibleDC failed");
		return false;
	}

	// ANTIALIASED_QUALITY : 白黒にした時に境目が安定する
	HFONT font = CreateFontW(-em, 0, 0, 0, m_desc.weight, FALSE, FALSE, FALSE,
		DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
		m_desc.face.empty() ? L"Yu Gothic UI" : m_desc.face.c_str());

	if (!font)
	{
		FontLog("CreateFontW failed");
		DeleteDC(dc);
		return false;
	}

	HGDIOBJ oldFont = SelectObject(dc, font);

	auto cleanup = [&]()
		{
			SelectObject(dc, oldFont);
			DeleteObject(font);
			DeleteDC(dc);
		};

	// 書体名が違うと GDI は黙って別の書体に置き換える。気付けるように知らせる。
	{
		wchar_t got[LF_FACESIZE] = {};
		GetTextFaceW(dc, LF_FACESIZE, got);
		if (!m_desc.face.empty() && m_desc.face != got)
			FontLog("face \"" + Narrow(m_desc.face) + "\" not found, using \"" + Narrow(got) + "\"");
	}

	// 縦の基準
	{
		TEXTMETRICW tm = {};
		GetTextMetricsW(dc, &tm);
		m_ascent = static_cast<float>(tm.tmAscent) / static_cast<float>(em);
		m_descent = static_cast<float>(tm.tmDescent) / static_cast<float>(em);
	}

	//------------------------------------------
	// 文字の用意
	//------------------------------------------
	// '?' は代わりの文字として必ず入れる。重複は除く。
	std::vector<char32_t> chars = _chars;
	chars.push_back(U'?');
	std::sort(chars.begin(), chars.end());
	chars.erase(std::unique(chars.begin(), chars.end()), chars.end());

	const KdSdfGen::Metric metric = m_desc.pixel ? KdSdfGen::Metric::Chebyshev : KdSdfGen::Metric::Euclid;

	// 縦横そのまま
	MAT2 mat = {};
	mat.eM11.value = 1;
	mat.eM22.value = 1;

	std::vector<RawGlyph> raws;
	raws.reserve(chars.size());

	std::vector<uint8_t> bitmap;
	std::vector<uint8_t> inside;

	for (char32_t c : chars)
	{
		// GDI の文字コードは UTF-16 の1単位まで
		if (c > 0xFFFF || (c >= 0xD800 && c <= 0xDFFF))
		{
			FontLog(CodeText(c) + " is outside BMP (skipped)");
			continue;
		}

		const wchar_t wc = static_cast<wchar_t>(c);

		// 書体に無い文字。GetGlyphOutline は書体の代替をしないので、ここで弾いて '?' にする。
		{
			WORD gi = 0;
			if (GetGlyphIndicesW(dc, &wc, 1, &gi, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR || gi == 0xFFFF)
			{
				FontLog(CodeText(c) + " not in font (shown as '?')");
				continue;
			}
		}

		GLYPHMETRICS gm = {};
		const DWORD size = GetGlyphOutlineW(dc, wc, GGO_GRAY8_BITMAP, &gm, 0, nullptr, &mat);
		if (size == GDI_ERROR)
		{
			FontLog(CodeText(c) + " GetGlyphOutline failed");
			continue;
		}

		RawGlyph r;
		r.code = c;
		r.advance = gm.gmCellIncX;
		r.originX = gm.gmptGlyphOrigin.x;
		r.originY = gm.gmptGlyphOrigin.y;
		r.boxW = static_cast<int>(gm.gmBlackBoxX);
		r.boxH = static_cast<int>(gm.gmBlackBoxY);

		// 空白は形を持たない(size == 0)
		if (size > 0 && r.boxW > 0 && r.boxH > 0)
		{
			bitmap.assign(size, 0);
			if (GetGlyphOutlineW(dc, wc, GGO_GRAY8_BITMAP, &gm, size, bitmap.data(), &mat) == GDI_ERROR)
			{
				FontLog(CodeText(c) + " GetGlyphOutline failed");
				continue;
			}

			// GGO_GRAY8 は 0〜64 の65段、行は4バイト境界
			const int pitch = (r.boxW + 3) & ~3;

			r.cellW = r.boxW + spread * 2;
			r.cellH = r.boxH + spread * 2;
			inside.assign(static_cast<size_t>(r.cellW) * r.cellH, 0);

			for (int y = 0; y < r.boxH; ++y)
			{
				for (int x = 0; x < r.boxW; ++x)
				{
					const uint8_t v = bitmap[static_cast<size_t>(y) * pitch + x];
					inside[static_cast<size_t>(y + spread) * r.cellW + (x + spread)] = (v >= 32) ? 1 : 0;
				}
			}

			KdSdfGen::BuildField(inside, r.cellW, r.cellH, spread, metric, r.field);
			r.hasImage = true;
		}

		raws.push_back(std::move(r));
	}

	cleanup();

	//------------------------------------------
	// 並べる
	//------------------------------------------
	std::vector<KdSdfGen::PackItem> items(raws.size());
	size_t area = 0;
	for (size_t i = 0; i < raws.size(); ++i)
	{
		if (!raws[i].hasImage) continue;
		items[i].w = raws[i].cellW;
		items[i].h = raws[i].cellH;
		area += static_cast<size_t>(raws[i].cellW + kGap) * (raws[i].cellH + kGap);
	}

	// 正方形に近くなる幅から始め、収まらなければ広げる
	int atlasW = 256;
	while (atlasW < kMaxSize && static_cast<size_t>(atlasW) * atlasW < area) atlasW *= 2;

	int atlasH = 0;
	for (;;)
	{
		atlasH = KdSdfGen::ShelfPack(items, atlasW, kGap);
		if (atlasH <= atlasW || atlasW >= kMaxSize) break;
		atlasW *= 2;
	}

	if (atlasH > kMaxSize)
	{
		FontLog("atlas too large (" + std::to_string(atlasW) + "x" + std::to_string(atlasH) + ")");
		return false;
	}
	atlasH = std::max(atlasH, 1);

	//------------------------------------------
	// アトラスへ書き込む
	//------------------------------------------
	// 何も無い所は「遠い外」= 0
	std::vector<uint8_t> px(static_cast<size_t>(atlasW) * atlasH, 0);

	for (size_t i = 0; i < raws.size(); ++i)
	{
		const RawGlyph& r = raws[i];
		if (!r.hasImage) continue;

		const auto& it = items[i];
		for (int y = 0; y < r.cellH; ++y)
		{
			std::copy_n(r.field.data() + static_cast<size_t>(y) * r.cellW, r.cellW,
				px.data() + static_cast<size_t>(it.y + y) * atlasW + it.x);
		}
	}

	{
		D3D11_TEXTURE2D_DESC td = {};
		td.Width = static_cast<UINT>(atlasW);
		td.Height = static_cast<UINT>(atlasH);
		td.MipLevels = 1;
		td.ArraySize = 1;
		td.Format = DXGI_FORMAT_R8_UNORM;
		td.SampleDesc.Count = 1;
		td.Usage = D3D11_USAGE_IMMUTABLE;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA sd = {};
		sd.pSysMem = px.data();
		sd.SysMemPitch = static_cast<UINT>(atlasW);

		auto* dev = KdDirect3D::Instance().WorkDev();

		if (FAILED(dev->CreateTexture2D(&td, &sd, m_tex.GetAddressOf())) ||
			FAILED(dev->CreateShaderResourceView(m_tex.Get(), nullptr, m_srv.GetAddressOf())))
		{
			FontLog("texture creation failed");
			m_srv.Reset();
			m_tex.Reset();
			return false;
		}
	}

	m_atlasW = atlasW;
	m_atlasH = atlasH;

	//------------------------------------------
	// グリフ表
	//------------------------------------------
	const float inv = 1.0f / static_cast<float>(em);
	const float iw = 1.0f / static_cast<float>(atlasW);
	const float ih = 1.0f / static_cast<float>(atlasH);

	for (size_t i = 0; i < raws.size(); ++i)
	{
		const RawGlyph& r = raws[i];

		Glyph g;
		g.hasImage = r.hasImage;
		g.advance = static_cast<float>(r.advance) * inv;

		if (r.hasImage)
		{
			const auto& it = items[i];
			g.u0 = static_cast<float>(it.x) * iw;
			g.v0 = static_cast<float>(it.y) * ih;
			g.u1 = static_cast<float>(it.x + r.cellW) * iw;
			g.v1 = static_cast<float>(it.y + r.cellH) * ih;

			// 黒い箱の左上はベースラインより上(GDIはy上向き)。余白のぶん外へ広げる。
			g.x = static_cast<float>(r.originX - spread) * inv;
			g.y = static_cast<float>(-r.originY - spread) * inv;
			g.w = static_cast<float>(r.cellW) * inv;
			g.h = static_cast<float>(r.cellH) * inv;
		}

		m_glyphs.emplace(r.code, g);
	}

	FontLog("atlas " + std::to_string(atlasW) + "x" + std::to_string(atlasH)
		+ ", " + std::to_string(m_glyphs.size()) + " glyphs");
	return true;
}
