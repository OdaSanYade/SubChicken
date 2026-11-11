#pragma once
// =====================================================================
//  CollisionComponent.h
//  純データの衝突コンポーネント。CollisionWorld が密に所有する。
//   config : 生成時に一度設定
//   input  : オーナーが毎フレーム書く（center / moveVector）
//   output : オーナーが毎フレーム読む（result / pushAccum / objectHits）
// =====================================================================
#include "../Const/CollisionTypes.h"

namespace Collision
{
	struct CollisionComponent
	{
		// ---- 静的設定 ----
		Shape             shape;
		ObjectType        type = ObjectType::None;
		uint32_t          tag = 0;
		uint32_t          typeMask = 0xFFFFFFFFu; // 反応したい相手タイプ集合
		bool              enabled = true;
		bool              isStatic = false;        // 動かない（プロップ等）
		bool              blocksCapsule = false;   // カプセルと物理的に押し合う
		float             inverseMass = 0.f;       // 0=不動。大きいほど軽く押される
		CapsuleMoveConfig moveConfig;

		// ---- 入力（オーナーが書く）----
		Math::Vector3     center;
		Math::Vector3     moveVector;

		// ---- 出力（オーナーが読む）----
		CapsuleMoveResult result;
		Math::Vector3     pushAccum;            // 他カプセルから押された合計
		std::vector<ObjectHit> objectHits;      // プル型イベント（毎フレームクリア）

		// AABB（ブロードフェーズ・グリッド用）
		// ※ center は shape.pivot（Top/Center/Bottom）基準の位置。AABBの中心には
		//    カプセルの真の中心（mid）を使う必要があるため変換して用いる。
		DirectX::BoundingBox GetAABB() const
		{
			DirectX::BoundingBox b;
			if (shape.kind == ShapeKind::Capsule)
			{
				b.Center = CapsulePivotToMid(center, shape.segHalf, shape.radius, shape.pivot);
				b.Extents = { shape.radius, shape.segHalf + shape.radius, shape.radius };
			}
			else
			{
				b.Center = center;
				b.Extents = shape.halfExtents;
			}
			return b;
		}

		// カプセルの真の中心（mid）。Capsule以外では center と同じ。
		Math::Vector3 GetCapsuleMid() const
		{
			return CapsulePivotToMid(center, shape.segHalf, shape.radius, shape.pivot);
		}

		bool IsCapsuleMover() const
		{
			return shape.kind == ShapeKind::Capsule && !isStatic;
		}
	};
}
