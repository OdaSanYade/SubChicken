#include "KdCreditShader.h"

namespace kdcredit_vs
{
#include "KdCreditShader_VS.shaderInc"
}

namespace kdcredit_bg_ps
{
#include "KdCreditBg_PS.shaderInc"
}

namespace kdcredit_text_vs
{
#include "KdCreditText_VS.shaderInc"
}

namespace kdcredit_text_ps
{
#include "KdCreditText_PS.shaderInc"
}

bool KdCreditShader::Init()
{
	Release();

	auto* dev = KdDirect3D::Instance().WorkDev();

	//-------------------------------------
	// 頂点シェーダ
	//-------------------------------------
	// 入力レイアウトは不要。どちらも SV_VertexID から組む。
	if (FAILED(dev->CreateVertexShader(
		kdcredit_vs::compiledBuffer, sizeof(kdcredit_vs::compiledBuffer), nullptr, &m_VS)))
	{
		assert(0 && "頂点シェーダー作成失敗(背景)");
		Release();
		return false;
	}

	if (FAILED(dev->CreateVertexShader(
		kdcredit_text_vs::compiledBuffer, sizeof(kdcredit_text_vs::compiledBuffer), nullptr, &m_textVS)))
	{
		assert(0 && "頂点シェーダー作成失敗(本文)");
		Release();
		return false;
	}

	//-------------------------------------
	// ピクセルシェーダ
	//-------------------------------------
	if (FAILED(dev->CreatePixelShader(
		kdcredit_bg_ps::compiledBuffer, sizeof(kdcredit_bg_ps::compiledBuffer), nullptr, &m_bgPS)))
	{
		assert(0 && "ピクセルシェーダー作成失敗(背景)");
		Release();
		return false;
	}

	if (FAILED(dev->CreatePixelShader(
		kdcredit_text_ps::compiledBuffer, sizeof(kdcredit_text_ps::compiledBuffer), nullptr, &m_textPS)))
	{
		assert(0 && "ピクセルシェーダー作成失敗(本文)");
		Release();
		return false;
	}

	//-------------------------------------
	// サンプラー
	//-------------------------------------
	// 1:1 で貼るので線形でも点でも同じ見た目。影を半端な位置で引くために線形。
	{
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD = 0;
		sd.MaxLOD = D3D11_FLOAT32_MAX;

		if (FAILED(dev->CreateSamplerState(&sd, m_sample.GetAddressOf())))
		{
			assert(0 && "サンプラー作成失敗");
			Release();
			return false;
		}
	}

	m_cb0.Create();
	m_time = 0.0f;

	return true;
}

void KdCreditShader::ReleaseLines()
{
	m_lineSRV.Reset();
	m_lineBuf.Reset();
	m_lineCount = 0;
}

void KdCreditShader::Release()
{
	KdSafeRelease(m_VS);
	KdSafeRelease(m_bgPS);
	KdSafeRelease(m_textVS);
	KdSafeRelease(m_textPS);
	m_sample.Reset();
	ReleaseLines();
	m_cb0.Release();
	m_time = 0.0f;
}

//===================================================
// 背景
//===================================================
void KdCreditShader::DrawBackground(const BgParams& _p)
{
	if (!m_VS || !m_bgPS) return;
	if (_p.appear <= 0.0f) return;

	m_time += static_cast<float>(Time::Instance().GetUnScaledDeltaTime());

	auto* ctx = KdDirect3D::Instance().WorkDevContext();

	//---------------------------------------
	// 定数バッファ
	//---------------------------------------
	{
		UINT num = 1;
		D3D11_VIEWPORT vp = {};
		ctx->RSGetViewports(&num, &vp);
		if (vp.Width <= 0.0f || vp.Height <= 0.0f) return;

		auto& w = m_cb0.Work();

		w.screenSize = { vp.Width, vp.Height };
		w.time = m_time;
		w.appear = std::clamp(_p.appear, 0.0f, 1.0f);
		w.scroll = _p.scroll;

		// 他のUIと同じ基準解像度の考え方
		w.uiScale = std::min(vp.Width / 1920.0f, vp.Height / 1080.0f);

		m_cb0.Write();
	}

	//---------------------------------------
	// ステート
	//---------------------------------------
	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Alpha);
	KdShaderManager::Instance().ChangeDepthStencilState(KdDepthStencilState::ZDisable);
	KdShaderManager::Instance().ChangeRasterizerState(KdRasterizerState::CullNone);

	//---------------------------------------
	// シェーダ
	//---------------------------------------
	ctx->VSSetShader(m_VS, nullptr, 0);
	ctx->PSSetShader(m_bgPS, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);

	ctx->VSSetConstantBuffers(0, 1, m_cb0.GetAddress());
	ctx->PSSetConstantBuffers(0, 1, m_cb0.GetAddress());

	ID3D11Buffer* nullVB = nullptr;
	UINT stride = 0, offset = 0;
	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 全画面を覆う三角形1枚
	ctx->Draw(3, 0);

	//---------------------------------------
	// 後始末
	//---------------------------------------
	KdShaderManager::Instance().UndoRasterizerState();
	KdShaderManager::Instance().UndoDepthStencilState();
	KdShaderManager::Instance().UndoBlendState();
}

