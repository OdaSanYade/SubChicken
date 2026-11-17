#include "MultiPassRuntime.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include "../../Shaders/ShaderSystemHelper.h"

using json = nlohmann::json;

namespace
{
	// ------------------------------------------------------------
	// シェーダバイトコードのディスクキャッシュ
	// ------------------------------------------------------------
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

	const char* kShaderCacheDir = "Asset/Cache/Shader";

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

// リソースの初期化
bool MultiPassRuntime::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
	m_device = device;
	m_context = context;

	if (!CreateFullscreenVS())    return false; // フルスクリーン描画用のVSを作る
	if (!CreateConstantBuffers()) return false; // 定数バッファを作る
	if (!CreateSamplerState())    return false; // サンプラーステートを作る
	if (!CreateRasterizerState()) return false; // ラスタライザーステートを作る
	if (!CreateCopyPS())          return false; // コピー用のPSを作る
	if (!CreateBlendState()) return false;      // ブレンドステートを作る
	return true;
}

// リソース解放
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
		if (!rt.inUse && rt.width == width && rt.height == height && rt.format == format)
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
	++m_poolFrame; //フレームごと行う

	// 画面サイズが変わったら、前のサイズの RT は全部捨てる
	// 新規画面サイズに変更
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

	// 古い RT を削除
	m_rtPool.erase(std::remove_if(m_rtPool.begin(), m_rtPool.end(),
		[&](const PooledRT& rt) { return !rt.inUse && stale(rt.lastUsedFrame); }),
		m_rtPool.end());

	// 古い Depth を削除
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

//フルスクリーン用VS作成
bool MultiPassRuntime::CreateFullscreenVS()
{
	// フルスクリーン描画用の VSコード
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

	//文字列コンパイル
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(kVSSource, strlen(kVSSource), "FSVS", nullptr, nullptr,
		"main", "vs_5_0", 0, 0, &blob, &err)))
	{
		return false;
	}
	return SUCCEEDED(m_device->CreateVertexShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &m_fullscreenVS));
}

// 定数バッファ作成
bool MultiPassRuntime::CreateConstantBuffers()
{
	D3D11_BUFFER_DESC desc = {};
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	
	desc.ByteWidth = 16 + 16 * 16;   // 272バイト（time/audio 16 + params 256）,時間やパラメータ用
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cbTime))) return false;

	desc.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 3; // 16 * 3 = 48バイト。World, View, Projection の 3つの行列
	if (FAILED(m_device->CreateBuffer(&desc, nullptr, &m_cbWVP))) return false;

	return true;
}

// サンプラーステート作成
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

// ラスタライザーステート作成
bool MultiPassRuntime::CreateRasterizerState()
{
	D3D11_RASTERIZER_DESC desc = {};
	desc.FillMode = D3D11_FILL_SOLID;
	desc.CullMode = D3D11_CULL_NONE;
	desc.DepthClipEnable = TRUE;
	return SUCCEEDED(m_device->CreateRasterizerState(&desc, &m_rsCullNone));
}

// 画面のWVPを更新
void MultiPassRuntime::UpdateWVPCB(const DirectX::XMMATRIX& wvp,
	const DirectX::XMMATRIX& worldView,
	const DirectX::XMMATRIX& world)
{
	struct CBData {
		DirectX::XMFLOAT4X4 wvp;       //Projection * View * World
		DirectX::XMFLOAT4X4 worldView; //View * World
		DirectX::XMFLOAT4X4 world;     //World
	} data;
	DirectX::XMStoreFloat4x4(&data.wvp, wvp);
	DirectX::XMStoreFloat4x4(&data.worldView, worldView);
	DirectX::XMStoreFloat4x4(&data.world, world);

	// 書き込み
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbWVP.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbWVP.Get(), 0);
	}
}

