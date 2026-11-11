#pragma once

// ===========================================================
//
// リザルト画面
// 
// ===========================================================
class KdResultShader
{
public:

	// 種別
	enum Kind : uint32_t
	{
		Headline = 0,	// 見出し(CLEAR / THE CHICKEN DIED)
		Stat = 1,		// 成績の1行(項目名 + 値)
		Rank = 2,		// ランクの大文字
	};

	// 成績の種類。HLSL側の RES_STAT_* と一致させること。
	enum StatKind : int
	{
		StatCombo = 0,
		StatTime = 1,
		StatLife = 2,
	};

	struct Instance
	{
		Math::Vector2 center;		//  8 : 画面ピクセル座標(左上原点)
		Math::Vector2 halfSize;		// 16 : 半径相当(ピクセル)
		Math::Vector4 color;		// 32 : 主色
		Math::Vector4 subColor;		// 48 : 副色
		Math::Vector4 params;		// 64 : 種別ごとの4値
		Math::Vector4 extra;		// 80 : 種別ごとの追加4値
		float tilt = 0.0f;			// 84 : クアッドの傾き(rad)
		uint32_t kind = 4;		// 88
		uint32_t pad[2] = {};		// 96
	};
	static_assert(sizeof(Instance) == 96, "HLSLのHudInstanceとstrideを合わせること");

	bool Init(int _capacity = 64);
	void Release();

	~KdResultShader() { Release(); }

	void Draw(const std::vector<Instance>& _instances);

private:

	bool CreateInstanceBuffer(int _capacity);

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11PixelShader* m_PS = nullptr;

	ID3D11Buffer* m_instanceBuf = nullptr;
	ID3D11ShaderResourceView* m_instanceSRV = nullptr;
	int m_capacity = 0;

	// 実時間で進む。リザルトは戦闘のスローを持ち込まない。
	float m_time = 0.0f;

	struct cbHud
	{
		Math::Vector2 screenSize;
		float time = 0.0f;
		float pad = 0.0f;
	};
	KdConstantBuffer<cbHud> m_cb0;

	// リザルトは要素が少ない。戦闘HUDほどの余裕は要らない。
	static constexpr int kMaxCapacity = 256;
};