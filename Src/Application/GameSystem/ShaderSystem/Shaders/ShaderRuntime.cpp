#include "ShaderRuntime.h"
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")

#include "ShaderSystemHelper.h"
#include "ShaderCodes.h"

using Microsoft::WRL::ComPtr;

// 共有cbuffer構造体（最大16パラメータ分の余裕を持たせる）
namespace {
	struct CBData {
		float time, bass, mid, treble;
		float params[16][4];   // パラメータ用の余白
	};
}

bool ShaderRuntime::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
	m_device = device;
	m_context = context;


	ShaderHelper::CreateCubemapFromFiles(ShaderHelper::faces, KdDirect3D::Instance().WorkDev(), ShaderHelper::cubeSRV);
	ShaderHelper::CreateRenderTargetCubemap(
		32,                                    // サイズ（イラディアンスは低解像度でOK）
		KdDirect3D::Instance().WorkDev(),
		ShaderHelper::irradianceTex,
		ShaderHelper::irradianceSRV,
		ShaderHelper::irradianceRTVs);
	ShaderHelper::CreatePrefilterCubemap(128, 5,   // ベース128、5ミップ
		KdDirect3D::Instance().WorkDev(),
		ShaderHelper::prefilterTex,
		ShaderHelper::prefilterSRV,
		ShaderHelper::prefilterRTVs);
	ShaderHelper::CreateBRDFLut(
		512,
		KdDirect3D::Instance().WorkDev(),
		ShaderHelper::brdfLutTex,
		ShaderHelper::brdfLutSRV,
		ShaderHelper::brdfLutRTV);
	ShaderHelper::CreateShadowMap(
		2048,
		KdDirect3D::Instance().WorkDev(),
		ShaderHelper::shadowMapTex,
		ShaderHelper::shadowMapDSV,
		ShaderHelper::shadowMapSRV);

	// カスケードごとに解像度を変える
	const int kCascadeSize[3] = { 2048, 1024, 512 };
	for (int c = 0; c < ShaderHelper::kCascadeCount; ++c)
	{
		ShaderHelper::CreateShadowMap(
			kCascadeSize[c],
			KdDirect3D::Instance().WorkDev(),
			ShaderHelper::cascadeTex[c],
			ShaderHelper::cascadeDSV[c],
			ShaderHelper::cascadeSRV[c]);
	}
	// VSコンパイル
	Microsoft::WRL::ComPtr<ID3DBlob> vsb, err;
	if (FAILED(D3DCompile(kVSCode, strlen(kVSCode), 0, 0, 0, "main", "vs_5_0", 0, 0, &vsb, &err)))
	{
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}
	if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), 0, &m_vs)))
		return false;

	{
		Microsoft::WRL::ComPtr<ID3DBlob> psb, errps;
		if (FAILED(D3DCompile(kShow2DPS, strlen(kShow2DPS), 0, 0, 0, "main", "ps_5_0", 0, 0, &psb, &errps)))
		{
			if (errps) OutputDebugStringA((char*)errps->GetBufferPointer());
			return false;
		}
		if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &m_testPS)))
			return false;
	}
	// イラディアンスPSコンパイル
	{
		Microsoft::WRL::ComPtr<ID3DBlob> psb, errps;
		if (FAILED(D3DCompile(kIrradiancePS, strlen(kIrradiancePS), 0, 0, 0, "main", "ps_5_0", 0, 0, &psb, &errps)))
		{
			if (errps) OutputDebugStringA((char*)errps->GetBufferPointer());
			return false;
		}
		if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &m_irradiancePS)))
			return false;

		{
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = 64;   // float4x4
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			device->CreateBuffer(&bd, nullptr, &m_cbFace);
		}
	}
	// プレフィルタPS
	{
		Microsoft::WRL::ComPtr<ID3DBlob> psb, errps;
		if (FAILED(D3DCompile(kPrefilterPS, strlen(kPrefilterPS), 0, 0, 0, "main", "ps_5_0", 0, 0, &psb, &errps)))
		{
			if (errps) OutputDebugStringA((char*)errps->GetBufferPointer());
			return false;
		}
		if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &m_prefilterPS)))
			return false;

		{
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = 80;   // float4x4(64) + float(4) + pad(12)
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			device->CreateBuffer(&bd, nullptr, &m_cbPrefilter);
		}
	}
	// BRDF LUT PS
	{
		Microsoft::WRL::ComPtr<ID3DBlob> psb, errps;
		if (FAILED(D3DCompile(kBRDFLUTPS, strlen(kBRDFLUTPS), 0, 0, 0, "main", "ps_5_0", 0, 0, &psb, &errps)))
		{
			if (errps) OutputDebugStringA((char*)errps->GetBufferPointer());
			return false;
		}
		if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &m_brdfPS)))
			return false;
	}

	//ShadowMap
	{
		{
			Microsoft::WRL::ComPtr<ID3DBlob> vsb, err;
			if (FAILED(D3DCompile(kShadowVS, strlen(kShadowVS), 0, 0, 0, "main", "vs_5_0", 0, 0, &vsb, &err)))
			{
				if (err) OutputDebugStringA((char*)err->GetBufferPointer());
				return false;
			}
			if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), 0, &m_shadowVS)))
				return false;

			// ★入力レイアウト（KdMeshVertex: POSITION, TEXCOORD, NORMAL）
			D3D11_INPUT_ELEMENT_DESC layout[] = {
				{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
				{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			};
			device->CreateInputLayout(layout, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &m_shadowInputLayout);
		}

		// cbShadow（lightWVP）
		{
			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = 64;   // float4x4
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			device->CreateBuffer(&bd, nullptr, &m_cbShadow);
		}

		// 影用アルファクリップPS
		{
			OutputDebugStringA("[shadow] alpha clip PS start\n");
			Microsoft::WRL::ComPtr<ID3DBlob> psb, errps;
			if (FAILED(D3DCompile(kShadowAlphaPS, strlen(kShadowAlphaPS),
				0, 0, 0, "main", "ps_5_0", 0, 0, &psb, &errps)))
			{
				if (errps) OutputDebugStringA((char*)errps->GetBufferPointer());
				return false;
			}
			if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(),
				psb->GetBufferSize(), 0, &m_shadowAlphaPS)))
				return false;

			D3D11_BUFFER_DESC bd = {};
			bd.ByteWidth = 16;   // float + pad
			bd.Usage = D3D11_USAGE_DYNAMIC;
			bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			if (FAILED(device->CreateBuffer(&bd, nullptr, &m_cbShadowClip)))
				return false;

			OutputDebugStringA("[shadow] alpha clip PS ready\n");
		}

	}

	// サンプラー
	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	device->CreateSamplerState(&sd, &m_sampler);

	// cbuffer
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = (sizeof(CBData) + 15) & ~15;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		device->CreateBuffer(&bd, 0, &m_cb);
	}

	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = 144;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		device->CreateBuffer(&bd, nullptr, &m_cbRaymarchCam);
	}

	{
		D3D11_DEPTH_STENCIL_DESC dsd = {};
		dsd.DepthEnable = TRUE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dsd.DepthFunc = D3D11_COMPARISON_LESS;
		device->CreateDepthStencilState(&dsd, &m_depthState);
	}

	GenerateIrradianceMap(context);
	GeneratePrefilterMap(context);
	GenerateBRDFLut(context);

	return true;
}

