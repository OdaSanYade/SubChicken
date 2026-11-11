#include "GameScene.h"
#include"../SceneManager.h"

//カメラ
#include "../../GameObject/Camera/CameraManager.h"
#include "../../GameObject/Camera/EntityCamera/EntityCamera.h"
#include "../../GameObject/Camera/DebugCamera/DebugCamera.h"

//オブジェクト
#include "../../GameObject/Stage/Stage.h"
#include "../../GameObject/Stage/Ornament/Mushroom/MushroomManager.h"

#include "../../GameObject/Chara/Player/Player.h"
#include "../../GameObject/Chara/Chick/ChickManager.h"

//フェーズ
#include "../../GameSystem/PhaseSystem/PhaseSystem.h"

//敵を一括管理
#include "../../GameObject/Chara/Enemies/Core/EnemyBase.h"
#include "../../GameObject/Chara/Enemies/Core/EnemyManager.h"


//デバッグ
#include "../../GameSystem/ComponentSystem/Debug/InspectorPanel.h"

//あたり判定システム
#include "../../GameSystem/Collision/CollisionWorld.h"
#include "../../GameSystem/Collision/Debug/CollisionDebug.h"
#include "../../GameSystem/Collision/Debug/DebugLine.h"

#include "../../GameSystem/Combat/CombatFx.h"
#include "../../GameSystem/Combat/ComboTracker.h"
#include "../../UI/Game/CombatHud.h"
#include "../../GameSystem/Combat/BreathGauge.h"

//描画システム
#include "../../GameSystem/ShaderSystem/ShaderRender.h"

namespace {
	// 開始地点の猶予。この円を出るまで敵に見つからない。
	constexpr float kStartZoneRadius = 30.0_m;
}

GameScene::~GameScene()
{}

void GameScene::Update()
{
	CombatFx::Instance().Update();
	BreathGauge::Instance().Update();
	ComboTracker::Instance().Update();
	HUD.Update();
	PHASE.Update();

	// ロック情報をHUDへ渡す
	if (auto sp = ObjectManager::Instance().GetWeak<EntityCamera>("EntityCamera").lock())
	{
		std::uint64_t id = 0;
		Math::Vector3 pos;
		const bool locked = sp && sp->GetSoftLockInfo(id, pos);
		HUD.SetLockTarget(locked, id, pos);
	}

	BaseScene::Update();

	m_collisionWorld->Update();

	if (RESULT.IsActive())
	{
		RESULT.Update();

		if (m_leaving)
		{
			if (m_choice == ResultHud::ChoiceRetry)
				SceneManager::Instance().SetNextScene(SceneManager::SceneType::Game);
			else
				SceneManager::Instance().SetNextScene(SceneManager::SceneType::Title);
		}
		return;
	}

	m_gameTime += Time::Instance().GetDeltaTime();
	CheckBattleEnd();

	static bool prevF1 = false;
	bool f1 = (GetAsyncKeyState(VK_F1) & 0x8000) != 0;
	if (f1 && !prevF1) {
		CameraManager::Instance().ToggleDebugMode();
		if (CameraManager::Instance().IsDebugMode())
		{
			m_cameraName = "DebugCamera";
			Time::Instance().SetTimeScale(0.0f);
		}
		else
		{
			m_cameraName = "EntityCamera";
			Time::Instance().SetTimeScale(1.0f);
		}
	}
	prevF1 = f1;
}

void GameScene::DrawSprite()
{
	HUD.Draw();

	PHASE.Draw();

	RESULT.Draw();
}

