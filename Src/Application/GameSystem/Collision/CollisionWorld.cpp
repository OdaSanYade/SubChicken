#include "CollisionWorld.h"
#include "Stage/StageMesh.h"

namespace
{
	using V3 = Math::Vector3;

	float ClosestPtSegSeg(const V3& p1, const V3& q1, const V3& p2, const V3& q2,
		V3& c1, V3& c2)
	{
		const float EPS = 1e-8f;
		V3 d1 = q1 - p1, d2 = q2 - p2, r = p1 - p2;
		float a = d1.Dot(d1), e = d2.Dot(d2), f = d2.Dot(r);
		float s, t;
		if (a <= EPS && e <= EPS) { c1 = p1; c2 = p2; return (c1 - c2).LengthSquared(); }
		if (a <= EPS) { s = 0.f; t = std::clamp(f / e, 0.f, 1.f); }
		else {
			float c = d1.Dot(r);
			if (e <= EPS) { t = 0.f; s = std::clamp(-c / a, 0.f, 1.f); }
			else {
				float b = d1.Dot(d2), denom = a * e - b * b;
				s = (denom != 0.f) ? std::clamp((b * f - c * e) / denom, 0.f, 1.f) : 0.f;
				t = (b * s + f) / e;
				if (t < 0.f) { t = 0.f; s = std::clamp(-c / a, 0.f, 1.f); }
				else if (t > 1.f) { t = 1.f; s = std::clamp((b - c) / a, 0.f, 1.f); }
			}
		}
		c1 = p1 + d1 * s; c2 = p2 + d2 * t;
		return (c1 - c2).LengthSquared();
	}

	V3 ClosestOnSeg(const V3& a, const V3& b, const V3& p)
	{
		V3 ab = b - a; float l2 = ab.LengthSquared();
		float t = (l2 > 1e-12f) ? std::clamp((p - a).Dot(ab) / l2, 0.f, 1.f) : 0.f;
		return a + ab * t;
	}
	V3 ClampBox(const V3& p, const V3& mn, const V3& mx)
	{
		return { std::clamp(p.x, mn.x, mx.x), std::clamp(p.y, mn.y, mx.y), std::clamp(p.z, mn.z, mx.z) };
	}

	// カプセル vs カプセル
	bool CapsuleVsCapsule(const V3& p1, const V3& q1, float r1,
		const V3& p2, const V3& q2, float r2, V3& n, float& depth)
	{
		V3 c1, c2; const float d2 = ClosestPtSegSeg(p1, q1, p2, q2, c1, c2);
		const float R = r1 + r2;
		if (d2 >= R * R) return false;
		const float d = std::sqrt(d2);
		n = (d > 1e-5f) ? (c1 - c2) / d : V3(0, 1, 0);
		depth = R - d;
		return true;
	}

	// カプセル vs AABB（最近接を反復近似）
	bool CapsuleVsAABB(const V3& p, const V3& q, float r,
		const V3& bc, const V3& be, V3& n, float& depth)
	{
		const V3 mn = bc - be, mx = bc + be;
		V3 sp = (p + q) * 0.5f, bp;
		for (int i = 0; i < 3; ++i) { bp = ClampBox(sp, mn, mx); sp = ClosestOnSeg(p, q, bp); }

		const V3 d = sp - bp; const float dist = d.Length();
		if (dist > 1e-5f)
		{
			if (dist >= r) return false;
			n = d / dist; depth = r - dist;
		}
		else
		{
			const float dx = std::min(sp.x - mn.x, mx.x - sp.x);
			const float dy = std::min(sp.y - mn.y, mx.y - sp.y);
			const float dz = std::min(sp.z - mn.z, mx.z - sp.z);
			if (dx <= dy && dx <= dz) { n = V3((sp.x - bc.x >= 0) ? 1.f : -1.f, 0, 0); depth = dx + r; }
			else if (dy <= dz) { n = V3(0, (sp.y - bc.y >= 0) ? 1.f : -1.f, 0); depth = dy + r; }
			else { n = V3(0, 0, (sp.z - bc.z >= 0) ? 1.f : -1.f); depth = dz + r; }
		}
		return true;
	}
}

