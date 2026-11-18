#pragma once

class KdCreditShader
{
public:

	struct BgParams
	{
		float appear = 1.0f;	// 登場(0→1)。閉じる時は 1→0
		float scroll = 0.0f;	// 本文のスクロール量(基準解像度px)。星の視差に使う
	};

	// 1文字ぶん。HLSL側の CreditTextInst と1バイトも違わないこと。
	struct TextInst
	{
		Math::Vector4 rect;		// x(中央から), y(上端), w, h : 基準解像度px
		Math::Vector4 uv;		// u0, v0, u1, v1
		Math::Vector4 color;
		Math::Vector4 params;	// x=縁取りの太さ(距離場の値) y=光の強さ z,w=未使用
	};
	static_assert(sizeof(TextInst) == 64, "HLSLのCreditTextInstと合わせること");

	struct TextParams
	{
		ID3D11ShaderResourceView* atlas = nullptr;
		int   atlasW = 0;
		int   atlasH = 0;
		float shadowPx = 4.0f;		// 影のずらし量(アトラスのピクセル)

		float appear = 1.0f;
		float scroll = 0.0f;		// 基準解像度px
		float uiScale = 1.0f;
		Math::Vector2 origin;		// 画面px。x=中央、y=スクロール0での本文の上端
		float fade = 0.12f;			// 画面上下で薄れる幅(画面高に対する比)

		// 描く範囲。画面に入る文字だけを渡す。
		UINT first = 0;
		UINT count = 0;
	};

	KdCreditShader() = default;
	~KdCreditShader() { Release(); }

	// 生のポインタを持つので複製させない
	KdCreditShader(const KdCreditShader&) = delete;
	KdCreditShader& operator=(const KdCreditShader&) = delete;

	bool Init();
	void Release();

	// 時間を進めるのはこちらだけ。本文は同じフレームの時間を使う。
	void DrawBackground(const BgParams& _p);

	// 文字の並びを GPU に置く。組版をやり直した時だけ呼ぶ。
	bool SetTextLines(const std::vector<TextInst>& _lines);
	UINT GetTextLineCount() const { return m_lineCount; }

	void DrawLines(const TextParams& _p);

private:

	void ReleaseLines();

	ID3D11VertexShader* m_VS = nullptr;		// 背景(全画面)
	ID3D11PixelShader* m_bgPS = nullptr;

	ID3D11VertexShader* m_textVS = nullptr;	// 本文
	ID3D11PixelShader* m_textPS = nullptr;

	Microsoft::WRL::ComPtr<ID3D11SamplerState> m_sample;

	Microsoft::WRL::ComPtr<ID3D11Buffer> m_lineBuf;
	Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_lineSRV;
	UINT m_lineCount = 0;

	// 実時間で進む。
	float m_time = 0.0f;

	// HLSL側の cbCredit と1バイトも違わないこと。
	struct cbCredit
	{
		Math::Vector2 screenSize;	//  8
		float time = 0.0f;			// 12
		float appear = 1.0f;		// 16

		float scroll = 0.0f;		// 20
		float uiScale = 1.0f;		// 24
		Math::Vector2 origin;		// 32

		Math::Vector2 shadowUV;		// 40
		uint32_t instBase = 0;		// 44
		float fade = 0.12f;			// 48
	};
	static_assert(sizeof(cbCredit) == 48, "HLSLのcbCreditと合わせること");

	KdConstantBuffer<cbCredit> m_cb0;
};
