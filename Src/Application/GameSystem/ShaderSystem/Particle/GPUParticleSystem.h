#pragma once

#include "Assets/ParticleEffect.h"   // ParticleAsset / ParticleLayer / BurstEntry / PathKey

class ParticleAsset;
class ShaderRuntime;

// 粒（64バイト）
struct GPUParticle
{
	DirectX::XMFLOAT3 pos;   float life;
	DirectX::XMFLOAT3 vel;   float age;
	DirectX::XMFLOAT4 color;
	float rot;  float rotSpeed;  float seed;  float _padP;   // 64
};

// エミッタ（592バイト、CPU/GPUでレイアウト一致）
// ※HLSL側（CS / VS / トレイルVS）の struct EmitterData と必ず同じ並びにすること
//   並びが崩れたら下の static_assert でビルドが止まる
struct EmitterData
{
	DirectX::XMFLOAT3 pos;          int   active;       // 16
	DirectX::XMFLOAT3 color;        float gravity;      // 32
	float initialSpeed; float spread; float lifeMin; float lifeMax;  // 48
	int   mode;  int emitTrigger;  float size;  int typeId;          // 64
	int   textureIndex; float cUpward; float cRadial; float cTangent;  // 80
	float cSwirl; float cSphere; float cDrag; float cTurbulence;       // 96
	float cSizeGrow; float cSeek; float cWave; float cSpiral;          // 112
	float cConverge; float emitScale; float _pad1; float _pad2;            // 128
	DirectX::XMFLOAT3 targetPos; float _pad3;                          // 144

	// Phase1（サイズ／アルファカーブ・回転・カールノイズ・速度連動・初速ランダム）
	float sizeBegin;    float sizeMid;   float sizeEnd;    float speedRandom;    // 160
	float fadeIn;       float fadeOut;   float rotSpeedMin; float rotSpeedMax;   // 176
	float curlStrength; float curlScale; float curlSpeed;  float velColorScale;  // 192

	// Phase2a（ストレッチビルボード・発生形状）
	float velSizeScale; int stretchOn; float stretchScale; int shapeType;        // 208
	float shapeRadius;  float shapeHeight; int shapeSurfaceOnly; float _pad4;    // 224

	// Form（粒そのものの形）
	int   formOn;     float formSides; float formRound;  float formSpike;        // 240
	float formAspect; float formTwist; float formEdge;   float formHollow;       // 256

	// Trail（残像 / リボン）
	int   trailOn;    float trailFade; float trailWidth; int   trailMode;        // 272

	// Form 拡張（回転・シアー・波打ち・ふくらみ・扇形・溶解・コア）
	float formRotate; float formShear; float formRipple; float formRippleFreq;   // 288
	float formBulge;  float formArc;   float formDissolve; float formCore;       // 304

	// Color（グラデーション＋粒ごとのばらつき）
	int   colorMode;   float hueJitter;  float satJitter;  float valJitter;      // 320
	float sizeRandom;  int   colorKeyCount; int   emitBase; int   emitCount;     // 336
	DirectX::XMFLOAT4 colorKey0;   // rgb + 位置(0〜1)                            // 352
	DirectX::XMFLOAT4 colorKey1;                                                 // 368
	DirectX::XMFLOAT4 colorKey2;                                                 // 384
	DirectX::XMFLOAT4 colorKey3;                                                 // 400

	// Simulation（空間・エミッタ速度）
	int   simSpace;    float inheritVel; float _pad8;     float _pad9;           // 416
	DirectX::XMFLOAT3 emitterVel; float _pad10;                                  // 432

	// Timing（CPU専用。GPUでは読まないが、レイアウトを合わせるため HLSL にも同じ行が要る）
	float delay; float duration; int looping; float prewarm;                     // 448
	DirectX::XMFLOAT3 emitAxisX; float _padA;   // 464
	DirectX::XMFLOAT3 emitAxisY; float _padB;   // 480
	DirectX::XMFLOAT3 emitAxisZ; float _padC;   // 496

