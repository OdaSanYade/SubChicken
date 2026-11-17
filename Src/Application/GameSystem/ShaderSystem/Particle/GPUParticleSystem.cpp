#include "GPUParticleSystem.h"
#include "Assets/ParticleAsset.h"
#include "Assets/ParticleAssetApply.h"
#include "../Shaders/ShaderRuntime.h"

#include "Shader/ParticleCS.shaderInc"
#include "Shader/ParticlePS.shaderInc"
#include "Shader/ParticleVS.shaderInc"

#include "Shader/TrailParticleVS.shaderInc"



GPUParticleSystem::GPUParticleSystem()
{}

GPUParticleSystem::~GPUParticleSystem()
{}

bool GPUParticleSystem::Init()
{
	// エミッタ配列を64個ぶん、全部無効で初期化
	m_emitterTypeNames.resize(kMaxEmitters);
	m_emitters.resize(kMaxEmitters);
	m_emitterRuntimes.resize(kMaxEmitters);
	m_emitterPrevPos.assign(kMaxEmitters, DirectX::XMFLOAT3{ 0, 0, 0 });
	m_aliveTimer.assign(kMaxEmitters, 0.0f);
	m_activeList.reserve(kMaxEmitters);
	m_trailList.reserve(kMaxEmitters);
	for (auto& e : m_emitters) { e = {}; e.active = 0; }

	// どこで失敗したか分かるように、段階ごとに名前を出す
	struct InitStep { const char* name; bool (GPUParticleSystem::*fn)(); };
	static const InitStep kSteps[] =
	{
		{ "ParticleBuffer",  &GPUParticleSystem::CreateParticleBuffer },
		{ "HistoryBuffer",   &GPUParticleSystem::CreateHistoryBuffer },
		{ "EmitterBuffer",   &GPUParticleSystem::CreateEmitterBuffer },
		{ "ConstantBuffers", &GPUParticleSystem::CreateConstantBuffers },
		{ "Shaders",         &GPUParticleSystem::CreateShaders },
		{ "TrailShader",     &GPUParticleSystem::CreateTrailShader },
		{ "BlendState",      &GPUParticleSystem::CreateBlendState },
		{ "RenderStates",    &GPUParticleSystem::CreateRenderStates },
		{ "ParticleTexture", &GPUParticleSystem::CreateSampler },
		{ "ListBuffers",     &GPUParticleSystem::CreateListBuffers },
	};
	for (const auto& step : kSteps)
	{
		if (!(this->*step.fn)())
		{
			OutputDebugStringA(("[particle] Init FAILED at " + std::string(step.name) + "\n").c_str());
			return false;
		}
	}

	return true;
}

// 巨大パーティクルバッファ
bool GPUParticleSystem::CreateParticleBuffer()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	// 全部 life=0で初期化
	std::vector<GPUParticle> initData(kTotalParticles);
	for (auto& p : initData)
	{
		p.pos = { 0,0,0 }; p.life = 0.0f;
		p.vel = { 0,0,0 }; p.age = 0.0f;
		p.color = { 1,1,1,1 };
		p.rot = 0.0f; p.rotSpeed = 0.0f; p.seed = 0.0f; p._padP = 0.0f;   // ★
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(GPUParticle) * kTotalParticles;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof(GPUParticle);

	D3D11_SUBRESOURCE_DATA srd = { initData.data() };
	if (FAILED(device->CreateBuffer(&desc, &srd, &m_particleBuffer)))
		return false;

	D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_UNKNOWN;
	uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	uavDesc.Buffer.NumElements = kTotalParticles;
	if (FAILED(device->CreateUnorderedAccessView(
		m_particleBuffer.Get(), &uavDesc, &m_particleUAV)))
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = kTotalParticles;
	if (FAILED(device->CreateShaderResourceView(
		m_particleBuffer.Get(), &srvDesc, &m_particleSRV)))
		return false;

	return true;
}

// エミッタバッファ（64個、CPUが毎フレーム更新するので DYNAMIC）
bool GPUParticleSystem::CreateEmitterBuffer()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(EmitterData) * kMaxEmitters;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	desc.StructureByteStride = sizeof(EmitterData);

	// 初期データ（全部無効）
	D3D11_SUBRESOURCE_DATA srd = { m_emitters.data() };
	if (FAILED(device->CreateBuffer(&desc, &srd, &m_emitterBuffer)))
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.NumElements = kMaxEmitters;
	if (FAILED(device->CreateShaderResourceView(
		m_emitterBuffer.Get(), &srvDesc, &m_emitterSRV)))
		return false;

	return true;
}

// エミッタ番号の一覧（CS・描画が「動いているエミッタ」だけを引くため）
bool GPUParticleSystem::CreateListBuffers()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	auto make = [&](ComPtr<ID3D11Buffer>& buf, ComPtr<ID3D11ShaderResourceView>& srv)
		{
			D3D11_BUFFER_DESC desc = {};
			desc.ByteWidth = sizeof(uint32_t) * kMaxEmitters;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			desc.StructureByteStride = sizeof(uint32_t);
			if (FAILED(device->CreateBuffer(&desc, nullptr, &buf))) return false;

			D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
			sd.Format = DXGI_FORMAT_UNKNOWN;
			sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
			sd.Buffer.NumElements = kMaxEmitters;
			return SUCCEEDED(device->CreateShaderResourceView(buf.Get(), &sd, &srv));
		};

	return make(m_activeListBuffer, m_activeListSRV)
		&& make(m_trailListBuffer, m_trailListSRV)
		&& make(m_prewarmListBuffer, m_prewarmListSRV);
}

void GPUParticleSystem::UploadList(ID3D11Buffer* buffer, const std::vector<uint32_t>& list)
{
	if (!buffer || list.empty()) return;
	ID3D11DeviceContext* context = KdDirect3D::Instance().WorkDevContext();
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		const size_t n = (list.size() < (size_t)kMaxEmitters) ? list.size() : (size_t)kMaxEmitters;
		memcpy(mapped.pData, list.data(), sizeof(uint32_t) * n);
		context->Unmap(buffer, 0);
	}
}

