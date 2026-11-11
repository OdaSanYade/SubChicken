#pragma once

using namespace Microsoft::WRL;

class KdTitleShader
{
public:

	// 見た目の調整値。毎フレーム渡す。
	struct Params
	{
		Math::Vector2 center;		// 画面ピクセル座標(左上原点)
		Math::Vector2 halfSize;		// 半サイズ(ピクセル)

		Math::Vector4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		float appear = 1.0f;		// 登場(0→1)

		//---- 常時 ----
		float heat = 1.0f;			// 陽炎。上ほど強く縦に歪む。
		float sheen = 1.0f;			// 光の走り(6秒周期)
		float edgeWave = 1.0f;		// 縁の揺らぎ
		float dust = 1.0f;			// 砂粒

		// 呼吸。既定は0。陽炎と重なると酔うので、使うときだけ入れる。
		float breath = 0.0f;

		//---- 一時的 ----
		float shake = 0.0f;			// 選択が動いた時などの震え
		float dissolve = 0.0f;		// 剥がれて散る(決定時)
	};

	bool Init();
	void Release();

	~KdTitleShader() { Release(); }

	// _tex : ロゴのテクスチャ。呼び出し側で保持すること。
	void Draw(const KdTexture& _tex, const Params& _p);

private:

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11PixelShader* m_PS = nullptr;

	ComPtr<ID3D11SamplerState> m_sample;

	// 実時間で進む。タイトルは止める理由が無い。
	float m_time = 0.0f;

	// HLSL側の cbTitle と1バイトも違わないこと。
	struct cbTitle
	{
		Math::Vector2 center;		//  8
		Math::Vector2 halfSize;		// 16

		Math::Vector2 screenSize;	// 24
		float time = 0.0f;			// 28
		float appear = 1.0f;		// 32

		Math::Vector4 tint;			// 48

		float heat = 1.0f;			// 52
		float sheen = 1.0f;			// 56
		float edgeWave = 1.0f;		// 60
		float dust = 1.0f;			// 64

		float breath = 0.0f;		// 68
		float shake = 0.0f;			// 72
		float dissolve = 0.0f;		// 76
		float pad = 0.0f;			// 80
	};
	static_assert(sizeof(cbTitle) == 80, "HLSLのcbTitleと合わせること");

	KdConstantBuffer<cbTitle> m_cb0;
};