bool ShaderRuntime::CompilePS(const std::string& code, ComPtr<ID3D11PixelShader>& outPS)
{
	Microsoft::WRL::ComPtr<ID3DBlob> psb, err;
	if (FAILED(D3DCompile(code.c_str(), code.size(), 0, 0, 0, "main", "ps_5_0", 0, 0, &psb, &err)))
	{
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}
	if (FAILED(m_device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), 0, &outPS)))
		return false;
	return true;
}

bool ShaderRuntime::RegisterEffect(const std::string& name, const std::string& psCode)
{
	EffectEntry entry;
	if (!CompilePS(psCode, entry.ps))
	{
		OutputDebugStringA(("RegisterEffect FAILED: " + name + "\n").c_str());
		return false;
	}
	m_effects[name] = entry;
	return true;
}

bool ShaderRuntime::HasEffect(const std::string& name) const
{
	return m_effects.find(name) != m_effects.end();
}

void ShaderRuntime::ApplyEffect(const std::string& name,
	ID3D11DeviceContext* ctx,
	ID3D11ShaderResourceView* const* textures, int textureCount,
	ID3D11RenderTargetView* outRTV, int width, int height,
	ID3D11DepthStencilView* depthDSV)
{
	auto it = m_effects.find(name);
	if (it == m_effects.end()) return;

	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		CBData d = {};
		d.time = m_time; d.bass = m_bass; d.mid = m_mid; d.treble = m_treble;
		// ★params を詰める（BindMaterialと同じ）
		for (int i = 0; i < (int)it->second.params.size() && i < 16; ++i)
		{
			d.params[i][0] = it->second.params[i].value[0];
			d.params[i][1] = it->second.params[i].value[1];
			d.params[i][2] = it->second.params[i].value[2];
			d.params[i][3] = it->second.params[i].value[3];
		}
		memcpy(m.pData, &d, sizeof(d));
		ctx->Unmap(m_cb.Get(), 0);
	}

	if (m_cbRaymarchCam)
	{
		D3D11_MAPPED_SUBRESOURCE mc;
		if (SUCCEEDED(ctx->Map(m_cbRaymarchCam.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mc)))
		{
			struct CamCB { 
				DirectX::XMFLOAT3 camPos; float pad; 
				DirectX::XMFLOAT4X4 invViewProj; 
				DirectX::XMFLOAT4X4 viewProj;
			} c;
			c.camPos = m_camPos;
			c.pad = 0;
			c.invViewProj = m_invViewProj;
			c.viewProj = m_viewProj;
			memcpy(mc.pData, &c, sizeof(c));
			ctx->Unmap(m_cbRaymarchCam.Get(), 0);
		}
	}

	D3D11_VIEWPORT vp = {};
	vp.Width = (float)width; vp.Height = (float)height; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);
	ctx->OMSetRenderTargets(1, &outRTV, depthDSV);
	if (depthDSV && m_depthState)
		ctx->OMSetDepthStencilState(m_depthState.Get(), 0);

	ctx->VSSetShader(m_vs.Get(), 0, 0);
	ctx->PSSetShader(it->second.ps.Get(), 0, 0);
	ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	if (m_cbRaymarchCam)
		ctx->PSSetConstantBuffers(3, 1, m_cbRaymarchCam.GetAddressOf());
	ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
	if (textureCount > 0)
		ctx->PSSetShaderResources(0, textureCount, textures);

	//ctx->PSSetConstantBuffers(0,1, m_cbPrefilter.GetAddressOf());
	//ctx->PSSetShaderResources(0, 1, ShaderHelper::shadowMapSRV.GetAddressOf());
	//ctx->PSSetShader(m_testPS.Get(), 0, 0);

	ctx->IASetInputLayout(0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->Draw(3, 0);

	ID3D11ShaderResourceView* nullSRV[8] = {};
	ctx->PSSetShaderResources(0, 8, nullSRV);
}

