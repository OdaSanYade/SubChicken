#pragma once
// =====================================================================
//  CollisionTypes.h
//  フレームワーク非依存の衝突型（Math::Vector3 / DirectX::BoundingBox のみ依存）
//  ※ プロジェクトの Pch（SimpleMath 等）が先に include される前提。
// =====================================================================
#include <cstdint>
#include <vector>

namespace Collision
{
	// ------------------------------------------------------------------
	//  世代付きハンドル（swap-pop されても安定して指せる）
	// ------------------------------------------------------------------
	struct Handle
	{
		uint32_t index = UINT32_MAX;
		uint32_t gen = 0;
		bool IsValid() const { return index != UINT32_MAX; }
		bool operator==(const Handle& o) const { return index == o.index && gen == o.gen; }
		bool operator!=(const Handle& o) const { return !(*this == o); }
	};

	// ------------------------------------------------------------------
	//  オブジェクト種別（排他・32未満に収める＝マスク用）
	// ------------------------------------------------------------------
	enum class ObjectType : uint32_t
	{
		None = 0,
		Player,
		Enemy,
		EnemyAttack,
		PlayerAttack,
		Item,
		Camera,
		Gimmick,

		Count
	};
	inline uint32_t TypeBit(ObjectType t) { return 1u << static_cast<uint32_t>(t); }

	// ------------------------------------------------------------------
	//  面・オブジェクトの性質（複合ビットマスク）
	// ------------------------------------------------------------------
	enum SurfaceTag : uint32_t
	{
		Tag_None = 0,
		Tag_Ground = 1u << 0,
		Tag_Wall = 1u << 1,
		Tag_Ice = 1u << 2,   // 滑る
		Tag_Mud = 1u << 3,   // 減速
		Tag_Damage = 1u << 4,   // ダメージ床
		Tag_Bounce = 1u << 5,
		Tag_NoStep = 1u << 6,   // 段差登り禁止
		Tag_Death = 1u << 7,   // 即死
		// ↓ 1u<<8 以降はゲーム側で自由に
	};

	// ------------------------------------------------------------------
	//  カプセルの基準点（center フィールドが実際にどこを指すか）
	// ------------------------------------------------------------------
	enum class CapsulePivot : uint8_t { Top, Center, Bottom };

	// ------------------------------------------------------------------
	//  形状
	// ------------------------------------------------------------------
	enum class ShapeKind : uint8_t { Capsule, AABB };

	struct Shape
	{
		ShapeKind     kind = ShapeKind::Capsule;
		float         radius = 0.5f;   // Capsule（segHalf=0 なら球）
		float         segHalf = 0.5f;   // Capsule スパイン半長 = 全高/2 - radius
		Math::Vector3 halfExtents{ 0.5f, 0.5f, 0.5f }; // AABB
		CapsulePivot  pivot = CapsulePivot::Center;    // center が Top/Center/Bottom のどれを指すか（Capsuleのみ有効）

		static Shape MakeCapsule(float _radius, float _height, CapsulePivot _pivot = CapsulePivot::Center)
		{
			Shape s; s.kind = ShapeKind::Capsule; s.radius = _radius;
			s.segHalf = (_height * 0.5f - _radius) > 0.f ? (_height * 0.5f - _radius) : 0.f;
			s.pivot = _pivot;
			return s;
		}
		static Shape MakeAABB(const Math::Vector3& _halfExt)
		{
			Shape s; s.kind = ShapeKind::AABB; s.halfExtents = _halfExt; return s;
		}
	};

