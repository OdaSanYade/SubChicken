#include "ShaderRender.h"

#include "Shaders/ShaderRuntime.h"
#include "Particle/GPUParticleSystem.h"
#include "Pipelines/PipelineSystem.h"
#include "Instance/InstancedSceneRenderer.h"
#include "TrailRender/TrailRenderer.h"
#include "GhostRender/GhostRenderer.h"
#include "X-RayRender/X-RayRender.h"

#include "Material/MaterialLibrary.h"
#include "Shaders/ShaderSystemHelper.h"
#include "Pipelines/Assets/MultiPassRuntime.h"

using Microsoft::WRL::ComPtr;

namespace
{
	// 共有インスタンスの実体
	std::shared_ptr<ShaderRender> g_shaderRender;
}

void ShaderRenderSystem::Create()
{
	if (g_shaderRender) { return; }

	g_shaderRender = std::make_shared<ShaderRender>();
}

const std::shared_ptr<ShaderRender>& ShaderRenderSystem::Get()
{
	return g_shaderRender;
}

void ShaderRenderSystem::Destroy()
{
	g_shaderRender.reset();
}

void   ShaderRenderSystem::InitRuntime() { Create(); g_shaderRender->InitRuntime(); }
size_t ShaderRenderSystem::PrepareCompile() { return g_shaderRender->PrepareCompile(); }
bool   ShaderRenderSystem::CompileSlot(size_t i) { return g_shaderRender->CompileSlot(i); }
void   ShaderRenderSystem::CommitCompile() { g_shaderRender->CommitCompile(); }
size_t ShaderRenderSystem::PrepareLayout() { return g_shaderRender->PrepareLayout(); }
bool   ShaderRenderSystem::LayoutSlot(size_t i) { return g_shaderRender->LayoutSlot(i); }
void   ShaderRenderSystem::InitPipeline() { g_shaderRender->InitPipeline(); }
size_t ShaderRenderSystem::PrepareMesh() { return g_shaderRender->PrepareMesh(); }
bool   ShaderRenderSystem::MeshSlot(size_t i) { return g_shaderRender->MeshSlot(i); }
void   ShaderRenderSystem::CommitMesh() { g_shaderRender->CommitMesh(); }

DirectX::BoundingSphere CalcModelBoundingSphere(const std::shared_ptr<KdModelData>& model)
{
	DirectX::BoundingSphere result;
	bool first = true;

	for (auto& node : model->GetOriginalNodes())
	{
		if (!node.m_spMesh) continue;
		const DirectX::BoundingSphere& bs = node.m_spMesh->GetBoundingSphere();

		if (first)
		{
			result = bs;
			first = false;
		}
		else
		{
			DirectX::BoundingSphere::CreateMerged(result, result, bs);
		}
	}

	if (first)
	{
		result.Center = { 0, 0, 0 };
		result.Radius = 1.0f;
	}

	return result;
}

// スキンモデルのボーン行列を作る
static int CalcBoneMatrices(const std::shared_ptr<KdModelWork>& work,
	std::vector<DirectX::XMFLOAT4X4>& out)
{
	if (!work) return 0;
	auto data = work->GetData();
	if (!data || !data->IsSkinMesh()) return 0;

	if (work->NeedCalcNodeMatrices())
		work->CalcNodeMatrices();

	auto& workNodes = work->GetNodes();
	auto& dataNodes = data->GetOriginalNodes();

	// 実際に使う本数を求める
	int count = 0;
	for (auto& nodeIdx : data->GetBoneNodeIndices())
	{
		const int bi = dataNodes[nodeIdx].m_boneIndex;
		if (bi >= 0 && bi + 1 > count) count = bi + 1;
	}
	if (count > InstancedSceneRenderer::kBonesPerInstance)
		count = InstancedSceneRenderer::kBonesPerInstance;
	if (count <= 0) return 0;

	DirectX::XMFLOAT4X4 ident;
	DirectX::XMStoreFloat4x4(&ident, DirectX::XMMatrixIdentity());
	out.assign((size_t)count, ident);

	for (auto& nodeIdx : data->GetBoneNodeIndices())
	{
		const int bi = dataNodes[nodeIdx].m_boneIndex;
		if (bi < 0 || bi >= count) continue;
		Math::Matrix b = dataNodes[nodeIdx].m_boneInverseWorldMatrix
			* workNodes[nodeIdx].m_worldTransform;
		DirectX::XMStoreFloat4x4(&out[bi], b);
	}
	return count;
}

ShaderRender::ShaderRender()
{
}

ShaderRender::~ShaderRender()
{
	if (m_runtime) { m_runtime->StopNetwork(); }

	// リリース版は常に設定ファイルの値で始まる
	if (m_liveLinkEnabled && m_pipelines && m_pipelines->Material())
	{
		//m_pipelines->Material()->SaveLastParams();
	}

	// GPU リソースはデバイスより先に解放する
	m_scene.reset();
	m_trails.reset();
	m_ghosts.reset();
	m_xray.reset();
	m_pipelines.reset();
	m_runtime.reset();
	if (m_gpuReady)
	{
		GPUParticleSystem::Instance().Release();
	}
	ShaderHelper::ReleaseAll();
}

