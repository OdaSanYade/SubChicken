#pragma once
#if __has_include(<nlohmann/json.hpp>)
#include <nlohmann/json.hpp>
#endif
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

//===================================================
//
// クレジットのデータ(credits.json)
//
// 読み込みだけを持つ。見た目(並べ方・大きさ・色)は CreditLayout 側。
// 文字列は UTF-8 で読み、wchar_t(UTF-16)へ変換して持つ。
//
// 壊れた JSON でも落とさない。
//  ・読めなければ空のデータを返す(戻るボタンは別の仕組みなので必ず動く)
//  ・キーが無ければ既定値を使い、ログに警告を出す
//
//===================================================

enum class CreditEntryType : uint8_t
{
	Name,		// 1行
	Pair,		// 左右に「項目 / 名前」
	Asset,		// 素材名・作者/ライセンス・備考の3段
	Space,		// 空き
	Count,
};

struct CreditEntry
{
	CreditEntryType type = CreditEntryType::Name;

	std::wstring text;		// name
	std::wstring name;		// pair : 左
	std::wstring credit;	// pair : 右
	std::wstring title;		// asset
	std::wstring author;	// asset
	std::wstring license;	// asset
	std::wstring note;		// asset(任意)
	float height = 0.0f;	// space(基準解像度px)
};

struct CreditSection
{
	std::wstring heading;
	std::vector<CreditEntry> entries;
};

// 書体。JSON の "font" で指定する。無ければ Windows の書体を使う。
struct CreditFont
{
	std::wstring file;		// TTF/OTF のパス(作業ディレクトリから)
	std::wstring face;		// 書体名(ファミリ名)
	bool  pixel = false;	// ドット字。距離をマス目で測り、角を丸めない
	int   emPx = 64;		// アトラスに焼く字の大きさ
};

struct CreditData
{
	CreditFont font;

	float scrollSpeed = 60.0f;	// 基準解像度px / 秒
	bool  loop = false;
	float startDelay = 1.0f;	// 流れ始めるまでの秒
	float endHold = 2.5f;		// loop 時、流れ切ってから先頭に戻るまでの秒

	std::wstring title;
	std::wstring subTitle;
	std::wstring footer;

	std::vector<CreditSection> sections;
};

//===================================================
// 補助
//===================================================
namespace CreditDetail
{
	inline void Log(const std::string& _msg)
	{
		OutputDebugStringA(("[Credit] " + _msg + "\n").c_str());
	}

	inline std::wstring Utf8ToWide(const std::string& _s)
	{
		if (_s.empty()) return {};

		const int n = MultiByteToWideChar(CP_UTF8, 0, _s.data(),
			static_cast<int>(_s.size()), nullptr, 0);
		if (n <= 0) return {};

		std::wstring w(static_cast<size_t>(n), L'\0');
		MultiByteToWideChar(CP_UTF8, 0, _s.data(), static_cast<int>(_s.size()), w.data(), n);
		return w;
	}

	// キーを取り出す。無い・型が違う時は既定値。
	// _required が true の時だけ「無い」を警告する。任意のキーで警告が溢れないように。
	template<class T>
	T Get(const nlohmann::json& _j, const char* _key, const T& _def,
		const std::string& _where, bool _required)
	{
		if (!_j.is_object())
		{
			Log(_where + ": not an object");
			return _def;
		}

		const auto it = _j.find(_key);
		if (it == _j.end())
		{
			if (_required) Log(_where + ": missing \"" + _key + "\" (default used)");
			return _def;
		}

		try
		{
			return it->template get<T>();
		}
		catch (const std::exception&)
		{
			Log(_where + ": wrong type \"" + _key + "\" (default used)");
			return _def;
		}
	}

	inline std::wstring GetStr(const nlohmann::json& _j, const char* _key,
		const std::string& _where, bool _required)
	{
		return Utf8ToWide(Get<std::string>(_j, _key, std::string(), _where, _required));
	}

	//------------------------------------------
	// 種別の表
	//------------------------------------------
	// 種別ごとに必須のキーを持つ。種別を足す時はここに1行足す。
	struct TypeDef
	{
		CreditEntryType type;
		std::vector<const char*> required;
	};

	inline const std::unordered_map<std::string, TypeDef>& TypeTable()
	{
		static const std::unordered_map<std::string, TypeDef> kTable = {
			{ "name",  { CreditEntryType::Name,  { "text" } } },
			{ "pair",  { CreditEntryType::Pair,  { "name", "credit" } } },
			{ "asset", { CreditEntryType::Asset, { "title", "author", "license" } } },
			{ "space", { CreditEntryType::Space, { "height" } } },
		};
		return kTable;
	}

