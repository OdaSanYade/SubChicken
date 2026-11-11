#include "ModelManager.h"
#include "../ResourceListLoader.h"

namespace
{
	constexpr const char* kLogTag = "ModelResourceManager";
}

// ----------------------------------------------------------
// 一括初期化(同期)
// ----------------------------------------------------------
void ModelResourceManager::Init()
{
	const size_t count = Prepare();

	for (size_t i = 0; i < count; ++i)
	{
		LoadSlot(i);
	}

	Commit();
}

// ----------------------------------------------------------
// メインスレッド
// JSONを読み、キーとIDを確定させ、器だけ確保する
// 戻り値はロード対象の件数
// ----------------------------------------------------------
size_t ModelResourceManager::Prepare()
{
	m_pending.clear();
	m_failedKeys.clear();
	m_resources.clear();
	m_keyToId.clear();
	m_resourceList.clear();

	std::vector<ResourceList::Entry> entries;
	if (!ResourceList::Load(m_listAddressable, entries, m_failedKeys))
	{
		OutputDebugStringA(("[ModelResourceManager] リストが読めません : " + m_listAddressable + "\n").c_str());
		assert(0 && U8("モデルのファイルリストを読み込めませんでした"));
		return 0;
	}

	// ここでIDを確定させる。ID = スロット番号
	m_pending.reserve(entries.size());
	for (auto& entry : entries)
	{
		PendingModel p;
		p.id = static_cast<ModelId>(m_pending.size());
		p.key = std::move(entry.key);
		p.filePath = std::move(entry.filePath);

		m_keyToId[p.key] = p.id;
		m_pending.push_back(std::move(p));
	}

	// スロットを先に確保する
	// 以降このvectorはリサイズされないので
	// 別インデックスへの書き込みは並列でも安全
	m_resourceList.assign(m_pending.size(), nullptr);

	return m_pending.size();
}

// ----------------------------------------------------------
// ワーカースレッド可
// スロット単位でモデルを読み込む
// m_pending は読むだけ、m_resourceList は自分のスロットにしか書かない
// ----------------------------------------------------------
bool ModelResourceManager::LoadSlot(size_t _slotIndex)
{
	if (_slotIndex >= m_pending.size()) { return false; }

	const PendingModel& p = m_pending[_slotIndex];

	auto modelData = std::make_shared<KdModelData>();
	if (!modelData->Load(p.filePath))
	{
		// 失敗しても止めない
		// 1つ欠けただけで起動しないと開発中につらいので
		// nullptrのまま残し、Commitで拾って報告する
		return false;
	}

	m_resourceList[p.id] = std::move(modelData);
	return true;
}

// ----------------------------------------------------------
// メインスレッド
// 名前引き用のマップを埋める
// ----------------------------------------------------------
void ModelResourceManager::Commit()
{
	m_resources.reserve(m_pending.size());

	for (const PendingModel& p : m_pending)
	{
		const std::shared_ptr<KdModelData>& data = m_resourceList[p.id];
		if (!data)
		{
			// 読めなかったものは名前でもIDでも引けないようにしておく。
			// IDだけ残っていると GetModelById が nullptr を返して、使う側で気づきにくい
			m_keyToId.erase(p.key);
			m_failedKeys.push_back(p.key);
			continue;
		}
		m_resources[p.key] = data;
	}

	m_pending.clear();
	m_pending.shrink_to_fit();

	ResourceList::ReportFailures(kLogTag, m_failedKeys);
	assert(m_failedKeys.empty() && U8("モデルの読み込みに失敗しました"));
}

// ----------------------------------------------------------
const std::string& ModelResourceManager::GetPendingKey(size_t _slotIndex) const
{
	static const std::string empty;
	return (_slotIndex < m_pending.size()) ? m_pending[_slotIndex].key : empty;
}

std::shared_ptr<KdModelData> ModelResourceManager::GetModel(const std::string& _key) const
{
	const auto it = m_resources.find(_key);
	if (it != m_resources.end()) { return it->second; }

	assert(0 && U8("モデルの存在を確認できませんでした"));
	return nullptr;
}

// 無いときは assert せず nullptr(前と同じ)。呼ぶ側で null チェックすること
std::shared_ptr<KdModelWork> ModelResourceManager::GetWork(const std::string& _key) const
{
	const auto it = m_resources.find(_key);
	if (it == m_resources.end()) { return nullptr; }

	auto spWork = std::make_shared<KdModelWork>();
	spWork->SetModelData(it->second);
	return spWork;
}

ModelId ModelResourceManager::GetModelId(const std::string& _key) const
{
	const auto it = m_keyToId.find(_key);
	if (it != m_keyToId.end()) { return it->second; }

	assert(0 && U8("モデルIDが見つかりませんでした"));
	return kInvalidModelId;
}
