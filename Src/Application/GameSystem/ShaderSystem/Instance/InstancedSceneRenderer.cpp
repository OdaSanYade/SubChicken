#include "InstancedSceneRenderer.h"
#include "Lod/LodBuilder.h"

#include "Shader/ClearCS.shaderInc"
#include "Shader/CountCS.shaderInc"
#include "Shader/OffsetCS.shaderInc"
#include "Shader/ScatterCS.shaderInc"

#include "Shader/InstanceVS.shaderInc"
#include "Shader/ShadowVS.shaderInc"

namespace {
	struct CBCull
	{
		DirectX::XMFLOAT4 planes[6];
		int instanceCount;
		int typeCount;
		int drawTypeCount;
		int _pad;
		DirectX::XMFLOAT3 camPos;
		float maxDist;
	};
	struct CBDraw
	{
		unsigned int meshID;
		unsigned int _pad[3];
	};
	// CS が読む drawType 情報
	struct DrawTypeGPU
	{
		unsigned int meshID;
		unsigned int indexCount;
		unsigned int startIndex;
		unsigned int _pad;
	};
}

bool InstancedSceneRenderer::CreateShaders()
{
	ID3D11Device* dev = KdDirect3D::Instance().WorkDev();

	auto compileCS = [&](const BYTE* byteCode, size_t size,
		Microsoft::WRL::ComPtr<ID3D11ComputeShader>& out) -> bool
		{
			HRESULT hr = dev->CreateComputeShader(byteCode, size, nullptr, &out);
			if (FAILED(hr))
			{
				return false;
			}

			return true;
		};

	if (!compileCS(ClearCS, sizeof(ClearCS), m_clearCS))   return false;
	if (!compileCS(CountCS, sizeof(CountCS), m_countCS))   return false;
	if (!compileCS(OffsetCS, sizeof(OffsetCS), m_offsetCS))  return false;
	if (!compileCS(ScatterCS, sizeof(ScatterCS), m_scatterCS)) return false;

	// ---- 通常描画VS ----
	{
		HRESULT hr = dev->CreateVertexShader(InstanceVS, sizeof(InstanceVS), nullptr, &m_vs);
		if (FAILED(hr))
		{
			return false;
		}

		D3D11_INPUT_ELEMENT_DESC layout[] = {
			{ "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "NORMAL",     0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TANGENT",    0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 36, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SKININDEX",  0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SKINWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		hr = dev->CreateInputLayout(layout, 6, InstanceVS, sizeof(InstanceVS), &m_layout);
		if (FAILED(hr))
		{
			return false;
		}
	}

	// ---- 影用VS ----
	{
		HRESULT hr = dev->CreateVertexShader(ShadowVS, sizeof(ShadowVS), nullptr, &m_shadowVS);
		if (FAILED(hr))
		{
			return false;
		}

		D3D11_INPUT_ELEMENT_DESC shLayout[] = {
			{ "POSITION",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD",   0, DXGI_FORMAT_R32G32_FLOAT,       0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SKININDEX",  0, DXGI_FORMAT_R16G16B16A16_UINT,  0, 48, D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "SKINWEIGHT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 56, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};
		hr = dev->CreateInputLayout(shLayout, 4, ShadowVS, sizeof(ShadowVS), &m_shadowLayout);
		if (FAILED(hr))
		{
			return false;
		}
	}

	return true;
}


bool InstancedSceneRenderer::CreateBuffers(int maxInstances)
{
	ID3D11Device* device = KdDirect3D::Instance().WorkDev();
	m_maxInstances = maxInstances;

	// 構造化バッファを作るヘルパ
	auto makeStructured = [&](UINT stride, UINT count, bool cpuWrite, bool uav,
		Microsoft::WRL::ComPtr<ID3D11Buffer>& buf,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>* srv,
		Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView>* uavOut) -> bool
		{
			D3D11_BUFFER_DESC d = {};
			d.ByteWidth = stride * count;
			d.Usage = cpuWrite ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT;
			d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
			d.CPUAccessFlags = cpuWrite ? D3D11_CPU_ACCESS_WRITE : 0;
			d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
			d.StructureByteStride = stride;
			if (FAILED(device->CreateBuffer(&d, nullptr, &buf))) return false;

			if (srv)
			{
				D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
				sv.Format = DXGI_FORMAT_UNKNOWN;
				sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
				sv.Buffer.NumElements = count;
				if (FAILED(device->CreateShaderResourceView(buf.Get(), &sv, srv->GetAddressOf())))
					return false;
			}
			if (uavOut)
			{
				D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
				uv.Format = DXGI_FORMAT_UNKNOWN;
				uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				uv.Buffer.NumElements = count;
				if (FAILED(device->CreateUnorderedAccessView(buf.Get(), &uv, uavOut->GetAddressOf())))
					return false;
			}
			return true;
		};

	// 全インスタンス（CPUが書く / CSが読む）
	if (!makeStructured(sizeof(InstanceData), maxInstances, true, false,
		m_allBuffer, &m_allSRV, nullptr)) return false;
	// 可視インスタンス（CSが書く / VSが読む）
	if (!makeStructured(sizeof(InstanceData), maxInstances, false, true,
		m_visibleBuffer, &m_visibleSRV, &m_visibleUAV)) return false;
	// カウント / オフセット / カーソル
	if (!makeStructured(sizeof(unsigned int), m_maxTypes, false, true,
		m_countBuffer, &m_countSRV, &m_countUAV)) return false;
	if (!makeStructured(sizeof(unsigned int), m_maxTypes, false, true,
		m_offsetBuffer, &m_offsetSRV, &m_offsetUAV)) return false;
	if (!makeStructured(sizeof(unsigned int), m_maxTypes, false, true,
		m_cursorBuffer, nullptr, &m_cursorUAV)) return false;
	// drawType情報（CPUが1回書く / CSが読む）
	if (!makeStructured(sizeof(DrawTypeGPU), m_maxDrawTypes, true, false,
		m_drawTypeBuffer, &m_drawTypeSRV, nullptr)) return false;
	if (!makeStructured(sizeof(InstanceParams), kMaxParamSets, true, false,
		m_paramBuffer, &m_paramSRV, nullptr)) return false;
	// Indirect引数（RAW UAV + DRAWINDIRECT_ARGS）
	{
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = 20 * m_maxDrawTypes;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		d.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS
			| D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_argsBuffer))) return false;   

		D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
		uv.Format = DXGI_FORMAT_R32_TYPELESS;
		uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uv.Buffer.NumElements = 5 * m_maxDrawTypes;
		uv.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(device->CreateUnorderedAccessView(m_argsBuffer.Get(), &uv, &m_argsUAV)))
			return false;   // ★m_argsUAV
	}

	// ---- 影用バッファ一式 ----
	if (!makeStructured(sizeof(InstanceData), maxInstances, false, true,
		m_shVisibleBuffer, &m_shVisibleSRV, &m_shVisibleUAV)) return false;
	if (!makeStructured(sizeof(unsigned int), m_maxTypes, false, true,
		m_shCountBuffer, &m_shCountSRV, &m_shCountUAV)) return false;
	if (!makeStructured(sizeof(unsigned int), m_maxTypes, false, true,
		m_shOffsetBuffer, &m_shOffsetSRV, &m_shOffsetUAV)) return false;
	if (!makeStructured(sizeof(unsigned int), m_maxTypes, false, true,
		m_shCursorBuffer, nullptr, &m_shCursorUAV)) return false;
	{
		D3D11_BUFFER_DESC d = {};
		d.ByteWidth = 20 * m_maxDrawTypes;
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		d.MiscFlags = D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS
			| D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_shArgsBuffer))) return false;

		D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {};
		uv.Format = DXGI_FORMAT_R32_TYPELESS;
		uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		uv.Buffer.NumElements = 5 * m_maxDrawTypes;
		uv.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
		if (FAILED(device->CreateUnorderedAccessView(m_shArgsBuffer.Get(), &uv, &m_shArgsUAV)))
			return false;
	}

	// 定数バッファ
	{
		D3D11_BUFFER_DESC d = {};
		d.Usage = D3D11_USAGE_DEFAULT;
		d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		d.ByteWidth = sizeof(CBCull);
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_cbCull))) return false;
		d.ByteWidth = sizeof(CBDraw);
		if (FAILED(device->CreateBuffer(&d, nullptr, &m_cbDraw))) return false;
	}

	// ボーン行列バッファ（全スキンインスタンス分を連結）
	if (!makeStructured(sizeof(DirectX::XMFLOAT4X4),
		kBoneCapacity, true, false,
		m_boneBuffer, &m_boneSRV, nullptr)) return false;

	return true;
}

