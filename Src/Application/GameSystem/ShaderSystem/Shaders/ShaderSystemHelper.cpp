#include "ShaderSystemHelper.h"

using Microsoft::WRL::ComPtr;

namespace ShaderHelper 
{
	// 6枚の画像ファイルからキューブマップを作成する
	bool CreateCubemapFromFiles(const std::array<std::string, 6>& faceFiles,
		ID3D11Device* device,
		ComPtr<ID3D11ShaderResourceView>& outSRV)
	{
		// 6面の順序: +X, -X, +Y, -Y, +Z, -Z （D3D11の規約）
		DirectX::ScratchImage faces[6];
		DirectX::TexMetadata meta{};

		for (int i = 0; i < 6; ++i)
		{
			std::wstring wpath = sjis_to_wide(faceFiles[i]);
			if (FAILED(DirectX::LoadFromWICFile(wpath.c_str(),
				DirectX::WIC_FLAGS_NONE, &meta, faces[i])))
			{
				return false;
			}
		}

		// 6面を1つの配列テクスチャにまとめる
		// 各面の Image を集める
		std::vector<DirectX::Image> images;
		for (int i = 0; i < 6; ++i)
		{
			const DirectX::Image* img = faces[i].GetImage(0, 0, 0);
			images.push_back(*img);
		}

		// キューブマップの metadata
		DirectX::TexMetadata cubeMeta = meta;
		cubeMeta.arraySize = 6;
		cubeMeta.miscFlags |= DirectX::TEX_MISC_TEXTURECUBE;   // ★キューブマップ指定
		cubeMeta.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

		// テクスチャ作成
		ComPtr<ID3D11Resource> res;
		if (FAILED(DirectX::CreateTexture(device,
			images.data(), 6, cubeMeta, &res)))
		{
			return false;
		}

		// キューブマップSRV
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = cubeMeta.format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;   // ★キューブマップビュー
		srvDesc.TextureCube.MipLevels = 1;
		srvDesc.TextureCube.MostDetailedMip = 0;

		if (FAILED(device->CreateShaderResourceView(res.Get(), &srvDesc, &outSRV)))
		{
			return false;
		}

		return true;
	}

