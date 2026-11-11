#include "TransitionShader.h"

#include "Shader/Transition_PS.shaderInc"
#include "Shader/Transition_VS.shaderInc"

bool TransitionShader::Init()
{
	ID3D11Device* dev = KdDirect3D::Instance().WorkDev();
	if (!dev) return false;

	Release();

	// --- シェーダ ---
	if (FAILED(dev->CreateVertexShader(Transition_VS, sizeof(Transition_VS),
		nullptr, m_vs.GetAddressOf())))
		return false;

	if (FAILED(dev->CreatePixelShader(Transition_PS, sizeof(Transition_PS),
		nullptr, m_ps.GetAddressOf())))
		return false;

	// --- 定数バッファ ---
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(cbTransition);
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	if (FAILED(dev->CreateBuffer(&bd, nullptr, m_cbBuffer.GetAddressOf())))
		return false;

	// --- サンプラー ---
	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
	sd.MinLOD = 0;
	sd.MaxLOD = D3D11_FLOAT32_MAX;
	if (FAILED(dev->CreateSamplerState(&sd, m_samp.GetAddressOf())))
		return false;

	// --- アルファブレンド ---
	D3D11_BLEND_DESC bld = {};
	bld.RenderTarget[0].BlendEnable = TRUE;
	bld.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	bld.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	bld.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	bld.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	bld.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	bld.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	bld.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(dev->CreateBlendState(&bld, m_blend.GetAddressOf())))
		return false;

	// --- 深度は使わない ---
	D3D11_DEPTH_STENCIL_DESC dsd = {};
	dsd.DepthEnable = FALSE;
	dsd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dsd.DepthFunc = D3D11_COMPARISON_ALWAYS;
	dsd.StencilEnable = FALSE;
	if (FAILED(dev->CreateDepthStencilState(&dsd, m_depth.GetAddressOf())))
		return false;

	// --- ラスタライザ ---
	D3D11_RASTERIZER_DESC rd = {};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.DepthClipEnable = TRUE;
	if (FAILED(dev->CreateRasterizerState(&rd, m_raster.GetAddressOf())))
		return false;

	m_initialized = true;
	return true;
}

void TransitionShader::Release()
{
	m_vs.Reset();
	m_ps.Reset();
	m_cbBuffer.Reset();
	m_samp.Reset();
	m_blend.Reset();
	m_depth.Reset();
	m_raster.Reset();
	m_copySRV.Reset();
	m_copyTex.Reset();
	m_initialized = false;
}

bool TransitionShader::UpdateCopyTexture(ID3D11Device* dev, ID3D11Texture2D* src)
{
	D3D11_TEXTURE2D_DESC srcDesc = {};
	src->GetDesc(&srcDesc);

	// 既存のコピー先が同じサイズ・フォーマットならそのまま使う
	if (m_copyTex)
	{
		D3D11_TEXTURE2D_DESC cur = {};
		m_copyTex->GetDesc(&cur);

		const bool sameSize = (cur.Width == srcDesc.Width) && (cur.Height == srcDesc.Height);
		const bool sameFormat = (cur.Format == srcDesc.Format);
		if (sameSize && sameFormat) return true;
	}

	m_copySRV.Reset();
	m_copyTex.Reset();

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = srcDesc.Width;
	desc.Height = srcDesc.Height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = srcDesc.Format;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	if (FAILED(dev->CreateTexture2D(&desc, nullptr, m_copyTex.GetAddressOf())))
		return false;

	if (FAILED(dev->CreateShaderResourceView(m_copyTex.Get(), nullptr, m_copySRV.GetAddressOf())))
	{
		m_copyTex.Reset();
		return false;
	}

	return true;
}