// 時間やパラメータなどの定数バッファの更新
void MultiPassRuntime::UpdateParameterCB(float time, float bass, float mid, float treble,
	const std::vector<std::array<float, 4>>& params)
{
	// その他パラメータfloat4を16個まで格納可能
	struct CBData {
		float time, bass, mid, treble;
		float params[16][4];
	} data = {};
	data.time = time; data.bass = bass; data.mid = mid; data.treble = treble;

	// パラメータをコピー
	for (int i = 0; i < (int)params.size() && i < 16; ++i)
	{
		data.params[i][0] = params[i][0];
		data.params[i][1] = params[i][1];
		data.params[i][2] = params[i][2];
		data.params[i][3] = params[i][3];
	}

	// 書き込む
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

// Jsonアセットからの情報を読み込む
bool MultiPassRuntime::LoadPipeline(const std::string& jsonText)
{
	json root;

	try { root = json::parse(jsonText); }
	catch (...) {
		// JSON パース失敗
		OutputDebugStringA("LoadPipeline: JSON parse failed\n");
		return false;
	}

	if (!root.contains("passes") || !root["passes"].is_array())
		return false;

	// G-Bufferのフォーマット一覧
	auto parseFormat = [](const std::string& s) -> DXGI_FORMAT {
		if (s == "R8G8B8A8_UNORM")      return DXGI_FORMAT_R8G8B8A8_UNORM;		//Buffer A アルベド値 + 金属度
		if (s == "R16G16B16A16_FLOAT")  return DXGI_FORMAT_R16G16B16A16_FLOAT;  //Buffer B 法線 + 粗さ
		if (s == "R32G32B32A32_FLOAT")  return DXGI_FORMAT_R32G32B32A32_FLOAT;  //Buffer C 位置
		if (s == "R32_FLOAT")           return DXGI_FORMAT_R32_FLOAT;           //Buffer D エミッシブ
		if (s == "R16G16_FLOAT")        return DXGI_FORMAT_R16G16_FLOAT;        //Buffer E マスク情報
		return DXGI_FORMAT_R8G8B8A8_UNORM;                                      //それ以外のバッファフォーマット
		};

	// パース後の構築も try で守る
	std::vector<PassRuntime> newPasses;

	try
	{
		for (const auto& pj : root["passes"])
		{
			if (!pj.is_object()) continue;   // 壊れたパスはスキップ

			// パスの情報を取得
			PassRuntime pass;
			pass.name = pj.value("name", "");					// パス名
			pass.passType = pj.value("passType", 0);			// パス種別（0:起点パス, 1:マルチパス）
			pass.scaleFactor = pj.value("scaleFactor", 1.0f);   // 解像度スケール
			pass.outputName = pj.value("outputName", "");       // 出力先の名前付きバッファ名
			pass.isMaterial = pj.value("isMaterial", false);    // マテリアルパスかどうか
			pass.targetCount = pj.value("targetCount", 1);      // 出力ターゲット数(G-Buffer)
			pass.usesPrevPass = pj.value("usesPrevPass", false);// 前のパスの出力を使用するか

			// targetNames
			// 作ったテクスチャの名前の一覧
			if (pj.contains("targetNames"))
				for (const auto& tn : pj["targetNames"])
					pass.targetNames.push_back(tn.get<std::string>());

			// namedBufferRefs
			// 参照する名前付きバッファの一覧
			if (pj.contains("namedBufferRefs"))
				for (const auto& nb : pj["namedBufferRefs"])
					pass.namedBufferRefs.push_back(nb.get<std::string>());

			if (pj.contains("targetFormats"))
			{
				// targetFormats
				// 作ったテクスチャのフォーマットの一覧
				for (auto& f : pj["targetFormats"])
					pass.targetFormats.push_back(parseFormat(f.get<std::string>()));
			}

			// ---- パラメータ ----
			if (pj.contains("params"))
			{
				for (const auto& paramJson : pj["params"])
				{
					ParamRuntime pr;
					pr.name = paramJson.value("name", "");		// パラメータ名
					pr.type = paramJson.value("type", "float"); // パラメータ型（float,float4）
					pr.minVal = paramJson.value("min", 0.0f);   // 最小値
					pr.maxVal = paramJson.value("max", 1.0f);   // 最大値

					// default
					if (paramJson.contains("default"))
					{
						const auto& d = paramJson["default"];

						// float4
						if (d.is_array())
						{
							for (int i = 0; i < 4 && i < (int)d.size(); ++i)
								pr.value[i] = d[i].get<float>();
						}
						// float
						else
						{
							pr.value[0] = d.get<float>();
						}
					}
					pass.params.push_back(pr);
				}
			}

			// ---- 入力 ----
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

			//=============================================
			//　シェーダコンパイル
			//  D3DCompileFromFile で 文字列のHLSL をコンパイする
			//=============================================

			// ---- PS コンパイル ----
			std::string psCode = pj.value("ps", ""); // ピクセルシェーダコード
			if (!CompilePS(psCode, pass.ps))
			{
				OutputDebugStringA(("PS compile FAILED: " + pass.name + "\n").c_str());
			}

			// ---- VS コンパイル（マテリアル）----
			if (pass.isMaterial)
			{
				std::string vsCode = pj.value("vs", ""); // 頂点シェーダコード
				std::vector<char> vsBytecode;
				if (CompileVS(vsCode, pass.vs, vsBytecode))
				{
					BuildMaterialLayout(pass, vsBytecode);
				}
				else
				{
					OutputDebugStringA(("VS compile FAILED: " + pass.name + "\n").c_str());
				}

				// スキンVSもコンパイル
				std::string vsSkinnedCode = pj.value("vsSkinned", ""); // スキン頂点シェーダコード
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

			//　このパスの情報を追加
			newPasses.push_back(std::move(pass));
		}
	}
	catch (const std::exception& e)
	{
		// JSON パース中に例外が発生した場合
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
}


// ポイントライトセット
void MultiPassRuntime::SetLights(const std::vector<PointLightData>& lights)
{
	// cbLights を作る
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

	// 書き込み
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbLights.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbLights.Get(), 0);
	}
}

// 最終出力
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

	// 前フレームの履歴を
	if (!m_historyTex)
		EnsureHistoryRT(screenW, screenH, m_historyFormat);
	if (m_historySRV)
		bufferMap["History"] = m_historySRV.Get();

	// 各パスごとに描画
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
		// 最初のパス
		if (i == 0)
		{
			prevSRV = externalInput; 
		}
		// その後のパス
		else if (!m_passResults[i - 1].empty())
		{
			int pr = m_passResults[i - 1][0];
			if (pr >= 0 && pr < (int)m_rtPool.size())
				prevSRV = m_rtPool[pr].srv.Get();
		}

		// NamedBuffer群
		std::vector<ID3D11ShaderResourceView*> namedSRVs;
		for (const auto& name : pass.namedBufferRefs)
		{
			auto it = bufferMap.find(name);
			namedSRVs.push_back(it != bufferMap.end() ? it->second : nullptr);
		}

		// マテリアルパス
		bool meshMode = pass.isMaterial && pass.vs && pass.inputLayout;

		// パス描画
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

	// 最終パスの結果を、履歴にコピー（次フレーム用）
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
				// CopyResource はサイズと形式が完全に同じでないと失敗する
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

// アセットPSコンパイル
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
	}

	// コンパイル
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(code.c_str(), code.size(), nullptr, nullptr, nullptr,
		"main", kTarget, 0, 0, &blob, &err)))
	{
		// コンパイル失敗
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}

	// キャッシュに保存
	SaveShaderCache(hash, kTarget, blob->GetBufferPointer(), blob->GetBufferSize());

	// PS 作成
	return SUCCEEDED(m_device->CreatePixelShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &outPS));
}

