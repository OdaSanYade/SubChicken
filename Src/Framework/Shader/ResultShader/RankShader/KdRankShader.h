#pragma once

// ===========================================================
//
// ランクの絵 シェーダ
// 
// ===========================================================
class KdRankShader
{
public:

	struct Params
	{
		Math::Vector2 center;		// 画面ピクセル座標(左上原点)
		Math::Vector2 halfSize;		// 半サイズ(ピクセル)

		Math::Vector4 tint = { 1.0f, 1.0f, 1.0f, 1.0f };

		// 孵化の進み(0→1)。殻と同じ値。
		float hatch = 1.0f;

		// 0=D 1=C 2=B 3=A 4=S
		int rank = 0;
	};

	bool Init();
	void Release();

	~KdRankShader() { Release(); }

	// _tex : ランクのアイコン。呼び出し側で保持すること。
	void Draw(const KdTexture& _tex, const Params& _p);

private:

	ID3D11VertexShader* m_VS = nullptr;
	ID3D11PixelShader* m_PS = nullptr;
	ID3D11SamplerState* m_sample = nullptr;

	// 実時間で進む。リザルトは戦闘のスローを持ち込まない。
	float m_time = 0.0f;

	// HLSL側の cbRank と1バイトも違わないこと。
	struct cbRank
	{
		Math::Vector2 center;		//  8
		Math::Vector2 halfSize;		// 16

		Math::Vector2 screenSize;	// 24
		float time = 0.0f;			// 28
		float hatch = 1.0f;			// 32

		Math::Vector4 tint;			// 48

		float rank = 0.0f;			// 52
		float pad0 = 0.0f;			// 56
		float pad1 = 0.0f;			// 60
		float pad2 = 0.0f;			// 64
	};
	static_assert(sizeof(cbRank) == 64, "HLSLのcbRankと合わせること");

	KdConstantBuffer<cbRank> m_cb0;
};