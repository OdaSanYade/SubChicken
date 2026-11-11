#include "Framework/KdFramework.h"

#include "KdTitleShader.h"

// ===========================================================
// コンパイル済みシェーダ
//
// .shaderInc は const BYTE compiledBuffer[] = {...}; を吐くだけのファイル。
// 関数の中で #include すると、その配列がまるごとローカル変数になり
// スタックへ載って溢れる。名前空間スコープで静的記憶域に置くこと。
// ===========================================================
namespace kdtitle_vs
{
#include "KdTitleShader_VS.shaderInc"
}

namespace kdtitle_ps
{
#include "KdTitleShader_PS.shaderInc"
}

bool KdTitleShader::Init()
{
	Release();

	//-------------------------------------
	// サンプラー
	//-------------------------------------
	{
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD = 0;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateSamplerState(&sd, m_sample.GetAddressOf())))
		{
			assert(0 && "サンプラー作成失敗");
			Release();
			return false;
		}
	}

	//-------------------------------------
	// 頂点シェーダ
	//-------------------------------------
	{
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(
			kdtitle_vs::compiledBuffer, sizeof(kdtitle_vs::compiledBuffer), nullptr, &m_VS)))
		{
			assert(0 && "頂点シェーダー作成失敗");
			Release();
			return false;
		}
		// 入力レイアウトは不要。SV_VertexIDから頂点を組み立てるため。
	}

	//-------------------------------------
	// ピクセルシェーダ
	//-------------------------------------
	{
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			kdtitle_ps::compiledBuffer, sizeof(kdtitle_ps::compiledBuffer), nullptr, &m_PS)))
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();
			return false;
		}
	}

	m_cb0.Create();
	m_time = 0.0f;

	return true;
}

void KdTitleShader::Release()
{
	KdSafeRelease(m_VS);
	KdSafeRelease(m_PS);
	m_cb0.Release();
	m_time = 0.0f;
}

void KdTitleShader::Draw(const KdTexture& _tex, const Params& _p)
{
	if (!m_VS || !m_PS) return;
	if (!_tex.GetSRView()) return;

	m_time += Time::Instance().GetUnScaledDeltaTime();

	auto* ctx = KdDirect3D::Instance().WorkDevContext();

	//---------------------------------------
	// 定数バッファ
	//---------------------------------------
	{
		UINT num = 1;
		D3D11_VIEWPORT vp = {};
		ctx->RSGetViewports(&num, &vp);

		auto& w = m_cb0.Work();

		w.center = _p.center;
		w.halfSize = _p.halfSize;
		w.screenSize = { vp.Width, vp.Height };
		w.time = m_time;
		w.appear = _p.appear;
		w.tint = _p.tint;
		w.heat = _p.heat;
		w.sheen = _p.sheen;
		w.edgeWave = _p.edgeWave;
		w.dust = _p.dust;
		w.breath = _p.breath;
		w.shake = _p.shake;
		w.dissolve = _p.dissolve;

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
	ctx->PSSetShader(m_PS, nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);

	ctx->VSSetConstantBuffers(0, 1, m_cb0.GetAddress());
	ctx->PSSetConstantBuffers(0, 1, m_cb0.GetAddress());

	ID3D11ShaderResourceView* srv = _tex.WorkSRView();
	ctx->PSSetShaderResources(0, 1, &srv);
	ctx->PSSetSamplers(0, 1, m_sample.GetAddressOf());

	ID3D11Buffer* nullVB = nullptr;
	UINT stride = 0, offset = 0;
	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	ctx->Draw(4, 0);

	//---------------------------------------
	// 後始末
	//---------------------------------------
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->PSSetShaderResources(0, 1, &nullSRV);

	KdShaderManager::Instance().UndoRasterizerState();
	KdShaderManager::Instance().UndoDepthStencilState();
	KdShaderManager::Instance().UndoBlendState();
}