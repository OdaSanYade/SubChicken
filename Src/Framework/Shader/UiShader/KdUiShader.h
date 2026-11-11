#pragma once

class KdUiShader
{
public:

	// 種別
	enum Kind : uint32_t
	{
		Button = 0,			// 細長い平行四辺形。輪郭が荒れる。タイトル向け。
		ButtonRound = 1,	// 角丸。設定画面などの落ち着いた場面向け。
		ButtonTab = 2,		// 上辺だけ角を残した札。並べて使う。
		ButtonPlate = 3,	// 帳票の欄。左端に帯、下に罫線。リザルト向け。
		Veil = 4,			// 暗幕(画面を沈める下地)
		Panel = 5,			// 枠(項目をまとめる区画)
		Label = 6,			// 見出しの語
		Hint = 7,			// 操作ヒント(アイコン + 短い語)
	};

	// 語の最大文字数。HLSL側の UI_WORD_MAX と一致させること。
	static constexpr int kWordMax = 6;

	// ボタンの先頭アイコン。HLSL側の UI_ICON_* と一致させること。
	// 決定に使うボタンを添えて、「押せば進む」を言葉なしで伝える。
	enum BtnIcon : int
	{
		IconNone = 0,
		IconFace = 1,		// フェイスボタン(丸に文字)
		IconEnter = 2,		// Enter(キーキャップ + 矢印)
		IconSpace = 3,		// Space(横長のキーキャップ)
		IconKeycap = 4,		// 任意の1文字のキーキャップ
		IconShoulder = 5,	// LB/RB
		IconTrigger = 6,	// LT/RT
	};

	// HLSL側の HudInstance と1バイトも違わないこと。
	// KdHudShader::Instance と同じ形。共有の cbHud / VS を使うため。
	struct Instance
	{
		Math::Vector2 center;		//  8 : 画面ピクセル座標(左上原点)
		Math::Vector2 halfSize;		// 16 : 半径相当(ピクセル)
		Math::Vector4 color;		// 32 : 主色
		Math::Vector4 subColor;		// 48 : 副色
		Math::Vector4 params;		// 64 : 種別ごとの4値
		Math::Vector4 extra;		// 80 : 種別ごとの追加4値
		float tilt = 0.0f;			// 84 : 部品によって意味が変わる(ボタンは出現量)
		uint32_t kind = Button;		// 88
		uint32_t pad[2] = {};		// 96
	};
	static_assert(sizeof(Instance) == 96, "HLSLのHudInstanceとstrideを合わせること");


	// 記号の詰め方。
	static constexpr int kIconStep = 32;

	static int PackIcon(int _shape, int _code)
	{
		if (_shape == IconNone) return 0;
		return _shape * kIconStep + (std::clamp(_code, -1, 25) + 1);
	}

	// 先頭アイコンを書き込む。_code は 0=A..25=Z、負で文字なし。
	static void SetButtonIcon(Instance& _inst, int _shape, int _code = -1)
	{
		const float a = std::min(_inst.subColor.w, 0.999f);
		_inst.subColor.w = static_cast<float>(PackIcon(_shape, _code)) + a;
	}

	// 操作ヒントを組む。アイコンと短い語を並べる。
	static void SetHint(Instance& _inst, int _shape, int _code,
		const char* _word, float _appear)
	{
		SetWord(_inst, _word);
		_inst.extra.z = static_cast<float>(PackIcon(_shape, _code));
		_inst.extra.w = std::clamp(_appear, 0.0f, 1.0f);
		_inst.tilt = 0.0f;
	}

	
	//------------------------------------------
	// 語の変換
	//------------------------------------------
	static void EncodeWord(const char* _s, float* _out)
	{
		for (int i = 0; i < kWordMax; ++i) _out[i] = -1.0f;
		if (!_s) return;

		for (int i = 0; i < kWordMax && _s[i] != '\0'; ++i)
		{
			const char c = _s[i];
			if (c >= 'A' && c <= 'Z') _out[i] = static_cast<float>(c - 'A');
		}
	}

	// 語をインスタンスへ書き込む。
	// Params.xyzw = 1〜4文字目、Extra.xy = 5〜6文字目。
	static void SetWord(Instance& _inst, const char* _s)
	{
		float c[kWordMax];
		EncodeWord(_s, c);

		_inst.params = { c[0], c[1], c[2], c[3] };
		_inst.extra.x = c[4];
		_inst.extra.y = c[5];
	}

	// 先頭アイコンを書き込む。
	static void SetButtonIcon(Instance& _inst, int _icon)
	{
		const float a = std::min(_inst.subColor.w, 0.999f);
		_inst.subColor.w = static_cast<float>(_icon) + a;
	}

	// ボタンの状態を書き込む。
	static void SetButtonState(Instance& _inst, bool _focus, float _press,
		float _appear, float _ghost = 0.0f)
	{
		if (_focus)
			_inst.extra.z = 2.0f + std::clamp(_press, 0.0f, 1.0f);
		else
			_inst.extra.z = -std::clamp(_ghost, 0.0f, 1.0f);

		_inst.extra.w = std::clamp(_appear, 0.0f, 1.0f);
		_inst.tilt = 0.0f;
	}

	// 操作ヒントを組む。アイコンと短い語を並べる。
	static void SetHint(Instance& _inst, int _icon, const char* _word, float _appear)
	{
		SetWord(_inst, _word);
		_inst.extra.z = static_cast<float>(_icon);
		_inst.extra.w = std::clamp(_appear, 0.0f, 1.0f);
		_inst.tilt = 0.0f;
	}

	//------------------------------------------
	// 描画
	//------------------------------------------
	bool Init(int _capacity = 32);
	void Release();

	~KdUiShader() { Release(); }

	void Draw(const std::vector<Instance>& _instances);

private:

	bool CreateInstanceBuffer(int _capacity);

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11PixelShader* m_PS = nullptr;

	ID3D11Buffer* m_instanceBuf = nullptr;
	ID3D11ShaderResourceView* m_instanceSRV = nullptr;
	int m_capacity = 0;

	// 実時間で進む。メニューは戦闘のスローを持ち込まない。
	float m_time = 0.0f;

	struct cbHud
	{
		Math::Vector2 screenSize;
		float time = 0.0f;
		float pad = 0.0f;
	};
	KdConstantBuffer<cbHud> m_cb0;

	// メニューは要素が少ない。戦闘HUDほどの余裕は要らない。
	static constexpr int kMaxCapacity = 128;
};