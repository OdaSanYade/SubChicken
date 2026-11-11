#include "GhostRenderer.h"

#include "Shader/GostPS.shaderInc"
#include "Shader/GostVS.shaderInc"

// 残像：共通の重ね描きに残像用シェーダを渡すだけ
bool GhostRenderer::Init()
{
	return InitShared(GostVS, sizeof(GostVS), GostPS, sizeof(GostPS));
}

//==============================================================================
// OverlayModelRenderer（残像・X 線の共通処理）
//==============================================================================
namespace {
	struct GhostCB
	{
		DirectX::XMFLOAT4X4 wvp;
		DirectX::XMFLOAT4X4 world;
		DirectX::XMFLOAT4   color;        
		DirectX::XMFLOAT4   rimColor;    
		DirectX::XMFLOAT4   moveDirSpeed;
		DirectX::XMFLOAT3   camPos;
		int                 boneOffset;
		float               time;
		DirectX::XMFLOAT3   _pad0;
	};
	static_assert(sizeof(GhostCB) % 16 == 0, "GhostCB must be 16-byte aligned");
}

bool OverlayModelRenderer::InitShared(const void* vsCode, size_t vsSize,
	const void* psCode, size_t psSize)
{
	ID3D11Device* dev = KdDirect3D::Instance().WorkDev();

	if (FAILED(dev->CreateVertexShader(vsCode, vsSize, nullptr, &m_vs)))
		return false;

	D3D11_INPUT_ELEMENT_DESC layout[] = {
	{ "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TANGENT",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "SKININDEX",  0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "SKINWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
	if (FAILED(dev->CreateInputLayout(layout, 6, vsCode, vsSize, &m_layout)))
		return false;

	if (FAILED(dev->CreatePixelShader(psCode, psSize, nullptr, &m_ps)))
		return false;

	{
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = sizeof(GhostCB);
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		if (FAILED(dev->CreateBuffer(&d, nullptr, &m_cb))) return false;
	}

	{
		const UINT boneCapacity = kBoneCapacity;

		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = sizeof(DirectX::XMFLOAT4X4) * boneCapacity;
		d.Usage = D3D11_USAGE_DYNAMIC;
		d.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		d.StructureByteStride = sizeof(DirectX::XMFLOAT4X4);
		if (FAILED(dev->CreateBuffer(&d, nullptr, &m_boneBuffer))) return false;

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_UNKNOWN;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
		srvDesc.BufferEx.FirstElement = 0;
		srvDesc.BufferEx.NumElements = boneCapacity;
		if (FAILED(dev->CreateShaderResourceView(m_boneBuffer.Get(), &srvDesc, &m_boneSRV)))
			return false;
	}

	{
		D3D11_DEPTH_STENCIL_DESC dsd = {};
		dsd.DepthEnable = FALSE;
		dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
		dsd.StencilEnable = FALSE;
		dsd.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
		dsd.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;
		dsd.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		dsd.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
		dsd.BackFace = dsd.FrontFace;
		if (FAILED(dev->CreateDepthStencilState(&dsd, &m_depthState))) return false;
	}

	{
		D3D11_RASTERIZER_DESC rd = {};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_BACK;
		rd.DepthClipEnable = TRUE;
		if (FAILED(dev->CreateRasterizerState(&rd, &m_raster))) return false;
	}

	{
		D3D11_BLEND_DESC bd = {};
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(dev->CreateBlendState(&bd, &m_additive))) return false;
	}

	{
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(dev->CreateSamplerState(&sd, &m_sampler))) return false;
	}

	return true;
}

int OverlayModelRenderer::GetModelBoneCount(const std::shared_ptr<KdModelData>& model)
{
	if (!model) return 0;
	auto it = m_boneCountCache.find(model.get());
	if (it != m_boneCountCache.end()) return it->second;

	int count = 0;
	auto& dataNodes = model->GetOriginalNodes();
	for (auto& nodeIdx : model->GetBoneNodeIndices())
	{
		const int bi = dataNodes[nodeIdx].m_boneIndex;
		if (bi >= 0 && bi + 1 > count) count = bi + 1;
	}
	m_boneCountCache[model.get()] = count;
	return count;
}

