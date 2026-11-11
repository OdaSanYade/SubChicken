#pragma once
#include "Pipelines/Assets/LightData.h"

class ShaderRuntime;
class PipelineSystem;
class InstancedSceneRenderer;
class MaterialLibrary;
class TrailRenderer;
class GhostRenderer;
class XRayRenderer;

struct VertexElement;


// カメラ視点の視線台
struct LightFrustum
{
	DirectX::XMFLOAT4 planes[6];

	void Build(const DirectX::XMMATRIX& viewProj)
	{
		DirectX::XMFLOAT4X4 m;
		DirectX::XMStoreFloat4x4(&m, viewProj);
		planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
		planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
		planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
		planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
		planes[4] = { m._13,         m._23,         m._33,         m._43 };
		planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };
		for (int i = 0; i < 6; ++i)
		{
			float len = sqrtf(planes[i].x * planes[i].x
				+ planes[i].y * planes[i].y
				+ planes[i].z * planes[i].z);
			if (len > 0.0001f)
			{
				planes[i].x /= len; planes[i].y /= len;
				planes[i].z /= len; planes[i].w /= len;
			}
		}
	}

	// 球が視錐台の外なら
	bool Contains(const DirectX::BoundingSphere& s) const
	{
		for (int i = 0; i < 6; ++i)
		{
			float d = planes[i].x * s.Center.x
				+ planes[i].y * s.Center.y
				+ planes[i].z * s.Center.z
				+ planes[i].w;
			if (d < -s.Radius) return false;
		}
		return true;
	}
};

//　ゴーストの1フレーム分の情報
struct GhostFrame
{
	Math::Matrix world;
	std::vector<DirectX::XMFLOAT4X4> bones;
	bool skinned = false;
};

//　ゴーストトレイルのデータ
struct GhostTrail
{
	ModelId  modelId = kInvalidModelId;
	uint32_t materialId = 0;
	std::vector<GhostFrame> frames;   // リング
	int head = 0;
	int count = 0;
	float interval = 0.05f;   // 何秒ごとに記録するか
	float timer = 0.0f;
	bool active = false;
};

//モデルデータ
struct ModelParam
{
	std::shared_ptr<KdModelData> m_spModel;
	Math::Matrix                 m_World;
	DirectX::BoundingSphere      m_BoundingSphere;
	ModelId                      m_ModelId = kInvalidModelId;
	uint32_t                     m_MaterialId = 0;
	DirectX::XMFLOAT4            m_Params[4] = {};   // AddStatic で渡されたインスタンスパラメータ
	bool                         m_HasParams = false;
};

// 1フレームぶんのカメラと画面の情報
struct FrameContext
{
	DirectX::XMMATRIX view = DirectX::XMMatrixIdentity();
	DirectX::XMMATRIX proj = DirectX::XMMatrixIdentity();
	DirectX::XMMATRIX viewProj = DirectX::XMMatrixIdentity();
	DirectX::XMMATRIX invView = DirectX::XMMatrixIdentity();
	DirectX::XMFLOAT3 camPos = { 0, 0, 0 };
	int   width = 1280;    // 描画先の実サイズ
	int   height = 720;
	float nearZ = 0.01f;   // 射影行列から求める
	float farZ = 2000.0f;
	float time = 0.0f;
	float dt = 0.0f;
};

struct WorkParam
{
	std::shared_ptr<KdModelWork> m_spModel;
	Math::Matrix                 m_World;
	DirectX::BoundingSphere m_BoundingSphere;
};

class ShaderRender 
{
public:

	ShaderRender();
	~ShaderRender();

	void Render(const std::shared_ptr<KdCamera>& _camera, const Math::Vector3& _lightDir = {-0.5f,1.0f,0.5f});
	void Update(const DirectX::XMFLOAT3& camPos);

	// 初期化分割
	void Init();
	void  InitRuntime();

