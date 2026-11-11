#pragma once
#include <fstream>
#include <string>
#include <vector>
#include "LessPath/AssetManager.h"

//==========================================================
// ResourceListLoader
// ModelData.json / TextureData.json のような
//   { "キー": { "fileName": "アドレサブル名" }, ... }
// という形のリストを読んで、ファイルパスの解決まで済ませる。
//
// ModelResourceManager と TextureManager がほぼ同じ処理を
// それぞれ持っていたので、ここにまとめた。
// メインスレッド専用(ASSET_MGR を触るため)
//==========================================================
namespace ResourceList
{
	// リストの各要素で、ファイルを指しているキー
	inline constexpr const char* kFileNameKey = "fileName";

	struct Entry
	{
		std::string key;		// ゲーム側から引くときの名前
		std::string filePath;	// 解決済みのファイルパス
	};

	//------------------------------------------------------
	// リストを読む
	// _outEntries    : 解決できたもの(JSONのキー順。nlohmann::json は名前順に並ぶ)
	// _outFailedKeys : fileName が無い・アドレサブル名が見つからない要素のキー
	// 戻り値         : リストファイル自体が読めたかどうか
	//------------------------------------------------------
	inline bool Load(const std::string& _listAddressable,
		std::vector<Entry>& _outEntries,
		std::vector<std::string>& _outFailedKeys)
	{
		std::string listPath;
		if (!ASSET_MGR.TryGetFilePath(_listAddressable, listPath)) { return false; }

		std::ifstream file(listPath);
		if (!file.is_open()) { return false; }

		const nlohmann::json list = nlohmann::json::parse(file, nullptr, false);
		if (list.is_discarded() || !list.is_object()) { return false; }

		_outEntries.reserve(_outEntries.size() + list.size());

		// パス解決までメインスレッドで済ませておくと
		// ワーカー側は ASSET_MGR に一切触らなくてよくなる
		for (const auto& [key, value] : list.items())
		{
			const auto fileNameIt = value.is_object() ? value.find(kFileNameKey) : value.end();
			if (fileNameIt == value.end() || !fileNameIt->is_string())
			{
				_outFailedKeys.push_back(key);
				continue;
			}

			std::string resolved;
			if (!ASSET_MGR.TryGetFilePath(fileNameIt->get<std::string>(), resolved))
			{
				_outFailedKeys.push_back(key);
				continue;
			}

			_outEntries.push_back({ key, std::move(resolved) });
		}

		return true;
	}

	// 読めなかったキーを出力ウィンドウに並べる
	inline void ReportFailures(const char* _tag, const std::vector<std::string>& _failedKeys)
	{
		for (const auto& key : _failedKeys)
		{
			OutputDebugStringA(("[" + std::string(_tag) + "] 読み込み失敗: " + key + "\n").c_str());
		}
	}
}
