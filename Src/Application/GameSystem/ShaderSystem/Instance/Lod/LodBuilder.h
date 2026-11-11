#pragma once
// 頂点クラスタリングによるメッシュ簡略化
// gridSize: 分割数（小さいほど激しく削減。16 で 1/10〜1/20 程度）
std::shared_ptr<KdMesh> BuildLodMesh(const KdMesh& src, int gridSize);