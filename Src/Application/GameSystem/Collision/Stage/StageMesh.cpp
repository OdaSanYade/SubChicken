#include "Pch.h"
#include "StageMesh.h"
#include <algorithm>
#include <cmath>

namespace
{
	using V3 = Math::Vector3;

	// Ericson: 線分-線分の最近接（戻り値=距離^2）
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

	// Ericson: 点-三角形の最近接点
	V3 ClosestPtPointTri(const V3& p, const V3& a, const V3& b, const V3& c)
	{
		V3 ab = b - a, ac = c - a, ap = p - a;
		float d1 = ab.Dot(ap), d2 = ac.Dot(ap);
		if (d1 <= 0.f && d2 <= 0.f) return a;
		V3 bp = p - b; float d3 = ab.Dot(bp), d4 = ac.Dot(bp);
		if (d3 >= 0.f && d4 <= d3) return b;
		float vc = d1 * d4 - d3 * d2;
		if (vc <= 0.f && d1 >= 0.f && d3 <= 0.f) { float v = d1 / (d1 - d3); return a + ab * v; }
		V3 cp = p - c; float d5 = ab.Dot(cp), d6 = ac.Dot(cp);
		if (d6 >= 0.f && d5 <= d6) return c;
		float vb = d5 * d2 - d1 * d6;
		if (vb <= 0.f && d2 >= 0.f && d6 <= 0.f) { float w = d2 / (d2 - d6); return a + ac * w; }
		float va = d3 * d6 - d5 * d4;
		if (va <= 0.f && (d4 - d3) >= 0.f && (d5 - d6) >= 0.f) {
			float w = (d4 - d3) / ((d4 - d3) + (d5 - d6)); return b + (c - b) * w;
		}
		float denom = 1.f / (va + vb + vc);
		return a + ab * (vb * denom) + ac * (vc * denom);
	}

	// 線分-三角形の最近接（戻り値=距離^2、最近接点を返す）
	void ClosestPtSegTri(const V3& p, const V3& q,
		const V3& a, const V3& b, const V3& c,
		V3& outSeg, V3& outTri, float& outD2)
	{
		V3 n = (b - a).Cross(c - a);
		float dp = (p - a).Dot(n), dq = (q - a).Dot(n);
		if (dp * dq < 0.f) {
			float tt = dp / (dp - dq);
			V3 hit = p + (q - p) * tt;
			V3 cpt = ClosestPtPointTri(hit, a, b, c);
			if ((cpt - hit).LengthSquared() < 1e-10f) { outSeg = outTri = hit; outD2 = 0.f; return; }
		}
		float best = FLT_MAX; V3 bs, bt;
		auto consider = [&](const V3& s, const V3& t) {
			float d = (s - t).LengthSquared(); if (d < best) { best = d; bs = s; bt = t; } };

		consider(p, ClosestPtPointTri(p, a, b, c));
		consider(q, ClosestPtPointTri(q, a, b, c));
		V3 c1, c2;
		ClosestPtSegSeg(p, q, a, b, c1, c2); consider(c1, c2);
		ClosestPtSegSeg(p, q, b, c, c1, c2); consider(c1, c2);
		ClosestPtSegSeg(p, q, c, a, c1, c2); consider(c1, c2);
		outSeg = bs; outTri = bt; outD2 = best;
	}

	inline bool Finite3(const V3& v)
	{
		return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
	}
	inline bool AabbOverlap(const V3& aMin, const V3& aMax, const V3& bMin, const V3& bMax)
	{
		return !(aMax.x < bMin.x || aMin.x > bMax.x ||
			aMax.y < bMin.y || aMin.y > bMax.y ||
			aMax.z < bMin.z || aMin.z > bMax.z);
	}
}

// =====================================================================
//  内部ヘルパ
// =====================================================================
void StageMesh::CellRange(const Math::Vector3& mn, const Math::Vector3& mx, int r[6]) const
{
	const float inv = 1.0f / m_cellSize;
	r[0] = std::clamp((int)std::floor((mn.x - m_origin.x) * inv), 0, m_nx - 1);
	r[1] = std::clamp((int)std::floor((mn.y - m_origin.y) * inv), 0, m_ny - 1);
	r[2] = std::clamp((int)std::floor((mn.z - m_origin.z) * inv), 0, m_nz - 1);
	r[3] = std::clamp((int)std::floor((mx.x - m_origin.x) * inv), 0, m_nx - 1);
	r[4] = std::clamp((int)std::floor((mx.y - m_origin.y) * inv), 0, m_ny - 1);
	r[5] = std::clamp((int)std::floor((mx.z - m_origin.z) * inv), 0, m_nz - 1);
}