// ----------------------------------------------------------
// フレーム情報を作る
// ----------------------------------------------------------
void ShaderRender::BuildFrameContext(const std::shared_ptr<KdCamera>& camera,
	ID3D11RenderTargetView* rtv)
{
	FrameContext& f = m_frame;

	f.view = camera->GetCameraViewMatrix();
	f.proj = camera->GetProjMatrix();
	f.viewProj = f.view * f.proj;
	f.invView = DirectX::XMMatrixInverse(nullptr, f.view);

	// カメラ位置
	DirectX::XMMATRIX camWorld = camera->GetCameraMatrix();
	f.camPos.x = DirectX::XMVectorGetX(camWorld.r[3]);
	f.camPos.y = DirectX::XMVectorGetY(camWorld.r[3]);
	f.camPos.z = DirectX::XMVectorGetZ(camWorld.r[3]);

	// 近い面・遠い面は射影行列から逆算する
	DirectX::XMFLOAT4X4 p;
	DirectX::XMStoreFloat4x4(&p, f.proj);
	if (fabsf(p._33) > 1e-6f && fabsf(p._33 - 1.0f) > 1e-6f && p._34 > 0.5f)
	{
		const float n = -p._43 / p._33;
		const float fz = p._33 * n / (p._33 - 1.0f);
		if (n > 0.0f && fz > n)
		{
			f.nearZ = n;
			f.farZ = fz;
		}
	}

	// 描画先の実サイズ（取れなければビューポート → 最後に既定値）
	bool sized = false;
	if (rtv)
	{
		Microsoft::WRL::ComPtr<ID3D11Resource> res;
		rtv->GetResource(&res);
		Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;
		if (res && SUCCEEDED(res.As(&tex)))
		{
			D3D11_TEXTURE2D_DESC d = {};
			tex->GetDesc(&d);
			if (d.Width > 0 && d.Height > 0)
			{
				f.width = (int)d.Width;
				f.height = (int)d.Height;
				sized = true;
			}
		}
	}
	if (!sized)
	{
		UINT n = 1;
		D3D11_VIEWPORT vp = {};
		KdDirect3D::Instance().WorkDevContext()->RSGetViewports(&n, &vp);
		if (n > 0 && vp.Width >= 1.0f && vp.Height >= 1.0f)
		{
			f.width = (int)vp.Width;
			f.height = (int)vp.Height;
		}
	}

	f.time = Time::Instance().GetTime();
	f.dt = Time::Instance().GetDeltaTime();
}

