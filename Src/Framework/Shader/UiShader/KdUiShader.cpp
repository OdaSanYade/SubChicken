#include "KdUiShader.h"
#include "../HudShader/KdHudShader.h"
#include "../../../Application/GameSystem/Combat/CombatFx.h"

// ===========================================================
// コンパイル済みシェーダ
// ===========================================================
namespace kdui_vs
{
#include "../HudShader/KdHudShader_VS.shaderInc"
}

namespace kdui_ps
{
#include "KdUiShader_PS.shaderInc"
}

bool KdUiShader::Init(int _capacity)
{
	Release();

	//-------------------------------------
	// 頂点シェーダ(戦闘HUDと共用)
	//-------------------------------------
	{
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(
			kdui_vs::compiledBuffer, sizeof(kdui_vs::compiledBuffer), nullptr, &m_VS)))
		{
			assert(0 && "頂点シェーダー作成失敗");
			Release();
			return false;
		}
	}

	//-------------------------------------
	// ピクセルシェーダ
	//-------------------------------------
	{
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreatePixelShader(
			kdui_ps::compiledBuffer, sizeof(kdui_ps::compiledBuffer), nullptr, &m_PS)))
		{
			assert(0 && "ピクセルシェーダー作成失敗");
			Release();
			return false;
		}
	}

	m_cb0.Create();
	m_time = 0.0f;

	if (!CreateInstanceBuffer(_capacity))
	{
		Release();
		return false;
	}

	return true;
}

void KdUiShader::Release()
{
	KdSafeRelease(m_VS);
	KdSafeRelease(m_PS);
	KdSafeRelease(m_instanceSRV);
	KdSafeRelease(m_instanceBuf);
	m_cb0.Release();
	m_capacity = 0;
	m_time = 0.0f;
}

bool KdUiShader::CreateInstanceBuffer(int _capacity)
{
	KdSafeRelease(m_instanceSRV);
	KdSafeRelease(m_instanceBuf);
	m_capacity = 0;

	if (_capacity <= 0) return false;

	// 毎フレーム全書き換えなので DYNAMIC + MAP_DISCARD
	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = static_cast<UINT>(sizeof(Instance) * _capacity);
	bd.Usage = D3D11_USAGE_DYNAMIC;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(Instance);

	if (FAILED(KdDirect3D::Instance().WorkDev()->CreateBuffer(&bd, nullptr, &m_instanceBuf)))
	{
		assert(0 && "インスタンスバッファ作成失敗");
		return false;
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_UNKNOWN;		// 構造化バッファはUNKNOWN
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sd.Buffer.FirstElement = 0;
	sd.Buffer.NumElements = static_cast<UINT>(_capacity);

	if (FAILED(KdDirect3D::Instance().WorkDev()->CreateShaderResourceView(
		m_instanceBuf, &sd, &m_instanceSRV)))
	{
		assert(0 && "インスタンスSRV作成失敗");
		KdSafeRelease(m_instanceBuf);
		return false;
	}

	m_capacity = _capacity;
	return true;
}

void KdUiShader::Draw(const std::vector<Instance>& _instances)
{
	if (_instances.empty()) return;
	if (!m_VS || !m_PS) return;

	// 実時間で進める。メニューは戦闘のスローを持ち込まない。
	m_time += CombatFx::Instance().RealDelta();

	const int count = std::min(static_cast<int>(_instances.size()), kMaxCapacity);

	// 足りなければ倍々で広げる。毎フレーム作り直さないため。
	if (count > m_capacity)
	{
		int newCap = (m_capacity > 0) ? m_capacity : 32;
		while (newCap < count) newCap *= 2;
		newCap = std::min(newCap, kMaxCapacity);
		if (!CreateInstanceBuffer(newCap)) return;
	}

	auto* ctx = KdDirect3D::Instance().WorkDevContext();

	//---------------------------------------
	// インスタンス情報を転送
	//---------------------------------------
	{
		D3D11_MAPPED_SUBRESOURCE mapped = {};
		if (FAILED(ctx->Map(m_instanceBuf, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;

		memcpy(mapped.pData, _instances.data(), sizeof(Instance) * count);
		ctx->Unmap(m_instanceBuf, 0);
	}

	//---------------------------------------
	// 定数バッファ
	//---------------------------------------
	{
		m_cb0.Work().screenSize = KdHudShader::GetViewportSize();
		m_cb0.Work().time = m_time;
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

	ctx->VSSetShaderResources(0, 1, &m_instanceSRV);

	// 頂点バッファもレイアウトも使わない
	ID3D11Buffer* nullVB = nullptr;
	UINT stride = 0, offset = 0;
	ctx->IASetInputLayout(nullptr);
	ctx->IASetVertexBuffers(0, 1, &nullVB, &stride, &offset);
	ctx->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	// 4頂点 × 要素数。全要素がこの1回で出る。
	ctx->DrawInstanced(4, static_cast<UINT>(count), 0, 0);

	//---------------------------------------
	// 後始末
	//---------------------------------------
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ctx->VSSetShaderResources(0, 1, &nullSRV);

	KdShaderManager::Instance().UndoRasterizerState();
	KdShaderManager::Instance().UndoDepthStencilState();
	KdShaderManager::Instance().UndoBlendState();
}