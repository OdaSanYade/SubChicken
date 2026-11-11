#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

using ModelId = uint32_t;
inline constexpr ModelId kInvalidModelId = static_cast<ModelId>(-1);

class ModelResourceManager
{
public:

	// リストのアドレサブル名。ResourceConfig.json で上書きできる
	static constexpr const char* kDefaultListAddressable = "ModelData_js";

	ModelResourceManager() = default;

	//コピーガード
	ModelResourceManager(const ModelResourceManager&) = delete;
	ModelResourceManager& operator = (const ModelResourceManager&) = delete;

	// 読み込むリストを差し替える(Prepare より前に呼ぶ)
	void SetListAddressable(std::string _addressable) { m_listAddressable = std::move(_addressable); }
	const std::string& GetListAddressable() const { return m_listAddressable; }

	//----------------------------------------------
	// 一括初期化(同期)
	// 中身は Prepare → LoadSlot × n → Commit と同じ
	//----------------------------------------------
	void Init();

	//----------------------------------------------
	// 非同期ロード用の3段構え
	//
	// Prepare  : メインスレッド。JSONを読みIDを確定、器だけ確保する
	// LoadSlot : ワーカースレッド可。スロット単位でファイルを読む
	//            スロットごとに別インデックスへ書くので排他は不要
	// Commit   : メインスレッド。名前引き用のマップを埋める
	//----------------------------------------------
	size_t Prepare();
	bool   LoadSlot(size_t _slotIndex);
	void   Commit();

	// 進捗表示に使う。Prepare後から有効
	size_t GetPendingCount()                const { return m_pending.size(); }
	const std::string& GetPendingKey(size_t _slotIndex) const;

	//リソースの獲得
	std::shared_ptr<KdModelData> GetModel(const std::string& _key) const;
	std::shared_ptr<KdModelWork> GetWork(const std::string& _key) const;

	//モデルのID発行
	ModelId GetModelId(const std::string& _key) const;
	const std::shared_ptr<KdModelData>& GetModelById(ModelId id) const
	{
		static const std::shared_ptr<KdModelData> null;
		if (id >= m_resourceList.size()) return null;
		return m_resourceList[id];
	}
	size_t GetModelCount() const { return m_resourceList.size(); }

	// 読み込みに失敗したキーの一覧(ロード画面での表示用)
	const std::vector<std::string>& GetFailedKeys() const { return m_failedKeys; }

private:

	// ロード予定の1件
	struct PendingModel
	{
		ModelId     id = kInvalidModelId;
		std::string key;
		std::string filePath;
	};

	std::string m_listAddressable = kDefaultListAddressable;

	std::unordered_map<std::string, std::shared_ptr<KdModelData>> m_resources;
	std::unordered_map<std::string, ModelId> m_keyToId;
	std::vector<std::shared_ptr<KdModelData>> m_resourceList;

	std::vector<PendingModel> m_pending;
	std::vector<std::string>  m_failedKeys;
};
