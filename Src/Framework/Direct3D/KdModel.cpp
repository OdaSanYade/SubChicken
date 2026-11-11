#include "KdModel.h"
#include "KdGLTFLoader.h"
#include "KdBinaryCacheUtil.h"
#include <filesystem>
#include <algorithm>

//コンストラクター
KdModelData::KdModelData()
{
}

//デストラクター
KdModelData::~KdModelData()
{
	Release();
}

//ロード関数
bool KdModelData::Load(std::string_view filename)
{
	Release();

	std::string fileDir = KdGetDirFromPath(filename.data());

	//----------------------------------------------------------
	// バイナリキャッシュがあればそこから読み込み、tinygltfでの
	// 再解析(JSONパース)を丸ごとスキップする
	//----------------------------------------------------------
	if (TryLoadFromCache(filename))
	{
		return true;
	}

	std::shared_ptr<KdGLTFModel> spGltfModel = KdLoadGLTFModel(filename.data());
	if (spGltfModel == nullptr) { return false; }

	CreateNodes(spGltfModel);
	CreateMaterials(spGltfModel, fileDir);
	CreateAnimations(spGltfModel);

	// 次回以降のために解析済み結果をキャッシュへ保存する
	SaveCache(filename, spGltfModel, fileDir);

	return true;
}

// ノード作成
void KdModelData::CreateNodes(const std::shared_ptr<KdGLTFModel>& spGltfModel)
{
	m_originalNodes.resize(spGltfModel->Nodes.size());

	for (UINT i = 0; i < spGltfModel->Nodes.size(); i++)
	{
		// 入力元ノード
		const KdGLTFNode& rSrcNode = spGltfModel->Nodes[i];

		// 出力先のノード参照
		Node& rDstNode = m_originalNodes[i];

		if (rSrcNode.IsMesh)
		{
			// メッシュ作成
			rDstNode.m_spMesh = std::make_shared<KdMesh>();

			if (rDstNode.m_spMesh)
			{
				rDstNode.m_spMesh->Create(rSrcNode.Mesh.Vertices, rSrcNode.Mesh.Faces, rSrcNode.Mesh.Subsets, rSrcNode.Mesh.IsSkinMesh);
			}

			// メッシュノードリストにインデックス登録
			m_meshNodeIndices.push_back(i);

		}

		// ノード情報セット
		rDstNode.m_name = rSrcNode.Name;

		rDstNode.m_localTransform = rSrcNode.LocalTransform;
		rDstNode.m_worldTransform = rSrcNode.WorldTransform;
		rDstNode.m_boneInverseWorldMatrix = rSrcNode.InverseBindMatrix;

		rDstNode.m_isSkinMesh = rSrcNode.Mesh.IsSkinMesh;

		rDstNode.m_boneIndex = rSrcNode.BoneNodeIndex;

		rDstNode.m_parent = rSrcNode.Parent;
		rDstNode.m_children = rSrcNode.Children;

		// 当たり判定用ノード検索
		if (rDstNode.m_name.find("COL") != std::string::npos)
		{
			// 判定用ノードに割り当て
			m_collisionMeshNodeIndices.push_back(i);
		}
		else
		{
			// 描画ノードに割り当て
			m_drawMeshNodeIndices.push_back(i);
		}
	}

	for (UINT nodeIdx = 0; nodeIdx < spGltfModel->Nodes.size(); nodeIdx++)
	{
		// ルートノードのIndexリスト
		if (spGltfModel->Nodes[nodeIdx].Parent == -1) { m_rootNodeIndices.push_back(nodeIdx); }

		// ボーンノードのIndexリスト
		int boneIdx = spGltfModel->Nodes[nodeIdx].BoneNodeIndex;

		if (boneIdx >= 0)
		{
			if (boneIdx >= (int)m_boneNodeIndices.size()) { m_boneNodeIndices.resize(boneIdx + 1); }
			
			m_boneNodeIndices[boneIdx] = nodeIdx;
		}
	}

	// 当たり判定用ノードが1つも見つからなければ、m_drawMeshNodeと同じ割り当てを行い
	// 見た目 = 当たり判定となる
	if (!m_collisionMeshNodeIndices.size())
	{
		m_collisionMeshNodeIndices = m_drawMeshNodeIndices;
	}
}