// 定数バッファ
bool GPUParticleSystem::CreateConstantBuffers()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = sizeof(GPUParticleCB);
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(device->CreateBuffer(&desc, nullptr, &m_cbParticle)))
		return false;
	char buf[128];
	sprintf_s(buf, "[particle] GPUParticleCB size = %zu\n", sizeof(GPUParticleCB));
	OutputDebugStringA(buf);
	sprintf_s(buf, "[particle] EmitterData size = %zu\n", sizeof(EmitterData));
	OutputDebugStringA(buf);
	sprintf_s(buf, "[particle] GPUParticle size = %zu (expect 64)\n", sizeof(GPUParticle));
	OutputDebugStringA(buf);

	D3D11_BUFFER_DESC drawDesc = {};
	drawDesc.ByteWidth = sizeof(GPUDrawCB);
	drawDesc.Usage = D3D11_USAGE_DEFAULT;
	drawDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(device->CreateBuffer(&drawDesc, nullptr, &m_cbDraw)))
		return false;

	D3D11_BUFFER_DESC trailDesc = {};
	trailDesc.ByteWidth = sizeof(GPUTrailCB);
	trailDesc.Usage = D3D11_USAGE_DEFAULT;
	trailDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	if (FAILED(device->CreateBuffer(&trailDesc, nullptr, &m_cbTrail)))
		return false;

	return true;
}

bool GPUParticleSystem::CreateShaders()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	if (FAILED(device->CreateComputeShader(
		ParticleCS,sizeof(ParticleCS), nullptr, &m_updateCS)))
		return false;

	if (FAILED(device->CreateVertexShader(
		ParticleVS, sizeof(ParticleVS), nullptr, &m_drawVS)))
		return false;

	if (FAILED(device->CreatePixelShader(
		ParticlePS, sizeof(ParticlePS), nullptr, &m_drawPS)))
		return false;

	return true;
}

// 履歴バッファ（トレイル用。粒ごとに kTrailPoints 点）
bool GPUParticleSystem::CreateHistoryBuffer()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();
	const UINT count = kTotalParticles * kTrailPoints;

	std::vector<DirectX::XMFLOAT4> zero(count, { 0.0f, 0.0f, 0.0f, 0.0f });

	D3D11_BUFFER_DESC bd = {};
	bd.ByteWidth = sizeof(DirectX::XMFLOAT4) * count;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
	bd.StructureByteStride = sizeof(DirectX::XMFLOAT4);

	D3D11_SUBRESOURCE_DATA init = {};
	init.pSysMem = zero.data();
	if (FAILED(device->CreateBuffer(&bd, &init, &m_historyBuffer)))
		return false;

	D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
	ud.Format = DXGI_FORMAT_UNKNOWN;
	ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
	ud.Buffer.NumElements = count;
	if (FAILED(device->CreateUnorderedAccessView(m_historyBuffer.Get(), &ud, &m_historyUAV)))
		return false;

	D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
	sd.Format = DXGI_FORMAT_UNKNOWN;
	sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
	sd.Buffer.NumElements = count;
	return SUCCEEDED(device->CreateShaderResourceView(m_historyBuffer.Get(), &sd, &m_historySRV));
}

// 粒と履歴を全消去（エディタの Restart 用）
void GPUParticleSystem::ResetParticles()
{
	ID3D11DeviceContext* context = KdDirect3D::Instance().WorkDevContext();
	if (!context) return;

	if (m_particleBuffer)
	{
		std::vector<GPUParticle> zero(kTotalParticles);
		for (auto& p : zero)
		{
			p.pos = { 0,0,0 }; p.life = 0.0f;
			p.vel = { 0,0,0 }; p.age = 0.0f;
			p.color = { 1,1,1,1 };
			p.rot = 0.0f; p.rotSpeed = 0.0f; p.seed = 0.0f; p._padP = 0.0f;
		}
		context->UpdateSubresource(m_particleBuffer.Get(), 0, nullptr, zero.data(), 0, 0);
	}

	if (m_historyBuffer)
	{
		std::vector<DirectX::XMFLOAT4> zero(kTotalParticles * kTrailPoints, { 0,0,0,0 });
		context->UpdateSubresource(m_historyBuffer.Get(), 0, nullptr, zero.data(), 0, 0);
	}

	m_trailCursor = 0;
	m_trailTimer = 0.0f;
}

// トレイル描画用VS（本体VSとは別。履歴バッファを読む）
bool GPUParticleSystem::CreateTrailShader()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	if(FAILED(device->CreateVertexShader(
		TrailVS, sizeof(TrailVS), nullptr, &m_trailVS)))
		return false;

	return true;
}

bool GPUParticleSystem::CreateBlendState()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	D3D11_BLEND_DESC desc = {};
	desc.RenderTarget[0].BlendEnable = TRUE;
	desc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	desc.RenderTarget[0].DestBlend = D3D11_BLEND_ONE;
	desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ONE;
	desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(device->CreateBlendState(&desc, &m_additiveBlend)))
		return false;

	D3D11_BLEND_DESC alpha = {};
	alpha.RenderTarget[0].BlendEnable = TRUE;
	alpha.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	alpha.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;  
	alpha.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	alpha.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	alpha.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	alpha.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	alpha.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	if (FAILED(device->CreateBlendState(&alpha, &m_alphaBlend)))
		return false;

	return true;
}

bool GPUParticleSystem::CreateRenderStates()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	D3D11_RASTERIZER_DESC rs = {};
	rs.FillMode = D3D11_FILL_SOLID;
	rs.CullMode = D3D11_CULL_NONE;
	rs.DepthClipEnable = TRUE;
	if (FAILED(device->CreateRasterizerState(&rs, &m_rasterState)))
		return false;

	D3D11_DEPTH_STENCIL_DESC ds = {};
	ds.DepthEnable = TRUE;
	//ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL; 
	ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	ds.DepthFunc = D3D11_COMPARISON_LESS;
	ds.StencilEnable = FALSE;
	if (FAILED(device->CreateDepthStencilState(&ds, &m_depthState)))
		return false;

	return true;
}

bool GPUParticleSystem::CreateSampler()
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();

	D3D11_SAMPLER_DESC sd = {};
	sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	if (FAILED(device->CreateSamplerState(&sd, &m_sampler)))
		return false;

	return true;
}