	size_t PrepareCompile();
	bool   CompileSlot(size_t _index);
	void   CommitCompile();

	size_t PrepareLayout();
	bool   LayoutSlot(size_t _index);

	void   InitPipeline();

	size_t PrepareMesh();
	bool   MeshSlot(size_t _index);
	void   CommitMesh();

	//ImGuiによるデバッグ確認
	void DrawDebug();

	//==========================================================
	//データを受け取る
	//==========================================================
	// 複数オブジェクト
	void AddModel(ModelId id, const Math::Matrix& world,
		uint32_t materialId, const DirectX::XMFLOAT4* params, float blend = 1.0f);

	// スキンメッシュオブジェクト
	void AddWork(WorkId id, uint32_t generation, const Math::Matrix& world, uint32_t materialId, const DirectX::XMFLOAT4* params, float blend = 1.0f);

	// 単体オブジェクト描画
	void AddStatic(ModelId id, const Math::Matrix& world, uint32_t materialId = 0, const DirectX::XMFLOAT4* params = nullptr);

	// ポイントライト
	void AddLight(const PointLightData& _data);

	// ゴースト
	void AddGhost(ModelId id, const Math::Matrix& world,
		const DirectX::XMFLOAT4& color,
		const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed);

	// スキンメッシュ用のゴースト
	void AddGhostSkinned(ModelId id, const Math::Matrix& world,
		const DirectX::XMFLOAT4& color,
		const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed,
		const DirectX::XMFLOAT4X4* bones, int boneCount);

	// X_Ray
	void AddXRay(ModelId id, const Math::Matrix& world,
		const DirectX::XMFLOAT4& color,
		const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed);

	// スキンメッシュ用のX_Ray
	void AddXRaySkinned(ModelId id, const Math::Matrix& world,
		const DirectX::XMFLOAT4& color,
		const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed,
		const DirectX::XMFLOAT4X4* bones, int boneCount);

	// トレイル
	void AddTrail(const TrailComponent& t, const DirectX::XMFLOAT3& camPos);


	//==========================================================
	// トレイル
	//==========================================================
	// トレイルに「点を打った時に出すパーティクル」が設定されていれば true
	bool GetTrailParticleSpawn(TrailId id, std::string& outEffect, float& outChance) const;
	// パーティクルエフェクトをワンショット再生
	void PlayParticleEffect(const std::string& name, const DirectX::XMFLOAT3& pos);

	//==========================================================
	//パラメータ変更API
	//==========================================================
	//マテリアルのパラメータ
	void SetRuntimeMaterialParam(const std::string& _materialName, const std::string& _paramName,
		float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 0.0f);
	//パイプラインパラメータ
	void SetPipelineParam(const std::string& _paramName,
		float x = 0.0f, float y = 0.0f, float z = 0.0f, float w = 0.0f);
	//パイプラインパラメータをデフォルトに戻す
	void SetDefaultPipelineParam();

	//マテリアル指定
	int GetMeshIdFor(ModelId modelId, uint32_t materialId);


	// Update に渡されたカメラ位置（Update が収集より前なら同じフレームの位置）
	const DirectX::XMFLOAT3& GetLastCamPos() const { return m_lastCamPos; }

	// 直近の Render で使ったフレーム情報
	const FrameContext& GetFrameContext() const { return m_frame; }

	// パーティクルの深度衝突（GBufferC を使う）。既定はオフ
	void SetParticleCollision(bool on, float restitution = 0.5f)
	{
		m_particleCollision = on;
		m_particleRestitution = restitution;
	}

	// 影に使うカスケードの段数（1〜3）
	static constexpr int kActiveCascades = 1;

private:

	// フレーム情報を作る（描画先のサイズは rtv から取る）
	void BuildFrameContext(const std::shared_ptr<KdCamera>& camera,
		ID3D11RenderTargetView* rtv);