void GameScene::DrawDebug()
{
	BaseScene::DrawDebug();
	kd::ecs::DrawEcsInspector("ECS Inspector", cm, g_selected);

	if (auto sp = ObjectManager::Instance().GetWeak<Player>("Player").lock())
	{
		sp->DrawDebugImGui();
	}

	//敵部隊デバッグ
	if (auto sp = ObjectManager::Instance().GetWeak<EnemyManager>("EnemyManager").lock())
	{
		sp->DrawDebugImGui();
	}

	if (auto sp = ObjectManager::Instance().GetWeak<ChickManager>("ChickManager").lock())
	{
		sp->DrawDebugImGui();
	}

	if (auto sp = ObjectManager::Instance().GetWeak<MushroomManager>("MushroomManager").lock())
	{
		sp->DrawDebugImGui();
	}

	//コリジョンデバッグ
	Math::Matrix viewProj;
	if (auto sp = ObjectManager::Instance().GetWeak<EntityCamera>("EntityCamera").lock())
	{
		viewProj = sp->GetCamera()->GetCameraViewMatrix() * sp->GetCamera()->GetProjMatrix();
	}
	m_lineDebug->Begin();
	CollisionDebugRenderer::Instance().Draw(*m_collisionWorld, *m_lineDebug);
	m_lineDebug->Flush(viewProj);
	CollisionDebugRenderer::Instance().DrawImGui();

	HUD.DrawDebugImGui();
}