// ゼロ初期化の EmitterData で粒が消えないようにする安全弁
static void SanitizeEmitter(EmitterData& e)
{
	if (e.sizeBegin == 0.0f && e.sizeMid == 0.0f && e.sizeEnd == 0.0f)
	{
		e.sizeBegin = 1.0f; e.sizeMid = 1.0f; e.sizeEnd = 1.0f;
	}
	if (e.curlScale == 0.0f)      e.curlScale = 0.5f;
	if (e.formAspect == 0.0f)     e.formAspect = 1.0f;
	if (e.formEdge == 0.0f)       e.formEdge = 0.15f;
	if (e.formBulge == 0.0f)      e.formBulge = 1.0f;
	if (e.formArc == 0.0f)        e.formArc = 1.0f;
	if (e.formRippleFreq == 0.0f) e.formRippleFreq = 6.0f;
	if (e.lifeMin > e.lifeMax)    e.lifeMax = e.lifeMin;
	if (e.rotSpeedMin > e.rotSpeedMax) e.rotSpeedMax = e.rotSpeedMin;

	if (e.emitCount < 0) e.emitCount = 0;
	if (e.emitBase < 0)  e.emitBase = 0;

	// カーブ（Phase8c）
	if (e.sizeCurvePointCount < 2 || e.sizeCurvePointCount > 8)   e.sizeCurvePointCount = 3;
	if (e.alphaCurvePointCount < 2 || e.alphaCurvePointCount > 8) e.alphaCurvePointCount = 4;
	if (e.sizeCurveInterp < 0 || e.sizeCurveInterp > 1)   e.sizeCurveInterp = 0;
	if (e.alphaCurveInterp < 0 || e.alphaCurveInterp > 1) e.alphaCurveInterp = 0;

	// グラデーションが未設定なら基本色から2キーで作る
	if (e.colorKeyCount < 2 || e.colorKeyCount > 4)
	{
		e.colorKeyCount = 2;
		e.colorKey0 = { e.color.x, e.color.y, e.color.z, 0.0f };
		e.colorKey1 = { e.color.x, e.color.y, e.color.z, 1.0f };
		e.colorKey2 = { 0, 0, 0, 1.0f };
		e.colorKey3 = { 0, 0, 0, 1.0f };
	}
	if (e.emitScale <= 0.0f){
		e.emitAxisX = { 1, 0, 0 };
		e.emitAxisY = { 0, 1, 0 };
		e.emitAxisZ = { 0, 0, 1 };
		e.emitScale = 1.0f;
	}
}

int GPUParticleSystem::AddEmitter(const EmitterData& e)
{
	// 空き（active==0）を探す
	for (int i = 0; i < kMaxEmitters; ++i)
	{
		if (m_emitters[i].active == 0)
		{
			m_emitters[i] = e;
			m_emitters[i].active = 1;
			SanitizeEmitter(m_emitters[i]);
			return i;
		}
	}

	// 満杯：最初の1回だけ知らせて、以降は数だけ数える
	if (m_droppedEmitters++ == 0)
	{
		char buf[128];
		sprintf_s(buf, "[particle] emitter pool full (%d). further drops are counted only\n", kMaxEmitters);
		OutputDebugStringA(buf);
	}
	return -1;
}

void GPUParticleSystem::RemoveEmitter(int id)
{
	if (id < 0 || id >= kMaxEmitters) return;
	m_emitters[id].active = 0;
}

void GPUParticleSystem::Emit(int id)
{
	if (id < 0 || id >= kMaxEmitters) return;
	m_emitters[id].emitTrigger = 1;   // 次のUpdateで発火、その後リセット
}

void GPUParticleSystem::SetEmitterPos(int id, const DirectX::XMFLOAT3& pos)
{
	if (id < 0 || id >= kMaxEmitters) return;
	m_emitters[id].pos = pos;
}

void GPUParticleSystem::SetEmitterTarget(int id, const DirectX::XMFLOAT3& target)
{
	if (id < 0 || id >= kMaxEmitters) return;
	m_emitters[id].targetPos = target;
}

const EmitterData* GPUParticleSystem::GetEmitter(int id) const
{
	if (id < 0 || id >= kMaxEmitters) return nullptr;
	return &m_emitters[id];
}

EmitterData* GPUParticleSystem::GetEmitter(int id)
{
	if (id < 0 || id >= kMaxEmitters) return nullptr;
	return &m_emitters[id];
}

// タイミング制御
void GPUParticleSystem::UpdateEmitterTiming(float dt)
{
	for (int i = 0; i < kMaxEmitters; ++i)
	{
		if (m_emitters[i].active == 0) continue;
		EmitterRuntime& rt = m_emitterRuntimes[i];

		rt.elapsed += dt;

		if (rt.isOneShot)
		{
			if (rt.elapsed > rt.lifeMax + 0.5f)
			{
				m_emitters[i].active = 0;   // 回収
				rt.isOneShot = false;
			}
			continue;
		}

		// Delay：まだ始まっていない
		if (!rt.started)
		{
			if (rt.elapsed < rt.delay)
			{
				m_emitters[i].active = 2;
				continue;
			}
			rt.started = true;
			rt.cycleTime = 0.0f;
			m_emitters[i].active = 1;
		}

		// Duration / Looping
		if (rt.duration > 0.0f)
		{
			rt.cycleTime += dt;
			if (rt.cycleTime >= rt.duration)
			{
				if (rt.looping) rt.cycleTime -= rt.duration;   // 繰り返す
				else            m_emitters[i].active = 2;      // 放出だけ止める
			}
		}
	}
}

// エミッタの移動速度
void GPUParticleSystem::UpdateEmitterVelocity(float dt)
{
	if (m_emitterPrevPos.size() != m_emitters.size())
		m_emitterPrevPos.assign(m_emitters.size(), DirectX::XMFLOAT3{ 0, 0, 0 });

	if (dt > 1e-5f)
	{
		for (int i = 0; i < kMaxEmitters; ++i)
		{
			m_emitters[i].emitterVel = {
				(m_emitters[i].pos.x - m_emitterPrevPos[i].x) / dt,
				(m_emitters[i].pos.y - m_emitterPrevPos[i].y) / dt,
				(m_emitters[i].pos.z - m_emitterPrevPos[i].z) / dt };
			m_emitterPrevPos[i] = m_emitters[i].pos;
		}
	}
	else
	{
		// ポーズ中に位置を動かされても、再開時に速度が暴れないようにする
		for (int i = 0; i < kMaxEmitters; ++i)
			m_emitterPrevPos[i] = m_emitters[i].pos;
	}
}

