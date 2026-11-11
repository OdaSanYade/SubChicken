#pragma once
#pragma comment(lib, "ws2_32.lib")

#include "Assets/ShaderAsset.h"
#include "LiveLinkServer.h"
// ============================================
// 汎用シェーダランタイム
// ============================================
class ShaderRuntime
{
public:
	bool Init(ID3D11Device* device, ID3D11DeviceContext* context);

	// ポストFXを名前で登録（PSのHLSLコードを渡す）
	bool RegisterEffect(const std::string& name, const std::string& psCode);

	// 登録済みエフェクトを適用
	void ApplyEffect(const std::string& name,
		ID3D11DeviceContext* ctx,
		ID3D11ShaderResourceView* const* textures, int textureCount,
		ID3D11RenderTargetView* outRTV, int width, int height,
		ID3D11DepthStencilView* depthDSV = nullptr);

	// 時間設定（全エフェクト共通）
	void SetTime(float t) { m_time = t; }
	void SetAudio(float b, float m, float t) { m_bass = b; m_mid = m; m_treble = t; }

	// ライブリンク（開発用）。bindAddr 以外からは受け付けない
	bool StartNetwork(unsigned short port = 54321, const char* bindAddr = "127.0.0.1");  // 受信開始
	void StopNetwork();
	void PollNetwork();   // 毎フレーム呼ぶ：受信して即リロード

	// 登録済みか
	bool HasEffect(const std::string& name) const;

	//指定ファイルを登録
	bool RegisterEffectFromFile(const std::string& name, const std::string& filepath);

	//指定フォルダ内にファイルをすべて登録
	void LoadEffectsFromFolder(const std::string& folderPath);

	//シェーダコードの書き換え
	bool ReloadEffect(const std::string& name, const std::string& psCode);

	//シェーダコードの読み直し
	bool ReloadEffectFromFile(const std::string& name, const std::string& filepath);

	// マテリアル登録（VS+PSのHLSL）。VSバイトコードは内部保持
	bool RegisterMaterial(const std::string& name,
		const std::string& vsCode,
		const std::string& psCode);

	//すべてのファイル登録
	bool RegisterMaterialFromFiles(const std::string& name,
		const std::string& vsPath,
		const std::string& psPath);

	// PSだけリロード（VS固定）
	bool ReloadMaterialPS(const std::string& name, const std::string& psCode);

	//アセット（json）ファイルロード
	void LoadAssetsFromFolder(const std::string& folderPath);
	std::vector<std::string> CollectAssetFiles(const std::string& folderPath) const;
	bool LoadAssetFile(const std::string& filePath);

	//マテリアル名獲得
	std::vector<std::string> GetMaterialNames() const;

	// マテリアルのVSバイトコード取得（向こうが入力レイアウト作成に使う）
	const void* GetMaterialVSBytecode(const std::string& name) const;
	size_t GetMaterialVSBytecodeSize(const std::string& name) const;

	// マテリアルの行列・時間設定
	void SetMaterialMatrices(const std::string& name,
		const DirectX::XMMATRIX& wvp,
		const DirectX::XMMATRIX& worldView);

	// マテリアルをバインド（シェーダ・cbuffer設定。描画は向こう）
	void BindMaterial(const std::string& name, ID3D11DeviceContext* ctx);

	void RegisterFromAsset(const ShaderAsset& asset);

	void SetParam(const std::string& effectName,
		const std::string& paramName, const float* values, int count);
	// 便利オーバーロード（float1つ）
	void SetParam(const std::string& effectName,
		const std::string& paramName, float v)
	{
		SetParam(effectName, paramName, &v, 1);
	}

	bool BuildMaterialLayout(const std::string& name,
		const std::vector<VertexElement>& layout);

	void DrawParamUI(const std::string& name);

	using PipelineCallback = std::function<void(const std::string& jsonText)>;
	void SetPipelineCallback(PipelineCallback cb) { m_pipelineCallback = cb; }

	using ParticleCallback = std::function<void(const std::string& jsonText)>;
	void SetParticleCallback(ParticleCallback cb) { m_particleCallback = cb; }

	using TrailCallback = std::function<void(const std::string& jsonText)>;
	void SetTrailCallback(TrailCallback cb) { m_trailCallback = cb; }

	DirectX::XMFLOAT3 m_camPos = { 0, 1.5f, -5.0f };
	DirectX::XMFLOAT4X4 m_invViewProj;
	void SetRaymarchCamera(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj,
		const DirectX::XMFLOAT3& camPos);

	void SetSceneLightDir(const DirectX::XMFLOAT3& dirInView)
	{
		m_sceneLightDir = dirInView;
	}

	bool SetMaterialParam(const std::string& matName,
		const std::string& paramName, float x, float y = 0.0f, float z = 0.0f, float w = 0.0f);
	
	void SetBones(const std::string& matName,
		const DirectX::XMFLOAT4X4* bones, int count);

	void RenderShadowMap(ID3D11DeviceContext* ctx,
		const DirectX::XMMATRIX& lightViewProj,
		std::function<void(const DirectX::XMMATRIX&)> drawModels);