bool ShaderRuntime::RegisterEffectFromFile(const std::string& name, const std::string& filepath)
{
	std::ifstream f(filepath);
	if (!f) {
		OutputDebugStringA(("file not found: " + filepath + "\n").c_str());
		return false;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	return RegisterEffect(name, ss.str());
}

void ShaderRuntime::LoadEffectsFromFolder(const std::string& folderPath)
{
	namespace fs = std::filesystem;
	if (!fs::exists(folderPath)) return;

	for (const auto& entry : fs::directory_iterator(folderPath))
	{
		if (entry.path().extension() == ".hlsl")
		{
			// ファイル名（拡張子なし）をエフェクト名に
			std::string name = entry.path().stem().string();
			RegisterEffectFromFile(name, entry.path().string());
			OutputDebugStringA(("loaded effect: " + name + "\n").c_str());
		}
	}
}

bool ShaderRuntime::ReloadEffect(const std::string& name, const std::string& psCode)
{
	ComPtr<ID3D11PixelShader> newPS;
	if (!CompilePS(psCode, newPS))
	{
		OutputDebugStringA(("ReloadEffect FAILED (compile): " + name + "\n").c_str());
		return false;   // 失敗時は古いシェーダを保持（クラッシュしない）
	}

	// 未登録なら新規追加、登録済みなら差し替え
	m_effects[name].ps = newPS;
	OutputDebugStringA(("ReloadEffect OK: " + name + "\n").c_str());
	return true;
}

bool ShaderRuntime::ReloadEffectFromFile(const std::string& name, const std::string& filepath)
{
	std::ifstream f(filepath);
	if (!f) {
		OutputDebugStringA(("reload: file not found: " + filepath + "\n").c_str());
		return false;
	}
	std::stringstream ss;
	ss << f.rdbuf();
	return ReloadEffect(name, ss.str());
}

bool ShaderRuntime::StartNetwork(unsigned short port, const char* bindAddr)
{
	return m_liveLink.Start(port, bindAddr);
}

void ShaderRuntime::StopNetwork()
{
	m_liveLink.Stop();
}

void ShaderRuntime::PollNetwork()
{
	if (!m_liveLink.IsActive()) return;
	m_liveLink.Poll([this](const std::string& header, const std::string& body)
		{
			HandleLiveMessage(header, body);
		});
}

void ShaderRuntime::HandleLiveMessage(const std::string& header, const std::string& body)
{
	OutputDebugStringA(("[LiveLink] recv " + header.substr(0, 64) + " (" +
		std::to_string(body.size()) + " bytes)\n").c_str());

	// 完全一致のヘッダ
	struct ExactRoute
	{
		const char* header;
		void (*fn)(ShaderRuntime& self, const std::string& body);
	};
	static const ExactRoute kExact[] =
	{
		{ "asset", [](ShaderRuntime& self, const std::string& b)
			{
				ShaderAsset asset;
				if (asset.LoadFromString(b))
				{
					self.RegisterFromAsset(asset);   // 既存なら定義ごと差し替え
					OutputDebugStringA(("[LiveLink] asset updated: " + asset.name + "\n").c_str());
				}
			} },
		{ "pipeline", [](ShaderRuntime& self, const std::string& b)
			{ if (self.m_pipelineCallback) self.m_pipelineCallback(b); } },
		{ "particle", [](ShaderRuntime& self, const std::string& b)
			{ if (self.m_particleCallback) self.m_particleCallback(b); } },
		{ "trail", [](ShaderRuntime& self, const std::string& b)
			{ if (self.m_trailCallback) self.m_trailCallback(b); } },
	};
	for (const auto& r : kExact)
	{
		if (header == r.header) { r.fn(*this, body); return; }
	}

	// "接頭辞:名前" のヘッダ
	struct PrefixRoute
	{
		const char* prefix;
		void (*fn)(ShaderRuntime& self, const std::string& name, const std::string& body);
	};
	static const PrefixRoute kPrefix[] =
	{
		{ "mat:", [](ShaderRuntime& self, const std::string& n, const std::string& b)
			{ self.ReloadMaterialPS(n, b); } },
		{ "fx:", [](ShaderRuntime& self, const std::string& n, const std::string& b)
			{ self.ReloadEffect(n, b); } },
	};
	for (const auto& r : kPrefix)
	{
		const size_t len = strlen(r.prefix);
		if (header.compare(0, len, r.prefix) == 0)
		{
			r.fn(*this, header.substr(len), body);
			return;
		}
	}

	OutputDebugStringA(("[LiveLink] unknown header ignored: " + header.substr(0, 64) + "\n").c_str());
}

bool ShaderRuntime::RegisterMaterial(const std::string& name,
	const std::string& vsCode, const std::string& psCode)
{
	// ★既存エントリのレイアウト指定を退避（あれば）
	std::vector<VertexElement> savedLayout;
	std::vector<ParamRuntime> savedParams;   // paramsも退避したいなら
	auto existing = m_materials.find(name);
	if (existing != m_materials.end())
	{
		savedLayout = existing->second.layoutSpec;
	}

	MaterialEntry entry;

	// VSコンパイル + バイトコード保持
	ComPtr<ID3DBlob> vsb, err;
	if (FAILED(D3DCompile(vsCode.c_str(), vsCode.size(), 0, 0, 0, "main", "vs_5_0", 0, 0, &vsb, &err)))
	{
		if (err) OutputDebugStringA((char*)err->GetBufferPointer());
		return false;
	}
	if (FAILED(m_device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), 0, &entry.vs)))
		return false;
	entry.vsBytecode.resize(vsb->GetBufferSize());
	memcpy(entry.vsBytecode.data(), vsb->GetBufferPointer(), vsb->GetBufferSize());

	if (!CompilePS(psCode, entry.ps)) return false;

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 2;
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	m_device->CreateBuffer(&bd, 0, &entry.cbTransform);

	// ★退避したレイアウト指定を引き継ぐ
	entry.layoutSpec = savedLayout;

	m_materials[name] = std::move(entry);
	++m_materialRevision;

	// ★レイアウト指定があれば、新しいVSバイトコードで作り直す
	if (!savedLayout.empty())
	{
		BuildMaterialLayout(name, savedLayout);
	}

	return true;
}
bool ShaderRuntime::RegisterMaterialFromFiles(const std::string& name,
	const std::string& vsPath, const std::string& psPath)
{
	// VS読み込み
	std::ifstream vf(vsPath);
	if (!vf) {
		OutputDebugStringA(("material VS not found: " + vsPath + "\n").c_str());
		return false;
	}
	std::stringstream vss; vss << vf.rdbuf();

	// PS読み込み
	std::ifstream pf(psPath);
	if (!pf) {
		OutputDebugStringA(("material PS not found: " + psPath + "\n").c_str());
		return false;
	}
	std::stringstream pss; pss << pf.rdbuf();

	return RegisterMaterial(name, vss.str(), pss.str());
}