	// Phase8c（サイズ／アルファのカーブ。エディタと同じ）
	int   sizeCurveOn;  float sizeCurve[8];                                  // 532
	int   sizeCurvePointCount; int sizeCurveInterp; float _padD;            // 544
	int   alphaCurveOn; float alphaCurve[8];                                 // 580
	int   alphaCurvePointCount; int alphaCurveInterp; float _padE;          // 592
};
static_assert(sizeof(EmitterData) == 592, "EmitterData must match HLSL struct EmitterData");
static_assert(sizeof(EmitterData) % 16 == 0, "EmitterData stride must be 16-byte aligned");
static_assert(offsetof(EmitterData, targetPos) == 128, "EmitterData layout changed");
static_assert(offsetof(EmitterData, sizeBegin) == 144, "EmitterData layout changed");
static_assert(offsetof(EmitterData, colorKey0) == 336, "EmitterData layout changed");
static_assert(offsetof(EmitterData, delay) == 432, "EmitterData layout changed");
static_assert(offsetof(EmitterData, emitAxisX) == 448, "EmitterData layout changed");
static_assert(offsetof(EmitterData, sizeCurveOn) == 496, "EmitterData layout changed");
static_assert(offsetof(EmitterData, alphaCurveOn) == 544, "EmitterData layout changed");

// 全体共通の定数（dt, time, カウント類）
struct GPUParticleCB
{
	float dt;                       // 0
	float time;                     // 4
	int   totalCount;               // 8
	int   particlesPerEmitter;      // 12   ← ここまで16
	DirectX::XMFLOAT4X4 viewProj;   // 16〜79（64バイト）
	int   screenW;                  // 80
	int   screenH;                  // 84
	int   collisionOn;              // 88
	float nearZ;                    // 92   ← ここまで96
	float farZ;                     // 96
	float restitution;              // 100
	int   trailCursor;              // 104
	int   trailWrite;               // 108  ← ここまで112
};

// 描画用CB
struct GPUDrawCB
{
	DirectX::XMFLOAT4X4 viewProj;
	DirectX::XMFLOAT4X4 view;
	float size;
	int   particlesPerEmitter;
	float nearZ;
	float farZ;
	DirectX::XMFLOAT3 camPos;
	float _pad;
};

// トレイル用CB（16バイト）
struct GPUTrailCB
{
	int trailCursor;
	int _pad0;
	int _pad1;
	int _pad2;
};

class GPUParticleSystem
{
public:

	bool Init();

	// 1フレーム進める。画面サイズと行列はその描画先のものを渡す（既定値は持たない）
	//  sceneWorldPos : GBufferC（xyz = ワールド座標）。衝突に使う
	void Update(float dt, float time, const DirectX::XMMATRIX& viewProj,
		int screenW, int screenH,
		bool collisionOn = false,
		float restitution = 0.5f,
		ID3D11ShaderResourceView* sceneWorldPos = nullptr,
		float nearZ = 0.01f, float farZ = 2000.0f);

	void Render(ID3D11RenderTargetView* rtv,
		const DirectX::XMMATRIX& view,
		const DirectX::XMMATRIX& viewProj,
		int width, int height,
		ID3D11DepthStencilView* depthDSV = nullptr,
		ID3D11ShaderResourceView* gbufferC = nullptr,
		const DirectX::XMFLOAT3& camPos = { 0,0,0 });

	void Release();

	// エミッタ操作
	int  AddEmitter(const EmitterData& e);   // 追加して emitterId を返す（-1=満杯）
	void RemoveEmitter(int id);              // 無効化
	void Emit(int id);                       // ワンショット発火
	void SetEmitterPos(int id, const DirectX::XMFLOAT3& pos);
	void SetEmitterTarget(int id, const DirectX::XMFLOAT3& target);
	EmitterData* GetEmitter(int id);              // 直接いじる用
	const EmitterData* GetEmitter(int id) const;  // 読むだけの用

	void ResetParticles();   // 粒と履歴を全消去（Restart 用）

	// 起動時に seconds 秒ぶん空回しして、最初から定常状態にする（動いている全エミッタ）
	void Prewarm(float seconds, float step = 0.0f);

	// 全エフェクトを止める（シーン切り替え時など）。止めた数を返す
	int StopAllEffects();

