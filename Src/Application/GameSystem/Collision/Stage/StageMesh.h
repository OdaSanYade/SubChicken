#pragma once
// =====================================================================
//  StageMesh.h  (slim / large-stage safe)
//  一様グリッド + Amanatides-Woo DDA レイキャスト + カプセルCCD。
//   ・セルサイズは m_cellSize に一本化（固定 kCellSize は廃止）
//   ・グリッド解像度は「軸上限」ではなく「総セル数の予算」で決める
//   ・広範囲にまたがる三角形は oversized リストへ退避して常時テスト
//   ・セル配列は CSR（start配列＋item配列）でメモリ効率を確保
// =====================================================================
#include "../Const/CollisionTypes.h"

class StageMesh
{
public:
	static constexpr float  kBaseCellSize = 3.0f;              // 基準セルサイズ
	static constexpr int    kMaxAxisCells = 1024;              // 1軸あたりの上限
	static constexpr size_t kMaxTotalCells = 4u * 1024u * 1024u;// 総セル数の上限(≒16MB)
	static constexpr long long kMaxTriCellSpan = 512;             // これを超える三角形は oversized 行き
	static constexpr bool   kBackfaceCulling = true;
	static constexpr float kSphereCastProbe = 2.0f;

	// tris と並列の tags（省略時は全0）
	void Build(std::vector<PrecomputedTri>&& _tris,
		std::vector<uint32_t>&& _tags = {});

	bool IsBuilt() const { return m_nx > 0; }
	const std::vector<PrecomputedTri>& Tris() const { return m_tris; }

	// ---- グリッド情報（デバッグ描画用）----
	float                CellSize()   const { return m_cellSize; }
	const Math::Vector3& GridOrigin() const { return m_origin; }
	void GridDimensions(int& _x, int& _y, int& _z) const { _x = m_nx; _y = m_ny; _z = m_nz; }
	uint32_t OversizedTriangleCount() const { return (uint32_t)m_oversized.size(); }

	// ---- レイキャスト（グリッドDDA）----
	Collision::HitResult Raycast(
		const Math::Vector3& start,
		const Math::Vector3& end,
		float epsilon = 1e-4f) const;

	// 指定AABBに重なるセル群から、ユニークな三角形インデックスを収集
	void GatherTriangles(const DirectX::BoundingBox& queryAABB,
		std::vector<uint32_t>& outIndices) const;

	// ---- カプセルCCD ----
	void GatherCapsulePathTriangles(
		const Math::Vector3& center, float segHalf, float radius,
		const Math::Vector3& displacement,
		const Collision::CapsuleMoveConfig& cfg,
		std::vector<uint32_t>& out) const;

	Collision::CapsuleMoveResult MoveCapsuleTris(
		const std::vector<uint32_t>& tris,
		const Math::Vector3& center, float segHalf, float radius,
		const Math::Vector3& displacement,
		float slopeLimit, int maxSubsteps) const;

	Collision::CapsuleMoveResult MoveCapsuleWithStep(
		const std::vector<uint32_t>& tris,
		const Math::Vector3& center, float segHalf, float radius,
		const Math::Vector3& displacement,
		const Collision::CapsuleMoveConfig& cfg) const;

	Collision::CapsuleMoveResult MoveCapsule(
		const Math::Vector3& center, float segHalf, float radius,
		const Math::Vector3& displacement,
		float slopeLimit = 0.6f,
		int   maxSubsteps = 16) const;

	Collision::HitResult SphereCast(
		const Math::Vector3& start, const Math::Vector3& end, float radius,
		float epsilon = 0.02f, int maxIterations = 48) const;

private:
	struct CapsuleContact { bool hit = false; Math::Vector3 normal; float depth = 0.f; uint32_t tag = 0; };

	CapsuleContact DeepestCapsuleContact(
		const std::vector<uint32_t>& tris,
		const Math::Vector3& p, const Math::Vector3& q, float radius) const;

	float ClosestTriangleDistance(const std::vector<uint32_t>& tris, const Math::Vector3& p,
		float searchRadius, Math::Vector3& outNormal, uint32_t& outTag) const;

	// AABB → セル範囲（[x0,y0,z0,x1,y1,z1]）。必ず m_cellSize を使う。
	void CellRange(const Math::Vector3& mn, const Math::Vector3& mx, int outRange[6]) const;

	// スタンプ更新（0回りのラップ処理込み）
	uint32_t NextStamp() const;

	size_t FlatIndex(int x, int y, int z) const
	{
		return (size_t)x + (size_t)m_nx * ((size_t)y + (size_t)m_ny * (size_t)z);
	}
	bool Valid(int x, int y, int z) const
	{
		return x >= 0 && x < m_nx && y >= 0 && y < m_ny && z >= 0 && z < m_nz;
	}

	Math::Vector3 m_origin;
	int           m_nx = 0, m_ny = 0, m_nz = 0;
	float         m_cellSize = kBaseCellSize;

	std::vector<PrecomputedTri> m_tris;
	std::vector<uint32_t>       m_triTag;

	// CSR グリッド
	std::vector<uint32_t> m_cellStart;  // サイズ nCells+1
	std::vector<uint32_t> m_cellItems;  // 三角形インデックス
	std::vector<uint32_t> m_oversized;  // セルに入れない巨大三角形

	mutable uint32_t              m_stamp = 0;
	mutable std::vector<uint32_t> m_triStamp;
	mutable std::vector<uint32_t> m_castScratch; // SphereCast用（単スレッド前提）
};