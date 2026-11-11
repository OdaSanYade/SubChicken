#pragma once

// ============================================
// モデルを G バッファの上に重ねて描く共通クラス
//  ・残像（GhostRenderer）と X 線（XRayRenderer）はこのクラスの設定違い
//  ・シェーダ（VS / PS）だけを派生側の Init で渡す
// ============================================
class OverlayModelRenderer
{
public:
	struct Entry
	{
		std::shared_ptr<KdModelData> model;
		DirectX::XMMATRIX world;
		DirectX::XMFLOAT4 color;
		DirectX::XMFLOAT4 rimColor;
		DirectX::XMFLOAT4 moveDirSpeed;
		int  boneOffset = -1;
	};

	virtual ~OverlayModelRenderer() = default;

	void BeginFrame() { m_entries.clear(); m_boneMatrices.clear(); }

	void Add(const std::shared_ptr<KdModelData>& model,
		const DirectX::XMMATRIX& world, const DirectX::XMFLOAT4& color,
		const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed)
	{
		m_entries.push_back({ model, world, color, rimColor, moveDirSpeed });
	}

	// bones は boneCount 本。モデルが実際に使う本数だけ送る
	void AddSkinned(const std::shared_ptr<KdModelData>& model,
		const DirectX::XMMATRIX& world, const DirectX::XMFLOAT4& color,
		const DirectX::XMFLOAT4& rimColor, const DirectX::XMFLOAT4& moveDirSpeed,
		const DirectX::XMFLOAT4X4* bones, int boneCount);

	// gbuffer : PS の t0 に渡す（残像 = GBufferC、X 線 = GBufferE）
	void Render(ID3D11RenderTargetView* rtv,
		const DirectX::XMMATRIX& viewProj,
		int width, int height,
		ID3D11ShaderResourceView* gbuffer,
		const DirectX::XMFLOAT3& camPos);

	int GetEntryCount() const { return (int)m_entries.size(); }
	int GetDroppedCount() const { return m_dropped; }

protected:
	// 派生側の Init から呼ぶ
	bool InitShared(const void* vsCode, size_t vsSize, const void* psCode, size_t psSize);

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// モデルが使うボーンの本数（最大ボーン番号 + 1）
	int GetModelBoneCount(const std::shared_ptr<KdModelData>& model);

	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader>  m_ps;
	ComPtr<ID3D11InputLayout>  m_layout;
	ComPtr<ID3D11Buffer>       m_cb;
	ComPtr<ID3D11BlendState>   m_additive;
	ComPtr<ID3D11DepthStencilState> m_depthState;
	ComPtr<ID3D11RasterizerState>   m_raster;
	ComPtr<ID3D11SamplerState> m_sampler;

	std::vector<Entry> m_entries;

	static const int kBonesPerInstance = 300;
	static const int kBoneCapacity = kBonesPerInstance * 64;   // ボーン行列の総数の上限
	ComPtr<ID3D11Buffer>             m_boneBuffer;
	ComPtr<ID3D11ShaderResourceView> m_boneSRV;
	std::vector<DirectX::XMFLOAT4X4> m_boneMatrices;
	std::unordered_map<const KdModelData*, int> m_boneCountCache;
	int m_dropped = 0;   // 容量不足で描けなかった累計
};
