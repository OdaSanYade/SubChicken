#include "PhaseShader.h"
#include "PhaseVS.shaderInc"
#include "PhasePS.shaderInc"

bool PhaseShader::Init()
{
	ID3D11Device* dev = KdDirect3D::Instance().WorkDev();

	//定数バッファ初期化
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = sizeof(cbPhase);
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(dev->CreateBuffer(&bd, nullptr, m_cb.GetAddressOf())))
			return false;
	}

	//サンプラー設定
	{
		D3D11_SAMPLER_DESC sd = {};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
		sd.MinLOD = 0;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(dev->CreateSamplerState(&sd,m_sample.GetAddressOf())))
			return false;
	}

	//シェーダ初期化
	{
		//頂点シェーダ作成
		if (FAILED(dev->CreateVertexShader(
			PhaseVS, sizeof(PhaseVS), nullptr, &m_vs))) 
		{
			return false;
		}

		//ピクセルシェーダ作成
		if (FAILED(dev->CreatePixelShader(
			PhasePS, sizeof(PhasePS), nullptr, &m_ps)))
		{
			return false;
		}
	}

	return true;
}

void PhaseShader::Draw()
{
	if (!m_isPhaseChange)return;
	if (!m_srv)return;

	ID3D11DeviceContext* ctx = KdDirect3D::Instance().WorkDevContext();

	//ビューポート
	UINT num = 1;
	D3D11_VIEWPORT vp = {};
	ctx->RSGetViewports(&num, &vp);

	//進捗度更新
	float dt = Time::Instance().GetUnScaledDeltaTime();
	m_progress = std::max(0.0f,m_progress - dt / m_progressSec);

	//定数バッファ設定
	D3D11_MAPPED_SUBRESOURCE mapped = {};
	if (FAILED(ctx->Map(m_cb.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
	cbPhase cb = {};
	const float w = vp.Width * 0.30f;
	cb.time = Time::Instance().GetTime();
	cb.progress = m_progress;
	cb.phase = static_cast<float>(m_phaseIndex);
	cb.screenSize = {vp.Width,vp.Height};
	cb.halfSize = { w, w / kBannerAspect };
	cb.center = { vp.Width / 2, vp.Height / 2 };
	cb.uvRange = { 0.0f, 1.0f };
	std::memcpy(mapped.pData,&cb,sizeof(cbPhase));
	ctx->Unmap(m_cb.Get(), 0);

	KdShaderManager::Instance().ChangeBlendState(KdBlendState::Alpha);
	KdShaderManager::Instance().ChangeDepthStencilState(KdDepthStencilState::ZDisable);
	KdShaderManager::Instance().ChangeRasterizerState(KdRasterizerState::CullNone);

	//シェーダ設定
	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ctx->PSSetShader(m_ps.Get(), nullptr, 0);
	ctx->GSSetShader(nullptr, nullptr, 0);

	//バッファ設定
	ctx->VSSetConstantBuffers(0, 1, m_cb.GetAddressOf());
	ctx->PSSetConstantBuffers(0, 1, m_cb.GetAddressOf());

	//テクスチャ設定
	ctx->PSSetShaderResources(0, 1, m_srv.GetAddressOf());
	ctx->PSSetSamplers(0,1,m_sample.GetAddressOf());

	//その他もろもろ
	ID3D11Buffer* nullVB = nullptr;
	UINT stride = 0, offset = 0;
	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	//描画
	ctx->Draw(4, 0);

	//後始末
	ctx->VSSetShader(nullptr, nullptr, 0);
	ctx->PSSetShader(nullptr, nullptr, 0);

	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->PSSetShaderResources(0, 1, &nullSRV);

	KdShaderManager::Instance().UndoRasterizerState();
	KdShaderManager::Instance().UndoDepthStencilState();
	KdShaderManager::Instance().UndoBlendState();

	//通知完了
	if (m_progress <= 0.0f)
	{
		m_isPhaseChange = false;
		m_progress = 1.0f;
	}
}

void PhaseShader::SetPhaseNotice(int phaseIndex, ID3D11ShaderResourceView * _srv, float _time)
{
	m_srv = _srv;
	m_progressSec = _time;
	m_progress = 1.0f;
	m_phaseIndex = phaseIndex;
	m_isPhaseChange = true;
}
