#include "MultiPassRuntime.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include "../../Shaders/ShaderSystemHelper.h"

using Microsoft::WRL::ComPtr;

using json = nlohmann::json;

namespace
{
	// ------------------------------------------------------------
	// シェーダバイトコードのディスクキャッシュ
	// ------------------------------------------------------------

	// FNV-1a。std::hash は実装依存で実行環境が変わると値が変わるため自前で持つ
	uint64_t ShaderHash(const std::string& _code, const char* _target)
	{
		uint64_t h = 14695981039346656037ull;

		auto mix = [&h](const char* p, size_t n)
			{
				for (size_t i = 0; i < n; ++i)
				{
					h ^= static_cast<unsigned char>(p[i]);
					h *= 1099511628211ull;
				}
			};

		mix(_target, strlen(_target));
		mix(_code.c_str(), _code.size());
		return h;
	}

	const char* kShaderCacheDir = "Cache/Shader";

	std::string ShaderCachePath(uint64_t _hash, const char* _target)
	{
		char buf[128];
		sprintf_s(buf, "%s/%s_%016llx.cso", kShaderCacheDir, _target,
			static_cast<unsigned long long>(_hash));
		return buf;
	}

	// キャッシュから読む。無ければ空を返す
	std::vector<char> LoadShaderCache(uint64_t _hash, const char* _target)
	{
		std::ifstream f(ShaderCachePath(_hash, _target), std::ios::binary);
		if (!f) { return {}; }

		return std::vector<char>(
			(std::istreambuf_iterator<char>(f)),
			std::istreambuf_iterator<char>());
	}

	void SaveShaderCache(uint64_t _hash, const char* _target,
		const void* _data, size_t _size)
	{
		std::error_code ec;
		std::filesystem::create_directories(kShaderCacheDir, ec);

		std::ofstream f(ShaderCachePath(_hash, _target), std::ios::binary);
		if (!f) { return; }

		f.write(static_cast<const char*>(_data), _size);
	}
}

bool MultiPassRuntime::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
	m_device = device;
	m_context = context;

	if (!CreateFullscreenVS())    return false;
	if (!CreateConstantBuffers()) return false;
	if (!CreateSamplerState())    return false;
	if (!CreateRasterizerState()) return false;
	if (!CreateCopyPS())          return false;
	if (!CreateBlendState()) return false;
	return true;
}

void MultiPassRuntime::Release()
{
	m_passes.clear();
	m_paramIndexDirty = true;
	m_rtPool.clear();
	m_depthPool.clear();
	m_rsCullNone.Reset();
	m_sampler.Reset();
	m_cbParams.Reset();
	m_cbWVP.Reset();
	m_cbTime.Reset();
	m_copyPS.Reset();
	m_alphaBlend.Reset();
	m_captureTex.Reset();
	m_captureSRV.Reset();
	m_fullscreenVS.Reset();
}

// RTプール
int MultiPassRuntime::AcquireRT(int width, int height,
	DXGI_FORMAT format)
{
	// 再利用チェックに format も加える
	for (int i = 0; i < (int)m_rtPool.size(); ++i)
	{
		auto& rt = m_rtPool[i];
		if (!rt.inUse && rt.width == width && rt.height == height && rt.format == format)   // ★format一致も条件に
		{
			rt.inUse = true;
			rt.lastUsedFrame = m_poolFrame;
			return i;
		}
	}
	PooledRT rt;
	rt.width = width; rt.height = height; rt.inUse = true;
	rt.lastUsedFrame = m_poolFrame;
	rt.format = format; 

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width; desc.Height = height;
	desc.MipLevels = 1; desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &rt.tex))) return -1;
	if (FAILED(m_device->CreateRenderTargetView(rt.tex.Get(), nullptr, &rt.rtv))) return -1;
	if (FAILED(m_device->CreateShaderResourceView(rt.tex.Get(), nullptr, &rt.srv))) return -1;

	m_rtPool.push_back(std::move(rt));
	return (int)m_rtPool.size() - 1;
}

void MultiPassRuntime::ResetRTPool()
{
	for (auto& rt : m_rtPool) rt.inUse = false;
}

void MultiPassRuntime::TrimPools(int screenW, int screenH)
{
	++m_poolFrame;

	// 画面サイズが変わったら、前のサイズの RT は全部捨てる
	const bool resized = (m_poolScreenW != 0 &&
		(m_poolScreenW != screenW || m_poolScreenH != screenH));
	m_poolScreenW = screenW;
	m_poolScreenH = screenH;

	auto stale = [&](uint32_t lastUsed)
		{
			return resized || (m_poolFrame - lastUsed) > kPoolEvictFrames;
		};

	const size_t rtBefore = m_rtPool.size();
	const size_t dsBefore = m_depthPool.size();
	m_rtPool.erase(std::remove_if(m_rtPool.begin(), m_rtPool.end(),
		[&](const PooledRT& rt) { return !rt.inUse && stale(rt.lastUsedFrame); }),
		m_rtPool.end());
	m_depthPool.erase(std::remove_if(m_depthPool.begin(), m_depthPool.end(),
		[&](const PooledDepth& d) { return !d.inUse && stale(d.lastUsedFrame); }),
		m_depthPool.end());

	if (m_rtPool.size() != rtBefore || m_depthPool.size() != dsBefore)
	{
		// 前フレームの名前付きバッファは消した RT を指しているかもしれない
		m_namedBufferSRVs.clear();
		char buf[128];
		sprintf_s(buf, "[Pipeline] RT pool trimmed: rt %zu -> %zu, depth %zu -> %zu\n",
			rtBefore, m_rtPool.size(), dsBefore, m_depthPool.size());
		OutputDebugStringA(buf);
	}
}

bool MultiPassRuntime::CreateFullscreenVS()
{
	static const char* kVSSource = R"(
struct VS_OUT
{
    float4 pos    : SV_Position;
    float2 uv     : TEXCOORD0;
    float3 normal : NORMAL;
    float3 viewNormal : NORMAL1;
};
VS_OUT main(uint id : SV_VertexID)
{
    VS_OUT o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    o.normal = float3(0.0f, 0.0f, 0.0f);
    o.viewNormal = float3(0.0f, 0.0f, 0.0f);
    return o;
}
)";
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(kVSSource, strlen(kVSSource), "FSVS", nullptr, nullptr,
		"main", "vs_5_0", 0, 0, &blob, &err)))
	{
		return false;
	}
	return SUCCEEDED(m_device->CreateVertexShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_fullscreenVS));
}