// アセットVSコンパイル
bool MultiPassRuntime::CompileVS(const std::string& code,
	ComPtr<ID3D11VertexShader>& outVS, std::vector<char>& outBytecode)
{
	if (code.empty()) return false;

	const char* kTarget = "vs_5_0";
	const uint64_t hash = ShaderHash(code, kTarget);

	// キャッシュにあればコンパイルを丸ごと省く
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

	// コンパイル
	ComPtr<ID3DBlob> blob, err;
	if (FAILED(D3DCompile(code.c_str(), code.size(), nullptr, nullptr, nullptr,
		"main", kTarget, 0, 0, &blob, &err)))
	{
		// コンパイル失敗
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}

	// キャッシュに保存
	SaveShaderCache(hash, kTarget, blob->GetBufferPointer(), blob->GetBufferSize());

	// VS 作成
	if (FAILED(m_device->CreateVertexShader(
		blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &outVS)))
		return false;

	// 入力レイアウト作成にバイトコードを使うので必ず返す
	outBytecode.resize(blob->GetBufferSize());
	memcpy(outBytecode.data(), blob->GetBufferPointer(), blob->GetBufferSize());
	return true;
}

//　静的オブジェクト用
void MultiPassRuntime::BuildMaterialLayout(PassRuntime& pass,
	const std::vector<char>& vsBytecode)
{
	// フレームワークは,UVと法線の間に4BのColor情報を持たせているのでoffsetを4B分ずらす
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 }, 
	};

	// 入力レイアウト作成
	m_device->CreateInputLayout(layout, 4,
		vsBytecode.data(), vsBytecode.size(), &pass.inputLayout);
}