uint32_t StageMesh::NextStamp() const
{
	++m_stamp;
	if (m_stamp == 0u) { std::fill(m_triStamp.begin(), m_triStamp.end(), 0u); ++m_stamp; }
	return m_stamp;
}

// =====================================================================
//  Build
// =====================================================================
void StageMesh::Build(std::vector<PrecomputedTri>&& _tris,
	std::vector<uint32_t>&& _tags)
{
	m_nx = m_ny = m_nz = 0;
	m_cellSize = kBaseCellSize;
	m_cellStart.clear();
	m_cellItems.clear();
	m_oversized.clear();
	m_triStamp.clear();
	m_stamp = 0;

	m_tris = std::move(_tris);
	m_triTag = std::move(_tags);
	if (m_triTag.size() != m_tris.size())
		m_triTag.assign(m_tris.size(), 0u);

	// ---- 1) 不正(NaN/Inf)・退化した三角形を除去し、法線とAABBを張り直す ----
	//     ここで落としておかないと境界計算とグリッド確保が破綻する。
	//     tag も同時に詰めてインデックスの対応を保つ。
	{
		size_t w = 0;
		for (size_t r = 0; r < m_tris.size(); ++r)
		{
			PrecomputedTri t = m_tris[r];
			const Math::Vector3 v1 = t.v0 + t.e0;
			const Math::Vector3 v2 = t.v0 + t.e1;
			if (!Finite3(t.v0) || !Finite3(v1) || !Finite3(v2)) continue;

			Math::Vector3 n = t.e0.Cross(t.e1);
			const float nl = n.Length();
			if (!(nl > 1e-12f)) continue; // 面積ゼロ
			t.normal = n / nl;

			t.bMin.x = std::min({ t.v0.x, v1.x, v2.x });
			t.bMin.y = std::min({ t.v0.y, v1.y, v2.y });
			t.bMin.z = std::min({ t.v0.z, v1.z, v2.z });
			t.bMax.x = std::max({ t.v0.x, v1.x, v2.x });
			t.bMax.y = std::max({ t.v0.y, v1.y, v2.y });
			t.bMax.z = std::max({ t.v0.z, v1.z, v2.z });

			m_tris[w] = t;
			m_triTag[w] = m_triTag[r];
			++w;
		}
		m_tris.resize(w);
		m_triTag.resize(w);
	}
	if (m_tris.empty()) return;

	// ---- 2) 全体境界 ----
	Math::Vector3 bMin = m_tris[0].bMin;
	Math::Vector3 bMax = m_tris[0].bMax;
	for (const auto& t : m_tris)
	{
		bMin.x = std::min(bMin.x, t.bMin.x); bMin.y = std::min(bMin.y, t.bMin.y); bMin.z = std::min(bMin.z, t.bMin.z);
		bMax.x = std::max(bMax.x, t.bMax.x); bMax.y = std::max(bMax.y, t.bMax.y); bMax.z = std::max(bMax.z, t.bMax.z);
	}

	const float pad = kBaseCellSize;
	m_origin = bMin - Math::Vector3(pad, pad, pad);
	const Math::Vector3 extent = (bMax - bMin) + Math::Vector3(2.f * pad, 2.f * pad, 2.f * pad);

	// ---- 3) セルサイズ決定 ----
	//  基準サイズで試し、軸上限か総セル数の予算を超えるなら収まるまで拡大する。
	//  ステージがどれだけ広くてもセル数は有限に収まり、かつ小さいステージでは
	//  基準サイズ(3.0)がそのまま使われる。
	auto axisCount = [](float ext, float cs) -> long long
		{
			const long long n = (long long)std::ceil(ext / cs);
			return (n < 1) ? 1 : n;
		};

	for (int guard = 0; guard < 128; ++guard)
	{
		const long long nx = axisCount(extent.x, m_cellSize);
		const long long ny = axisCount(extent.y, m_cellSize);
		const long long nz = axisCount(extent.z, m_cellSize);
		const bool axisOk = (nx <= kMaxAxisCells && ny <= kMaxAxisCells && nz <= kMaxAxisCells);
		if (axisOk && nx * ny * nz <= (long long)kMaxTotalCells)
		{
			m_nx = (int)nx; m_ny = (int)ny; m_nz = (int)nz;
			break;
		}
		m_cellSize *= 1.35f;
	}
	if (m_nx == 0) // ここに来ることはまず無いが保険（グリッド1個扱い）
	{
		m_nx = m_ny = m_nz = 1;
		m_cellSize = std::max({ extent.x, extent.y, extent.z, kBaseCellSize });
	}

	const size_t nCells = (size_t)m_nx * (size_t)m_ny * (size_t)m_nz;
	m_cellStart.assign(nCells + 1, 0u);
	m_triStamp.assign(m_tris.size(), 0u);

	// ---- 4) パス1: セルごとの件数を数える ----
	//  極端に広い三角形（地面板など）はセルに撒くとインデックスが爆発するので
	//  oversized へ退避し、クエリ時に常時テストする。
	std::vector<uint8_t> isBig(m_tris.size(), 0u);
	int rg[6];
	for (size_t i = 0; i < m_tris.size(); ++i)
	{
		CellRange(m_tris[i].bMin, m_tris[i].bMax, rg);
		const long long span =
			(long long)(rg[3] - rg[0] + 1) * (rg[4] - rg[1] + 1) * (rg[5] - rg[2] + 1);
		if (span > kMaxTriCellSpan)
		{
			isBig[i] = 1u;
			m_oversized.push_back((uint32_t)i);
			continue;
		}
		for (int z = rg[2]; z <= rg[5]; ++z)
			for (int y = rg[1]; y <= rg[4]; ++y)
				for (int x = rg[0]; x <= rg[3]; ++x)
					++m_cellStart[FlatIndex(x, y, z) + 1];
	}

	// ---- 5) 前方累積 → 実体配列 ----
	for (size_t c = 0; c < nCells; ++c)
		m_cellStart[c + 1] += m_cellStart[c];

	m_cellItems.resize(m_cellStart[nCells]);
	std::vector<uint32_t> cursor(m_cellStart.begin(), m_cellStart.begin() + nCells);

	// ---- 6) パス2: 書き込み ----
	for (size_t i = 0; i < m_tris.size(); ++i)
	{
		if (isBig[i]) continue;
		CellRange(m_tris[i].bMin, m_tris[i].bMax, rg);
		for (int z = rg[2]; z <= rg[5]; ++z)
			for (int y = rg[1]; y <= rg[4]; ++y)
				for (int x = rg[0]; x <= rg[3]; ++x)
					m_cellItems[cursor[FlatIndex(x, y, z)]++] = (uint32_t)i;
	}
}