bool MultiPassRuntime::CreateConstantBuffers()
{
	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	// b0: time(4) + audio(12... 実際は4+4+4+4=16) + params[16]*16 = 16 + 256 = 272
	desc.ByteWidth = 16 + 16 * 16;   // ★272バイト（time/audio 16 + params 256）
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cbTime))) return false;

	desc.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 3;
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cbWVP))) return false;

	return true;
}

bool MultiPassRuntime::CreateSamplerState()
{
	D3D11_SAMPLER_DESC desc = {};
	desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	desc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	desc.MaxLOD = D3D11_FLOAT32_MAX;
	return SUCCEEDED(m_device->CreateSamplerState(&desc, &m_sampler));
}

bool MultiPassRuntime::CreateRasterizerState()
{
	D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode = D3D11_FILL_SOLID;
	desc.CullMode = D3D11_CULL_NONE;
	desc.DepthClipEnable = TRUE;
	return SUCCEEDED(m_device->CreateRasterizerState(&desc, &m_rsCullNone));
}


void MultiPassRuntime::UpdateWVPCB(const DirectX::XMMATRIX& wvp,
	const DirectX::XMMATRIX& worldView,
	const DirectX::XMMATRIX& world)
{
	struct CBData {
		DirectX::XMFLOAT4X4 wvp;
		DirectX::XMFLOAT4X4 worldView;
		DirectX::XMFLOAT4X4 world;
	} data;
	DirectX::XMStoreFloat4x4(&data.wvp, wvp);
	DirectX::XMStoreFloat4x4(&data.worldView, worldView);
	DirectX::XMStoreFloat4x4(&data.world, world);

	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbWVP.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbWVP.Get(), 0);
	}
}
void MultiPassRuntime::UpdateEffectCB(float time, float bass, float mid, float treble,
	const std::vector<std::array<float, 4>>& params)
{
	// b0 のレイアウト: time, bass, mid, treble, then float4 params[16]
	struct CBData {
		float time, bass, mid, treble;
		float params[16][4];
	} data = {};
	data.time = time; data.bass = bass; data.mid = mid; data.treble = treble;

	for (int i = 0; i < (int)params.size() && i < 16; ++i)
	{
		data.params[i][0] = params[i][0];
		data.params[i][1] = params[i][1];
		data.params[i][2] = params[i][2];
		data.params[i][3] = params[i][3];
	}

	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbTime.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbTime.Get(), 0);
	}
}

int MultiPassRuntime::AcquireDepth(int width, int height)
{
	for (int i = 0; i < (int)m_depthPool.size(); ++i)
	{
		auto& d = m_depthPool[i];
		if (!d.inUse && d.width == width && d.height == height)
		{
			d.inUse = true;
			d.lastUsedFrame = m_poolFrame;
			return i;
		}
	}
	PooledDepth d;
	d.width = width; d.height = height; d.inUse = true;
	d.lastUsedFrame = m_poolFrame;

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width; desc.Height = height;
	desc.MipLevels = 1; desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &d.tex))) return -1;
	if (FAILED(m_device->CreateDepthStencilView(d.tex.Get(), nullptr, &d.dsv))) return -1;

	m_depthPool.push_back(std::move(d));
	return (int)m_depthPool.size() - 1;
}

void MultiPassRuntime::ResetDepthPool()
{
	for (auto& d : m_depthPool) d.inUse = false;
}

bool MultiPassRuntime::LoadPipeline(const std::string& jsonText)
{
	json root;

	try { root = json::parse(jsonText); }
	catch (...) {
		OutputDebugStringA("LoadPipeline: JSON parse failed\n");
		return false;
	}

	if (!root.contains("passes") || !root["passes"].is_array())
		return false;

	auto parseFormat = [](const std::string& s) -> DXGI_FORMAT {
		if (s == "R8G8B8A8_UNORM")      return DXGI_FORMAT_R8G8B8A8_UNORM;
		if (s == "R16G16B16A16_FLOAT")  return DXGI_FORMAT_R16G16B16A16_FLOAT;
		if (s == "R32G32B32A32_FLOAT")  return DXGI_FORMAT_R32G32B32A32_FLOAT;
		if (s == "R32_FLOAT")           return DXGI_FORMAT_R32_FLOAT;
		if (s == "R16G16_FLOAT")        return DXGI_FORMAT_R16G16_FLOAT;
		return DXGI_FORMAT_R8G8B8A8_UNORM;
		};

	// ★パース後の構築も try で守る
	std::vector<PassRuntime> newPasses;

	try
	{
		for (const auto& pj : root["passes"])
		{
			if (!pj.is_object()) continue;   // 壊れたパスはスキップ

			PassRuntime pass;
			pass.name = pj.value("name", "");
			pass.passType = pj.value("passType", 0);
			pass.scaleFactor = pj.value("scaleFactor", 1.0f);
			pass.outputName = pj.value("outputName", "");
			pass.isMaterial = pj.value("isMaterial", false);
			pass.targetCount = pj.value("targetCount", 1);
			pass.usesPrevPass = pj.value("usesPrevPass", false);

			// targetNames
			if (pj.contains("targetNames"))
				for (const auto& tn : pj["targetNames"])
					pass.targetNames.push_back(tn.get<std::string>());

			// namedBufferRefs
			if (pj.contains("namedBufferRefs"))
				for (const auto& nb : pj["namedBufferRefs"])
					pass.namedBufferRefs.push_back(nb.get<std::string>());

			if (pj.contains("targetFormats"))
			{
				for (auto& f : pj["targetFormats"])
					pass.targetFormats.push_back(parseFormat(f.get<std::string>()));
			}

			// ---- パラメータ ----
			if (pj.contains("params"))
			{
				for (const auto& paramJson : pj["params"])
				{
					ParamRuntime pr;
					pr.name = paramJson.value("name", "");
					pr.type = paramJson.value("type", "float");
					pr.minVal = paramJson.value("min", 0.0f);
					pr.maxVal = paramJson.value("max", 1.0f);

					// default（数値 or 配列）
					if (paramJson.contains("default"))
					{
						const auto& d = paramJson["default"];
						if (d.is_array())
						{
							for (int i = 0; i < 4 && i < (int)d.size(); ++i)
								pr.value[i] = d[i].get<float>();
						}
						else
						{
							pr.value[0] = d.get<float>();
						}
					}
					pass.params.push_back(pr);
				}
			}

			// ---- 入力（g_input0,1,2...）----
			if (pj.contains("inputs"))
			{
				for (const auto& inJson : pj["inputs"])
				{
					InputBinding in;
					in.kind = inJson.value("kind", "");
					in.key = inJson.value("key", "");

					// texture なら、texturePath からロード
					if (in.kind == "texture")
					{
						std::string texPath = inJson.value("texturePath", "");
						if (!texPath.empty())
						{
							in.texture = std::make_shared<KdTexture>();
							if (in.texture->Load(texPath))
								in.textureSRV = in.texture->WorkSRView();
							else
								in.texture = nullptr;
						}
					}
					pass.inputs.push_back(in);
				}
			}

			// ---- PS コンパイル ----
			std::string psCode = pj.value("ps", "");
			if (!CompilePS(psCode, pass.ps))
			{
				OutputDebugStringA(("PS compile FAILED: " + pass.name + "\n").c_str());
				// コンパイル失敗パスも積む（実行時にnullチェックで弾く）
			}

			// ---- VS コンパイル（マテリアル）----
			if (pass.isMaterial)
			{
				std::string vsCode = pj.value("vs", "");
				std::vector<char> vsBytecode;
				if (CompileVS(vsCode, pass.vs, vsBytecode))
				{
					BuildMaterialLayout(pass, vsBytecode);
				}
				else
				{
					OutputDebugStringA(("VS compile FAILED: " + pass.name + "\n").c_str());
				}

				// ★スキンVSもコンパイル（あれば）
				std::string vsSkinnedCode = pj.value("vsSkinned", "");
				if (!vsSkinnedCode.empty())
				{
					std::vector<char> vsSkinnedBytecode;
					if (CompileVS(vsSkinnedCode, pass.vsSkinned, vsSkinnedBytecode))
					{
						BuildMaterialLayoutSkinned(pass, vsSkinnedBytecode);
						pass.hasSkinnedVS = true;
					}
					else
					{
						OutputDebugStringA(("Skinned VS compile FAILED: " + pass.name + "\n").c_str());
					}
				}
			}

			newPasses.push_back(std::move(pass));
		}
	}
	catch (const std::exception& e)
	{
		OutputDebugStringA(("LoadPipeline: build failed: "
			+ std::string(e.what()) + "\n").c_str());
		return false;
	}
	m_passes = std::move(newPasses);
	m_paramIndexDirty = true;
	CaptureDefaults();
	return true;
}

