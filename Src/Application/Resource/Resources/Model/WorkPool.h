#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "ModelManager.h"	// ModelId

using WorkId = uint32_t;
inline constexpr WorkId kInvalidWorkId = static_cast<WorkId>(-1);

//==========================================================
// WorkPool
// KdModelWork をスロットで使い回すプール。
// 外には WorkId + generation だけを渡し、ポインタは持たせない。
// 破棄されたスロットは generation が進むので、古いIDで引いても nullptr が返る。
//
// ※メインスレッド専用(排他はしていない)
//==========================================================
class WorkPool
{
public:

	//新規Work確保、IDを返す
	//モデルデータが null のときは作らず kInvalidWorkId を返す
	WorkId Create(const std::shared_ptr<KdModelData>& _modelData,
		ModelId srcModelId = kInvalidModelId)
	{
		if (!_modelData)
		{
			assert(false && "WorkPool: モデルデータが null です");
			return kInvalidWorkId;
		}

		WorkId id;
		if (!m_freeList.empty())
		{
			id = m_freeList.back();
			m_freeList.pop_back();
		}
		else
		{
			id = static_cast<WorkId>(m_slots.size());
			m_slots.emplace_back();
		}

		auto& slot = m_slots[id];
		slot.work = std::make_shared<KdModelWork>();
		slot.work->SetModelData(_modelData);
		slot.alive = true;
		slot.srcModelId = srcModelId;
		return id;
	}

	ModelId GetSrcModelId(WorkId id) const
	{
		return (id < m_slots.size()) ? m_slots[id].srcModelId : kInvalidModelId;
	}

	//------------------------------------------------------
	// 破棄(generationを進めて古いIDを無効化)
	// 既に破棄済みのスロットは何もしない。
	// 前はここを見ていなかったので、2回呼ぶと同じIDがフリーリストに2つ入り、
	// 次の Create 2回で同じスロットを2人が使う状態になっていた
	//------------------------------------------------------
	bool Destroy(WorkId id)
	{
		if (!IsAliveSlot(id)) { return false; }

		auto& slot = m_slots[id];
		slot.work.reset();
		slot.alive = false;
		slot.srcModelId = kInvalidModelId;
		++slot.generation;
		m_freeList.push_back(id);
		return true;
	}

	// generation も照合する版。
	// 古いIDを握ったままの誰かが、使い回された別のWorkを消してしまうのを防げるのでこっちを推奨
	bool Destroy(WorkId id, uint32_t generation)
	{
		if (!IsValid(id, generation)) { return false; }
		return Destroy(id);
	}

	// ID と generation が今も有効か
	bool IsValid(WorkId id, uint32_t generation) const
	{
		return IsAliveSlot(id) && m_slots[id].generation == generation;
	}

	KdModelWork* Get(WorkId id, uint32_t generation)
	{
		return IsValid(id, generation) ? m_slots[id].work.get() : nullptr;
	}

	const KdModelWork* Get(WorkId id, uint32_t generation) const
	{
		return IsValid(id, generation) ? m_slots[id].work.get() : nullptr;
	}

	std::shared_ptr<KdModelWork> GetShared(WorkId id, uint32_t generation)
	{
		return IsValid(id, generation) ? m_slots[id].work : nullptr;
	}

	uint32_t GetGeneration(WorkId id) const
	{
		return (id < m_slots.size()) ? m_slots[id].generation : 0;
	}

private:

	bool IsAliveSlot(WorkId id) const
	{
		return id < m_slots.size() && m_slots[id].alive;
	}

	struct Slot
	{
		std::shared_ptr<KdModelWork> work;
		uint32_t generation = 0;	// 一周しても(約42億回の破棄)実用上は問題ない
		bool alive = false;
		ModelId srcModelId = kInvalidModelId;
	};

	std::vector<Slot> m_slots;
	std::vector<WorkId> m_freeList;
};
