#include "Pch.h"
#include "EnemyStateSetup.h"
#include "../../../../GameSystem/StateMachine/StateMachineManager.h"	
#include "../Core/EnemyBase.h"
#include "../../../../GameSystem/Combat/CombatFx.h"
#include "../../../Config/StateTuning.h"

using namespace StateLiterals;

namespace {

	// ※ここにあった kThrustSwing / kSweepSwing は、コピーした変数がどこにも渡されておらず
	//   実際には使われていなかったので消した(要確認リスト参照)

	// ひよこを咥えた敵が走り去る方向。ステージの奥(-Z)がフィールドの外になっている
	constexpr Math::Vector3 kCarryFleeDir = { 0.0f, 0.0f, -1.0f };

	// 遷移の優先度。数字が大きいほど強い(同じフレームに複数の遷移が来たら強いほうが残る)
	namespace Priority
	{
		constexpr uint8_t kAlert = 5;		// 被弾などで警戒へ
		constexpr uint8_t kInvestigate = 7;	// 仲間に呼ばれて捜索へ
		constexpr uint8_t kAttack = 8;		// 溜め / 攻撃通知
		constexpr uint8_t kCall = 9;		// 仲間を呼ぶ
		constexpr uint8_t kChase = 10;		// 見つけたら最優先で追う
	}

	// 敵の params は縮尺前の値をそのまま持ち、GameScale::ApplyToStateParams に任せている。
	// なので JSON 側でも単位変換はしない
	const StateTuning::ParamUnits kParamUnits = { {}, {} };

	// 割り算に使う時間の下限
	constexpr float kMinDuration = 0.01f;

	// params の "<名前>.r/g/b" を色にする
	Math::Vector3 ParamColor(const StateContext& ctx, std::string_view _name)
	{
		const std::string base(_name);
		return {
			ctx.Param(MakeStateId(base + ".r")),
			ctx.Param(MakeStateId(base + ".g")),
			ctx.Param(MakeStateId(base + ".b")) };
	}

	LightComponent& Light(EnemyBase* _e) { return _e->GetComponentManager().get<LightComponent>(_e->GetEntity()); }

	// 突き(距離がある)か薙ぎ(近い)かを相手との距離で選ぶ
	bool WantsThrust(EnemyBase* _e, float _thrustMinDistance) { return _e->GetTargetDistance() >= _thrustMinDistance; }

	// 死亡は何よりも優先
	bool TryDeath(StateContext& ctx, EnemyBase* _e)
	{
		if (!_e->ConsumeDeathRequest()) return false;

		_e->ReleaseAttackToken();

		ctx.ChangeStateForced("Fall"_sid);
		return true;
	}

	// 士気が折れたら距離を取る
	bool TryFlee(StateContext& ctx, EnemyBase* _e)
	{
		if (!_e->WantsToFlee()) return false;

		_e->ReleaseAttackToken();
		_e->ClearApproachPoint();
		ctx.ChangeStateForced("Flee"_sid);
		return true;
	}

	bool TryReaction(StateContext& ctx, EnemyBase* _e)
	{
		static const auto kStates = std::array{
			"Stagger"_sid, "Blow"_sid, "Launch"_sid, "Down"_sid };

		const int level = _e->ConsumeReactionLevel();
		if (level <= 0) return false;

		_e->ReleaseAttackToken();

		const int i = std::clamp(level, 1, static_cast<int>(kStates.size())) - 1;

		// Forced なのは Attack の子が MinTime を持っているため
		ctx.ChangeStateForced(kStates[i]);
		return true;
	}

	bool TryFreeze(StateContext& ctx, EnemyBase* _e)
	{
		if (!_e->ConsumeFreezeRequest()) return false;

		ctx.ChangeStateForced("Frozen"_sid);
		return true;
	}

	bool TryCarry(StateContext& ctx, EnemyBase* _e)
	{
		if (!_e->ConsumeCarryRequest()) return false;

		_e->ReleaseAttackToken();
		_e->ClearApproachPoint();

		// Forced なのは Attack の子が MinTime を持っているため
		ctx.ChangeStateForced("Carry"_sid);
		return true;
	}

	// 硬直明けの戻り先
	void RecoverFrom(StateContext& ctx, EnemyBase* _e)
	{
		_e->EndHitPose();
		_e->ResetGravityScale();
		ctx.ChangeStateForced(_e->IsTargetInSight() ? "Chase"_sid : "Alert"_sid);
	}

	// 走り去り中に溜まった要求を捨てる。残すと消える直前に別ステートへ飛ぶ。
	void DiscardRequests(EnemyBase* _e)
	{
		_e->ConsumeDeathRequest();
		_e->ConsumeFreezeRequest();
		_e->ConsumeShatterRequest();
		_e->ConsumeReactionLevel();
		_e->ConsumeAlertRequest();
		_e->ConsumeLandedFlag();
		_e->ConsumeCallReceived();
		_e->ConsumeCarryRequest();
	}
}