// =====================================================================
//  GatherTriangles
// =====================================================================
void StageMesh::GatherTriangles(const DirectX::BoundingBox& aabb,
	std::vector<uint32_t>& out) const
{
	out.clear();
	if (!IsBuilt()) return;

	const Math::Vector3 c(aabb.Center);
	const Math::Vector3 e(aabb.Extents);
	const Math::Vector3 qMin = c - e;
	const Math::Vector3 qMax = c + e;

	int rg[6];
	CellRange(qMin, qMax, rg);

	const uint32_t cur = NextStamp();

	out.reserve(64);
	for (int z = rg[2]; z <= rg[5]; ++z)
		for (int y = rg[1]; y <= rg[4]; ++y)
			for (int x = rg[0]; x <= rg[3]; ++x)
			{
				const size_t cell = FlatIndex(x, y, z);
				for (uint32_t k = m_cellStart[cell]; k < m_cellStart[cell + 1]; ++k)
				{
					const uint32_t idx = m_cellItems[k];
					if (m_triStamp[idx] == cur) continue;
					m_triStamp[idx] = cur;
					out.push_back(idx);
				}
			}

	// 広範囲三角形は AABB で軽く弾いてから追加
	for (uint32_t idx : m_oversized)
	{
		const auto& tri = m_tris[idx];
		if (!AabbOverlap(qMin, qMax, tri.bMin, tri.bMax)) continue;
		if (m_triStamp[idx] == cur) continue;
		m_triStamp[idx] = cur;
		out.push_back(idx);
	}
}

