#pragma once
#include "IObject.h"

//==========================================================
// ObjectManager
// シーンに置いたオブジェクトを ID で持ち、まとめて更新する。
//
// 更新の途中で Register / 削除が呼ばれても map を壊さないよう、
// 回している最中は登録・削除を待たせて、回し終わってからまとめて反映する。
//==========================================================
class ObjectManager
{
public:

	//オブジェクト登録
	void Register(const std::shared_ptr<IObject>& _obj)
	{
		if (!_obj) return;

		const std::string id = _obj->GetID();

		// 更新中なら map へ入れるのだけを待たせる。Init はどちらでもすぐ呼ぶ
		if (m_iterating > 0) m_pendingAdd.push_back(_obj);
		else                 InsertObject(id, _obj);

		Game::SetEntityName(_obj->GetComponentManager(), _obj->GetEntity(), id);

		_obj->Init();
	}

	//削除待ちオブジェクトを溜める
	void RequestUnRegister(const std::string& id)
	{
		m_pendingRemove.push_back(id);
	}

	//全オブジェクト解除
	void UnRegister()
	{
		for (auto& [key, obj] : m_objMap)
			obj->Release();
		m_objMap.clear();
		m_pendingAdd.clear();
		m_pendingRemove.clear();
	}

	//全オブジェクトの前更新
	void PreUpdateAll()
	{
		ForEachObject([](IObject& obj) { obj.PreUpdate(); });
	}

	//一部オブジェクトの更新処理
	void UpdateId(const std::vector<std::string>& _ids)
	{
		IterationScope scope(*this);
		for (const auto& id : _ids)
		{
			const auto it = m_objMap.find(id);
			if (it != m_objMap.end()) it->second->Update();
		}
	}

	//全オブジェクトの更新処理
	void UpdateAll()
	{
		ForEachObject([](IObject& obj) { obj.Update(); });
	}

	//全オブジェクトの後更新
	void PostUpdateAll()
	{
		ForEachObject([](IObject& obj) { obj.PostUpdate(); });

		//削除待ちオブジェクト解放
		FlushPendingRemove();
	}

	//他オブジェクトの情報を取得
	void SetWeak(const std::string& _id, const std::shared_ptr<IObject>& _obj, const std::string& _name)
	{
		const auto it = m_objMap.find(_id);
		if (it != m_objMap.end())
			it->second->SetWeak(_name, _obj);
	}

	//------------------------------------------------------
	// オブジェクトを weak_ptr で取得
	// 型が違うときは空を返す。前は static_pointer_cast だったので、
	// ID と型の組み合わせを間違えると壊れたポインタが返っていた
	//------------------------------------------------------
	template <typename T>
	std::weak_ptr<T> GetWeak(const std::string& _id)
	{
		const auto it = m_objMap.find(_id);
		if (it == m_objMap.end()) return std::weak_ptr<T>();

		auto casted = std::dynamic_pointer_cast<T>(it->second);
		assert(casted && "ObjectManager::GetWeak : 指定した型と登録されている型が違います");
		return casted;
	}

private:

	// 回している間だけカウントを上げる。入れ子になっても大丈夫なように数で持つ
	struct IterationScope
	{
		explicit IterationScope(ObjectManager& _owner) : owner(_owner) { ++owner.m_iterating; }
		~IterationScope()
		{
			--owner.m_iterating;
			if (owner.m_iterating == 0) owner.FlushPendingAdd();
		}
		IterationScope(const IterationScope&) = delete;
		IterationScope& operator=(const IterationScope&) = delete;

		ObjectManager& owner;
	};

	template<class Fn>
	void ForEachObject(Fn&& _fn)
	{
		IterationScope scope(*this);
		for (auto& [key, obj] : m_objMap) _fn(*obj);
	}

	// 同じ ID があれば入れ替える
	void InsertObject(const std::string& _id, const std::shared_ptr<IObject>& _obj)
	{
		if (m_objMap.contains(_id)) UnRegister(_id);
		m_objMap[_id] = _obj;
	}

	void FlushPendingAdd()
	{
		// InsertObject の中で Register が呼ばれることは無いが、念のため写してから回す
		auto pending = std::move(m_pendingAdd);
		m_pendingAdd.clear();
		for (const auto& obj : pending) InsertObject(obj->GetID(), obj);
	}

	//削除待ちオブジェクトを一斉削除
	void FlushPendingRemove()
	{
		auto pending = std::move(m_pendingRemove);
		m_pendingRemove.clear();
		for (const auto& id : pending) UnRegister(id);
	}

	//指定オブジェクト解除
	void UnRegister(const std::string& id)
	{
		const auto it = m_objMap.find(id);
		if (it != m_objMap.end())
		{
			it->second->Release();
			m_objMap.erase(it);
		}
	}

	std::unordered_map<std::string, std::shared_ptr<IObject>> m_objMap;
	std::vector<std::string> m_pendingRemove;
	std::vector<std::shared_ptr<IObject>> m_pendingAdd;
	int m_iterating = 0;

private:
	ObjectManager() = default;
	~ObjectManager() = default;

	ObjectManager(const ObjectManager&) = delete;
	ObjectManager& operator = (const ObjectManager&) = delete;
public:
	static ObjectManager& Instance()
	{
		static ObjectManager instance;
		return instance;
	}
};