bool InstancedSceneRenderer::Init(int maxInstances, int maxTypes, int maxDrawTypes)
{
	m_maxTypes = (maxTypes > 0) ? maxTypes : kDefaultMaxTypes;
	m_maxDrawTypes = (maxDrawTypes > 0) ? maxDrawTypes : kDefaultMaxDrawTypes;
	m_types.reserve(m_maxTypes);
	m_drawTypes.reserve(m_maxDrawTypes);

	if (!CreateShaders()) return false;
	if (!CreateBuffers(maxInstances)) return false;
	m_ready = true;
	return true;
}

void InstancedSceneRenderer::Submit(int meshID, const DirectX::XMMATRIX& world,
	const InstanceParams* params, int textureIndex)
{
	if (meshID >= 0 && meshID < (int)m_types.size())
	{
		int lodID = m_types[meshID].lodMeshID;
		if (lodID >= 0)
		{
			DirectX::XMFLOAT3 wp;
			DirectX::XMStoreFloat3(&wp, world.r[3]);
			float dx = wp.x - m_frameCamPos.x;
			float dy = wp.y - m_frameCamPos.y;
			float dz = wp.z - m_frameCamPos.z;
			float dist = sqrtf(dx * dx + dy * dy + dz * dz);
			if (dist > m_lodDistance)
				meshID = lodID;   // 遠い → LODに差し替え
		}
	}
	if (!AddInstance(meshID, world)) return;
	m_instances.back().textureIndex = (unsigned int)textureIndex;

	// 0番は BeginFrame で入れた既定値
	m_instances.back().paramOffset = 0;
	if (params)
	{
		if ((int)m_instanceParams.size() < kMaxParamSets)
		{
			m_instances.back().paramOffset = (unsigned int)m_instanceParams.size();
			m_instanceParams.push_back(*params);
		}
		else
		{
			++m_droppedParams;
			WarnOnce(1u << 1, "[Instanced] param sets full (kMaxParamSets). default params used");
		}
	}
}

