#pragma once
// =====================================================================
//  CompPool.h
//  世代付き密ストレージ。
//   - Data() は常に密（先頭から走査すればキャッシュフレンドリー）
//   - Handle は他要素の破棄（swap-pop）を跨いでも有効
//   - スロット番号は不変、実データ位置だけ swap-pop で動く二段構え
// =====================================================================
#include "CollisionTypes.h"
#include <vector>
#include <utility>

namespace Collision
{
	template<class T>
	class CompPool
	{
	public:
		Handle Create(const T& init = T{})
		{
			uint32_t slot;
			if (!m_free.empty()) { slot = m_free.back(); m_free.pop_back(); }
			else { slot = (uint32_t)m_slots.size(); m_slots.push_back({}); }

			const uint32_t dataIdx = (uint32_t)m_data.size();
			m_data.push_back(init);
			m_dataToSlot.push_back(slot);

			m_slots[slot].dataIdx = dataIdx;
			m_slots[slot].alive = true;
			return { slot, m_slots[slot].gen };
		}

		void Destroy(Handle h)
		{
			if (!Alive(h)) return;
			const uint32_t slot = h.index;
			const uint32_t dataIdx = m_slots[slot].dataIdx;
			const uint32_t lastIdx = (uint32_t)m_data.size() - 1;

			// 末尾を dataIdx へ swap-pop（密を維持）
			if (dataIdx != lastIdx)
			{
				m_data[dataIdx] = std::move(m_data[lastIdx]);
				const uint32_t moved = m_dataToSlot[lastIdx];
				m_dataToSlot[dataIdx] = moved;
				m_slots[moved].dataIdx = dataIdx;
			}
			m_data.pop_back();
			m_dataToSlot.pop_back();

			m_slots[slot].alive = false;
			++m_slots[slot].gen;          // 以後、古い Handle は無効
			m_free.push_back(slot);
		}

		bool Alive(Handle h) const
		{
			return h.index < m_slots.size()
				&& m_slots[h.index].alive
				&& m_slots[h.index].gen == h.gen;
		}

		T* Get(Handle h)
		{
			if (!Alive(h)) return nullptr;
			return &m_data[m_slots[h.index].dataIdx];
		}
		const T* Get(Handle h) const
		{
			if (!Alive(h)) return nullptr;
			return &m_data[m_slots[h.index].dataIdx];
		}

		// 密配列上の位置 → 安定ハンドル
		Handle HandleAt(uint32_t dataIdx) const
		{
			const uint32_t slot = m_dataToSlot[dataIdx];
			return { slot, m_slots[slot].gen };
		}

		std::vector<T>& Data() { return m_data; }
		const std::vector<T>& Data() const { return m_data; }
		uint32_t Size() const { return (uint32_t)m_data.size(); }

		void Reserve(uint32_t n)
		{
			m_data.reserve(n); m_dataToSlot.reserve(n); m_slots.reserve(n);
		}
		void Clear()
		{
			m_data.clear(); m_dataToSlot.clear(); m_slots.clear(); m_free.clear();
		}

	private:
		struct Slot { uint32_t dataIdx = 0; uint32_t gen = 0; bool alive = false; };
		std::vector<T>        m_data;        // 密。更新はここを頭から
		std::vector<uint32_t> m_dataToSlot;  // data位置 → スロット番号
		std::vector<Slot>     m_slots;       // スロット番号 → data位置＋世代
		std::vector<uint32_t> m_free;        // 空きスロット
	};
}