//シェーダ描画処理
void ShaderRender::Render(const std::shared_ptr<KdCamera>& _camera, const Math::Vector3& _lightDir)
{
	m_stats = {};
	if (!_camera || !m_runtime || !m_pipelines) return;

	Math::Vector3 copyLight = _lightDir;
	copyLight.Normalize();

	//安全に描画する
	auto ctx = KdDirect3D::Instance().WorkDevContext();
	ID3D11RenderTargetView* rtv = nullptr; ID3D11DepthStencilView* dsv = nullptr;
	ctx->OMGetRenderTargets(1, &rtv, &dsv);

	//カメラと画面（このフレームの値を1回だけ作る）
	BuildFrameContext(_camera, rtv);
	const FrameContext& f = m_frame;
	const DirectX::XMMATRIX& view = f.view;
	const DirectX::XMMATRIX& viewProj = f.viewProj;
	const DirectX::XMFLOAT3& camPos = f.camPos;
	const int screenW = f.width;
	const int screenH = f.height;

	m_runtime->SetRaymarchCamera(view, f.proj, camPos);

	m_pipelines->SetCamera(camPos, viewProj);
	//平行光
	m_pipelines->SetParam("LightDir", copyLight.x, copyLight.y, copyLight.z, 1.0f);

	// シャドウマップを描く
	if (m_scene)
	{
		m_scene->EndFrame();
		m_scene->Cull(ctx, viewProj, camPos);
	}
	// EndFrameでこのフレームのインスタンス数が決まってから判定する
	const bool sceneReady = (m_scene && m_scene->IsReady());

	static_assert(kActiveCascades >= 1 && kActiveCascades <= ShaderHelper::kCascadeCount,
		"kActiveCascades must be 1..kCascadeCount");
	const float kCascadeRadius[ShaderHelper::kCascadeCount] = { 80.0f, 50.0f, 80.0f };
	const int kCascadeSize[ShaderHelper::kCascadeCount] = { 2048, 1024, 512 };
	DirectX::XMMATRIX cascadeVP[ShaderHelper::kCascadeCount];
	float cascadeDist[ShaderHelper::kCascadeCount] = {};
	for (int c = 0; c < kActiveCascades; ++c)
	{
		cascadeVP[c] = ShaderHelper::MakeLightViewProj(
			copyLight, camPos, kCascadeRadius[c]);
		cascadeDist[c] = kCascadeRadius[c];

		//ライト視錐台
		LightFrustum lf;
		lf.Build(cascadeVP[c]);

		if (sceneReady)
			m_scene->CullShadow(ctx, cascadeVP[c], camPos);

		m_runtime->RenderShadowMapTo(ctx, ShaderHelper::cascadeDSV[c].Get(),
			kCascadeSize[c], cascadeVP[c],
			[&](const DirectX::XMMATRIX& lvp) {
				// 単体の静的オブジェクト
				for (auto& m : m_staticList)
				{
					m_runtime->SetShadowMatrix(ctx, m.m_World * lvp);
					DrawShadowCulled(m, lf);
				}
				// GPU駆動の影
				m_runtime->SetShadowMatrix(ctx, lvp);
				if (sceneReady)
					m_scene->DrawAllShadow(ctx);
			});
	}
	for (int c = kActiveCascades; c < ShaderHelper::kCascadeCount; ++c)
	{
		cascadeVP[c] = cascadeVP[0];
		cascadeDist[c] = cascadeDist[0];
	}
	m_pipelines->Material()->SetCascades(cascadeVP,
		DirectX::XMFLOAT3(cascadeDist[0], cascadeDist[1], cascadeDist[2]));

	// 点光源
	m_pipelines->Material()->SetLights(m_lightList);

	//フラスタム
	DirectX::BoundingFrustum frustum(f.proj);
	frustum.Transform(frustum, f.invView);

	m_pipelines->RenderMaterial(f.time, 0, 0, 0,
		Math::Matrix::Identity, Math::Matrix::Identity, Math::Matrix::Identity,
		screenW, screenH,
		[&]() {
			// 単体の静的オブジェクト
			for (auto& m : m_staticList)
			{
				DirectX::BoundingSphere ws;
				m.m_BoundingSphere.Transform(ws, m.m_World);
				if (frustum.Contains(ws) == DirectX::DISJOINT) continue;

				m_pipelines->UseStaticVS();
				m_pipelines->UpdateWorldMatrix(
					m.m_World * viewProj, m.m_World * view, m.m_World);

				// マテリアル指定があり、パラメータを持たないものだけ PS を差し替える
				bool overridden = false;
				if (m.m_MaterialId != 0 && !m.m_HasParams)
				{
					const MaterialCacheEntry* me = GetMaterialEntry(m.m_MaterialId);
					if (me && me->ps)
					{
						m_pipelines->Material()->OverrideMaterialPS(me->ps, &me->params);
						overridden = true;
					}
				}

				Draw3DObjectCulled(m, frustum);

				if (overridden)
					m_pipelines->Material()->RestoreMaterialPS();
			}

			if (!sceneReady) return;

			m_pipelines->UpdateWorldMatrix(viewProj, view, Math::Matrix::Identity);
			m_scene->DrawAll(ctx,
				[&](const std::shared_ptr<KdModelData>& spModel, int materialNo)
				{
					// 材質とテクスチャの設定
					auto& materials = spModel->GetMaterials();
					if (materialNo < 0 || materialNo >= (int)materials.size()) return;
					const KdMaterial& mat = materials[materialNo];
					ID3D11ShaderResourceView* baseSRV =
						mat.m_baseColorTex ? mat.m_baseColorTex->WorkSRView()
						: KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
					ctx->PSSetShaderResources(6, 1, &baseSRV);
					ID3D11ShaderResourceView* mrSRV =
						mat.m_metallicRoughnessTex ? mat.m_metallicRoughnessTex->WorkSRView()
						: KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
					ctx->PSSetShaderResources(7, 1, &mrSRV);
					m_pipelines->SetMaterialParams(
						DirectX::XMFLOAT4(mat.m_baseColorRate.x, mat.m_baseColorRate.y,
							mat.m_baseColorRate.z, mat.m_baseColorRate.w),
						mat.m_metallicRate, mat.m_roughnessRate);
				},
				[&](int materialId)
				{
					if (materialId == 0)
					{
						m_pipelines->Material()->RestoreMaterialPS();
						return;
					}
					// マテリアルを持っているのであればピクセルシェーダの切替を行う
					const MaterialCacheEntry* me = GetMaterialEntry((uint32_t)materialId);
					if (!me || !me->ps) { m_pipelines->Material()->EndOverlay(); return; }
					m_pipelines->Material()->OverrideMaterialPS(me->ps, &me->params);
					m_pipelines->Material()->BeginOverlay(me->ps, me->writeMask, &me->params);
				});
			m_pipelines->Material()->EndOverlay();
		});

	auto gbC = m_pipelines->GetGBuffer("GBufferC");
	auto gbE = m_pipelines->GetGBuffer("GBufferE");

	//GPUパーティクルのシミュレーション
	if (m_gpuReady && m_particleStepPending)
	{
		m_particleStepPending = false;
		GPUParticleSystem::Instance().Update(f.dt, f.time,
			viewProj, screenW, screenH,
			m_particleCollision && gbC != nullptr, m_particleRestitution,
			gbC, f.nearZ, f.farZ);
	}

	// 重ね描きの段
	struct RenderStage
	{
		const char* name;
		ID3D11ShaderResourceView* input;
		bool required;
		std::function<void(ID3D11ShaderResourceView*)> draw;
	};
	const RenderStage stages[] =
	{
		{ "xray", gbE, true, [&](ID3D11ShaderResourceView* in)
			{ if (m_xray) m_xray->Render(rtv, viewProj, screenW, screenH, in, camPos); } },
		{ "ghost", gbC, false, [&](ID3D11ShaderResourceView* in)
			{ if (m_ghosts) m_ghosts->Render(rtv, viewProj, screenW, screenH, in, camPos); } },
		{ "trail", gbC, false, [&](ID3D11ShaderResourceView* in)
			{ if (m_trails) m_trails->Render(rtv, viewProj, screenW, screenH, in, camPos); } },
		{ "particle", gbC, false, [&](ID3D11ShaderResourceView* in)
			{ if (m_gpuReady) GPUParticleSystem::Instance().Render(rtv, view, viewProj, screenW, screenH, dsv, in, camPos); } },
	};
	for (const auto& st : stages)
	{
		if (st.required && !st.input) continue;
		st.draw(st.input);
	}

	if (rtv) rtv->Release();
	if (dsv) dsv->Release();

	// デバッグ表示用に数を控える
	m_lastCounts.lights = (int)m_lightList.size();
	m_lastCounts.ghosts = m_ghosts ? m_ghosts->GetEntryCount() : 0;
	m_lastCounts.xray = m_xray ? m_xray->GetEntryCount() : 0;
	m_lastCounts.trailVerts = m_trails ? m_trails->GetVertexCount() : 0;

	//データの解放
	m_staticList.clear();
	m_lightList.clear();

	m_lastCamPos = camPos;
	if (m_ghosts) m_ghosts->BeginFrame();
	if (m_trails) m_trails->BeginFrame();
	if (m_xray)   m_xray->BeginFrame();
}