	inline bool HasKey(const std::vector<const char*>& _keys, const char* _k)
	{
		for (const char* k : _keys)
			if (std::string_view(k) == _k) return true;
		return false;
	}

	inline bool ParseEntry(const nlohmann::json& _j, const std::string& _where, CreditEntry& _out)
	{
		const std::string typeName = Get<std::string>(_j, "type", std::string(), _where, true);

		const auto& table = TypeTable();
		const auto it = table.find(typeName);
		if (it == table.end())
		{
			Log(_where + ": unknown type \"" + typeName + "\" (skipped)");
			return false;
		}

		const auto& req = it->second.required;
		_out.type = it->second.type;

		_out.text = GetStr(_j, "text", _where, HasKey(req, "text"));
		_out.name = GetStr(_j, "name", _where, HasKey(req, "name"));
		_out.credit = GetStr(_j, "credit", _where, HasKey(req, "credit"));
		_out.title = GetStr(_j, "title", _where, HasKey(req, "title"));
		_out.author = GetStr(_j, "author", _where, HasKey(req, "author"));
		_out.license = GetStr(_j, "license", _where, HasKey(req, "license"));
		_out.note = GetStr(_j, "note", _where, false);
		_out.height = std::max(0.0f, Get<float>(_j, "height", 0.0f, _where, HasKey(req, "height")));
		return true;
	}
}

//===================================================
// 読み込み
//===================================================
// 失敗したら false。_out は既定値のまま(空表示)になる。
inline bool LoadCreditData(const std::string& _path, CreditData& _out)
{
	using namespace CreditDetail;

	_out = CreditData{};

	std::ifstream ifs(_path, std::ios::binary);
	if (!ifs)
	{
		Log("cannot open " + _path);
		return false;
	}

	// 例外を投げない形で読む。壊れていれば is_discarded() が立つ。
	const nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
	if (root.is_discarded() || !root.is_object())
	{
		Log("parse error " + _path);
		return false;
	}

	_out.scrollSpeed = std::max(1.0f, Get<float>(root, "scrollSpeed", 60.0f, "root", true));
	_out.loop = Get<bool>(root, "loop", false, "root", false);
	_out.startDelay = std::max(0.0f, Get<float>(root, "startDelay", 1.0f, "root", false));
	_out.endHold = std::max(0.0f, Get<float>(root, "endHold", 2.5f, "root", false));

	// 書体。無ければ既定の書体で出す。
	{
		const auto it = root.find("font");
		if (it != root.end() && it->is_object())
		{
			_out.font.file = GetStr(*it, "file", "font", false);
			_out.font.face = GetStr(*it, "face", "font", true);
			_out.font.pixel = Get<bool>(*it, "pixel", false, "font", false);
			_out.font.emPx = static_cast<int>(std::clamp(
				Get<float>(*it, "emPx", 64.0f, "font", false), 8.0f, 256.0f));
		}
	}

	// title / footer はオブジェクトで持つ。無ければ出さないだけ。
	{
		const auto it = root.find("title");
		if (it != root.end() && it->is_object())
		{
			_out.title = GetStr(*it, "text", "title", true);
			_out.subTitle = GetStr(*it, "sub", "title", false);
		}
	}
	{
		const auto it = root.find("footer");
		if (it != root.end() && it->is_object())
			_out.footer = GetStr(*it, "text", "footer", true);
	}

	//------------------------------------------
	// 区画
	//------------------------------------------
	const auto secIt = root.find("sections");
	if (secIt == root.end() || !secIt->is_array())
	{
		Log("missing \"sections\" array");
		return true;	// 見出しと締めだけでも出す
	}

	int si = 0;
	for (const auto& sj : *secIt)
	{
		const std::string where = "sections[" + std::to_string(si++) + "]";
		if (!sj.is_object())
		{
			Log(where + ": not an object (skipped)");
			continue;
		}

		CreditSection sec;
		sec.heading = GetStr(sj, "heading", where, false);

		const auto entIt = sj.find("entries");
		if (entIt != sj.end() && entIt->is_array())
		{
			int ei = 0;
			for (const auto& ej : *entIt)
			{
				const std::string ew = where + ".entries[" + std::to_string(ei++) + "]";

				CreditEntry e;
				if (ParseEntry(ej, ew, e)) sec.entries.push_back(std::move(e));
			}
		}
		else
		{
			Log(where + ": missing \"entries\" array");
		}

		_out.sections.push_back(std::move(sec));
	}

	return true;
}
