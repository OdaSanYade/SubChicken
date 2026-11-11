#pragma once


class TrailRenderer
{
public:
	static const int kMaxTextures = 8;

	// 頂点（40バイト）。トレイル共通の見た目パラメータは ParamSet に分け、番号で引く
	struct Vertex
	{
		DirectX::XMFLOAT3 pos;      //  0
		DirectX::XMFLOAT2 uv;       // 12
		DirectX::XMFLOAT4 color;    // 20
		uint32_t paramIndex;        // 36   ParamSet の番号
	};

	// トレイル1本ぶんの見た目パラメータ（112バイト、TrailVS.hlsl の TrailParams と同じ並び）
	struct ParamSet
	{
		DirectX::XMFLOAT4 param;    // edgeSoft, edgeCore, texIndex, emissive
		DirectX::XMFLOAT4 param2;   // fadePow, coreWidth, coreWhite, coreBoost
		DirectX::XMFLOAT4 param3;   // rootBoost, depthFade, -, -
		DirectX::XMFLOAT4 param4;   // uvNoiseStrength, uvNoiseScale, uvNoiseSpeed, dissolveAmount
		DirectX::XMFLOAT4 param5;   // dissolveNoiseScale, dissolveEdgeWidth, edgeNoiseStrength, edgeNoiseScale
		DirectX::XMFLOAT4 param6;   // cellAmount, cellScale, cellEdgeWidth, cellGlow
		DirectX::XMFLOAT4 param7;   // weaveStrength, weaveScale, -, -
	};
	static_assert(sizeof(Vertex) == 40, "TrailRenderer::Vertex must be 40 bytes");
	static_assert(sizeof(ParamSet) == 112, "TrailRenderer::ParamSet must match TrailVS.hlsl");
	static const int kMaxParamSets = 4096;

	bool Init();

	// ---- アセット ----
	int  LoadAssetFolder(const std::string& folder, bool recursive = true);
	// 名前から TrailId を引く。見つからなければ kInvalidTrailId
	TrailId GetTrailId(const std::string& key) const;
	// エディタからのライブ更新（UDP受信）。同名があれば差し替える
	void ApplyAssetFromString(const std::string& jsonStr);
	// 登録済みの名前一覧（デバッグ表示用）
	std::vector<std::string> GetAssetNames() const;

	
	bool GetParticleSpawn(TrailId id, std::string& outEffect, float& outChance) const;

	// ---- 収集と描画 ----
	void BeginFrame() { m_vertsAdd.clear(); m_vertsAlpha.clear(); m_params.clear(); }

	// トレイルを1本積む。リングバッファの展開もここで行う
	void AddTrail(const TrailComponent& t, const DirectX::XMFLOAT3& camPos, float time = 0.0f);

	void Render(ID3D11RenderTargetView* rtv,
		const DirectX::XMMATRIX& viewProj,
		int width, int height,
		ID3D11ShaderResourceView* gbufferC,
		const DirectX::XMFLOAT3& camPos);

	void SetTextureSlot(int slot, ID3D11ShaderResourceView* srv)
	{
		if (slot >= 0 && slot < kMaxTextures) m_textureSlots[slot] = srv;
	}

	int GetVertexCount() const { return (int)(m_vertsAdd.size() + m_vertsAlpha.size()); }
	int GetMaxVertexCount() const { return m_maxVerts; }
	int GetDroppedVertexCount() const { return m_droppedVerts; }   // 容量不足で描けなかった累計

private:
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// 見た目パラメータ
	struct Asset;

	bool LoadAsset(const std::string& path, const std::string& key);
	const Asset* FindAsset(TrailId id) const;

	void DrawList(ID3D11DeviceContext* ctx, const std::vector<Vertex>& verts,
		ID3D11BlendState* blend);

	std::shared_ptr<std::vector<Asset>>                m_assets;   
	std::unordered_map<std::string, TrailId>           m_keyToId;

	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader>  m_ps;
	ComPtr<ID3D11InputLayout>  m_layout;
	ComPtr<ID3D11Buffer>       m_vb;
	ComPtr<ID3D11Buffer>       m_cb;
	ComPtr<ID3D11BlendState>   m_additive;
	ComPtr<ID3D11BlendState>   m_alphaBlend;
	ComPtr<ID3D11DepthStencilState> m_depthState;
	ComPtr<ID3D11RasterizerState>   m_raster;
	ComPtr<ID3D11SamplerState> m_sampler;

	std::vector<Vertex> m_vertsAdd;
	std::vector<Vertex> m_vertsAlpha;
	std::vector<ParamSet> m_params;
	ComPtr<ID3D11Buffer>             m_paramBuffer;
	ComPtr<ID3D11ShaderResourceView> m_paramSRV;
	int m_maxVerts = 0;
	int m_droppedVerts = 0;

	ID3D11ShaderResourceView* m_textureSlots[kMaxTextures] = {};
	std::unordered_map<TrailId, int> m_idToIndex;
};