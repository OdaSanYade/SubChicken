#pragma once
#include <Application/GameSystem/ComponentSystem/KdComponentManager.h>

class ShaderRender;
class WorkPool;

namespace Render {

	//全体更新
	void UpdateAll(kd::ecs::ComponentManager& cm, float dt, ShaderRender& renderer);

	//オブジェクトの収集
	void CollectRenderSystem(kd::ecs::ComponentManager& cm, ShaderRender& renderer,float dt);

	// ライトの収集
	void CollectLight(kd::ecs::ComponentManager& cm, ShaderRender& renderer);

	// ポストエフェクト描画
	void UpdatePostEffects(kd::ecs::ComponentManager& cm, ShaderRender& renderer, float dt);

	//マテリアルのブレンド更新
	void UpdateMaterialBlend(kd::ecs::ComponentManager& cm, float dt);

	// 残像モデルのポーズをコピー
	bool CaptureGhostBonePose(const RenderComponent& rc, DirectX::XMFLOAT4X4* outBones);

	//残像の更新
	void UpdateGhostTrail(kd::ecs::ComponentManager& cm, float dt);

	//リボントレイルの更新（renderer を渡すと、点を打った瞬間にトレイル設定のパーティクルを出す）
	void UpdateTrail(kd::ecs::ComponentManager& cm, float dt, ShaderRender* renderer = nullptr);

	//トレイルの根元と先端を直接指定
	void PushTrailPoint(TrailComponent& t, const Math::Vector3& root,
		const Math::Vector3& tip);
}