	// 容量の使用状況（デバッグ表示用）
	struct Stats
	{
		int activeEmitters = 0;    // 今フレーム計算したエミッタ数
		int trailEmitters = 0;     // トレイルを描いたエミッタ数
		int maxEmitters = 0;
		int droppedEmitters = 0;   // 満杯で作れなかった累計
		int liveEffects = 0;       // 動いているエフェクト数
		int prewarmSteps = 0;      // 直近の Prewarm の刻み数
	};
	Stats GetStats() const;

	void SetTextureSRV(ID3D11ShaderResourceView* srv) { m_externalSRV = srv; }

	// ---- エフェクト（複数レイヤー・バースト・移動パス）----
	bool LoadEffect(const std::string& name, const std::string& path);
	// フォルダ内の全エフェクトをロード（再帰的にサブフォルダも探索）
	int LoadEffectFolder(const std::string& folder, bool recursive = true);
	// 継続再生。ハンドルを返す（-1 = 失敗）
	// ハンドルは (世代 << 16) | 番号。止めた後に同じ番号が再利用されても、古いハンドルは何もしない
	int  SpawnEffect(const std::string& name, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& angle = { 0.0f,0.0f,0.0f });
	// ワンショット。粒が消えたら自動で回収される
	void PlayEffect(const std::string& name, const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT3& angle = {0.0f,0.0f,0.0f});
	// ハンドルのエフェクトの位置を変更
	void SetEffectPos(int handle, const DirectX::XMFLOAT3& pos);
	// ハンドルのエフェクトを停止（粒は残る）
	void StopEffect(int handle);
	// ハンドルエフェクトの位置を基準に、向き・スケールを設定
	void SetEffectTransform(int handle,
		const DirectX::XMFLOAT3& pos,
		const DirectX::XMFLOAT3& forward = { 0, 1, 0 },
		const DirectX::XMFLOAT3& up = { 0, 0, 1 },
		float scale = 1.0f);
	// 向き付きでワンショット再生
	void PlayEffectAt(const std::string& name,
		const DirectX::XMFLOAT3& pos,
		const DirectX::XMFLOAT3& forward,
		const DirectX::XMFLOAT3& up = { 0, 1, 0 },
		float scale = 1.0f);
	// 2点間を飛ぶ（弾道トレイル）。移動は path 機構を流用する
	void PlayBeam(const std::string& name,
		const DirectX::XMFLOAT3& from, const DirectX::XMFLOAT3& to,
		float travelTime = 0.6f);
	// エンティティを囲う円。up が円の法線になる
	int  SpawnRing(const std::string& name,
		const DirectX::XMFLOAT3& center, float radius,
		const DirectX::XMFLOAT3& up = { 0, 1, 0 });

	// 全レイヤーの発生形状の半径を上書き（リングの拡縮に使う）
	void SetEffectRadius(int handle, float radius);

	// 登録済みの種類名を全部返す（デバッグ表示・一覧用）
	std::vector<std::string> GetEffectNames() const;

	static const int kMaxTextures = 8;
	static const int kTrailPoints = 8;   // 1粒あたりの履歴点数（HLSL の TRAIL_POINTS と一致させること）

	// スロットにテクスチャSRVをセット
	void SetTextureSlot(int slot, ID3D11ShaderResourceView* srv);

	void SetRuntime(ShaderRuntime* rt) { m_runtime = rt; }
	// テクスチャスロットに「エフェクト」を割り当てる
	bool SetEffectSlot(int slot, const std::string& effectName);

	void ApplyAssetFromString(const std::string& jsonStr);

	void SetBlendMode(bool additive) { m_additiveMode = additive; }

	void SetExposure(float e) { m_exposure = e; }
	float GetExposure() const { return m_exposure; }

private:

	static const int kMaxEmitters = 256;
	static const int kParticlesPerEmitter = 1024;
	static const int kTotalParticles = kMaxEmitters * kParticlesPerEmitter; // 262144
	static const int kThreadsPerGroup = 256;   // ParticleCS の numthreads と一致
	static_assert(kParticlesPerEmitter % kThreadsPerGroup == 0,
		"kParticlesPerEmitter must be a multiple of kThreadsPerGroup");
	static_assert(sizeof(GPUParticle) == 64, "GPUParticle layout must match HLSL");
	static_assert(sizeof(GPUParticleCB) % 16 == 0, "GPUParticleCB must be 16-byte aligned");
	static_assert(sizeof(GPUDrawCB) % 16 == 0, "GPUDrawCB must be 16-byte aligned");

