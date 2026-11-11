#include "PlayerStateSetup.h"
#include "../../../../GameSystem/StateMachine/StateMachineManager.h"
#include "../../../Config/StateTuning.h"
#include "../Player.h"

using namespace StateLiterals;

namespace
{
	// 演出用のparams は "fx." で始めている。

	// 遷移の優先度。数字が大きいほど強い(同じフレームに複数の遷移が来たら強いほうが残る)
	namespace Priority
	{
		constexpr uint8_t kDead = 20;		// 何よりも優先
		constexpr uint8_t kCryFull = 14;	// 全力鳴き
		constexpr uint8_t kCounter = 12;	// ジャスト回避からの反撃
		constexpr uint8_t kDodge = 10;
		constexpr uint8_t kAttack = 8;		// 弱・強どちらも
	}

	// メートル/秒速で持っている params。JSON には縮尺前の値を書く
	const StateTuning::ParamUnits kParamUnits = {
		{ "reach", "radius", "keepDist", "maxStep", "swing.top", "swing.bottom", "fx.lift" },
		{ "knockback" },
	};

	// params の "swing.*" から振りの軌跡を作る。
	// asset の文字列はステート定義の中にあるので、定義を作り直さない限り指したままでいい
	Player::AttackSwing MakeSwing(const StateContext& ctx)
	{
		Player::AttackSwing s;
		s.asset = ctx.ParamStr("swing.asset"_sid, s.asset);
		s.lifeTime = ctx.Param("swing.life"_sid, s.lifeTime);
		s.widthScale = ctx.Param("swing.width"_sid, s.widthScale);
		s.topY = ctx.Param("swing.top"_sid, s.topY);
		s.bottomY = ctx.Param("swing.bottom"_sid, s.bottomY);
		s.yawDeg = ctx.Param("swing.yaw"_sid, s.yawDeg);
		s.swingSec = ctx.Param("swing.sec"_sid, s.swingSec);
		s.inner = ctx.Param("swing.inner"_sid, s.inner);
		s.outer = ctx.Param("swing.outer"_sid, s.outer);
		return s;
	}

	// params の "<名前>.r/g/b" を色にする
	Math::Vector3 ParamColor(const StateContext& ctx, std::string_view _name)
	{
		const std::string base(_name);
		return {
			ctx.Param(MakeStateId(base + ".r")),
			ctx.Param(MakeStateId(base + ".g")),
			ctx.Param(MakeStateId(base + ".b")) };
	}

	// よく使うコンポーネントの取り出し
	PostEffectComponent& PostEffect(Player* _p) { return _p->GetComponentManager().get<PostEffectComponent>(_p->GetEntity()); }
	LightComponent& Light(Player* _p) { return _p->GetComponentManager().get<LightComponent>(_p->GetEntity()); }
	RenderComponent& Render(Player* _p) { return _p->GetComponentManager().get<RenderComponent>(_p->GetEntity()); }

	float DeltaTime() { return static_cast<float>(Time::Instance().GetDeltaTime()); }
}