// 動いているエミッタの一覧を作る
void GPUParticleSystem::BuildActiveLists(float dt)
{
	m_activeList.clear();
	m_trailList.clear();

	for (int i = 0; i < kMaxEmitters; ++i)
	{
		const EmitterData& e = m_emitters[i];
		if (e.active != 0)
		{
			// 粒の寿命の最大 + 余裕。空きに戻った後もこの時間だけは動かし続ける
			const float life = (e.lifeMax > e.lifeMin) ? e.lifeMax : e.lifeMin;
			m_aliveTimer[i] = life + 0.5f;
		}
		else if (m_aliveTimer[i] > 0.0f)
		{
			m_aliveTimer[i] -= dt;
		}

		if (e.active != 0 || m_aliveTimer[i] > 0.0f)
		{
			m_activeList.push_back((uint32_t)i);
			if (e.trailOn != 0) m_trailList.push_back((uint32_t)i);
		}
	}
}

// 一覧のエミッタだけ CS を回す
void GPUParticleSystem::DispatchSimulation(ID3D11ShaderResourceView* listSRV, int listCount,
	GPUParticleCB cb, ID3D11ShaderResourceView* sceneWorldPos)
{
	if (listCount <= 0 || !listSRV) return;
	ID3D11DeviceContext* context = KdDirect3D::Instance().WorkDevContext();

	cb.totalCount = listCount * kParticlesPerEmitter;
	cb.particlesPerEmitter = kParticlesPerEmitter;
	if (!sceneWorldPos) cb.collisionOn = 0;
	context->UpdateSubresource(m_cbParticle.Get(), 0, nullptr, &cb, 0, 0);

	context->CSSetShader(m_updateCS.Get(), nullptr, 0);
	context->CSSetConstantBuffers(0, 1, m_cbParticle.GetAddressOf());
	ID3D11ShaderResourceView* srvs[3] = { m_emitterSRV.Get(), sceneWorldPos, listSRV };
	context->CSSetShaderResources(1, 3, srvs);   // t1: エミッタ / t2: GBufferC / t3: 一覧
	UINT initCounts[2] = { 0, 0 };
	ID3D11UnorderedAccessView* uavs[2] = { m_particleUAV.Get(), m_historyUAV.Get() };
	context->CSSetUnorderedAccessViews(0, 2, uavs, initCounts);

	// 1エミッタ = kParticlesPerEmitter / 256 グループ
	const UINT groups = (UINT)(listCount * (kParticlesPerEmitter / kThreadsPerGroup));
	context->Dispatch(groups, 1, 1);

	ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
	context->CSSetUnorderedAccessViews(0, 2, nullUAVs, initCounts);
	ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
	context->CSSetShaderResources(1, 3, nullSRVs);
	context->CSSetShader(nullptr, nullptr, 0);
}

void GPUParticleSystem::Update(float dt, float time, const DirectX::XMMATRIX& viewProj,
	int screenW, int screenH,
	bool collisionOn, float restitution,
	ID3D11ShaderResourceView* sceneWorldPos,
	float nearZ, float farZ)
{
	ID3D11DeviceContext* context = KdDirect3D::Instance().WorkDevContext();

	// エフェクト（レイヤーの位置・バースト・パス）を先に解決する
	UpdateEffects(dt);

	// タイミング制御
	UpdateEmitterTiming(dt);

	// エミッタの移動速度（Inherit Velocity 用）
	UpdateEmitterVelocity(dt);

	// 動かすエミッタの一覧
	BuildActiveLists(dt);

	// エミッタ配列を GPU に転送
	// 一覧の最大番号までだけ送る
	if (!m_activeList.empty())
	{
		const size_t count = (size_t)m_activeList.back() + 1;
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(m_emitterBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			memcpy(mapped.pData, m_emitters.data(), sizeof(EmitterData) * count);
			context->Unmap(m_emitterBuffer.Get(), 0);
		}
		UploadList(m_activeListBuffer.Get(), m_activeList);
		UploadList(m_trailListBuffer.Get(), m_trailList);
	}

	// 共通CB
	GPUParticleCB cb = {};
	cb.dt = dt;
	cb.time = time;
	cb.particlesPerEmitter = kParticlesPerEmitter;
	DirectX::XMStoreFloat4x4(&cb.viewProj, DirectX::XMMatrixTranspose(viewProj));
	cb.screenW = (screenW > 0) ? screenW : 1;
	cb.screenH = (screenH > 0) ? screenH : 1;
	cb.collisionOn = collisionOn ? 1 : 0;
	cb.nearZ = nearZ;
	cb.farZ = farZ;
	cb.restitution = restitution;

	// トレイル：一定間隔で履歴カーソルを進める
	m_trailTimer += dt;
	int trailWrite = 0;
	if (m_trailTimer >= kTrailInterval)
	{
		m_trailTimer -= kTrailInterval;
		m_trailCursor = (m_trailCursor + 1) % kTrailPoints;
		trailWrite = 1;
	}
	cb.trailCursor = m_trailCursor;
	cb.trailWrite = trailWrite;

	m_lastCB = cb;
	m_lastCB.collisionOn = 0;
	m_lastTime = time;
	m_lastNearZ = nearZ;
	m_lastFarZ = farZ;

	// CS 実行
	DispatchSimulation(m_activeListSRV.Get(), (int)m_activeList.size(), cb, sceneWorldPos);

	// ワンショットの発火フラグを消費
	for (auto& e : m_emitters)
		e.emitTrigger = 0;
}

