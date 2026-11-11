#pragma once
#include "Utility/MaterialNameTable.h"

enum class RenderKind : uint8_t { Model, Work , Static};

// マテリアル名前引き用
struct MaterialId
{
	uint32_t value = 0;
};

struct RenderComponent
{
	RenderKind kind = RenderKind::Model;
	uint32_t   assetId = 0;
	uint32_t   generation = 0;
	MaterialId materialId;
	DirectX::XMFLOAT4 params[4] = {};

	// 遷移
	float blend = 1.0f;          // 0=効果なし, 1=完全に適用
	float blendTarget = 1.0f;    // 目標値
	float blendSpeed = 0.0f;     // 1秒あたりの変化量（0=即時）

};

// MaterialId を名前で設定
inline void SetMaterialId(MaterialId& id, const std::string& name)
{
	id.value = MaterialIdFromName(name);
}
inline void ClearMaterialId(MaterialId& id)
{
	id.value = 0;
}
inline const std::string& GetMaterialIdName(const MaterialId& id)
{
	return MaterialNameFromId(id.value);
}

// マテリアル操作
inline void SetMaterial(RenderComponent& rc, const std::string& name)
{
	SetMaterialId(rc.materialId, name);
}
inline void ClearMaterial(RenderComponent& rc)
{
	ClearMaterialId(rc.materialId);
}
inline const std::string& GetMaterialName(const RenderComponent& rc)
{
	return MaterialNameFromId(rc.materialId.value);
}

// パラメータ操作
inline void SetParam(RenderComponent& rc, int index,
	float x, float y = 0.0f, float z = 0.0f, float w = 0.0f)
{
	if (index < 0 || index >= 4) return;
	rc.params[index] = { x, y, z, w };
}
inline void SetParamX(RenderComponent& rc, int index, float x)
{
	if (index < 0 || index >= 4) return;
	rc.params[index].x = x;
}
inline float GetParamComp(const RenderComponent& rc, int index, int comp)
{
	if (index < 0 || index >= 4 || comp < 0 || comp >= 4) return 0.0f;
	return (&rc.params[index].x)[comp];
}

// マテリアルを適用しながらフェードイン
inline void ApplyMaterial(RenderComponent& rc, const std::string& name,
	float duration = 0.3f)
{
	rc.materialId.value = MaterialIdFromName(name);
	rc.blend = 0.0f;
	rc.blendTarget = 1.0f;
	rc.blendSpeed = (duration > 0.001f) ? (1.0f / duration) : 0.0f;
}

// フェードアウトして解除（完全に消えたら materialId を 0 に）
inline void FadeOutMaterial(RenderComponent& rc, float duration = 0.3f)
{
	rc.blendTarget = 0.0f;
	rc.blendSpeed = (duration > 0.001f) ? (1.0f / duration) : 0.0f;
}

static_assert(std::is_trivially_copyable_v<RenderComponent>, "RenderComponent must be trivially copyable");