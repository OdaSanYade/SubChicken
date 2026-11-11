#include "Framework/KdFramework.h"

#include "KdTexture.h"
#include "KdBinaryCacheUtil.h"
#include <filesystem>

namespace
{
	//----------------------------------------------------------
	// DDSキャッシュ機能の有効/無効
	//----------------------------------------------------------
	// true  : .kdtcache.dds(またはkUseDedicatedDdsCacheFolder時は.ddscache内)を
	//         生成・利用する(既定)
	// false : DDSキャッシュ機能自体を無効化する。既存のキャッシュファイルが
	//         あっても読みに行かず、新規生成もしない
	//         (従来通り毎回WICデコード+ミップ生成を行う)
	constexpr bool kEnableDdsCache = true;

	//----------------------------------------------------------
	// DDSキャッシュの保存場所設定
	//----------------------------------------------------------
	// true  : 元ファイルと同じフォルダの下に専用サブフォルダ(kDdsCacheFolderName)を
	//         作り、その中へキャッシュをまとめる
	//         例) Asset/Textures/Player/.ddscache/Hair.png.dds
	// false : 元ファイルと同じ場所へ "<元ファイル名>.kdtcache.dds" として保存する(既定)
	//         例) Asset/Textures/Player/Hair.png.kdtcache.dds
	constexpr bool kUseDedicatedDdsCacheFolder = false;

	// kUseDedicatedDdsCacheFolder = true の時に使うサブフォルダ名
	constexpr const char* kDdsCacheFolderName = ".ddscache";

	// 画像の最高解像度ミップを実際に走査し、透明度が使われているかを判定する。
	// PNG等はアルファチャンネルを持っていても実際は全ピクセル不透明(255)なだけの
	// ケースが多く、そのままだとBC3(8bit/pixel)を選んでしまい無駄に容量を食う。
	// 実際に使われている場合だけBC3にし、それ以外はBC1(4bit/pixel)にする
	bool HasMeaningfulAlpha(const DirectX::Image& baseImage)
	{
		bool hasAlpha = false;

		DirectX::EvaluateImage(baseImage,
			[&](const DirectX::XMVECTOR* pixels, size_t width, size_t)
			{
				if (hasAlpha) { return; }	// 既に見つかっていれば以降の行は見ない

				for (size_t x = 0; x < width; ++x)
				{
					if (DirectX::XMVectorGetW(pixels[x]) < 0.999f)
					{
						hasAlpha = true;
						return;
					}
				}
			});

		return hasAlpha;
	}
}