void ShaderRuntime::SetMaterialMatrices(const std::string& name,
	const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& worldView)
{
	auto it = m_materials.find(name);
	if (it == m_materials.end()) return;
	DirectX::XMStoreFloat4x4(&it->second.wvp, wvp);
	DirectX::XMStoreFloat4x4(&it->second.worldView, worldView);
}

const void* ShaderRuntime::GetMaterialVSBytecode(const std::string& name) const
{
    auto it = m_materials.find(name);
    if (it == m_materials.end()) return nullptr;
    return it->second.vsBytecode.data();
}

size_t ShaderRuntime::GetMaterialVSBytecodeSize(const std::string& name) const
{
    auto it = m_materials.find(name);
    if (it == m_materials.end()) return 0;
    return it->second.vsBytecode.size();
}

bool ShaderRuntime::ReloadMaterialPS(const std::string& name, const std::string& psCode)
{
	auto it = m_materials.find(name);
	if (it == m_materials.end()) return false;

	ComPtr<ID3D11PixelShader> newPS;
	if (!CompilePS(psCode, newPS))
	{
		OutputDebugStringA(("ReloadMaterialPS FAILED: " + name + "\n").c_str());
		return false;   // 古いPS保持
	}
	it->second.ps = newPS;
	++m_materialRevision;
	return true;
}

// ----------------------------------------------------------
// 対象ファイルの一覧を作る
// ----------------------------------------------------------
std::vector<std::string> ShaderRuntime::CollectAssetFiles(const std::string& folderPath) const
{
	std::vector<std::string> files;

	std::error_code ec;
	if (!std::filesystem::exists(folderPath, ec)) { return files; }

	for (const auto& entry :
		std::filesystem::directory_iterator(folderPath, ec))
	{
		if (!entry.is_regular_file()) { continue; }

		// TODO: 既存の LoadAssetsFromFolder と同じ拡張子判定にする
		if (entry.path().extension() != ".json") { continue; }

		files.push_back(entry.path().string());
	}

	// 走査順は環境依存なので固定する
	// 登録順が変わるとマテリアルIDがずれる
	std::sort(files.begin(), files.end());

	return files;
}

// ----------------------------------------------------------
// 1件を読み込んで登録する
// ----------------------------------------------------------
bool ShaderRuntime::LoadAssetFile(const std::string& filePath)
{
	// TODO: 既存の LoadAssetsFromFolder のループ内と同じ処理にする
	//       おおむね以下の形のはず
	ShaderAsset asset;
	if (!asset.LoadFromFile(filePath)) { return false; }

	RegisterFromAsset(asset);
	return true;
}

// ----------------------------------------------------------
// 一括ロード(同期)
// ----------------------------------------------------------
void ShaderRuntime::LoadAssetsFromFolder(const std::string& folderPath)
{
	for (const auto& path : CollectAssetFiles(folderPath))
	{
		LoadAssetFile(path);
	}
}

std::vector<std::string> ShaderRuntime::GetMaterialNames() const
{
	std::vector<std::string> names;
	for (auto& [name, entry] : m_materials)
		names.push_back(name);
	return names;
}