	// ------------------------------------------------------------------
	//  center（pivot基準の位置）⇔ mid（カプセル形状としての真の中心）の相互変換
	//   ・Top    : center = カプセル最上点（頭）
	//   ・Center : center = カプセル中心（従来通り。変換なし）
	//   ・Bottom : center = カプセル最下点（足元）
	// ------------------------------------------------------------------
	inline Math::Vector3 CapsulePivotToMid(const Math::Vector3& pivotPos,
		float segHalf, float radius, CapsulePivot pivot)
	{
		const float offset = segHalf + radius;
		switch (pivot)
		{
		case CapsulePivot::Top:    return pivotPos - Math::Vector3(0, offset, 0);
		case CapsulePivot::Bottom: return pivotPos + Math::Vector3(0, offset, 0);
		default:                   return pivotPos; // Center
		}
	}
	inline Math::Vector3 CapsuleMidToPivot(const Math::Vector3& mid,
		float segHalf, float radius, CapsulePivot pivot)
	{
		const float offset = segHalf + radius;
		switch (pivot)
		{
		case CapsulePivot::Top:    return mid + Math::Vector3(0, offset, 0);
		case CapsulePivot::Bottom: return mid - Math::Vector3(0, offset, 0);
		default:                   return mid; // Center
		}
	}

	// ------------------------------------------------------------------
	//  レイ / 重なり結果
	// ------------------------------------------------------------------
	struct HitResult
	{
		bool          isHit = false;
		float         distance = 0.0f;
		Math::Vector3 hitpos;
		Math::Vector3 normal;
		uint32_t      tag = 0;
		ObjectType    type = ObjectType::None;
	};

	// ------------------------------------------------------------------
	//  カプセル連続移動（CCD）
	// ------------------------------------------------------------------
	struct CapsuleMoveConfig
	{
		float slopeLimit = 0.7071f; // normal.y >= これ → 歩ける床（cos45°）
		float stepHeight = 0.4f;    // 登れる段差
		float snapDistance = 0.35f; // 下り坂/小段差の張り付き探査距離
		bool  wasGrounded = false;  // 前フレーム接地（下りスナップ発動条件）
		int   maxSubsteps = 16;
	};

	struct CapsuleMoveResult
	{
		Math::Vector3 position;              // 解決後のカプセル中心
		bool          grounded = false;
		Math::Vector3 groundNormal;
		bool          hitWall = false;
		Math::Vector3 wallNormal;
		bool          hitCeiling = false;
		uint32_t      groundTag = 0;
		uint32_t      wallTag = 0;
		uint32_t      ceilingTag = 0;

		// オブジェクトヒット（最深1件を集約）
		bool          hitObject = false;
		Math::Vector3 objectNormal;
		uint32_t      objectTag = 0;
		ObjectType    objectType = ObjectType::None;
	};

	// ------------------------------------------------------------------
	//  戦闘（ヒット/ハートボックス）
	// ------------------------------------------------------------------
	enum class Team : uint8_t { Neutral, Player, Enemy };

	struct HitVolume { Math::Vector3 p0, p1; float radius = 0.5f; }; // p0==p1 で球

	struct HitData
	{
		float         damage = 0.f;
		Math::Vector3 knockback;                 // 方向×強さ（ワールド）
		int           hitstopFrames = 0;
		uint32_t      attackId = 0;              // スイング識別（多段ヒット防止）
		uint32_t      tag = 0;                   // 属性（炎/氷/打ち上げ等）
		ObjectType    attackerType = ObjectType::None;
	};

	// ------------------------------------------------------------------
	//  プル型イベント（オブジェクトが毎フレーム読み取る）
	// ------------------------------------------------------------------
	struct ObjectHit
	{
		Handle        other;
		ObjectType    otherType = ObjectType::None;
		uint32_t      otherTag = 0;
		Math::Vector3 normal;   // self→other の押し出し軸（トリガーは概算 or ゼロ）
	};
}


struct PrecomputedTri
{
	Math::Vector3 v0;
	Math::Vector3 e0;     // v1 - v0
	Math::Vector3 e1;     // v2 - v0
	Math::Vector3 normal; // 正規化済み表面法線
	Math::Vector3 bMin;   // 三角形AABB最小
	Math::Vector3 bMax;   // 三角形AABB最大
};