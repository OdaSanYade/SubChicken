#pragma once

using namespace Microsoft::WRL;

class PhaseShader 
{
public:

	PhaseShader() = default;
	~PhaseShader() = default;

	// シェーダ初期化
	bool Init();

	// 描画
	void Draw();

	// 描画するテクスチャをセット
	void SetPhaseNotice(int phaseIndex,ID3D11ShaderResourceView* _srv,float _time = 3.0f);

private:

	// 構造体バッファ
	struct cbPhase
	{
		float time;
		float progress;
		float phase;
		float padding;

		Math::Vector2 screenSize;
		Math::Vector2 halfSize;

		Math::Vector2 center;
		Math::Vector2 uvRange;
	};

	//テクスチャ
	ComPtr<ID3D11ShaderResourceView> m_srv;
	ComPtr<ID3D11SamplerState>       m_sample;

	//定数バッファ
	ComPtr<ID3D11Buffer> m_cb;

	//シェーダ
	ComPtr<ID3D11VertexShader> m_vs;
	ComPtr<ID3D11PixelShader>  m_ps;

	static constexpr float kBannerAspect = 2.0f;
	bool  m_isPhaseChange = false;
	int   m_phaseIndex = 0;
	float m_progress  = 1.0f;//進捗0～1
	float m_progressSec = 1.0f;  //通知の全体時間
};