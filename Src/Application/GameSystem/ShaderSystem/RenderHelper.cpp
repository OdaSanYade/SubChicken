#include "RenderHelper.h"
#include "ShaderRender.h"

#include <random>

void Render::UpdateAll(kd::ecs::ComponentManager& cm, float dt, ShaderRender& renderer)
{
	UpdateMaterialBlend(cm, dt);
	UpdateGhostTrail(cm, dt);
	UpdateTrail(cm, dt, &renderer);
	UpdatePostEffects(cm, renderer,dt);
	CollectRenderSystem(cm, renderer,dt);
	CollectLight(cm, renderer);
}	
//オブジェクトの描画情報を収集する
void Render::CollectRenderSystem(kd::ecs::ComponentManager& cm, ShaderRender& renderer,float dt)
{
	(void)dt;   // 減衰処理は UpdateGhostTrail に一本化した

	cm.view<TransformComponent, RenderComponent>(
		[&](kd::ecs::Entity entity, TransformComponent& tc, RenderComponent& rc)
		{
			Math::Matrix world = tc.GetWorldMatrix();

			// 本体
			if (rc.kind == RenderKind::Static)
				renderer.AddStatic(rc.assetId, world, rc.materialId.value, rc.params);
			else if (rc.kind == RenderKind::Model)
				renderer.AddModel(rc.assetId, world, rc.materialId.value, rc.params, rc.blend);
			else
				renderer.AddWork(rc.assetId, rc.generation, world,
					rc.materialId.value, rc.params, rc.blend);

			if (!cm.has<GhostTrailComponent>(entity)) return;
			auto& g = cm.get<GhostTrailComponent>(entity);

			ModelId srcId = rc.assetId;
			if (rc.kind == RenderKind::Work)
				srcId = RES_MGR.GetWorkPool()->GetSrcModelId(rc.assetId);

			// ---- XRay: 履歴を使わず「現在のポーズ」を1回だけ出す ----
			if (g.mode == GhostMode::XRay)
			{
				DirectX::XMFLOAT4 color = { g.color.x, g.color.y, g.color.z, 1.0f };   // a=1 で全表示
				DirectX::XMFLOAT4 rim = { g.rimColor.x, g.rimColor.y, g.rimColor.z, g.hitPulse };
				DirectX::XMFLOAT4 move = { 0.0f, 0.0f, 0.0f, 0.0f };                  // 引き伸ばし不要

				// 毎フレームの現在ボーンを取得(AddSkinned 側でコピーされるので使い回しでOK)
				static std::vector<DirectX::XMFLOAT4X4> bones(GhostTrailComponent::kMaxBones);
				if (CaptureGhostBonePose(rc, bones.data()))
					renderer.AddXRaySkinned(srcId, world, color, rim, move,
						bones.data(), GhostTrailComponent::kMaxBones);
				else
					renderer.AddXRay(srcId, world, color, rim, move);
				return;
			}

			// ---- ここから下は従来のゴースト(履歴ループ)。AddXRay* の分岐は削除 ----
			if (g.count <= 0) return;
			for (int i = 0; i < g.count; ++i)
			{
				int idx = (g.head - 1 - i + GhostTrailComponent::kMaxFrames * 2)
					% GhostTrailComponent::kMaxFrames;
				float fade = 1.0f - (g.ages[idx] / g.lifeTime);
				if (fade <= 0.0f) continue;

				// ★進行方向・速度を1つ古いフレームとの差分から推定(idea23用)
				Math::Vector3 moveDir = {};
				float moveSpeed = 0.0f;
				if (i + 1 < g.count)
				{
					int olderIdx = (g.head - 1 - (i + 1) + GhostTrailComponent::kMaxFrames * 2)
						% GhostTrailComponent::kMaxFrames;
					Math::Vector3 d = g.worlds[idx].Translation() - g.worlds[olderIdx].Translation();
					float len = d.Length();
					if (len > 0.0001f)
					{
						moveDir = d / len;
						float t = len / g.spacing;
						moveSpeed = (t < 0.0f) ? 0.0f : (t > 1.0f ? 1.0f : t);
					}
				}

				DirectX::XMFLOAT4 ghostColor = { g.color.x, g.color.y, g.color.z, fade };
				DirectX::XMFLOAT4 rimColor4 = { g.rimColor.x, g.rimColor.y, g.rimColor.z, g.hitPulse };
				DirectX::XMFLOAT4 moveDirSpeed4 = { moveDir.x, moveDir.y, moveDir.z, moveSpeed };

				if (g.boneValid[idx])
					renderer.AddGhostSkinned(srcId, g.worlds[idx], ghostColor, rimColor4, moveDirSpeed4,
						g.bonePoses[idx], GhostTrailComponent::kMaxBones);
				else
					renderer.AddGhost(srcId, g.worlds[idx], ghostColor, rimColor4, moveDirSpeed4);
			}
		});

	cm.view<TrailComponent>(
		[&](kd::ecs::Entity, TrailComponent& t)
		{
			renderer.AddTrail(t, renderer.GetLastCamPos());
		});
}
//ライトの描画情報を収集する
void Render::CollectLight(kd::ecs::ComponentManager& cm, ShaderRender& renderer)
{
	cm.view<TransformComponent, LightComponent>(
		[&](kd::ecs::Entity, TransformComponent& tc, LightComponent& lc)
		{
			if (!lc.enabled) return;

			Math::Matrix world = tc.GetWorldMatrix();
			Math::Vector3 p = world.Translation();

			// オフセットをワールドへ
			DirectX::XMVECTOR off = DirectX::XMVector3TransformNormal(
				DirectX::XMLoadFloat3(&lc.offset), world);
			DirectX::XMFLOAT3 o; DirectX::XMStoreFloat3(&o, off);
			DirectX::XMFLOAT3 pos = { p.x + o.x, p.y + o.y, p.z + o.z };

			if (lc.type == LightType::Spot)
			{
				// モデルの前方（Z軸）を照射方向とする
				DirectX::XMFLOAT3 dir = { world._31, world._32, world._33 };
				renderer.AddLight(MakeSpotLight(pos, lc.range, lc.color, lc.power,
					dir, lc.innerDeg, lc.outerDeg));
			}
			else
			{
				renderer.AddLight(MakePointLight(pos, lc.range, lc.color, lc.power));
			}
		});
}
//ポストエフェクトの更新
void Render::UpdatePostEffects(kd::ecs::ComponentManager& cm, ShaderRender& renderer, float dt)
{
	bool postEffectEnabled = false;
	bool RenderEditMode = false;

	cm.view<PostEffectComponent>(
		[&](kd::ecs::Entity, PostEffectComponent& p)
		{
			if (p.isEditMode)
			{
				RenderEditMode = p.isEditMode;
				return;
			}

			//全体の色味
			{
				auto& e = p.screenTone;
				e.tween.duration = e.easeDuration;
				e.tween.easeType = e.easeType;
				// enabledなら設定値
				e.tween.SetTarget(e.enabled ? e.saturation : 0.0f);
				e.tween.Update(dt);

				if (e.tween.IsActive())
				{
					renderer.SetPipelineParam("Saturation", e.tween.current);
					postEffectEnabled = true;
				}
			}
			//色収差
			{
				auto& e = p.abberation;
				e.tween.duration = e.easeDuration;
				e.tween.easeType = e.easeType;
				e.tween.SetTarget(e.enabled ? e.strength : 0.0f);
				e.tween.Update(dt);

				if (e.tween.IsActive())
				{
					renderer.SetPipelineParam("Aberration", e.tween.current);
					postEffectEnabled = true;
				}
			}
			//放射状ブラー
			{
				auto& e = p.radialBlur;
				e.tween.duration = e.easeDuration;
				e.tween.easeType = e.easeType;
				e.tween.SetTarget(e.enabled ? e.strength : 0.0f);
				e.tween.Update(dt);

				if (e.tween.IsActive())
				{
					renderer.SetPipelineParam("RadialStrength", e.tween.current);
					renderer.SetPipelineParam("RadialFalloff", e.falloff); // 形状値は即時反映のまま
					postEffectEnabled = true;
				}
			}
			//ビネット
			{
				auto& e = p.vignette;
				e.tween.duration = e.easeDuration;
				e.tween.easeType = e.easeType;
				e.tween.SetTarget(e.enabled ? e.strength : 0.0f);
				e.tween.Update(dt);

				if (e.tween.IsActive())
				{
					renderer.SetPipelineParam("Vignette", e.tween.current);
					renderer.SetPipelineParam("VignetteColor", e.color.x, e.color.y, e.color.z, 1.0f);
					postEffectEnabled = true;
				}
			}
		});

	// どの効果もアクティブでないならデフォルト値を設定する
	if (!postEffectEnabled && !RenderEditMode)
		renderer.SetDefaultPipelineParam();
}
//マテリアルのブレンド値を更新
void Render::UpdateMaterialBlend(kd::ecs::ComponentManager& cm, float dt)
{
	cm.view<RenderComponent>([&](kd::ecs::Entity, RenderComponent& rc)
		{
			if (rc.blendSpeed <= 0.0f) { rc.blend = rc.blendTarget; return; }
			float d = rc.blendTarget - rc.blend;
			float step = rc.blendSpeed * dt;
			if (fabsf(d) <= step) rc.blend = rc.blendTarget;
			else                  rc.blend += (d > 0 ? step : -step);

			if (rc.blend <= 0.0f && rc.blendTarget <= 0.0f)
				rc.materialId.value = 0;

			// blend の予約枠を設定
			rc.params[3].w = rc.blend;
		});
}
// 残像モデルのポーズをコピー
bool Render::CaptureGhostBonePose(const RenderComponent& rc, DirectX::XMFLOAT4X4* outBones)
{
	if (rc.kind != RenderKind::Work) return false;

	auto work = RES_MGR.GetWorkPool()->GetShared(rc.assetId, rc.generation);
	if (!work) return false;

	auto data = work->GetData();
	if (!data || !data->IsSkinMesh()) return false;

	if (work->NeedCalcNodeMatrices())
		work->CalcNodeMatrices();

	DirectX::XMFLOAT4X4 ident;
	DirectX::XMStoreFloat4x4(&ident, DirectX::XMMatrixIdentity());
	for (int i = 0; i < GhostTrailComponent::kMaxBones; ++i)
		outBones[i] = ident;

	auto& workNodes = work->GetNodes();
	auto& dataNodes = data->GetOriginalNodes();
	for (auto& nodeIdx : data->GetBoneNodeIndices())
	{
		Math::Matrix b = dataNodes[nodeIdx].m_boneInverseWorldMatrix
			* workNodes[nodeIdx].m_worldTransform;
		int boneIndex = dataNodes[nodeIdx].m_boneIndex;
		if (boneIndex < 0 || boneIndex >= GhostTrailComponent::kMaxBones) continue;
		DirectX::XMStoreFloat4x4(&outBones[boneIndex], b);
	}
	return true;
}
//残像の更新
void Render::UpdateGhostTrail(kd::ecs::ComponentManager& cm, float dt)
{
	const int N = GhostTrailComponent::kMaxFrames;

	cm.view<TransformComponent, RenderComponent, GhostTrailComponent>(
		[&](kd::ecs::Entity, TransformComponent& tc, RenderComponent& rc, GhostTrailComponent& g)
		{
			// ★ヒット演出パルスの減衰(約0.25秒で0に)。減衰はここ1か所だけ
			if (g.hitPulse > 0.0f)
			{
				g.hitPulse -= dt * 4.0f;
				if (g.hitPulse < 0.0f) g.hitPulse = 0.0f;
			}

			// X線モードは「現在のポーズ」だけを使うので履歴は記録しない
			if (g.mode == GhostMode::XRay)
			{
				g.count = 0;
				g.hasLast = false;
				return;
			}

			Math::Matrix world = tc.GetWorldMatrix();
			Math::Vector3 pos = world.Translation();

			for (int i = 0; i < g.count; ++i)
			{
				int idx = (g.head - 1 - i + N * 2) % N;
				g.ages[idx] += dt;
			}

			if (g.recording)
			{
				bool put = !g.hasLast;
				if (g.hasLast)
				{
					Math::Vector3 d = pos - g.lastPos;
					if (d.Length() >= g.spacing) put = true;
				}
				if (put)
				{
					g.worlds[g.head] = world;
					g.ages[g.head] = 0.0f;
					g.boneValid[g.head] = CaptureGhostBonePose(rc, g.bonePoses[g.head]);

					g.head = (g.head + 1) % N;
					if (g.count < N) ++g.count;
					g.lastPos = pos;
					g.hasLast = true;
				}
			}
			else
			{
				g.hasLast = false;
			}

			while (g.count > 0)
			{
				int oldest = (g.head - g.count + N * 2) % N;
				if (g.ages[oldest] < g.lifeTime) break;
				--g.count;
			}
		});
}
//トレイルの更新
void Render::UpdateTrail(kd::ecs::ComponentManager& cm, float dt, ShaderRender* renderer)
{
	const int N = TrailComponent::kMaxPoints;

	// 新しい点を打った瞬間に、トレイル設定のパーティクルを確率で出す
	static std::mt19937 rng(12345u);
	std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
	auto spawnParticle = [&](const TrailComponent& t)
		{
			if (!renderer) return;
			std::string effect;
			float chance = 0.0f;
			if (!renderer->GetTrailParticleSpawn(t.trailId, effect, chance)) return;
			if (dist01(rng) >= chance) return;
			const auto& p = t.points[(t.head - 1 + N) % N];
			renderer->PlayParticleEffect(effect, { p.x, p.y, p.z });
		};

	cm.view<TransformComponent, TrailComponent>(
		[&](kd::ecs::Entity, TransformComponent& tc, TrailComponent& t)
		{
			Math::Matrix world = tc.GetWorldMatrix();
			Math::Vector3 pos = world.Translation();

			// 寿命
			for (int i = 0; i < t.count; ++i)
			{
				int idx = (t.head - 1 - i + N * 2) % N;
				t.ages[idx] += dt;
			}

			// 点を打つ
			if (t.source == static_cast<int>(TrailSource::External))
			{
				if (t.emitDirty)
				{
					// RootTip は points=中心 / ups=半幅ベクトル
					t.points[t.head] = (t.emitRoot + t.emitTip) * 0.5f;
					t.ups[t.head] = (t.emitTip - t.emitRoot) * 0.5f;
					t.ages[t.head] = 0.0f;

					t.head = (t.head + 1) % N;
					if (t.count < N) ++t.count;

					t.lastPos = t.points[(t.head - 1 + N) % N];
					t.hasLast = true;
					t.emitDirty = false;
					spawnParticle(t);
				}
			}
			else if (t.emitting)
			{
				bool put = !t.hasLast;
				if (t.hasLast)
				{
					Math::Vector3 d = pos - t.lastPos;
					if (d.Length() >= t.minDist) put = true;
				}
				if (put)
				{
					t.points[t.head] = pos;

					// モデルのY軸を刃方向とする
					Math::Vector3 blade(world._21, world._22, world._23);
					blade.Normalize();

					// RootTip のときは「半幅ベクトル」として記録する
					t.ups[t.head] = (t.widthMode == (int)TrailWidthMode::RootTip)
						? blade * (t.width * 0.5f)
						: blade;

					t.ages[t.head] = 0.0f;
					t.head = (t.head + 1) % N;
					if (t.count < N) ++t.count;
					t.lastPos = pos;
					t.hasLast = true;
					spawnParticle(t);
				}
			}
			else t.hasLast = false;

			// 寿命切れを古い方から削る
			while (t.count > 0)
			{
				int oldest = (t.head - t.count + N * 2) % N;
				if (t.ages[oldest] < t.lifeTime) break;
				--t.count;
			}
		});
}
//外部から根元と先端を与えて点を打つ
void Render::PushTrailPoint(TrailComponent& t, const Math::Vector3& root,
	const Math::Vector3& tip)
{
	const int N = TrailComponent::kMaxPoints;

	// RootTip は points=中心 / ups=半幅ベクトル
	t.points[t.head] = (root + tip) * 0.5f;
	t.ups[t.head] = (tip - root) * 0.5f;
	t.ages[t.head] = 0.0f;

	t.head = (t.head + 1) % N;
	if (t.count < N) ++t.count;

	t.lastPos = t.points[(t.head - 1 + N) % N];
	t.hasLast = true;
}