void MultiPassRuntime::RebuildParamIndex()
{
	m_paramIndex.clear();
	for (auto& pass : m_passes)
		for (auto& p : pass.params)
			m_paramIndex[p.name].push_back(&p);
	m_paramIndexDirty = false;
}

void MultiPassRuntime::SetParam(const std::string& paramName,
	float x, float y, float z, float w)
{
	// 毎フレーム呼ばれるので、名前 → パラメータの表を引くだけにする
	if (m_paramIndexDirty) RebuildParamIndex();

	auto it = m_paramIndex.find(paramName);
	if (it == m_paramIndex.end()) return;
	for (ParamRuntime* p : it->second)
		p->value = { x, y, z, w };
}

void MultiPassRuntime::CaptureDefaults()
{
	// 新しく読んだパイプラインの値で上書き（前のパイプラインにしか無い名前は残す）
	for (auto& pass : m_passes)
		for (auto& p : pass.params)
			m_defaultValues[p.name] = p.value;
}

void MultiPassRuntime::RestoreDefaults()
{
	if (m_paramIndexDirty) RebuildParamIndex();

	for (auto& [name, value] : m_defaultValues)
	{
		auto it = m_paramIndex.find(name);
		if (it == m_paramIndex.end()) continue;
		for (ParamRuntime* p : it->second)
			p->value = value;
	}
	// "__last__" は起動時に一度だけ当てて既定値に取り込んでいる（PipelineSystem::LoadDefaults）
}

void MultiPassRuntime::SetLights(const std::vector<PointLightData>& lights)
{
	// cbLights を作る（初回）
	if (!m_cbLights)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = 16 + sizeof(PointLightData) * 512;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &m_cbLights);
	}

	// 転送
	struct CBLights {
		int count; float pad[3];
		PointLightData lights[512];
	} data = {};
	data.count = (int)std::min(lights.size(), (size_t)512);
	for (int i = 0; i < data.count; ++i)
		data.lights[i] = lights[i];

	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbLights.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbLights.Get(), 0);
	}
}

ID3D11ShaderResourceView* MultiPassRuntime::GetFinalResultSRV()
{
	if (m_passResults.empty()) return nullptr;
	const auto& last = m_passResults.back();
	if (last.empty()) return nullptr;
	int idx = last[0];
	if (idx < 0 || idx >= (int)m_rtPool.size()) return nullptr;
	return m_rtPool[idx].srv.Get();
}