// =====================================================================
CollisionWorld::CollisionWorld()
{
	m_stage = std::make_shared<StageMesh>();
	m_collision.Reserve(256);
	m_combat.Reserve(128);
	m_triCache.reserve(256);
}
CollisionWorld::~CollisionWorld() = default;

// =====================================================================
//  ステージ
// =====================================================================
void CollisionWorld::SetStage(std::shared_ptr<KdModelData> modelData,
	const Math::Matrix& worldMat,
	std::function<uint32_t(std::string_view)> tagResolver)
{
	m_stageModel = modelData;
	m_stageWorld = worldMat;

	if (!m_stageModel)
	{
		m_stage->Build({}, {});
		return;
	}

	std::vector<PrecomputedTri> tris;
	std::vector<uint32_t>       tags;

	const auto& dataNodes = m_stageModel->GetOriginalNodes();
	const auto& collIndices = m_stageModel->GetCollisionMeshNodeIndices();
	const auto& indices = collIndices.empty()
		? m_stageModel->GetDrawMeshNodeIndices()
		: collIndices;

	for (int idx : indices)
	{
		const auto mesh = dataNodes[idx].m_spMesh;
		if (!mesh) continue;

		// ※ ノード名フィールドはプロジェクトに合わせて調整（下は一例）
		const uint32_t nodeTag = tagResolver
			? tagResolver(std::string_view(dataNodes[idx].m_name)) : 0u;

		const Math::Matrix fullWorld = m_stageWorld;
		const auto& positions = mesh->GetVertexPositions();
		const auto& faces = mesh->GetFaces();

		tris.reserve(tris.size() + faces.size());
		tags.reserve(tags.size() + faces.size());
		for (const auto& face : faces)
		{
			PrecomputedTri pt;
			pt.v0 = Math::Vector3::Transform(positions[face.Idx[0]], fullWorld);
			const Math::Vector3 v1 = Math::Vector3::Transform(positions[face.Idx[1]], fullWorld);
			const Math::Vector3 v2 = Math::Vector3::Transform(positions[face.Idx[2]], fullWorld);

			pt.e0 = v1 - pt.v0;
			pt.e1 = v2 - pt.v0;
			pt.normal = pt.e0.Cross(pt.e1);
			pt.normal.Normalize();

			pt.bMin.x = std::min({ pt.v0.x, v1.x, v2.x });
			pt.bMin.y = std::min({ pt.v0.y, v1.y, v2.y });
			pt.bMin.z = std::min({ pt.v0.z, v1.z, v2.z });
			pt.bMax.x = std::max({ pt.v0.x, v1.x, v2.x });
			pt.bMax.y = std::max({ pt.v0.y, v1.y, v2.y });
			pt.bMax.z = std::max({ pt.v0.z, v1.z, v2.z });

			tris.push_back(pt);
			tags.push_back(nodeTag);
		}
	}

	m_stage->Build(std::move(tris), std::move(tags));
}

void CollisionWorld::BuildStageRaw(std::vector<PrecomputedTri>&& tris,
	std::vector<uint32_t>&& tags)
{
	m_stage->Build(std::move(tris), std::move(tags));
}

// =====================================================================
//  生成
// =====================================================================
Collision::Handle CollisionWorld::CreateCollision(const Collision::CollisionComponent& init)
{
	return m_collision.Create(init);
}
Collision::Handle CollisionWorld::CreateCombat(const Collision::CombatComponent& init)
{
	return m_combat.Create(init);
}

void CollisionWorld::SubmitHitbox(const Collision::HitVolume& vol,
	const Collision::HitData& data, Collision::Team team, Collision::Handle attacker)
{
	m_hitboxes.push_back({ vol, data, team, attacker });
}