void TransitionShader::Draw(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* tex, float progress, float switchPoint)
{
	if (!m_initialized || !ctx) return;
	if (!tex)return;

	auto& direct = KdDirect3D::Instance();
	ID3D11Device* dev = direct.WorkDev();
	if (!dev) return;

	ID3D11RenderTargetView* backRTV = *direct.GetBackBuffer()->WorkRTViewAddress();
	if (!backRTV) return;

	// --- バックバッファのテクスチャ本体を取得 ---
	ComPtr<ID3D11Resource> backRes;
	backRTV->GetResource(backRes.GetAddressOf());

	ComPtr<ID3D11Texture2D> backTex;
	if (FAILED(backRes.As(&backTex))) return;

	if (!UpdateCopyTexture(dev, backTex.Get())) return;

	// --- 今の画面をコピー ---
	D3D11_TEXTURE2D_DESC backDesc = {};
	backTex->GetDesc(&backDesc);

	if (backDesc.SampleDesc.Count > 1)
	{
		ctx->ResolveSubresource(m_copyTex.Get(), 0, backTex.Get(), 0, backDesc.Format);
	}
	else
	{
		ctx->CopyResource(m_copyTex.Get(), backTex.Get());
	}

	// --- 定数バッファ更新 ---
	cbTransition cb;
	cb.time = Time::Instance().GetTime();
	cb.progress = std::clamp(progress, 0.0f, 1.0f);
	cb.switchPoint = std::clamp(switchPoint, 0.0f, 1.0f);
	cb.padding = 0.0f;

	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(ctx->Map(m_cbBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
	std::memcpy(mapped.pData, &cb, sizeof(cbTransition));
	ctx->Unmap(m_cbBuffer.Get(), 0);

	// --- 直前のステートを退避 ---
	ComPtr<ID3D11RenderTargetView> prevRTV;
	ComPtr<ID3D11DepthStencilView> prevDSV;
	ctx->OMGetRenderTargets(1, prevRTV.GetAddressOf(), prevDSV.GetAddressOf());

	ComPtr<ID3D11BlendState> prevBlend;
	float prevBlendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	UINT  prevSampleMask = 0xFFFFFFFF;
	ctx->OMGetBlendState(prevBlend.GetAddressOf(), prevBlendFactor, &prevSampleMask);

	ComPtr<ID3D11DepthStencilState> prevDepth;
	UINT prevStencilRef = 0;
	ctx->OMGetDepthStencilState(prevDepth.GetAddressOf(), &prevStencilRef);

	ComPtr<ID3D11RasterizerState> prevRaster;
	ctx->RSGetState(prevRaster.GetAddressOf());

	UINT prevViewportCount = 1;
	D3D11_VIEWPORT prevViewport = {};
	ctx->RSGetViewports(&prevViewportCount, &prevViewport);

	// --- 描画先：バックバッファ（深度なし）---
	ctx->OMSetRenderTargets(1, &backRTV, nullptr);

	D3D11_VIEWPORT vp = {};
	vp.Width = static_cast<float>(backDesc.Width);
	vp.Height = static_cast<float>(backDesc.Height);
	vp.MaxDepth = 1.0f;
	ctx->RSSetViewports(1, &vp);

	// --- トランジション用の設定 ---
	const float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	ctx->OMSetBlendState(m_blend.Get(), blendFactor, 0xFFFFFFFF);
	ctx->OMSetDepthStencilState(m_depth.Get(), 0);
	ctx->RSSetState(m_raster.Get());

	ctx->IASetInputLayout(nullptr);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->PSSetShader(m_ps.Get(), nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);
	ctx->HSSetShader(nullptr, nullptr, 0);
	ctx->DSSetShader(nullptr, nullptr, 0);

	ctx->PSSetConstantBuffers(0, 1, m_cbBuffer.GetAddressOf());
	ctx->PSSetShaderResources(0, 1, m_copySRV.GetAddressOf());
	ctx->PSSetShaderResources(1, 1, &tex);
	ctx->PSSetSamplers(0, 1, m_samp.GetAddressOf());

	ctx->Draw(3, 0);

	// --- ステートを元に戻す ---
	ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
	ctx->PSSetShaderResources(0, 1, nullSRVs);

	ctx->OMSetBlendState(prevBlend.Get(), prevBlendFactor, prevSampleMask);
	ctx->OMSetDepthStencilState(prevDepth.Get(), prevStencilRef);
	ctx->RSSetState(prevRaster.Get());

	if (prevViewportCount > 0)
	{
		ctx->RSSetViewports(1, &prevViewport);
	}

	ID3D11RenderTargetView* restoreRTV = prevRTV.Get();
	ctx->OMSetRenderTargets(1, &restoreRTV, prevDSV.Get());
}