// 2D画像(resource)リソースから、最適なビューを作成する
// ・resource		… 2D画像リソース
// ・ppSRV			… 作成されたShaderResourceViewを受け取るための変数のアドレス
// ・ppRTV			… 作成されたRenderTargetViewを受け取るための変数のアドレス
// ・ppDSV			… 作成されたDepthStencilViewを受け取るための変数のアドレス
static bool KdCreateViewsFromTexture2D(ID3D11Texture2D* resource, ID3D11ShaderResourceView** ppSRV, ID3D11RenderTargetView** ppRTV, ID3D11DepthStencilView** ppDSV)
{
	// リソースが無い
	if (resource == nullptr)return false;

	// テクスチャ本体の情報取得
	D3D11_TEXTURE2D_DESC desc;
	resource->GetDesc(&desc);

	//===========================================================
	//
	// RenderTargetViewを作成する
	//
	//===========================================================

	// レンダーターゲットフラグがついてる時のみ
	if (ppRTV && desc.BindFlags & D3D11_BIND_RENDER_TARGET)
	{
		// 作成するビューの設定
		D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = desc.Format;	// Format
		// 単品のテクスチャ(通常テクスチャ)の場合
		if (desc.ArraySize == 1) {
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;			// 単品テクスチャ
		}
		// テクスチャ配列の場合
		else {
			rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;		// テクスチャ配列
			rtvDesc.Texture2DArray.ArraySize = desc.ArraySize;			// 要素数
			rtvDesc.Texture2DArray.FirstArraySlice = 0;
			rtvDesc.Texture2DArray.MipSlice = 0;
		}

		// レンダーターゲットビュー作成
		HRESULT hr = KdDirect3D::Instance().WorkDev()->CreateRenderTargetView(resource, &rtvDesc, ppRTV);
		if (FAILED(hr))
		{
			assert(0 && "RenderTargetViewの作成に失敗");
			return false;
		}
	}

	//===========================================================
	//
	// ShaderResourceViewの情報を作成する
	//
	//===========================================================
	// シェーダリソースビューフラグがついてる時のみ
	if (ppSRV && desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)
	{
		// 作成するビューの設定
		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

		// テクスチャがZバッファの場合は、最適なフォーマットにする
		if (desc.BindFlags & D3D11_BIND_DEPTH_STENCIL)
		{
			switch (desc.Format) {
				// 16ビット
			case DXGI_FORMAT_R16_TYPELESS:
				srvDesc.Format = DXGI_FORMAT_R16_UNORM;
				break;
				// 32ビット
			case DXGI_FORMAT_R32_TYPELESS:
				srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
				break;
				// 24ビット(Zバッファ) + 8ビット(ステンシルバッファ) 
			case DXGI_FORMAT_R24G8_TYPELESS:
				srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
				break;
			default:
				assert(0 && "[ShaderResource] 対応していないフォーマットです");
				break;
			}
		}
		// Zバッファでない場合は、そのまま同じフォーマットを使用
		else
		{
			srvDesc.Format = desc.Format;
		}

		// 単品のテクスチャ(通常テクスチャ)の場合
		if (desc.ArraySize == 1)
		{
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = desc.MipLevels;
			if (srvDesc.Texture2D.MipLevels <= 0)srvDesc.Texture2D.MipLevels = (UINT)-1;
		}
		// テクスチャ配列の場合
		else {
			// さらにキューブマップの場合
			if (desc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE) {
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
			}
			// 通常テクスチャ配列
			else {
				srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2DARRAY;
			}
			srvDesc.Texture2DArray.MostDetailedMip = 0;
			srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
			srvDesc.Texture2DArray.ArraySize = desc.ArraySize;	// 要素数
			srvDesc.Texture2DArray.FirstArraySlice = 0;
		}

		// シェーダリソースビュー作成
		HRESULT hr = KdDirect3D::Instance().WorkDev()->CreateShaderResourceView(resource, &srvDesc, ppSRV);
		if (FAILED(hr))
		{
			assert(0 && "ShaderResourceViewの作成に失敗");
			return false;
		}
	}

	//===========================================================
	//
	// DepthStencilViewを作成する
	//
	//===========================================================
	// Zバッファフラグがついてる時のみ
	if (ppDSV && desc.BindFlags & D3D11_BIND_DEPTH_STENCIL) {
		// 作成するビューの設定
		D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};

		// テクスチャー作成時に指定したフォーマットと互換性があり、深度ステンシルビューとして指定できるフォーマットを指定する
		switch (desc.Format) {
			// 16ビット
		case DXGI_FORMAT_R16_TYPELESS:
			dsvDesc.Format = DXGI_FORMAT_D16_UNORM;
			break;
			// 32ビット
		case DXGI_FORMAT_R32_TYPELESS:
			dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
			break;
			// 24ビット(Zバッファ) + 8ビット(ステンシルバッファ) 
		case DXGI_FORMAT_R24G8_TYPELESS:
			dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			break;
		default:
			assert(0 && "[DepthStencil] 対応していないフォーマットです");
			break;
		}

		// 単品のテクスチャ(通常テクスチャ)の場合
		if (desc.ArraySize == 1) {
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
			dsvDesc.Texture2D.MipSlice = 0;
		}
		// テクスチャ配列の場合
		else {
			dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
			dsvDesc.Texture2DArray.ArraySize = desc.ArraySize;
			dsvDesc.Texture2DArray.MipSlice = 0;
			dsvDesc.Texture2DArray.FirstArraySlice = 0;
		}

		//-------------------------------
		// デプスステンシルビュー作成
		//-------------------------------
		HRESULT hr = KdDirect3D::Instance().WorkDev()->CreateDepthStencilView(resource, &dsvDesc, ppDSV);
		if (FAILED(hr)) {
			assert(0 && "DepthStencilViewの作成に失敗");
			return false;
		}
	}

	return true;
}


// リソースを取得
const ID3D11Texture2D* KdTexture::GetResource() const
{
	return WorkResource();
}