// =====================================================================
//  Update
// =====================================================================
void CollisionWorld::Update()
{
	using namespace Collision;

	// 0) 毎フレームリセット
	for (auto& c : m_collision.Data()) { c.objectHits.clear(); c.pushAccum = Math::Vector3::Zero; }
	for (auto& c : m_combat.Data()) { c.received.clear(); }

	// 1) ステージCCD ＋ カプセル押し合い
	if (m_stage && m_stage->IsBuilt())
	{
		auto& comps = m_collision.Data();
		for (uint32_t i = 0; i < comps.size(); ++i)
		{
			auto& c = comps[i];
			if (!c.enabled || c.isStatic) continue;
			if (c.shape.kind != ShapeKind::Capsule) continue;

			// c.center は shape.pivot（Top/Center/Bottom）基準の位置。
			// ステージCCD・オブジェクト押し合いはカプセルの真の中心（mid）で行う。
			const Math::Vector3 mid = c.GetCapsuleMid();

			m_stage->GatherCapsulePathTriangles(
				mid, c.shape.segHalf, c.shape.radius, c.moveVector, c.moveConfig, m_triCache);

			auto res = m_stage->MoveCapsuleWithStep(
				m_triCache, mid, c.shape.segHalf, c.shape.radius, c.moveVector, c.moveConfig);
			Math::Vector3 center = res.position; // mid基準

			// オブジェクト押し合い前の位置を控えておく（押し出し量をスイープするため）
			const Math::Vector3 preObjectPos = center;
			ResolveCapsuleObjects(i, center, res);

			if (res.hitObject)
			{
				// ResolveCapsuleObjects は重なり深さぶんを center に直接加算するだけで、
				// ステージとの当たりを一切見ていない。デカいカプセルに押される等で
				// 一度の押し出し量が大きいと、そのままでは床/壁を素通りしてしまう
				// （＝地面貫通）。押し出し前→後を「移動」として扱い、
				// 改めてステージへスイープし直す。
				const Math::Vector3 pushDisp = center - preObjectPos;

				m_stage->GatherCapsulePathTriangles(
					preObjectPos, c.shape.segHalf, c.shape.radius, pushDisp, c.moveConfig, m_pushTriCache);

				auto fix = m_stage->MoveCapsuleTris(
					m_pushTriCache, preObjectPos, c.shape.segHalf, c.shape.radius,
					pushDisp, c.moveConfig.slopeLimit, c.moveConfig.maxSubsteps);
				center = fix.position;
				if (fix.grounded) { res.grounded = true; res.groundNormal = fix.groundNormal; res.groundTag = fix.groundTag; }
				if (fix.hitWall) { res.hitWall = true;  res.wallNormal = fix.wallNormal;     res.wallTag = fix.wallTag; }
			}

			// mid → pivot基準の位置に戻してから公開する（オーナーはpivot基準でcenterを読み書きする）
			res.position = CapsuleMidToPivot(center, c.shape.segHalf, c.shape.radius, c.shape.pivot);
			comps[i].result = res;
		}
	}

	// 2) 物体間トリガー
	ProcessObjectPairs();

	// 3) 戦闘
	ProcessHits();
}