void GPUParticleSystem::Render(ID3D11RenderTargetView* rtv,
	const DirectX::XMMATRIX& view,
	const DirectX::XMMATRIX& viewProj,
	int width, int height,
	ID3D11DepthStencilView* depthDSV,
	ID3D11ShaderResourceView* gbufferC,
	const DirectX::XMFLOAT3& camPos)
{
	ID3D11DeviceContext* context = KdDirect3D::Instance().WorkDevContext();

	// ---- 退避 ----
	ID3D11RenderTargetView* oldRTV = nullptr;
	ID3D11DepthStencilView* oldDSV = nullptr;
	context->OMGetRenderTargets(1, &oldRTV, &oldDSV);
	UINT oldVPCount = 1;
	D3D11_VIEWPORT oldVP = {};
	context->RSGetViewports(&oldVPCount, &oldVP);
	ID3D11BlendState* oldBlend = nullptr;
	float oldBF[4] = {}; UINT oldMask = 0;
	context->OMGetBlendState(&oldBlend, oldBF, &oldMask);
	ID3D11RasterizerState* oldRS = nullptr;
	context->RSGetState(&oldRS);
	ID3D11DepthStencilState* oldDS = nullptr;
	UINT oldStencilRef = 0;
	context->OMGetDepthStencilState(&oldDS, &oldStencilRef);

	// ---- 適用 ----
	context->RSSetState(m_rasterState.Get());
	context->OMSetDepthStencilState(m_depthState.Get(), 0);
	context->OMSetRenderTargets(1, &rtv, nullptr); 
	D3D11_VIEWPORT vp = { 0,0,(float)width,(float)height,0,1 };
	context->RSSetViewports(1, &vp);

	float blendFactor[4] = { 0, 0, 0, 0 };
	ID3D11BlendState* blend = m_additiveMode ? m_additiveBlend.Get() : m_alphaBlend.Get();
	context->OMSetBlendState(blend, blendFactor, 0xFFFFFFFF);

	context->GSSetShader(nullptr, nullptr, 0);
	context->HSSetShader(nullptr, nullptr, 0);
	context->DSSetShader(nullptr, nullptr, 0);

	// 描画CB
	GPUDrawCB cb = {};
	DirectX::XMStoreFloat4x4(&cb.viewProj, DirectX::XMMatrixTranspose(viewProj));
	DirectX::XMStoreFloat4x4(&cb.view, DirectX::XMMatrixTranspose(view));
	cb.size = 0.1f;
	cb.particlesPerEmitter = kParticlesPerEmitter;
	cb.nearZ = m_lastNearZ;
	cb.farZ = m_lastFarZ;
	cb.camPos = camPos;
	context->UpdateSubresource(m_cbDraw.Get(), 0, nullptr, &cb, 0, 0);

	context->VSSetShader(m_drawVS.Get(), nullptr, 0);
	context->PSSetShader(m_drawPS.Get(), nullptr, 0);
	context->VSSetShaderResources(0, 1, m_particleSRV.GetAddressOf());
	context->VSSetShaderResources(1, 1, m_emitterSRV.GetAddressOf());
	context->VSSetConstantBuffers(0, 1, m_cbDraw.GetAddressOf());
	context->PSSetConstantBuffers(0, 1, m_cbDraw.GetAddressOf());

	context->PSSetShaderResources(0, 1, &gbufferC);
	context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());

	context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
	context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
	context->IASetInputLayout(nullptr);
	context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 動いているエミッタの粒だけ描く
	context->VSSetShaderResources(3, 1, m_activeListSRV.GetAddressOf());
	if (!m_activeList.empty())
		context->Draw((UINT)(m_activeList.size() * kParticlesPerEmitter * 6), 0);

	// ---- トレイル----
	if (!m_trailList.empty() && m_trailVS)
	{
		GPUTrailCB tcb = {};
		tcb.trailCursor = m_trailCursor;
		context->UpdateSubresource(m_cbTrail.Get(), 0, nullptr, &tcb, 0, 0);

		ID3D11ShaderResourceView* trailSRVs[4] = {
			m_particleSRV.Get(), m_emitterSRV.Get(), m_historySRV.Get(), m_trailListSRV.Get() };
		context->VSSetShaderResources(0, 4, trailSRVs);

		ID3D11Buffer* trailCBs[2] = { m_cbDraw.Get(), m_cbTrail.Get() };
		context->VSSetConstantBuffers(0, 2, trailCBs);

		context->VSSetShader(m_trailVS.Get(), nullptr, 0);
		context->Draw((UINT)(m_trailList.size() * kParticlesPerEmitter * kTrailPoints * 6), 0);

		// 本体VSに戻す
		context->VSSetShader(m_drawVS.Get(), nullptr, 0);
	}

	// SRV 外す
	ID3D11ShaderResourceView* nullSRV = nullptr;
	ID3D11ShaderResourceView* nullVSSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
	context->VSSetShaderResources(0, 4, nullVSSRVs);
	context->PSSetShaderResources(0, 1, &nullSRV);

	// ---- 復元 ----
	context->OMSetRenderTargets(1, &oldRTV, oldDSV);
	if (oldVPCount > 0) context->RSSetViewports(1, &oldVP);
	context->OMSetBlendState(oldBlend, oldBF, oldMask);
	context->RSSetState(oldRS);
	context->OMSetDepthStencilState(oldDS, oldStencilRef);
	if (oldRTV)   oldRTV->Release();
	if (oldDSV)   oldDSV->Release();
	if (oldBlend) oldBlend->Release();
	if (oldRS)    oldRS->Release();
	if (oldDS)    oldDS->Release();
}

void GPUParticleSystem::Release()
{
	m_particleBuffer.Reset();
	m_particleUAV.Reset();
	m_particleSRV.Reset();
	m_historyBuffer.Reset();
	m_historyUAV.Reset();
	m_historySRV.Reset();
	m_trailVS.Reset();
	m_cbTrail.Reset();
	m_emitterBuffer.Reset();
	m_emitterSRV.Reset();
	m_updateCS.Reset();
	m_drawVS.Reset();
	m_drawPS.Reset();
	m_cbParticle.Reset();
	m_cbDraw.Reset();
	m_additiveBlend.Reset();
	m_rasterState.Reset();
	m_depthState.Reset();
	m_sampler.Reset();
	m_alphaBlend.Reset();
	m_activeListBuffer.Reset();
	m_activeListSRV.Reset();
	m_trailListBuffer.Reset();
	m_trailListSRV.Reset();
	m_prewarmListBuffer.Reset();
	m_prewarmListSRV.Reset();
}