// =====================================================================
//  Raycast (Amanatides-Woo DDA)
// =====================================================================
Collision::HitResult StageMesh::Raycast(
	const Math::Vector3& start, const Math::Vector3& end, float epsilon) const
{
	Collision::HitResult result;
	if (!IsBuilt()) return result;

	const Math::Vector3 dir = end - start;
	const float rayLen = dir.Length();
	if (rayLen < 1e-9f) return result;
	const Math::Vector3 nd = dir * (1.0f / rayLen);

	const float cs = m_cellSize;
	float closestT = FLT_MAX;

	// Möller-Trumbore（ヒットしたら result / closestT を更新）
	auto testTri = [&](uint32_t triIdx)
		{
			const auto& tri = m_tris[triIdx];

			if constexpr (kBackfaceCulling)
			{
				if (tri.normal.Dot(nd) > 0.f) return;
			}

			const Math::Vector3 h = nd.Cross(tri.e1);
			const float a = tri.e0.Dot(h);
			if (std::abs(a) < epsilon) return;

			const float f = 1.0f / a;
			const Math::Vector3 sv = start - tri.v0;
			const float u = f * sv.Dot(h);
			if (u < 0.f || u > 1.f) return;

			const Math::Vector3 q = sv.Cross(tri.e0);
			const float v = f * nd.Dot(q);
			if (v < 0.f || u + v > 1.f) return;

			const float hitT = f * tri.e1.Dot(q);
			if (hitT < epsilon || hitT > rayLen || hitT >= closestT) return;

			closestT = hitT;
			Math::Vector3 normal = tri.normal;
			if constexpr (!kBackfaceCulling)
			{
				if (normal.Dot(nd) > 0.f) normal = -normal;
			}

			result.isHit = true;
			result.distance = hitT;
			result.hitpos = start + nd * hitT;
			result.normal = normal;
			result.tag = m_triTag[triIdx];
		};

	// ---- 0) 広範囲三角形（グリッド未登録）を先に処理 ----
	if (!m_oversized.empty())
	{
		const Math::Vector3 segMin{ std::min(start.x, end.x), std::min(start.y, end.y), std::min(start.z, end.z) };
		const Math::Vector3 segMax{ std::max(start.x, end.x), std::max(start.y, end.y), std::max(start.z, end.z) };
		for (uint32_t idx : m_oversized)
		{
			const auto& tri = m_tris[idx];
			if (!AabbOverlap(segMin, segMax, tri.bMin, tri.bMax)) continue;
			testTri(idx);
		}
	}

	// ---- 1) 開始セル決定（グリッド外ならスラブ判定で進入点を求める）----
	int cx = (int)std::floor((start.x - m_origin.x) / cs);
	int cy = (int)std::floor((start.y - m_origin.y) / cs);
	int cz = (int)std::floor((start.z - m_origin.z) / cs);

	if (!Valid(cx, cy, cz))
	{
		const float gx = m_nx * cs, gy = m_ny * cs, gz = m_nz * cs;
		float tEnter = 0.f, tExit = rayLen;
		auto slab = [](float s, float d, float lo, float hi, float& tIn, float& tOut) -> bool
			{
				if (std::abs(d) < 1e-9f) return (s >= lo && s <= hi);
				float t1 = (lo - s) / d, t2 = (hi - s) / d;
				if (t1 > t2) std::swap(t1, t2);
				tIn = std::max(tIn, t1); tOut = std::min(tOut, t2);
				return tIn <= tOut;
			};
		const bool hit =
			slab(start.x, nd.x, m_origin.x, m_origin.x + gx, tEnter, tExit) &&
			slab(start.y, nd.y, m_origin.y, m_origin.y + gy, tEnter, tExit) &&
			slab(start.z, nd.z, m_origin.z, m_origin.z + gz, tEnter, tExit);
		if (!hit || tEnter > rayLen) return result; // oversized の結果はそのまま返る

		const Math::Vector3 entry = start + nd * tEnter;
		cx = std::clamp((int)std::floor((entry.x - m_origin.x) / cs), 0, m_nx - 1);
		cy = std::clamp((int)std::floor((entry.y - m_origin.y) / cs), 0, m_ny - 1);
		cz = std::clamp((int)std::floor((entry.z - m_origin.z) / cs), 0, m_nz - 1);
	}

	// ---- 2) DDA ----
	const int sx = (nd.x >= 0.f) ? 1 : -1;
	const int sy = (nd.y >= 0.f) ? 1 : -1;
	const int sz = (nd.z >= 0.f) ? 1 : -1;

	auto safeDelta = [cs](float d) -> float {
		return (std::abs(d) < 1e-9f) ? FLT_MAX : cs / std::abs(d); };
	const float dX = safeDelta(nd.x), dY = safeDelta(nd.y), dZ = safeDelta(nd.z);

	auto tNextBound = [cs](float sc, float ndir, int step, float origin, int cell) -> float
		{
			if (std::abs(ndir) < 1e-9f) return FLT_MAX;
			const float boundary = origin + (float)(step > 0 ? cell + 1 : cell) * cs;
			float t = (boundary - sc) / ndir;
			if (t < 0.f) t += cs / std::abs(ndir);
			return t;
		};
	float tNX = tNextBound(start.x, nd.x, sx, m_origin.x, cx);
	float tNY = tNextBound(start.y, nd.y, sy, m_origin.y, cy);
	float tNZ = tNextBound(start.z, nd.z, sz, m_origin.z, cz);

	const uint32_t curStamp = NextStamp();

	for (;;)
	{
		if (!Valid(cx, cy, cz)) break;

		const size_t cell = FlatIndex(cx, cy, cz);
		for (uint32_t k = m_cellStart[cell]; k < m_cellStart[cell + 1]; ++k)
		{
			const uint32_t triIdx = m_cellItems[k];
			if (m_triStamp[triIdx] == curStamp) continue;
			m_triStamp[triIdx] = curStamp;
			testTri(triIdx);
		}

		const float tExit = std::min({ tNX, tNY, tNZ });
		if (tExit > rayLen)                    break;
		if (result.isHit && closestT <= tExit) break;

		if (tNX <= tNY && tNX <= tNZ) { cx += sx; tNX += dX; }
		else if (tNY <= tNZ) { cy += sy; tNY += dY; }
		else { cz += sz; tNZ += dZ; }
	}
	return result;
}

