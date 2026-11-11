#include "Pch.h"
#include "ChickStateSetup.h"
#include "../../../../GameSystem/StateMachine/StateMachineManager.h"
#include "../../../Config/StateTuning.h"
#include "../Chick.h"

using namespace StateLiterals;

namespace {

	// 遷移の優先度。数字が大きいほど強い(同じフレームに複数の遷移が来たら強いほうが残る)
	namespace Priority
	{
		constexpr uint8_t kFollow = 5;		// プレイヤーに気づいてついていく
		constexpr uint8_t kGiveUp = 8;		// 見失って元の場所へ
		constexpr uint8_t kSettle = 9;		// 巣の持ち場へ向かう(見失いより強い)
	}

	// メートルで持っている params。JSON には縮尺前の値を書く
	const StateTuning::ParamUnits kParamUnits = {
		{ "followRange", "loseRange", "keepDistance", "dashRange", "walkRange" },
		{},
	};

	// params に "<名前>.r/g/b" で入れた色をライトへ当てる
	void ApplyLightColor(StateContext& ctx, Chick* _c, std::string_view _name)
	{
		auto& cm = _c->GetComponentManager();
		if (!cm.has<LightComponent>(_c->GetEntity())) return;

		const std::string base(_name);
		auto& lc = cm.get<LightComponent>(_c->GetEntity());
		lc.color = {
			ctx.Param(MakeStateId(base + ".r")),
			ctx.Param(MakeStateId(base + ".g")),
			ctx.Param(MakeStateId(base + ".b")) };
	}

	// 連れ去りは何よりも優先。倒れている最中でも割り込む。
	bool TryCarried(StateContext& ctx, Chick* _c)
	{
		if (!_c->ConsumeCarryRequest()) return false;

		ctx.ChangeStateForced("Carried"_sid);
		return true;
	}

	bool TryDown(StateContext& ctx, Chick* _c)
	{
		if (!_c->ConsumeDownRequest()) return false;

		ctx.ChangeStateForced("Down"_sid);
		return true;
	}

	bool TryHurt(StateContext& ctx, Chick* _c)
	{
		if (!_c->ConsumeHurt()) return false;

		ctx.ChangeStateForced("Hurt"_sid);
		return true;
	}
}