void InstancedSceneRenderer::WarnOnce(unsigned int bit, const char* msg)
{
	if (m_warned & bit) return;
	m_warned |= bit;
	OutputDebugStringA(msg);
	OutputDebugStringA("\n");
}

InstancedSceneRenderer::Usage InstancedSceneRenderer::GetUsage() const
{
	Usage u;
	u.instances = (int)m_instances.size();      u.maxInstances = m_maxInstances;
	u.types = (int)m_types.size();              u.maxTypes = m_maxTypes;
	u.drawTypes = (int)m_drawTypes.size();      u.maxDrawTypes = m_maxDrawTypes;
	u.params = (int)m_instanceParams.size();    u.maxParams = kMaxParamSets;
	u.bones = (int)m_boneMatrices.size();       u.maxBones = kBoneCapacity;
	u.droppedInstances = m_droppedInstances;
	u.droppedParams = m_droppedParams;
	u.droppedBones = m_droppedBones;
	u.droppedTypes = m_droppedTypes;
	return u;
}

void InstancedSceneRenderer::EndFrame()
{
	Build(); 
}

int InstancedSceneRenderer::RegisterModel(const std::shared_ptr<KdModelData>& spModel)
{
	if (!spModel) return -1;
	if ((int)m_types.size() >= m_maxTypes)
	{
		++m_droppedTypes;
		WarnOnce(1u << 3, "[Instanced] mesh types full. model not registered (raise maxTypes)");
		return -1;
	}

	int meshID = (int)m_types.size();
	MeshType mt;
	mt.model = spModel;

	// 境界球（最初に見つかったメッシュのものを採用）
	bool found = false;
	for (auto& node : spModel->GetOriginalNodes())
	{
		if (!node.m_spMesh) continue;
		if (!found)
		{
			auto& bs = node.m_spMesh->GetBoundingSphere();
			mt.localCenter = bs.Center;
			mt.localRadius = bs.Radius;
			found = true;
		}
		// subset ごとに drawType を作る
		auto& subsets = node.m_spMesh->GetSubsets();
		for (int i = 0; i < (int)subsets.size(); ++i)
		{
			if (subsets[i].FaceCount == 0) continue;
			if ((int)m_drawTypes.size() >= m_maxDrawTypes)
			{
				WarnOnce(1u << 4, "[Instanced] draw types full. some subsets are not drawn (raise maxDrawTypes)");
				break;
			}
			DrawType dt;
			dt.meshID = meshID;
			dt.mesh = node.m_spMesh.get();
			dt.subsetNo = i;
			dt.materialNo = subsets[i].MaterialNo;
			dt.indexCount = subsets[i].FaceCount * 3;
			dt.startIndex = subsets[i].FaceStart * 3;
			m_drawTypes.push_back(dt);
		}
	}

	if (!found) return -1;

	m_types.push_back(mt);
	return meshID;
}