// =====================================================================
//  球スイープ
// =====================================================================
float StageMesh::ClosestTriangleDistance(
	const std::vector<uint32_t>& tris, const Math::Vector3& p,
	float searchRadius, Math::Vector3& outNormal, uint32_t& outTag) const
{
	float         best2 = FLT_MAX;
	Math::Vector3 bestPt;
	uint32_t      bestIdx = UINT32_MAX;

	const Math::Vector3 qMin = p - Math::Vector3(searchRadius, searchRadius, searchRadius);
	const Math::Vector3 qMax = p + Math::Vector3(searchRadius, searchRadius, searchRadius);

	for (uint32_t idx : tris)
	{
		const auto& tri = m_tris[idx];
		if (!AabbOverlap(qMin, qMax, tri.bMin, tri.bMax)) continue;

		const Math::Vector3 v1 = tri.v0 + tri.e0;
		const Math::Vector3 v2 = tri.v0 + tri.e1;
		const Math::Vector3 cp = ClosestPtPointTri(p, tri.v0, v1, v2);

		const float d2 = (p - cp).LengthSquared();
		if (d2 >= best2) continue;
		best2 = d2; bestPt = cp; bestIdx = idx;
	}
	if (bestIdx == UINT32_MAX) return FLT_MAX;

	const float d = std::sqrt(best2);
	outTag = m_triTag[bestIdx];
	outNormal = (d > 1e-5f) ? (p - bestPt) / d : m_tris[bestIdx].normal;
	return d;
}

