#pragma once

namespace ShaderHelper 
{
	// ヘッダで using namespace を広げないよう、この名前空間の中だけで使う別名
	template<typename T> using ComPtr = Microsoft::WRL::ComPtr<T>;

	// キューブマップを6枚の画像ファイルから作成する
	bool CreateCubemapFromFiles(const std::array<std::string, 6>& faceFiles,
		ID3D11Device* device,
		ComPtr<ID3D11ShaderResourceView>& outSRV);

	// キューブマップのレンダーターゲットを作成する
	bool CreateRenderTargetCubemap(int size, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11ShaderResourceView>& outSRV,
		std::array<ComPtr<ID3D11RenderTargetView>, 6>& outRTVs);

	// ミップ付きRTキューブマップ（プレフィルタ用）を作成する
	bool CreatePrefilterCubemap(int baseSize, int mipLevels, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11ShaderResourceView>& outSRV,
		std::vector<std::array<ComPtr<ID3D11RenderTargetView>, 6>>& outRTVs);

	// BRDF LUTを作成する
	bool CreateBRDFLut(int size, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11ShaderResourceView>& outSRV,
		ComPtr<ID3D11RenderTargetView>& outRTV);

	//	シャドウマップを作成する
	bool CreateShadowMap(int size, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11DepthStencilView>& outDSV,
		ComPtr<ID3D11ShaderResourceView>& outSRV);

	//　ライト行列を作成
	DirectX::XMMATRIX MakeLightViewProj(const DirectX::XMFLOAT3& lightDir,
		const DirectX::XMFLOAT3& sceneCenter, float sceneRadius);

	// キューブマップの6面の画像ファイルパス
	static std::array<std::string, 6> faces = {
		"Asset/Cubemap/posX.png",   // +X 赤
		"Asset/Cubemap/negX.png",   // -X シアン
		"Asset/Cubemap/posY.png",   // +Y 緑
		"Asset/Cubemap/negY.png",   // -Y マゼンタ
		"Asset/Cubemap/posZ.png",   // +Z 青
		"Asset/Cubemap/negZ.png",   // -Z 黄
	};

	struct FaceView { DirectX::XMFLOAT3 forward, up; };
	static const FaceView kFaceViews[6] = {
		{ { 1, 0, 0}, {0, 1, 0} },   // +X
		{ {-1, 0, 0}, {0, 1, 0} },   // -X
		{ { 0, 1, 0}, {0, 0,-1} },   // +Y
		{ { 0,-1, 0}, {0, 0, 1} },   // -Y
		{ { 0, 0, 1}, {0, 1, 0} },   // +Z
		{ { 0, 0,-1}, {0, 1, 0} },   // -Z
	};

	extern ComPtr<ID3D11Texture2D> irradianceTex;
	extern ComPtr<ID3D11ShaderResourceView> irradianceSRV;
	extern ComPtr<ID3D11ShaderResourceView> cubeSRV;
	extern std::array<ComPtr<ID3D11RenderTargetView>, 6> irradianceRTVs;

	extern ComPtr<ID3D11Texture2D> prefilterTex;
	extern ComPtr<ID3D11ShaderResourceView> prefilterSRV;
	extern std::vector<std::array<ComPtr<ID3D11RenderTargetView>, 6>> prefilterRTVs;

	extern ComPtr<ID3D11Texture2D> brdfLutTex;
	extern ComPtr<ID3D11ShaderResourceView> brdfLutSRV;
	extern ComPtr<ID3D11RenderTargetView> brdfLutRTV;

	extern ComPtr<ID3D11Texture2D> shadowMapTex;
	extern ComPtr<ID3D11DepthStencilView> shadowMapDSV;
	extern ComPtr<ID3D11ShaderResourceView> shadowMapSRV;

	// CSM: カスケード3段
	static const int kCascadeCount = 3;
	extern ComPtr<ID3D11Texture2D>          cascadeTex[kCascadeCount];
	extern ComPtr<ID3D11DepthStencilView>   cascadeDSV[kCascadeCount];
	extern ComPtr<ID3D11ShaderResourceView> cascadeSRV[kCascadeCount];

	// 上のグローバルをすべて解放する。デバイスより先に呼ぶこと（ShaderRender の破棄時に呼ぶ）
	void ReleaseAll();
}