ID3D11Texture2D* KdTexture::WorkResource() const
{
	if (m_srv == nullptr)return nullptr;

	ID3D11Resource* res;
	m_srv->GetResource(&res);

	ID3D11Texture2D* tex2D;
	if (FAILED(res->QueryInterface<ID3D11Texture2D>(&tex2D)))
	{
		res->Release();
		return nullptr;
	}
	res->Release();
	tex2D->Release();

	return tex2D;
}

std::string KdTexture::GetDdsCachePath(std::string_view filename)
{
	if (!kUseDedicatedDdsCacheFolder)
	{
		return std::string(filename) + ".kdtcache.dds";
	}

	// 専用サブフォルダへ集約する場合:
	// "Asset/Textures/Player/Hair.png" → "Asset/Textures/Player/.ddscache/Hair.png.dds"
	const std::filesystem::path srcPath(filename);
	const std::filesystem::path cacheDir = srcPath.parent_path() / kDdsCacheFolderName;

	return (cacheDir / (srcPath.filename().string() + ".dds")).string();
}

bool KdTexture::Load(std::string_view filename, bool renderTarget, bool depthStencil, bool generateMipmap)
{
	Release();
	if (filename.empty())return false;

	// ファイル名をWideCharへ変換
	std::wstring wFilename = sjis_to_wide(filename.data());

	//------------------------------------
	// 画像読み込み
	//------------------------------------

	// Bind Flags
	UINT bindFlags = 0;
	bindFlags |= D3D11_BIND_SHADER_RESOURCE;
	if (renderTarget)bindFlags |= D3D11_BIND_RENDER_TARGET;
	if (depthStencil)bindFlags |= D3D11_BIND_DEPTH_STENCIL;


	// ※DirectX Texライブラリを使用して画像を読み込む

	DirectX::TexMetadata meta;
	DirectX::ScratchImage image;

	bool bLoaded = false;

	//------------------------------------
	// DDSキャッシュ(ミップ生成済み)があれば、そこから直接読み込む
	// (RenderTarget/DepthStencil用の動的テクスチャは対象外。
	//  元がDDSファイルの場合はそのままDDS読込されるため対象外)
	//------------------------------------
	const bool useDdsCache = kEnableDdsCache && generateMipmap && !renderTarget && !depthStencil;
	std::string ddsCachePath;

	if (useDdsCache)
	{
		const std::string srcPath = std::string(filename);
		ddsCachePath = GetDdsCachePath(filename);

		std::error_code ec;
		if (std::filesystem::exists(ddsCachePath, ec) && !ec)
		{
			const auto srcTime = std::filesystem::last_write_time(srcPath, ec);
			if (!ec)
			{
				const auto cacheTime = std::filesystem::last_write_time(ddsCachePath, ec);
				if (!ec && cacheTime >= srcTime)
				{
					std::vector<uint8_t> ddsBytes;
					if (KdBinaryCache::LoadCompressedFile(ddsCachePath, ddsBytes) &&
						SUCCEEDED(DirectX::LoadFromDDSMemory(ddsBytes.data(), ddsBytes.size(), DirectX::DDS_FLAGS_NONE, &meta, image)))
					{
						bLoaded = true;
					}
				}
			}
		}
	}

	// キャッシュから読めなかった場合は元ファイルから通常ロードする
	bool loadedFromSourceImage = false;

	if (!bLoaded)
	{
		// WIC画像読み込み
		//  WIC_FLAGS_ALL_FRAMES … gifアニメなどの複数フレームを読み込んでくれる
		if (SUCCEEDED(DirectX::LoadFromWICFile(wFilename.c_str(), DirectX::WIC_FLAGS_ALL_FRAMES, &meta, image)))
		{
			bLoaded = true;
			loadedFromSourceImage = true;
		}

		// DDS画像読み込み
		if (bLoaded == false) {
			if (SUCCEEDED(DirectX::LoadFromDDSFile(wFilename.c_str(), DirectX::DDS_FLAGS_NONE, &meta, image)))
			{
				bLoaded = true;
			}
		}

		// TGA画像読み込み
		if (bLoaded == false) {
			if (SUCCEEDED(DirectX::LoadFromTGAFile(wFilename.c_str(), &meta, image)))
			{
				bLoaded = true;
				loadedFromSourceImage = true;
			}
		}

		// HDR画像読み込み
		if (bLoaded == false) {
			if (SUCCEEDED(DirectX::LoadFromHDRFile(wFilename.c_str(), &meta, image)))
			{
				bLoaded = true;
				loadedFromSourceImage = true;
			}
		}

		// 読み込み失敗
		if (bLoaded == false)
		{
			return false;
		}

		// ミップマップ生成
		if (meta.mipLevels == 1 && generateMipmap)
		{
			DirectX::ScratchImage mipChain;
			if (SUCCEEDED(DirectX::GenerateMipMaps(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::TEX_FILTER_DEFAULT, 0, mipChain)))
			{
				image.Release();
				image = std::move(mipChain);
			}
		}

		// ブロック圧縮(BC)する。無圧縮(RGBA8等)のままDDS保存すると
		// 元のPNG等より容量が大きくなってしまう(ミップ分も加算されるため)。
		// BC圧縮すればディスク容量・VRAM使用量・ロード速度の全てが改善する
		// (圧縮自体はキャッシュ生成時の1回だけなのでコストは許容できる)。
		// フォーマット上アルファチャンネルを持っていても実際は不透明なだけの
		// PNG等が多いため、最高解像度ミップを実走査して本当に使われている
		// 場合だけBC3(8bit/pixel)にし、それ以外はBC1(4bit/pixel)にする
		if (useDdsCache)
		{
			const bool needsAlpha =
				DirectX::HasAlpha(image.GetMetadata().format) &&
				HasMeaningfulAlpha(image.GetImages()[0]);

			const DXGI_FORMAT compressFormat = needsAlpha
				? (DirectX::IsSRGB(image.GetMetadata().format) ? DXGI_FORMAT_BC3_UNORM_SRGB : DXGI_FORMAT_BC3_UNORM)
				: (DirectX::IsSRGB(image.GetMetadata().format) ? DXGI_FORMAT_BC1_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM);

			DirectX::ScratchImage compressed;
			if (SUCCEEDED(DirectX::Compress(image.GetImages(), image.GetImageCount(), image.GetMetadata(),
				compressFormat, DirectX::TEX_COMPRESS_DEFAULT, DirectX::TEX_THRESHOLD_DEFAULT, compressed)))
			{
				image = std::move(compressed);
			}
			// 圧縮に失敗しても致命的ではない(無圧縮のまま使う)ため戻り値は見ない
		}

		// 次回以降のために、ミップ生成済みの画像をDDSキャッシュへ保存する
		// (失敗しても致命的ではないため、戻り値は見ずに継続する)
		if (useDdsCache && loadedFromSourceImage)
		{
			if (kUseDedicatedDdsCacheFolder)
			{
				// 専用サブフォルダを使う設定の場合、保存前にフォルダを作成しておく
				std::error_code ec;
				std::filesystem::create_directories(
					std::filesystem::path(ddsCachePath).parent_path(), ec);
			}

			// 一旦メモリ上にDDSを作り(BC圧縮済みデータそのもの)、
			// それをさらに汎用圧縮してからファイルへ保存する
			DirectX::Blob ddsBlob;
			if (SUCCEEDED(DirectX::SaveToDDSMemory(image.GetImages(), image.GetImageCount(), image.GetMetadata(), DirectX::DDS_FLAGS_NONE, ddsBlob)))
			{
				const uint8_t* p = static_cast<const uint8_t*>(ddsBlob.GetBufferPointer());
				std::vector<uint8_t> ddsBytes(p, p + ddsBlob.GetBufferSize());

				KdBinaryCache::SaveCompressedFile(ddsCachePath, ddsBytes);
			}
		}
	}

	//------------------------------------
	// テクスチャリソース作成
	//------------------------------------
	ID3D11Texture2D* tex2D = nullptr;
	if (FAILED(DirectX::CreateTextureEx(
		KdDirect3D::Instance().WorkDev(),				// Direct3D Device
		image.GetImages(),
		image.GetImageCount(),
		image.GetMetadata(),
		D3D11_USAGE_DEFAULT,							// Usage
		bindFlags,										// Bind Flags
		0,												// CPU Access Flags
		0,												// MiscFlags
		DirectX::CREATETEX_FLAGS::CREATETEX_DEFAULT,	// CREATETEX_DEFAULT
		(ID3D11Resource**)&tex2D)
	)) {

		return false;
	}

	//------------------------------------
	// テクスチャリソース(m_resource)から、各ビューを作成する
	//------------------------------------
	if (KdCreateViewsFromTexture2D(tex2D, &m_srv, &m_rtv, &m_dsv) == false)
	{
		tex2D->Release();
		Release();
		return false;
	}

	// 画像情報取得
	tex2D->GetDesc(&m_desc);
	tex2D->Release();

	m_filepath = filename;

	return true;
}