void ShaderRuntime::BindMaterial(const std::string& name, ID3D11DeviceContext* ctx)
{
	auto it = m_materials.find(name);
	if (it == m_materials.end()) return;
	auto& mat = it->second;

	D3D11_MAPPED_SUBRESOURCE m;

	// --- b1: 行列cbuffer ---
	if (SUCCEEDED(ctx->Map(mat.cbTransform.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		struct { DirectX::XMFLOAT4X4 wvp, wv; } td{ mat.wvp, mat.worldView };
		memcpy(m.pData, &td, sizeof(td));
		ctx->Unmap(mat.cbTransform.Get(), 0);
	}

	// --- b0: 共有effect cbuffer（time+audio+params）---
	if (SUCCEEDED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		CBData d = {};
		d.time = m_time; d.bass = m_bass; d.mid = m_mid; d.treble = m_treble;
		for (int i = 0; i < (int)mat.params.size() && i < 16; ++i)
		{
			d.params[i][0] = mat.params[i].value[0];
			d.params[i][1] = mat.params[i].value[1];
			d.params[i][2] = mat.params[i].value[2];
			d.params[i][3] = mat.params[i].value[3];
			d.params[15][0] = m_sceneLightDir.x;
			d.params[15][1] = m_sceneLightDir.y;
			d.params[15][2] = m_sceneLightDir.z;
			d.params[15][3] = 0.0f;
		}
		memcpy(m.pData, &d, sizeof(d));
		ctx->Unmap(m_cb.Get(), 0);
	}

	ctx->VSSetShader(mat.vs.Get(), 0, 0);
	ctx->PSSetShader(mat.ps.Get(), 0, 0);
	ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	ctx->VSSetConstantBuffers(1, 1, mat.cbTransform.GetAddressOf());
	if (mat.cbBones)
		ctx->VSSetConstantBuffers(2, 1, mat.cbBones.GetAddressOf());
	ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

	if (mat.inputLayout)
		ctx->IASetInputLayout(mat.inputLayout.Get());
}

void ShaderRuntime::RegisterFromAsset(const ShaderAsset& asset)
{
	if (asset.type == "material")
	{
		RegisterMaterial(asset.name, asset.vs, asset.ps);   // コード更新
		// パラメータ定義を作り直す
		auto& entry = m_materials[asset.name];
		entry.params.clear();   // ★古い定義をクリア
		for (auto& p : asset.params)
		{
			ParamRuntime pr;
			pr.name = p.name;
			pr.type = p.type;
			pr.value = p.defaultValue;
			pr.minVal = p.minVal;
			pr.maxVal = p.maxVal;
			entry.params.push_back(pr);
		}
		entry.writeMask = asset.writeMask;
		++m_materialRevision;
	}
	else  // effect
	{
		RegisterEffect(asset.name, asset.ps);
		auto& entry = m_effects[asset.name];
		entry.params.clear();   // ★
		for (auto& p : asset.params)
		{
			ParamRuntime pr;
			pr.name = p.name; pr.type = p.type;
			pr.value = p.defaultValue;
			pr.minVal = p.minVal; pr.maxVal = p.maxVal;
			entry.params.push_back(pr);
		}
	}
}

void ShaderRuntime::SetParam(const std::string& effectName, const std::string& paramName, const float* values, int count)
{
	// エフェクトを探す
	auto fxIt = m_effects.find(effectName);
	if (fxIt != m_effects.end())
	{
		for (auto& pr : fxIt->second.params)
			if (pr.name == paramName)
			{
				for (int i = 0; i < count && i < 4; ++i) pr.value[i] = values[i];
				return;
			}
	}
	// マテリアルを探す
	auto matIt = m_materials.find(effectName);
	if (matIt != m_materials.end())
	{
		for (auto& pr : matIt->second.params)
			if (pr.name == paramName)
			{
				for (int i = 0; i < count && i < 4; ++i) pr.value[i] = values[i];
				++m_materialRevision;
				return;
			}
	}
}

bool ShaderRuntime::BuildMaterialLayout(const std::string& name,
	const std::vector<VertexElement>& layout)
{
	auto it = m_materials.find(name);
	if (it == m_materials.end() || layout.empty()) return false;
	if (it->second.vsBytecode.empty()) return false;

	it->second.layoutSpec = layout;

	auto formatInfo = [](const std::string& f) -> std::pair<DXGI_FORMAT, UINT> {
		if (f == "float")  return { DXGI_FORMAT_R32_FLOAT, 4 };
		if (f == "float2") return { DXGI_FORMAT_R32G32_FLOAT, 8 };
		if (f == "float3") return { DXGI_FORMAT_R32G32B32_FLOAT, 12 };
		if (f == "float4") return { DXGI_FORMAT_R32G32B32A32_FLOAT, 16 };
		if (f == "uint4")  return { DXGI_FORMAT_R16G16B16A16_UINT, 8 };
		return { DXGI_FORMAT_R32G32B32_FLOAT, 12 };
		};

	std::vector<D3D11_INPUT_ELEMENT_DESC> desc;
	std::vector<std::string> semanticStorage;   // c_str()の寿命を保つ
	semanticStorage.reserve(layout.size());

	UINT autoOffset = 0;
	for (auto& e : layout)
	{
		auto [fmt, size] = formatInfo(e.format);
		semanticStorage.push_back(e.semantic);

		UINT off = (e.offset >= 0) ? (UINT)e.offset : autoOffset;

		D3D11_INPUT_ELEMENT_DESC d = {};
		d.SemanticName = semanticStorage.back().c_str();
		d.SemanticIndex = e.index;
		d.Format = fmt;
		d.InputSlot = 0;
		d.AlignedByteOffset = off;
		d.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		d.InstanceDataStepRate = 0;
		desc.push_back(d);

		autoOffset = off + size;   // 次の自動オフセット
	}

	HRESULT hr = m_device->CreateInputLayout(
		desc.data(), (UINT)desc.size(),
		it->second.vsBytecode.data(),
		it->second.vsBytecode.size(),
		it->second.inputLayout.ReleaseAndGetAddressOf());

	if (FAILED(hr))
	{
		OutputDebugStringA(("BuildMaterialLayout FAILED: " + name + "\n").c_str());
		return false;
	}
	return true;
}
void ShaderRuntime::DrawParamUI(const std::string& name)
{
    // エフェクトかマテリアルか
    std::vector<ParamRuntime>* params = nullptr;
    auto fxIt = m_effects.find(name);
    if (fxIt != m_effects.end()) params = &fxIt->second.params;
    auto matIt = m_materials.find(name);
    if (matIt != m_materials.end()) params = &matIt->second.params;

    if (!params) return;

    ImGui::Text("%s", name.c_str());
    for (auto& p : *params)
    {
        if (p.type == "float")
            ImGui::SliderFloat(p.name.c_str(), &p.value[0], p.minVal, p.maxVal);
        else if (p.type == "float3")
            ImGui::ColorEdit3(p.name.c_str(), p.value.data());   // 色として編集
		else if (p.type == "float4") {
			ImGui::ColorEdit4(p.name.c_str(), p.value.data());   // 色として編集
			ImGui::DragFloat4(p.name.c_str(), p.value.data(), 0.01f, p.minVal, p.maxVal); // ドラッグで編集
		}
        else
            ImGui::SliderFloat4(p.name.c_str(), p.value.data(), p.minVal, p.maxVal);
    }
}
void ShaderRuntime::SetRaymarchCamera(const DirectX::XMMATRIX& view,
	const DirectX::XMMATRIX& proj, const DirectX::XMFLOAT3& camPos)
{
	using namespace DirectX;
	m_camPos = camPos;
	XMMATRIX viewProj = view * proj;
	XMMATRIX invVP = XMMatrixInverse(nullptr, viewProj);
	XMStoreFloat4x4(&m_invViewProj, invVP);
	XMStoreFloat4x4(&m_viewProj, viewProj);
}

bool ShaderRuntime::SetMaterialParam(const std::string& matName,
	const std::string& paramName, float x, float y, float z, float w)
{
	auto it = m_materials.find(matName);
	if (it == m_materials.end()) return false;
	for (auto& p : it->second.params)
	{
		if (p.name == paramName)
		{
			p.value[0] = x; p.value[1] = y;
			p.value[2] = z; p.value[3] = w;
			++m_materialRevision;
			return true;
		}
	}
	return false;
}

void ShaderRuntime::SetBones(const std::string& matName,
	const DirectX::XMFLOAT4X4* bones, int count)
{
	auto it = m_materials.find(matName);
	if (it == m_materials.end()) return;
	auto& mat = it->second;

	// cbBones 未作成なら作る（300行列分）
	if (!mat.cbBones)
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * 300;   // maxBoneBufferSize
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		m_device->CreateBuffer(&bd, nullptr, &mat.cbBones);
	}

	// 転送
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(m_context->Map(mat.cbBones.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		int n = (count < 300) ? count : 300;
		memcpy(mapped.pData, bones, sizeof(DirectX::XMFLOAT4X4) * n);
		m_context->Unmap(mat.cbBones.Get(), 0);
	}
}
void ShaderRuntime::GenerateIrradianceMap(ID3D11DeviceContext* ctx)
{
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	using namespace DirectX;
	struct FaceView { XMFLOAT3 forward, up; };
	static const FaceView kFaces[6] = {
		{ { 1, 0, 0}, {0, 1, 0} },
		{ {-1, 0, 0}, {0, 1, 0} },
		{ { 0, 1, 0}, {0, 0,-1} },
		{ { 0,-1, 0}, {0, 0, 1} },
		{ { 0, 0, 1}, {0, 1, 0} },
		{ { 0, 0,-1}, {0, 1, 0} },
	};

	int size = 32;
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)size; vp.Height = (float)size; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	ctx->RSSetState(nullptr);

	ctx->PSSetShaderResources(0, 1, ShaderHelper::cubeSRV.GetAddressOf());
	ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
	ctx->PSSetShader(m_irradiancePS.Get(), nullptr, 0);
	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (int i = 0; i < 6; ++i)
	{
		XMVECTOR fwd = XMLoadFloat3(&kFaces[i].forward);
		XMVECTOR up = XMLoadFloat3(&kFaces[i].up);
		XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, fwd));
		up = XMVector3Cross(fwd, right);
		XMMATRIX faceMat = XMMatrixIdentity();
		faceMat.r[0] = right;
		faceMat.r[1] = up;
		faceMat.r[2] = fwd;

		D3D11_MAPPED_SUBRESOURCE m;
		if (SUCCEEDED(ctx->Map(m_cbFace.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
		{
			XMFLOAT4X4 fm;
			XMStoreFloat4x4(&fm, faceMat);
			memcpy(m.pData, &fm, sizeof(fm));
			ctx->Unmap(m_cbFace.Get(), 0);
		}
		ctx->PSSetConstantBuffers(0, 1, m_cbFace.GetAddressOf());

		ID3D11RenderTargetView* rtv = ShaderHelper::irradianceRTVs[i].Get();
		ctx->OMSetRenderTargets(1, &rtv, nullptr);
		ctx->Draw(3, 0);
	}

	ID3D11ShaderResourceView* nullSRV[1] = {};
	ctx->PSSetShaderResources(0, 1, nullSRV);

	ID3D11RenderTargetView* nullRTV[1] = { nullptr };
	ctx->OMSetRenderTargets(1, nullRTV, nullptr);

	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0)
		ctx->RSSetViewports(1, &oldVP);

	// 退避したものを解放
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
}

void ShaderRuntime::GeneratePrefilterMap(ID3D11DeviceContext* ctx)
{
	// ステート退避（イラディアンスと同じ）
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	using namespace DirectX;
	struct FaceView { XMFLOAT3 forward, up; };
	static const FaceView kFaces[6] = {
		{ { 1, 0, 0}, {0, 1, 0} },
		{ {-1, 0, 0}, {0, 1, 0} },
		{ { 0, 1, 0}, {0, 0,-1} },
		{ { 0,-1, 0}, {0, 0, 1} },
		{ { 0, 0, 1}, {0, 1, 0} },
		{ { 0, 0,-1}, {0, 1, 0} },
	};

	int baseSize = 128;
	int mipLevels = 5;

	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	ctx->RSSetState(nullptr);

	ctx->PSSetShaderResources(0, 1, ShaderHelper::cubeSRV.GetAddressOf());   // 元の環境マップ
	ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
	ctx->PSSetShader(m_prefilterPS.Get(), nullptr, 0);
	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	for (int mip = 0; mip < mipLevels; ++mip)
	{
		int mipSize = baseSize >> mip;   // ミップごとにサイズが半分
		float roughness = (float)mip / (float)(mipLevels - 1);   // 0, 0.25, 0.5, 0.75, 1.0

		D3D11_VIEWPORT vp = {};
		vp.Width = (float)mipSize; vp.Height = (float)mipSize; vp.MaxDepth = 1.0f;
		ctx->RSSetViewports(1, &vp);

		for (int face = 0; face < 6; ++face)
		{
			// faceMat
			XMVECTOR fwd = XMLoadFloat3(&kFaces[face].forward);
			XMVECTOR up = XMLoadFloat3(&kFaces[face].up);
			XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, fwd));
			up = XMVector3Cross(fwd, right);
			XMMATRIX faceMat = XMMatrixIdentity();
			faceMat.r[0] = right;
			faceMat.r[1] = up;
			faceMat.r[2] = fwd;

			// cbPrefilter に faceMat + roughness
			D3D11_MAPPED_SUBRESOURCE m;
			if (SUCCEEDED(ctx->Map(m_cbPrefilter.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
			{
				struct PrefilterCB {
					XMFLOAT4X4 faceMat;
					float roughness;
					float pad[3];
				} cb;
				XMStoreFloat4x4(&cb.faceMat, faceMat);
				cb.roughness = roughness;
				memcpy(m.pData, &cb, sizeof(cb));
				ctx->Unmap(m_cbPrefilter.Get(), 0);
			}
			ctx->PSSetConstantBuffers(0, 1, m_cbPrefilter.GetAddressOf());

			// このミップ・この面のRTVに描く
			ID3D11RenderTargetView* rtv = ShaderHelper::prefilterRTVs[mip][face].Get();
			ctx->OMSetRenderTargets(1, &rtv, nullptr);
			ctx->Draw(3, 0);
		}
	}

	// クリーンアップ + ステート復元
	ID3D11ShaderResourceView* nullSRV[1] = {};
	ctx->PSSetShaderResources(0, 1, nullSRV);
	ID3D11RenderTargetView* nullRTV[1] = { nullptr };
	ctx->OMSetRenderTargets(1, nullRTV, nullptr);
	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) ctx->RSSetViewports(1, &oldVP);
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
}

void ShaderRuntime::GenerateBRDFLut(ID3D11DeviceContext* ctx)
{
	OutputDebugStringA("[brdf] GenerateBRDFLut called\n");
	// ステート退避
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	int size = 512;
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)size; vp.Height = (float)size; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	// ★ラスタライザ、ブレンド、深度を明示的に設定
	ctx->RSSetState(nullptr);                          // デフォルトラスタライザ（カリングに注意）
	ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF); // ブレンドなし
	ctx->OMSetDepthStencilState(nullptr, 0);            // 深度テストなし

	if (m_brdfPS == nullptr)
		OutputDebugStringA("[brdf] m_brdfPS is NULL!\n");
	ctx->PSSetShader(m_brdfPS.Get(), nullptr, 0);
	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->IASetInputLayout(nullptr);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ID3D11RenderTargetView* rtv = ShaderHelper::brdfLutRTV.Get();

	if (rtv == nullptr)
		OutputDebugStringA("[brdf] brdfLutRTV is NULL!\n");

	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	ctx->Draw(3, 0);

	// クリーンアップ + 復元
	ID3D11RenderTargetView* nullRTV[1] = { nullptr };
	ctx->OMSetRenderTargets(1, nullRTV, nullptr);
	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) ctx->RSSetViewports(1, &oldVP);
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();

	OutputDebugStringA("[brdf] Draw done\n");
}

