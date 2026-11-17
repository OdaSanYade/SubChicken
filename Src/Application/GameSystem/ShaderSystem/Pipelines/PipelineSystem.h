#pragma once
#include "Assets/MultiPassRuntime.h"

// マルチパスのマテリアル/ポストエフェクトをまとめて管理する
class PipelineSystem
{
public:
	using DrawMeshCallback = std::function<void()>;

	void Init(ID3D11Device* device, ID3D11DeviceContext* context);

	// フォルダ内の .pipeline.json をロード（自動振り分け）
	// 既定の materialFileを初期ロードに使う
	void LoadDefaults(const std::string& materialPath);

	// ライブシンク受信（ShaderRuntime のコールバックに繋ぐ）
	void OnPipelineReceived(const std::string& json);

	// マテリアル実行（モデルを描く）→ 画面に合成
	void RenderMaterial(float time, float bass, float mid, float treble,
		const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& worldView, const DirectX::XMMATRIX& world,
		int screenW, int screenH, DrawMeshCallback drawMesh);

	// ImGui: パラメータUI + ローダー
	void DrawDebugUI(const std::string& assetFolder = "Asset/Data");

	MultiPassRuntime* Material() { return m_material.get(); }

	void SetParam(const std::string& paramName, float x, float y = 0, float z = 0, float w = 0)
	{
		if (m_material) m_material->SetParam(paramName, x, y, z, w);
	}

	// マテリアルのパラメータを既定値に戻す
	void RestoreDefaultParams()
	{
		if (m_material) m_material->RestoreDefaults();
	}

	// 材質のパラメータの変更
	void SetMaterialParams(const DirectX::XMFLOAT4& baseColor, float metallic, float roughness)
	{
		if (m_material) m_material->SetMaterialParams(baseColor, metallic, roughness);
	}

	// カメラ行列の更新
	void UpdateWorldMatrix(const DirectX::XMMATRIX& wvp,
		const DirectX::XMMATRIX& worldView, const DirectX::XMMATRIX& world)
	{
		if (m_material) m_material->UpdateWorldMatrix(wvp, worldView, world);
	}

	// カメラの設定
	void SetCamera(const DirectX::XMFLOAT3& camPos,
		const DirectX::XMMATRIX& viewProj);

	// 静的モデル OR スキンメッシュのどちらのVSを使うか
	void UseStaticVS();
	void UseSkinnedVS();
	// スキンメッシュのボーン行列の設定
	void SetBones(const DirectX::XMFLOAT4X4* bones, int count);

	// パイプライン側で作成去れたテクスチャを使える
	ID3D11ShaderResourceView* GetGBuffer(const std::string& name)
	{
		return m_material ? m_material->GetNamedBufferSRV(name) : nullptr;
	}

private:

	std::shared_ptr<MultiPassRuntime> m_material;
	ID3D11Device* m_device = nullptr;
	ID3D11DeviceContext* m_context = nullptr;
};