void MultiPassRuntime::Execute(float time, float bass, float mid, float treble,
	const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& worldView, const DirectX::XMMATRIX& world,
	int screenW, int screenH,
	DrawMeshCallback drawMesh,
	ID3D11ShaderResourceView* externalInput)
{
	// ステート退避
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	m_context->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1; D3D11_VIEWPORT oldVP = {};
	m_context->RSGetViewports(&oldVPCount, &oldVP);
	ID3D11RasterizerState* oldRS = nullptr;
	m_context->RSGetState(&oldRS);
	D3D11_PRIMITIVE_TOPOLOGY oldTopo;
	m_context->IAGetPrimitiveTopology(&oldTopo);

	ResetRTPool();
	ResetDepthPool();
	m_passResults.clear();
	TrimPools(screenW, screenH);

	m_lastTime = time; m_lastBass = bass; m_lastMid = mid; m_lastTreble = treble;
	UpdateWVPCB(wvp, worldView, world);

	std::unordered_map<std::string, ID3D11ShaderResourceView*> bufferMap;

	// TAA: 前フレームの履歴を
	if (!m_historyTex)
		EnsureHistoryRT(screenW, screenH, m_historyFormat);
	if (m_historySRV)
		bufferMap["History"] = m_historySRV.Get();

	for (int i = 0; i < (int)m_passes.size(); ++i)
	{
		PassRuntime& pass = m_passes[i];

		int w = (int)(screenW * pass.scaleFactor);
		int h = (int)(screenH * pass.scaleFactor);
		if (w < 1) w = 1;
		if (h < 1) h = 1;

		int targetCount = (pass.targetCount > 0) ? pass.targetCount : 1;
		std::vector<int> rtIndices;
		for (int t = 0; t < targetCount; ++t)
		{
			DXGI_FORMAT fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
			if (t < (int)pass.targetFormats.size())
				fmt = pass.targetFormats[t];
			int idx = AcquireRT(w, h, fmt);
			if (idx >= 0) rtIndices.push_back(idx);
		}
		if (rtIndices.empty()) { m_passResults.push_back({}); continue; }

		// PrevPass = 直前パスの target0
		ID3D11ShaderResourceView* prevSRV = nullptr;
		if (i == 0)
		{
			prevSRV = externalInput;   // 最初のパスはゲーム画面
		}
		else if (!m_passResults[i - 1].empty())
		{
			int pr = m_passResults[i - 1][0];
			if (pr >= 0 && pr < (int)m_rtPool.size())
				prevSRV = m_rtPool[pr].srv.Get();
		}

		// NamedBuffer
		std::vector<ID3D11ShaderResourceView*> namedSRVs;
		for (const auto& name : pass.namedBufferRefs)
		{
			auto it = bufferMap.find(name);
			namedSRVs.push_back(it != bufferMap.end() ? it->second : nullptr);
		}

		// meshMode: マテリアルパス（モデルを描く）
		bool meshMode = pass.isMaterial && pass.vs && pass.inputLayout;

		RenderPass(pass, rtIndices, prevSRV, namedSRVs, meshMode, drawMesh);
		m_passResults.push_back(rtIndices);

		// bufferMap に登録
		if (pass.targetCount >= 2)
		{
			for (int t = 0; t < (int)rtIndices.size() &&
				t < (int)pass.targetNames.size(); ++t)
			{
				if (!pass.targetNames[t].empty())
					bufferMap[pass.targetNames[t]] = m_rtPool[rtIndices[t]].srv.Get();
			}
		}
		else if (!pass.outputName.empty())
		{
			bufferMap[pass.outputName] = m_rtPool[rtIndices[0]].srv.Get();
		}
	}

	m_namedBufferSRVs = bufferMap;

	// TAA: 最終パスの結果を、履歴にコピー（次フレーム用）
	{
		ID3D11ShaderResourceView* finalSRV = GetFinalResultSRV();
		if (!finalSRV)
		{
			OutputDebugStringA("[TAA] copy skipped: finalSRV is null\n");
		}
		else if (!m_historyTex)
		{
			OutputDebugStringA("[TAA] copy skipped: historyTex is null\n");
		}
		else
		{
			ID3D11Resource* srcRes = nullptr;
			finalSRV->GetResource(&srcRes);
			if (srcRes)
			{
				// CopyResource はサイズと形式が完全に同じでないと失敗する（デバッグレイヤーでエラー）
				// 違うときは履歴を最終結果に合わせて作り直してからコピーする
				ID3D11Texture2D* srcTex = nullptr;
				srcRes->QueryInterface(IID_PPV_ARGS(&srcTex));
				bool canCopy = false;
				if (srcTex)
				{
					D3D11_TEXTURE2D_DESC sd = {};
					srcTex->GetDesc(&sd);
					srcTex->Release();

					if (sd.SampleDesc.Count == 1 && sd.ArraySize == 1 && sd.MipLevels == 1)
					{
						if ((int)sd.Width != m_historyW || (int)sd.Height != m_historyH ||
							sd.Format != m_historyFormat)
						{
							EnsureHistoryRT((int)sd.Width, (int)sd.Height, sd.Format);
						}
						canCopy = (m_historyTex != nullptr);
					}
				}
				if (canCopy)
					m_context->CopyResource(m_historyTex.Get(), srcRes);
				else
					OutputDebugStringA("[TAA] copy skipped: final result is not a plain 2D texture\n");
				srcRes->Release();
			}
			else
			{
				OutputDebugStringA("[TAA] copy skipped: srcRes is null\n");
			}
		}
	}

	// ステート復元
	m_context->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) m_context->RSSetViewports(1, &oldVP);
	m_context->RSSetState(oldRS);
	m_context->IASetPrimitiveTopology(oldTopo);
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
	if (oldRS) oldRS->Release();
}

bool MultiPassRuntime::CompilePS(const std::string& code,
	ComPtr<ID3D11PixelShader>& outPS)
{
	if (code.empty()) return false;

	const char* kTarget = "ps_5_0";
	const uint64_t hash = ShaderHash(code, kTarget);

	// キャッシュにあればコンパイルを丸ごと省く
	if (auto cached = LoadShaderCache(hash, kTarget); !cached.empty())
	{
		if (SUCCEEDED(m_device->CreatePixelShader(
			cached.data(), cached.size(), nullptr, &outPS)))
		{
			return true;
		}
		// 壊れたキャッシュだった場合は通常のコンパイルへ落とす
	}

	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(code.c_str(), code.size(), nullptr, nullptr, nullptr,
		"main", kTarget, 0, 0, &blob, &err)))
	{
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}

	SaveShaderCache(hash, kTarget, blob->GetBufferPointer(), blob->GetBufferSize());

	return SUCCEEDED(m_device->CreatePixelShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &outPS));
}
bool MultiPassRuntime::CompileVS(const std::string& code,
	ComPtr<ID3D11VertexShader>& outVS, std::vector<char>& outBytecode)
{
	if (code.empty()) return false;

	const char* kTarget = "vs_5_0";
	const uint64_t hash = ShaderHash(code, kTarget);

	if (auto cached = LoadShaderCache(hash, kTarget); !cached.empty())
	{
		if (SUCCEEDED(m_device->CreateVertexShader(
			cached.data(), cached.size(), nullptr, &outVS)))
		{
			// 入力レイアウト作成に使うので必ず返す
			outBytecode = std::move(cached);
			return true;
		}
	}

	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(code.c_str(), code.size(), nullptr, nullptr, nullptr,
		"main", kTarget, 0, 0, &blob, &err)))
	{
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}

	if (FAILED(m_device->CreateVertexShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &outVS)))
		return false;

	SaveShaderCache(hash, kTarget, blob->GetBufferPointer(), blob->GetBufferSize());

	outBytecode.resize(blob->GetBufferSize());
	memcpy(outBytecode.data(), blob->GetBufferPointer(), blob->GetBufferSize());
	return true;
}

