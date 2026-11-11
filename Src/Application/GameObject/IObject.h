#pragma once

#include "../GameSystem/Collision/CollisionWorld.h"

class IObject : public std::enable_shared_from_this<IObject>
{
public:

	IObject(kd::ecs::ComponentManager& cm, const std::shared_ptr<CollisionWorld>& world) : m_cm(cm), m_wpWorld(world)
	{
		m_entity = cm.createEntity();
		cm.add<TransformComponent>(m_entity, {});
	}

	virtual ~IObject()
	{
		// Release() 済みならエンティティはもう消してある。
		DestroyEntityOnce();

		if (auto sp = m_wpWorld.lock())
			sp->DestroyCollision(m_handle);
	}

	virtual void Init() {}

	virtual void PreUpdate() {}
	virtual void Update() {}
	virtual void PostUpdate() {}

	// エンティティを先に片付ける。何度呼んでもいい
	void Release()
	{
		DestroyEntityOnce();
	}

	virtual std::string GetID() = 0;

	void SetWeak(const std::string& _name, std::weak_ptr<IObject> _obj)
	{
		// 先に登録したほうを優先する(上書きしない)
		if (m_wpObjectMap.contains(_name)) return;
		m_wpObjectMap[_name] = _obj;
	}

	kd::ecs::Entity GetEntity() const { return m_entity; }
	kd::ecs::ComponentManager& GetComponentManager() { return m_cm; }

protected:

	//------------------------------------------------------
	// 自分より長生きする相手(入力システムなど)に渡すコールバックを作る。
	// 呼ばれた時点で自分が消えていれば何もしない。
	//
	//   input.AddListener("Attack", KdInputEventType::Press,
	//       MakeSafeCallback<Player>([](Player& self) { self.SetFlag(...); }));
	//
	// [this] をそのまま渡すと、シーンを抜けた後にボタンを押しただけで
	// 消えたオブジェクトを触ってしまう。
	//------------------------------------------------------
	template<class Self, class Fn>
	std::function<void()> MakeSafeCallback(Fn _fn)
	{
		std::weak_ptr<IObject> weak = weak_from_this();

		// shared_ptr で持たれていない(make_shared していない)と weak が作れない。
		// その場合は気づけるように止めて、動作だけは前と同じ生ポインタ版にする
		if (weak.expired())
		{
			assert(false && "IObject: shared_ptr で管理されていないので安全なコールバックを作れません");
			Self* self = static_cast<Self*>(this);
			return [self, fn = std::move(_fn)]() { fn(*self); };
		}

		return [weak, fn = std::move(_fn)]()
			{
				if (auto self = weak.lock()) fn(static_cast<Self&>(*self));
			};
	}

	std::unordered_map<std::string, std::weak_ptr<IObject>> m_wpObjectMap;

	//コンポーネント
	kd::ecs::Entity m_entity;
	kd::ecs::ComponentManager& m_cm;

	//コリジョン
	std::weak_ptr<CollisionWorld> m_wpWorld;
	Collision::Handle             m_handle;

private:

	void DestroyEntityOnce()
	{
		if (m_isEntityDestroyed) return;
		m_isEntityDestroyed = true;
		m_cm.destroyEntity(m_entity);
	}

	bool m_isEntityDestroyed = false;
};