	void RenderShadowMapTo(ID3D11DeviceContext* ctx,
		ID3D11DepthStencilView* dsv, int size,
		const DirectX::XMMATRIX& lightViewProj,
		std::function<void(const DirectX::XMMATRIX&)> drawModels);
	void SetShadowMatrix(ID3D11DeviceContext* ctx, const DirectX::XMMATRIX& lightWVP);

	// アルファクリップ用シャドウ
	void SetShadowAlphaClip(bool enable) { m_shadowAlphaClip = enable; }
	void SetShadowAlphaThreshold(float t) { m_shadowAlphaThreshold = t; }

	ID3D11PixelShader* GetMaterialPS(const std::string& name) const;
	// マテリアルのデフォルトパラメータを取得（params[16] 相当）
	bool GetMaterialParamDefaults(const std::string& name,
		std::vector<std::array<float, 4>>& out) const;

	unsigned int GetMaterialWriteMask(const std::string& name) const;

	// マテリアルのVSと入力レイアウトを取得
	ID3D11VertexShader* GetMaterialVS(const std::string& name) const;
	ID3D11InputLayout* GetMaterialLayout(const std::string& name) const;
	ID3D11VertexShader* GetMaterialVSSkinned(const std::string& name) const;
	ID3D11InputLayout* GetMaterialLayoutSkinned(const std::string& name) const;
	bool HasMaterial(const std::string& name) const;

	// マテリアル（PS・パラメータ・登録）が変わるたびに増える番号。キャッシュの更新判定に使う
	uint32_t GetMaterialRevision() const { return m_materialRevision; }

private:
	uint32_t m_materialRevision = 0;
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	struct ParamRuntime
	{
		std::string name;
		std::string type;
		std::array<float, 4> value;
		float minVal = 0.0f;
		float maxVal = 1.0f;
	};

	struct EffectEntry
	{
		ComPtr<ID3D11PixelShader> ps;
		std::vector<ParamRuntime> params;
	};
	std::map<std::string, EffectEntry> m_effects;

	bool CompilePS(const std::string& code, ComPtr<ID3D11PixelShader>& outPS);

	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
	ComPtr<ID3D11VertexShader> m_vs;       // 共有フルスクリーンVS
	ComPtr<ID3D11SamplerState> m_sampler;  // 共有サンプラー
	ComPtr<ID3D11Buffer> m_cb;             // 共有cbuffer（time+audio+params）


	float m_time = 0, m_bass = 0, m_mid = 0, m_treble = 0;

	struct MaterialEntry
	{
		ComPtr<ID3D11VertexShader> vs;
		ComPtr<ID3D11PixelShader> ps;
		std::vector<char> vsBytecode;
		ComPtr<ID3D11Buffer> cbTransform;
		ComPtr<ID3D11Buffer> cbBones;
		DirectX::XMFLOAT4X4 wvp{}, worldView{};
		std::vector<ParamRuntime> params;
		ComPtr<ID3D11InputLayout> inputLayout;
		std::vector<VertexElement> layoutSpec;
		unsigned int writeMask = 0xF;
		ComPtr<ID3D11VertexShader> vsSkinned;
		ComPtr<ID3D11InputLayout>  inputLayoutSkinned;
		std::vector<char>          vsSkinnedBytecode;
	};
	std::map<std::string, MaterialEntry> m_materials;
	
	ComPtr<ID3D11Buffer> m_cbRaymarchCam;
	ComPtr<ID3D11DepthStencilState> m_depthState;
	DirectX::XMFLOAT4X4 m_viewProj;

	PipelineCallback m_pipelineCallback;
	ParticleCallback m_particleCallback;
	TrailCallback    m_trailCallback;

	LiveLinkServer m_liveLink;

	// 受信したヘッダ → 処理の対応表。表に無いヘッダは捨てる
	void HandleLiveMessage(const std::string& header, const std::string& body);

	DirectX::XMFLOAT3 m_sceneLightDir = { 0.3f, 0.5f, -1.0f };

	//テスト
	ComPtr<ID3D11PixelShader> m_testPS;  
	ComPtr<ID3D11PixelShader> m_irradiancePS;   
	ComPtr<ID3D11Buffer> m_cbFace;

	ComPtr<ID3D11Buffer> m_cbPrefilter;
	ComPtr<ID3D11PixelShader> m_prefilterPS;

	ComPtr<ID3D11PixelShader> m_brdfPS;

	ComPtr<ID3D11VertexShader> m_shadowVS;
	ComPtr<ID3D11InputLayout> m_shadowInputLayout;
	ComPtr<ID3D11Buffer> m_cbShadow;

	ComPtr<ID3D11PixelShader> m_shadowAlphaPS;
	ComPtr<ID3D11Buffer>      m_cbShadowClip;
	float                     m_shadowAlphaThreshold = 0.8f;
	bool                      m_shadowAlphaClip = true;

	void GenerateIrradianceMap(ID3D11DeviceContext* ctx);
	void GeneratePrefilterMap(ID3D11DeviceContext* ctx);
	void GenerateBRDFLut(ID3D11DeviceContext* ctx);
};