static EmitterData MakeEmitterFromAsset(const ParticleAsset& a)
{
	EmitterData e = {};
	ApplyAssetToEmitter(a, e);
	e.active = 1;
	e.mode = 0;
	e.emitTrigger = 0;
	e.emitBase = 0;
	e.emitCount = 0;
	e.typeId = 0;
	e.textureIndex = 0;
	e.emitterVel = { 0.0f, 0.0f, 0.0f };
	e.emitAxisX = { 1, 0, 0 };
	e.emitAxisY = { 0, 1, 0 };
	e.emitAxisZ = { 0, 0, 1 };
	e.emitScale = 1.0f;
	return e;
}

// 起動時に seconds 秒ぶん空回しして、最初から定常状態にする（動いている全エミッタ）
void GPUParticleSystem::Prewarm(float seconds, float step)
{
	std::vector<int> ids;
	for (int i = 0; i < kMaxEmitters; ++i)
		if (m_emitters[i].active != 0) ids.push_back(i);
	PrewarmEmitters(ids, seconds, step);
}

// 指定エミッタだけ CS を空回しする。エフェクトの時間（バースト・パス）は進めない
void GPUParticleSystem::PrewarmEmitters(const std::vector<int>& ids, float seconds, float step)
{
	m_lastPrewarmSteps = 0;
	if (seconds <= 0.0f || ids.empty() || !m_updateCS) return;

	// 1フレームで止まらないよう刻み数に上限を設け、超える分は刻みを粗くする
	constexpr int kMaxSteps = 120;
	float dt = (step > 0.0f) ? step : (1.0f / 30.0f);
	int steps = (int)(seconds / dt);
	if (steps > kMaxSteps)
	{
		dt = seconds / (float)kMaxSteps;
		steps = kMaxSteps;
	}
	if (steps <= 0) return;

	std::vector<uint32_t> list;
	list.reserve(ids.size());
	for (int id : ids)
		if (id >= 0 && id < kMaxEmitters && m_emitters[id].active != 0) list.push_back((uint32_t)id);
	if (list.empty()) return;
	std::sort(list.begin(), list.end());

	ID3D11DeviceContext* context = KdDirect3D::Instance().WorkDevContext();

	// 最新のエミッタ配列を送る（作ったばかりのエミッタを含める）
	{
		const size_t count = (size_t)list.back() + 1;
		D3D11_MAPPED_SUBRESOURCE mapped;
		if (SUCCEEDED(context->Map(m_emitterBuffer.Get(), 0,
			D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
		{
			// 一覧に無いエミッタも含めて、使っている範囲は全部送る
			size_t sendCount = count;
			if (!m_activeList.empty() && (size_t)m_activeList.back() + 1 > sendCount)
				sendCount = (size_t)m_activeList.back() + 1;
			memcpy(mapped.pData, m_emitters.data(), sizeof(EmitterData) * sendCount);
			context->Unmap(m_emitterBuffer.Get(), 0);
		}
	}
	UploadList(m_prewarmListBuffer.Get(), list);

	// 時刻は「今から seconds 秒前」から進める
	GPUParticleCB cb = m_lastCB;
	cb.dt = dt;
	cb.collisionOn = 0;
	cb.particlesPerEmitter = kParticlesPerEmitter;
	const float t0 = m_lastTime - seconds;
	for (int i = 0; i < steps; ++i)
	{
		cb.time = t0 + (float)i * dt;
		cb.trailWrite = (i == steps - 1) ? 1 : 0;   // 最後の刻みだけ履歴に書く
		cb.trailCursor = m_trailCursor;
		DispatchSimulation(m_prewarmListSRV.Get(), (int)list.size(), cb, nullptr);
	}
	m_lastPrewarmSteps = steps;
}

void GPUParticleSystem::ApplyAssetFromString(const std::string& jsonStr)
{
	ParticleEffect fx;
	if (!fx.LoadFromString(jsonStr) || fx.name.empty())
	{
		OutputDebugStringA("[particle] parse FAILED\n");
		return;
	}

	// 名前だけで届いたときは末尾が "/名前" のものを探して、そのキーで差し替える
	if (m_effectTypes.find(fx.name) == m_effectTypes.end())
	{
		const std::string tail = "/" + fx.name;
		for (const auto& kv : m_effectTypes)
		{
			const std::string& k = kv.first;
			if (k.size() > tail.size() &&
				k.compare(k.size() - tail.size(), tail.size(), tail) == 0)
			{
				fx.name = k;
				break;
			}
		}
	}

	OutputDebugStringA(("[particle] recv effect=[" + fx.name + "] layers=" +
		std::to_string(fx.layers.size()) + "\n").c_str());

	m_effectTypes[fx.name] = fx;

	//レイヤーを持っていない場合
	if (!fx.layers.empty())
	{
		ParticleAsset a = fx.layers[0].asset;
		a.name = fx.name;
		m_types[fx.name] = a;
	}

	// 同じ種類で動いているエフェクトを作り直す
	for (size_t i = 0; i < m_effectInstances.size(); ++i)
	{
		if (!m_effectInstances[i].active) continue;
		if (m_effectInstances[i].typeName != fx.name) continue;

		// 作り直しても、ゲーム側が持っているハンドルはそのまま使えるようにする
		const EffectInstance old = m_effectInstances[i];

		for (auto& lr : m_effectInstances[i].layers)
			if (lr.emitterId >= 0) m_emitters[lr.emitterId].active = 0;
		m_effectInstances[i].active = false;

		int h = SpawnEffect(fx.name, old.origin, old.angle);
		if (h < 0) continue;
		const int ni = h & 0xFFFF;
		if (ni != (int)i)
		{
			std::swap(m_effectInstances[ni], m_effectInstances[i]);
			// 入れ替わった空き側は、新しく振った世代を持たせて古いハンドルを無効のままにする
			m_effectInstances[ni].generation = (uint16_t)(((uint32_t)h >> 16) & 0xFFFFu);
		}

		EffectInstance& inst = m_effectInstances[i];
		inst.generation = old.generation;
		inst.oneShot = old.oneShot;
		inst.axisX = old.axisX; inst.axisY = old.axisY; inst.axisZ = old.axisZ;
		inst.scale = old.scale;
		inst.overridePath = old.overridePath;
		inst.pathEnd = old.pathEnd;
		inst.pathTime = old.pathTime;
		inst.overrideRadius = old.overrideRadius;
		inst.radius = old.radius;
	}

	// 出しているエミッタも更新
	if (!fx.layers.empty())
	{
		for (int i = 0; i < kMaxEmitters; ++i)
		{
			if (m_emitters[i].active == 0) continue;
			if (m_emitterTypeNames[i] != fx.name) continue;

			EmitterData fresh = MakeEmitterFromAsset(fx.layers[0].asset);
			fresh.pos = m_emitters[i].pos;
			fresh.active = m_emitters[i].active;
			fresh.mode = m_emitters[i].mode;
			fresh.emitTrigger = m_emitters[i].emitTrigger;
			fresh.emitBase = m_emitters[i].emitBase;
			m_emitters[i] = fresh;
		}
	}
}

//------------------------------------------------------------------------------
// エフェクト（複数レイヤー）
//------------------------------------------------------------------------------
static std::string MakeEffectName(const std::filesystem::path& file,
	const std::filesystem::path& baseFolder)
{
	// exp.particle.json → exp
	std::string name = file.stem().string();          // "exp.particle"
	size_t dot = name.find(".particle");
	if (dot != std::string::npos) name = name.substr(0, dot);

	// サブフォルダにあるなら相対パスを前置する
	std::error_code ec;
	auto rel = std::filesystem::relative(file.parent_path(), baseFolder, ec);
	if (!ec && !rel.empty() && rel != ".")
	{
		std::string prefix = rel.generic_string();    // 区切りを / に統一
		if (!prefix.empty() && prefix != ".") name = prefix + "/" + name;
	}
	return name;
}

bool GPUParticleSystem::LoadEffect(const std::string& name, const std::string& path)
{
	ParticleEffect fx;
	if (!fx.LoadFromFile(path)) return false;

	// name が空ならファイル名から決める
	std::string key = name;
	if (key.empty())
		key = MakeEffectName(std::filesystem::path(path),
			std::filesystem::path(path).parent_path());

	fx.name = key;
	m_effectTypes[key] = fx;

	// 旧APIとの互換
	if (!fx.layers.empty())
	{
		ParticleAsset a = fx.layers[0].asset;
		a.name = key;
		m_types[key] = a;
	}
	return true;
}
int GPUParticleSystem::LoadEffectFolder(const std::string& folder, bool recursive)
{
	namespace fs = std::filesystem;

	std::error_code ec;
	fs::path base(folder);
	if (!fs::exists(base, ec) || !fs::is_directory(base, ec))
	{
		OutputDebugStringA(("[particle] LoadEffectFolder: not a directory: "
			+ folder + "\n").c_str());
		return 0;
	}

	int loaded = 0;
	int failed = 0;

	auto tryLoad = [&](const fs::path& p)
		{
			std::string s = p.string();
			if (s.size() < 14) return;
			if (s.substr(s.size() - 14) != ".particle.json") return;

			std::string name = MakeEffectName(p, base);

			if (LoadEffect(name, s))
			{
				++loaded;
				OutputDebugStringA(("[particle]   + " + name + "\n").c_str());
			}
			else
			{
				++failed;
				OutputDebugStringA(("[particle]   ! failed: " + s + "\n").c_str());
			}
		};

	if (recursive)
	{
		for (const auto& ent : fs::recursive_directory_iterator(base, ec))
		{
			if (ec) break;
			if (ent.is_regular_file(ec)) tryLoad(ent.path());
		}
	}
	else
	{
		for (const auto& ent : fs::directory_iterator(base, ec))
		{
			if (ec) break;
			if (ent.is_regular_file(ec)) tryLoad(ent.path());
		}
	}

	char buf[256];
	sprintf_s(buf, "[particle] LoadEffectFolder(%s): loaded=%d failed=%d\n",
		folder.c_str(), loaded, failed);
	OutputDebugStringA(buf);
	return loaded;
}

std::vector<std::string> GPUParticleSystem::GetEffectNames() const
{
	std::vector<std::string> out;
	out.reserve(m_effectTypes.size());
	for (const auto& kv : m_effectTypes) out.push_back(kv.first);
	return out;
}

int GPUParticleSystem::SpawnEffect(const std::string& name, const DirectX::XMFLOAT3& pos,const DirectX::XMFLOAT3& angle)
{
	// エフェクトの情報を獲得
	auto it = m_effectTypes.find(name);
	if (it == m_effectTypes.end()) return -1;
	const ParticleEffect& fx = it->second;

	// 空きスロットを探す
	int index = -1;
	for (size_t i = 0; i < m_effectInstances.size(); ++i)
		if (!m_effectInstances[i].active) { index = (int)i; break; }

	// 空きスロットが見つからない場合
	// 最後尾にエフェクトインスタンスを追加する
	if (index < 0)
	{
		if ((int)m_effectInstances.size() >= kMaxEffectInstances) return -1;
		m_effectInstances.push_back(EffectInstance{});
		index = (int)m_effectInstances.size() - 1;
	}
	EffectInstance& inst = m_effectInstances[index];

	// 再利用のたびに世代を進めそのエフェクトの枠の情報を埋める
	const uint16_t nextGen = (uint16_t)((inst.generation % 0x7FFF) + 1);
	inst = EffectInstance{};
	inst.generation = nextGen;
	inst.active = true;
	inst.typeName = name;
	inst.origin = pos;
	inst.maxLife = fx.TotalDuration();
	inst.angle = angle;
	inst.layers.resize(fx.layers.size());
	float prewarmMax = 0.0f;
	std::vector<int> prewarmIds;

	// レイヤーから受け取ったエフェクトの情報をいれる
	for (size_t li = 0; li < fx.layers.size(); ++li)
	{
		const ParticleLayer& L = fx.layers[li];
		if (!L.enabled) continue;

		EmitterData e = MakeEmitterFromAsset(L.asset);
		e.pos = { pos.x + L.offset.x, pos.y + L.offset.y, pos.z + L.offset.z };

		// バーストを持つレイヤー
		e.mode = L.bursts.empty() ? 0 : 2;

		int id = AddEmitter(e);
		if (id < 0) continue;

		inst.layers[li].emitterId = id;
		inst.layers[li].nextBurst = 0;
		m_emitterTypeNames[id] = name;

		// タイミング情報
		EmitterRuntime& rt = m_emitterRuntimes[id];
		rt.isOneShot = false;
		rt.elapsed = 0.0f;
		rt.lifeMax = L.asset.lifeMax;
		rt.delay = L.asset.delay;
		rt.duration = L.asset.duration;
		rt.looping = (L.asset.looping != 0);
		rt.started = false;
		rt.cycleTime = 0.0f;

		if (L.asset.prewarm > 0.0f) prewarmIds.push_back(id);
		if (L.asset.prewarm > prewarmMax) prewarmMax = L.asset.prewarm;
	}

	// Prewarm は全レイヤーを作り終えてから、このエフェクトのエミッタだけ1回回す
	if (prewarmMax > 0.0f) PrewarmEmitters(prewarmIds, prewarmMax);

	return MakeHandle(index, inst.generation);
}

GPUParticleSystem::EffectInstance* GPUParticleSystem::ResolveHandle(int handle)
{
	if (handle < 0) return nullptr;
	const int index = handle & 0xFFFF;
	const uint16_t gen = (uint16_t)(((uint32_t)handle >> 16) & 0xFFFFu);
	if (index >= (int)m_effectInstances.size()) return nullptr;
	EffectInstance& inst = m_effectInstances[index];
	if (!inst.active || inst.generation != gen) return nullptr;
	return &inst;
}

int GPUParticleSystem::StopAllEffects()
{
	int stopped = 0;
	for (auto& inst : m_effectInstances)
	{
		if (!inst.active) continue;
		for (auto& lr : inst.layers)
			if (lr.emitterId >= 0) m_emitters[lr.emitterId].active = 2;
		inst.oneShot = true;
		inst.elapsed = 0.0f;
		inst.maxLife = 3.0f;
		++stopped;
	}
	if (stopped > 0)
	{
		char buf[128];
		sprintf_s(buf, "[particle] StopAllEffects: %d effects stopped\n", stopped);
		OutputDebugStringA(buf);
	}
	return stopped;
}

GPUParticleSystem::Stats GPUParticleSystem::GetStats() const
{
	Stats st;
	st.activeEmitters = (int)m_activeList.size();
	st.trailEmitters = (int)m_trailList.size();
	st.maxEmitters = kMaxEmitters;
	st.droppedEmitters = m_droppedEmitters;
	st.prewarmSteps = m_lastPrewarmSteps;
	for (const auto& inst : m_effectInstances)
		if (inst.active) ++st.liveEffects;
	return st;
}

void GPUParticleSystem::PlayEffect(const std::string& name, const DirectX::XMFLOAT3& pos,const DirectX::XMFLOAT3& angle)
{
	int h = SpawnEffect(name, pos, angle);
	if (EffectInstance* inst = ResolveHandle(h)) inst->oneShot = true;
}

void GPUParticleSystem::SetEffectPos(int handle, const DirectX::XMFLOAT3& pos)
{
	if (EffectInstance* inst = ResolveHandle(handle)) inst->origin = pos;
}

void GPUParticleSystem::StopEffect(int handle)
{
	EffectInstance* pInst = ResolveHandle(handle);
	if (!pInst) return;
	EffectInstance& inst = *pInst;

	// 放出だけ止めて、残りの粒は寿命まで生かす
	for (auto& lr : inst.layers)
		if (lr.emitterId >= 0) m_emitters[lr.emitterId].active = 2;

	inst.oneShot = true;
	inst.elapsed = 0.0f;
	inst.maxLife = 3.0f;
}

// 毎フレームの更新
void GPUParticleSystem::UpdateEffects(float dt)
{
	for (auto& inst : m_effectInstances)
	{
		if (!inst.active) continue;

		auto it = m_effectTypes.find(inst.typeName);
		if (it == m_effectTypes.end()) { inst.active = false; continue; }
		const ParticleEffect& fx = it->second;

		inst.elapsed += dt;

		for (size_t li = 0; li < inst.layers.size() && li < fx.layers.size(); ++li)
		{
			int id = inst.layers[li].emitterId;
			if (id < 0) continue;

			const ParticleLayer& L = fx.layers[li];
			EmitterData& e = m_emitters[id];

			// 位置
			e.emitAxisX = inst.axisX;
			e.emitAxisY = inst.axisY;
			e.emitAxisZ = inst.axisZ;
			e.emitScale = inst.scale;
			if (inst.overrideRadius) e.shapeRadius = inst.radius;

			DirectX::XMFLOAT3 pk;
			if (inst.overridePath)
			{
				float t = (inst.pathTime > 0.0f)
					? (inst.elapsed / inst.pathTime) : 1.0f;
				if (t > 1.0f) t = 1.0f;
				pk = { inst.pathEnd.x * t, inst.pathEnd.y * t, inst.pathEnd.z * t };
			}
			else
			{
				pk = ParticleEffect::EvalPath(L.path, inst.elapsed);
			}
			DirectX::XMFLOAT3 lo = { L.offset.x + pk.x, L.offset.y + pk.y, L.offset.z + pk.z };

			// オフセットも基底で回す
			DirectX::XMFLOAT3 ro = {
				inst.axisX.x * lo.x + inst.axisY.x * lo.y + inst.axisZ.x * lo.z,
				inst.axisX.y * lo.x + inst.axisY.y * lo.y + inst.axisZ.y * lo.z,
				inst.axisX.z * lo.x + inst.axisY.z * lo.y + inst.axisZ.z * lo.z };

			e.pos = { inst.origin.x + ro.x * inst.scale,
					  inst.origin.y + ro.y * inst.scale,
					  inst.origin.z + ro.z * inst.scale };

			// バースト：経過時間に応じて発火する
			e.emitTrigger = 0;
			if (!L.bursts.empty())
			{
				size_t& next = inst.layers[li].nextBurst;
				int fire = 0;
				while (next < L.bursts.size() && L.bursts[next].time <= inst.elapsed)
				{
					fire += L.bursts[next].count;
					++next;
				}
				if (fire > 0)
				{
					if (fire > kParticlesPerEmitter) fire = kParticlesPerEmitter;
					e.emitCount = fire;
					e.emitTrigger = 1;
					e.emitBase = (e.emitBase + fire) % kParticlesPerEmitter;
				}
			}
		}

		// ワンショットの回収
		if (inst.oneShot && inst.elapsed > inst.maxLife + 0.5f)
		{
			for (auto& lr : inst.layers)
				if (lr.emitterId >= 0) m_emitters[lr.emitterId].active = 0;
			inst.active = false;
		}
	}
}