	// キューブマップのレンダーターゲットを作成する
	bool CreateRenderTargetCubemap(int size, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11ShaderResourceView>& outSRV,
		std::array<ComPtr<ID3D11RenderTargetView>, 6>& outRTVs)
	{
		// キューブマップテクスチャ（RT + SRV）
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = size;
		desc.Height = size;
		desc.MipLevels = 1;
		desc.ArraySize = 6;                              // ★6面
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;    // HDR（環境光は明るさ範囲が要る）
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;   // ★キューブマップ

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &outTex)))
			return false;

		// SRV（キューブマップとして読む）
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(outTex.Get(), &srvDesc, &outSRV)))
			return false;

		// 各面のRTV（6面それぞれに描くため）
		for (int i = 0; i < 6; ++i)
		{
			D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = desc.Format;
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
			rtvDesc.Texture2DArray.MipSlice = 0;
			rtvDesc.Texture2DArray.FirstArraySlice = i;  
			rtvDesc.Texture2DArray.ArraySize = 1;
			if (FAILED(device->CreateRenderTargetView(outTex.Get(), &rtvDesc, &outRTVs[i])))
				return false;
		}

		return true;
	}

	// ミップ付きRTキューブマップ
	bool CreatePrefilterCubemap(int baseSize, int mipLevels, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11ShaderResourceView>& outSRV,
		std::vector<std::array<ComPtr<ID3D11RenderTargetView>, 6>>& outRTVs)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = baseSize;
		desc.Height = baseSize;
		desc.MipLevels = mipLevels;
		desc.ArraySize = 6;
		desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &outTex)))
			return false;

		// SRV（全ミップ）
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
		srvDesc.TextureCube.MipLevels = mipLevels;
		if (FAILED(device->CreateShaderResourceView(outTex.Get(), &srvDesc, &outSRV)))
			return false;

		// 各ミップ × 各面のRTV
		outRTVs.resize(mipLevels);
		for (int mip = 0; mip < mipLevels; ++mip)
		{
			for (int face = 0; face < 6; ++face)
			{
				D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
				rtvDesc.Format = desc.Format;
				rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
				rtvDesc.Texture2DArray.MipSlice = mip;
				rtvDesc.Texture2DArray.FirstArraySlice = face;
				rtvDesc.Texture2DArray.ArraySize = 1;
				if (FAILED(device->CreateRenderTargetView(outTex.Get(), &rtvDesc, &outRTVs[mip][face])))
					return false;
			}
		}
		return true;
	}

	bool CreateBRDFLut(int size, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11ShaderResourceView>& outSRV,
		ComPtr<ID3D11RenderTargetView>& outRTV)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = size;
		desc.Height = size;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R16G16_FLOAT;  
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &outTex)))
			return false;
		if (FAILED(device->CreateShaderResourceView(outTex.Get(), nullptr, &outSRV)))
			return false;
		if (FAILED(device->CreateRenderTargetView(outTex.Get(), nullptr, &outRTV)))
			return false;
		return true;
	}

	bool CreateShadowMap(int size, ID3D11Device* device,
		ComPtr<ID3D11Texture2D>& outTex,
		ComPtr<ID3D11DepthStencilView>& outDSV,
		ComPtr<ID3D11ShaderResourceView>& outSRV)
	{
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = size;
		desc.Height = size;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R24G8_TYPELESS;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

		if (FAILED(device->CreateTexture2D(&desc, nullptr, &outTex)))
			return false;

		// DSV（深度を書く）
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
		dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;   // 深度として書く
		dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		if (FAILED(device->CreateDepthStencilView(outTex.Get(), &dsvDesc, &outDSV)))
			return false;

		// SRV（深度を読む）
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;   // 深度として読む
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		if (FAILED(device->CreateShaderResourceView(outTex.Get(), &srvDesc, &outSRV)))
			return false;

		return true;
	}

	DirectX::XMMATRIX MakeLightViewProj(const DirectX::XMFLOAT3& lightDir,
		const DirectX::XMFLOAT3& sceneCenter, float sceneRadius)
	{
		using namespace DirectX;
		XMVECTOR dir = XMVector3Normalize(XMLoadFloat3(&lightDir));
		XMVECTOR center = XMLoadFloat3(&sceneCenter);

		// 光源の位置 = シーン中心から、光の逆方向に離れる
		XMVECTOR lightPos = center + dir * sceneRadius * 2.0f;

		// ビュー行列（光源から中心を見る）
		XMVECTOR up = XMVectorSet(0, 1, 0, 0);
		// dir が真上・真下だと up と平行になるので調整
		if (fabsf(XMVectorGetX(XMVector3Dot(dir, up))) > 0.99f)
			up = XMVectorSet(1, 0, 0, 0);
		XMMATRIX view = XMMatrixLookAtLH(lightPos, center, up);

		// 平行投影（シーンを覆う範囲）
		XMMATRIX proj = XMMatrixOrthographicLH(
			sceneRadius * 2.0f, sceneRadius * 2.0f,   // 幅・高さ
			0.1f, sceneRadius * 4.0f);                 // near, far

		return view * proj;
	}

	ComPtr<ID3D11Texture2D> irradianceTex;
	ComPtr<ID3D11ShaderResourceView> irradianceSRV;
	ComPtr<ID3D11ShaderResourceView> cubeSRV;
	std::array<ComPtr<ID3D11RenderTargetView>, 6> irradianceRTVs;

	ComPtr<ID3D11Texture2D> prefilterTex;
	ComPtr<ID3D11ShaderResourceView> prefilterSRV;
	std::vector<std::array<ComPtr<ID3D11RenderTargetView>, 6>> prefilterRTVs;

	ComPtr<ID3D11Texture2D> brdfLutTex;
	ComPtr<ID3D11ShaderResourceView> brdfLutSRV;
	ComPtr<ID3D11RenderTargetView> brdfLutRTV;

	ComPtr<ID3D11Texture2D> shadowMapTex;
	ComPtr<ID3D11DepthStencilView> shadowMapDSV;
	ComPtr<ID3D11ShaderResourceView> shadowMapSRV;

	ComPtr<ID3D11Texture2D>          cascadeTex[kCascadeCount];
	ComPtr<ID3D11DepthStencilView>   cascadeDSV[kCascadeCount];
	ComPtr<ID3D11ShaderResourceView> cascadeSRV[kCascadeCount];

	void ReleaseAll()
	{
		irradianceTex.Reset();
		irradianceSRV.Reset();
		cubeSRV.Reset();
		for (auto& r : irradianceRTVs) r.Reset();

		prefilterTex.Reset();
		prefilterSRV.Reset();
		prefilterRTVs.clear();

		brdfLutTex.Reset();
		brdfLutSRV.Reset();
		brdfLutRTV.Reset();

		shadowMapTex.Reset();
		shadowMapDSV.Reset();
		shadowMapSRV.Reset();

		for (int i = 0; i < kCascadeCount; ++i)
		{
			cascadeTex[i].Reset();
			cascadeDSV[i].Reset();
			cascadeSRV[i].Reset();
		}
	}
}