	//静的オブジェクト用カリング描画
	void Draw3DObjectCulled(const ModelParam& mp,
		const DirectX::BoundingFrustum& frustum);

	//シャドウマップ用カリング描画
	void DrawShadowCulled(const ModelParam& mp, const LightFrustum& lf);

	//シェーダ群
	std::shared_ptr<ShaderRuntime>     m_runtime;
	std::shared_ptr<PipelineSystem>    m_pipelines;
	std::unique_ptr<InstancedSceneRenderer> m_scene;
	std::shared_ptr<MaterialLibrary> m_matLib;
	std::shared_ptr<TrailRenderer> m_trails;
	std::shared_ptr<GhostRenderer> m_ghosts;
	std::shared_ptr<XRayRenderer> m_xray;


	std::unordered_map<uint64_t, int> m_variantCache;

	// マテリアル番号 → PS・書き込みマスク・既定パラメータ
	struct MaterialCacheEntry
	{
		ID3D11PixelShader* ps = nullptr;
		unsigned int writeMask = 0xF;
		std::vector<std::array<float, 4>> params;
		uint32_t revision = ~0u;   // ShaderRuntime::GetMaterialRevision と比べて古ければ引き直す
	};
	std::vector<MaterialCacheEntry> m_materialCache;
	uint32_t m_materialLibRevision = ~0u;
	const MaterialCacheEntry* GetMaterialEntry(uint32_t materialId);
	bool m_gpuReady = false;

	std::vector<std::string> m_pendingMaterials;
	std::vector<std::string> m_pendingAssetFiles;
	std::vector<VertexElement> m_kdLayout;

	//描画対象
	std::vector<ModelParam> m_staticList = {};
	std::unordered_map<ModelId, std::vector<DirectX::BoundingSphere>> m_nodeSpheres;

	//ポイントライト
	std::vector<PointLightData> m_lightList = {};

	//フレーム情報
	FrameContext m_frame;
	DirectX::XMFLOAT3 m_lastCamPos = { 0, 0, 0 };

	// パーティクル（シミュレーションは Render 内で1フレーム1回だけ回す）
	bool  m_particleStepPending = false;
	bool  m_particleCollision = false;
	float m_particleRestitution = 0.5f;

	// ポストエフェクトの既定値を戻したか（戻すのは切り替わった時だけ）
	bool m_pipelineAtDefault = false;

	// ライブリンク（開発用）を使うか
	bool m_liveLinkEnabled = false;

	//描画用モデルID格納
	std::vector<int> m_modelIdToMeshId;
	std::vector<int> m_workIdToMeshId;

	// カリング統計（デバッグ用）
	struct CullStats
	{
		int staticNodesTotal = 0;    // ステージのノード総数
		int staticNodesDrawn = 0;    // 実際に描いたノード数
		int staticDrawCalls = 0;     // DrawSubset の回数
		int shadowNodesTotal = 0;   // シャドウマップのノード総数
		int shadowNodesDrawn = 0;   // シャドウマップに描画されたノード数
	};
	CullStats m_stats;

	// 直近の Render で積まれていた数（Render の最後でリストを空にするので控えておく）
	struct FrameCounts
	{
		int lights = 0;
		int ghosts = 0;
		int xray = 0;
		int trailVerts = 0;
	};
	FrameCounts m_lastCounts;
};

namespace ShaderRenderSystem
{
	// 生成と初期化
	void Create();

	// 取得
	const std::shared_ptr<ShaderRender>& Get();

	// 破棄
	void Destroy();

	// ロードタスクから呼ぶ段階関数
	void   InitRuntime();
	size_t PrepareCompile();
	bool   CompileSlot(size_t _index);
	void   CommitCompile();
	size_t PrepareLayout();
	bool   LayoutSlot(size_t _index);
	void   InitPipeline();
	size_t PrepareMesh();
	bool   MeshSlot(size_t _index);
	void   CommitMesh();
}