// ----------------------------------------------------------
// 一括初期化(同期)
// ----------------------------------------------------------
void ShaderRender::Init()
{
	InitRuntime();

	const size_t assetCount = PrepareCompile();
	for (size_t i = 0; i < assetCount; ++i) { CompileSlot(i); }
	CommitCompile();

	const size_t matCount = PrepareLayout();
	for (size_t i = 0; i < matCount; ++i) { LayoutSlot(i); }

	InitPipeline();

	const size_t meshCount = PrepareMesh();
	for (size_t i = 0; i < meshCount; ++i) { MeshSlot(i); }
	CommitMesh();
}
// ----------------------------------------------------------
// ランタイム生成
// ----------------------------------------------------------
void ShaderRender::InitRuntime()
{
	m_runtime = std::make_shared<ShaderRuntime>();
	m_runtime->Init(KdDirect3D::Instance().WorkDev(),
		KdDirect3D::Instance().WorkDevContext());

	// LiveSync用のUDP（エディタからのホットリロード）
#ifdef SHADER_LIVELINK_DISABLED
	m_liveLinkEnabled = false;
#else
	m_liveLinkEnabled = true;
	{
		std::ifstream f("Asset/Data/Shader/LiveLink.json");
		if (f)
		{
			try
			{
				nlohmann::json j;
				f >> j;
				m_liveLinkEnabled = j.value("enabled", true);
			}
			catch (...) {}
		}
	}
#endif

	if (m_liveLinkEnabled)
		m_runtime->StartNetwork(54321);
}

// ----------------------------------------------------------
// コンパイル対象の一覧を作る
// ----------------------------------------------------------
size_t ShaderRender::PrepareCompile()
{
	m_pendingAssetFiles =
		m_runtime->CollectAssetFiles("Asset/Data/Shader/Material");

	return m_pendingAssetFiles.size();
}

// ----------------------------------------------------------
// シェーダ1本をコンパイルして登録する
// ----------------------------------------------------------
bool ShaderRender::CompileSlot(size_t _index)
{
	if (_index >= m_pendingAssetFiles.size()) { return false; }

	return m_runtime->LoadAssetFile(m_pendingAssetFiles[_index]);
}

// ----------------------------------------------------------
void ShaderRender::CommitCompile()
{
	m_pendingAssetFiles.clear();
	m_pendingAssetFiles.shrink_to_fit();

	// 全マテリアルが揃ってから1回だけ構築する
	const auto t0 = std::chrono::steady_clock::now();

	m_matLib = std::make_shared<MaterialLibrary>();
	m_matLib->BuildFromRuntime(m_runtime.get());

	const float ms = std::chrono::duration<float, std::milli>(
		std::chrono::steady_clock::now() - t0).count();

	char buf[128];
	sprintf_s(buf, "[Shader] BuildFromRuntime = %.1f ms\n", ms);
	OutputDebugStringA(buf);
}

size_t ShaderRender::PrepareLayout()
{
	// KDのインプットレイアウト定義
	m_kdLayout = {
		{"POSITION",   0, "float3", 0},
		{"TEXCOORD",   0, "float2", 12},
		{"NORMAL",     0, "float3", 24},
		{"TANGENT",    0, "float3", 36},
		{"SKININDEX",  0, "uint4",  48},
		{"SKINWEIGHT", 0, "float4", 56},
	};

	m_pendingMaterials = m_runtime->GetMaterialNames();

	char buf[128];
	sprintf_s(buf, "[Shader] materials = %zu\n", m_pendingMaterials.size());
	OutputDebugStringA(buf);

	return m_pendingMaterials.size();
}

// ----------------------------------------------------------
bool ShaderRender::LayoutSlot(size_t _index)
{
	if (_index >= m_pendingMaterials.size()) { return false; }

	m_runtime->BuildMaterialLayout(m_pendingMaterials[_index], m_kdLayout);
	return true;
}