//===================================================
// 本文
//===================================================
bool KdCreditShader::SetTextLines(const std::vector<TextInst>& _lines)
{
	ReleaseLines();
	if (_lines.empty()) return true;

	auto* dev = KdDirect3D::Instance().WorkDev();

	// 組版をやり直すまで変わらないので IMMUTABLE。毎フレームの転送は無い。
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = static_cast<UINT>(sizeof(TextInst) * _lines.size());
	bd.Usage = D3D11_USAGE_IMMUTABLE;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(TextInst);

	D3D11_SUBRESOURCE_DATA init = {};
	init.pSysMem = _lines.data();

	if (FAILED(dev->CreateBuffer(&bd, &init, m_lineBuf.GetAddressOf())))
	{
		assert(0 && "本文バッファ作成失敗");
		ReleaseLines();
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_UNKNOWN;		// 構造化バッファはUNKNOWN
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sd.Buffer.FirstElement = 0;
	sd.Buffer.NumElements = static_cast<UINT>(_lines.size());

	if (FAILED(dev->CreateShaderResourceView(m_lineBuf.Get(), &sd, m_lineSRV.GetAddressOf())))
	{
		assert(0 && "本文SRV作成失敗");
		ReleaseLines();
		return false;
	}

	m_lineCount = static_cast<UINT>(_lines.size());
	return true;
}

void KdCreditShader::DrawLines(const TextParams& _p)
{
	if (!m_textVS || !m_textPS) return;
	if (!_p.atlas || !m_lineSRV) return;
	if (_p.atlasW <= 0 || _p.atlasH <= 0) return;
	if (_p.count == 0 || _p.first >= m_lineCount) return;
	if (_p.appear <= 0.0f) return;

	const UINT count = std::min(_p.count, m_lineCount - _p.first);

	auto* ctx = KdDirect3D::Instance().WorkDevContext();

	//---------------------------------------
	// 定数バッファ
	//---------------------------------------
	{
		UINT num = 1;
		D3D11_VIEWPORT vp = {};
		ctx->RSGetViewports(&num, &vp);
		if (vp.Width <= 0.0f || vp.Height <= 0.0f) return;

		auto& w = m_cb0.Work();

		w.screenSize = { vp.Width, vp.Height };
		w.time = m_time;		// 背景と同じフレームの時間
		w.appear = std::clamp(_p.appear, 0.0f, 1.0f);
		w.scroll = _p.scroll;
		w.uiScale = _p.uiScale;
		w.origin = _p.origin;
		w.shadowUV = { _p.shadowPx / static_cast<float>(_p.atlasW),
					   _p.shadowPx / static_cast<float>(_p.atlasH) };
		w.instBase = _p.first;
		w.fade = _p.fade;

		m_cb0.Write();
	}

	//---------------------------------------
	// ステート
	//---------------------------------------
	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Alpha);
	KdShaderManager::Instance().ChangeDepthStencilState(KdDepthStencilState::ZDisable);
	KdShaderManager::Instance().ChangeRasterizerState(KdRasterizerState::CullNone);

	//---------------------------------------
	// シェーダ
	//---------------------------------------
	ctx->VSSetShader(m_textVS, nullptr, 0);
	ctx->PSSetShader(m_textPS, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);

	ctx->VSSetConstantBuffers(0, 1, m_cb0.GetAddress());
	ctx->PSSetConstantBuffers(0, 1, m_cb0.GetAddress());

	ID3D11ShaderResourceView* lineSRV = m_lineSRV.Get();
	ctx->VSSetShaderResources(1, 1, &lineSRV);

	ID3D11ShaderResourceView* atlas = _p.atlas;
	ctx->PSSetShaderResources(0, 1, &atlas);
	ctx->PSSetSamplers(0, 1, m_sample.GetAddressOf());

	ID3D11Buffer* nullVB = nullptr;
	UINT stride = 0, offset = 0;
	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// 画面に入る行だけを1回で描く
	ctx->DrawInstanced(4, count, 0, 0);

	//---------------------------------------
	// 後始末
	//---------------------------------------
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->VSSetShaderResources(1, 1, &nullSRV);
	ctx->PSSetShaderResources(0, 1, &nullSRV);

	KdShaderManager::Instance().UndoRasterizerState();
	KdShaderManager::Instance().UndoDepthStencilState();
	KdShaderManager::Instance().UndoBlendState();
}