bool KdTexture::Create(ID3D11Texture2D* pTexture2D)
{
	Release();
	if (pTexture2D == nullptr)return false;

	//---------------------------------------------
	// 画像リソースから、各ビューを作成する
	//---------------------------------------------
	if (KdCreateViewsFromTexture2D(pTexture2D, &m_srv, &m_rtv, &m_dsv) == false) {
		Release();
		return false;
	}

	// ファイル名記憶
	m_filepath = "";

	// 画像情報取得
	pTexture2D->GetDesc(&m_desc);

	return true;

}

bool KdTexture::Create(const D3D11_TEXTURE2D_DESC & desc, const D3D11_SUBRESOURCE_DATA * fillData)
{
	Release();

	//--------------------------------------------
	// 2Dテクスチャの生成
	//--------------------------------------------
	ID3D11Texture2D* resource;
	HRESULT hr = KdDirect3D::Instance().WorkDev()->CreateTexture2D(&desc, fillData, &resource);
	if (FAILED(hr)) {
		Release();
		return false;
	}

	//--------------------------------------------
	// m_pTexture2Dから、各ビューを作成する
	//--------------------------------------------
	if (KdCreateViewsFromTexture2D(resource, &m_srv, &m_rtv, &m_dsv) == false)
	{
		Release();
		return false;
	}

	// 画像情報取得
	resource->GetDesc(&m_desc);
	resource->Release();

	return true;
}

