#include "ResourceManager.h"

namespace
{
	// AssetManager の設定ファイル。
	// これだけは AssetManager が動く前に読むので、アドレサブル名ではなくパスで指定するしかない
	constexpr const char* kAssetConfigPath = "./Asset/Data/Config/AssetManagerConfig.json";

	// ここから先はアドレサブル名で引ける
	constexpr const char* kResourceConfigAddressable = "ResourceConfig_js";

	// ResourceConfig.json の中の場所
	constexpr const char* kPtrPollInterval = "/jsonWatcher/pollIntervalMs";
	constexpr const char* kPtrModelList = "/resourceLists/model";
	constexpr const char* kPtrTextureList = "/resourceLists/texture";
	constexpr const char* kFieldConfigFiles = "configFiles";

	// JSONはコメントが書けないので、_ で始まるキーを説明書き用にしている
	constexpr char kCommentPrefix = '_';
}

void ResourceManager::Init()
{
	InitConfigs();

	m_modelList->Init();
	m_texList->Init();
}

void ResourceManager::InitConfigs()
{
	// JsonResourceManager を先に作っておかないと、AssetManager の設定が読めない
	CreateSubSystems();

	InitAssetManager();
	ApplyResourceConfig();
}

void ResourceManager::Release()
{
	// Work がモデルを掴んでいるので Work から先に消す。
	// データリストは最後(デストラクタで監視スレッドを止めて待つ)
	m_workPool.reset();
	m_texList.reset();
	m_modelList.reset();
	m_dataList.reset();
}

void ResourceManager::CreateSubSystems()
{
	if (!m_dataList) { m_dataList = std::make_shared<JsonResourceManager>(); }
	if (!m_modelList) { m_modelList = std::make_shared<ModelResourceManager>(); }
	if (!m_workPool) { m_workPool = std::make_shared<WorkPool>(); }
	if (!m_texList) { m_texList = std::make_shared<TextureManager>(); }
}

void ResourceManager::InitAssetManager()
{
	// ファイルが無ければ既定値(今までと同じ設定)で動かす
	if (m_dataList->TryRegisterByPath(kAssetConfigPath, kAssetConfigKey))
	{
		ASSET_MGR.Init(m_dataList->Get(kAssetConfigKey));
	}
	else
	{
		ASSET_MGR.Init();
	}
}

void ResourceManager::ApplyResourceConfig()
{
	// 無くても止めない。下の GetValue が全部既定値を返すので今までどおり動く
	m_dataList->TryRegister(kResourceConfigAddressable, kResourceConfigKey);

	// 監視間隔
	m_dataList->SetPollIntervalMs(m_dataList->GetValue<int>(
		kResourceConfigKey, kPtrPollInterval, JsonResourceManager::kDefaultPollIntervalMs));

	// モデル・テクスチャのリストファイル
	m_modelList->SetListAddressable(m_dataList->GetValue<std::string>(
		kResourceConfigKey, kPtrModelList, ModelResourceManager::kDefaultListAddressable));
	m_texList->SetListAddressable(m_dataList->GetValue<std::string>(
		kResourceConfigKey, kPtrTextureList, TextureManager::kDefaultListAddressable));

	// 他の設定ファイルもまとめて登録しておく
	//   "configFiles": { "StateMachineConfig": "StateMachineConfig_js" }
	// 左がキー(Get で使う名前)、右がアドレサブル名
	const nlohmann::json configFiles = m_dataList->Get(kResourceConfigKey, kFieldConfigFiles);
	if (!configFiles.is_object()) { return; }

	for (const auto& [key, addressable] : configFiles.items())
	{
		// "_説明" のような _ 始まりのキーはメモ書きなので飛ばす
		if (key.empty() || key.front() == kCommentPrefix) { continue; }
		if (!addressable.is_string()) { continue; }
		m_dataList->TryRegister(addressable.get<std::string>(), key);
	}
}