Collision::HitResult StageMesh::SphereCast(
	const Math::Vector3& start, const Math::Vector3& end, float radius,
	float epsilon, int maxIterations) const
{
	Collision::HitResult res;
	if (!IsBuilt()) return res;

	const Math::Vector3 seg = end - start;
	const float len = seg.Length();
	if (len < 1e-5f) return res;

	const Math::Vector3 dir = seg / len;
	const float r = std::max(radius, 1e-3f);
	const float probe = kSphereCastProbe;

	// 経路全体＋探査余白ぶんの三角形を1回だけ収集する。
	// 球が触れうる三角形は必ずこの箱に入るので、外は無視して安全。
	{
		const float pad = r + probe;
		const Math::Vector3 mn{
			std::min(start.x, end.x) - pad, std::min(start.y, end.y) - pad, std::min(start.z, end.z) - pad };
		const Math::Vector3 mx{
			std::max(start.x, end.x) + pad, std::max(start.y, end.y) + pad, std::max(start.z, end.z) + pad };

		DirectX::BoundingBox box;
		box.Center = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
		box.Extents = { (mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f };
		GatherTriangles(box, m_castScratch);
	}
	if (m_castScratch.empty()) return res;

	const float eps = std::max(epsilon, 1e-3f);
	float t = 0.0f;
	bool  escaped = false; // 開始地点の埋まりから抜けたか

	for (int i = 0; i < maxIterations && t <= len; ++i)
	{
		const Math::Vector3 c = start + dir * t;

		Math::Vector3 n = Math::Vector3::Up;
		uint32_t      tag = 0;
		const float   d = ClosestTriangleDistance(m_castScratch, c, r + probe, n, tag);
		const float   clearance = (d == FLT_MAX) ? probe : (d - r);

		// この距離までは絶対に当たらない＝一気に進めてよい（conservative advancement）
		if (clearance >= eps) { escaped = true; t += clearance; continue; }

		// 開始時点で埋まっている間は当たり扱いにしない（壁際でカメラが潰れる原因）
		if (!escaped) { t += std::max(r * 0.5f, eps * 4.0f); continue; }

		res.isHit = true;
		res.distance = t;
		res.hitpos = c;
		res.normal = n;
		res.tag = tag;
		return res;
	}
	return res;
}

// =====================================================================
//  カプセルCCD
// =====================================================================
StageMesh::CapsuleContact StageMesh::DeepestCapsuleContact(
	const std::vector<uint32_t>& tris,
	const Math::Vector3& p, const Math::Vector3& q, float radius) const
{
	CapsuleContact best;
	const float r2 = radius * radius;

	const Math::Vector3 cMin = {
		std::min(p.x, q.x) - radius, std::min(p.y, q.y) - radius, std::min(p.z, q.z) - radius };
	const Math::Vector3 cMax = {
		std::max(p.x, q.x) + radius, std::max(p.y, q.y) + radius, std::max(p.z, q.z) + radius };
	const Math::Vector3 capCenter = (p + q) * 0.5f;

	for (uint32_t idx : tris)
	{
		const auto& tri = m_tris[idx];
		if (!AabbOverlap(cMin, cMax, tri.bMin, tri.bMax)) continue;

		const Math::Vector3 v0 = tri.v0;
		const Math::Vector3 v1 = tri.v0 + tri.e0;
		const Math::Vector3 v2 = tri.v0 + tri.e1;

		Math::Vector3 segPt, triPt; float d2;
		ClosestPtSegTri(p, q, v0, v1, v2, segPt, triPt, d2);
		if (d2 >= r2) continue;

		const float dist = std::sqrt(d2);
		const float depth = radius - dist;
		if (depth <= best.depth) continue;

		Math::Vector3 normal;
		if (dist > 1e-5f) normal = (segPt - triPt) * (1.f / dist);
		else {
			normal = tri.normal;
			if (normal.Dot(capCenter - triPt) < 0.f) normal = -normal;
		}
		best.hit = true; best.normal = normal; best.depth = depth; best.tag = m_triTag[idx];
	}
	return best;
}

void StageMesh::GatherCapsulePathTriangles(
	const Math::Vector3& center, float segHalf, float radius,
	const Math::Vector3& disp, const Collision::CapsuleMoveConfig& cfg,
	std::vector<uint32_t>& out) const
{
	using V3 = Math::Vector3;
	const float upReach = cfg.stepHeight + std::max(disp.y, 0.f);
	const float downReach = cfg.snapDistance - std::min(disp.y, 0.f);

	const V3 a = center;
	const V3 b = center + V3(disp.x, 0.f, disp.z);
	V3 mn = { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) };
	V3 mx = { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) };
	mn.y -= downReach; mx.y += upReach;

	const float pad = std::max(radius, 1e-3f) * 2.f + std::max(segHalf, 0.f) + 0.25f;
	mn -= V3(pad, pad, pad); mx += V3(pad, pad, pad);

	DirectX::BoundingBox box;
	box.Center = { (mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f };
	box.Extents = { (mx.x - mn.x) * 0.5f, (mx.y - mn.y) * 0.5f, (mx.z - mn.z) * 0.5f };
	GatherTriangles(box, out);
}