void SetupEnemyStates()
{
	auto* sys = SM_MGR.CreateSystem("Enemy");

	// 既に登録済みなら何もしない
	if (sys->GetStateCount() > 0) return;

	// =======================================================
	// ベース
	// =======================================================

	// ---- 通常 ----
	sys->State("Normal").Tag(EnemyTag::kNormal)
		.Anim("Walk")
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				if (TryDeath(ctx, e)) return;
				if (TryCarry(ctx, e)) return;
				if (TryFlee(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				// 発見したら追跡へ
				if (e->IsTargetInSight()) { e->ConsumeCallReceived(); ctx.ChangeState("Chase"_sid, Priority::kChase); return; }

				// 仲間に呼ばれたら、その位置まで見に行く
				if (e->ConsumeCallReceived()) { ctx.ChangeState("Investigate"_sid, Priority::kInvestigate); return; }

				// 攻撃を受けた等で警戒だけ入れたい場合
				if (e->ConsumeAlertRequest()) { ctx.ChangeState("Alert"_sid, Priority::kAlert); return; }
			});

	// ---- 追跡 ----
	sys->State("Chase").Tag(EnemyTag::kChase)
		.Anim("Dash")
		.Param("attackRange", 6.0f)
		.Param("wishRange", 18.0f)		// 外周スロット(リング半径+個体差+重なり)より広く取る
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				// 被弾は何より優先
				if (TryDeath(ctx, e)) return;
				if (TryCarry(ctx, e)) return;
				if (TryFlee(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				e->ConsumeAlertRequest();
				e->ConsumeLandedFlag();

				// 見失ったら警戒へ
				if (!e->IsTargetInSight()) { e->ReleaseAttackToken(); ctx.ChangeState("Alert"_sid); return; }

				e->MarkLastKnown();

				// 仲間を呼ぶ
				if (e->WantsToCall()) { ctx.ChangeState("Call"_sid, Priority::kCall); return; }

				const float dist = e->GetTargetDistance();

				// 持ち場に着いているなら距離に関わらず要求する。
				// 外周に回された敵が wishRange の外に出ると、
				// 本命枠が空いたまま誰も殴りに来ない時間ができる。
				if (e->IsAtApproachPoint()
					|| dist <= e->AttackReach(ctx.Param("wishRange"_sid)))
					e->RequestAttackToken();

				if (e->HasAttackToken() && dist <= e->AttackReach(ctx.Param("attackRange"_sid)))
				{

					if (e->IsHarassRole())
					{
						e->ClearWindupTotal();
						ctx.ChangeState("Notice"_sid, Priority::kAttack);
					}
					else
					{
						ctx.ChangeState("Windup"_sid, Priority::kAttack);
					}
					return;
				}
			});

	// ---- 警戒 ----
	sys->State("Alert").Tag(EnemyTag::kAlert)
		.Anim("Threat")
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				if (TryDeath(ctx, e)) return;
				if (TryCarry(ctx, e)) return;
				if (TryFlee(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				e->ConsumeAlertRequest();
				e->ConsumeLandedFlag();

				// 再発見したら追跡へ復帰
				if (e->IsTargetInSight()) { e->ConsumeCallReceived(); ctx.ChangeState("Chase"_sid, Priority::kChase); return; }

				// 仲間の呼び声。見回しを打ち切って、呼ばれた位置まで足を運ぶ。
				if (e->ConsumeCallReceived()) ctx.ChangeState("Investigate"_sid, Priority::kInvestigate);
			});

	// =======================================================
	// 通常の子
	// =======================================================

	// うろつき
	sys->State("Wander").Base("Normal")
		.Param("moveSpeed", 10.0f)
		.Param("turnRate", 3.0f)
		.Param("restRate", 0.3f)
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->RefreshWanderDir(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				if (TryDeath(ctx, e)) return;
				if (TryCarry(ctx, e)) return;
				if (TryFlee(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->SetTurnRate(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyMotion(dt, e->MoveSpeed(ctx.Param("moveSpeed"_sid)));

				if (e->ConsumeLandedFlag()) { e->RefreshWanderDir(); return; }

				if (e->TickWanderTimer(dt))
				{
					if (KdRandom::GetFloat(0.0f, 1.0f) < ctx.Param("restRate"_sid))
						ctx.ChangeState("Rest"_sid);
					else
						e->RefreshWanderDir();
				}
			});

	// 立ち止まり
	sys->State("Rest").Base("Normal")
		.Anim("Idle", 10.0f, true)
		.Param("restTime", 1.5f)
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->ConsumeLandedFlag();

				// 速度0でも呼ぶのは重力とノックバック減衰を進めるため
				e->ApplyMotion(Time::Instance().GetDeltaTime(), 0.0f);

				if (ctx.Time() >= ctx.Param("restTime"_sid))
					ctx.ChangeState("Wander"_sid);
			});

	// =======================================================
	// 追跡の子
	// =======================================================
	sys->State("Pursue").Base("Chase")
		.Param("moveSpeed", 22.0f)
		.Param("turnRate", 12.0f)
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 向きは常に相手
				e->FaceTarget(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyApproachMotion(dt, e->MoveSpeed(ctx.Param("moveSpeed"_sid)));

				if (e->IsAtApproachPoint()) ctx.ChangeState("Keep"_sid);
			});

	// 持ち場で待機(足を止めて向き続ける)
	sys->State("Keep").Base("Chase")
		.Anim("Threat", 10.0f, true)
		.Param("turnRate", 12.0f)
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->FaceTarget(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyApproachMotion(dt, 0.0f);

				// 持ち場から押し出された/プレイヤーが動いた
				if (e->IsOffApproachPoint()) ctx.ChangeState("Pursue"_sid);
			});

	// 呼ぶ
	sys->State("Call").Base("Chase")
		.Anim("Threat", 10.0f, true)
		.Param("callTime", 0.7f)
		.Param("turnRate", 6.0f)
		.Param("fx.trauma", 0.12f)
		.MinTime(0.5f)
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				e->StopMotion();
				e->ReleaseAttackToken();
				e->BeginCall();
				CombatFx::Instance().AddTrauma(ctx.Param("fx.trauma"_sid));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				const float callTime = ctx.Param("callTime"_sid);
				e->ApplyCallPose((callTime > 0.0f) ? (ctx.Time() / callTime) : 1.0f);

				e->FaceTarget(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyMotion(dt, 0.0f);

				if (ctx.Time() >= callTime) ctx.ChangeState("Pursue"_sid);
			})

		.OnExit([](StateContext& ctx) { ctx.As<EnemyBase>()->EndCallPose(); });

	// =======================================================
	// 警戒の子
	// =======================================================
	sys->State("LookAround").Base("Alert")
		.Anim("Idle", 10.0f, true)
		.Param("alertTime", 3.0f)
		.Param("turnRate", 8.0f)
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->BeginLookAround(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->SetTurnRate(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyMotion(dt, 0.0f);	// 足は止める

				if (ctx.Time() >= ctx.Param("alertTime"_sid))
					ctx.ChangeState("Normal"_sid);
			});

	// 捜索
	sys->State("Investigate").Base("Alert")
		.Anim("Dash", 10.0f, true)
		.Param("moveSpeed", 16.0f)
		.Param("turnRate", 8.0f)
		.Param("arriveSlack", 2.0f)
		.Param("giveUpTime", 6.0f)	// 保険
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->ConsumeLandedFlag();

				// 旋回を待たずに歩き出す
				e->FaceLastKnown(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyChaseMotion(dt, e->MoveSpeed(ctx.Param("moveSpeed"_sid)));

				if (e->GetLastKnownDistance() <= ctx.Param("arriveSlack"_sid)
					|| ctx.Time() >= ctx.Param("giveUpTime"_sid))
					ctx.ChangeState("LookAround"_sid);
			});

	// ---- 攻撃 ----
	sys->State("Attack").Tag(EnemyTag::kAttack)
		.Anim("Threat", 10, false)
		.OnEnter([](StateContext& ctx)
			{
				AudioManager::Instance().Play("enemyAttack");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				if (TryDeath(ctx, e)) return;
				if (TryCarry(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				// 攻撃中は権利を持ち続ける
				e->RequestAttackToken();
				e->ConsumeLandedFlag();

				// 攻撃中に被弾したら中断する
				if (e->ConsumeAlertRequest())
				{
					e->ReleaseAttackToken();
					ctx.ChangeStateForced(e->IsTargetInSight() ? "Chase"_sid : "Alert"_sid);
				}
			});

	// =======================================================
	// 被弾リアクション
	// =======================================================
	sys->State("Damage").Tag(EnemyTag::kDamage)
		.Anim("Hit", 10.0f, false)
		.OnEnter([](StateContext& ctx)
			{
				GPUParticleSystem::Instance().PlayEffect("Hit", ctx.As<EnemyBase>()->GetPosition());
				auto& rc = ctx.As<EnemyBase>()->GetComponentManager().get<RenderComponent>(ctx.As<EnemyBase>()->GetEntity());
				SetMaterial(rc, "Damage");

			}
		)
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				// 硬直中の追撃
				if (TryDeath(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				e->ConsumeAlertRequest();
			})
		.OnExit([](StateContext& ctx)
			{
				auto& rc = ctx.As<EnemyBase>()->GetComponentManager().get<RenderComponent>(ctx.As<EnemyBase>()->GetEntity());
				SetMaterial(rc, ctx.As<EnemyBase>()->GetSpec().defaultMaterial);
			});

	// 軽い怯み
	sys->State("Stagger").Base("Damage")
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->StopMotion(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->ConsumeLandedFlag();
				e->ApplyHitPose();
				e->ApplyMotion(dt, 0.0f);

				if (e->TickStagger(dt)) RecoverFrom(ctx, e);
			})
		.OnExit([](StateContext& ctx)
			{
				ctx.As<EnemyBase>()->EndHitPose();
			});

	// 吹き飛び
	sys->State("Blow").Base("Damage")
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				GPUParticleSystem::Instance().PlayEffect("Blow", e->GetPosition());
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->ConsumeLandedFlag();
				e->ApplyHitPose();
				e->ApplyMotion(dt, 0.0f);

				if (e->TickStagger(dt)) ctx.ChangeStateForced("Down"_sid);
			})
		.OnExit([](StateContext& ctx) { ctx.As<EnemyBase>()->EndHitPose(); });

	// 打ち上げ
	sys->State("Launch").Base("Damage")
		.Param("maxAirTime", 2.0f)
		.Param("fx.landHitstop", 0.08f)	// 落着の衝撃
		.Param("fx.landTrauma", 0.35f)
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				GPUParticleSystem::Instance().PlayEffect("Blow", e->GetPosition());
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->ApplyHitPose();
				e->ApplyMotion(dt, 0.0f);

				// 硬直タイマーは進めるが、抜けるのは着地が条件
				e->TickStagger(dt);

				if (e->ConsumeLandedFlag() || ctx.Time() >= ctx.Param("maxAirTime"_sid))
				{
					CombatFx::Instance().RequestHitStop(ctx.Param("fx.landHitstop"_sid));	// 落着の衝撃
					CombatFx::Instance().AddTrauma(ctx.Param("fx.landTrauma"_sid));
					ctx.ChangeStateForced("Down"_sid);
				}
			})
		.OnExit([](StateContext& ctx) { ctx.As<EnemyBase>()->EndHitPose(); });

	// ダウン
	sys->State("Down").Base("Damage")
		.Param("getUpTime", 1.0f)
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->StopMotion(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->ConsumeLandedFlag();
				e->ApplyMotion(dt, 0.0f);

				if (ctx.Time() >= ctx.Param("getUpTime"_sid))
					RecoverFrom(ctx, e);
			})
		.OnExit([](StateContext& ctx) { ctx.As<EnemyBase>()->EndHitPose(); });

	// =======================================================
	// 硬化(鳴き声)
	// =======================================================
	sys->State("Frozen").Base("Damage").Tag(EnemyTag::kFrozen)
		// 固まっている間は目を青く光らせる
		.Param("fx.light.r", 0.0f).Param("fx.light.g", 0.3f).Param("fx.light.b", 1.0f)
		.Param("fx.lightPower", 0.3f)
		.Param("fx.lightLift", 1.0f)	// ライトを置く高さ(目の高さからの差ではなく足元から)
		.Param("fx.effectLiftRatio", 1.5f)	// 硬化エフェクトを目の高さの何倍に置くか
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				auto& audio = AudioManager::Instance();
				audio.Play("frozen");

				e->StopMotion();
				GPUParticleSystem::Instance().PlayEffect("Frozen", e->GetPosition());

				// 硬質化状態のエフェクト
				GPUParticleSystem::Instance().StopEffect(e->GetEffectID());
				e->SetEffectID(
					GPUParticleSystem::Instance().SpawnEffect("Chaos",
						e->GetPosition() + Math::Vector3::Up * e->GetEyeHeight())
				);

				auto& lc = Light(e);
				lc.enabled = true;
				lc.color = ParamColor(ctx, "fx.light");
				lc.offset = { 0.0f, ctx.Param("fx.lightLift"_sid) - e->GetEyeHeight(), 0.0f };
				lc.power = ctx.Param("fx.lightPower"_sid);

				auto& rc = e->GetComponentManager().get<RenderComponent>(e->GetEntity());
				SetMaterial(rc, "Test");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());
				GPUParticleSystem::Instance().SetEffectPos(e->GetEffectID(), e->GetPosition() + Math::Vector3::Up * e->GetEyeHeight() * ctx.Param("fx.effectLiftRatio"_sid));

				e->ConsumeFreezeRequest();
				e->ConsumeAlertRequest();
				e->ConsumeLandedFlag();

				// ブレイク成功
				if (e->ConsumeShatterRequest())
				{
					e->EndFrozenPose();
					e->BurstFromFreeze(true);

					GPUParticleSystem::Instance().PlayEffect("Shatter", e->GetPosition());

					ctx.ChangeStateForced("Blow"_sid);
					return;
				}

				e->ConsumeReactionLevel();

				e->ApplyFrozenPose();
				e->ApplyMotion(dt, 0.0f);

				// 時間切れ
				if (e->TickFreeze(dt))
				{
					e->EndFrozenPose();
					e->BurstFromFreeze(false);
					ctx.ChangeStateForced("Blow"_sid);
				}
			})
		.OnExit([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				auto& audio = AudioManager::Instance();
				audio.Play("unFrozen");

				e->EndFrozenPose();

				// 通常エフェクトに戻す
				GPUParticleSystem::Instance().StopEffect(e->GetEffectID());

				GPUParticleSystem::Instance().PlayEffect("Unfrozen", e->GetPosition());

				auto& lc = e->GetComponentManager().get<LightComponent>(e->GetEntity());
				lc.enabled = false;

				auto& rc = e->GetComponentManager().get<RenderComponent>(e->GetEntity());
				// 硬化マテリアルの1番パラメータ(凍り具合)を戻す
				constexpr int   kFrozenParamSlot = 1;
				constexpr float kFrozenParamOff = 0.0f;
				SetParam(rc, kFrozenParamSlot, kFrozenParamOff);
				SetMaterial(rc, e->GetSpec().defaultMaterial);
			});

	// =======================================================
	// 死
	// =======================================================
	sys->State("Die").Tag(EnemyTag::kDie)
		.Anim("Die", 10.0f)
		.Param("fx.trauma", 0.4f)
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				e->EndWindup();
				e->EndHitPose();
				e->ClearApproachPoint();
				e->ReleaseAttackToken();

				GPUParticleSystem::Instance().PlayEffect("Destroy", e->GetPosition());

				AudioManager::Instance().Play("death");
				CombatFx::Instance().AddTrauma(ctx.Param("fx.trauma"_sid));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				e->ConsumeDeathRequest();
				e->ConsumeReactionLevel();
				e->ConsumeFreezeRequest();
				e->ConsumeShatterRequest();
				e->ConsumeAlertRequest();
				e->ConsumeLandedFlag();

				e->ApplyMotion(dt, 0.0f);

				e->TickCorpse(dt);
			});

	sys->State("Fall").Base("Die");


	// =======================================================
	// 逃走
	// =======================================================
	sys->State("Flee").Tag(EnemyTag::kFlee)
		.Anim("Dash")
		.Param("fleeTime", 2.5f)
		.Param("fleeDistance", 23.0f)	// これだけ離れたら落ち着く
		.Param("moveSpeed", 20.0f)
		.Param("turnRate", 9.0f)
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->ReleaseAttackToken();
				e->ClearApproachPoint();
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				if (TryDeath(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;
				if (TryReaction(ctx, e)) return;

				e->ConsumeAlertRequest();
				e->ConsumeLandedFlag();
				e->ConsumeCallReceived();	// 逃げている最中は呼ばれても行かない

				// 背を向けて走る
				e->FaceAwayFromTarget(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyChaseMotion(dt, e->MoveSpeed(ctx.Param("moveSpeed"_sid)));

				const bool farEnough = e->GetTargetDistance() >= ctx.Param("fleeDistance"_sid);
				if (!farEnough && ctx.Time() < ctx.Param("fleeTime"_sid)) return;

				// 立ち直る。しばらくは折れない。
				e->EndFlee();
				ctx.ChangeStateForced(e->IsTargetInSight() ? "Chase"_sid : "Alert"_sid);
			});

	// =======================================================
	// 連れ去り
	// =======================================================
	sys->State("Carry").Tag(EnemyTag::kCarry)
		.Anim("Dash")
		.Param("fx.trauma", 0.3f)
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				e->EndWindup();			// 溜めの途中で咥えた場合もスケールを戻す
				e->EndAttackNotice();
				e->ClearWindupTotal();

				CombatFx::Instance().AddTrauma(ctx.Param("fx.trauma"_sid));
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();

				// 走り去り中は何も受け付けない。ひよこも離さないし、ステートも抜けない。
				if (e->IsLeaving())
				{
					DiscardRequests(e);
					return;
				}

				// 死亡だけは通す。倒された時点で口は開く。
				if (TryDeath(ctx, e)) return;
				if (TryFreeze(ctx, e)) return;

				// 体幹を崩されて口を開いた。崩れの怯みを見せてから戦線へ戻る。
				// 咥え直しは起きない。
				if (!e->IsCarrying())
				{
					if (TryReaction(ctx, e)) return;
					ctx.ChangeStateForced(e->IsTargetInSight() ? "Chase"_sid : "Alert"_sid);
					return;
				}

				// 崩されるまでは怯みでは落とさない
				e->ConsumeReactionLevel();
				e->ConsumeAlertRequest();
				e->ConsumeLandedFlag();
				e->ConsumeCallReceived();
			});

	// ---- 運び去る ----
	// 入ったら抜けない
	sys->State("CarryRun").Base("Carry")
		.Anim("Dash", 10.0f, true)
		.Param("moveSpeed", 10.0f)
		.Param("turnRate", 7.0f)	// 逃走より鈍い
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->BeginLeave(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 決まった方向へ逃がす
				e->FaceDirection(kCarryFleeDir,
					e->TurnRate(ctx.Param("turnRate"_sid)));

				// 重力なし。当たり判定を切っているので床も無い扱いになる。
				e->ApplyChaseMotion(dt,
					e->CarrySpeed(e->MoveSpeed(ctx.Param("moveSpeed"_sid))), false);

				e->TickLeave(dt);
			});

	// ---- うろつき ----
	sys->State("CarryWander").Base("Carry")
		.Anim("Walk", 10.0f, true)
		.Param("moveSpeed", 8.0f)
		.Param("turnRate", 3.0f)
		.Param("runDelay", 5.0f)	// 咥えてからこれだけ経ったら走り去る
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->RefreshWanderDir(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 時間はステート側で数える。DelayCall に ctx を渡すと、
				// 待っている間に倒された/固まった敵まで走り去らせてしまう。
				if (ctx.Time() >= ctx.Param("runDelay"_sid))
				{
					ctx.ChangeState("CarryRun"_sid);
					return;
				}

				e->SetTurnRate(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyMotion(dt, e->MoveSpeed(ctx.Param("moveSpeed"_sid)));

				// 壁に当たった/段差から落ちた時に向きを引き直す
				if (e->ConsumeLandedFlag()) { e->RefreshWanderDir(); return; }

				if (e->TickWanderTimer(dt)) e->RefreshWanderDir();
			});

	// =======================================================
	// 攻撃の子
	// =======================================================

	// 構え(相手を向く猶予)
	sys->State("Windup").Base("Attack")
		.Anim("Threat", 10, true)
		.Param("backUntil", 0.6f)	// 溜めのこの割合までは後ろへ引く
		.Param("windupTime", 0.5f)
		.Param("turnRate", 10.0f)
		.Param("backSpeed", 12.0f)
		.MinTime(0.05f)
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->StopMotion();
				e->BeginWindup();
				e->SetSuperArmor(true);
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 溜めの長さは種類ごとに変える。長いほど読みやすい敵になる。
				const float windupTime = e->WindupTime(ctx.Param("windupTime"_sid));
				const float t01 = (windupTime > 0.0f) ? (ctx.Time() / windupTime) : 1.0f;

				// 潰れ→伸び上がりの見た目
				e->ApplyWindupPose(t01);

				// 踏み込む直前までは相手を追って向き直る
				e->FaceTarget(e->TurnRate(ctx.Param("turnRate"_sid)));

				// 前半だけ後ろへ引く(負の速度)。後半は足を止めて溜め切る。
				const float back = (t01 < ctx.Param("backUntil"_sid)) ? e->MoveSpeed(ctx.Param("backSpeed"_sid)) : 0.0f;
				e->ApplyMotion(dt, -back);

				const float notice = std::min(e->NoticeTime(), windupTime);
				e->SetWindupTotal(windupTime);

				if (ctx.Time() >= windupTime - notice)
					ctx.ChangeState("Notice"_sid);
			})
		// 被弾で中断された場合もここでスケールが戻る
		.OnExit([](StateContext& ctx)
			{
				ctx.As<EnemyBase>()->EndWindup();
				ctx.As<EnemyBase>()->SetSuperArmor(false);
			});

	// 攻撃通知
	sys->State("Notice").Base("Attack").Tag(EnemyTag::kNotice)
		.Anim("Attack", 10, false)
		.Param("turnRate", 8.0f)
		.Param("thrustMin", 4.0f)		// 相手がこれより遠ければ突き、近ければ薙ぎ
		// 予告中は目を赤く明滅させる
		.Param("fx.light.r", 0.8f).Param("fx.light.g", 0.1f).Param("fx.light.b", 0.2f)
		.Param("fx.lightExtent", 8.0f)
		.Param("fx.lightLift", 1.0f)
		.Param("fx.flickerAmp", 5.0f)	// 明滅の振れ幅
		.Param("fx.flickerBase", 5.0f)	// 明滅の中心
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->SetSweepFlip(KdRandom::GetInt(0, 1) == 0);

				GPUParticleSystem::Instance().PlayEffect(
					"AttackNotice",
					e->GetPosition() + Math::Vector3::Up * e->GetEyeHeight());

				// 溜めの輪(IsWindingUp)を切らさない。牽制のように
				// 溜めを経由しない攻撃でも、ここから予告が始まる。
				e->BeginWindup();
				e->BeginAttackNotice();

				auto& lc = Light(e);
				lc.enabled = true;
				lc.color = ParamColor(ctx, "fx.light");
				lc.range = ctx.Param("fx.lightExtent"_sid);
				lc.power = lc.range;
				lc.offset = { 0.0f, ctx.Param("fx.lightLift"_sid), 0.0f };
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				const float notice = std::max(e->NoticeTime(), kMinDuration);
				const float t01 = std::clamp(ctx.Time() / notice, 0.0f, 1.0f);

				// 前の値の sin を取って次の値にする。周期の決まっていない不規則な明滅になる
				auto& lc = Light(e);
				lc.range = std::sin(lc.range) * ctx.Param("fx.flickerAmp"_sid) + ctx.Param("fx.flickerBase"_sid);
				lc.power = lc.range;

				e->TickAttackNotice(t01);

				// 溜めから来た場合は、その姿勢の続きから伸び上がらせる
				const float total = e->GetWindupTotal();
				const float pose = (total > 0.0f)
					? std::clamp((total - notice + ctx.Time()) / total, 0.0f, 1.0f)
					: t01;
				e->ApplyWindupPose(pose);

				// 直前まで向き直る。ここで追尾を切ると通知が意味を失う。
				e->FaceTarget(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyMotion(dt, 0.0f);

				const bool thrust = WantsThrust(e, ctx.Param("thrustMin"_sid));

				if (ctx.Time() < notice) return;

				//if (e->IsHarassRole()) { ctx.ChangeState("Jab"_sid); return; }
				ctx.ChangeState(thrust ? "Thrust"_sid : "Sweep"_sid);
			})
		// 被弾や死亡で中断された場合もここで元へ戻る
		.OnExit([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->EndAttackNotice();
				e->EndWindup();

				auto& lc = e->GetComponentManager().get<LightComponent>(e->GetEntity());
				lc.enabled = false;
			});

	// 斬りつけ(方向固定 + 踏み込み + 判定発生)
	// モーションは Notice で出し切っているので、ここは待機クリップのまま繋ぐ。
	sys->State("Strike").Base("Attack")
		.Anim("Threat")
		.Param("strikeTime", 0.25f)
		.Param("hitStart", 0.05f)		// 判定の発生
		.Param("hitEnd", 0.18f)			// 判定の消滅
		.Param("damage", 10.0f)
		.Param("reach", 6.0f)			// 判定の長さ
		.Param("radius", 1.2f)			// 判定の太さ
		.Param("knockback", 60.0f)
		.Param("stepSpeed", 22.0f)
		.Param("turnRate", 4.0f)
		.MinTime(0.2f)
		.OnEnter([](StateContext& ctx)
			{
				// ここで方向とattackIdを確定する
				ctx.As<EnemyBase>()->BeginAttack();
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());
				const float t = ctx.Time();
				// 確定済みの方向へ短く踏み込む(相手は追尾しない)
				e->ApplyStrikePose(t / std::max(ctx.Param("strikeTime"_sid), kMinDuration));
				e->FaceAttackDir(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyChaseMotion(dt, e->MoveSpeed(ctx.Param("stepSpeed"_sid)));

				// 判定は発生フレームの間だけ出す。
				// 毎フレーム提出しても attackId が同じなので多段ヒットしない。
				const float hitStart = ctx.Param("hitStart"_sid);
				const float hitEnd = ctx.Param("hitEnd"_sid);
				if (t >= hitStart && t < hitEnd)
				{
					e->SpawnAttackHitbox(
						e->AttackDamage(ctx.Param("damage"_sid)),
						e->AttackReach(ctx.Param("reach"_sid)),
						e->AttackReach(ctx.Param("radius"_sid)),
						e->AttackKnockback(ctx.Param("knockback"_sid)));
				}

				if (t >= ctx.Param("strikeTime"_sid))
					ctx.ChangeState("Interval"_sid);
			});

	// ---- 突き ----
	sys->State("Thrust").Base("Attack")
		.Anim("Threat")
		.Param("damage", 12.0f)
		.Param("reach", 14.0f)		// 長い
		.Param("radius", 1.0f)		// 細い
		.Param("knockback", 70.0f)
		.Param("stepSpeed", 26.0f)		// 深く踏み込む
		.Param("strikeTime", 0.22f)
		.Param("hitStart", 0.04f)
		.Param("hitEnd", 0.16f)
		.Param("turnRate", 4.0f)	// 元はフォールバックだけで使われていた値
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->BeginAttack();
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());
				const float t = ctx.Time();
				// 確定済みの方向へ短く踏み込む(相手は追尾しない)
				e->ApplyStrikePose(t / std::max(ctx.Param("strikeTime"_sid), kMinDuration));
				e->FaceAttackDir(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyChaseMotion(dt, e->MoveSpeed(ctx.Param("stepSpeed"_sid)));

				// 判定は発生フレームの間だけ出す。
				// 毎フレーム提出しても attackId が同じなので多段ヒットしない。
				const float hitStart = ctx.Param("hitStart"_sid);
				const float hitEnd = ctx.Param("hitEnd"_sid);
				if (t >= hitStart && t < hitEnd)
				{
					e->SpawnAttackHitbox(
						e->AttackDamage(ctx.Param("damage"_sid)),
						e->AttackReach(ctx.Param("reach"_sid)),
						e->AttackReach(ctx.Param("radius"_sid)),
						e->AttackKnockback(ctx.Param("knockback"_sid)));
				}

				if (t >= ctx.Param("strikeTime"_sid))
					ctx.ChangeState("Interval"_sid);
			});

	// ---- 薙ぎ払い ----
	sys->State("Sweep").Base("Attack")
		.Anim("Threat")
		.Param("damage", 14.0f)
		.Param("reach", 4.0f)
		.Param("radius", 5.0f)
		.Param("knockback", 110.0f)
		.Param("stepSpeed", 4.0f)
		.Param("strikeTime", 0.30f)
		.Param("hitStart", 0.08f)
		.Param("hitEnd", 0.24f)
		.Param("turnRate", 4.0f)	// 元はフォールバックだけで使われていた値
		.OnEnter([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				e->BeginAttack();
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());
				const float t = ctx.Time();
				// 確定済みの方向へ短く踏み込む(相手は追尾しない)
				e->ApplyStrikePose(t / std::max(ctx.Param("strikeTime"_sid), kMinDuration));
				e->FaceAttackDir(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyChaseMotion(dt, e->MoveSpeed(ctx.Param("stepSpeed"_sid)));

				// 判定は発生フレームの間だけ出す。
				// 毎フレーム提出しても attackId が同じなので多段ヒットしない。
				const float hitStart = ctx.Param("hitStart"_sid);
				const float hitEnd = ctx.Param("hitEnd"_sid);
				if (t >= hitStart && t < hitEnd)
				{
					e->SpawnAttackHitbox(
						e->AttackDamage(ctx.Param("damage"_sid)),
						e->AttackReach(ctx.Param("reach"_sid)),
						e->AttackReach(ctx.Param("radius"_sid)),
						e->AttackKnockback(ctx.Param("knockback"_sid)));
				}

				if (t >= ctx.Param("strikeTime"_sid))
					ctx.ChangeState("Interval"_sid);
			});


	// 硬直
	sys->State("Interval").Base("Attack")
		.Anim("Threat")
		.Param("intervalTime", 1.2f)
		.Param("turnRate", 6.0f)
		.Param("attackRange", 6.0f)
		.Param("hysteresis", 1.5f)
		.Param("beatSlack", 0.5f)
		.OnEnter([](StateContext& ctx) { ctx.As<EnemyBase>()->StopMotion(); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* e = ctx.As<EnemyBase>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 硬直中も相手の方は向き続ける
				e->FaceTarget(e->TurnRate(ctx.Param("turnRate"_sid)));
				e->ApplyMotion(dt, 0.0f);

				// 種類ごとの手数の差 + 個体ごとのばらつき
				const float ready = e->IntervalTime(ctx.Param("intervalTime"_sid)) + e->GetAttackBias();
				if (ctx.Time() < ready) return;

				// 見失っていれば警戒へ
				if (!e->IsTargetInSight()) { e->ReleaseAttackToken(); ctx.ChangeState("Alert"_sid); return; }

				// 逃げられていれば追跡へ戻る
				const float leave = e->AttackReach(ctx.Param("attackRange"_sid))
					+ ctx.Param("hysteresis"_sid);
				if (e->GetTargetDistance() > leave) { e->ReleaseAttackToken(); ctx.ChangeState("Chase"_sid); return; }

				// 連続で殴りすぎたら順番を譲る
				if (e->ShouldYieldToken()) { e->ReleaseAttackToken(); ctx.ChangeState("Chase"_sid); return; }

				// ---- 拍に乗せる ----
				if (!e->IsAttackBeatReady()
					&& (ctx.Time() - ready) < ctx.Param("beatSlack"_sid)) return;

				// まだ間合いの内側 → 続けてもう一度殴る
				if (e->IsHarassRole())
				{
					e->ClearWindupTotal();
					ctx.ChangeState("Notice"_sid);
				}
				else
				{
					ctx.ChangeState("Windup"_sid);
				}
			});

	sys->SetEntryState("Wander");

	// ベースが直接指定されたときの飛び先
	sys->SetDefaultChild("Normal", "Wander");
	sys->SetDefaultChild("Chase", "Pursue");
	sys->SetDefaultChild("Alert", "LookAround");
	sys->SetDefaultChild("Damage", "Stagger");
	sys->SetDefaultChild("Attack", "Windup");
	sys->SetDefaultChild("Flee", "Flee");
	sys->SetDefaultChild("Die", "Fall");
	sys->SetDefaultChild("Carry", "CarryWander");

	// ここまでがコード側の既定値。EnemyStates.json に書いた値で上書きする
	StateTuning::Apply(*sys, EnemyTuningKey::kStatesFile, kParamUnits);

	// 定義ミスを起動時に洗い出す
	std::string err;
	if (!sys->Validate(&err))
	{
		assert(false && "Enemyステート定義に問題があります");
	}

	GameScale::ApplyToStateParams(sys);
}