// ----------------------------------------------------------
// パイプラインと各レンダラ
// ----------------------------------------------------------
void ShaderRender::InitPipeline()
{
	m_pendingMaterials.clear();
	m_pendingMaterials.shrink_to_fit();

	auto mark = [](const char* _name, auto _t0)
		{
			const float ms = std::chrono::duration<float, std::milli>(
				std::chrono::steady_clock::now() - _t0).count();
			char buf[128];
			sprintf_s(buf, "[Pipeline] %s = %.1f ms\n", _name, ms);
			OutputDebugStringA(buf);
		};

	auto t = std::chrono::steady_clock::now();
	m_pipelines = std::make_shared<PipelineSystem>();
	m_pipelines->Init(KdDirect3D::Instance().WorkDev(),
		KdDirect3D::Instance().WorkDevContext());
	mark("PipelineSystem::Init", t);

	t = std::chrono::steady_clock::now();
	m_pipelines->LoadDefaults(
		"Asset/Data/Shader/Pipeline/Render.pipeline.json");
	mark("LoadDefaults", t);

	t = std::chrono::steady_clock::now();
	m_trails = std::make_shared<TrailRenderer>();
	m_trails->Init();
	m_trails->LoadAssetFolder("Asset/Data/Shader/Trail");
	mark("Trail", t);

	t = std::chrono::steady_clock::now();
	m_ghosts = std::make_shared<GhostRenderer>();
	m_ghosts->Init();
	mark("Ghost", t);

	t = std::chrono::steady_clock::now();
	m_xray = std::make_shared<XRayRenderer>();
	m_xray->Init();
	mark("X-Ray", t);

	t = std::chrono::steady_clock::now();
	auto& effect = GPUParticleSystem::Instance();
	m_gpuReady = effect.Init();
	effect.LoadEffectFolder("Asset/Data/Shader/Particle");
	mark("Particle", t);

	m_runtime->SetPipelineCallback([this](const std::string& json)
		{ m_pipelines->OnPipelineReceived(json); });

	m_runtime->SetParticleCallback([this](const std::string& json)
		{ GPUParticleSystem::Instance().ApplyAssetFromString(json); });

	m_runtime->SetTrailCallback([this](const std::string& json)
		{ if (m_trails) m_trails->ApplyAssetFromString(json); });
}

// ----------------------------------------------------------
// モデル登録の準備
// ----------------------------------------------------------
size_t ShaderRender::PrepareMesh()
{
	auto& mgr = RES_MGR.GetModelList();

	m_scene = std::make_unique<InstancedSceneRenderer>();
	m_modelIdToMeshId.clear();
	m_variantCache.clear();

	// 種別・drawType の上限をモデル数から決める
	const size_t modelCount = mgr->GetModelCount();
	int subsetTotal = 0;
	for (size_t i = 0; i < modelCount; ++i)
	{
		const auto& model = mgr->GetModelById(static_cast<ModelId>(i));
		if (!model) continue;
		for (auto& node : model->GetOriginalNodes())
			if (node.m_spMesh) subsetTotal += (int)node.m_spMesh->GetSubsets().size();
	}
	constexpr int kVariantTypeBudget = 256;
	constexpr int kVariantDrawBudget = 2048;
	const int maxTypes = (int)modelCount * 2 + kVariantTypeBudget;
	const int maxDrawTypes = subsetTotal * 2 + kVariantDrawBudget;

	if (!m_scene->Init(65536, maxTypes, maxDrawTypes))
	{
		m_scene.reset();
		return 0;
	}

	char buf[160];
	sprintf_s(buf, "[Shader] instanced capacity: types=%d drawTypes=%d (models=%zu)\n",
		maxTypes, maxDrawTypes, modelCount);
	OutputDebugStringA(buf);

	// 先に領域を確保しておく
	m_modelIdToMeshId.assign(modelCount, -1);

	return modelCount;
}

// ----------------------------------------------------------
// モデル1件を登録する
// ----------------------------------------------------------
bool ShaderRender::MeshSlot(size_t _index)
{
	auto& mgr = RES_MGR.GetModelList();

	const ModelId id = static_cast<ModelId>(_index);
	const auto& model = mgr->GetModelById(id);
	if (!model) { return false; }

	// ノードごとの境界球をキャッシュ
	auto& nodes = model->GetOriginalNodes();

	std::vector<DirectX::BoundingSphere> spheres;
	spheres.resize(nodes.size());

	for (size_t i = 0; i < nodes.size(); ++i)
	{
		if (nodes[i].m_spMesh)
		{
			spheres[i] = nodes[i].m_spMesh->GetBoundingSphere();
		}
		else
		{
			spheres[i] = { {0, 0, 0}, 0.0f };
		}
	}
	m_nodeSpheres[id] = std::move(spheres);

	// インスタンス描画へ登録
	if (m_scene)
	{
		m_modelIdToMeshId[id] = m_scene->RegisterModelWithLod(model, 8);
	}
	return true;
}

// ----------------------------------------------------------
void ShaderRender::CommitMesh()
{
}

void ShaderRender::Update(const DirectX::XMFLOAT3& camPos)
{
	m_lastCamPos = camPos;
	if (m_scene) m_scene->BeginFrame(camPos);
	if (m_runtime)
	{
		m_runtime->SetTime(Time::Instance().GetTime());
		if (m_liveLinkEnabled) m_runtime->PollNetwork();
	}

	//GPUパーティクルは Render の中で、このフレームのカメラと GBufferC を使って進める
	if (m_gpuReady) m_particleStepPending = true;
}