bool InstancedSceneRenderer::AddInstance(int meshID, const DirectX::XMMATRIX& world)
{
	if (meshID < 0 || meshID >= (int)m_types.size()) return false;

	// BeginFrame より前に呼ばれても、0番は必ず既定値にしておく
	if (m_instanceParams.empty())
	{
		InstanceParams def = {};
		def.p[3].w = 1.0f;
		m_instanceParams.push_back(def);
	}

	if ((int)m_instances.size() >= m_maxInstances)
	{
		++m_droppedInstances;
		WarnOnce(1u << 0, "[Instanced] instance buffer full. instances dropped");
		return false;
	}

	const MeshType& mt = m_types[meshID];
	InstanceData d = {};
	DirectX::XMStoreFloat4x4(&d.world, world);

	// 境界球を world 変換
	DirectX::XMVECTOR c = DirectX::XMVector3Transform(
		DirectX::XMLoadFloat3(&mt.localCenter), world);
	float sx = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[0]));
	float sy = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[1]));
	float sz = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[2]));
	float maxScale = (sx > sy) ? ((sx > sz) ? sx : sz) : ((sy > sz) ? sy : sz);
	DirectX::XMFLOAT3 cf; DirectX::XMStoreFloat3(&cf, c);
	d.boundingSphere = { cf.x, cf.y, cf.z, mt.localRadius * maxScale };
	d.meshID = (unsigned int)meshID;
	d.boneOffset = 0;    
	d.isSkinned = 0;   
	d.textureIndex = 0;
	d.paramOffset = 0;

	m_instances.push_back(d);
	return true;
}