// マテリアル作成
void KdModelData::CreateMaterials(const std::shared_ptr<KdGLTFModel>& spGltfModel, const std::string& fileDir)
{
	//マテリアル配列を受け取れるサイズのメモリを確保
	m_materials.resize(spGltfModel->Materials.size());

	for (UINT i = 0; i < m_materials.size(); ++i)
	{
		// src = sourceの略
		// dst = destinationの略
		const KdGLTFMaterial& rSrcMaterial = spGltfModel->Materials[i];
		KdMaterial& rDstMaterial = m_materials[i];

		// 名前
		rDstMaterial.m_name = rSrcMaterial.Name;

		// 各テクスチャの設定
		rDstMaterial.SetTextures(fileDir, rSrcMaterial.BaseColorTexName,
			rSrcMaterial.MetallicRoughnessTexName, rSrcMaterial.EmissiveTexName, rSrcMaterial.NormalTexName);

		// 基本色
		rDstMaterial.m_baseColorRate = rSrcMaterial.BaseColor;

		// 金属性・粗さ
		rDstMaterial.m_metallicRate = rSrcMaterial.Metallic;
		rDstMaterial.m_roughnessRate = rSrcMaterial.Roughness;

		// 自己発光・エミッシブ
		rDstMaterial.m_emissiveRate = rSrcMaterial.Emissive;
	}
}

// アニメーション作成
void KdModelData::CreateAnimations(const std::shared_ptr<KdGLTFModel>& spGltfModel)
{
	// アニメーションデータ
	m_spAnimations.resize(spGltfModel->Animations.size());

	for (UINT i = 0; i < m_spAnimations.size(); ++i)
	{
		const KdGLTFAnimationData& rSrcAnimation = *spGltfModel->Animations[i];

		m_spAnimations[i] = std::make_shared<KdAnimationData>();
		KdAnimationData& rDstAnimation = *(m_spAnimations[i]);

		rDstAnimation.m_name = rSrcAnimation.m_name;

		rDstAnimation.m_maxLength = rSrcAnimation.m_maxLength;

		rDstAnimation.m_nodes.resize(rSrcAnimation.m_nodes.size());

		for (UINT j = 0; j < rDstAnimation.m_nodes.size(); ++j)
		{
			rDstAnimation.m_nodes[j].m_nodeOffset = rSrcAnimation.m_nodes[j]->m_nodeOffset;
			rDstAnimation.m_nodes[j].m_translations = rSrcAnimation.m_nodes[j]->m_translations;
			rDstAnimation.m_nodes[j].m_rotations = rSrcAnimation.m_nodes[j]->m_rotations;
			rDstAnimation.m_nodes[j].m_scales = rSrcAnimation.m_nodes[j]->m_scales;
		}
	}
}

// アニメーションデータ取得：文字列検索
const std::shared_ptr<KdAnimationData> KdModelData::GetAnimation(std::string_view animName) const
{
	for (auto&& anim : m_spAnimations)
	{
		if (anim->m_name == animName)
		{
			return anim;
		}
	}

	return nullptr;
}

// アニメーションデータ取得：番号指定
const std::shared_ptr<KdAnimationData> KdModelData::GetAnimation(UINT index) const
{
	return index >= m_spAnimations.size() ? nullptr : m_spAnimations[index];
}

// 解放
void KdModelData::Release()
{
	m_materials.clear();
	m_originalNodes.clear();

	m_rootNodeIndices.clear();
	m_boneNodeIndices.clear();
	m_meshNodeIndices.clear();
	m_drawMeshNodeIndices.clear();
}

