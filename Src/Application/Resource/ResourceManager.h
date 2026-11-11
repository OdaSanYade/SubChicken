#pragma once
#include <memory>
#include <string>
#include "Resources/LessPath/AssetManager.h"
#include "Resources/DataList/JsonResourceManager.h"
#include "Resources/Model/ModelManager.h"
#include "Resources/Texture/TextureManager.h"
#include "Resources/Model/WorkPool.h"

class ResourceManager
{
public:

	// 設定ファイルを引くときのキー。GetDataList()->Get(キー) で中身が取れる
	static constexpr const char* kAssetConfigKey = "AssetManagerConfig";
	static constexpr const char* kResourceConfigKey = "ResourceConfig";

	//----------------------------------------------
	// 初期化
	// InitConfigs() のあとモデル・テクスチャを同期ロードする
	//----------------------------------------------
	void Init();

	//----------------------------------------------
	// 設定まわりだけ初期化する
	// サブシステム生成 → AssetManager → ResourceConfig の順。
	// ロード画面で Prepare / LoadSlot / Commit を自前で回すときは、
	// ASSET_MGR.Init() と CreateSubSystems() の代わりにこれを先に呼ぶ
	//----------------------------------------------
	void InitConfigs();

	//解放
	void Release();

	// 各リストの実体だけ作る
	void CreateSubSystems();

	//----------------------------------------------
	// データリストを獲得
	// Register : jsonファイルの登録(アドレサブル名, key)
	// Get      : データの獲得(key)
	// GetValue : 値を1つ取る(key, "/a/b", 既定値)
	//
	// 戻り値は const 参照。前は非const参照だったので、
	// 呼ぶ側で reset() や代入をするとマネージャ本体が消えてしまう作りだった
	//----------------------------------------------
	const std::shared_ptr<JsonResourceManager>& GetDataList() const { return m_dataList; }

	//----------------------------------------------
	// モデルリストを獲得
	// Get : リソースの獲得(key)
	//----------------------------------------------
	const std::shared_ptr<ModelResourceManager>& GetModelList() const { return m_modelList; }

	//----------------------------------------------
	// ワークプール
	//----------------------------------------------
	const std::shared_ptr<WorkPool>& GetWorkPool() const { return m_workPool; }

	//----------------------------------------------
	// テクスチャリストを獲得
	// Get : リソースの獲得(key)
	//----------------------------------------------
	const std::shared_ptr<TextureManager>& GetTexList() const { return m_texList; }

	std::string GetFilePath(const std::string& _fileName) const { return ASSET_MGR.GetFilePath(_fileName); }

private:

	// AssetManager の設定を読んで初期化する
	void InitAssetManager();

	// ResourceConfig を読んで各サブシステムに反映する
	void ApplyResourceConfig();

	//リソースの管理クラス
	std::shared_ptr<JsonResourceManager>  m_dataList;
	std::shared_ptr<ModelResourceManager> m_modelList;
	std::shared_ptr<WorkPool>             m_workPool;
	std::shared_ptr<TextureManager>       m_texList;

//シングルトン
private:

	ResourceManager() = default;
	~ResourceManager() = default;

	//コピーガード
	ResourceManager(const ResourceManager&) = delete;
	ResourceManager& operator = (const ResourceManager&) = delete;

public:

	static ResourceManager& GetInstance()
	{
		static ResourceManager instance;
		return instance;
	}
};

#define RES_MGR ResourceManager::GetInstance()
