#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include "KdSdfFontGen.h"

//===================================================
//
// SDF フォント
//
// TTF から、要る文字だけを実行時に SDF のアトラスへ焼く。
//  ・外部ツールも、文字集めのバッチも要らない
//  ・JSON に新しい文字が増えても、焼き直すだけで出る
//  ・TTF はインストールせず、このプロセスの中だけで読み込む(FR_PRIVATE)
//
// クレジット専用ではない。他の画面やゲームでもそのまま使える。
//
// 座標の約束
//   グリフの位置・大きさ・送りは「em」(字の大きさ = 1)で持つ。
//   y は下向き。0 がベースライン、負が上。
//   使う側で em に字の大きさ(px)を掛ければ、どの大きさでも同じ形になる。
//
//===================================================
class KdSdfFont
{
public:

	struct Desc
	{
		std::wstring file;		// TTF/OTF のパス。空なら Windows に入っている書体を使う
		std::wstring face;		// 書体名(ファミリ名)
		int  emPx = 64;			// アトラスに焼く字の大きさ。ドット字はドット数の倍数にする
		int  spreadPx = 12;		// 距離を持つ幅。縁取りや光はこの中に収める
		bool pixel = false;		// ドット字。距離をマス目で測り、角を丸めない
		int  weight = 400;
	};

	struct Glyph
	{
		bool  hasImage = false;	// 空白は形を持たず、送りだけ
		float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;

		// 板の位置と大きさ(em)。ベースライン上のペン位置からの距離。距離の余白込み。
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;

		float advance = 0.0f;	// 送り(em)
	};

	KdSdfFont() = default;
	~KdSdfFont() { Release(); }

	KdSdfFont(const KdSdfFont&) = delete;
	KdSdfFont& operator=(const KdSdfFont&) = delete;

	// _chars の文字を焼く。前のアトラスは捨てる。
	// 書体に無い文字は警告を出して '?' に置き換える('?' も無ければ出さない)。
	bool Build(const Desc& _desc, const std::vector<char32_t>& _chars);
	void Release();

	// 無ければ '?'。'?' も無ければ nullptr。
	const Glyph* Find(char32_t _c) const;

	ID3D11ShaderResourceView* GetSRV() const { return m_srv.Get(); }
	int GetWidth() const { return m_atlasW; }
	int GetHeight() const { return m_atlasH; }

	float GetAscent() const { return m_ascent; }	// em
	float GetDescent() const { return m_descent; }	// em

	// 距離場の値の 1em あたりの変化量。縁取りの太さ(em)を距離場の値へ直すのに使う。
	float GetFieldPerEm() const
	{
		return (m_desc.spreadPx > 0)
			? static_cast<float>(m_desc.emPx) / (2.0f * static_cast<float>(m_desc.spreadPx))
			: 0.0f;
	}

	int GetEmPx() const { return m_desc.emPx; }

private:

	bool LoadFontFile(const std::wstring& _file);
	void UnloadFontFile();

	Desc m_desc;
	std::wstring m_loadedFile;	// AddFontResourceEx 済みのファイル

	std::unordered_map<char32_t, Glyph> m_glyphs;
	float m_ascent = 0.8f;
	float m_descent = 0.2f;

	Microsoft::WRL::ComPtr<ID3D11Texture2D> m_tex;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_srv;
	int m_atlasW = 0;
	int m_atlasH = 0;

	static constexpr int kGap = 2;			// グリフどうしの隙間。線形補間の滲み込み防止
	static constexpr int kMaxSize = 8192;
};