//描画に必要なデータの追加
void ShaderRender::AddModel(ModelId id, const Math::Matrix& world,
	uint32_t materialId, const DirectX::XMFLOAT4* params, float blend)
{
	if (!m_scene) return;

	InstancedSceneRenderer::InstanceParams ip = {};
	if (params) 
		for (int i = 0; i < 4; ++i) ip.p[i] = params[i];
	ip.p[3].w = blend;

	// 標準マテリアル（全GBufferを書く）
	int base = GetMeshIdFor(id, 0);
	if (base >= 0) m_scene->Submit(base, world, &ip);

	// 上書きマテリアル（マスクされたRTだけ書き換える）
	if (materialId != 0)
	{
		int variant = GetMeshIdFor(id, materialId);
		if (variant >= 0) m_scene->Submit(variant, world, &ip);
	}
}
void ShaderRender::AddWork(WorkId id, uint32_t generation,
	const Math::Matrix& world, uint32_t materialId, const DirectX::XMFLOAT4* params, float blend)
{
	if (!m_scene) return;

	auto& pool = RES_MGR.GetWorkPool();
	auto work = pool->GetShared(id, generation);
	if (!work) return;
	ModelId srcId = pool->GetSrcModelId(id);

	InstancedSceneRenderer::InstanceParams ip = {};
	if (params)
		for (int i = 0; i < 4; ++i) ip.p[i] = params[i];
	ip.p[3].w = blend;

	static std::vector<DirectX::XMFLOAT4X4> bones;
	const int boneCount = CalcBoneMatrices(work, bones);
	const bool skinned = (boneCount > 0);

	// 標準マテリアル
	int boneOffset = -1;
	int base = GetMeshIdFor(srcId, 0);
	if (base >= 0)
	{
		if (skinned) boneOffset = m_scene->SubmitSkinned(base, world, bones.data(), boneCount, &ip);
		else         m_scene->Submit(base, world, &ip);
	}

	//上書きマテリアル（スキンなら本体と同じボーン配列を使い回す）
	if (materialId != 0)
	{
		int variant = GetMeshIdFor(srcId, materialId);
		if (variant >= 0)
		{
			if (skinned) m_scene->SubmitSkinned(variant, world, bones.data(), boneCount, &ip, boneOffset);
			else         m_scene->Submit(variant, world, &ip);
		}
	}
}
void ShaderRender::AddStatic(ModelId id, const Math::Matrix& world,uint32_t materialId, const DirectX::XMFLOAT4* params)
{
	const auto& model = RES_MGR.GetModelList()->GetModelById(id);
	if (!model) return;

	ModelParam mp;
	mp.m_spModel = model;
	mp.m_World = world;
	mp.m_BoundingSphere = CalcModelBoundingSphere(model);
	mp.m_ModelId = id;
	mp.m_MaterialId = materialId;
	if (params)
	{
		for (int i = 0; i < 4; ++i) mp.m_Params[i] = params[i];
		mp.m_HasParams = true;
	}

	// 上書きマテリアル＋パラメータ付き：本体は静的描画（ノードカリング付き）のまま、
	// マテリアルだけインスタンス描画の重ね塗りで出す（AddModel と同じ見え方）。
	// 静的描画の VS はインスタンスパラメータを PS へ渡せないため
	if (materialId != 0 && mp.m_HasParams && m_scene)
	{
		const int variant = GetMeshIdFor(id, materialId);
		if (variant >= 0)
		{
			InstancedSceneRenderer::InstanceParams ip = {};
			for (int i = 0; i < 4; ++i) ip.p[i] = params[i];
			m_scene->Submit(variant, world, &ip);
		}
		else
		{
			mp.m_HasParams = false;   // 重ね塗りできないなら従来どおり PS 差し替え
		}
	}

	m_staticList.push_back(mp);
}
void ShaderRender::AddLight(const PointLightData& _data)
{
	m_lightList.push_back({_data});
}

void ShaderRender::AddGhost(ModelId id, const Math::Matrix& world,
	const DirectX::XMFLOAT4& color,
	const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed)
{
	if (!m_ghosts) return;
	const auto& model = RES_MGR.GetModelList()->GetModelById(id);
	if (model) m_ghosts->Add(model, world, color, rimColor, moveDirSpeed);
}
void ShaderRender::AddGhostSkinned(ModelId id, const Math::Matrix& world,
	const DirectX::XMFLOAT4& color,
	const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed,
	const DirectX::XMFLOAT4X4* bones, int boneCount)
{
	if (!m_ghosts) return;
	const auto& model = RES_MGR.GetModelList()->GetModelById(id);
	if (model) m_ghosts->AddSkinned(model, world, color, rimColor, moveDirSpeed, bones, boneCount);
}

void ShaderRender::AddXRay(ModelId id, const Math::Matrix& world, const DirectX::XMFLOAT4& color, const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed)
{
	if (!m_xray) return;
	const auto& model = RES_MGR.GetModelList()->GetModelById(id);
	if (model) m_xray->Add(model, world, color, rimColor, moveDirSpeed);
}