//スキンメッシュ用
void MultiPassRuntime::BuildMaterialLayoutSkinned(PassRuntime& pass,
	const std::vector<char>& vsBytecode)
{
	// フレームワークは,UVと法線の間に4BのColor情報を持たせているのでoffsetを4B分ずらす
	D3D11_INPUT_ELEMENT_DESC layout[] =
	{
		{ "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TANGENT",    0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "SKININDEX",  0, DXGI_FORMAT_R16G16B16A16_UINT, 0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "SKINWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};

	// 入力レイアウト作成
	m_device->CreateInputLayout(layout, 6,
		vsBytecode.data(), vsBytecode.size(), &pass.inputLayoutSkinned);
}

// ボーンの設定
void MultiPassRuntime::SetBones(const DirectX::XMFLOAT4X4* bones, int count)
{
	if (!m_cbBones)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 300;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &m_cbBones);
	}

	// ボーン行列を転送
	int n = (count < 300) ? count : 300;
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, bones, sizeof(DirectX::XMFLOAT4X4) * n);
		m_context->Unmap(m_cbBones.Get(), 0);
	}
}

// 各パスの描画
void MultiPassRuntime::RenderPass(PassRuntime& pass, const std::vector<int>& rtIndices,
	ID3D11ShaderResourceView* prevSRV,
	const std::vector<ID3D11ShaderResourceView*>& namedSRVs,
	bool meshMode, DrawMeshCallback drawMesh)
{
	if (!pass.ps || rtIndices.empty()) return;

	PooledRT& target = m_rtPool[rtIndices[0]];

	UpdateParameterCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble,
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

	// 定数バッファにバインド
	m_context->PSSetConstantBuffers(0, 1, m_cbTime.GetAddressOf());
	if (m_cbParams)
		m_context->PSSetConstantBuffers(2, 1, m_cbParams.GetAddressOf());
	if (m_cbCamera)
		m_context->PSSetConstantBuffers(3, 1, m_cbCamera.GetAddressOf());
	if (m_cbLights)
		m_context->PSSetConstantBuffers(4, 1, m_cbLights.GetAddressOf());
	if(m_cbMaterial)
		m_context->PSSetConstantBuffers(5, 1, m_cbMaterial.GetAddressOf());
	if (m_cbShadowLighting)
		m_context->PSSetConstantBuffers(6, 1, m_cbShadowLighting.GetAddressOf());

	// 入力を連番でバインド
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

	// SRVセット
	if (!inputSRVs.empty())
	{
		m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
		m_context->PSSetShaderResources(0, (UINT)inputSRVs.size(), inputSRVs.data());
		if (meshMode)
		{
			m_context->VSSetSamplers(0, 1, m_sampler.GetAddressOf());
			m_context->VSSetShaderResources(0, (UINT)inputSRVs.size(), inputSRVs.data());
		}
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

	std::vector<ID3D11RenderTargetView*> rtvs;
	for (int idx : rtIndices) rtvs.push_back(m_rtPool[idx].rtv.Get());

	// モデルを描く場合
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
		// フルスクリーン
		m_context->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), nullptr);
		m_context->OMSetDepthStencilState(nullptr, 0);
		m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
		m_context->IASetInputLayout(nullptr);
		m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
		m_context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
		m_context->Draw(3, 0);
	}

	// 後始末
	ID3D11ShaderResourceView* nullSRVs[26] = {};
	m_context->PSSetShaderResources(0, 26, nullSRVs);
	m_context->VSSetShaderResources(0, 26, nullSRVs);
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

