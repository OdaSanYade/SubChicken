#include "TextureManager.h"
#include "../ResourceListLoader.h"

namespace
{
	constexpr const char* kLogTag = "TextureManager";
}

// ----------------------------------------------------------
// 一括初期化(同期)
// ----------------------------------------------------------
void TextureManager::Init()
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
// JSONを読み、スロットを確保する
// 戻り値はロード対象の件数
// ----------------------------------------------------------
size_t TextureManager::Prepare()
{
	m_pending.clear();
	m_failedKeys.clear();
	m_texture.clear();
	m_loaded.clear();

	std::vector<ResourceList::Entry> entries;
	if (!ResourceList::Load(m_listAddressable, entries, m_failedKeys))
	{
		OutputDebugStringA(("[TextureManager] リストが読めません : " + m_listAddressable + "\n").c_str());
		assert(0 && U8("画像のファイルリストを読み込めませんでした"));
		return 0;
	}

	m_pending.reserve(entries.size());
	for (auto& entry : entries)
	{
		PendingTexture p;
		p.slot = m_pending.size();
		p.key = std::move(entry.key);
		p.filePath = std::move(entry.filePath);

		m_pending.push_back(std::move(p));
	}

	// スロットを先に確保する
	// 以降このvectorはリサイズされないので
	// 別インデックスへの書き込みは並列でも安全
	m_loaded.assign(m_pending.size(), nullptr);

	return m_pending.size();
}

// ----------------------------------------------------------
// スロット単位で1枚読む
// ----------------------------------------------------------
bool TextureManager::LoadSlot(size_t _slotIndex)
{
	if (_slotIndex >= m_pending.size()) { return false; }

	const PendingTexture& p = m_pending[_slotIndex];

	auto texData = std::make_shared<KdTexture>();

	// 失敗した空テクスチャが混ざると描画時まで気づけないので戻り値は必ず見る
	if (!texData->Load(p.filePath))
	{
		return false;
	}

	m_loaded[p.slot] = std::move(texData);
	return true;
}

// ----------------------------------------------------------
// メインスレッド
// 名前引き用のマップを埋める
// ----------------------------------------------------------
void TextureManager::Commit()
{
	m_texture.reserve(m_pending.size());

	for (const PendingTexture& p : m_pending)
	{
		const std::shared_ptr<KdTexture>& tex = m_loaded[p.slot];
		if (!tex)
		{
			m_failedKeys.push_back(p.key);
			continue;
		}
		m_texture[p.key] = tex;
	}

	m_pending.clear();
	m_pending.shrink_to_fit();

	ResourceList::ReportFailures(kLogTag, m_failedKeys);
	assert(m_failedKeys.empty() && U8("画像の読み込みに失敗しました"));
}

// ----------------------------------------------------------
const std::string& TextureManager::GetPendingKey(size_t _slotIndex) const
{
	static const std::string empty;
	return (_slotIndex < m_pending.size()) ? m_pending[_slotIndex].key : empty;
}

std::shared_ptr<KdTexture> TextureManager::GetTex(const std::string& _key) const
{
	const auto it = m_texture.find(_key);
	if (it != m_texture.end()) { return it->second; }

	assert(0 && U8("画像の存在を確認できませんでした"));
	return nullptr;
}
