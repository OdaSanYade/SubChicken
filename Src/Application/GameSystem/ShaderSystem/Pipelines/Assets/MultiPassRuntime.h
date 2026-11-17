#pragma once
#include "LightData.h"

using namespace DirectX;
using namespace Microsoft::WRL;

class MultiPassRuntime
{
public:

	// 描画用コールバック
	using DrawMeshCallback = std::function<void()>;

	// 入力の種類
	struct InputBinding
	{
		std::string kind;       // テクスチャの種類（前パス,MRTの名前付きバッファなど）
		std::string key;        // パス名やテクスチャ名など
		std::shared_ptr<KdTexture> texture;
		ComPtr<ID3D11ShaderResourceView> textureSRV;  // textureのとき、ロード済みSRV
	};

	struct CBShadowLighting
	{
		DirectX::XMFLOAT4X4 lightViewProj[3];   // 192
		DirectX::XMFLOAT4   cascadeDist;        // xyz=各段の距離上限, w=未使用
	};

	// パラメータ全パラメータfloat4として登録
	struct ParamRuntime
	{
		std::string name;
		std::string type;        // "float" / "float4"
		std::array<float, 4> value{};
		float minVal = 0.0f, maxVal = 1.0f;
	};

	// 1パス分の情報
	struct PassRuntime
	{
		std::string name;
		int passType = 0;
		float scaleFactor = 1.0f;
		std::string outputName;
		bool isMaterial = false;

		// シェーダ群
		ComPtr<ID3D11PixelShader>  ps;
		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11InputLayout>  inputLayout;
		// スキンメッシュ
		ComPtr<ID3D11VertexShader> vsSkinned;
		ComPtr<ID3D11InputLayout>  inputLayoutSkinned;

		std::vector<ParamRuntime> params;
		std::vector<InputBinding> inputs;

		int targetCount = 1;
		std::vector<std::string> targetNames; 
		bool usesPrevPass = false;
		std::vector<std::string> namedBufferRefs;

		std::vector<DXGI_FORMAT> targetFormats;
		bool hasSkinnedVS = false;
	};

	MultiPassRuntime() = default;
	~MultiPassRuntime() { Release(); };

	bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

	// pipeline.json を読み込む
	bool LoadPipeline(const std::string& jsonText);

	//　特定パラメータの変更
	void SetParam(const std::string& paramName, float x, float y = 0, float z = 0, float w = 0);

	// ライト情報の格納
	void SetLights(const std::vector<PointLightData>& lights);

	//最終出力SRV
	ID3D11ShaderResourceView* GetFinalResultSRV();

	// そのパスのパラメータ群
	std::vector<std::array<float, 4>> GetPassParamValues(const PassRuntime& pass)
	{
		std::vector<std::array<float, 4>> vals;
		for (const auto& p : pass.params)
			vals.push_back(p.value);
		return vals;
	}

	// Execute に外部入力（ゲーム画面）を渡せる
	void Execute(float time, float bass, float mid, float treble,
		const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& worldView, 
		const DirectX::XMMATRIX& world = DirectX::XMMatrixIdentity(),
		int screenW = 1280, int screenH = 720,
		DrawMeshCallback drawMesh = nullptr,
		ID3D11ShaderResourceView* externalInput = nullptr);
	//描画
	void DrawToScreen(int screenW, int screenH);

	void DrawParamUI(const char* uiId = "pipeline");

	bool LoadPipelineFromFile(const std::string& path);

	// 現在のパラメータ値を名前付きで保存
	void SavePreset(const std::string& presetName);
	// プリセットを適用
	void ApplyPreset(const std::string& presetName);
	// プリセット一覧
	std::vector<std::string> GetPresetNames() const;
	// プリセットをファイルに保存/読み込み
	bool SavePresetsToFile(const std::string& path);
	bool LoadPresetsFromFile(const std::string& path);
	void SetPresetsPath(const std::string& path) { m_presetsPath = path; }
	void DeletePreset(const std::string& presetName);

	void SaveLastParams();   // 現在のパラメータ値を "__last__" として保存
	void LoadLastParams();   // "__last__" を復元

	// 既定値：LoadPipeline 成功時の値（起動時は "__last__" を当てた後の値）を覚えておき、
	// RestoreDefaults で戻す。エディタからパイプラインが届くと、その値が新しい既定値になる
	void CaptureDefaults();
	void RestoreDefaults();

	void UpdateWorldMatrix(const DirectX::XMMATRIX& wvp,
		const DirectX::XMMATRIX& worldView,
		const DirectX::XMMATRIX& world)
	{
		UpdateWVPCB(wvp, worldView, world);
	}

	void SetMaterialParams(const DirectX::XMFLOAT4& baseColor, float metallic, float roughness);

	void SetShadowViewProj(const DirectX::XMMATRIX& lightViewProj);

	void SetCascades(const DirectX::XMMATRIX* vps, const DirectX::XMFLOAT3& dists);

	void SetCamera(const DirectX::XMFLOAT3& camPos,
		const DirectX::XMMATRIX& viewProj);
	
	void UseStaticVS();
	void UseSkinnedVS();

	void SetBones(const DirectX::XMFLOAT4X4* bones, int count);

	ID3D11ShaderResourceView* GetNamedBufferSRV(const std::string& name);

	// マテリアルパスの描画中（drawMesh コールバック内）に、PSを一時差し替え
	void OverrideMaterialPS(ID3D11PixelShader* ps,
		const std::vector<std::array<float, 4>>* params = nullptr);
	void RestoreMaterialPS();

