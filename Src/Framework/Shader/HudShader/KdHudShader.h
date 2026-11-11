#pragma once

class KdHudShader
{
public:

	// 種別
	enum Kind : uint32_t
	{
		Lock = 0,		// ロックオンの羽根
		Gauge = 1,		// 敵の頭上ゲージ
		Warn = 2,		// 攻撃予兆の収束リング
		Arrow = 3,		// 画面端の予兆インジケータ
		Ring = 4,		// カウンター受付の円弧
		Wave = 5,		// 鳴き声の波紋
		Health = 6,		// プレイヤーの生命線
		Count = 7,		// 敵の残数(卵)
		Compass = 8,	// 画面外の敵の方位
		Combo = 9,		// コンボ
		Focus = 10,		// ジャスト回避の集中線
		Damage = 11,	// ダメージの数値
		Guide = 12,		// 操作ガイド
		Pips = 13,		// 点で数を示す汎用表示
		Score = 14,		// 撃破数と経過時間
	};

	// 操作ガイドのボタン形状
	enum BtnShape : int
	{
		BtnFace = 0,		// Xboxのフェイスボタン(A/B/X/Y)
		BtnShoulder = 1,	// LB/RB
		BtnTrigger = 2,		// LT/RT
		BtnDPad = 3,		// 十字キー
		BtnMenu = 4,		// Start/Back
		BtnKeycap = 5,		// キーボードのキーキャップ
		BtnMouseL = 6,		// マウス左ボタン(本体の形 + 名称)
		BtnMouseR = 7,		// マウス右ボタン
		BtnMouseM = 8,		// マウス中ボタン(ホイール)
		BtnWordKey = 9,		// 語で書くキー(SHIFT/SPACEなど)
	};

	// 語で書くキーの種類
	enum KeyWord : int
	{
		WordShift = 0,
		WordCtrl = 1,
		WordAlt = 2,
		WordSpace = 3,
		WordEnter = 4,
		WordTab = 5,
		WordEsc = 6,
	};

	// 操作ガイドの動作アイコン
	enum ActionIcon : int
	{
		ActAttack = 0,
		ActHeavy = 1,
		ActDodge = 2,
		ActCry = 3,
	};

	// クアッド拡張
	static constexpr float kHealthPadX = 0.75f;
	static constexpr float kHealthPadY = 0.45f;

	static constexpr float kEnemyBarExpandX = 1.45f;
	static constexpr float kEnemyBarExpandY = 2.60f;

	// HLSL側の HudInstance と1バイトも違わないこと
	struct Instance
	{
		Math::Vector2 center;		//  8 : 画面ピクセル座標(左上原点)
		Math::Vector2 halfSize;		// 16 : 半径相当(ピクセル)
		Math::Vector4 color;		// 32 : 主色
		Math::Vector4 subColor;		// 48 : 副色
		Math::Vector4 params;		// 64 : 種別ごとの4値
		Math::Vector4 extra;		// 80 : 種別ごとの追加4値
		float tilt = 0.0f;			// 84 : クアッドの傾き(rad)
		uint32_t kind = Lock;		// 88
		uint32_t pad[2] = {};		// 96
	};
	static_assert(sizeof(Instance) == 96, "HLSLのHudInstanceとstrideを合わせること");

	// _capacity : 同時に出せるマーカー数の初期値
	bool Init(int _capacity = 256);
	void Release();

	~KdHudShader() { Release(); }

	// 全マーカーを1ドローコールで描く
	void Draw(const std::vector<Instance>& _instances);

	// 画面サイズ
	static Math::Vector2 GetViewportSize();

private:

	bool CreateInstanceBuffer(int _capacity);

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11PixelShader* m_PS = nullptr;

	ID3D11Buffer* m_instanceBuf = nullptr;
	ID3D11ShaderResourceView* m_instanceSRV = nullptr;
	int m_capacity = 0;

	// 実時間で進む
	float m_time = 0.0f;

	struct cbHud
	{
		Math::Vector2 screenSize;
		float time = 0.0f;
		float pad = 0.0f;
	};
	KdConstantBuffer<cbHud> m_cb0;

	// 際限なく確保しないための上限
	static constexpr int kMaxCapacity = 4096;
};