#pragma once

// ============================================
// GPU駆動レンダリング : 複数メッシュ種別（Bucket Sort）
//   バケット単位 = モデル(meshID)、Draw単位 = drawType(モデル×subset)
//   1本の可視バッファに、種別ごとにまとめて詰める
// ============================================
class InstancedSceneRenderer
{
public:
	// 種別・drawType の上限は Init で決める（下はその既定値）
	static const int kDefaultMaxTypes = 256;       // メッシュ種別（LOD・マテリアル差分を含む）
	static const int kDefaultMaxDrawTypes = 4096;  // drawType（種別×subset）

	static const int kBonesPerInstance = 300;      // 1体のボーン数の上限
	static const int kMaxSkinnedInstances = 128;
	static const int kBoneCapacity = kBonesPerInstance * kMaxSkinnedInstances;   // ボーン行列の総数の上限

	struct InstanceData
	{
		DirectX::XMFLOAT4X4 world;            // 64
		DirectX::XMFLOAT4   boundingSphere;   // 16
		unsigned int        meshID;           // 4
		unsigned int        boneOffset;       // 4
		unsigned int        isSkinned;        // 4
		unsigned int        textureIndex;     // 4
		unsigned int        paramOffset;      // 4
		unsigned int        _pad[3];          // 12  → 計112
	};

	struct InstanceParams
	{
		DirectX::XMFLOAT4 p[4];
	};
	static const int kMaxParamSets = 4096;
	static_assert(sizeof(InstanceData) == 112, "InstanceData must match inc_Instance.hlsli");
	static_assert(sizeof(InstanceParams) == 64, "InstanceParams must be 4 x float4");

	// maxTypes / maxDrawTypes は 0 なら既定値
	bool Init(int maxInstances, int maxTypes = 0, int maxDrawTypes = 0);

	// ---- 毎フレーム描画API ----  
	void BeginFrame(const DirectX::XMFLOAT3& camPos);
	// メッシュを、ワールド行列付きで登録
	void Submit(int meshID, const DirectX::XMMATRIX& world,
		const InstanceParams* params = nullptr, int textureIndex = 0);
	// スキンモデルを、ボーン行列付きで登録。使ったボーン配列の先頭位置を返す（-1 = 失敗）
	//  sharedBoneOffset >= 0 なら、同じフレームに送ったボーン配列を使い回す（bones は読まない）
	int SubmitSkinned(int meshID, const DirectX::XMMATRIX& world,
		const DirectX::XMFLOAT4X4* bones, int boneCount,
		const InstanceParams* params = nullptr, int sharedBoneOffset = -1);
	void EndFrame();                                          

	// モデルを種別として登録 → meshID を返す（-1=失敗）
	int  RegisterModel(const std::shared_ptr<KdModelData>& spModel);
	// インスタンスを追加（登録は全部済ませてから Build を呼ぶ）。入らなければ false
	bool AddInstance(int meshID, const DirectX::XMMATRIX& world);
	// 追加したインスタンスをGPUへ転送
	void Build();

	// 毎フレーム: カウント → オフセット/引数 → 詰め込み
	void Cull(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& viewProj,const Math::Vector3& camPos);

	// drawMesh コールバック内から呼ぶ（全種別を描画）
	// bindMaterial: (spModel, subsetIndex) を受け取り、テクスチャ等をバインドする関数
	template<class F, class G>
	void DrawAll(ID3D11DeviceContext* ctx, F&& bindMaterial, G&& onMaterialChange);

	bool IsReady() const { return m_ready && m_instanceCount > 0; }
	int  GetInstanceCount() const { return m_instanceCount; }

	void CullShadow(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& lightViewProj, const Math::Vector3& camPos);
	void DrawAllShadow(ID3D11DeviceContext* ctx);

	int GetTypeCount() const { return (int)m_types.size(); }
	int GetDrawTypeCount() const { return (int)m_drawTypes.size(); }

	// LOD付きで登録（内部でLODメッシュを生成し、別種別として登録）
	int RegisterModelWithLod(const std::shared_ptr<KdModelData>& spModel,
		int gridSize = 8);
	// LOD切り替え距離
	void SetLodDistance(float d) { m_lodDistance = d; }

	int RegisterMaterialVariant(int baseMeshID, int materialId);

	// 容量の使用状況（デバッグ表示用）。dropped* は上限で捨てた数（フレーム毎 / 累計）
	struct Usage
	{
		int instances = 0, maxInstances = 0;
		int types = 0, maxTypes = 0;
		int drawTypes = 0, maxDrawTypes = 0;
		int params = 0, maxParams = 0;
		int bones = 0, maxBones = 0;
		int droppedInstances = 0;   // このフレーム
		int droppedParams = 0;      // このフレーム
		int droppedBones = 0;       // このフレーム
		int droppedTypes = 0;       // 累計（登録できなかったモデル・差分）
	};
	Usage GetUsage() const;

private:
	struct MeshType
	{
		std::shared_ptr<KdModelData> model;
		DirectX::XMFLOAT3 localCenter{ 0,0,0 };
		float localRadius = 1.0f;
		int lodMeshID = -1;
	};
	struct DrawType
	{
		int meshID = 0;
		const KdMesh* mesh = nullptr;
		int subsetNo = 0;
		unsigned int materialNo = 0;
		unsigned int indexCount = 0;
		unsigned int startIndex = 0;
		int materialId = 0;
	};