void MultiPassRuntime::BuildMaterialLayout(PassRuntime& pass,
	const std::vector<char>& vsBytecode)
{
	// KdMeshVertex: Pos@0, UV@12, Color@20(4B), Normal@24
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }, 
	};
	m_device->CreateInputLayout(layout, 4,
		vsBytecode.data(), vsBytecode.size(), &pass.inputLayout);
}

void MultiPassRuntime::BuildMaterialLayoutSkinned(PassRuntime& pass,
	const std::vector<char>& vsBytecode)
{
	// KdMeshVertex: Pos@0, UV@12, Color@20(4B), Normal@24, Tangent@36,
	//               SkinIndex@48(short4=8B), SkinWeight@56(float4=16B)
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "SKININDEX",  0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "SKINWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	m_device->CreateInputLayout(layout, ARRAYSIZE(layout),
		vsBytecode.data(), vsBytecode.size(), &pass.inputLayoutSkinned);
}

void MultiPassRuntime::SetBones(const DirectX::XMFLOAT4X4* bones, int count)
{
	if (!m_cbBones)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 300;   // 300ボーン
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &m_cbBones);
	}
	int n = (count < 300) ? count : 300;
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, bones, sizeof(DirectX::XMFLOAT4X4) * n);
		m_context->Unmap(m_cbBones.Get(), 0);
	}
}

void MultiPassRuntime::RenderPass(PassRuntime& pass, const std::vector<int>& rtIndices,
	ID3D11ShaderResourceView* prevSRV,
	const std::vector<ID3D11ShaderResourceView*>& namedSRVs,
	bool meshMode, DrawMeshCallback drawMesh)
{
	if (!pass.ps || rtIndices.empty()) return;

	PooledRT& target = m_rtPool[rtIndices[0]];

	UpdateEffectCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble,
		GetPassParamValues(pass));

	// クリア
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	for (int idx : rtIndices)
		m_context->ClearRenderTargetView(m_rtPool[idx].rtv.Get(), clearColor);

	m_context->RSSetState(m_rsCullNone.Get());
	m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	m_context->OMSetDepthStencilState(nullptr, 0);
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)target.width; vp.Height = (float)target.height;
	vp.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &vp);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_context->PSSetShader(pass.ps.Get(), nullptr, 0);

	{
		ID3D11SamplerState* samplers[4] = {
			m_sampler.Get(), m_sampler.Get(), m_sampler.Get(), m_sampler.Get() };
		m_context->PSSetSamplers(0, 4, samplers);
		m_context->VSSetSamplers(0, 4, samplers);
	}

	m_context->PSSetConstantBuffers(0, 1, m_cbTime.GetAddressOf());
	if (m_cbParams)
		m_context->PSSetConstantBuffers(2, 1, m_cbParams.GetAddressOf());
	if (m_cbCamera)
		m_context->PSSetConstantBuffers(3, 1, m_cbCamera.GetAddressOf());
	if (m_cbLights)
		m_context->PSSetConstantBuffers(4, 1, m_cbLights.GetAddressOf());
	if (m_cbShadowLighting)
		m_context->PSSetConstantBuffers(6, 1, m_cbShadowLighting.GetAddressOf());
	// 入力を t0 から連番でバインド（inputs の順）
	std::vector<ID3D11ShaderResourceView*> inputSRVs;
	int namedIdx = 0;
	for (const auto& in : pass.inputs)
	{
		ID3D11ShaderResourceView* srv = nullptr;
		if (in.kind == "texture")
			srv = in.textureSRV.Get();
		else if (in.kind == "prevpass")
			srv = prevSRV;
		else if (in.kind == "namedbuffer")
			srv = (namedIdx < (int)namedSRVs.size()) ? namedSRVs[namedIdx++] : nullptr;
		inputSRVs.push_back(srv);
	}

	if (!inputSRVs.empty())
	{
		m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		m_context->PSSetShaderResources(0, (UINT)inputSRVs.size(), inputSRVs.data());
		if (meshMode)
		{
			m_context->VSSetSamplers(0, 1, m_sampler.GetAddressOf());
			m_context->VSSetShaderResources(0, (UINT)inputSRVs.size(), inputSRVs.data());
		}

		if (ShaderHelper::irradianceSRV)
			m_context->PSSetShaderResources(20, 1, ShaderHelper::irradianceSRV.GetAddressOf());
		if (ShaderHelper::prefilterSRV)
			m_context->PSSetShaderResources(21, 1, ShaderHelper::prefilterSRV.GetAddressOf());
		if (ShaderHelper::brdfLutSRV)
			m_context->PSSetShaderResources(22, 1, ShaderHelper::brdfLutSRV.GetAddressOf());
		if (ShaderHelper::cascadeSRV[0])
		{
			ID3D11ShaderResourceView* csrvs[3] = {
				ShaderHelper::cascadeSRV[0].Get(),
				ShaderHelper::cascadeSRV[1].Get(),
				ShaderHelper::cascadeSRV[2].Get() };
			m_context->PSSetShaderResources(23, 3, csrvs);
		}
	}

	std::vector<ID3D11RenderTargetView*> rtvs;
	for (int idx : rtIndices) rtvs.push_back(m_rtPool[idx].rtv.Get());

	if (meshMode)
	{
		int depthIndex = AcquireDepth(target.width, target.height);
		if (depthIndex >= 0)
		{
			m_context->ClearDepthStencilView(m_depthPool[depthIndex].dsv.Get(),
				D3D11_CLEAR_DEPTH, 1.0f, 0);
			m_context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(),
				m_depthPool[depthIndex].dsv.Get());
		}
		else
			m_context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), nullptr);

		m_context->OMSetDepthStencilState(nullptr, 0);
		m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);

		// 今のメッシュパスを覚える（コールバックから切り替えるため）
		m_currentMeshPass = &pass;

		// VS共通のCB
		m_context->VSSetConstantBuffers(0, 1, m_cbTime.GetAddressOf());
		m_context->VSSetConstantBuffers(1, 1, m_cbWVP.GetAddressOf());
		if (m_cbParams)
			m_context->VSSetConstantBuffers(2, 1, m_cbParams.GetAddressOf());

		// デフォルトは静的VS（コールバックが切り替える）
		UseStaticVS();

		// モデルを描く（コールバック内で、静的/スキンを切り替え）
		if (drawMesh)
			drawMesh();

		m_currentMeshPass = nullptr;
	}
	else
	{
		// フルスクリーン（既存）
		m_context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), nullptr);
		m_context->OMSetDepthStencilState(nullptr, 0);
		m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		m_context->IASetInputLayout(nullptr);
		m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		m_context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		m_context->Draw(3, 0);
	}

	// クリーンアップ
	ID3D11ShaderResourceView* nullSRVs[16] = {};
	m_context->PSSetShaderResources(0, 16, nullSRVs);
	m_context->VSSetShaderResources(0, 16, nullSRVs);
	ID3D11RenderTargetView* nullRTVs[8] = {};
	m_context->OMSetRenderTargets((UINT)rtvs.size(), nullRTVs, nullptr);
}