Collision::CapsuleMoveResult StageMesh::MoveCapsuleTris(
	const std::vector<uint32_t>& tris,
	const Math::Vector3& center, float segHalf, float radius,
	const Math::Vector3& displacement, float slopeLimit, int maxSubsteps) const
{
	Collision::CapsuleMoveResult res;
	res.position = center;
	if (tris.empty()) { res.position = center + displacement; return res; }

	segHalf = std::max(segHalf, 0.f);
	const float r = std::max(radius, 1e-3f);

	const float dispLen = displacement.Length();
	const float maxStep = r * 0.5f;
	const int   N = (dispLen > 1e-6f)
		? std::clamp((int)std::ceil(dispLen / maxStep), 1, std::max(1, maxSubsteps)) : 1;
	const Math::Vector3 step = displacement / (float)N;

	const float     skin = 0.001f;
	constexpr float kCeilY = -0.6f;
	constexpr int   kDepenIter = 6;

	for (int i = 0; i < N; ++i)
	{
		res.position += step;
		for (int it = 0; it < kDepenIter; ++it)
		{
			const Math::Vector3 p = res.position + Math::Vector3(0, segHalf, 0);
			const Math::Vector3 q = res.position - Math::Vector3(0, segHalf, 0);
			CapsuleContact h = DeepestCapsuleContact(tris, p, q, r);
			if (!h.hit) break;
			res.position += h.normal * (h.depth + skin);

			if (h.normal.y >= slopeLimit) { res.grounded = true;   res.groundNormal = h.normal; res.groundTag = h.tag; }
			else if (h.normal.y < kCeilY) { res.hitCeiling = true; res.ceilingTag = h.tag; }
			else { res.hitWall = true;    res.wallNormal = h.normal;   res.wallTag = h.tag; }
		}
	}

	// =====================================================================
	//  トンネリング検知（スイープ・セーフティネット）
	// ---------------------------------------------------------------------
	//  上のサブステップ＋押し出しは「離散位置での重なり」しか見ておらず、
	//  1回の移動量が大きいとき（他カプセルからの押し出し・大きな吹き飛ばし等）
	//  薄い床や壁を一度も重ならずに素通りしてしまう＝地面貫通が起こり得る。
	//  ここでは始点→終点をカプセル軸沿いの3点（下端・中心・上端）でスイープし、
	//  経路上にステージ三角形との交差があれば貫通直前まで巻き戻して押し出す。
	//  m_tris全体を使うレイキャストで判定するため、呼び出し側が渡した tris
	//  （ブロードフェーズで絞り込んだ候補）が押し出し等で不十分でも取りこぼさない。
	// =====================================================================
	{
		const Math::Vector3 moved = res.position - center;
		const float movedLen = moved.Length();
		if (movedLen > 1e-4f)
		{
			const Math::Vector3 dir = moved / movedLen;
			const Math::Vector3 offsets[3] = {
				Math::Vector3(0, -(segHalf + r), 0), // 最下端
				Math::Vector3(0,  0,             0), // 中心（スパイン基準点）
				Math::Vector3(0,  segHalf + r,   0), // 最上端
			};

			float         bestT = FLT_MAX;
			Math::Vector3 bestNormal(0.f, 1.f, 0.f);
			uint32_t      bestTag = 0;
			int           bestOffset = -1;

			for (int k = 0; k < 3; ++k)
			{
				const Math::Vector3 s = center + offsets[k];
				const Math::Vector3 e = res.position + offsets[k];
				const Collision::HitResult hr = Raycast(s, e);
				if (!hr.isHit) continue;
				if (hr.distance < bestT)
				{
					bestT = hr.distance; bestNormal = hr.normal; bestTag = hr.tag; bestOffset = k;
				}
			}

			if (bestOffset >= 0)
			{
				// 貫通直前の位置まで巻き戻し、ヒット法線方向へ半径+スキン分だけ押し出す
				const Math::Vector3 hitOnRay = center + offsets[bestOffset] + dir * bestT;
				res.position = hitOnRay - offsets[bestOffset] + bestNormal * (r + skin);

				if (bestNormal.y >= slopeLimit) { res.grounded = true;   res.groundNormal = bestNormal; res.groundTag = bestTag; }
				else if (bestNormal.y < kCeilY) { res.hitCeiling = true; res.ceilingTag = bestTag; }
				else { res.hitWall = true;      res.wallNormal = bestNormal;   res.wallTag = bestTag; }
			}
		}
	}

	return res;
}