	static constexpr float kTrailInterval = 1.0f / 30.0f;   // 履歴を刻む間隔

	bool CreateParticleBuffer();
	bool CreateHistoryBuffer();
	bool CreateEmitterBuffer();
	bool CreateConstantBuffers();
	bool CreateShaders();
	bool CreateTrailShader();
	bool CreateBlendState();
	bool CreateRenderStates();
	bool CreateParticleTexture();
	bool CreateListBuffers();

	// 動いているエミッタ（放出中、または粒が残っている）の一覧を作る
	void BuildActiveLists(float dt);
	// 一覧（listSRV、listCount 個）のエミッタだけ CS を回す
	void DispatchSimulation(ID3D11ShaderResourceView* listSRV, int listCount,
		GPUParticleCB cb, ID3D11ShaderResourceView* sceneWorldPos);
	// 共通CBの土台（行列・画面サイズなど）
	GPUParticleCB m_lastCB = {};
	float m_lastTime = 0.0f;
	// 一覧を GPU に送る
	void UploadList(ID3D11Buffer* buffer, const std::vector<uint32_t>& list);
	// タイミング制御（Delay / Duration / Looping / ワンショット回収）
	void UpdateEmitterTiming(float dt);
	// エミッタの移動速度（Inherit Velocity 用）
	void UpdateEmitterVelocity(float dt);
	// 指定エミッタだけ seconds 秒ぶん空回しする
	void PrewarmEmitters(const std::vector<int>& ids, float seconds, float step = 0.0f);

	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// 巨大パーティクルバッファ
	ComPtr<ID3D11Buffer>              m_particleBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_particleUAV;
	ComPtr<ID3D11ShaderResourceView>  m_particleSRV;

	// 履歴バッファ（トレイル）
	ComPtr<ID3D11Buffer>              m_historyBuffer;
	ComPtr<ID3D11UnorderedAccessView> m_historyUAV;
	ComPtr<ID3D11ShaderResourceView>  m_historySRV;
	int   m_trailCursor = 0;
	float m_trailTimer = 0.0f;

	// エミッタバッファ（CPUが毎フレーム更新）
	ComPtr<ID3D11Buffer>             m_emitterBuffer;
	ComPtr<ID3D11ShaderResourceView> m_emitterSRV;
	std::vector<EmitterData>         m_emitters;   // CPU側の実体（kMaxEmitters 個）
	std::vector<DirectX::XMFLOAT3>   m_emitterPrevPos;   // 前フレーム位置（Inherit Velocity 用）

	// 動いているエミッタの一覧（CS・本体描画用 / トレイル描画用）
	ComPtr<ID3D11Buffer>             m_activeListBuffer;
	ComPtr<ID3D11ShaderResourceView> m_activeListSRV;
	ComPtr<ID3D11Buffer>             m_trailListBuffer;
	ComPtr<ID3D11ShaderResourceView> m_trailListSRV;
	ComPtr<ID3D11Buffer>             m_prewarmListBuffer;
	ComPtr<ID3D11ShaderResourceView> m_prewarmListSRV;
	std::vector<uint32_t>            m_activeList;
	std::vector<uint32_t>            m_trailList;
	std::vector<float>               m_aliveTimer;   // 放出を止めてから粒が消えるまでの残り時間
	int m_droppedEmitters = 0;
	int m_lastPrewarmSteps = 0;

	// シェーダ
	ComPtr<ID3D11ComputeShader> m_updateCS;
	ComPtr<ID3D11VertexShader>  m_drawVS;
	ComPtr<ID3D11VertexShader>  m_trailVS;
	ComPtr<ID3D11PixelShader>   m_drawPS;

	// 定数バッファ
	ComPtr<ID3D11Buffer> m_cbParticle;   // GPUParticleCB
	ComPtr<ID3D11Buffer> m_cbDraw;       // GPUDrawCB
	ComPtr<ID3D11Buffer> m_cbTrail;      // GPUTrailCB

