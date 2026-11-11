#pragma once
#include "../IObject.h"

//==========================================================
// IChara
// 動いて殴り合うもの(プレイヤー・敵・ひよこ)の共通部分。
// 体力・攻撃判定のハンドル・重力の状態を持つ。
//==========================================================
class IChara : public IObject
{
public:

	IChara(kd::ecs::ComponentManager& cm, const std::shared_ptr<CollisionWorld>& world)
		:IObject(cm, world)
	{
		// 体力は派生側の Init で ResetHealth() して上書きする前提の仮の値
		m_cm.add<Game::Health>(m_entity, { kDefaultHp });
	}

	~IChara() override
	{
		if (auto sp = m_wpWorld.lock())
			sp->DestroyCombat(m_combatHandle);
	}

	virtual std::string GetID()override { return "Chara"; }

	void StartAttack() { m_currentAttackId = m_nextAttackId++; }
	void ChangeMaterial(uint32_t _materialId) { m_cm.get<RenderComponent>(m_entity).materialId.value = _materialId; }

	// ---- 常時発動のエフェクトの管理 ----
	void SetEffectID(int id) { m_effectID = id; }
	int  GetEffectID() const { return m_effectID; }

protected:

	// 体力を入れ直す。Health はコンストラクタで追加済みなので add ではなく上書きする。
	// (add をもう一度呼ぶと、ECS によっては二重追加で止まる)
	void ResetHealth(float _hp)
	{
		auto& hc = m_cm.get<Game::Health>(m_entity);
		hc.hp = _hp;
		hc.maxHp = _hp;
	}

	// 派生側が何も設定しなかったときの体力
	static constexpr float kDefaultHp = 300.0f;

	//攻撃用
	Collision::Handle m_combatHandle;
	uint32_t m_nextAttackId = 1;
	uint32_t m_currentAttackId = 0;

	Math::Vector3 m_dir   = Math::Vector3::Zero;
	Math::Vector3 m_move = Math::Vector3::Zero;

	// 重力。毎フレーム m_gravity に m_gravityPow を掛けて加速し、m_gravityMax で止める
	float m_gravity = 1.0f;
	float m_gravityPow = 1.2f;
	float m_gravityMax = 50.0f;
	bool m_isGrounded = false;

	float m_jumpMoment = 0.0f;
	float m_speed    = 1.0f;
	bool  m_isActive = true;

	// ---- エフェクトID ----
	int m_effectID = -1;
};
