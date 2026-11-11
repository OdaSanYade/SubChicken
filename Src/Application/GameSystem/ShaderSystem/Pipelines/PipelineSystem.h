#pragma once
#include "Assets/MultiPassRuntime.h"

// マルチパスのマテリアル/ポストエフェクトをまとめて管理する
class PipelineSystem
{
public:
	using DrawMeshCallback = std::function<void()>;

	void Init(ID3D11Device* device, ID3D11DeviceContext* context);

	// フォルダ内の .pipeline.json を全部ロード（自動振り分け）
	// 既定の materialFile / effectFile を初期ロードに使う
	void LoadDefaults(const std::string& materialPath,
		const std::string& effectPath);

	// 1ファイルをロード（中身で material/effect を自動振り分け）
	void LoadAuto(const std::string& path);

	// ライブシンク受信（ShaderRuntime のコールバックに繋ぐ）
	void OnPipelineReceived(const std::string& json);

	// マテリアル実行（モデルを描く）→ 画面に合成
	void RenderMaterial(float time, float bass, float mid, float treble,
		const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& worldView, const DirectX::XMMATRIX& world,
		int screenW, int screenH, DrawMeshCallback drawMesh);

	// ポストエフェクト実行（画面全体を加工）→ 画面に合成
	void RenderPostEffect(float time, float bass, float mid, float treble,
		int screenW, int screenH);

	// ImGui: パラメータUI + ローダー
	void DrawDebugUI(const std::string& assetFolder = "Asset/Data");

	MultiPassRuntime* Material() { return m_material.get(); }
	MultiPassRuntime* PostEffect() { return m_postEffect.get(); }

	void SetParam(const std::string& paramName, float x, float y = 0, float z = 0, float w = 0)
	{
		if (m_material) m_material->SetParam(paramName, x, y, z, w);
		if (m_postEffect) m_postEffect->SetParam(paramName, x, y, z, w);
	}

	// マテリアル・ポストエフェクト両方のパラメータを既定値に戻す
	void RestoreDefaultParams()
	{
		if (m_material) m_material->RestoreDefaults();
		if (m_postEffect) m_postEffect->RestoreDefaults();
	}

	void UpdateWorldMatrix(const DirectX::XMMATRIX& wvp,
		const DirectX::XMMATRIX& worldView, const DirectX::XMMATRIX& world)
	{
		if (m_material) m_material->UpdateWorldMatrix(wvp, worldView, world);
	}

	void SetMaterialParams(const DirectX::XMFLOAT4& baseColor, float metallic, float roughness)
	{
		if (m_material) m_material->SetMaterialParams(baseColor, metallic, roughness);
	}

	void SetCamera(const DirectX::XMFLOAT3& camPos,
		const DirectX::XMMATRIX& viewProj);

	void UseStaticVS();
	void UseSkinnedVS();

	void SetBones(const DirectX::XMFLOAT4X4* bones, int count);

	ID3D11ShaderResourceView* GetGBuffer(const std::string& name)
	{
		return m_material ? m_material->GetNamedBufferSRV(name) : nullptr;
	}

private:
	std::shared_ptr<MultiPassRuntime> m_material;
	std::shared_ptr<MultiPassRuntime> m_postEffect;
	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;

	// ローダーUI用キャッシュ
	struct CachedPipeline {
		std::string path, fname;
		bool isEffect;
	};
	std::vector<CachedPipeline> m_cache;
	bool m_cacheDirty = true;
	void RefreshCache(const std::string& folder);
	void DrawLoader(const std::string& folder);
};