void ShaderRender::AddXRaySkinned(ModelId id, const Math::Matrix & world, const DirectX::XMFLOAT4 & color, const DirectX::XMFLOAT4 & rimColor, const DirectX::XMFLOAT4 & moveDirSpeed, const DirectX::XMFLOAT4X4 * bones, int boneCount)
{
	if (!m_xray) return;
	const auto& model = RES_MGR.GetModelList()->GetModelById(id);
	if (model) m_xray->AddSkinned(model, world, color, rimColor, moveDirSpeed, bones, boneCount);
}

//パラメータ設定
void ShaderRender::SetRuntimeMaterialParam(const std::string& _materialName, const std::string& _paramName, float x, float y, float z, float w)
{
	m_runtime->SetMaterialParam(_materialName, _paramName, x, y, z, w);
}
void ShaderRender::SetPipelineParam(const std::string& _paramName, float x, float y, float z, float w)
{
	if (!m_pipelines) return;
	m_pipelines->SetParam(_paramName, x, y, z, w);
	m_pipelineAtDefault = false;
}

void ShaderRender::SetDefaultPipelineParam()
{
	// 毎フレーム呼ばれるので、効果から「なし」に切り替わった時だけ戻す
	if (!m_pipelines || m_pipelineAtDefault) return;
	m_pipelines->RestoreDefaultParams();
	m_pipelineAtDefault = true;
}

int ShaderRender::GetMeshIdFor(ModelId modelId, uint32_t materialId)
{
	if (modelId >= m_modelIdToMeshId.size()) return -1;
	int base = m_modelIdToMeshId[modelId];
	if (base < 0 || materialId == 0) return base;

	uint64_t key = ((uint64_t)modelId << 32) | materialId;
	auto it = m_variantCache.find(key);
	if (it != m_variantCache.end()) return it->second;

	int variant = m_scene->RegisterMaterialVariant(base, (int)materialId);
	m_variantCache[key] = variant;
	return variant;
}

void ShaderRender::AddTrail(const TrailComponent& t, const DirectX::XMFLOAT3& camPos)
{
	if (!m_trails || t.count < 2) return;
	m_trails->AddTrail(t, camPos, Time::Instance().GetTime());
}
const ShaderRender::MaterialCacheEntry* ShaderRender::GetMaterialEntry(uint32_t materialId)
{
	if (!m_matLib || !m_runtime || materialId == 0) return nullptr;

	const uint32_t rev = m_runtime->GetMaterialRevision();

	// ライブリンクでマテリアルが増えたら表にも足す
	if (m_materialLibRevision != rev)
	{
		m_matLib->SyncFromRuntime(m_runtime.get());
		m_materialLibRevision = rev;
	}

	if (materialId >= m_matLib->Count()) return nullptr;
	if (m_materialCache.size() < m_matLib->Count())
		m_materialCache.resize(m_matLib->Count());

	MaterialCacheEntry& e = m_materialCache[materialId];
	if (e.revision != rev)
	{
		const std::string& name = m_matLib->GetName(materialId);
		e.ps = m_runtime->GetMaterialPS(name);
		e.writeMask = m_runtime->GetMaterialWriteMask(name);
		e.params.clear();
		m_runtime->GetMaterialParamDefaults(name, e.params);
		e.revision = rev;
	}
	return &e;
}

bool ShaderRender::GetTrailParticleSpawn(TrailId id, std::string& outEffect, float& outChance) const
{
	if (!m_trails) return false;
	return m_trails->GetParticleSpawn(id, outEffect, outChance);
}

void ShaderRender::PlayParticleEffect(const std::string& name, const DirectX::XMFLOAT3& pos)
{
	if (!m_gpuReady) return;
	GPUParticleSystem::Instance().PlayEffect(name, pos);
}

void ShaderRender::Draw3DObjectCulled(const ModelParam& mp,
	const DirectX::BoundingFrustum& frustum)
{
	auto& spModel = mp.m_spModel;
	if (!spModel) return;
	auto ctx = KdDirect3D::Instance().WorkDevContext();
	auto& materials = spModel->GetMaterials();
	auto& nodes = spModel->GetOriginalNodes();

	// キャッシュした境界球を引く
	auto it = m_nodeSpheres.find(mp.m_ModelId);
	const std::vector<DirectX::BoundingSphere>* spheres =
		(it != m_nodeSpheres.end()) ? &it->second : nullptr;

	for (int idx : spModel->GetDrawMeshNodeIndices())
	{
		auto& node = nodes[idx];
		if (!node.m_spMesh) continue;
		m_stats.staticNodesTotal++;

		// ノード単位の視錐台カリング
		if (spheres && idx < (int)spheres->size())
		{
			DirectX::BoundingSphere ws;
			(*spheres)[idx].Transform(ws, mp.m_World);
			if (frustum.Contains(ws) == DirectX::DISJOINT) continue;
		}
		m_stats.staticNodesDrawn++;

		node.m_spMesh->SetToDevice();
		auto& subsets = node.m_spMesh->GetSubsets();
		for (int i = 0; i < (int)subsets.size(); ++i)
		{
			const KdMaterial& mat = materials[subsets[i].MaterialNo];
			ID3D11ShaderResourceView* baseSRV =
				mat.m_baseColorTex ? mat.m_baseColorTex->WorkSRView()
				: KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
			ctx->PSSetShaderResources(6, 1, &baseSRV);
			ID3D11ShaderResourceView* mrSRV =
				mat.m_metallicRoughnessTex ? mat.m_metallicRoughnessTex->WorkSRView()
				: KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
			ctx->PSSetShaderResources(7, 1, &mrSRV);
			m_pipelines->SetMaterialParams(
				DirectX::XMFLOAT4(mat.m_baseColorRate.x, mat.m_baseColorRate.y,
					mat.m_baseColorRate.z, mat.m_baseColorRate.w),
				mat.m_metallicRate, mat.m_roughnessRate);
			node.m_spMesh->DrawSubset(i);

			m_stats.staticDrawCalls++;
		}
	}
}