void CollisionWorld::ResolveCapsuleObjects(
	uint32_t selfIdx, Math::Vector3& center, Collision::CapsuleMoveResult& res)
{
	using namespace Collision;
	auto& comps = m_collision.Data();
	auto& self = comps[selfIdx];

	const float    moverInv = std::max(self.inverseMass, 0.f);
	const uint32_t moverMask = self.typeMask;
	const uint32_t moverBit = TypeBit(self.type);
	float deepest = 0.f;

	for (uint32_t j = 0; j < comps.size(); ++j)
	{
		if (j == selfIdx) continue;
		auto& other = comps[j];
		if (!other.enabled) continue;

		DirectX::BoundingBox moverAABB;
		moverAABB.Center = center;
		moverAABB.Extents = { self.shape.radius, self.shape.segHalf + self.shape.radius, self.shape.radius };
		if (!moverAABB.Intersects(other.GetAABB())) continue;

		const bool     blocks = other.blocksCapsule;
		const uint32_t otherBit = TypeBit(other.type);
		const bool     moverCares = (moverMask & otherBit) != 0;
		const bool     otherCares = (other.typeMask & moverBit) != 0;
		if (!blocks && !moverCares && !otherCares) continue;

		// カプセルmover同士は小index側が1回だけ解決
		const bool otherCapsuleMover = other.IsCapsuleMover();
		if (otherCapsuleMover && selfIdx > j) continue;

		const V3 p = center + V3(0, self.shape.segHalf, 0);
		const V3 q = center - V3(0, self.shape.segHalf, 0);

		V3 n; float depth = 0.f; bool hit = false;
		if (other.shape.kind == ShapeKind::Capsule)
		{
			const V3 otherMid = other.GetCapsuleMid();
			hit = CapsuleVsCapsule(
				p, q, self.shape.radius,
				otherMid + V3(0, other.shape.segHalf, 0),
				otherMid - V3(0, other.shape.segHalf, 0),
				other.shape.radius, n, depth);
		}
		else
		{
			const auto bb = other.GetAABB();
			hit = CapsuleVsAABB(p, q, self.shape.radius, V3(bb.Center), V3(bb.Extents), n, depth);
		}
		if (!hit) continue;

		if (blocks)
		{
			const float otherInv = std::max(other.inverseMass, 0.f);
			const float sum = moverInv + otherInv;
			if (sum > 1e-8f)
			{
				center += n * (depth * (moverInv / sum));
				if (otherInv > 0.f)
					other.pushAccum += (-n) * (depth * (otherInv / sum));
			}
		}

		if (depth > deepest)
		{
			deepest = depth;
			res.hitObject = true;
			res.objectNormal = n;
			res.objectTag = other.tag;
			res.objectType = other.type;
		}

		if (moverCares) self.objectHits.push_back({ m_collision.HandleAt(j),      other.type, other.tag,  n });
		if (otherCares) other.objectHits.push_back({ m_collision.HandleAt(selfIdx), self.type,  self.tag, -n });
	}
}

void CollisionWorld::ProcessObjectPairs()
{
	using namespace Collision;
	auto& comps = m_collision.Data();
	m_gridEntries.clear();

	auto key = [](int x, int z) -> uint64_t {
		return (static_cast<uint64_t>(static_cast<uint32_t>(x)) << 32) | static_cast<uint32_t>(z);
		};

	for (uint32_t i = 0; i < comps.size(); ++i)
	{
		auto& c = comps[i];
		if (!c.enabled) continue;
		if (c.IsCapsuleMover()) continue; // phase1 で処理済み

		const auto bb = c.GetAABB();
		const int x0 = (int)std::floor((bb.Center.x - bb.Extents.x) / kObjectCellSize);
		const int x1 = (int)std::floor((bb.Center.x + bb.Extents.x) / kObjectCellSize);
		const int z0 = (int)std::floor((bb.Center.z - bb.Extents.z) / kObjectCellSize);
		const int z1 = (int)std::floor((bb.Center.z + bb.Extents.z) / kObjectCellSize);
		for (int z = z0; z <= z1; ++z)
			for (int x = x0; x <= x1; ++x)
				m_gridEntries.push_back({ key(x, z), i });
	}
	if (m_gridEntries.empty()) return;

	std::sort(m_gridEntries.begin(), m_gridEntries.end(),
		[](const GridEntry& a, const GridEntry& b) { return a.key < b.key; });

	m_gridPairs.clear();
	for (size_t i = 0; i < m_gridEntries.size(); )
	{
		const uint64_t k = m_gridEntries[i].key;
		size_t j = i + 1;
		while (j < m_gridEntries.size() && m_gridEntries[j].key == k) ++j;
		for (size_t a = i; a < j; ++a)
			for (size_t b = a + 1; b < j; ++b)
			{
				uint32_t ia = m_gridEntries[a].dataIdx, ib = m_gridEntries[b].dataIdx;
				if (ia > ib) std::swap(ia, ib);
				m_gridPairs.emplace_back(ia, ib);
			}
		i = j;
	}
	std::sort(m_gridPairs.begin(), m_gridPairs.end());
	m_gridPairs.erase(std::unique(m_gridPairs.begin(), m_gridPairs.end()), m_gridPairs.end());

	for (const auto& pr : m_gridPairs)
	{
		auto& ca = comps[pr.first];
		auto& cb = comps[pr.second];

		const uint32_t aBit = TypeBit(ca.type), bBit = TypeBit(cb.type);
		const bool aWantsB = (ca.typeMask & bBit) != 0;
		const bool bWantsA = (cb.typeMask & aBit) != 0;
		if (!aWantsB && !bWantsA) continue;
		if (!ca.GetAABB().Intersects(cb.GetAABB())) continue;

		Math::Vector3 n = cb.center - ca.center;
		if (n.LengthSquared() > 1e-8f) n.Normalize(); else n = Math::Vector3(0, 0, 0);

		if (aWantsB) ca.objectHits.push_back({ m_collision.HandleAt(pr.second), cb.type, cb.tag,  n });
		if (bWantsA) cb.objectHits.push_back({ m_collision.HandleAt(pr.first),  ca.type, ca.tag, -n });
	}
}

