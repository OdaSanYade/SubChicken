#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class TextureManager
{
public:

	// リストのアドレサブル名。ResourceConfig.json で上書きできる
	static constexpr const char* kDefaultListAddressable = "TextureData_js";

	TextureManager() = default;
	~TextureManager() = default;

	// const 参照で受けるのが正しい形(前は TextureManager& になっていた)
	TextureManager(const TextureManager&) = delete;
	TextureManager& operator = (const TextureManager&) = delete;

	// 読み込むリストを差し替える(Prepare より前に呼ぶ)
	void SetListAddressable(std::string _addressable) { m_listAddressable = std::move(_addressable); }
	const std::string& GetListAddressable() const { return m_listAddressable; }

	//----------------------------------------------
	// 一括初期化(同期)
	//----------------------------------------------
	void Init();

	//----------------------------------------------
	// 非同期ロード用の3段構え
	//
	// Prepare  : メインスレッド。JSONを読みスロットを確保する
	// LoadSlot : スロット単位で1枚読む
	//            スロットごとに別インデックスへ書くので排他は不要
	// Commit   : メインスレッド。名前引き用のマップを埋める
	//----------------------------------------------
	size_t Prepare();
	bool   LoadSlot(size_t _slotIndex);
	void   Commit();

	// 進捗表示に使う。Prepare後から有効
	size_t GetPendingCount()                            const { return m_pending.size(); }
	const std::string& GetPendingKey(size_t _slotIndex) const;

	std::shared_ptr<KdTexture> GetTex(const std::string& _key) const;

	// 読み込みに失敗したキーの一覧(ロード画面での表示用)
	const std::vector<std::string>& GetFailedKeys() const { return m_failedKeys; }

private:

	// ロード予定の1件
	struct PendingTexture
	{
		size_t      slot = 0;
		std::string key;
		std::string filePath;
	};

	std::string m_listAddressable = kDefaultListAddressable;

	std::unordered_map<std::string, std::shared_ptr<KdTexture>> m_texture;

	std::vector<std::shared_ptr<KdTexture>> m_loaded;
	std::vector<PendingTexture> m_pending;
	std::vector<std::string>    m_failedKeys;
};