	void BeginOverlay(ID3D11PixelShader* ps, unsigned int writeMask,
		const std::vector<std::array<float, 4>>* params = nullptr);
	void EndOverlay();

private:

	void Release();

	// ---- RTプール----
	struct PooledRT {
		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11RenderTargetView> rtv;
		ComPtr<ID3D11ShaderResourceView> srv;
		int width = 0, height = 0; bool inUse = false;
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uint32_t lastUsedFrame = 0;
	};
	int AcquireRT(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);
	void ResetRTPool();

	struct PooledDepth {
		ComPtr<ID3D11Texture2D> tex;
		ComPtr<ID3D11DepthStencilView> dsv;
		int width = 0, height = 0; bool inUse = false;
		uint32_t lastUsedFrame = 0;
	};
	int AcquireDepth(int w, int h);
	void ResetDepthPool();

	// プールの掃除：しばらく使っていない RT と、画面サイズが変わった時の古い RT を解放する
	void TrimPools(int screenW, int screenH);
	uint32_t m_poolFrame = 0;
	int m_poolScreenW = 0, m_poolScreenH = 0;
	static constexpr uint32_t kPoolEvictFrames = 120;

	//　初期化用
	bool CreateFullscreenVS();
	bool CreateConstantBuffers();
	bool CreateSamplerState();
	bool CreateRasterizerState();

	//定数バッファ更新
	void UpdateWVPCB(const DirectX::XMMATRIX& wvp,
		const DirectX::XMMATRIX& worldView,const DirectX::XMMATRIX& world);
	void UpdateParameterCB(float time, float bass, float mid, float treble,
		const std::vector<std::array<float, 4>>& params);

	// 文字列コンパイル
	bool CompilePS(const std::string& code, ComPtr<ID3D11PixelShader>& outPS);
	bool CompileVS(const std::string& code, ComPtr<ID3D11VertexShader>& outVS,
		std::vector<char>& outBytecode);
	void BuildMaterialLayout(PassRuntime& pass, const std::vector<char>& vsBytecode);
	void BuildMaterialLayoutSkinned(PassRuntime& pass,
		const std::vector<char>& vsBytecode);

	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;

	std::vector<PooledRT> m_rtPool;
	std::vector<PooledDepth> m_depthPool;

	ComPtr<ID3D11VertexShader> m_fullscreenVS;
	ComPtr<ID3D11SamplerState> m_sampler;
	ComPtr<ID3D11RasterizerState> m_rsCullNone;

	size_t m_paramCapacity = 0;
	std::vector<PassRuntime> m_passes;

	//定数バッファ群
	ComPtr<ID3D11Buffer> m_cbTime;    // b0
	ComPtr<ID3D11Buffer> m_cbWVP;	  // b1
	ComPtr<ID3D11Buffer> m_cbParams;  // b2
	ComPtr<ID3D11Buffer> m_cbLights;  // b3
	ComPtr<ID3D11Buffer> m_cbMaterial;// b4
	ComPtr<ID3D11Buffer> m_cbShadowLighting; // b5
	ComPtr<ID3D11Buffer> m_cbCamera; // b6
	ComPtr<ID3D11Buffer> m_cbBones;

	// SetParam 用：名前 → 該当パラメータ（m_passes を作り直したら作り直す）
	std::unordered_map<std::string, std::vector<ParamRuntime*>> m_paramIndex;
	bool m_paramIndexDirty = true;
	void RebuildParamIndex();

	// 既定値（CaptureDefaults で記録）
	std::unordered_map<std::string, std::array<float, 4>> m_defaultValues;

	static constexpr int kBaseSize = 512;   
	static constexpr int kPrevPassSlot = 8;
	static constexpr int kNamedBufferBaseSlot = 9;

	std::vector<std::vector<int>> m_passResults;   // 各パスの結果RTインデックス
	void RenderPass(
		PassRuntime& pass, const std::vector<int>& rtIndices,
		ID3D11ShaderResourceView* prevSRV,
		const std::vector<ID3D11ShaderResourceView*>& namedSRVs,
		bool meshMode, DrawMeshCallback drawMesh);

	ComPtr<ID3D11PixelShader> m_copyPS;
	bool CreateCopyPS();

	ComPtr<ID3D11BlendState> m_alphaBlend;
	bool CreateBlendState();

	float m_lastTime,m_lastBass,m_lastMid,m_lastTreble;

	struct Preset {
		std::string name;
		std::unordered_map<std::string, std::array<float, 4>> values;
	};
	std::vector<Preset> m_presets;
	std::string m_presetsPath;
	char m_presetNameBuf[64] = "";
	void CreateMaterialCB();

	PassRuntime* m_currentMeshPass = nullptr;
	std::unordered_map<std::string, ID3D11ShaderResourceView*> m_namedBufferSRVs;

	//TAA
	ComPtr<ID3D11Texture2D> m_historyTex;
	ComPtr<ID3D11ShaderResourceView> m_historySRV;
	ComPtr<ID3D11RenderTargetView> m_historyRTV;
	int m_historyW = 0, m_historyH = 0;
	DXGI_FORMAT m_historyFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	std::string m_taaOutputName;
	void EnsureHistoryRT(int width, int height, DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM);

	// Overlay
	ID3D11BlendState* GetWriteMaskBlend(unsigned int mask);
	std::unordered_map<unsigned int, ComPtr<ID3D11BlendState>> m_maskBlends;
	ComPtr<ID3D11DepthStencilState> m_overlayDepth;
};