bool MultiPassRuntime::CreateCopyPS()
{
	static const char* kCopyPS = R"(
Texture2D g_tex : register(t0);
SamplerState g_samp : register(s0);
struct PS_IN { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 main(PS_IN In) : SV_Target0
{
    return g_tex.Sample(g_samp, In.uv);
}
)";
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(kCopyPS, strlen(kCopyPS), nullptr, nullptr, nullptr,
		"main", "ps_5_0", 0, 0, &blob, &err)))
	{
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}
	return SUCCEEDED(m_device->CreatePixelShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_copyPS));
}

bool MultiPassRuntime::CreateBlendState()
{
	D3D11_BLEND_DESC desc = {};
	desc.RenderTarget[0].BlendEnable = TRUE;
	desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	desc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	return SUCCEEDED(m_device->CreateBlendState(&desc, &m_alphaBlend));
}

ID3D11ShaderResourceView* MultiPassRuntime::CaptureScreen()
{
	// 現在バインドされているRTVを取得
	ID3D11RenderTargetView* curRTV = nullptr;
	ID3D11DepthStencilView* curDSV = nullptr;
	m_context->OMGetRenderTargets(1, &curRTV, &curDSV);
	if (!curRTV)
	{
		if (curDSV) curDSV->Release();
		return nullptr;
	}

	// RTV から元リソース・テクスチャを取得
	ID3D11Resource* srcRes = nullptr;
	curRTV->GetResource(&srcRes);

	ID3D11Texture2D* srcTex = nullptr;
	if (srcRes) srcRes->QueryInterface(IID_PPV_ARGS(&srcTex));

	ID3D11ShaderResourceView* result = nullptr;
	if (srcTex)
	{
		D3D11_TEXTURE2D_DESC srcDesc = {};
		srcTex->GetDesc(&srcDesc);

		EnsureCaptureRT(srcDesc);

		if (m_captureTex)
		{
			m_context->CopyResource(m_captureTex.Get(), srcRes);
			result = m_captureSRV.Get();
		}
	}

	if (srcTex) srcTex->Release();
	if (srcRes) srcRes->Release();
	if (curRTV) curRTV->Release();
	if (curDSV) curDSV->Release();

	return result;
}

void MultiPassRuntime::EnsureCaptureRT(const D3D11_TEXTURE2D_DESC& srcDesc)
{
	// 既に同じサイズ・フォーマットなら作り直さない
	if (m_captureTex &&
		m_captureW == srcDesc.Width &&
		m_captureH == srcDesc.Height &&
		m_captureFormat == srcDesc.Format)
		return;

	m_captureTex.Reset();
	m_captureSRV.Reset();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = srcDesc.Width;
	desc.Height = srcDesc.Height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;       // ★ソースと同じフォーマット
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_captureTex))) return;
	if (FAILED(m_device->CreateShaderResourceView(
		m_captureTex.Get(), nullptr, &m_captureSRV)))
	{
		m_captureTex.Reset();
		return;
	}

	m_captureW = srcDesc.Width;
	m_captureH = srcDesc.Height;
	m_captureFormat = srcDesc.Format;
}

void MultiPassRuntime::SetMaterialParams(const DirectX::XMFLOAT4& baseColor,
	float metallic, float roughness)
{
	if (!m_cbMaterial) CreateMaterialCB();

	struct MatCB {
		DirectX::XMFLOAT4 baseColor;
		float metallic;
		float roughness;
		float hasNormalMap;
		float pad;
	} data;
	data.baseColor = baseColor;
	data.metallic = metallic;
	data.roughness = roughness;
	data.hasNormalMap = 0.0f;

	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbMaterial.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbMaterial.Get(), 0);
	}

	// b5 にバインド（PS）
	m_context->PSSetConstantBuffers(5, 1, m_cbMaterial.GetAddressOf());
}

void MultiPassRuntime::SetShadowViewProj(const DirectX::XMMATRIX& lightViewProj)
{
	if (!m_cbShadowLighting) 
	{ 
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = 64;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &m_cbShadowLighting);
	}
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbShadowLighting.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		DirectX::XMFLOAT4X4 mat;
		DirectX::XMStoreFloat4x4(&mat, lightViewProj);
		memcpy(m.pData, &mat, sizeof(mat));
		m_context->Unmap(m_cbShadowLighting.Get(), 0);
	}
	m_context->PSSetConstantBuffers(6, 1, m_cbShadowLighting.GetAddressOf());
}

void MultiPassRuntime::SetCamera(const DirectX::XMFLOAT3& camPos,
	const DirectX::XMMATRIX& viewProj)
{
	if (!m_cbCamera)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = 144;   // camPos16 + invViewProj64 + viewProj64
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &m_cbCamera);
	}

	using namespace DirectX;
	XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);

	struct CamCB {
		XMFLOAT3 camPos; float pad;
		XMFLOAT4X4 invViewProj;
		XMFLOAT4X4 viewProj;
	} data;
	data.camPos = camPos;
	data.pad = 0;
	XMStoreFloat4x4(&data.invViewProj, invVP);
	XMStoreFloat4x4(&data.viewProj, viewProj);

	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbCamera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbCamera.Get(), 0);
	}
}
void MultiPassRuntime::CreateMaterialCB()
{
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = 32;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	m_device->CreateBuffer(&bd, nullptr, &m_cbMaterial);
}