void SetupPlayerStates()
{
	auto* sys = SM_MGR.CreateSystem("Player");

	// 既に登録済みなら何もしない(Initから複数回呼ばれても安全にする)
	if (sys->GetStateCount() > 0) return;

	// =======================================================
	// ベース
	// =======================================================

	// ---- 接地 ----
	sys->State("Grounded").Tag(PlayerTag::kGrounded)
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();

				// 優先度は上の Priority を参照(死亡 > 全力鳴き > カウンター > 回避 > 攻撃)
				if (p->ConsumeDeadRequest()) { ctx.ChangeState("Dead"_sid, Priority::kDead); return; }

				// 被弾は死亡の次に強い
				if (p->ConsumeHurtRequest()) { ctx.ChangeStateForced("Hurt"_sid); return; }

				// ジャスト回避の受付が生きている間、攻撃入力はカウンターに化ける
				if (p->IsCounterReady() && p->ConsumeAttackRequest())
				{
					ctx.ChangeState("Counter"_sid, Priority::kCounter);
					return;
				}

				if (p->ConsumeDodgeRequest()) { ctx.ChangeState("Dodge"_sid, Priority::kDodge); return; }
				if (p->ConsumeAttackRequest()) { ctx.ChangeState("Attack1"_sid, Priority::kAttack); return; }
				if (p->ConsumeHeavyRequest()) { ctx.ChangeState("Heavy1"_sid, Priority::kAttack); return; }
				if (p->ConsumeCryRequest())
				{
					if (p->CanCryFull()) { ctx.ChangeState("CryFull"_sid, Priority::kCryFull); return; }
				}
			});

	// ---- アクション(割り込み不可の動作) ----
	sys->State("Action").Tag(PlayerTag::kAction)
		.Param("trailAsset", "Dodge")
		.Param("fx.aberration", 0.2f)		// アクション中ずっと掛ける色収差
		.OnEnter([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				auto& trail = p->GetComponentManager().get<TrailComponent>(p->GetEntity());
				trail.source = static_cast<int>(TrailSource::Transform);
				trail.widthMode = static_cast<int>(TrailWidthMode::Billboard);
				trail.SetAsset(ctx.ParamStr("trailAsset"_sid));
				trail.emitting = true;

				auto& pe = PostEffect(p);
				pe.abberation.enabled = true;
				pe.abberation.strength = ctx.Param("fx.aberration"_sid);
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();

				if (p->ConsumeDeadRequest()) { ctx.ChangeState("Dead"_sid, Priority::kDead); return; }

				// 攻撃中でも被弾で中断する。
				if (p->ConsumeHurtRequest() && !p->IsCry()) { ctx.ChangeStateForced("Hurt"_sid); return; }

				if (p->IsCancelable() && p->IsDodgeReady() && p->ConsumeDodgeRequest())
					ctx.ChangeStateForced("Dodge"_sid);
			})
		.OnExit([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->GetComponentManager().get<TrailComponent>(p->GetEntity()).emitting = false;

				auto& pe = p->GetComponentManager().get<PostEffectComponent>(p->GetEntity());
				pe.AllStopEnabled();
			});

	// =======================================================
	// 地面の子
	// =======================================================
	sys->State("Idle").Base("Grounded")
		.Anim("Idle", 10.0f)
		.Param("fx.landTrauma", 0.25f)
		.OnEnter([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->StopMotion();
				p->ClearComboQueue();
				p->ConsumeCounterHit();

				// 高所からの着地はカメラを揺らす
				if (p->ConsumeHardLanding()) p->AddCameraTrauma(ctx.Param("fx.landTrauma"_sid));

				auto& pe = p->GetComponentManager().get<PostEffectComponent>(p->GetEntity());
				pe.AllStopEnabled();
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();

				// 速度0でも呼ぶのは、接地維持の下向きの力を毎フレーム与えるため
				p->ApplyGroundedMotion(DeltaTime(), 0.0f);

				if (p->IsMoving()) ctx.ChangeState("Walk"_sid);
			});

	sys->State("Walk").Base("Grounded")
		.Anim("Walk", 10.0f)
		.Param("moveSpeed", 5.0f)
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->ApplyGroundedMotion(DeltaTime(), ctx.Param("moveSpeed"_sid));

				if (!p->IsMoving())      ctx.ChangeState("Idle"_sid);
				else if (p->IsDashing()) ctx.ChangeState("Run"_sid);
			});

	sys->State("Run").Base("Grounded")
		.Anim("Dash", 8.0f)
		.Param("moveSpeed", 30.0f)
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->ApplyGroundedMotion(DeltaTime(), ctx.Param("moveSpeed"_sid));
				if (!p->IsMoving())       ctx.ChangeState("Idle"_sid);
				else if (!p->IsDashing()) ctx.ChangeState("Walk"_sid);
			});

	// 全力鳴き
	sys->State("CryFull").Base("Action")
		.Anim("Say", 2.2f, false)
		.AllowFrom({ "Idle"_sid, "Walk"_sid, "Run"_sid })
		.MinTime(0.3f)
		.Buffer(0.3f)
		.Param("chargeTime", 0.65f)
		.Param("cancelFrom", 1.10f)
		.Param("endTime", 1.35f)
		// 演出
		.Param("effect", "explosion")
		.Param("fx.lift", 1.0_m)
		.Param("fx.radialBlur", 2.0f)
		.Param("fx.aberration", 0.5f)
		.Param("fx.lightExtent", 20.0f)
		.Param("fx.lightPower", 1.0f)
		.Param("fx.light.r", 0.0f).Param("fx.light.g", 0.5f).Param("fx.light.b", 1.0f)
		.OnEnter([](StateContext& ctx)
			{
				// 音は4層を同時に鳴らす(低・中・高の声と効果音)
				auto& audio = AudioManager::Instance();
				audio.Play("fullCryLow");
				audio.Play("fullCryMid");
				audio.Play("fullCryHigh");
				audio.Play("fullCrySE");

				auto* p = ctx.As<Player>();
				p->StopMotion();
				p->ClearComboQueue();
				p->SetCry(true);
				GPUParticleSystem::Instance().PlayEffect(ctx.ParamStr("effect"_sid),
					p->GetPosition() + Math::Vector3(0.0f, ctx.Param("fx.lift"_sid), 0.0f));

				auto& pe = PostEffect(p);
				pe.radialBlur.enabled = true;
				pe.radialBlur.strength = ctx.Param("fx.radialBlur"_sid);

				pe.abberation.enabled = true;
				pe.abberation.strength = ctx.Param("fx.aberration"_sid);

				auto& lc = Light(p);
				lc.enabled = true;
				lc.range = ctx.Param("fx.lightExtent"_sid);
				lc.power = ctx.Param("fx.lightPower"_sid);
				lc.color = ParamColor(ctx, "fx.light");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();
				const float charge = ctx.Param("chargeTime"_sid);

				p->StopMotion();
				p->ApplyLandingPose();

				// ---- 溜め ----
				if (t < charge)
				{
					p->ApplyCryCharge(t / charge);
				}
				// ---- 解放(1回だけ) ----
				else if (!p->ConsumeComboQueue())
				{
					p->EmitCry(true);
					p->QueueComboInput();
				}

				if (t >= ctx.Param("cancelFrom"_sid))
				{
					p->OpenCancelWindow();

					if (p->ConsumeAttackRequest())
					{
						ctx.ChangeStateForced("Attack1"_sid);
						return;
					}
				}

				if (t >= ctx.Param("endTime"_sid))
					ctx.ChangeStateForced("Idle"_sid);
			})
		.OnExit([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->ClearComboQueue();
				p->SetCry(false);

				Light(p).enabled = false;
				PostEffect(p).AllStopEnabled();
			}
		);
	// =======================================================
	// アクションの子: 回避
	// =======================================================
	sys->State("Dodge").Base("Action")
		.Anim("Rolling", 2.0f, false)
		.Param("dodgeSpeed", 80.0f)
		.Param("duration", 0.15f)
		.MinTime(0.15f)
		.Buffer(0.25f)
		// 演出
		.Param("effect", "Dodge")
		.Param("sound", "dodge")
		.Param("ghostMaterial", "TestEmissive")
		.Param("fx.ghostParam", -0.4f)		// マテリアルの0番パラメータ(残像の濃さ)
		.Param("fx.ghostLife", 1.0f)
		.Param("fx.radialBlur", 0.3f)
		.Param("fx.aberration", 0.3f)
		.Param("trailEffect", "Dash")
		.Param("fx.lift", 1.5_m)
		.CanEnter([](const StateContext& ctx)
			{
				return ctx.As<Player>()->IsDodgeReady();
			})
		.OnEnter([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();

				GPUParticleSystem::Instance().PlayEffect(ctx.ParamStr("effect"_sid), p->GetPosition());
				p->BeginDodge(ctx.Param("dodgeSpeed"_sid));

				AudioManager::Instance().Play(ctx.ParamStr("sound"_sid));

				// 残像。描画パラメータの0番で残像の濃さを決めている
				constexpr int kGhostParamSlot = 0;
				auto& g = p->GetComponentManager().get<GhostTrailComponent>(p->GetEntity());
				BeginGhostTrail(g, ctx.ParamStr("ghostMaterial"_sid));
				SetParam(Render(p), kGhostParamSlot, ctx.Param("fx.ghostParam"_sid));
				g.lifeTime = ctx.Param("fx.ghostLife"_sid);

				auto& pe = PostEffect(p);
				pe.radialBlur.enabled = true;
				pe.radialBlur.strength = ctx.Param("fx.radialBlur"_sid);

				pe.abberation.enabled = true;
				pe.abberation.strength = ctx.Param("fx.aberration"_sid);
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->ApplyLandingPose();
				GPUParticleSystem::Instance().PlayEffect(ctx.ParamStr("trailEffect"_sid),
					p->GetPosition() + Math::Vector3(0.0f, ctx.Param("fx.lift"_sid), 0.0f));
				// ジャスト回避成立中の攻撃入力 → カウンター。
				// MinTime を張っているので Forced で抜ける。
				if (p->IsCounterReady() && p->ConsumeAttackRequest())
				{
					ctx.ChangeStateForced("Counter"_sid);
					return;
				}

				p->ApplyDodgeMotion(DeltaTime());

				const float duration = ctx.Param("duration"_sid);
				const float t = ctx.Time();

				// 終了時の接地判定で戻り先を自分で決める。
				// カウンター受付が残っていても、続きは Grounded/Airborne 側が拾う。
				if (t >= duration)
					ctx.ChangeStateForced("Grounded"_sid);
			})
		.OnExit([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->EndDodge();
				auto& g = p->GetComponentManager().get<GhostTrailComponent>(p->GetEntity());
				EndGhostTrail(g);

				PostEffect(p).AllStopEnabled();
			});

	// =======================================================
	// アクションの子: 3段コンボ
	// =======================================================
	sys->State("Attack1").Base("Action")
		.Anim("ClawAttack", 4.0f, false)
		.AllowFrom({ "Idle"_sid, "Walk"_sid, "Run"_sid })
		.MinTime(0.12f)
		.Buffer(0.3f)
		.Param("damage", 15.0f)
		.Param("reach", 15.0_m)
		.Param("radius", 2.6_m)
		.Param("knockback", 100.0_mps)
		.Param("keepDist", 4.0_m)
		.Param("maxStep", 125.0_m)
		.Param("stepTime", 0.05f)
		.Param("hitStart", 0.04f)
		.Param("hitEnd", 0.18f)
		.Param("chainStart", 0.10f)
		.Param("endTime", 0.30f)
		.Param("cancelFrom", 0.18f)		
		.Param("confirmScale", 0.6f)	
		.Param("hitstop", 5.0f)	// 元はフォールバックだけで使われていた値
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Slash1").Param("swing.life", 0.20f).Param("swing.width", 1.0f)
		.Param("swing.top", 4.0_m).Param("swing.bottom", -1.5_m).Param("swing.yaw", 85.0f)
		.Param("swing.sec", 0.14f).Param("swing.inner", 0.65f).Param("swing.outer", 1.45f)
		.OnEnter([](StateContext& ctx)
			{
				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				// 今カメラが狙っている敵を優先して踏み込む
				ctx.As<Player>()->BeginAttackStep(true,
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));

				ctx.As<Player>()->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();

				p->ApplyLandingPose();

				p->ApplyStepMotion(DeltaTime());

				if (t >= ctx.Param("hitStart"_sid) && t < ctx.Param("hitEnd"_sid))
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kNormal);
				}

				// 当てたときだけ後半を縮める
				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeAttackRequest())
					p->QueueComboInput();

				// 弱から強へ派生できる。コンボの分岐を作る。
				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeHeavyRequest())
				{
					ctx.ChangeStateForced("Heavy2"_sid);
					return;
				}

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced(p->ConsumeComboQueue() ? "Attack2"_sid : "Idle"_sid);
			});

	sys->State("Attack2").Base("Action")
		.Anim("ClawAttack_Combo2", 4.5f, false)
		.MinTime(0.12f)
		.Param("damage", 22.0f)
		.Param("reach", 19.0_m)
		.Param("radius", 3.2_m)
		.Param("knockback", 130.0_mps)
		.Param("hitstop", 7.0f)
		.Param("keepDist", 4.0_m)
		.Param("maxStep", 100.0_m)
		.Param("stepTime", 0.05f)
		.Param("hitStart", 0.05f)
		.Param("hitEnd", 0.20f)
		.Param("chainStart", 0.12f)
		.Param("endTime", 0.35f)
		.Param("cancelFrom", 0.2f)
		.Param("confirmScale", 0.6f)
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Slash2").Param("swing.life", 0.22f).Param("swing.width", 1.0f)
		.Param("swing.top", 4.0_m).Param("swing.bottom", -1.5_m).Param("swing.yaw", -85.0f)
		.Param("swing.sec", 0.15f).Param("swing.inner", 0.65f).Param("swing.outer", 1.45f)
		.OnEnter([](StateContext& ctx)
			{

				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				// 2段目以降は常に一番近い敵へ踏み込む
				ctx.As<Player>()->BeginAttackStep(false,
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));

				ctx.As<Player>()->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();

				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				p->ApplyLandingPose();

				p->ApplyStepMotion(DeltaTime());

				if (t >= ctx.Param("hitStart"_sid) && t < ctx.Param("hitEnd"_sid))
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kNormal);
				}

				// 判定を出し切ってから回避で抜けられる
				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeAttackRequest())
					p->QueueComboInput();

				// 弱から強へ派生できる。コンボの分岐を作る。
				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeHeavyRequest())
				{
					ctx.ChangeStateForced("Heavy2"_sid);
					return;
				}

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced(p->ConsumeComboQueue() ? "Attack3"_sid : "Idle"_sid);
			});

	// 締め
	sys->State("Attack3").Base("Action")
		.Anim("ClawAttack_Combo3", 3.0f, false)
		.MinTime(0.65f)
		.Param("damage", 34.0f)
		.Param("reach", 26.0_m)
		.Param("radius", 4.2_m)
		.Param("knockback", 260.0_mps)		
		.Param("hitstop", 14.0f)		
		.Param("launch", 1.4f)			
		.Param("keepDist", 4.5_m)
		.Param("maxStep", 100.0_m)
		.Param("stepTime", 0.05f)
		.Param("hitStart", 0.15f)		
		.Param("hitEnd", 0.30f)
		.Param("endTime", 0.65f)
		.Param("focusFov", 52.0f)
		.Param("fx.focusLambda", 14.0f)	// 寄せるときの速さ
		.Param("cancelFrom", 0.6f)		
		.Param("confirmScale", 1.0f)	
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Slash3").Param("swing.life", 0.35f).Param("swing.width", 1.0f)
		.Param("swing.top", 7.0_m).Param("swing.bottom", -2.0_m).Param("swing.yaw", 110.0f)
		.Param("swing.sec", 0.15f).Param("swing.inner", 0.60f).Param("swing.outer", 1.55f)
		.OnEnter([](StateContext& ctx)
			{

				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				auto* p = ctx.As<Player>();
				p->BeginAttackStep(false,
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));

				// 締めの間だけ画面を寄せる
				p->PushCameraFocus(ctx.Param("focusFov"_sid), ctx.Param("fx.focusLambda"_sid));

				ctx.As<Player>()->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();

				p->ApplyLandingPose();

				p->ApplyStepMotion(DeltaTime());

				if (t >= ctx.Param("hitStart"_sid) && t < ctx.Param("hitEnd"_sid))
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kCombo3,
						ctx.Param("launch"_sid));
				}

				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				p->ConsumeAttackRequest();	

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced("Idle"_sid);
			})
		.OnExit([](StateContext& ctx) { ctx.As<Player>()->PopCameraFocus(); });

	// =======================================================
	// 強攻撃
	// =======================================================
	sys->State("Heavy1").Base("Action")
		.Anim("ClawAttack_Heavy", 2.6f, false)
		.AllowFrom({ "Idle"_sid, "Walk"_sid, "Run"_sid })
		.MinTime(0.18f)
		.Buffer(0.3f)
		.Param("damage", 56.0f)
		.Param("reach", 22.0_m)
		.Param("radius", 3.8_m)
		.Param("knockback", 190.0_mps)
		.Param("hitstop", 12.0f)
		.Param("launch", 0.9f)
		.Param("keepDist", 4.5_m)
		.Param("maxStep", 70.0_m)
		.Param("stepTime", 0.05f)
		.Param("hitStart", 0.22f)		
		.Param("hitEnd", 0.38f)
		.Param("chainStart", 0.26f)
		.Param("cancelFrom", 0.40f)
		.Param("confirmScale", 0.45f)
		.Param("endTime", 0.9f)
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Heavy").Param("swing.life", 0.35f).Param("swing.width", 1.2f)
		.Param("swing.top", 6.5_m).Param("swing.bottom", -2.0_m).Param("swing.yaw", 110.0f)
		.Param("swing.sec", 0.15f).Param("swing.inner", 0.60f).Param("swing.outer", 1.55f)
		.OnEnter([](StateContext& ctx)
			{
				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				ctx.As<Player>()->BeginHeavyStep(true,
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));

				ctx.As<Player>()->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();

				p->ApplyStepMotion(DeltaTime());
				p->ApplyLandingPose();

				if (t >= ctx.Param("hitStart"_sid) && t < ctx.Param("hitEnd"_sid))
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kHeavy,
						ctx.Param("launch"_sid));
				}

				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				// 強→強で繋ぐ
				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeHeavyRequest())
					p->QueueComboInput();

				// 強→弱にも戻せる。分岐を一方通行にしない。
				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeAttackRequest())
				{
					ctx.ChangeStateForced("Attack2"_sid);
					return;
				}

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced(p->ConsumeComboQueue() ? "Heavy2"_sid : "Idle"_sid);
			});

	// 強の2段目。締めとして打ち上げる。
	sys->State("Heavy2").Base("Action")
		.Anim("ClawAttack_Heavy", 2.2f, false)
		.MinTime(0.2f)
		.Param("damage", 60.0f)
		.Param("reach", 28.0_m)
		.Param("radius", 4.6_m)
		.Param("knockback", 280.0_mps)
		.Param("hitstop", 18.0f)
		.Param("launch", 1.6f)
		.Param("keepDist", 5.0_m)
		.Param("maxStep", 50.0_m)
		.Param("stepTime", 0.05f)
		.Param("hitStart", 0.26f)
		.Param("hitEnd", 0.44f)
		.Param("cancelFrom", 0.48f)
		.Param("confirmScale", 0.45f)
		.Param("endTime", 0.9f)
		.Param("focusFov", 50.0f)
		.Param("fx.focusLambda", 14.0f)	// 寄せるときの速さ
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Heavy").Param("swing.life", 0.35f).Param("swing.width", 1.2f)
		.Param("swing.top", 6.5_m).Param("swing.bottom", -2.0_m).Param("swing.yaw", 110.0f)
		.Param("swing.sec", 0.15f).Param("swing.inner", 0.60f).Param("swing.outer", 1.55f)
		.OnEnter([](StateContext& ctx)
			{
				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				auto* p = ctx.As<Player>();
				p->BeginHeavyStep(true,
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));

				p->PushCameraFocus(ctx.Param("focusFov"_sid), ctx.Param("fx.focusLambda"_sid));

				ctx.As<Player>()->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();

				p->ApplyStepMotion(DeltaTime());
				p->ApplyLandingPose();

				if (t >= ctx.Param("hitStart"_sid) && t < ctx.Param("hitEnd"_sid))
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kSmash,
						ctx.Param("launch"_sid));
				}

				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				p->ConsumeHeavyRequest();

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced("Idle"_sid);
			})
		.OnExit([](StateContext& ctx) { ctx.As<Player>()->PopCameraFocus(); });

	// =======================================================
	// アクションの子: カウンター(ジャスト回避からの反撃)
	// =======================================================
	sys->State("Counter").Base("Action")
		.Anim("Counter", 3.0f, false)
		.MinTime(0.15f)
		.Param("damage", 60.0f)
		.Param("reach", 28.0_m)
		.Param("radius", 4.2_m)
		.Param("knockback", 240.0_mps)
		.Param("hitstop", 12.0f)
		.Param("keepDist", 4.0_m)
		.Param("maxStep", 180.0_m)
		.Param("stepTime", 0.04f)
		.Param("hitStart", 0.04f)
		.Param("hitEnd", 0.24f)
		.Param("chainStart", 0.12f)
		.Param("endTime", 0.55f)
		.Param("confirmScale", 0.7f)	// 元はフォールバックだけで使われていた値
		.Param("cancelFrom", 0.24f)	// 元はフォールバックだけで使われていた値
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Counter").Param("swing.life", 0.35f).Param("swing.width", 1.0f)
		.Param("swing.top", 1.0_m).Param("swing.bottom", -0.5_m).Param("swing.yaw", 0.0f)
		.Param("swing.sec", 0.10f).Param("swing.inner", 0.60f).Param("swing.outer", 1.50f)
		.OnEnter([](StateContext& ctx)
			{
				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				AudioManager::Instance().Play("counter");

				auto* p = ctx.As<Player>();
				p->ConsumeCounterHit();

				// スロー解除・視野角復帰もこの中で行われる
				p->BeginCounterStep(
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));
				p->LockCancel();

				p->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();
				const float hitEnd = ctx.Param("hitEnd"_sid);

				p->ApplyLandingPose();

				p->ApplyStepMotion(DeltaTime());

				if (t >= ctx.Param("hitStart"_sid) && t < hitEnd)
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kCounter);
				}

				// 判定を出し切ってから命中を確認する
				if (t >= hitEnd && p->ConsumeCounterHit())
				{
					ctx.ChangeStateForced("Finisher"_sid);
					return;
				}

				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				if (t >= ctx.Param("chainStart"_sid) * scale && p->ConsumeAttackRequest())
					p->QueueComboInput();

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced(p->ConsumeComboQueue() ? "Attack2"_sid : "Idle"_sid);
			})
		.OnExit([](StateContext& ctx) {
				ctx.As<Player>()->EndPerfectDodge();
				ctx.As<Player>()->UnlockCancel();
			});

	// =======================================================
	// アクションの子: 追撃(カウンターが当たったときだけ入る)
	// =======================================================
	sys->State("Finisher").Base("Action")
		.Anim("Counter_Finish", 2.5f, false)
		.MinTime(0.2f)
		.Param("damage", 80.0f)
		.Param("reach", 30.0_m)
		.Param("radius", 4.8_m)
		.Param("knockback", 300.0_mps)
		.Param("hitstop", 20.0f)
		.Param("launch", 2.2f)			
		.Param("keepDist", 3.5_m)
		.Param("maxStep", 200.0_m)
		.Param("stepTime", 0.04f)
		.Param("hitStart", 0.10f)		
		.Param("hitEnd", 0.30f)
		.Param("endTime", 0.75f)
		.Param("focusFov", 46.0f)
		.Param("fx.focusLambda", 16.0f)	// 寄せるときの速さ
		.Param("fx.trauma", 0.5f)
		.Param("confirmScale", 0.7f)	// 元はフォールバックだけで使われていた値
		.Param("cancelFrom", 0.30f)	// 元はフォールバックだけで使われていた値
		// 振りの軌跡(トレイル)
		.Param("swing.asset", "Counter").Param("swing.life", 0.35f).Param("swing.width", 1.0f)
		.Param("swing.top", 1.0_m).Param("swing.bottom", -0.5_m).Param("swing.yaw", 0.0f)
		.Param("swing.sec", 0.10f).Param("swing.inner", 0.60f).Param("swing.outer", 1.50f)
		.OnEnter([](StateContext& ctx)
			{

				GPUParticleSystem::Instance().PlayEffect("Attack", ctx.As<Player>()->GetPosition());

				AudioManager::Instance().Play("counter");

				auto* p = ctx.As<Player>();

				p->ApplyLandingPose();

				// カウンターと同じ相手へもう一度踏み込む
				p->BeginCounterStep(
					ctx.Param("keepDist"_sid),
					ctx.Param("maxStep"_sid),
					ctx.Param("stepTime"_sid));

				p->PushCameraFocus(ctx.Param("focusFov"_sid), ctx.Param("fx.focusLambda"_sid));
				p->AddCameraTrauma(ctx.Param("fx.trauma"_sid));
				p->LockCancel();

				p->BeginAttackTrail(MakeSwing(ctx));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float t = ctx.Time();

				p->ApplyStepMotion(DeltaTime());

				if (t >= ctx.Param("hitStart"_sid) && t < ctx.Param("hitEnd"_sid))
				{
					p->SpawnAttackHitbox(
						ctx.Param("damage"_sid),
						ctx.Param("reach"_sid),
						ctx.Param("radius"_sid),
						ctx.Param("knockback"_sid),
						static_cast<int>(ctx.Param("hitstop"_sid)),
						PlayerAttackTag::kFinisher,
						ctx.Param("launch"_sid));
				}

				const float scale = p->DidAttackLand() ? ctx.Param("confirmScale"_sid) : 1.0f;

				if (t >= ctx.Param("cancelFrom"_sid) * scale)
					p->OpenCancelWindow();

				p->ConsumeAttackRequest();
				p->ConsumeCounterHit();		

				if (t >= ctx.Param("endTime"_sid) * scale)
					ctx.ChangeStateForced("Idle"_sid);
			})
		.OnExit([](StateContext& ctx) {
				ctx.As<Player>()->PopCameraFocus();
				ctx.As<Player>()->UnlockCancel();
			});

	// ---- 被弾 ----
	sys->State("Hurt").Base("Action")
		.Anim("Idle", 2.0f, false)
		.MinTime(0.10f)
		.Param("recoverFrom", 0.72f)	// よろけの終盤。この割合から回避で抜けられる
		.Param("sound", "hit")
		.Param("effect", "TakeDamage")
		.Param("damageMaterial", "Damage")
		.Param("fx.vignetteEase", 0.15f)
		.Param("fx.vignetteStrength", 0.4f)
		.Param("fx.vignette.r", 0.5f).Param("fx.vignette.g", 0.1f).Param("fx.vignette.b", 0.05f)
		.OnEnter([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();

				AudioManager::Instance().Play(ctx.ParamStr("sound"_sid));
				GPUParticleSystem::Instance().PlayEffect(ctx.ParamStr("effect"_sid), p->GetPosition());

				SetMaterial(Render(p), ctx.ParamStr("damageMaterial"_sid));

				// 画面の縁を赤くする
				const Math::Vector3 c = ParamColor(ctx, "fx.vignette");
				auto& pe = PostEffect(p);
				pe.vignette.enabled = true;
				pe.vignette.easeType = EaseType::Linear;
				pe.vignette.easeDuration = ctx.Param("fx.vignetteEase"_sid);
				pe.vignette.color = { c.x, c.y, c.z, 1.0f };
				pe.vignette.strength = ctx.Param("fx.vignetteStrength"_sid);

				// 入力の予約を全て捨てる
				p->ConsumeAttackRequest();
				p->ConsumeHeavyRequest();
				p->ConsumeDodgeRequest();
				p->ConsumeCryRequest();
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				const float dt = DeltaTime();
				const float dur = p->GetHurtDuration();
				const float t = ctx.Time();

				// 長さ 0 のよろけで割らないように下限を置く
				constexpr float kMinDuration = 0.01f;

				p->ApplyHurtMotion(dt);
				p->ApplyHurtPose(t / std::max(dur, kMinDuration));

				// 終盤だけ回避で抜けられる
				if (t >= dur * ctx.Param("recoverFrom"_sid) && p->ConsumeDodgeRequest())
				{
					ctx.ChangeStateForced("Dodge"_sid);
					return;
				}

				if (t >= dur)
					ctx.ChangeStateForced("Idle"_sid);
			})
		.OnExit([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->UnlockCancel();

				// 普段のマテリアルへ戻す(PlayerTuning の materialName)
				SetMaterial(Render(p), p->GetTuning().materialName.c_str());

				PostEffect(p).AllStopEnabled();
			});

	sys->State("Dead").Base("Action")
		.Anim("Dying", 4.0f, false)
		.Buffer(0.3f)
		.Param("endDelay", 1.0f)		// 倒れてから「終わった」扱いにするまで
		.Param("fx.saturation", 0.1f)		// 画面の彩度を落とす
		.OnUpdate([](StateContext& ctx)
			{
				auto* p = ctx.As<Player>();
				p->StopMotion();

				auto& pe = PostEffect(p);
				pe.screenTone.enabled = true;
				pe.screenTone.saturation = ctx.Param("fx.saturation"_sid);

				if (p->IsActive() && ctx.Time() >= ctx.Param("endDelay"_sid))
					p->EndActive();
			});

	sys->SetEntryState("Idle");

	sys->SetDefaultChild("Grounded", "Idle");
	sys->SetDefaultChild("Action", "Dodge");
	sys->RegisterEventName("Hit");

	// ここまでがコード側の既定値。PlayerStates.json に書いた値で上書きする
	StateTuning::Apply(*sys, PlayerTuningKey::kStatesFile, kParamUnits);

	// 定義ミスを起動時に洗い出す
	std::string err;
	if (!sys->Validate(&err))
	{
		assert(false && "Playerステート定義に問題があります");
	}

	GameScale::ApplyToStateParams(sys);
}