void CollisionWorld::ProcessHits()
{
	using namespace Collision;
	m_landedThisFrame.clear();

	//デバッグ用にコピー
	m_hitboxSnapshot.clear();
	for (auto& hb : m_hitboxes)
		m_hitboxSnapshot.push_back({ hb.vol, hb.team });

	if (m_hitboxes.empty()) { m_hitRegistry.clear(); return; }

	m_activeAttackIds.clear();
	for (auto& hb : m_hitboxes) m_activeAttackIds.insert(hb.data.attackId);

	auto& recvs = m_combat.Data();
	for (auto& hb : m_hitboxes)
		for (uint32_t j = 0; j < recvs.size(); ++j)
		{
			auto& rcv = recvs[j];
			if (!rcv.hittable)           continue;
			if (rcv.team == hb.team)     continue;

			const Handle rh = m_combat.HandleAt(j);
			if (hb.attacker.IsValid() && hb.attacker == rh) continue; // 自傷防止

			const uint64_t regKey = ((uint64_t)hb.data.attackId << 32) | rh.index;
			if (m_hitRegistry.count(regKey)) continue;

			Math::Vector3 nn; float depth;
			if (!CapsuleVsCapsule(hb.vol.p0, hb.vol.p1, hb.vol.radius,
				rcv.hurtVolume.p0, rcv.hurtVolume.p1, rcv.hurtVolume.radius, nn, depth)) continue;

			m_hitRegistry.insert(regKey);
			m_landedThisFrame.insert(hb.data.attackId);
			rcv.received.push_back(hb.data);
		}

	for (auto it = m_hitRegistry.begin(); it != m_hitRegistry.end(); )
		if (!m_activeAttackIds.count((uint32_t)(*it >> 32))) it = m_hitRegistry.erase(it);
		else ++it;

	m_hitboxes.clear();
}

// =====================================================================
//  クエリ
// =====================================================================
Collision::HitResult CollisionWorld::RaycastStage(
	const Math::Vector3& start, const Math::Vector3& end) const
{
	if (m_stage && m_stage->IsBuilt())
		return m_stage->Raycast(start, end);
	return {};
}

Collision::CapsuleMoveResult CollisionWorld::SweepCapsuleStage(
	const Math::Vector3& center, float segHalf, float radius,
	const Math::Vector3& displacement) const
{
	if (!m_stage || !m_stage->IsBuilt())
	{
		Collision::CapsuleMoveResult r; r.position = center + displacement; return r;
	}
	return m_stage->MoveCapsule(center, segHalf, radius, displacement);
}

Collision::HitResult CollisionWorld::SphereCastStage(
	const Math::Vector3& start, const Math::Vector3& end, float radius) const
{
	if (m_stage && m_stage->IsBuilt())
		return m_stage->SphereCast(start, end, radius);
	return {};
}