void MultiPassRuntime::EnsureHistoryRT(int width, int height, DXGI_FORMAT format)
{
	if (m_historyTex && m_historyW == width && m_historyH == height && m_historyFormat == format)
		return;
	m_historyTex.Reset();
	m_historySRV.Reset();
	m_historyRTV.Reset();
	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = width; desc.Height = height;
	desc.MipLevels = 1; desc.ArraySize = 1;
	desc.Format = format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	m_historyFormat = format;
	if (FAILED(m_device->CreateTexture2D(&desc, nullptr, &m_historyTex)))
	{
		OutputDebugStringA("[TAA] history tex CREATE FAILED\n");
		return;
	}
	m_device->CreateRenderTargetView(m_historyTex.Get(), nullptr, &m_historyRTV);
	m_device->CreateShaderResourceView(m_historyTex.Get(), nullptr, &m_historySRV);
	m_historyW = width; m_historyH = height;
	float clr[4] = { 0,0,0,0 };
	m_context->ClearRenderTargetView(m_historyRTV.Get(), clr);
}

void MultiPassRuntime::UseStaticVS()
{
	if (!m_currentMeshPass) return;
	m_context->VSSetShader(m_currentMeshPass->vs.Get(), nullptr, 0);
	m_context->IASetInputLayout(m_currentMeshPass->inputLayout.Get());
}

void MultiPassRuntime::UseSkinnedVS()
{
	if (!m_currentMeshPass) return;
	if (!m_currentMeshPass->hasSkinnedVS) return;   // スキンVSがなければ何もしない
	m_context->VSSetShader(m_currentMeshPass->vsSkinned.Get(), nullptr, 0);
	m_context->IASetInputLayout(m_currentMeshPass->inputLayoutSkinned.Get());
	// cbBones を b2 にバインド
	if (m_cbBones)
		m_context->VSSetConstantBuffers(10, 1, m_cbBones.GetAddressOf());
}

void MultiPassRuntime::DrawToScreen(int screenW, int screenH)
{
	ID3D11ShaderResourceView* finalSRV = GetFinalResultSRV();
	if (!finalSRV) return;

	D3D11_VIEWPORT vp = {};
	vp.Width = (float)screenW; vp.Height = (float)screenH;
	vp.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &vp);

	m_context->RSSetState(m_rsCullNone.Get());
	m_context->OMSetBlendState(m_alphaBlend.Get(), nullptr, 0xFFFFFFFF);  // ★アルファ合成
	m_context->OMSetDepthStencilState(nullptr, 0);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_context->IASetInputLayout(nullptr);
	m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

	m_context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
	m_context->PSSetShader(m_copyPS.Get(), nullptr, 0);
	m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
	m_context->PSSetShaderResources(0, 1, &finalSRV);

	m_context->Draw(3, 0);

	// ブレンドを戻す
	m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	ID3D11ShaderResourceView* nullSRV[1] = {};
	m_context->PSSetShaderResources(0, 1, nullSRV);
}

void MultiPassRuntime::DrawParamUI(const char* uiId)
{
	ImGui::PushID(uiId);   // ★このUI全体をスコープ化

	// ============ プリセット ============
	if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);

		ImGui::TextDisabled("Save current values as:");
		ImGui::SetNextItemWidth(160);
		ImGui::InputTextWithHint("##presetname", "preset name...",
			m_presetNameBuf, sizeof(m_presetNameBuf));
		ImGui::SameLine();
		bool canSave = (strlen(m_presetNameBuf) > 0);
		if (!canSave) ImGui::BeginDisabled();
		if (ImGui::Button("Save", ImVec2(60, 0)))
		{
			SavePreset(m_presetNameBuf);
			m_presetNameBuf[0] = '\0';
		}
		if (!canSave) ImGui::EndDisabled();

		ImGui::Spacing();

		auto names = GetPresetNames();
		if (names.empty())
		{
			ImGui::TextDisabled("  (no presets yet)");
		}
		else
		{
			ImGui::TextDisabled("Saved presets:");
			for (auto& name : names)
			{
				ImGui::PushID(name.c_str());
				if (ImGui::Button(name.c_str(), ImVec2(140, 0)))
					ApplyPreset(name);
				ImGui::SameLine();
				ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.2f, 0.2f, 1.0f));
				if (ImGui::SmallButton("Delete"))
				{
					DeletePreset(name);
					ImGui::PopStyleColor();
					ImGui::PopID();
					break;
				}
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
		}

		ImGui::Unindent(8.0f);
		ImGui::Spacing();
	}

	ImGui::Separator();
	ImGui::Spacing();

	// ============ パラメータ ============
	ImGui::TextDisabled("Parameters");
	ImGui::Spacing();

	bool anyParams = false;
	for (int pi = 0; pi < (int)m_passes.size(); ++pi)
	{
		PassRuntime& pass = m_passes[pi];
		if (pass.params.empty()) continue;
		anyParams = true;

		std::string header = pass.name + "##pass" + std::to_string(pi);
		if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent(8.0f);
			ImGui::PushItemWidth(-80.0f);
			for (auto& p : pass.params)
			{
				std::string label = p.name + "##" + std::to_string(pi);
				if (p.type == "float")
					ImGui::SliderFloat(label.c_str(), &p.value[0], p.minVal, p.maxVal);
				else if (p.type == "float4")
					ImGui::ColorEdit4(label.c_str(), p.value.data(),
						ImGuiColorEditFlags_AlphaBar);
				else
					ImGui::SliderFloat4(label.c_str(), p.value.data(),
						p.minVal, p.maxVal);
			}
			ImGui::PopItemWidth();
			ImGui::Unindent(8.0f);
		}
	}

	if (!anyParams)
		ImGui::TextDisabled("  (no parameters in this pipeline)");

	ImGui::PopID();   // ★スコープ終了
}

bool MultiPassRuntime::LoadPipelineFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f)
	{
		OutputDebugStringA(("Pipeline file not found: " + path + "\n").c_str());
		return false;
	}
	std::string jsonText((std::istreambuf_iterator<char>(f)),
		std::istreambuf_iterator<char>());
	return LoadPipeline(jsonText);
}

void MultiPassRuntime::ApplyPreset(const std::string& presetName)
{
	for (auto& preset : m_presets)
	{
		if (preset.name != presetName) continue;

		// 各パラメータに、名前が一致する値を書き戻す
		for (auto& pass : m_passes)
		{
			for (auto& p : pass.params)
			{
				auto it = preset.values.find(p.name);
				if (it != preset.values.end())
					p.value = it->second;
			}
		}
		return;
	}
}

std::vector<std::string> MultiPassRuntime::GetPresetNames() const
{
	std::vector<std::string> names;
	for (auto& p : m_presets) names.push_back(p.name);
	return names;
}

