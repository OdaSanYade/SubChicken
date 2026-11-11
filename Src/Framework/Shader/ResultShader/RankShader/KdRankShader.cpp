#include "Framework/KdFramework.h"

#include "KdRankShader.h"

// ===========================================================
// コンパイル済みシェーダ
//
// .shaderInc は const BYTE compiledBuffer[] = {...}; を吐くだけのファイル。
// 関数の中で #include すると、その配列がまるごとローカル変数になり
// スタックへ載って溢れる。名前空間スコープで静的記憶域に置くこと。
// ===========================================================
namespace kdrank_vs
{
#include "KdRankShader_VS.shaderInc"
}

namespace kdrank_ps
{
#include "KdRankShader_PS.shaderInc"
}

bool KdRankShader::Init()
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
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateSamplerState(&sd, &m_sample)))
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
			kdrank_vs::compiledBuffer, sizeof(kdrank_vs::compiledBuffer), nullptr, &m_VS)))
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
			kdrank_ps::compiledBuffer, sizeof(kdrank_ps::compiledBuffer), nullptr, &m_PS)))
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

void KdRankShader::Release()
{
	KdSafeRelease(m_VS);
	KdSafeRelease(m_PS);
	KdSafeRelease(m_sample);
	m_cb0.Release();
	m_time = 0.0f;
}

void KdRankShader::Draw(const KdTexture& _tex, const Params& _p)
{
	if (!m_VS || !m_PS) return;
	if (!_tex.GetSRView()) return;

	// 実時間で進める。リザルトは戦闘のスローを持ち込まない。
	m_time += Time::Instance().GetDeltaTime();

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
		w.hatch = _p.hatch;
		w.tint = _p.tint;
		w.rank = static_cast<float>(std::clamp(_p.rank, 0, 4));

		m_cb0.Write();
	}

	//---------------------------------------
	// ステート
	//---------------------------------------
	// ★ KdBlendState の列挙名は環境に合わせること(Alpha/AlphaBlend等)
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
	ctx->PSSetSamplers(0, 1, &m_sample);

	// 頂点バッファもレイアウトも使わない
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