void GameScene::Enter()
{
	BaseScene::Enter();

	auto& om = ObjectManager::Instance();

	//コリジョンシステム
	m_collisionWorld = std::make_shared<CollisionWorld>();
	m_lineDebug = std::make_shared<LineDebugDraw>();
	m_lineDebug->Init();

	//オブジェクトの登録
	auto camera = std::make_shared<EntityCamera>(cm, m_collisionWorld);
	om.Register(camera);

	auto debugCamera = std::make_shared<DebugCamera>(cm, m_collisionWorld);
	om.Register(debugCamera);

	auto stage = std::make_shared<Stage>(cm, m_collisionWorld);
	om.Register(stage);

	auto mushroom = std::make_shared<MushroomManager>(cm, m_collisionWorld);
	om.Register(mushroom);

	auto enemyManager = std::make_shared<EnemyManager>(cm, m_collisionWorld);
	om.Register(enemyManager);

	auto player = std::make_shared<Player>(cm, m_collisionWorld);
	om.Register(player);

	auto chickManager = std::make_shared<ChickManager>(cm, m_collisionWorld);
	om.Register(chickManager);

	//ステージのあたり判定登録
	if (auto sp = om.GetWeak<Stage>("Stage").lock()) {
		const StageData data = sp->GetStageData();
		m_collisionWorld->SetStage(data.m_model, data.m_mat,
			[](std::string_view nodeName) -> uint32_t { return Collision::Tag_None; });
	}

	//各オブジェクトのもろもろ設定
	camera->SetTarget(player->GetEntity());
	player->SetCamera(camera);
	enemyManager->SetTargetPlayer(player->GetEntity());

	//ひよこ
	chickManager->SetTarget(player->GetEntity());
	chickManager->SetNestPosition(Math::Vector3::Zero);
	chickManager->SpawnAll();

	//------------------------------------------
	// 開始地点の猶予
	//------------------------------------------
	enemyManager->SetStartZoneRadius(kStartZoneRadius);
	enemyManager->SetOnExposed(
		[wpChick = std::weak_ptr<ChickManager>(chickManager)]()
		{
			if (auto sp = wpChick.lock()) sp->RevealIdleChicks();

			// 見つかった合図。画面が揺れるだけでも「始まった」は伝わる。
			CombatFx::Instance().AddTrauma(0.35f);
		});

	mushroom->SetTarget(player->GetEntity());
	mushroom->SpawnAll();

	CameraManager::Instance().RegisterGameCamera(camera);
	CameraManager::Instance().RegisterDebugCamera(debugCamera);
	CameraManager::Instance().SetDebugMode(false);

	HUD.Reset();


	//------------------------------------------
	// ゲームフェーズ
	//------------------------------------------
	PHASE.Reset();
	auto& phase = PHASE.GetHooks();
	phase.SpawnEnemy =
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)](int _p)
		{
			if (auto sp = wpEnemy.lock())
				sp->SpawnPhase(_p);
		};
	phase.PlayerAlive =
		[wpPlayer = std::weak_ptr<Player>(player)]()
		{
			if (auto sp = wpPlayer.lock())
				return !sp->IsDead();
			else
				return false;
		};
	PHASE.Init();


	//------------------------------------------
	// プレイヤー
	//------------------------------------------
	player->SetEnemyPositionProvider(
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)]()
		{
			std::vector<Math::Vector3> out;

			auto sp = wpEnemy.lock();
			if (!sp) return out;

			const auto& enemies = sp->GetEnemyies();
			out.reserve(enemies.size());
			for (const auto& e : enemies)
			{
				if (!e->IsTargetable()) continue;
				out.push_back(e->GetPosition());
			}
			return out;
		});

	player->SetThreatFunction(
		[wp = std::weak_ptr<EnemyManager>(enemyManager)]()
		{
			auto sp = wp.lock();
			if (!sp) return false;

			// 1体でも通知中なら「狙われている」とみなす。
			for (const auto& e : sp->GetEnemyies())
			{
				if (e->IsTargetable() && e->IsAttackNotice()) return true;
			}
			return false;
		});


	//------------------------------------------
	// カメラ
	//------------------------------------------
	camera->SetPullCandidatesProvider(
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)]()
		{
			std::vector<CameraComponent::PullCandidate> out;

			auto sp = wpEnemy.lock();
			if (!sp) return out;

			const auto& enemies = sp->GetEnemyies();
			out.reserve(enemies.size());
			for (const auto& e : enemies)
			{
				if (!e->IsTargetable()) continue;	// ★死体をロックしない
				Math::Vector3 pos = e->GetPosition();
				pos.y += 2.5_m;
				out.emplace_back(
					static_cast<std::uint64_t>(e->GetEntity().index), pos);
			}
			return out;
		});

	player->SetFreezeFunction(
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)](
			const Math::Vector3& _center, float _radius, float _duration, bool _nearestOnly)
		{
			auto sp = wpEnemy.lock();
			if (!sp) return;

			const auto& enemies = sp->GetEnemyies();
			const float r2 = _radius * _radius;

			// 全力鳴きは範囲内の全員
			for (const auto& e : enemies)
			{
				if (!e->IsTargetable()) continue;

				Math::Vector3 d = e->GetPosition() - _center;
				d.y = 0.0f;
				if (d.LengthSquared() > r2) continue;

				e->Freeze(_duration);
				sp->ForceExpose("freeze");
			}
		});

	player->SetFrozenPositionProvider(
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)]()
		{
			std::vector<Math::Vector3> out;

			auto sp = wpEnemy.lock();
			if (!sp) return out;

			for (const auto& e : sp->GetEnemyies())
			{
				if (e->IsTargetable() && e->IsFrozen()) out.push_back(e->GetPosition());
			}
			return out;
		});


	//------------------------------------------
	// 敵
	//------------------------------------------
	enemyManager->SetChickTargetProvider(
		[wpChick = std::weak_ptr<ChickManager>(chickManager)]()
		{
			if (auto sp = wpChick.lock()) return sp->GetEnemyTargets();
			return EnemyManager::TargetPoints{};
		});


	//------------------------------------------
	// ひよこ
	//------------------------------------------
	chickManager->SetCarrierHooks(
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)]
		(const Math::Vector3& _pos, int _chickId)
		{
			auto sp = wpEnemy.lock();
			return sp ? sp->RequestCarrier(_pos, _chickId) : -1;
		},
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)]
		(int _carrierId, int _chickId, ChickManager::CarrierPose& _out)
		{
			auto sp = wpEnemy.lock();
			if (!sp) return false;

			EnemyManager::CarrierInfo info;
			info.foot = _out.foot;	// 見つからなかった時はひよこ側の保険をそのまま返す

			const bool held = sp->GetCarrierPose(_carrierId, _chickId, info);

			_out.mouth = info.mouth;
			_out.foot = info.foot;
			_out.scale = info.scale;
			_out.leaving = info.leaving;
			_out.gone = info.gone;
			return held;
		});


	//------------------------------------------
	// HUD
	//------------------------------------------
	//カメラ情報
	HUD.SetCameraProvider(
		[wpCam = std::weak_ptr<CameraBase>(camera)](CombatHud::CameraInfo& _out)
		{
			auto sp = wpCam.lock();
			if (!sp) return false;

			_out.view = sp->GetViewMatrix();
			_out.fovDeg = sp->GetFovY();
			_out.nearZ = sp->GetNearZ();
			_out.farZ = sp->GetFarZ();
			_out.position = sp->GetResolvedWorldMatrix().Translation();
			return true;
		});
	//敵情報
	HUD.SetEnemyProvider(
		[wpEnemy = std::weak_ptr<EnemyManager>(enemyManager)]()
		{
			std::vector<CombatHud::EnemyInfo> out;

			auto sp = wpEnemy.lock();
			if (!sp) return out;

			const auto& enemies = sp->GetEnemyies();
			out.reserve(enemies.size());

			for (const auto& e : enemies)
			{
				const float death = e->GetDeathFlash();
				if (!e->IsTargetable() && death <= 0.0f) continue;	// 死体には出さない

				CombatHud::EnemyInfo info;
				info.id = static_cast<std::uint64_t>(e->GetEntity().index);
				info.position = e->GetPosition();
				info.height = e->GetEyeHeight() + 1.5_m;
				info.healthRatio = e->GetHealthRatio();
				info.poiseRatio = e->GetPoiseRatio();
				info.windup = e->IsWindingUp();
				info.windupProgress = e->GetWindupProgress();
				info.strikeFlash = e->GetStrikeFlash();
				info.frozen = e->IsFrozen();
				info.breakFlash = e->GetBreakFlash();
				info.staggered = e->IsStaggered();
				info.deathFlash = death;
				info.staggerRemain = e->GetStaggerRemainRatio();
				info.frozenRemain = e->GetFreezeRemainRatio();
				info.attackPower = std::clamp(e->GetSpec().damageScale / 2.0f, 0.0f, 1.0f);
				info.healthValue = e->GetHealthValue();
				info.healthMax = e->GetHealthMax();
				out.push_back(info);
			}
			return out;
		});

	//ひよこ情報(画面外インジケータ)
	HUD.SetChickProvider(
		[wpChick = std::weak_ptr<ChickManager>(chickManager)]()
		{
			std::vector<CombatHud::ChickInfo> out;

			auto sp = wpChick.lock();
			if (!sp) return out;

			for (const auto& t : sp->GetIndicatorTargets())
			{
				CombatHud::ChickInfo info;
				info.id = static_cast<std::uint64_t>(t.id);
				info.position = t.position;
				info.height = 1.0_m;
				info.carried = t.carried;
				out.push_back(info);
			}
			return out;
		});

	//プレイヤー情報
	HUD.SetPlayerProvider(
		[wpPlayer = std::weak_ptr<Player>(player)](CombatHud::PlayerInfo& _out)
		{
			auto sp = wpPlayer.lock();
			if (!sp) return false;

			_out.position = sp->GetPosition();
			_out.healthRatio = sp->GetHealthRatio();
			_out.healthDisplay = sp->GetHealthDisplayRatio();
			_out.healthValue = sp->GetHealthValue();
			_out.damageShock = sp->GetDamageShock();
			_out.breathRatio = BreathGauge::Instance().GetRatio();
			_out.breathDisplay = BreathGauge::Instance().GetDisplayRatio();
			_out.breathFlash = BreathGauge::Instance().GetFlash();
			_out.counterRatio = sp->GetCounterRatio();
			_out.parryWindow = sp->IsCounterReady();
			_out.cryWaveRatio = sp->GetCryWaveRatio();
			_out.cryWasFull = sp->IsCryFull();
			_out.dodgeReady = sp->IsDodgeReady();
			_out.dodgeCooldown = sp->GetDodgeCooldownRatio();
			_out.airborne = !sp->IsGrounded();
			_out.dead = (sp->GetHealthRatio() <= 0.0f);
			return true;
		});

	//ゲーム状況
	HUD.SetStageProvider(
		[wpChick = std::weak_ptr<ChickManager>(chickManager)](CombatHud::StageInfo& _out)
		{
			auto chick = wpChick.lock();
			if (!chick) return false;

			_out.totalCount = chick->GetTotalCount();
			_out.killedCount = chick->GetIndicatorTargetCount();
			_out.aliveCount = chick->GetTotalCount() - chick->GetIndicatorTargetCount();
			_out.elapsedSec = PHASE.GetElapsed();

			return true;
		});

	RES_MGR.GetDataList()->Register("HudConfig", "HudConfig");
	HUD.SetConfigKey("HudConfig");
	HUD.Init();
	HUD.SetStepRange(25.0_m);

	// リザルト
	m_leaving = false;
	m_battleEnded = false;
	m_leaveTimer = 0.0f;
	m_choice = ResultHud::ChoiceRetry;

	m_gameTime = 0.0f;
	RESULT.Init();
	RESULT.SetStandard({ 500.0f, 900.0f, 5 });

	RESULT.SetDecideCallback(
		[this](ResultHud::Choice _c)
		{
			m_leaving = true;
			m_choice = _c;
		});

	//メインカメラ登録
	m_cameraName = camera->GetID();

	m_bgm = AudioManager::Instance().Play("Fight");
	if (m_bgm) m_bgm->FadeIn(2.0f);
}