Collision::CapsuleMoveResult StageMesh::MoveCapsuleWithStep(
	const std::vector<uint32_t>& tris,
	const Math::Vector3& center, float segHalf, float radius,
	const Math::Vector3& displacement,
	const Collision::CapsuleMoveConfig& cfg) const
{
	using V3 = Math::Vector3;
	const V3 up(0, 1, 0);
	const float sl = cfg.slopeLimit;
	const int   ms = cfg.maxSubsteps;

	const V3    dispXZ(displacement.x, 0.f, displacement.z);
	const float dispY = displacement.y;

	// 1) 水平移動
	auto hRes = MoveCapsuleTris(tris, center, segHalf, radius, dispXZ, sl, ms);
	V3 pos = hRes.position;

	const V3 desiredXZ = center + dispXZ;
	const float plannedLen = dispXZ.Length();
	V3 achieved = pos - center; achieved.y = 0.f;
	const float achievedLen = achieved.Length();
	const bool blocked = (plannedLen > 1e-4f) &&
		(achievedLen < plannedLen - 1e-3f) && hRes.hitWall;

	// 2) 段差登り（持ち上げ→前進→降ろす）
	if (blocked && cfg.stepHeight > 0.f)
	{
		auto  upR = MoveCapsuleTris(tris, pos, segHalf, radius, up * cfg.stepHeight, sl, ms);
		const float lifted = upR.position.y - pos.y;

		V3 remain = desiredXZ - pos; remain.y = 0.f;
		auto fwdR = MoveCapsuleTris(tris, upR.position, segHalf, radius, remain, sl, ms);
		auto downR = MoveCapsuleTris(tris, fwdR.position, segHalf, radius, -up * (lifted + 0.02f), sl, ms);

		V3 stepAchieved = downR.position - center; stepAchieved.y = 0.f;
		if (stepAchieved.Length() > achievedLen + 1e-3f && downR.grounded)
		{
			pos = downR.position;
			hRes = downR;
		}
	}

	Collision::CapsuleMoveResult res = hRes;
	res.position = pos;

	// 3) 垂直（重力/ジャンプ）
	if (std::abs(dispY) > 1e-6f)
	{
		auto vR = MoveCapsuleTris(tris, pos, segHalf, radius, up * dispY, sl, ms);
		pos = vR.position; res.position = pos;

		if (dispY <= 0.f) {
			if (vR.grounded) { res.grounded = true; res.groundNormal = vR.groundNormal; res.groundTag = vR.groundTag; }
		}
		else {
			if (vR.hitCeiling) { res.hitCeiling = true; res.ceilingTag = vR.ceilingTag; }
		}
		if (vR.hitWall) { res.hitWall = true; res.wallNormal = vR.wallNormal; res.wallTag = vR.wallTag; }
	}

	// 4) 接地スナップ（下り坂・小段差で浮かない）
	if (!res.grounded && cfg.wasGrounded && dispY <= 1e-4f && cfg.snapDistance > 0.f)
	{
		auto sR = MoveCapsuleTris(tris, pos, segHalf, radius, -up * cfg.snapDistance, sl, ms);
		if (sR.grounded)
		{
			pos = sR.position; res.position = pos;
			res.grounded = true; res.groundNormal = sR.groundNormal; res.groundTag = sR.groundTag;
		}
	}
	return res;
}

Collision::CapsuleMoveResult StageMesh::MoveCapsule(
	const Math::Vector3& center, float segHalf, float radius,
	const Math::Vector3& displacement, float slopeLimit, int maxSubsteps) const
{
	if (!IsBuilt())
	{
		Collision::CapsuleMoveResult res; res.position = center + displacement; return res;
	}
	Collision::CapsuleMoveConfig tmp; tmp.stepHeight = 0.f; tmp.snapDistance = 0.f;
	std::vector<uint32_t> tris;
	GatherCapsulePathTriangles(center, segHalf, radius, displacement, tmp, tris);
	return MoveCapsuleTris(tris, center, segHalf, radius, displacement, slopeLimit, maxSubsteps);
}