void InstancedSceneRenderer::Build()
{
	m_instanceCount = (int)m_instances.size();
	if (m_instanceCount <= 0) return;

	ID3D11DeviceContext* ctx = KdDirect3D::Instance().WorkDevContext();
	D3D11_MAPPED_SUBRESOURCE mapped;

	// 全インスタンスを転送
	if (SUCCEEDED(ctx->Map(m_allBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, m_instances.data(), sizeof(InstanceData) * m_instanceCount);
		ctx->Unmap(m_allBuffer.Get(), 0);
	}
	// drawType情報を転送
	std::vector<DrawTypeGPU> dts(m_drawTypes.size());
	for (size_t i = 0; i < m_drawTypes.size(); ++i)
	{
		dts[i].meshID = (unsigned int)m_drawTypes[i].meshID;
		dts[i].indexCount = m_drawTypes[i].indexCount;
		dts[i].startIndex = m_drawTypes[i].startIndex;
		dts[i]._pad = 0;
	}
	if (!dts.empty() &&
		SUCCEEDED(ctx->Map(m_drawTypeBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, dts.data(), sizeof(DrawTypeGPU) * dts.size());
		ctx->Unmap(m_drawTypeBuffer.Get(), 0);
	}

	// ボーン行列を転送
	if (!m_boneMatrices.empty() &&
		SUCCEEDED(ctx->Map(m_boneBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, m_boneMatrices.data(),
			sizeof(DirectX::XMFLOAT4X4) * m_boneMatrices.size());
		ctx->Unmap(m_boneBuffer.Get(), 0);
	}

	if (!m_instanceParams.empty() &&
		SUCCEEDED(ctx->Map(m_paramBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
	{
		memcpy(mapped.pData, m_instanceParams.data(),
			sizeof(InstanceParams) * m_instanceParams.size());
		ctx->Unmap(m_paramBuffer.Get(), 0);
	}
}

int InstancedSceneRenderer::SubmitSkinned(int meshID, const DirectX::XMMATRIX& world,
	const DirectX::XMFLOAT4X4* bones, int boneCount,
	const InstanceParams* params, int sharedBoneOffset)
{
	if (meshID < 0 || meshID >= (int)m_types.size()) return -1;
	if ((int)m_instances.size() >= m_maxInstances)
	{
		++m_droppedInstances;
		WarnOnce(1u << 0, "[Instanced] instance buffer full. instances dropped");
		return -1;
	}

	unsigned int offset = 0;
	if (sharedBoneOffset >= 0 && sharedBoneOffset < (int)m_boneMatrices.size())
	{
		// 同じモデルのマテリアル差分など：送ったボーンを使い回す
		offset = (unsigned int)sharedBoneOffset;
	}
	else
	{
		// 実際に使う本数だけ送る
		int n = (boneCount < kBonesPerInstance) ? boneCount : kBonesPerInstance;
		if (n <= 0 || !bones) return -1;
		if ((int)m_boneMatrices.size() + n > kBoneCapacity)
		{
			++m_droppedBones;
			WarnOnce(1u << 2, "[Instanced] bone buffer full. skinned instances dropped");
			return -1;
		}
		offset = (unsigned int)m_boneMatrices.size();
		m_boneMatrices.insert(m_boneMatrices.end(), bones, bones + n);
	}

	// インスタンスを追加して、スキン情報を書き込む
	if (!AddInstance(meshID, world)) return -1;
	InstanceData& d = m_instances.back();
	d.boneOffset = offset;
	d.isSkinned = 1;
	d.paramOffset = 0;
	if (params)
	{
		if ((int)m_instanceParams.size() < kMaxParamSets)
		{
			d.paramOffset = (unsigned int)m_instanceParams.size();
			m_instanceParams.push_back(*params);
		}
		else
		{
			++m_droppedParams;
			WarnOnce(1u << 1, "[Instanced] param sets full (kMaxParamSets). default params used");
		}
	}
	return (int)offset;
}

void InstancedSceneRenderer::Cull(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& viewProj, const Math::Vector3& camPos)
{
	if (!IsReady()) return;

	{
		ID3D11ShaderResourceView* nulls[3] = { nullptr, nullptr, nullptr };
		ctx->VSSetShaderResources(10, 3, nulls);
	}

	// 視錐台6平面を抽出
	DirectX::XMFLOAT4X4 m;
	DirectX::XMStoreFloat4x4(&m, viewProj);
	CBCull cb = {};
	cb.planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
	cb.planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
	cb.planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
	cb.planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
	cb.planes[4] = { m._13,         m._23,         m._33,         m._43 };
	cb.planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };
	for (int i = 0; i < 6; ++i)
	{
		float len = sqrtf(cb.planes[i].x * cb.planes[i].x
			+ cb.planes[i].y * cb.planes[i].y
			+ cb.planes[i].z * cb.planes[i].z);
		if (len > 0.0001f)
		{
			cb.planes[i].x /= len; cb.planes[i].y /= len;
			cb.planes[i].z /= len; cb.planes[i].w /= len;
		}
	}
	cb.instanceCount = m_instanceCount;
	cb.typeCount = (int)m_types.size();
	cb.drawTypeCount = (int)m_drawTypes.size();
	cb.camPos = camPos; cb.maxDist = 0.0f;
	ctx->UpdateSubresource(m_cbCull.Get(), 0, nullptr, &cb, 0, 0);
	ctx->CSSetConstantBuffers(0, 1, m_cbCull.GetAddressOf());

	ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };

	// パス0: カウンタ初期化
	{
		ID3D11UnorderedAccessView* uavs[2] = { m_countUAV.Get(), m_cursorUAV.Get() };
		ctx->CSSetShader(m_clearCS.Get(), nullptr, 0);
		ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		ctx->Dispatch((m_maxTypes + 63) / 64, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
	}
	// パス1: カウント
	{
		ctx->CSSetShader(m_countCS.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 1, m_allSRV.GetAddressOf());
		ctx->CSSetUnorderedAccessViews(0, 1, m_countUAV.GetAddressOf(), nullptr);
		ctx->Dispatch((m_instanceCount + 63) / 64, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 1, nullSRVs);
	}
	// パス2: オフセット＋Indirect引数
	{
		ID3D11ShaderResourceView* srvs[2] = { m_countSRV.Get(), m_drawTypeSRV.Get() };
		ID3D11UnorderedAccessView* uavs[2] = { m_offsetUAV.Get(), m_argsUAV.Get() };
		ctx->CSSetShader(m_offsetCS.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 2, srvs);
		ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		ctx->Dispatch(1, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 2, nullSRVs);
	}
	// パス3: 詰め込み
	{
		ID3D11ShaderResourceView* srvs[2] = { m_allSRV.Get(), m_offsetSRV.Get() };
		ID3D11UnorderedAccessView* uavs[2] = { m_visibleUAV.Get(), m_cursorUAV.Get() };
		ctx->CSSetShader(m_scatterCS.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 2, srvs);
		ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		ctx->Dispatch((m_instanceCount + 63) / 64, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 2, nullSRVs);
	}
	ctx->CSSetShader(nullptr, nullptr, 0);
}

void InstancedSceneRenderer::BindForDraw(ID3D11DeviceContext* ctx, int meshID)
{
	CBDraw cd = {};
	cd.meshID = (unsigned int)meshID;
	ctx->UpdateSubresource(m_cbDraw.Get(), 0, nullptr, &cd, 0, 0);

	ctx->IASetInputLayout(m_layout.Get());
	ctx->VSSetShader(m_vs.Get(), nullptr, 0);
	ID3D11ShaderResourceView* srvs[3] = {
		m_visibleSRV.Get(), m_offsetSRV.Get(), m_boneSRV.Get() };
	ctx->VSSetShaderResources(10, 3, srvs);
	ctx->VSSetConstantBuffers(7, 1, m_cbDraw.GetAddressOf());
	ctx->PSSetShaderResources(17, 1, m_paramSRV.GetAddressOf());
}

void InstancedSceneRenderer::CullShadow(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& viewProj,const Math::Vector3& camPos)
{
	if (!IsReady()) return;

	{
		ID3D11ShaderResourceView* nulls[3] = { nullptr, nullptr, nullptr };
		ctx->VSSetShaderResources(10, 3, nulls);
	}

	// 視錐台6平面を抽出
	DirectX::XMFLOAT4X4 m;
	DirectX::XMStoreFloat4x4(&m, viewProj);
	CBCull cb = {};
	cb.planes[0] = { m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41 };
	cb.planes[1] = { m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41 };
	cb.planes[2] = { m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42 };
	cb.planes[3] = { m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42 };
	cb.planes[4] = { m._13,         m._23,         m._33,         m._43 };
	cb.planes[5] = { m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43 };
	for (int i = 0; i < 6; ++i)
	{
		float len = sqrtf(cb.planes[i].x * cb.planes[i].x
			+ cb.planes[i].y * cb.planes[i].y
			+ cb.planes[i].z * cb.planes[i].z);
		if (len > 0.0001f)
		{
			cb.planes[i].x /= len; cb.planes[i].y /= len;
			cb.planes[i].z /= len; cb.planes[i].w /= len;
		}
	}
	cb.instanceCount = m_instanceCount;
	cb.typeCount = (int)m_types.size();
	cb.drawTypeCount = (int)m_drawTypes.size();
	cb.camPos = camPos; cb.maxDist = 40.0f;
	ctx->UpdateSubresource(m_cbCull.Get(), 0, nullptr, &cb, 0, 0);
	ctx->CSSetConstantBuffers(0, 1, m_cbCull.GetAddressOf());

	ID3D11UnorderedAccessView* nullUAVs[2] = { nullptr, nullptr };
	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };

	// パス0: カウンタ初期化
	{
		ID3D11UnorderedAccessView* uavs[2] = { m_shCountUAV.Get(), m_shCursorUAV.Get() };
		ctx->CSSetShader(m_clearCS.Get(), nullptr, 0);
		ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		ctx->Dispatch((m_maxTypes + 63) / 64, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
	}
	// パス1: カウント
	{
		ctx->CSSetShader(m_countCS.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 1, m_allSRV.GetAddressOf());
		ctx->CSSetUnorderedAccessViews(0, 1, m_shCountUAV.GetAddressOf(), nullptr);
		ctx->Dispatch((m_instanceCount + 63) / 64, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 1, nullSRVs);
	}
	// パス2: オフセット＋Indirect引数
	{
		ID3D11ShaderResourceView* srvs[2] = { m_shCountSRV.Get(), m_drawTypeSRV.Get() };
		ID3D11UnorderedAccessView* uavs[2] = { m_shOffsetUAV.Get(), m_shArgsUAV.Get() };
		ctx->CSSetShader(m_offsetCS.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 2, srvs);
		ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		ctx->Dispatch(1, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 2, nullSRVs);
	}
	// パス3: 詰め込み
	{
		ID3D11ShaderResourceView* srvs[2] = { m_allSRV.Get(), m_shOffsetSRV.Get() };
		ID3D11UnorderedAccessView* uavs[2] = { m_shVisibleUAV.Get(), m_shCursorUAV.Get() };
		ctx->CSSetShader(m_scatterCS.Get(), nullptr, 0);
		ctx->CSSetShaderResources(0, 2, srvs);
		ctx->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);
		ctx->Dispatch((m_instanceCount + 63) / 64, 1, 1);
		ctx->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
		ctx->CSSetShaderResources(0, 2, nullSRVs);
	}
	ctx->CSSetShader(nullptr, nullptr, 0);
}

void InstancedSceneRenderer::DrawAllShadow(ID3D11DeviceContext* ctx)
{
	if (!IsReady()) return;
	const KdMesh* lastMesh = nullptr;
	for (size_t i = 0; i < m_drawTypes.size(); ++i)
	{
		const DrawType& dt = m_drawTypes[i];
		if (!dt.mesh) continue;
		if (dt.mesh != lastMesh) { dt.mesh->SetToDevice(); lastMesh = dt.mesh; }

		CBDraw cd = {}; cd.meshID = (unsigned int)dt.meshID;
		ctx->UpdateSubresource(m_cbDraw.Get(), 0, nullptr, &cd, 0, 0);

		ctx->IASetInputLayout(m_shadowLayout.Get());
		ctx->VSSetShader(m_shadowVS.Get(), nullptr, 0);
		ID3D11ShaderResourceView* srvs[3] = {
			m_shVisibleSRV.Get(), m_shOffsetSRV.Get(), m_boneSRV.Get() };
		ctx->VSSetShaderResources(10, 3, srvs);
		ctx->VSSetConstantBuffers(7, 1, m_cbDraw.GetAddressOf());

		ctx->DrawIndexedInstancedIndirect(m_shArgsBuffer.Get(), (UINT)i * 20);
	}
}

void InstancedSceneRenderer::BeginFrame(const DirectX::XMFLOAT3& camPos)
{
	m_instances.clear();
	m_boneMatrices.clear();
	m_instanceParams.clear();
	m_frameCamPos = camPos;
	m_droppedInstances = 0;
	m_droppedParams = 0;
	m_droppedBones = 0;

	// 0番 = パラメータを渡さなかったインスタンス用の既定値（blend = 1）
	InstanceParams def = {};
	def.p[3].w = 1.0f;
	m_instanceParams.push_back(def);
}

int InstancedSceneRenderer::RegisterModelWithLod(
	const std::shared_ptr<KdModelData>& spModel, int gridSize)
{
	// LOD0（元モデル）を通常どおり登録
	int id0 = RegisterModel(spModel);
	if (id0 < 0) return -1;

	// LOD1 を、別種別として登録（入らなければ LOD なし）
	if ((int)m_types.size() >= m_maxTypes) return id0;
	int id1 = (int)m_types.size();

	MeshType mt;
	mt.model = spModel;
	mt.localCenter = m_types[id0].localCenter;
	mt.localRadius = m_types[id0].localRadius;
	bool any = false;

	for (auto& node : spModel->GetOriginalNodes())
	{
		if (!node.m_spMesh) continue;
		auto lod = BuildLodMesh(*node.m_spMesh, gridSize);
		if (!lod) continue;
		m_lodMeshKeeper.push_back(lod);   // 寿命を保持

		auto& subsets = lod->GetSubsets();
		for (int i = 0; i < (int)subsets.size(); ++i)
		{
			if (subsets[i].FaceCount == 0) continue;
			if ((int)m_drawTypes.size() >= m_maxDrawTypes)
			{
				WarnOnce(1u << 4, "[Instanced] draw types full. some subsets are not drawn (raise maxDrawTypes)");
				break;
			}
			DrawType dt;
			dt.meshID = id1;
			dt.mesh = lod.get();
			dt.subsetNo = i;
			dt.materialNo = subsets[i].MaterialNo;
			dt.indexCount = subsets[i].FaceCount * 3;
			dt.startIndex = subsets[i].FaceStart * 3;
			m_drawTypes.push_back(dt);
			any = true;
		}
	}
	if (!any) return id0;   // LODが作れなければ、LODなしで運用

	m_types.push_back(mt);
	m_types[id0].lodMeshID = id1;
	return id0;
}

int InstancedSceneRenderer::RegisterMaterialVariant(int baseMeshID, int materialId)
{
	if (baseMeshID < 0 || baseMeshID >= (int)m_types.size()) return -1;
	if ((int)m_types.size() >= m_maxTypes)
	{
		// 入らないときは本体を二重に描かないよう -1 を返す
		++m_droppedTypes;
		WarnOnce(1u << 3, "[Instanced] mesh types full. material variant not registered (raise maxTypes)");
		return -1;
	}

	int newId = (int)m_types.size();
	MeshType mt = m_types[baseMeshID];
	mt.lodMeshID = -1;   // バリアントにLODは付けない

	size_t count = m_drawTypes.size();   // 先に控える（push_back で伸びるため）
	for (size_t i = 0; i < count; ++i)
	{
		if (m_drawTypes[i].meshID != baseMeshID) continue;
		if ((int)m_drawTypes.size() >= m_maxDrawTypes)
		{
			WarnOnce(1u << 4, "[Instanced] draw types full. some subsets are not drawn (raise maxDrawTypes)");
			break;
		}
		DrawType dt = m_drawTypes[i];
		dt.meshID = newId;
		dt.materialId = materialId;
		m_drawTypes.push_back(dt);
	}
	m_types.push_back(mt);
	return newId;
}