void SetTextureInfo(D3D11_TEXTURE2D_DESC& rDesc, int w, int h, DXGI_FORMAT format, UINT arrayCnt, UINT miscFlags)
{
	rDesc.Width = (UINT)w;
	rDesc.Height = (UINT)h;
	rDesc.Format = format;
	rDesc.ArraySize = arrayCnt;
	rDesc.MiscFlags = miscFlags;
}

bool KdTexture::Create(int w, int h, DXGI_FORMAT format, UINT arrayCnt, const D3D11_SUBRESOURCE_DATA * fillData)
{
	Release();


	// 作成する2Dテクスチャ設定
	D3D11_TEXTURE2D_DESC desc = {};

	SetTextureInfo(desc, w, h, format, arrayCnt, 0);

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	// 作成
	if (Create(desc, fillData) == false)return false;

	return true;
}

bool KdTexture::CreateRenderTarget(int w, int h, DXGI_FORMAT format, UINT arrayCnt, const D3D11_SUBRESOURCE_DATA* fillData, UINT miscFlags)
{
	Release();

	// 作成する2Dテクスチャ設定
	D3D11_TEXTURE2D_DESC desc = {};

	SetTextureInfo(desc, w, h, format, arrayCnt, 0);
	
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.MiscFlags = miscFlags;

	// 作成
	if (Create(desc, fillData) == false)return false;

	return true;
}

bool KdTexture::CreateDepthStencil(int w, int h, DXGI_FORMAT format, UINT arrayCnt, const D3D11_SUBRESOURCE_DATA* fillData, UINT miscFlags)
{
	Release();

	// 作成する2Dテクスチャ設定
	D3D11_TEXTURE2D_DESC desc = {};

	SetTextureInfo(desc, w, h, format, arrayCnt, 0);

	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
	desc.CPUAccessFlags = 0;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.MiscFlags = miscFlags;

	// 作成
	if (Create(desc, fillData) == false)return false;

	return true;
}

void KdTexture::SetSRView(ID3D11ShaderResourceView* srv)
{
	if (srv == nullptr)return;

	Release();
	m_srv = srv;	// セット
	m_srv->AddRef();// 参照カウンタを増やしておく

	// 画像情報取得
	WorkResource()->GetDesc(&m_desc);
}

void KdTexture::Release()
{
	KdSafeRelease(m_srv);
	KdSafeRelease(m_rtv);
	KdSafeRelease(m_dsv);

	m_filepath = "";
}