	// ステート
	ComPtr<ID3D11BlendState>        m_additiveBlend;
	ComPtr<ID3D11BlendState>        m_alphaBlend;
	ComPtr<ID3D11RasterizerState>   m_rasterState;
	ComPtr<ID3D11DepthStencilState> m_depthState;

	// テクスチャ
	ComPtr<ID3D11ShaderResourceView> m_particleSRVTex;
	ComPtr<ID3D11SamplerState>       m_sampler;
	ID3D11ShaderResourceView* m_externalSRV = nullptr;

	std::map<std::string, ParticleAsset> m_types;

	// ---- エフェクトのランタイム ----
	struct EffectInstance
	{
		bool  active = false;
		bool  oneShot = false;
		uint16_t generation = 0;   // 番号を再利用するたびに増やす
		float elapsed = 0.0f;
		float maxLife = 0.0f;
		std::string typeName;
		DirectX::XMFLOAT3 origin{ 0.0f, 0.0f, 0.0f };
		DirectX::XMFLOAT3 axisX{ 1, 0, 0 };
		DirectX::XMFLOAT3 axisY{ 0, 1, 0 };
		DirectX::XMFLOAT3 axisZ{ 0, 0, 1 };
		float scale = 1.0f;

		bool overridePath = false;
		bool overrideRadius = false;
		float radius = 1.0f;
		float pathTime = 0.0f;;
		DirectX::XMFLOAT3 pathEnd{ 0,0,0 };
		DirectX::XMFLOAT3 angle = { 0.0f, 0.0f, 0.0f };

		struct LayerRt
		{
			int    emitterId = -1;
			size_t nextBurst = 0;
		};
		std::vector<LayerRt> layers;
	};

	std::map<std::string, ParticleEffect> m_effectTypes;
	std::vector<EffectInstance>           m_effectInstances;

	// ハンドル ⇔ 番号
	static int MakeHandle(int index, uint16_t generation)
	{
		return (int)(((uint32_t)generation << 16) | ((uint32_t)index & 0xFFFFu));
	}
	// 有効なハンドルならインスタンスを返す（古い世代・停止済みは nullptr）
	EffectInstance* ResolveHandle(int handle);
	static constexpr int kMaxEffectInstances = 0xFFFF;

	void UpdateEffects(float dt);

	struct EmitterRuntime
	{
		bool  isOneShot = false;   // ワンショットか
		float elapsed = 0.0f;      // 発火からの経過時間
		float lifeMax = 0.0f;      // この時間を超えたら回収

		// Timing（Delay / Duration / Looping）
		float delay = 0.0f;        // 放出開始までの待ち
		float duration = 0.0f;     // 放出し続ける長さ（0 = 無限）
		bool  looping = true;      // duration 経過後に繰り返すか
		bool  started = false;     // delay を過ぎたか
		float cycleTime = 0.0f;    // 現サイクルの経過
	};
	std::vector<EmitterRuntime> m_emitterRuntimes;

	ID3D11ShaderResourceView* m_textureSlots[kMaxTextures] = {};
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_texCircle;  // 丸
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_texStar;    // 星
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_texRing;    // リング

	bool CreateBuiltinTexture(int pattern,
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& outSRV);

	struct EffectSlot
	{
		Microsoft::WRL::ComPtr<ID3D11Texture2D>          tex;
		Microsoft::WRL::ComPtr<ID3D11RenderTargetView>   rtv;
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
		std::string effectName;
		bool active = false;
	};
	EffectSlot m_effectSlots[kMaxTextures];   // テクスチャスロットと対応
	ShaderRuntime* m_runtime = nullptr;
	int m_effectTexSize = 256;

	bool m_additiveMode = true;

	std::vector<std::string> m_emitterTypeNames;

	float m_exposure = 1.0f;
	float m_lastNearZ = 0.01f;
	float m_lastFarZ = 2000.0f;

private:

	GPUParticleSystem();
	~GPUParticleSystem();

public:
	static GPUParticleSystem& Instance() 
	{
		static GPUParticleSystem instance;
		return instance;
	}
};