void OverlayModelRenderer::AddSkinned(const std::shared_ptr<KdModelData>& model,
	const DirectX::XMMATRIX& world, const DirectX::XMFLOAT4& color,
	const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed,
	const DirectX::XMFLOAT4X4* bones, int boneCount)
{
	// モデルが実際に使う本数だけ送る（分からなければ渡された本数）
	int n = (boneCount < kBonesPerInstance) ? boneCount : kBonesPerInstance;
	const int used = GetModelBoneCount(model);
	if (used > 0 && used < n) n = used;
	if (n <= 0 || !bones) { Add(model, world, color, rimColor, moveDirSpeed); return; }

	if ((int)m_boneMatrices.size() + n > kBoneCapacity)
	{
		if (m_dropped++ == 0)
			OutputDebugStringA("[Overlay] bone buffer full. skinned overlays dropped\n");
		return;
	}

	const int offset = (int)m_boneMatrices.size();
	m_boneMatrices.insert(m_boneMatrices.end(), bones, bones + n);

	m_entries.push_back({ model, world, color, rimColor, moveDirSpeed, offset });
}

void OverlayModelRenderer::Render(ID3D11RenderTargetView* rtv,
	const DirectX::XMMATRIX& viewProj, int width, int height,
	ID3D11ShaderResourceView* gbuffer, const DirectX::XMFLOAT3& camPos)
{
	if (m_entries.empty() || !rtv) return;
	ID3D11DeviceContext* ctx = KdDirect3D::Instance().WorkDevContext();

	if (!m_boneMatrices.empty())
	{
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(ctx->Map(m_boneBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, m_boneMatrices.data(),
				sizeof(DirectX::XMFLOAT4X4) * m_boneMatrices.size());
			ctx->Unmap(m_boneBuffer.Get(), 0);
		}
	}
	ctx->VSSetShaderResources(1, 1, m_boneSRV.GetAddressOf());

	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	ctx->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	ID3D11BlendState* oldBlend = nullptr; float oldBF[4] = {}; UINT oldMask = 0;
	ctx->OMGetBlendState(&oldBlend, oldBF, &oldMask);
	ID3D11RasterizerState* oldRS = nullptr; ctx->RSGetState(&oldRS);
	ID3D11DepthStencilState* oldDS = nullptr; UINT oldRef = 0;
	ctx->OMGetDepthStencilState(&oldDS, &oldRef);
	UINT oldVPCount = 1; D3D11_VIEWPORT oldVP = {};
	ctx->RSGetViewports(&oldVPCount, &oldVP);

	ctx->OMSetRenderTargets(1, &rtv, nullptr);
	D3D11_VIEWPORT vp = { 0, 0, (float)width, (float)height, 0, 1 };
	ctx->RSSetViewports(1, &vp);
	ctx->RSSetState(m_raster.Get());
	ctx->OMSetDepthStencilState(m_depthState.Get(), 0);
	float bf[4] = { 0,0,0,0 };
	ctx->OMSetBlendState(m_additive.Get(), bf, 0xFFFFFFFF);

	ctx->IASetInputLayout(m_layout.Get());
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->PSSetShader(m_ps.Get(), nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	ctx->PSSetShaderResources(0, 1, &gbuffer);
	ctx->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

	const float time = Time::Instance().GetTime();
	for (auto& e : m_entries)
	{
		if (!e.model) continue;

		GhostCB cb = {};
		DirectX::XMStoreFloat4x4(&cb.wvp, e.world * viewProj);
		DirectX::XMStoreFloat4x4(&cb.world, e.world);
		cb.color = e.color;
		cb.rimColor = e.rimColor;
		cb.moveDirSpeed = e.moveDirSpeed;
		cb.camPos = camPos;
		cb.boneOffset = e.boneOffset;
		cb.time = time;
		ctx->UpdateSubresource(m_cb.Get(), 0, nullptr, &cb, 0, 0);

		auto& nodes = e.model->GetOriginalNodes();
		for (int idx : e.model->GetDrawMeshNodeIndices())
		{
			auto& node = nodes[idx];
			if (!node.m_spMesh) continue;
			node.m_spMesh->SetToDevice();
			auto& subsets = node.m_spMesh->GetSubsets();
			for (int i = 0; i < (int)subsets.size(); ++i)
				node.m_spMesh->DrawSubset(i);
		}
	}

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->PSSetShaderResources(0, 1, &nullSRV);
	ctx->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) ctx->RSSetViewports(1, &oldVP);
	ctx->OMSetBlendState(oldBlend, oldBF, oldMask);
	ctx->RSSetState(oldRS);
	ctx->OMSetDepthStencilState(oldDS, oldRef);
	if (oldRTV) oldRTV->Release();
	if (oldDSV) oldDSV->Release();
	if (oldBlend) oldBlend->Release();
	if (oldRS) oldRS->Release();
	if (oldDS) oldDS->Release();
}