void GameScene::Exit()
{
	BaseScene::Exit();
	BreathGauge::Instance().Reset();
	HUD.Release();
	RESULT.Close();
	RESULT.Release();
}

void GameScene::Init()
{}

void GameScene::CheckBattleEnd()
{
	if (m_battleEnded) return;
	auto& om = ObjectManager::Instance();

	auto chick = om.GetWeak<ChickManager>("ChickManager").lock();
	auto player = om.GetWeak<Player>("Player").lock();
	if (!chick || !player) return;

	// 勝ちは「生き残っている子を全員巣まで運べたか」。攫われて消えた子は数に入らない。
	const bool win = chick->IsAllRescued();
	// 全員攫われたらクリアできないので負け
	const bool lose = !player->IsActive() || chick->IsAllLost();

	if (!win && !lose) return;

	if (win)
	{
		if (m_bgm) m_bgm->Stop();
		m_bgm = AudioManager::Instance().Play("Win");
		if (m_bgm) m_bgm->FadeIn(2.0f);
	}
	else
	{
		if (m_bgm) m_bgm->Stop();
		m_bgm = AudioManager::Instance().Play("Lose");
		if (m_bgm) m_bgm->FadeIn(2.0f);
	}

	m_battleEnded = true;

	//------------------------------------------
	// 記録をまとめる
	//------------------------------------------
	BattleResult r;
	r.cleared = win;
	r.elapsedSec = PHASE.GetElapsed();
	r.lifeRatio = std::max(player->GetHealthRatio(), 0.0f);
	r.maxCombo = chick->GetRescuedCount();
	// 救出数を成績に出す
	r.killed = chick->GetRescuedCount();
	r.total = chick->GetTotalCount();

	RESULT.Begin(r);

	// 戦闘中の演出を止める
	ComboTracker::Instance().Reset();
	CombatFx::Instance().ClearSlow();
	KdInputActionSystem::Instance().StopVibration(0);
}