bool KdModelData::IsSkinMesh()
{
	for (auto& node : m_originalNodes)
	{
		if (node.m_isSkinMesh) { return true; }
	}

	return false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
//
// バイナリキャッシュ(.kdmcache)
//
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
namespace
{
	// キャッシュファイルの形式が変わったら上げる(バージョン不一致時は再生成する)
	constexpr UINT32 kKdmCacheMagic = 0x4B444D43;	// 'KDMC'
	// v2: 回転キーの16bit量子化 + ファイル全体の汎用圧縮(KdBinaryCache)を追加
	// v3: 回転キーの保存対象を m_val → m_quat へ修正
	//     (v2は補間に使われない m_val を読み書きしていたため、キャッシュ経由で
	//      ロードすると全ての回転が単位クォータニオンになりポーズが崩れていた)
	constexpr UINT32 kKdmCacheVersion = 3;

	// 回転(クォータニオン)キーはノード数×アニメーション数ぶん大量に存在し
	// キャッシュ容量の大半を占めるため、各成分をint16(-1.0〜1.0を量子化)で
	// 保存する(float4=16byte → int16x4=8byte、時間を含めても半分程度になる)。
	// クォータニオンは正規化されているため各成分は必ず[-1,1]に収まり、
	// 量子化しても見た目上の誤差はほぼ出ない
	void WriteRotationKeysQuantized(KdBinaryCache::ByteWriter& w, const std::vector<KdAnimKeyQuaternion>& keys)
	{
		w.WritePOD(static_cast<UINT32>(keys.size()));

		auto quantize = [](float v) -> INT16
			{
				const float c = std::clamp(v, -1.0f, 1.0f);
				return static_cast<INT16>(std::lround(c * 32767.0f));
			};

		for (const auto& key : keys)
		{
			w.WritePOD(key.m_time);

			// ★補間で実際に使われるのは m_quat なので、こちらを保存する
			w.WritePOD(quantize(key.m_quat.x));
			w.WritePOD(quantize(key.m_quat.y));
			w.WritePOD(quantize(key.m_quat.z));
			w.WritePOD(quantize(key.m_quat.w));
		}
	}

	bool ReadRotationKeysQuantized(KdBinaryCache::ByteReader& r, std::vector<KdAnimKeyQuaternion>& keys)
	{
		UINT32 count = 0;
		if (!r.ReadPOD(count)) { return false; }

		// 1キーあたりの実バイト数 = m_time(4) + INT16 x 4(8)
		constexpr size_t kBytesPerKey = sizeof(float) + sizeof(INT16) * 4;

		// resizeの前に必ず範囲チェックする(壊れたデータでcountが巨大だとbad_allocになる)
		if (count != 0 && count > r.GetRemain() / kBytesPerKey) { return false; }

		keys.clear();
		if (!KdBinaryCache::TryResizeVector(keys, count)) { return false; }

		for (auto& key : keys)
		{
			if (!r.ReadPOD(key.m_time)) { return false; }

			INT16 qx = 0, qy = 0, qz = 0, qw = 0;
			if (!r.ReadPOD(qx)) { return false; }
			if (!r.ReadPOD(qy)) { return false; }
			if (!r.ReadPOD(qz)) { return false; }
			if (!r.ReadPOD(qw)) { return false; }

			key.m_quat.x = qx / 32767.0f;
			key.m_quat.y = qy / 32767.0f;
			key.m_quat.z = qz / 32767.0f;
			key.m_quat.w = qw / 32767.0f;

			// 量子化誤差でわずかに正規化が崩れるため、ここで正規化し直す
			// (Slerpは非正規化クォータニオンを渡すと補間結果が歪む)
			key.m_quat.Normalize();
		}
		return true;
	}
}

// filename に対応するキャッシュパスを返す
std::string KdModelData::GetCachePath(std::string_view filename)
{
	return std::string(filename) + ".kdmcache";
}

// キャッシュから読み込む(成功:true)
bool KdModelData::TryLoadFromCache(std::string_view filename)
{
	namespace fs = std::filesystem;

	const std::string srcPath = std::string(filename);
	const std::string cachePath = GetCachePath(filename);

	std::error_code ec;
	if (!fs::exists(cachePath, ec) || ec) { return false; }
	if (!fs::exists(srcPath, ec) || ec) { return false; }

	// 元のglTF/binより古いキャッシュは無効(手動でモデルを更新した場合の対策)
	const auto srcWriteTime = fs::last_write_time(srcPath, ec);
	if (ec) { return false; }
	const auto cacheWriteTime = fs::last_write_time(cachePath, ec);
	if (ec) { return false; }
	if (cacheWriteTime < srcWriteTime) { return false; }

	// ファイル全体を読み込み、展開する(圧縮されていなければそのまま返る)
	std::vector<uint8_t> buffer;
	if (!KdBinaryCache::LoadCompressedFile(cachePath, buffer)) { return false; }

	KdBinaryCache::ByteReader r(buffer.data(), buffer.size());

	UINT32 magic = 0, version = 0;
	if (!r.ReadPOD(magic) || magic != kKdmCacheMagic) { return false; }
	if (!r.ReadPOD(version) || version != kKdmCacheVersion) { return false; }

	// ----- ノード -----
	UINT32 nodeCount = 0;
	if (!r.ReadPOD(nodeCount)) { return false; }

	m_originalNodes.clear();
	m_originalNodes.resize(nodeCount);

	m_rootNodeIndices.clear();
	m_boneNodeIndices.clear();
	m_meshNodeIndices.clear();
	m_drawMeshNodeIndices.clear();
	m_collisionMeshNodeIndices.clear();

	for (UINT32 i = 0; i < nodeCount; ++i)
	{
		Node& n = m_originalNodes[i];

		if (!r.ReadString(n.m_name)) { return false; }
		if (!r.ReadPOD(n.m_localTransform)) { return false; }
		if (!r.ReadPOD(n.m_worldTransform)) { return false; }
		if (!r.ReadPOD(n.m_boneInverseWorldMatrix)) { return false; }
		if (!r.ReadPOD(n.m_parent)) { return false; }
		if (!r.ReadVector(n.m_children)) { return false; }
		if (!r.ReadPOD(n.m_boneIndex)) { return false; }
		if (!r.ReadPOD(n.m_isSkinMesh)) { return false; }

		UINT8 hasMesh = 0;
		if (!r.ReadPOD(hasMesh)) { return false; }

		if (hasMesh)
		{
			std::vector<KdMeshVertex> vertices;
			std::vector<KdMeshFace>   faces;
			std::vector<KdMeshSubset> subsets;
			if (!r.ReadVector(vertices)) { return false; }
			if (!r.ReadVector(faces))    { return false; }
			if (!r.ReadVector(subsets))  { return false; }

			n.m_spMesh = std::make_shared<KdMesh>();
			n.m_spMesh->Create(vertices, faces, subsets, n.m_isSkinMesh);

			m_meshNodeIndices.push_back(i);
		}

		// CreateNodes()と同じ基準でIndexリストを再構築する
		if (n.m_name.find("COL") != std::string::npos)
		{
			m_collisionMeshNodeIndices.push_back(i);
		}
		else
		{
			m_drawMeshNodeIndices.push_back(i);
		}

		if (n.m_parent == -1) { m_rootNodeIndices.push_back(i); }

		if (n.m_boneIndex >= 0)
		{
			if (n.m_boneIndex >= static_cast<int>(m_boneNodeIndices.size()))
			{
				m_boneNodeIndices.resize(n.m_boneIndex + 1);
			}
			m_boneNodeIndices[n.m_boneIndex] = i;
		}
	}

	if (m_collisionMeshNodeIndices.empty())
	{
		m_collisionMeshNodeIndices = m_drawMeshNodeIndices;
	}

	// ----- マテリアル -----
	UINT32 materialCount = 0;
	if (!r.ReadPOD(materialCount)) { return false; }

	m_materials.clear();
	m_materials.resize(materialCount);

	for (UINT32 i = 0; i < materialCount; ++i)
	{
		KdMaterial& mat = m_materials[i];

		if (!r.ReadString(mat.m_name)) { return false; }
		if (!r.ReadPOD(mat.m_baseColorRate)) { return false; }
		if (!r.ReadPOD(mat.m_metallicRate)) { return false; }
		if (!r.ReadPOD(mat.m_roughnessRate)) { return false; }
		if (!r.ReadPOD(mat.m_emissiveRate)) { return false; }

		std::string fileDir, baseColName, mtRfColName, emiColName, nmlColName;
		if (!r.ReadString(fileDir))     { return false; }
		if (!r.ReadString(baseColName)) { return false; }
		if (!r.ReadString(mtRfColName)) { return false; }
		if (!r.ReadString(emiColName))  { return false; }
		if (!r.ReadString(nmlColName))  { return false; }

		// テクスチャ本体はKdMaterial::SetTextures内部のKdAssetsキャッシュ経由で
		// 取得されるため、2回目以降(=既にロード済み)は高速
		mat.SetTextures(fileDir, baseColName, mtRfColName, emiColName, nmlColName);
	}

	// ----- アニメーション -----
	UINT32 animCount = 0;
	if (!r.ReadPOD(animCount)) { return false; }

	m_spAnimations.clear();
	m_spAnimations.resize(animCount);

	for (UINT32 i = 0; i < animCount; ++i)
	{
		m_spAnimations[i] = std::make_shared<KdAnimationData>();
		KdAnimationData& anim = *m_spAnimations[i];

		if (!r.ReadString(anim.m_name)) { return false; }
		if (!r.ReadPOD(anim.m_maxLength)) { return false; }

		UINT32 animNodeCount = 0;
		if (!r.ReadPOD(animNodeCount)) { return false; }
		anim.m_nodes.resize(animNodeCount);

		for (UINT32 j = 0; j < animNodeCount; ++j)
		{
			KdAnimationData::Node& an = anim.m_nodes[j];
			if (!r.ReadPOD(an.m_nodeOffset)) { return false; }
			if (!r.ReadVector(an.m_translations)) { return false; }
			if (!ReadRotationKeysQuantized(r, an.m_rotations)) { return false; }
			if (!r.ReadVector(an.m_scales))       { return false; }
		}
	}

	return true;
}

// キャッシュへ保存する
void KdModelData::SaveCache(std::string_view filename, const std::shared_ptr<KdGLTFModel>& spGltfModel, const std::string& fileDir) const
{
	const std::string cachePath = GetCachePath(filename);

	KdBinaryCache::ByteWriter w;

	w.WritePOD(kKdmCacheMagic);
	w.WritePOD(kKdmCacheVersion);

	// ----- ノード -----
	w.WritePOD(static_cast<UINT32>(m_originalNodes.size()));

	for (UINT32 i = 0; i < m_originalNodes.size(); ++i)
	{
		const Node& n = m_originalNodes[i];

		w.WriteString(n.m_name);
		w.WritePOD(n.m_localTransform);
		w.WritePOD(n.m_worldTransform);
		w.WritePOD(n.m_boneInverseWorldMatrix);
		w.WritePOD(n.m_parent);
		w.WriteVector(n.m_children);
		w.WritePOD(n.m_boneIndex);
		w.WritePOD(n.m_isSkinMesh);

		const UINT8 hasMesh = n.m_spMesh ? 1 : 0;
		w.WritePOD(hasMesh);

		if (hasMesh)
		{
			// KdMeshの公開アクセサでは頂点のUV/色/接線/スキン情報までは
			// 復元できないため、元データ(spGltfModel)側からそのまま保存する
			// (CreateNodes()はNodes[i]を1:1で処理しているため添字が対応する)
			const KdGLTFNode& srcNode = spGltfModel->Nodes[i];
			w.WriteVector(srcNode.Mesh.Vertices);
			w.WriteVector(srcNode.Mesh.Faces);
			w.WriteVector(srcNode.Mesh.Subsets);
		}
	}

	// ----- マテリアル -----
	w.WritePOD(static_cast<UINT32>(m_materials.size()));

	for (UINT32 i = 0; i < m_materials.size(); ++i)
	{
		const KdMaterial& mat = m_materials[i];
		w.WriteString(mat.m_name);
		w.WritePOD(mat.m_baseColorRate);
		w.WritePOD(mat.m_metallicRate);
		w.WritePOD(mat.m_roughnessRate);
		w.WritePOD(mat.m_emissiveRate);

		const KdGLTFMaterial& src = spGltfModel->Materials[i];
		w.WriteString(fileDir);
		w.WriteString(src.BaseColorTexName);
		w.WriteString(src.MetallicRoughnessTexName);
		w.WriteString(src.EmissiveTexName);
		w.WriteString(src.NormalTexName);
	}

	// ----- アニメーション -----
	w.WritePOD(static_cast<UINT32>(m_spAnimations.size()));

	for (const auto& spAnim : m_spAnimations)
	{
		w.WriteString(spAnim->m_name);
		w.WritePOD(spAnim->m_maxLength);
		w.WritePOD(static_cast<UINT32>(spAnim->m_nodes.size()));

		for (const auto& an : spAnim->m_nodes)
		{
			w.WritePOD(an.m_nodeOffset);
			w.WriteVector(an.m_translations);
			WriteRotationKeysQuantized(w, an.m_rotations);
			w.WriteVector(an.m_scales);
		}
	}

	// ファイル全体を汎用圧縮した上で保存する(失敗しても致命的ではないため
	// 戻り値は見ない。次回はキャッシュ無し扱いで通常ロードされるだけ)
	KdBinaryCache::SaveCompressedFile(cachePath, w.GetBuffer());
}


// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 
// KdModelWork
// 
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
const KdModelData::Node* KdModelWork::FindDataNode(std::string_view name) const
{
	if (m_spData == nullptr) { return nullptr; }

	return m_spData->FindNode(name.data());
}

// ノード検索：文字列
const KdModelWork::Node* KdModelWork::FindNode(std::string_view name) const
{
	for (auto&& node : m_coppiedNodes)
	{
		if (node.m_name == name.data())
		{
			return &node;
		}
	}

	return nullptr;
}

// 可変ノード検索：文字列
KdModelWork::Node* KdModelWork::FindWorkNode(std::string_view name)
{
	for (auto&& node : m_coppiedNodes)
	{
		if (node.m_name == name.data())
		{
			m_needCalcNode = true;

			return &node;
		}
	}

	return nullptr;
}

// モデル設定：コピーノードの生成
void KdModelWork::SetModelData(const std::shared_ptr<KdModelData>& rModel)
{ 
	m_spData = rModel;

	size_t nodeSize = rModel->GetOriginalNodes().size();

	m_coppiedNodes.resize(nodeSize);

	// ノードのコピーを生成
	for (UINT i = 0; i < nodeSize; ++i)
	{
		m_coppiedNodes[i].copy(rModel->GetOriginalNodes()[i]);
	}

	m_needCalcNode = true;
}

void KdModelWork::SetModelData(std::string_view fileName)
{
	// モデルのセット
	SetModelData(KdAssets::Instance().m_modeldatas.GetData(fileName));
}

// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// ルートノードから各ノードの行列を計算していく
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
void KdModelWork::CalcNodeMatrices()
{
	if (!m_spData) { assert(0 && "モデルのないノード行列計算"); return; }

	// 全ボーン行列を書き込み
	for (auto&& nodeIdx : m_spData->GetRootNodeIndices())
	{
		recCalcNodeMatrices(nodeIdx);
	}

	m_needCalcNode = false;
}

// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// ノード行列計算用の再起用関数
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
void KdModelWork::recCalcNodeMatrices(int nodeIdx, int parentNodeIdx)
{
	auto& data = m_spData->GetOriginalNodes()[nodeIdx];
	auto& work = m_coppiedNodes[nodeIdx];

	// 親との行列を合成
	if (parentNodeIdx >= 0)
	{
		auto& parent = m_coppiedNodes[data.m_parent];

		work.m_worldTransform = work.m_localTransform * parent.m_worldTransform;
	}
	// 親が居ない場合は親は自分自身とする
	else
	{
		work.m_worldTransform = work.m_localTransform;
	}

	for (auto childNodeIdx : data.m_children)
	{
		recCalcNodeMatrices(childNodeIdx, nodeIdx);
	}
}