void ShaderRuntime::RenderShadowMap(ID3D11DeviceContext* ctx,
	const DirectX::XMMATRIX& lightViewProj,
	std::function<void(const DirectX::XMMATRIX&)> drawModels)
{
	ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
	ctx->PSSetShaderResources(18, 1, nullSRV);
	// ステート退避
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	// シャドウマップをクリア
	ctx->ClearDepthStencilView(ShaderHelper::shadowMapDSV.Get(),
		D3D11_CLEAR_DEPTH, 1.0f, 0);

	// 深度だけ描く（カラーRTなし）
	ID3D11RenderTargetView* nullRTV[1] = { nullptr };
	ctx->OMSetRenderTargets(1, nullRTV, ShaderHelper::shadowMapDSV.Get());

	int size = 2048;
	D3D11_VIEWPORT vp = {};
	vp.Width = (float)size; vp.Height = (float)size; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	ctx->VSSetShader(m_shadowVS.Get(), nullptr, 0);

	ctx->PSSetShader(nullptr, nullptr, 0);
	
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	ctx->RSSetState(nullptr);   // デフォルト（カリング注意）
	ctx->IASetInputLayout(m_shadowInputLayout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetConstantBuffers(0, 1, m_cbShadow.GetAddressOf());

	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);

	// モデルを描く（コールバックが lightViewProj を使って、各モデルを描く）
	drawModels(lightViewProj);

	// ステート復元
	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) ctx->RSSetViewports(1, &oldVP);
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
}