void SetupChickStates()
{
	auto* sys = SM_MGR.CreateSystem("Chick");

	// 既に登録済みなら何もしない
	if (sys->GetStateCount() > 0) return;

	// =======================================================
	// ベース: 待機
	// =======================================================
	sys->State("Idle").Tag(ChickTag::kIdle)
		.Param("followRange", 12.0_m)
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				if (TryCarried(ctx, c)) return;
				if (TryDown(ctx, c)) return;
				if (TryHurt(ctx, c))return;

				// 巣に収まったひよこはもう動かない
				if (c->IsRescued()) return;

				if (c->IsPlayerNear(ctx.Param("followRange"_sid)))
					ctx.ChangeState("Walk"_sid, Priority::kFollow);
			});

	// ---- 立ち止まり ----
	sys->State("Rest").Base("Idle")
		.Anim("LookAround", 10.0f, true)
		.OnEnter([](StateContext& ctx) { ctx.As<Chick>()->StopMotion(); })
		.OnUpdate([](StateContext& ctx)
			{
				// 速度0でも呼ぶのは重力を進めるため
				ctx.As<Chick>()->ApplyMotion(static_cast<float>(Time::Instance().GetDeltaTime()), 0.0f);
			});

	// ---- 位置取り ----
	sys->State("Settle").Base("Idle")
		.Anim("Walk", 10.0f, true)
		.Param("moveSpeed", 9.0f)
		.Param("turnRate", 10.0f)
		.Param("arriveSlack", 0.8f)
		.OnEnter([](StateContext& ctx) { ctx.As<Chick>()->SetRescued(true); })
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 持ち場を取り上げられた
				if (!c->HasSlot()) { ctx.ChangeState("Rest"_sid); return; }

				c->MoveTo(c->GetSlotPosition(), dt,
					ctx.Param("moveSpeed"_sid),
					ctx.Param("turnRate"_sid));

				if (c->GetSlotDistance() <= ctx.Param("arriveSlack"_sid))
					ctx.ChangeState("Rest"_sid);
			});

	// =======================================================
	// ベース: 追従
	// =======================================================
	sys->State("Follow").Tag(ChickTag::kFollow)
		.Param("loseRange", 40.0_m)
		// ついてきている間は緑、離れたら白に戻す
		.Param("followLight.r", 0.0f).Param("followLight.g", 0.5f).Param("followLight.b", 0.0f)
		.Param("normalLight.r", 0.5f).Param("normalLight.g", 0.5f).Param("normalLight.b", 0.5f)
		.OnEnter([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();
				c->StopHide();
				ApplyLightColor(ctx, c, "followLight");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				if (TryCarried(ctx, c)) return;
				if (TryDown(ctx, c)) return;
				if (TryHurt(ctx, c)) return;

				// 巣に入って持ち場をもらった → 位置取りへ
				if (c->HasSlot()) { ctx.ChangeState("Settle"_sid, Priority::kSettle); return; }

				// 離れすぎ/見失った → 元の位置へ
				if (!c->HasTarget()
					|| c->GetTargetDistance() > ctx.Param("loseRange"_sid))
				{
					ctx.ChangeState("Rest"_sid, Priority::kGiveUp);
					return;
				}
			})
		.OnExit([](StateContext& ctx)
			{
				ApplyLightColor(ctx, ctx.As<Chick>(), "normalLight");
			});

	// ---- 歩き(近い) ----
	sys->State("Walk").Base("Follow")
		.Anim("Walk", 10.0f, true)
		.Param("moveSpeed", 10.0f)
		.Param("turnRate", 9.0f)
		.Param("keepDistance", 3.0_m)
		.Param("dashRange", 14.0_m)
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				c->FollowTarget(static_cast<float>(Time::Instance().GetDeltaTime()),
					ctx.Param("moveSpeed"_sid),
					ctx.Param("turnRate"_sid),
					ctx.Param("keepDistance"_sid));

				if (c->GetTargetDistance() >= ctx.Param("dashRange"_sid))
					ctx.ChangeState("Dash"_sid);
			});

	// ---- 走り(離された) ----
	sys->State("Dash").Base("Follow")
		.Anim("Dash", 10.0f, true)
		.Param("moveSpeed", 17.0f)
		.Param("turnRate", 12.0f)
		.Param("keepDistance", 3.0_m)
		.Param("walkRange", 9.0_m)
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				c->FollowTarget(static_cast<float>(Time::Instance().GetDeltaTime()),
					ctx.Param("moveSpeed"_sid),
					ctx.Param("turnRate"_sid),
					ctx.Param("keepDistance"_sid));

				if (c->GetTargetDistance() <= ctx.Param("walkRange"_sid))
					ctx.ChangeState("Walk"_sid);
			});

	// =======================================================
	// ダウン
	// =======================================================
	sys->State("DownChick").Tag(ChickTag::kDown)
		.OnEnter([](StateContext& ctx) 
			{
				auto* c = ctx.As<Chick>();
				c->StopMotion();

				auto& rc = c->GetComponentManager().get<RenderComponent>(c->GetEntity());
				SetMaterial(rc, "Damage");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());

				// 倒れているところを咥えられる
				if (TryCarried(ctx, c)) return;

				// 足は止めるが、重力は進めて地面に落とす
				c->ApplyMotion(dt, 0.0f);

				if (!c->TickDown(dt)) return;

				c->Revive();
				ctx.ChangeStateForced("Rest"_sid);
			})
		.OnExit([](StateContext& ctx)
		{
			auto* c = ctx.As<Chick>();

			auto& rc = c->GetComponentManager().get<RenderComponent>(c->GetEntity());
			SetMaterial(rc, "ChickDefault");
		});
	
	sys->State("Down").Base("DownChick")
		.Anim("Down", 1.0f, false);

	// =======================================================
	// 連れ去られ
	// =======================================================
	sys->State("Carry").Tag(ChickTag::kCarried)
		.Anim("Struggle", 5.0f, true)
		.Param("sound", "carry")
		// 咥えられている間は赤、離されたら白に戻す
		.Param("carryLight.r", 0.5f).Param("carryLight.g", 0.0f).Param("carryLight.b", 0.0f)
		.Param("normalLight.r", 0.5f).Param("normalLight.g", 0.5f).Param("normalLight.b", 0.5f)
		.OnEnter([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				AudioManager::Instance().Play(ctx.ParamStr("sound"_sid));

				c->StopMotion();
				ApplyLightColor(ctx, c, "carryLight");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				c->StopMotion();

				if (c->IsCarried()) return;

				ctx.ChangeStateForced("Rest"_sid);
			})
		.OnExit([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();
				ApplyLightColor(ctx,c, "normalLight");

				GPUParticleSystem::Instance().PlayEffect("Release", c->GetPosition());
			});
	sys->State("Carried").Base("Carry");

	// =======================================================
	// ダメージ
	// =======================================================
	sys->State("HurtChick").Tag(ChickTag::kHurt)
		.Anim("Damage", 1.0f, false)
		.Param("sound", "chickHurt")
		.OnEnter([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();
				AudioManager::Instance().Play(ctx.ParamStr("sound"_sid));
				c->StopMotion();

				// ダメージマテリアル
				auto& rc = c->GetComponentManager().get<RenderComponent>(c->GetEntity());
				SetMaterial(rc, "Damage");
			})
		.OnUpdate([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();
				const float dt = static_cast<float>(Time::Instance().GetDeltaTime());
				c->ApplyMotion(dt, 0.0f);

				if (!c->TickHurt(dt)) return;
				ctx.ChangeStateForced("Rest"_sid);
			})
		.OnExit([](StateContext& ctx)
			{
				auto* c = ctx.As<Chick>();

				c->RecoverHurt();

				auto& rc = c->GetComponentManager().get<RenderComponent>(c->GetEntity());
				SetMaterial(rc, "ChickDefault");
			});
	sys->State("Hurt").Base("HurtChick")
		.Anim("Damage", 1.0f, false);

	sys->SetEntryState("Rest");

	// ベースが直接指定されたときの飛び先
	sys->SetDefaultChild("Carry", "Carried");
	sys->SetDefaultChild("DownChick", "Down");
	sys->SetDefaultChild("HurtChick", "Hurt");
	sys->SetDefaultChild("Idle", "Rest");
	sys->SetDefaultChild("Follow", "Walk");

	// ここまでがコード側の既定値。ChickStates.json に書いた値で上書きする
	StateTuning::Apply(*sys, ChickTuningKey::kStatesFile, kParamUnits);

	// 定義ミスを起動時に洗い出す
	std::string err;
	if (!sys->Validate(&err))
	{
		assert(false && "Chickステート定義に問題があります");
	}

	GameScale::ApplyToStateParams(sys);
}