	bool CreateShaders();
	bool CreateBuffers(int maxInstances);
	void BindForDraw(ID3D11DeviceContext* ctx, int meshID);

	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;
	ComPtr<ID3D11VertexShader>        m_vs;
	ComPtr<ID3D11InputLayout>         m_layout;
	ComPtr<ID3D11ComputeShader>       m_clearCS;
	ComPtr<ID3D11ComputeShader>       m_countCS;
	ComPtr<ID3D11ComputeShader>       m_offsetCS;
	ComPtr<ID3D11ComputeShader>       m_scatterCS;

	// バッファ
	ComPtr<ID3D11Buffer>              m_allBuffer;
	ComPtr<ID3D11ShaderResourceView>  m_allSRV;
	ComPtr<ID3D11Buffer>              m_visibleBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_visibleUAV;
	ComPtr<ID3D11ShaderResourceView>  m_visibleSRV;
	ComPtr<ID3D11Buffer>              m_countBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_countUAV;
	ComPtr<ID3D11ShaderResourceView>  m_countSRV;
	ComPtr<ID3D11Buffer>              m_offsetBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_offsetUAV;
	ComPtr<ID3D11ShaderResourceView>  m_offsetSRV;
	ComPtr<ID3D11Buffer>              m_cursorBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_cursorUAV;
	ComPtr<ID3D11Buffer>              m_argsBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_argsUAV;
	ComPtr<ID3D11Buffer>              m_drawTypeBuffer;
	ComPtr<ID3D11ShaderResourceView>  m_drawTypeSRV;
	ComPtr<ID3D11Buffer>              m_cbCull;
	ComPtr<ID3D11Buffer>              m_cbDraw;

	// インスタンス情報
	std::vector<MeshType>     m_types;
	std::vector<DrawType>     m_drawTypes;
	std::vector<InstanceData> m_instances;
	int  m_maxInstances = 0;
	int  m_maxTypes = kDefaultMaxTypes;
	int  m_maxDrawTypes = kDefaultMaxDrawTypes;
	int  m_instanceCount = 0;

	// 上限で捨てた数と、1回だけの警告
	int  m_droppedInstances = 0;
	int  m_droppedParams = 0;
	int  m_droppedBones = 0;
	int  m_droppedTypes = 0;
	unsigned int m_warned = 0;
	void WarnOnce(unsigned int bit, const char* msg);
	bool m_ready = false;

	// スキンメッシュ用11
	ComPtr<ID3D11Buffer>             m_boneBuffer;
	ComPtr<ID3D11ShaderResourceView> m_boneSRV;
	std::vector<DirectX::XMFLOAT4X4> m_boneMatrices;

	// シャドウ用
	ComPtr<ID3D11VertexShader>        m_shadowVS;
	ComPtr<ID3D11InputLayout>         m_shadowLayout;
	ComPtr<ID3D11Buffer>              m_shVisibleBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_shVisibleUAV;
	ComPtr<ID3D11ShaderResourceView>  m_shVisibleSRV;
	ComPtr<ID3D11Buffer>              m_shCountBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_shCountUAV;
	ComPtr<ID3D11ShaderResourceView>  m_shCountSRV;
	ComPtr<ID3D11Buffer>              m_shOffsetBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_shOffsetUAV;
	ComPtr<ID3D11ShaderResourceView>  m_shOffsetSRV;
	ComPtr<ID3D11Buffer>              m_shCursorBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_shCursorUAV;
	ComPtr<ID3D11Buffer>              m_shArgsBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_shArgsUAV;

	// パラメータ用
	ComPtr<ID3D11Buffer>             m_paramBuffer;
	ComPtr<ID3D11ShaderResourceView> m_paramSRV;
	std::vector<InstanceParams>      m_instanceParams;

	//LOD用
	float m_lodDistance = 20.0f;
	std::vector<std::shared_ptr<KdMesh>> m_lodMeshKeeper;
	DirectX::XMFLOAT3 m_frameCamPos;
};

// ---- DrawAll ----
template<class F, class G>
void InstancedSceneRenderer::DrawAll(ID3D11DeviceContext* ctx,
	F&& bindMaterial, G&& onMaterialChange)
{
	if (!IsReady()) return;
	const KdMesh* lastMesh = nullptr;
	int lastMaterialId = -1;

	for (size_t i = 0; i < m_drawTypes.size(); ++i)
	{
		const DrawType& dt = m_drawTypes[i];
		if (!dt.mesh) continue;

		// マテリアルが変わったときだけ、PSを差し替え
		if (dt.materialId != lastMaterialId)
		{
			onMaterialChange(dt.materialId);
			lastMaterialId = dt.materialId;
		}

		if (dt.mesh != lastMesh) { dt.mesh->SetToDevice(); lastMesh = dt.mesh; }

		bindMaterial(m_types[dt.meshID].model, (int)dt.materialNo);
		BindForDraw(ctx, dt.meshID);
		ctx->DrawIndexedInstancedIndirect(m_argsBuffer.Get(), (UINT)i * 20);
	}

	ID3D11ShaderResourceView* nulls[3] = { nullptr, nullptr, nullptr };
	ctx->VSSetShaderResources(10, 3, nulls);
}