// 各モデルの lightWVP を設定するヘルパー
void ShaderRuntime::SetShadowMatrix(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& lightWVP)
{
	D3D11_MAPPED_SUBRESOURCE m;
	if (SUCCEEDED(ctx->Map(m_cbShadow.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
	{
		DirectX::XMFLOAT4X4 mat;
		DirectX::XMStoreFloat4x4(&mat, lightWVP);
		memcpy(m.pData, &mat, sizeof(mat));
		ctx->Unmap(m_cbShadow.Get(), 0);
	}
}

ID3D11PixelShader* ShaderRuntime::GetMaterialPS(const std::string& name) const
{
	auto it = m_materials.find(name);
	if (it == m_materials.end()) return nullptr;
	return it->second.ps.Get();
}

bool ShaderRuntime::GetMaterialParamDefaults(const std::string& name,
	std::vector<std::array<float, 4>>& out) const
{
	auto it = m_materials.find(name);
	if (it == m_materials.end()) return false;
	out.clear();
	for (const auto& p : it->second.params)
		out.push_back(p.value);
	return true;
}

void ShaderRuntime::RenderShadowMapTo(ID3D11DeviceContext* ctx,
	ID3D11DepthStencilView* dsv, int size,
	const DirectX::XMMATRIX& lightViewProj,
	std::function<void(const DirectX::XMMATRIX&)> drawModels)
{
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	ctx->ClearDepthStencilView(dsv, D3D11_CLEAR_DEPTH, 1.0f, 0);   // 引数のdsv

	ID3D11RenderTargetView* nullRTV[1] = { nullptr };
	ctx->OMSetRenderTargets(1, nullRTV, dsv);                      // 引数のdsv

	// カスケードSRVを外す（DSVとのハザード防止）
	{
		ID3D11ShaderResourceView* nulls[3] = { nullptr, nullptr, nullptr };
		ctx->PSSetShaderResources(23, 3, nulls);
	}

	D3D11_VIEWPORT vp = {};
	vp.Width = (float)size; vp.Height = (float)size; vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	ctx->VSSetShader(m_shadowVS.Get(), nullptr, 0);
	// シャドウクリップする用
	if (m_shadowAlphaClip && m_shadowAlphaPS)
	{
		D3D11_MAPPED_SUBRESOURCE mc;
		if (SUCCEEDED(ctx->Map(m_cbShadowClip.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mc)))
		{
			float c[4] = { m_shadowAlphaThreshold, 0, 0, 0 };
			memcpy(mc.pData, c, sizeof(c));
			ctx->Unmap(m_cbShadowClip.Get(), 0);
		}

		ctx->PSSetShader(m_shadowAlphaPS.Get(), nullptr, 0);
		ctx->PSSetConstantBuffers(1, 1, m_cbShadowClip.GetAddressOf());
		ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

		// 白テクスチャ：誰もテクスチャを貼らない描画のために、白を既定で入れておく
		ID3D11ShaderResourceView* white =
			KdDirect3D::Instance().GetWhiteTex()->WorkSRView();
		ctx->PSSetShaderResources(0, 1, &white);
	}
	else
	{
		ctx->PSSetShader(nullptr, nullptr, 0);
	}
	ctx->OMSetDepthStencilState(nullptr, 0);
	ctx->OMSetBlendState(nullptr, nullptr, 0xFFFFFFFF);
	ctx->RSSetState(nullptr);
	ctx->IASetInputLayout(m_shadowInputLayout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetConstantBuffers(0, 1, m_cbShadow.GetAddressOf());

	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);

	drawModels(lightViewProj);
	{
		//　シャドウクリップPSの掃除
		ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
		ctx->PSSetShaderResources(0, 1, nullSRV);
	}
	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) ctx->RSSetViewports(1, &oldVP);
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
}

unsigned int ShaderRuntime::GetMaterialWriteMask(const std::string& name) const
{
	auto it = m_materials.find(name);
	return (it != m_materials.end()) ? it->second.writeMask : 0xF;
}

ID3D11VertexShader* ShaderRuntime::GetMaterialVS(const std::string& n) const
{
	auto it = m_materials.find(n); return it != m_materials.end() ? it->second.vs.Get() : nullptr;
}

ID3D11InputLayout* ShaderRuntime::GetMaterialLayout(const std::string& n) const
{
	auto it = m_materials.find(n); return it != m_materials.end() ? it->second.inputLayout.Get() : nullptr;
}

ID3D11VertexShader* ShaderRuntime::GetMaterialVSSkinned(const std::string& n) const
{
	auto it = m_materials.find(n); return it != m_materials.end() ? it->second.vsSkinned.Get() : nullptr;
}

ID3D11InputLayout* ShaderRuntime::GetMaterialLayoutSkinned(const std::string& n) const
{
	auto it = m_materials.find(n); return it != m_materials.end() ? it->second.inputLayoutSkinned.Get() : nullptr;
}

bool ShaderRuntime::HasMaterial(const std::string& n) const
{
	return m_materials.find(n) != m_materials.end();
}