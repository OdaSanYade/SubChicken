#pragma once
// =====================================================================
//  CollisionWorld.h
//  コンポーネントを密に所有し、まとめて処理するマネージャ。
//   - CompPool<CollisionComponent> / CompPool<CombatComponent> を所有
//   - StageMesh を所有
//   - Update() で「ステージCCD → カプセル押し合い → 物体間トリガー → 戦闘」
//  オブジェクトは Handle を1個持ち、入力を書いて result / events を読む（プル型）。
// =====================================================================
#include "Const/CollisionTypes.h"
#include "Const/CompPool.h"
#include "Component/CollisionComponent.h"
#include "Component/CombatComponent.h"

class StageMesh;
class KdModelData;

class CollisionWorld
{
	friend class CollisionDebugRenderer; // デバッグ閲覧用（任意）

public:
	CollisionWorld();
	~CollisionWorld();

	// ---- ステージ ----
	// KdModelData から構築（tagResolver でノード名→性質タグを解決。省略で全0）
	void SetStage(std::shared_ptr<KdModelData> modelData, const Math::Matrix& worldMat,
		std::function<uint32_t(std::string_view)> tagResolver = nullptr);
	// フレームワーク非依存の直接構築
	void BuildStageRaw(std::vector<PrecomputedTri>&& tris,
		std::vector<uint32_t>&& tags = {});

	std::shared_ptr<StageMesh> GetStageMesh() const { return m_stage; }

	// ---- コンポーネント生成/破棄（World::Update の外で呼ぶこと）----
	Collision::Handle CreateCollision(const Collision::CollisionComponent& init);
	void              DestroyCollision(Collision::Handle h) { m_collision.Destroy(h); }
	Collision::CollisionComponent* GetCollision(Collision::Handle h) { return m_collision.Get(h); }

	Collision::Handle CreateCombat(const Collision::CombatComponent& init);
	void              DestroyCombat(Collision::Handle h) { m_combat.Destroy(h); }
	Collision::CombatComponent* GetCombat(Collision::Handle h) { return m_combat.Get(h); }

	// ---- 戦闘：アクティブ窓中に攻撃側が毎フレーム提出 ----
	void SubmitHitbox(const Collision::HitVolume& vol,
		const Collision::HitData& data,
		Collision::Team attackerTeam,
		Collision::Handle attacker = {});
	// 攻撃が今フレーム誰かに当たったか（ヒットストップ等の判定に）
	bool DidAttackLand(uint32_t attackId) const { return m_landedThisFrame.count(attackId) != 0; }

	// ---- 毎フレーム 1 回 ----
	void Update();

	// ---- クエリ ----
	Collision::HitResult RaycastStage(const Math::Vector3& start, const Math::Vector3& end) const;
	// カプセルをステージへスイープ（ワンショット。カメラ寄せ等に）
	Collision::CapsuleMoveResult SweepCapsuleStage(
		const Math::Vector3& center, float segHalf, float radius,
		const Math::Vector3& displacement) const;

	Collision::HitResult SphereCastStage(
		const Math::Vector3& start, const Math::Vector3& end, float radius) const;

	// デバッグ用スナップショット
	struct HitboxSnapshot
	{
		Collision::HitVolume vol;
		Collision::Team      team;
	};
	const std::vector<HitboxSnapshot>& GetHitboxSnapshot() const { return m_hitboxSnapshot; }


private:
	void ResolveCapsuleObjects(uint32_t selfIdx, Math::Vector3& center,
		Collision::CapsuleMoveResult& res);
	void ProcessObjectPairs();   // 非カプセル同士のトリガー
	void ProcessHits();          // 戦闘

	// 物体間ブロードフェーズ（XZセルハッシュ・毎フレ再構築）
	static constexpr float kObjectCellSize = 6.0f;
	struct GridEntry { uint64_t key; uint32_t dataIdx; };
	std::vector<GridEntry>                    m_gridEntries;
	std::vector<std::pair<uint32_t, uint32_t>> m_gridPairs;

	// 戦闘
	struct PendingHitbox
	{
		Collision::HitVolume vol;
		Collision::HitData   data;
		Collision::Team      team;
		Collision::Handle    attacker;
	};
	std::vector<PendingHitbox>     m_hitboxes;
	std::unordered_set<uint64_t>   m_hitRegistry;      // (attackId, receiverSlot) 命中済み
	std::unordered_set<uint32_t>   m_activeAttackIds;
	std::unordered_set<uint32_t>   m_landedThisFrame;

	Collision::CompPool<Collision::CollisionComponent> m_collision;
	Collision::CompPool<Collision::CombatComponent>    m_combat;

	std::shared_ptr<StageMesh>    m_stage;
	std::shared_ptr<KdModelData>  m_stageModel;
	Math::Matrix m_stageWorld;

	mutable std::vector<uint32_t> m_triCache;     // カプセル経路の三角形（1体1回・再利用）
	mutable std::vector<uint32_t> m_pushTriCache; // オブジェクト押し出し後のステージ再スイープ用（1体1回・再利用）

	std::vector<HitboxSnapshot> m_hitboxSnapshot;
};