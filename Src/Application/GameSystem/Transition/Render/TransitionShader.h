#pragma once

class TransitionShader
{
public:

	bool Init();
	void Release();

	bool IsValid() const { return m_initialized; }

	void Draw(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* tex, float progress = 1.0f, float switchPoint = 0.5f);

private:

	template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// 画面コピー用テクスチャをsrcと同じサイズ・フォーマットで用意する（変化時のみ再生成）
	bool UpdateCopyTexture(ID3D11Device* dev, ID3D11Texture2D* src);

	struct cbTransition
	{
		float time = 0.0f;
		float progress = 0.0f;
		float switchPoint = 0.5f;
		float padding = 0.0f;
	};
	static_assert(sizeof(cbTransition) % 16 == 0, "cbTransition must be 16-byte aligned");

	ComPtr<ID3D11VertexShader>       m_vs;
	ComPtr<ID3D11PixelShader>        m_ps;
	ComPtr<ID3D11Buffer>             m_cbBuffer;
	ComPtr<ID3D11SamplerState>       m_samp;
	ComPtr<ID3D11BlendState>         m_blend;
	ComPtr<ID3D11DepthStencilState>  m_depth;
	ComPtr<ID3D11RasterizerState>    m_raster;

	// 描画直前の画面のコピー（シェーダの g_tex）
	ComPtr<ID3D11Texture2D>          m_copyTex;
	ComPtr<ID3D11ShaderResourceView> m_copySRV;

	bool m_initialized = false;
};