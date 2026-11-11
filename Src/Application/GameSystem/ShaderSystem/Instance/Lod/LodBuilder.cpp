#include "LodBuilder.h"

std::shared_ptr<KdMesh> BuildLodMesh(const KdMesh& src, int gridSize)
{
	const auto& positions = src.GetVertexPositions();
	const auto& faces = src.GetFaces();
	const auto& subsets = src.GetSubsets();
	if (positions.empty() || faces.empty()) return nullptr;

	// バウンディングボックスからグリッドの大きさを決める
	const DirectX::BoundingBox& bb = src.GetBoundingBox();
	float ext[3] = { bb.Extents.x * 2.0f, bb.Extents.y * 2.0f, bb.Extents.z * 2.0f };
	float cell[3];
	for (int i = 0; i < 3; ++i)
		cell[i] = (ext[i] > 0.0001f) ? (ext[i] / gridSize) : 1.0f;
	float minP[3] = {
		bb.Center.x - bb.Extents.x,
		bb.Center.y - bb.Extents.y,
		bb.Center.z - bb.Extents.z };

	// 各頂点が属するセルを求め、セル→新頂点番号 の対応を作る
	std::unordered_map<uint64_t, int> cellToNew;
	std::vector<int> oldToNew(positions.size());
	std::vector<KdMeshVertex> newVerts;
	std::vector<int> accumCount;   // 平均を取るための個数

	const auto& srcVerts = src.GetVertices();

	for (size_t i = 0; i < positions.size(); ++i)
	{
		int cx = (int)((positions[i].x - minP[0]) / cell[0]);
		int cy = (int)((positions[i].y - minP[1]) / cell[1]);
		int cz = (int)((positions[i].z - minP[2]) / cell[2]);
		cx = (cx < 0) ? 0 : (cx >= gridSize ? gridSize - 1 : cx);
		cy = (cy < 0) ? 0 : (cy >= gridSize ? gridSize - 1 : cy);
		cz = (cz < 0) ? 0 : (cz >= gridSize ? gridSize - 1 : cz);
		uint64_t key = ((uint64_t)cx << 42) | ((uint64_t)cy << 21) | (uint64_t)cz;

		auto it = cellToNew.find(key);
		if (it == cellToNew.end())
		{
			int idx = (int)newVerts.size();
			cellToNew[key] = idx;
			oldToNew[i] = idx;
			KdMeshVertex v = {};
			if (i < srcVerts.size())
				v = srcVerts[i];       
			else
				v.Pos = positions[i];
			v.Color = 0xFFFFFFFF;
			newVerts.push_back(v);
			accumCount.push_back(1);
		}
		else
		{
			int idx = it->second;
			oldToNew[i] = idx;
			newVerts[idx].Pos += positions[i];  
			accumCount[idx]++;
		}
	}
	// 平均を確定
	for (size_t i = 0; i < newVerts.size(); ++i)
	{
		if (accumCount[i] > 1)
			newVerts[i].Pos /= (float)accumCount[i];
	}

	// 面を作り直す（潰れた三角形は捨てる）。subsetごとに詰め直す
	std::vector<KdMeshFace> newFaces;
	std::vector<KdMeshSubset> newSubsets;
	for (const auto& ss : subsets)
	{
		KdMeshSubset ns = {};
		ns.MaterialNo = ss.MaterialNo;
		ns.FaceStart = (UINT)newFaces.size();
		for (UINT f = 0; f < ss.FaceCount; ++f)
		{
			const KdMeshFace& src_f = faces[ss.FaceStart + f];
			int a = oldToNew[src_f.Idx[0]];
			int b = oldToNew[src_f.Idx[1]];
			int c = oldToNew[src_f.Idx[2]];
			if (a == b || b == c || c == a) continue;   // 潰れた三角形
			KdMeshFace nf;
			nf.Idx[0] = a; nf.Idx[1] = b; nf.Idx[2] = c;
			newFaces.push_back(nf);
		}
		ns.FaceCount = (UINT)newFaces.size() - ns.FaceStart;
		newSubsets.push_back(ns);
	}
	if (newFaces.empty()) return nullptr;

	// 法線を面法線から再計算（平均）
	std::vector<DirectX::XMFLOAT3> nrm(newVerts.size(), { 0,0,0 });
	for (const auto& f : newFaces)
	{
		DirectX::XMVECTOR p0 = DirectX::XMLoadFloat3(&newVerts[f.Idx[0]].Pos);
		DirectX::XMVECTOR p1 = DirectX::XMLoadFloat3(&newVerts[f.Idx[1]].Pos);
		DirectX::XMVECTOR p2 = DirectX::XMLoadFloat3(&newVerts[f.Idx[2]].Pos);
		DirectX::XMVECTOR n = DirectX::XMVector3Cross(
			DirectX::XMVectorSubtract(p1, p0),
			DirectX::XMVectorSubtract(p2, p0));
		for (int k = 0; k < 3; ++k)
		{
			DirectX::XMVECTOR acc = DirectX::XMLoadFloat3(&nrm[f.Idx[k]]);
			DirectX::XMStoreFloat3(&nrm[f.Idx[k]], DirectX::XMVectorAdd(acc, n));
		}
	}
	for (size_t i = 0; i < newVerts.size(); ++i)
	{
		DirectX::XMVECTOR n = DirectX::XMLoadFloat3(&nrm[i]);
		if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(n)) > 0.0001f)
			n = DirectX::XMVector3Normalize(n);
		else
			n = DirectX::XMVectorSet(0, 1, 0, 0);
		DirectX::XMStoreFloat3(&newVerts[i].Normal, n);
	}

	auto mesh = std::make_shared<KdMesh>();
	if (!mesh->Create(newVerts, newFaces, newSubsets, src.IsSkinMesh()))
		return nullptr;
	return mesh;
}