// ブレンドステートを作成
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

// マテリアル値の変更
void MultiPassRuntime::SetMaterialParams(const DirectX::XMFLOAT4& baseColor,
	float metallic, float roughness)
{
	if (!m_cbMaterial)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = 32;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &m_cbMaterial);
	}

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

	// 転送
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbMaterial.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbMaterial.Get(), 0);
	}
}

// シャドウ用
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

	DirectX::XMFLOAT4X4 mat;
	DirectX::XMStoreFloat4x4(&mat, lightViewProj);

	// 転送
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbShadowLighting.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &mat, sizeof(mat));
		m_context->Unmap(m_cbShadowLighting.Get(), 0);
	}
}

// カメラ情報
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

	// 転送
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(m_context->Map(m_cbCamera.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		memcpy(m.pData, &data, sizeof(data));
		m_context->Unmap(m_cbCamera.Get(), 0);
	}
}

// TAAの前フレーム情報
void MultiPassRuntime::EnsureHistoryRT(int width, int height, DXGI_FORMAT format)
{
	// 過去テクスチャがありサイズとフォーマットが同じなら何もしない
	if (m_historyTex && m_historyW == width && m_historyH == height && m_historyFormat == format)return;

	// 古い過去テクスチャを破棄
	m_historyTex.Reset();
	m_historySRV.Reset();
	m_historyRTV.Reset();

	//　新規の過去テクスチャを作る
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


//============== VSの更新 ======================

// 静的オブジェクト用VS
void MultiPassRuntime::UseStaticVS()
{
	if (!m_currentMeshPass) return;
	m_context->VSSetShader(m_currentMeshPass->vs.Get(), nullptr, 0);
	m_context->IASetInputLayout(m_currentMeshPass->inputLayout.Get());
}

// スキニング用VSに切り替え
// UseStaticVSの上書き
void MultiPassRuntime::UseSkinnedVS()
{
	if (!m_currentMeshPass) return;
	if (!m_currentMeshPass->hasSkinnedVS) return;
	m_context->VSSetShader(m_currentMeshPass->vsSkinned.Get(), nullptr, 0);
	m_context->IASetInputLayout(m_currentMeshPass->inputLayoutSkinned.Get());

	// b10にボーンの情報を渡す
	// スキニングメッシュしかこの情報は使わないので、スキニング用VSの時だけセットする
	if (m_cbBones)
		m_context->VSSetConstantBuffers(10, 1, m_cbBones.GetAddressOf());
}

// ==========================================

// 最終描画
void MultiPassRuntime::DrawToScreen(int screenW, int screenH)
{
	//最終出力SRVを取得(これを描画する)
	ID3D11ShaderResourceView* finalSRV = GetFinalResultSRV();
	if (!finalSRV) return;

	D3D11_VIEWPORT vp = {};
	vp.Width = (float)screenW; vp.Height = (float)screenH;
	vp.MaxDepth = 1.0f;
	m_context->RSSetViewports(1, &vp);

	m_context->RSSetState(m_rsCullNone.Get());
	m_context->OMSetBlendState(m_alphaBlend.Get(), nullptr, 0xFFFFFFFF);
	m_context->OMSetDepthStencilState(nullptr, 0);
	m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	m_context->IASetInputLayout(nullptr);
	m_context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);

	// フルスクリーン描画用のシェーダ
	m_context->VSSetShader(m_fullscreenVS.Get(), nullptr, 0);
	m_context->PSSetShader(m_copyPS.Get(), nullptr, 0);
	m_context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
	m_context->PSSetShaderResources(0, 1, &finalSRV);

	//　描画
	m_context->Draw(3, 0);

	// ブレンドを戻す
	m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	ID3D11ShaderResourceView* nullSRV[1] = {};
	m_context->PSSetShaderResources(0, 1, nullSRV);
}