void ShaderRender::DrawShadowCulled(const ModelParam& mp, const LightFrustum& lf)
{
	auto& spModel = mp.m_spModel;
	if (!spModel) return;
	auto  ctx = KdDirect3D::Instance().WorkDevContext();
	auto& nodes = spModel->GetOriginalNodes();
	auto& materials = spModel->GetMaterials();

	auto it = m_nodeSpheres.find(mp.m_ModelId);
	const std::vector<DirectX::BoundingSphere>* spheres =
		(it != m_nodeSpheres.end()) ? &it->second : nullptr;

	for (int idx : spModel->GetDrawMeshNodeIndices())
	{
		auto& node = nodes[idx];
		if (!node.m_spMesh) continue;
		m_stats.shadowNodesTotal++;

		// ライト視錐台でカリング
		if (spheres && idx < (int)spheres->size())
		{
			DirectX::BoundingSphere ws;
			(*spheres)[idx].Transform(ws, mp.m_World);
			if (!lf.Contains(ws)) continue;
		}
		m_stats.shadowNodesDrawn++;

		node.m_spMesh->SetToDevice();
		auto& subsets = node.m_spMesh->GetSubsets();
		for (int i = 0; i < (int)subsets.size(); ++i)
		{
			// アルファクリップ用にベースカラーを t0 へ
			const KdMaterial& mat = materials[subsets[i].MaterialNo];
			ID3D11ShaderResourceView* baseSRV =
				mat.m_baseColorTex ? mat.m_baseColorTex->WorkSRView()
				: KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
			ctx->PSSetShaderResources(0, 1, &baseSRV);

			node.m_spMesh->DrawSubset(i);
		}
	}
}

void ShaderRender::DrawDebug()
{
	const FrameContext& f = m_frame;
	ImGui::Text("Frame: %d x %d  near %.3f  far %.1f", f.width, f.height, f.nearZ, f.farZ);
	ImGui::Text("Static nodes: %d / %d  (draw calls %d)",
		m_stats.staticNodesDrawn, m_stats.staticNodesTotal, m_stats.staticDrawCalls);
	ImGui::Text("Shadow nodes: %d / %d",
		m_stats.shadowNodesDrawn, m_stats.shadowNodesTotal);

	// 容量の使用状況
	if (ImGui::CollapsingHeader("Render capacity", ImGuiTreeNodeFlags_DefaultOpen))
	{
		auto row = [](const char* name, int used, int cap, int dropped)
			{
				const bool hot = (dropped > 0) || (cap > 0 && used >= cap);
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(name);
				ImGui::TableSetColumnIndex(1);
				if (hot) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d / %d", used, cap);
				else     ImGui::Text("%d / %d", used, cap);
				ImGui::TableSetColumnIndex(2);
				if (dropped > 0) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%d", dropped);
				else             ImGui::TextUnformatted("-");
			};

		if (ImGui::BeginTable("##caps", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
		{
			ImGui::TableSetupColumn("item");
			ImGui::TableSetupColumn("used / max");
			ImGui::TableSetupColumn("dropped");
			ImGui::TableHeadersRow();

			if (m_scene)
			{
				const auto u = m_scene->GetUsage();
				row("instances", u.instances, u.maxInstances, u.droppedInstances);
				row("mesh types", u.types, u.maxTypes, u.droppedTypes);
				row("draw types", u.drawTypes, u.maxDrawTypes, 0);
				row("param sets", u.params, u.maxParams, u.droppedParams);
				row("bone matrices", u.bones, u.maxBones, u.droppedBones);
			}
			if (m_gpuReady)
			{
				const auto p = GPUParticleSystem::Instance().GetStats();
				row("particle emitters", p.activeEmitters, p.maxEmitters, p.droppedEmitters);
				row("particle trails", p.trailEmitters, p.maxEmitters, 0);
				row("particle effects", p.liveEffects, 0, 0);
			}
			if (m_trails)
				row("trail vertices", m_lastCounts.trailVerts, m_trails->GetMaxVertexCount(),
					m_trails->GetDroppedVertexCount());
			if (m_ghosts)
				row("ghosts", m_lastCounts.ghosts, 0, m_ghosts->GetDroppedCount());
			if (m_xray)
				row("x-ray", m_lastCounts.xray, 0, m_xray->GetDroppedCount());
			row("point lights", m_lastCounts.lights, 512, 0);

			ImGui::EndTable();
		}
	}

	if (m_runtime)
	{
		for(const std::string& matName : m_runtime->GetMaterialNames())
			m_runtime->DrawParamUI(matName);
	}
	if (m_pipelines) m_pipelines->DrawDebugUI();
}