bool MultiPassRuntime::SavePresetsToFile(const std::string& path)
{
	nlohmann::json root;
	json arr = json::array();
	for (auto& preset : m_presets)
	{
		json pj;
		pj["name"] = preset.name;
		json vals = json::object();
		for (auto& [k, v] : preset.values)
			vals[k] = { v[0], v[1], v[2], v[3] };
		pj["values"] = vals;
		arr.push_back(pj);
	}
	root["presets"] = arr;

	std::ofstream f(path);
	if (!f) return false;
	f << root.dump(2);
	return true;
}

bool MultiPassRuntime::LoadPresetsFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::string text((std::istreambuf_iterator<char>(f)),
		std::istreambuf_iterator<char>());

	try {
		auto root = nlohmann::json::parse(text);
		m_presets.clear();
		if (root.contains("presets"))
		{
			for (auto& pj : root["presets"])
			{
				Preset preset;
				preset.name = pj.value("name", "");
				if (pj.contains("values"))
					for (auto& [k, v] : pj["values"].items())
						preset.values[k] = { v[0], v[1], v[2], v[3] };
				m_presets.push_back(preset);
			}
		}
	}
	catch (...) { return false; }
	return true;
}

void MultiPassRuntime::SavePreset(const std::string& presetName)
{
	Preset preset;
	preset.name = presetName;
	for (auto& pass : m_passes)
		for (auto& p : pass.params)
			preset.values[p.name] = p.value;

	bool replaced = false;
	for (auto& existing : m_presets)
	{
		if (existing.name == presetName)
		{
			existing.values = preset.values;
			replaced = true;
			break;
		}
	}
	if (!replaced)
		m_presets.push_back(preset);

	if (!m_presetsPath.empty())
		SavePresetsToFile(m_presetsPath);
}

void MultiPassRuntime::DeletePreset(const std::string& presetName)
{
	m_presets.erase(
		std::remove_if(m_presets.begin(), m_presets.end(),
			[&](const Preset& p) { return p.name == presetName; }),
		m_presets.end());

	if (!m_presetsPath.empty())
		SavePresetsToFile(m_presetsPath);   // 削除も永続化
}

void MultiPassRuntime::SaveLastParams()
{
	SavePreset("__last__");   // 既存のSavePresetを使う（ファイルにも保存される）
}

void MultiPassRuntime::LoadLastParams()
{
	// "__last__" プリセットがあれば適用
	for (auto& preset : m_presets)
	{
		if (preset.name == "__last__")
		{
			ApplyPreset("__last__");
			return;
		}
	}
}

ID3D11ShaderResourceView* MultiPassRuntime::GetNamedBufferSRV(const std::string& name)
{
	auto it = m_namedBufferSRVs.find(name);
	return (it != m_namedBufferSRVs.end()) ? it->second : nullptr;
}

void MultiPassRuntime::OverrideMaterialPS(ID3D11PixelShader* ps,
	const std::vector<std::array<float, 4>>* params)
{
	if (!m_currentMeshPass) return;
	if (ps) m_context->PSSetShader(ps, nullptr, 0);
	if (params)
		UpdateEffectCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble, *params);
}

void MultiPassRuntime::RestoreMaterialPS()
{
	if (!m_currentMeshPass) return;
	m_context->PSSetShader(m_currentMeshPass->ps.Get(), nullptr, 0);
	UpdateEffectCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble,
		GetPassParamValues(*m_currentMeshPass));
}

void MultiPassRuntime::SetCascades(const DirectX::XMMATRIX* vps,
	const DirectX::XMFLOAT3& dists)
{
	// バッファが無ければ作る
	if (!m_cbShadowLighting)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(CBShadowLighting);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		ID3D11Device* dev = nullptr;
		m_context->GetDevice(&dev);
		if (!dev) return;
		HRESULT hr = dev->CreateBuffer(&bd, nullptr, &m_cbShadowLighting);
		dev->Release();
		if (FAILED(hr)) return;
	}

	CBShadowLighting cb = {};
	for (int i = 0; i < 3; ++i)
		DirectX::XMStoreFloat4x4(&cb.lightViewProj[i], vps[i]);
	cb.cascadeDist = { dists.x, dists.y, dists.z, 0.0f };
	m_context->UpdateSubresource(m_cbShadowLighting.Get(), 0, nullptr, &cb, 0, 0);
}

// 書き込みマスクごとのブレンドステートをキャッシュして返す
ID3D11BlendState* MultiPassRuntime::GetWriteMaskBlend(unsigned int mask)
{
	auto it = m_maskBlends.find(mask);
	if (it != m_maskBlends.end()) return it->second.Get();

	D3D11_BLEND_DESC d = {};
	d.IndependentBlendEnable = TRUE;
	for (int i = 0; i < 8; ++i)
	{
		d.RenderTarget[i].BlendEnable = FALSE;
		d.RenderTarget[i].RenderTargetWriteMask =
			(mask & (1u << i)) ? D3D11_COLOR_WRITE_ENABLE_ALL : 0;
	}
	ComPtr<ID3D11BlendState> bs;
	if (FAILED(m_device->CreateBlendState(&d, &bs))) return nullptr;
	m_maskBlends[mask] = bs;
	return bs.Get();
}

void MultiPassRuntime::BeginOverlay(ID3D11PixelShader* ps, unsigned int writeMask,
	const std::vector<std::array<float, 4>>* params)
{
	if (!m_currentMeshPass) return;
	if (ps) m_context->PSSetShader(ps, nullptr, 0);
	if (params)
		UpdateEffectCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble, *params);

	// 深度: LESS_EQUAL・書き込みなし（同じ面をもう一度通す）
	if (!m_overlayDepth)
	{
		D3D11_DEPTH_STENCIL_DESC dd = {};
		dd.DepthEnable = TRUE;
		dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dd.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
		m_device->CreateDepthStencilState(&dd, &m_overlayDepth);
	}
	m_context->OMSetDepthStencilState(m_overlayDepth.Get(), 0);

	float bf[4] = { 0,0,0,0 };
	m_context->OMSetBlendState(GetWriteMaskBlend(writeMask), bf, 0xFFFFFFFF);
}

void MultiPassRuntime::EndOverlay()
{
	if (!m_currentMeshPass) return;
	m_context->PSSetShader(m_currentMeshPass->ps.Get(), nullptr, 0);
	UpdateEffectCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble,
		GetPassParamValues(*m_currentMeshPass));
	m_context->OMSetDepthStencilState(nullptr, 0);
	m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}