void MultiPassRuntime::DrawParamUI(const char* uiId)
{
	ImGui::PushID(uiId); 

	// ============ プリセット ============
	if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen))
	{
		ImGui::Indent(8.0f);

		// プリセット一覧
		ImGui::TextDisabled("Save current values as:");
		ImGui::SetNextItemWidth(160);
		ImGui::InputTextWithHint("##presetname", "preset name...",
			m_presetNameBuf, sizeof(m_presetNameBuf));
		ImGui::SameLine();

		// プリセットをセーブ
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

		// プリセットがない場合
		if (names.empty())
		{
			ImGui::TextDisabled("  (no presets yet)");
		}

		// プリセットがある場合その一覧を表示
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

	// 各パラメータ
	// パスごとのパラメータを一覧表示する
	ImGui::TextDisabled("Parameters");
	ImGui::Spacing();

	bool anyParams = false;
	for (int pi = 0; pi < (int)m_passes.size(); ++pi)
	{
		PassRuntime& pass = m_passes[pi];
		if (pass.params.empty()) continue;
		anyParams = true;

		// パス名を表示
		std::string header = pass.name + "##pass" + std::to_string(pi);
		if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
		{
			ImGui::Indent(8.0f);
			ImGui::PushItemWidth(-80.0f);
			for (auto& p : pass.params)
			{
				// float ...スライダー
				// float4... Color
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

	// パラメータが1つもない場合はその旨を表示
	if (!anyParams)
		ImGui::TextDisabled("  (no parameters in this pipeline)");

	ImGui::PopID();
}

//　指定パスのパイプラインアッセットを取得
bool MultiPassRuntime::LoadPipelineFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f)
	{
		// ファイルが開けなかった
		OutputDebugStringA(("[Pipeline] : Pipeline file not found: " + path + "\n").c_str());
		return false;
	}

	//　パイプラインアセットがあるならそのjsonをロードする
	std::string jsonText((std::istreambuf_iterator<char>(f)),
		std::istreambuf_iterator<char>());
	return LoadPipeline(jsonText);
}

// プリセットを適応
void MultiPassRuntime::ApplyPreset(const std::string& presetName)
{
	for (auto& preset : m_presets)
	{
		// 押されたプリセットボタンを探す
		if (preset.name != presetName) continue;

		// 各パラメータに、名前が一致する値を書き戻す
		for (auto& pass : m_passes)
		{
			for (auto& p : pass.params)
			{
				// パラメータを書き込み
				auto it = preset.values.find(p.name);
				if (it != preset.values.end())
					p.value = it->second;
			}
		}
		return;
	}
}

// プリセットの名前の一覧を獲得
std::vector<std::string> MultiPassRuntime::GetPresetNames() const
{
	std::vector<std::string> names;
	for (auto& p : m_presets) names.push_back(p.name);
	return names;
}

// プリセットを外部ファイル(json)に保存
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

// プリセットのロード
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

// プリセットの内部セーブ
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

	// 外部ファイルにプリセットを保存
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
	SavePreset("__last__");   // __last__というプリセットを生成（これが実質デフォルトのパラメータ）
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

// パイプラインで作成したバッファを取得
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
		UpdateParameterCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble, *params);
}

void MultiPassRuntime::RestoreMaterialPS()
{
	if (!m_currentMeshPass) return;
	m_context->PSSetShader(m_currentMeshPass->ps.Get(), nullptr, 0);
	UpdateParameterCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble,
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

// マテリアル上書き用
void MultiPassRuntime::BeginOverlay(ID3D11PixelShader* ps, unsigned int writeMask,
	const std::vector<std::array<float, 4>>* params)
{
	if (!m_currentMeshPass) return;
	if (ps) m_context->PSSetShader(ps, nullptr, 0);
	if (params)
		UpdateParameterCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble, *params);

	// 深度: LESS_EQUAL
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
	UpdateParameterCB(m_lastTime, m_lastBass, m_lastMid, m_lastTreble,
		GetPassParamValues(*m_currentMeshPass));
	m_context->OMSetDepthStencilState(nullptr, 0);
	m_context->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
}