#pragma once
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

class C_AssetManager
{
public:

	struct MetaData
	{
		std::string filePath;
	};

	//------------------------------------------------------
	// 設定
	// AssetManagerConfig.json から読む。JSONに書いていない項目は下の値のまま
	// (= JSONが無くても今までと同じ動きになる)
	//------------------------------------------------------
	struct Config
	{
		std::string assetRoot = "./Asset";						// Assetファイルの先頭ディレクトリ
		std::string metaExtension = ".kdfwmeta";				// 作成するメタファイルの拡張子
		std::string logFile = "./Asset/AssetManager.log";		// Log保存場所
		std::string fontDirectory = "./Asset/Texture/Fonts";	// FontManager に任せるので除外するディレクトリ(空なら除外しない)
		std::vector<std::string> supportedExtensions =			// メタファイルを作る拡張子
		{
			".json", ".TTF", ".png", ".gltf", ".wav", ".mp3",
		};
		bool regenerateMetaFiles = true;						// 起動のたびにメタファイルを全部作り直すか

		// JSONから読む。読めない項目は既定値のまま
		static Config FromJson(const nlohmann::json& _json);
	};

	//------------------------------------------------------
	// 初期化
	// 引数なしは既定の設定で動く
	//------------------------------------------------------
	void Init();
	void Init(const nlohmann::json& _configJson);
	void Init(const Config& _config);

	// Metaファイルを探してApplicationで使用するリストを作成する
	void CreateAddressablesList();

	// AddressableNameを指定してファイルパスの取得
	// 見つからなければ false を返し、out は変更しない
	bool TryGetFilePath(const std::string& addressableName, std::string& out) const;

	// AddressableNameを指定してファイルパスの取得
	// 見つからない場合は空文字を返す(デバッグ時はassertで停止)
	std::string GetFilePath(const std::string& addressableName) const;

	// 登録されているか確認するだけ
	bool Contains(const std::string& addressableName) const;

	// Assetsフォルダ以下をクロールして、足りないMetaファイルを作る
	void CreateMetaFileForAllFiles();

	// サポートしているファイル形式か確認する
	bool IsSupportedFile(const std::filesystem::path& filePath) const;

	// ファイル一つに対してのMetaファイル作成
	nlohmann::json CreateMetaFileForFile(const std::filesystem::path& srcFile) const;

	// AddressableName作成
	std::string AddressableName(const std::filesystem::path& srcFile) const;

	// 作成したMetaファイルを全部削除
	void DeleteAllMetaFiles();

	// 今の設定(前は m_assetFilePass などを直接公開していた)
	const Config& GetConfig() const { return m_config; }

private:

	// Asset以下を再帰的に回す。途中でアクセスできないフォルダがあっても止まらない
	void ForEachAssetEntry(const std::function<void(const std::filesystem::directory_entry&)>& _fn) const;

	bool IsMetaFile(const std::filesystem::directory_entry& _entry) const;
	bool IsInFontDirectory(const std::filesystem::path& _path) const;

	// メタファイル1つを読む。壊れていたら false
	bool ReadMetaFile(const std::filesystem::path& _metaPath, std::string& _outAddressable, MetaData& _outMeta) const;

	// ログ。Init で1回だけ作り直し、あとは全部追記
	void WriteLog(const std::string& _message) const;

	Config m_config;

	// AddressableNameとメタ情報のリスト
	std::unordered_map<std::string, MetaData> m_addressables;

	// const な GetFilePath からも書きたいので mutable
	mutable std::ofstream m_log;

	C_AssetManager() = default;

public:

	// シングルトンなのでコピーさせない(前はコピーコンストラクタが残っていた)
	C_AssetManager(const C_AssetManager&) = delete;
	C_AssetManager& operator=(const C_AssetManager&) = delete;

	static C_AssetManager& GetInstance()
	{
		static C_AssetManager instance;
		return instance;
	}
};
#define ASSET_MGR C_AssetManager::GetInstance()
