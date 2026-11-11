#include "KdHudShader.h"
#include "../../../Application/GameSystem/Combat/CombatFx.h"

namespace kdhud_vs
{
#include "KdHudShader_VS.shaderInc"
}

namespace kdhud_ps
{
#include "KdHudShader_PS.shaderInc"
}

Math::Vector2 KdHudShader::GetViewportSize()
{
	UINT num = 1;
	D3D11_VIEWPORT vp = {};
	KdDirect3D::Instance().WorkDevContext()->RSGetViewports(&num, &vp);
	return Math::Vector2(vp.Width, vp.Height);
}

bool KdHudShader::Init(int _capacity)
{
	Release();

	//-------------------------------------
	// 頂点シェーダ
	//-------------------------------------
	{
		if (FAILED(KdDirect3D::Instance().WorkDev()->CreateVertexShader(
			kdhud_vs::compiledBuffer, sizeof(kdhud_vs::compiledBuffer), nullptr, &m_VS)))
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
			kdhud_ps::compiledBuffer, sizeof(kdhud_ps::compiledBuffer), nullptr, &m_PS)))
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

void KdHudShader::Release()
{
	KdSafeRelease(m_VS);
	KdSafeRelease(m_PS);
	KdSafeRelease(m_instanceSRV);
	KdSafeRelease(m_instanceBuf);
	m_cb0.Release();
	m_capacity = 0;
	m_time = 0.0f;
}

bool KdHudShader::CreateInstanceBuffer(int _capacity)
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

void KdHudShader::Draw(const std::vector<Instance>& _instances)
{
	if (_instances.empty()) return;
	if (!m_VS || !m_PS) return;

	// 実時間で進める。ヒットストップ中もUIは動かす。
	// 止めると画面が固まったように見えてしまう。
	m_time += CombatFx::Instance().RealDelta();

	// 上限を超えた分は捨てる。際限なく確保しない。
	const int count = std::min(static_cast<int>(_instances.size()), kMaxCapacity);

	if (count > m_capacity)
	{
		int newCap = (m_capacity > 0) ? m_capacity : 256;
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
		m_cb0.Work().screenSize = GetViewportSize();
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

	// 4頂点 × マーカー数。全マーカーがこの1回で出る。
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