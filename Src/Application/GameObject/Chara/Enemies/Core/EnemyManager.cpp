#include "Pch.h"
#include "EnemyManager.h"
#include "../../../../GameSystem/Combat/CombatFx.h"

#include "EnemyBase.h"
#include "../Type/Normal/Normal.h"
#include "../Type/Rusher/Rusher.h"
#include "../Type/Heavy/Heavy.h"


namespace {
	template<class T>
	std::shared_ptr<EnemyBase> MakeEnemy(
		kd::ecs::ComponentManager& _cm, const std::shared_ptr<CollisionWorld>& _world)
	{
		return std::make_shared<T>(_cm, _world);
	}

	float DistSqXZ(const Math::Vector3& _a, const Math::Vector3& _b)
	{
		const float dx = _a.x - _b.x;
		const float dz = _a.z - _b.z;
		return dx * dx + dz * dz;
	}


	using EnemyFactory = std::shared_ptr<EnemyBase>(*)(
		kd::ecs::ComponentManager&, const std::shared_ptr<CollisionWorld>&);

	// 部隊の1枠
	struct SquadUnit
	{
		EnemyFactory make = nullptr;
		int          count = 0;
	};

	// 編成表
	struct SquadTemplate
	{
		const char* name;
		float       weight;
		SquadUnit   units[4];
	};

	constexpr SquadTemplate kSquadTable[] = {
		// 索敵隊。仲間を呼ぶ役が2人いるので、放置すると芋づるで寄ってくる。
		{ "Scouting", 2.0f, { { &MakeEnemy<Normal>,  2 }, { &MakeEnemy<Rusher>, 3 },
							  { &MakeEnemy<Heavy>, 1 }, {} } },

							  // 突撃隊。手数で押す。数が多いので硬化の連鎖が一番育つ。
							  { "Assault",  3.0f, { { &MakeEnemy<Rusher>, 4 }, { &MakeEnemy<Normal>, 1 },
												  { &MakeEnemy<Heavy>,  1 }, {} } },

												  // 重装隊。正面から来る。固めて砕く的として置く。
												  { "Armored",  1.5f, { { &MakeEnemy<Heavy>,  2 }, { &MakeEnemy<Normal>, 3 },
																	  {  &MakeEnemy<Rusher>, 2 }, {}}},

																	  // 巡回隊。一番おとなしい。最初に当たる相手として使う。
													  { "Patrol",   2.0f, { { &MakeEnemy<Normal>, 4 }, { &MakeEnemy<Rusher>,  2 },
																		  {}, {} } },
	};

	const SquadTemplate& PickSquadTemplate()
	{
		float total = 0.0f;
		for (const auto& t : kSquadTable) total += t.weight;

		float r = KdRandom::GetFloat(0.0f, total);
		for (const auto& t : kSquadTable)
		{
			r -= t.weight;
			if (r <= 0.0f) return t;
		}
		return kSquadTable[0];
	}

	// 部隊全員へ呼び声を届ける。届いた数を返す。
	// 既に自力で見えている敵には効かない(ReceiveCall 側で弾く)。
	int CallSquadMembers(const std::vector<std::shared_ptr<EnemyBase>>& _enemies,
		int _squadId, const Math::Vector3& _pos)
	{
		int reached = 0;
		for (const auto& enemy : _enemies)
		{
			if (enemy->GetSquadId() != _squadId) continue;
			if (!enemy->IsTargetable()) continue;
			if (enemy->ReceiveCall(_pos)) ++reached;
		}
		return reached;
	}

	// デバッグ配置のセーブデータは編成名を文字列で持つ
	const SquadTemplate* FindSquadTemplateByName(const std::string& _name)
	{
		for (const auto& t : kSquadTable)
		{
			if (_name == t.name) return &t;
		}
		return nullptr;
	}
}

void EnemyManager::Init()
{
	// 敵全員で共有する値と、マネージャの値をシーンごとに読み直す
	EnemyTuning::Reload();
	Tuning::Load(m_tuning, EnemyTuningKey::kFile, "EnemyManager");

	m_debugRange = m_tuning.squadSpread;
	if (m_zoneRadius <= 0.0f) m_zoneRadius = m_tuning.startZoneRadius;

	// 配置データを読む
	LoadDebugSpawns();
}

void EnemyManager::PreUpdate()
{
	// 走り去って消えた敵を片付ける。
	// 咥えていたひよこは記録を見てひよこ側で消えるので、先に控えておく。
	for (const auto& enemy : m_spEnemies)
	{
		if (!enemy->IsVanished()) continue;

		if (enemy->IsCarrying())
			m_vanishedCarriers[static_cast<int>(enemy->GetEntity().index)] = enemy->GetCarryChickId();

		// 逃げ切った敵もこの波で片付いた1体として数える。数えないとフェーズが進まない。
		++m_phaseKilled;
	}

	m_spEnemies.erase(
		std::remove_if(m_spEnemies.begin(), m_spEnemies.end(),
			[](const auto& enemy) { return enemy->IsVanished(); }),
		m_spEnemies.end());
}

void EnemyManager::Update()
{
	SpownLogic();

	//------------------------------------------
	// プレイヤー位置を全敵へ配る
	//------------------------------------------
	Math::Vector3 targetPos = Math::Vector3::Zero;
	bool          hasTarget = false;

	// SetTargetPlayer の前は空のエンティティなので読まない(敵の更新も待たせる)
	if (!m_cm.has<TransformComponent>(m_targetPlayer)) return;
	targetPos = m_cm.get<TransformComponent>(m_targetPlayer).GetPosition();

	// 開始地点の猶予。円を出るまでは誰も見つけられない。
	UpdateStartZone(targetPos);
	hasTarget = m_exposed;

	// PostUpdate の各パスでも使うので控えておく
	m_lastTargetPos = targetPos;
	m_lastHasTarget = hasTarget;

	//------------------------------------------
	// 部隊の集計と士気
	//------------------------------------------
	// 逃走の判定に使う士気は部隊ごと。全体の残存率(m_morale)はHUD用。
	UpdateSquads();
	UpdateRaid();

	const int alive = GetAliveCount();
	m_peakAlive = std::max(m_peakAlive, alive);
	m_morale = (m_peakAlive > 0)
		? static_cast<float>(alive) / static_cast<float>(m_peakAlive) : 1.0f;



	// 攻撃権と持ち場は、各敵を更新する前に全体を見て決める
	struct Cand { int id; Math::Vector3 pos; };
	std::vector<Cand> cands;
	cands.push_back({ 0, targetPos });
	if (m_chickTargets)
	{
		for (const auto& [id, pos] : m_chickTargets()) cands.push_back({ id, pos });
	}

	auto findCand = [&cands](int _id) -> int
		{
			for (size_t i = 0; i < cands.size(); ++i)
				if (cands[i].id == _id) return static_cast<int>(i);
			return -1;
		};

	std::vector<int> assign(m_spEnemies.size(), -1);

	// 乗り換えのクールダウンは1フレームに1回だけ進める。
	// 敵ごとのループ内で回すと、頭数ぶん早く切れて標的がぶれ続ける。
	{
		const float dt = Time::Instance().GetDeltaTime();
		for (auto& [key, timer] : m_retargetTimer) timer = std::max(0.0f, timer - dt);
	}

	for (size_t i = 0; i < m_spEnemies.size(); ++i)
	{
		auto& enemy = m_spEnemies[i];
		if (!enemy->IsTargetable()) { enemy->ClearTarget(); continue; }

		// 咥えている敵は戦線から外す。標的も持ち場も要らない。
		if (enemy->IsCarrying())
		{
			enemy->ClearApproachPoint();
			enemy->SetTarget(targetPos, hasTarget);	// 背を向ける先としてだけ使う
			continue;
		}

		// 猶予中は誰も狙わない。視界も標的の有無で決まるので、これで気付かなくなる。
		if (!m_exposed)
		{
			enemy->ClearTarget();
			enemy->ClearApproachPoint();
			continue;
		}

		const int key = static_cast<int>(enemy->GetEntity().index);

		// 今の標的
		int cur = -1;
		if (auto it = m_targetOf.find(key); it != m_targetOf.end())
			cur = findCand(it->second);

		const bool locked = enemy->HasAttackToken()
			|| enemy->IsWindingUp() || enemy->IsAttackNotice()
			|| enemy->IsRetargetLocked();
		const bool cooling = (cur >= 0) && (m_retargetTimer[key] > 0.0f);

		int pick = cur;
		if (cur < 0 || (!locked && !cooling))
		{
			// 視界内で一番近いひよこ。いなければプレイヤー。
			int   bestChick = -1;
			float best = FLT_MAX;
			const Math::Vector3 self = enemy->GetPosition();

			for (size_t t = 1; t < cands.size(); ++t)
			{
				if (!enemy->IsNear(cands[t].pos, m_tuning.chickNoticeRadius)) continue;
				const float d = DistSqXZ(self, cands[t].pos);
				if (d < best) { best = d; bestChick = static_cast<int>(t); }
			}

			if (bestChick < 0)       pick = 0;
			else if (cur >= 1 && enemy->IsNear(cands[cur].pos, m_tuning.chickNoticeRadius)) pick = cur;
			else                     pick = bestChick;
		}

		// 標的が変わったら古い持ち場は捨てる
		if (pick != cur)
		{
			enemy->ClearApproachPoint();
			m_retargetTimer[key] = m_tuning.retargetCooldown;
		}

		m_targetOf[key] = cands[pick].id;
		assign[i] = pick;
		enemy->SetTarget(cands[pick].pos, hasTarget);

		enemy->SetTargetAimHeight(pick == 0 ? -1.0f : m_tuning.chickAimHeight);
	}

	std::vector<Math::Vector3> targets;
	targets.reserve(cands.size());
	for (const auto& c : cands) targets.push_back(c.pos);

	// 攻撃権と持ち場は、各敵を更新する前に決める
	UpdateAttackTokens(hasTarget, static_cast<int>(cands.size()), assign);
	UpdateFormation(targets, assign);

	for (auto& enemy : m_spEnemies) enemy->Update();
}
void EnemyManager::PostUpdate()
{
	// 死亡演出が終わった敵だけを取り除く
	m_spEnemies.erase(
		std::remove_if(m_spEnemies.begin(), m_spEnemies.end(),
			[](const auto& enemy) {return !enemy->IsActive(); }),
		m_spEnemies.end());

	for (auto& enemy : m_spEnemies)
	{
		enemy->PostUpdate();
		if (enemy->ConsumeDeathCount()) ++m_phaseKilled;
	}

	// ---- 手を出したら猶予は終わり ----
	// 円の中から殴っても隠れ続けられると、安全圏から削る遊び方が残る。
	if (!m_exposed)
	{
		for (const auto& ev : CombatFx::Instance().Events())
		{
			if (ev.attackerType != Collision::ObjectType::PlayerAttack) continue;
			Expose("attacked");
			break;
		}
	}

	// ---- とどめ演出 ----
	for (const auto& ev : CombatFx::Instance().Events())
	{
		if (!ev.lethal) continue;
		if (ev.attackerType != Collision::ObjectType::PlayerAttack) continue;

		const bool last = (GetAliveCount() <= 0);
		CombatFx::Instance().RequestKillFx(last);

		//------------------------------------------
		// ラストキルの間
		//------------------------------------------
		if (!last || m_lastKillDone) continue;

		m_lastKillDone = true;

		const Math::Vector3& vib = m_tuning.lastKillVibration;
		CombatFx::Instance().RequestHitStop(m_tuning.lastKillHitStop);
		CombatFx::Instance().AddTrauma(m_tuning.lastKillTrauma);
		CombatFx::Instance().AddVibration(vib.x, vib.y, vib.z);

		GPUParticleSystem::Instance().PlayEffect(m_tuning.lastKillEffect, ev.position);
		GPUParticleSystem::Instance().PlayEffect("Blow", ev.position);
	}

	UpdateCalls();

	//------------------------------------------
	// 砕けの連鎖
	//------------------------------------------
	for (const auto& source : m_spEnemies)
	{
		if (!source->ConsumeShatterBurst()) continue;

		// 世代の上限に達したか、時間切れで配る分が残っていない。
		const float transfer = source->GetShatterTransfer();
		if (transfer <= 0.0f) continue;

		const Math::Vector3 origin = source->GetPosition();
		const int depth = source->GetChainDepth();

		// 溜め込んだぶんだけ範囲も広がる
		const float t = std::clamp(transfer / m_tuning.shatterFullTransfer, 0.0f, 1.0f);
		const float radius = m_tuning.shatterChainRadius * (m_tuning.shatterRadiusMin + (1.0f - m_tuning.shatterRadiusMin) * t);
		const float r2 = radius * radius;

		int chained = 0;
		for (const auto& other : m_spEnemies)
		{
			if (other.get() == source.get()) continue;
			if (!other->IsTargetable()) continue;

			Math::Vector3 d = other->GetPosition() - origin;
			if (std::fabs(d.y) > m_tuning.shatterChainHeight) continue;

			d.y = 0.0f;
			const float distSq = d.LengthSquared();
			if (distSq > r2) continue;

			// 距離減衰
			const float k = 1.0f - (std::sqrt(distSq) / radius);
			const float damage = transfer * (m_tuning.shatterFalloffMin + (1.0f - m_tuning.shatterFalloffMin) * k);

			other->BlastByShatter(origin, damage, depth);
			++chained;
		}

		if (chained <= 0) continue;

		// 巻き込んだ数が多いほど強く止めて揺らす
		CombatFx::Instance().RequestHitStop(std::min(
			m_tuning.shatterHitStopBase + chained * m_tuning.shatterHitStopPerHit, m_tuning.shatterMaxHitStop));
		CombatFx::Instance().AddTrauma(m_tuning.shatterTraumaBase + chained * m_tuning.shatterTraumaPerHit);

		// 最初の連鎖でたくさん巻き込んだときだけ、少しのあいだ減速して見せる。
		// (DelayCall に何もキャプチャしていないので、途中でシーンが変わっても安全)
		if (chained >= m_tuning.chainSlowCount && depth == 0)
		{
			CombatFx::Instance().SetSlow(m_tuning.chainSlowScale);
			Time::Instance().DelayCall(m_tuning.chainSlowSec, []() {CombatFx::Instance().SetSlow(1.0f); }, false);
		}
	}

	//------------------------------------------
	// 死体のなぎ倒し
	//------------------------------------------
	for (const auto& corpse : m_spEnemies)
	{
		if (!corpse->IsCorpseFlying()) continue;

		const Math::Vector3 origin = corpse->GetPosition();
		const Math::Vector3 velocity = corpse->GetKnockbackVelocity();

		// 体格そのままが凶器になる。Heavy を飛ばす価値をここで作る。
		const float scale = corpse->GetSpec().bodyScale;
		const float radius = m_tuning.corpseSlamRadius * scale;
		const float damage = m_tuning.corpseSlamDamage * scale;
		const float r2 = radius * radius;

		Math::Vector3 forward = velocity;
		forward.y = 0.0f;
		const bool hasForward = (forward.LengthSquared() > 0.0f);
		if (hasForward) forward.Normalize();

		int slammed = 0;
		for (const auto& other : m_spEnemies)
		{
			if (other.get() == corpse.get()) continue;
			if (!other->IsTargetable()) continue;

			Math::Vector3 d = other->GetPosition() - origin;
			if (std::fabs(d.y) > m_tuning.corpseSlamHeight) continue;

			d.y = 0.0f;
			const float distSq = d.LengthSquared();
			if (distSq > r2) continue;

			// 進行方向の前方だけ巻き込む。
			// 全方向にすると、すれ違った後ろの敵まで巻き添えになって嘘に見える。
			if (hasForward && distSq > 0.0f)
			{
				Math::Vector3 n = d;
				n.Normalize();
				if (n.Dot(forward) < m_tuning.corpseSlamCone) continue;
			}

			other->SlamByCorpse(origin, velocity, damage);
			++slammed;
		}

		if (slammed <= 0) continue;

		corpse->OnCorpseSlammed(slammed);

		CombatFx::Instance().RequestHitStop(m_tuning.corpseSlamHitStopBase + slammed * m_tuning.corpseSlamHitStopPerHit);
		CombatFx::Instance().AddTrauma(m_tuning.corpseSlamTraumaBase + slammed * m_tuning.corpseSlamTraumaPerHit);
	}
}

//============================================================
// AIデバッグビューア
//============================================================
void EnemyManager::DrawDebugImGui()
{
	if (!ImGui::Begin("Enemy AI"))
	{
		ImGui::End();
		return;
	}

	//------------------------------------------
	// 群れ全体
	//------------------------------------------
	const int alive = GetAliveCount();

	ImGui::Text("Alive %d / Peak %d / Spawned %d",
		alive, m_peakAlive, m_spawnedTotal);
	ImGui::Text("Corpses %d", static_cast<int>(m_spEnemies.size()) - alive);
	ImGui::Text("Squads %d (engaged %d)",
		static_cast<int>(m_squads.size()), GetEngagedSquadCount());

	ImGui::Text("Morale");
	ImGui::SameLine();
	ImGui::ProgressBar(m_morale, ImVec2(-1.0f, 0.0f));

	{
		bool reinforce = m_autoReinforce;
		if (ImGui::Checkbox("Auto reinforce", &reinforce)) m_autoReinforce = reinforce;
	}

	int commits = 0;
	int harass = 0;
	int fleeing = 0;
	for (const auto& enemy : m_spEnemies)
	{
		if (!enemy->HasAttackToken()) continue;
		if (enemy->GetAttackRole() == AttackRole::Harass) ++harass;
		else                                              ++commits;
	}

	ImGui::Text("Token  Commit %d/%d   Harass %d/%d",
		commits, m_tuning.maxCommit, harass, m_tuning.maxHarass);

	// 権利を欲しがっている数。0が続くなら wishRange か持ち場の距離を疑う。
	int wanting = 0;
	for (const auto& enemy : m_spEnemies)
	{
		if (enemy->WantsAttackToken()) ++wanting;
	}
	ImGui::Text("Wanting %d", wanting);

	//------------------------------------------
	// 開始地点の猶予
	//------------------------------------------
	ImGui::Separator();
	if (m_zoneEnabled)
	{
		const float dist = std::sqrt(DistSqXZ(m_lastTargetPos, m_zoneCenter));

		ImGui::Text("StartZone  %s  (reason: %s)",
			m_exposed ? "EXPOSED" : "hidden", m_exposed ? m_exposeReason : "-");
		ImGui::Text("  center (%.1f, %.1f)  player dist %.1f / %.1f",
			m_zoneCenter.x, m_zoneCenter.z, dist, m_zoneRadius);

		if (!m_exposed)
		{
			ImGui::SameLine();
			ImGui::ProgressBar(std::clamp(dist / m_zoneRadius, 0.0f, 1.0f), ImVec2(-1.0f, 0.0f));
			if (ImGui::Button("Expose now")) Expose("debug");
		}

		const char* raid = m_raidActive ? "active" : (m_raidPending ? "pending" : "-");
		ImGui::Text("  Raid %s  squads %d  t=%.1f", raid,
			static_cast<int>(m_raidSquads.size()), m_raidTimer);
		if (m_raidActive)
			ImGui::Text("  raid point (%.1f, %.1f)", m_raidPos.x, m_raidPos.z);
	}
	else
	{
		ImGui::TextDisabled("StartZone off");
	}

	ImGui::Separator();

	//------------------------------------------
	// 個体一覧
	//------------------------------------------
	static const ImVec4 kCommitColor{ 1.0f, 0.45f, 0.35f, 1.0f };
	static const ImVec4 kHarassColor{ 1.0f, 0.85f, 0.35f, 1.0f };
	static const ImVec4 kIdleColor{ 0.70f, 0.70f, 0.70f, 1.0f };

	for (const auto& squad : m_squads)
	{
		ImGui::PushID(squad.id);

		ImGui::Separator();
		ImGui::Text("Squad %d  %-8s  %d/%d  %s",
			squad.id, squad.name, squad.alive, squad.peak,
			squad.engaged ? "ENGAGED" : "idle");

		ImGui::Text("morale");
		ImGui::SameLine();
		ImGui::ProgressBar(squad.morale, ImVec2(-1.0f, 0.0f));

		ImGui::PopID();
	}

	ImGui::Separator();

	int index = 0;
	for (const auto& enemy : m_spEnemies)
	{
		ImGui::PushID(index);

		const AttackRole role = enemy->GetAttackRole();
		const ImVec4& color = enemy->HasAttackToken()
			? ((role == AttackRole::Harass) ? kHarassColor : kCommitColor)
			: kIdleColor;

		const char* roleName = enemy->HasAttackToken()
			? ((role == AttackRole::Harass) ? "Harass" : "Commit") : "-";

		ImGui::TextColored(color, "[%02d] S%-2d %-7s %-7s %-6s HP %3d%%  Poise %3d%%",
			index,
			enemy->GetSquadId(),
			enemy->GetTypeName(),
			enemy->GetStateLabel(),
			roleName,
			static_cast<int>(enemy->GetHealthRatio() * 100.0f),
			static_cast<int>(enemy->GetPoiseRatio() * 100.0f));

		if (ImGui::TreeNode("detail"))
		{
			const Math::Vector3 pos = enemy->GetPosition();

			ImGui::Text("pos    %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);
			ImGui::Text("target %.1f m", enemy->GetTargetDistance());
			ImGui::Text("slot   %.1f m (bias %.1f) %s",
				enemy->GetApproachDistance(), enemy->GetSlotDistanceBias(),
				enemy->IsAtApproachPoint() ? "ARRIVED" : "");
			ImGui::Text("flags  %s%s%s%s%s%s%s",
				enemy->IsFrozen() ? "Frozen " : "",
				enemy->IsStaggered() ? "Stagger " : "",
				enemy->IsWindingUp() ? "Windup " : "",
				enemy->IsAttackNotice() ? "Notice " : "",
				enemy->IsFleeCooling() ? "FleeCD " : "",
				enemy->IsTokenCooling() ? "TokenCD " : "",
				enemy->IsLeaving() ? "Leaving " : "");

			if (enemy->IsAttackNotice())
			{
				ImGui::Text("notice");
				ImGui::SameLine();
				ImGui::ProgressBar(enemy->GetNoticeProgress(), ImVec2(-1.0f, 0.0f));
			}

			// ステートマシン本体のビューア(既存)
			enemy->DrawDebugImGui();

			ImGui::TreePop();
		}

		ImGui::PopID();
		++index;
	}

	ImGui::Separator();

	//------------------------------------------
	// デバッグ配置(部隊のセーブ＆ロード)
	//------------------------------------------
	DrawDebugSpawnPlacer();

	ImGui::End();
}

void EnemyManager::SpawnPhase(int _phaseIndex)
{
	// 手で置いた地点から、そのフェーズのものだけを展開する。
	// 自動生成だとステージの外や届かない場所へ湧くので、配置データを正とする。
	int squads = 0;

	for (const auto& e : m_debugSpawnEntries)
	{
		if (e.phase >= 0 && e.phase != _phaseIndex) continue;

		SpawnSquadAt(e.anchor, e.squadName, e.range);
		++squads;
	}

	// 置き忘れに気付けるようにする。無言で0体だと原因が分からない。
	if (squads <= 0)
	{
		assert(false && "このフェーズの配置が置かれていません");
	}

	// このフェーズで出した数。前フェーズの死体は混ぜない。
	m_phaseTotal = 0;
	for (const auto& e : m_spEnemies)
	{
		if (e->IsTargetable()) ++m_phaseTotal;
	}

	// 士気の分母もフェーズごとに引き直す
	m_peakAlive = 0;
	m_phaseKilled = 0;
	m_lastKillDone = false;
}

//============================================================
// ひよこの連れ去り
//============================================================
int EnemyManager::RequestCarrier(const Math::Vector3& _chickPos, int _chickId)
{
	EnemyBase* best = nullptr;
	float bestDist = m_tuning.carryPickRadius * m_tuning.carryPickRadius;

	for (const auto& enemy : m_spEnemies)
	{
		if (!enemy->CanCarry()) continue;

		const float d = DistSqXZ(enemy->GetPosition(), _chickPos);
		if (d >= bestDist) continue;

		bestDist = d;
		best = enemy.get();
	}

	if (!best) return -1;

	best->BeginCarry(_chickId);
	return static_cast<int>(best->GetEntity().index);
}

namespace {
	// 指名番号から敵を引く。番号はエンティティ番号そのもの。
	EnemyBase* FindEnemyByIndex(
		const std::vector<std::shared_ptr<EnemyBase>>& _enemies, int _index)
	{
		if (_index < 0) return nullptr;

		for (const auto& enemy : _enemies)
		{
			if (static_cast<int>(enemy->GetEntity().index) == _index) return enemy.get();
		}
		return nullptr;
	}
}

bool EnemyManager::GetCarrierPose(int _carrierId, int _chickId, CarrierInfo& _out) const
{
	_out.scale = 1.0f;
	_out.leaving = false;
	_out.gone = false;

	// 走り去って片付けられた運び手。咥えられていた子はもう戻らない。
	// 番号は使い回されることがあるので、敵を探すより先に見る。
	if (const auto it = m_vanishedCarriers.find(_carrierId);
		it != m_vanishedCarriers.end() && it->second == _chickId)
	{
		_out.gone = true;
		return false;
	}

	EnemyBase* enemy = FindEnemyByIndex(m_spEnemies, _carrierId);
	if (!enemy) return false;

	// 口を開いた時点でも足元は要る
	_out.foot = enemy->GetPosition();

	if (!enemy->IsCarrying()) return false;
	if (enemy->GetCarryChickId() != _chickId) return false;

	// 片付けの前に消え切ったのを見つけた場合
	if (enemy->IsVanished())
	{
		_out.gone = true;
		return false;
	}

	_out.mouth = enemy->GetMouthPosition();
	_out.leaving = enemy->IsLeaving();
	_out.scale = enemy->GetLeaveScale();
	return true;
}

void EnemyManager::ReleaseCarrier(int _carrierId)
{
	if (EnemyBase* enemy = FindEnemyByIndex(m_spEnemies, _carrierId)) enemy->EndCarry();
}

void EnemyManager::UpdateCalls()
{
	//------------------------------------------
	// 呼び声を配る
	//------------------------------------------
	for (const auto& caller : m_spEnemies)
	{
		if (!caller->ConsumeCallRequest()) continue;

		const Math::Vector3 origin = caller->GetPosition();

		const Math::Vector3 alertPos = m_lastHasTarget ? m_lastTargetPos : origin;

		const float radius = caller->GetSpec().callRadius;
		const float r2 = radius * radius;

		int reached = 0;
		for (const auto& other : m_spEnemies)
		{
			if (other.get() == caller.get()) continue;
			if (!other->IsTargetable()) continue;

			// 同じ部隊には距離を問わず届く
			const bool sameSquad = (other->GetSquadId() == caller->GetSquadId());

			if (!sameSquad)
			{
				Math::Vector3 d = other->GetPosition() - origin;
				d.y = 0.0f;
				if (d.LengthSquared() > r2) continue;
			}

			// 既に見えている/動けない敵は数に入らない
			if (!other->ReceiveCall(alertPos)) continue;

			++reached;

			// 声が届いた部隊はそのまま交戦に入る
			if (Squad* squad = FindSquad(other->GetSquadId())) squad->engaged = true;
		}

		// 何体動いたかを揺れの強さで返す
		if (reached > 0)
			CombatFx::Instance().AddTrauma(std::min(
				m_tuning.callTraumaBase + reached * m_tuning.callTraumaPerHit, m_tuning.callTraumaMax));
	}
}

int EnemyManager::GetAliveCount() const
{
	int count = 0;
	for (const auto& enemy : m_spEnemies)
	{
		if (enemy->IsTargetable()) ++count;
	}
	return count;
}

void EnemyManager::UpdateAttackTokens(bool _hasTarget, int _targetCount,
	const std::vector<int>& _assign)
{
	// 標的を持たない(死体など)敵の権利は無条件で返却
	for (size_t i = 0; i < m_spEnemies.size(); ++i)
		if (_assign[i] < 0) m_spEnemies[i]->ReleaseAttackToken();

	for (int t = 0; t < _targetCount; ++t)
	{
		int commits = 0, harass = 0;

		// 返却と集計
		for (size_t i = 0; i < m_spEnemies.size(); ++i)
		{
			if (_assign[i] != t) continue;
			auto& enemy = m_spEnemies[i];
			if (!enemy->HasAttackToken()) continue;

			if (!_hasTarget || !enemy->WantsAttackToken())
			{
				enemy->ReleaseAttackToken();
				continue;
			}
			if (enemy->GetAttackRole() == AttackRole::Harass) ++harass;
			else                                              ++commits;
		}

		if (!_hasTarget || (commits >= m_tuning.maxCommit && harass >= m_tuning.maxHarass)) continue;

		// 配布(この標的を狙っている敵だけ)
		std::vector<EnemyBase*> candidates;
		for (size_t i = 0; i < m_spEnemies.size(); ++i)
		{
			if (_assign[i] != t) continue;
			auto& enemy = m_spEnemies[i];
			if (!enemy->IsTargetable() || enemy->HasAttackToken()) continue;
			if (!enemy->WantsAttackToken() || enemy->IsTokenCooling()) continue;
			candidates.push_back(enemy.get());
		}

		std::sort(candidates.begin(), candidates.end(),
			[](EnemyBase* a, EnemyBase* b) { return a->GetTargetDistance() < b->GetTargetDistance(); });

		for (auto* enemy : candidates)
		{
			if (commits < m_tuning.maxCommit && !enemy->NeverCommits())
			{
				enemy->GrantAttackToken(AttackRole::Commit); ++commits; continue;
			}
			if (harass < m_tuning.maxHarass)
			{
				enemy->GrantAttackToken(AttackRole::Harass); ++harass; continue;
			}
			break;
		}
	}

	for (auto& enemy : m_spEnemies) enemy->ClearAttackWish();
}

void EnemyManager::UpdateFormation(const std::vector<Math::Vector3>& _targets,
	const std::vector<int>& _assign)
{
	if (m_spEnemies.empty()) return;

	for (size_t t = 0; t < _targets.size(); ++t)
	{
		const float radius = (t == 0) ? m_tuning.ringRadius : m_tuning.chickRingRadius;
		UpdateFormationAround(_targets[t], radius, static_cast<int>(t), _assign);
	}
}

void EnemyManager::UpdateFormationAround(const Math::Vector3& _center, float _radius,
	int _targetIndex, const std::vector<int>& _assign)
{
	// 分割数は設定ファイルで変えられるので固定長配列ではなく vector。最低1つ
	const int slotCount = std::max(m_tuning.slotCount, 1);
	std::vector<Math::Vector3> slots(slotCount);
	std::vector<bool> used(slotCount, false);

	for (int i = 0; i < slotCount; ++i)
	{
		const float a = DirectX::XM_2PI * static_cast<float>(i) / static_cast<float>(slotCount);
		slots[i] = _center + Math::Vector3(std::cos(a), 0.0f, std::sin(a)) * _radius;   // ①
	}

	std::vector<EnemyBase*> sorted;
	for (size_t i = 0; i < m_spEnemies.size(); ++i)
	{
		auto& enemy = m_spEnemies[i];
		if (_assign[i] != _targetIndex) continue;                                       // ② この標的を狙う敵だけ
		if (!enemy->IsTargetable()) continue;
		if (!IsSquadEngaged(enemy->GetSquadId())) continue;
		sorted.push_back(enemy.get());
	}

	std::sort(sorted.begin(), sorted.end(),
		[&_center](EnemyBase* _a, EnemyBase* _b)
		{
			return DistSqXZ(_a->GetPosition(), _center) < DistSqXZ(_b->GetPosition(), _center);
		});

	for (auto* enemy : sorted)
	{
		if (enemy->HasAttackToken())
		{
			Math::Vector3 away = enemy->GetPosition() - _center;
			away.y = 0.0f;
			if (away.LengthSquared() > 0.0f) away.Normalize();
			else                             away = Math::Vector3::Right;

			enemy->SetApproachPoint(_center + away * m_tuning.commitStandOff);	// 中心ではなく手前
			continue;
		}

		const Math::Vector3 pos = enemy->GetPosition();

		int   bestIndex = -1;
		float bestDist = FLT_MAX;
		bool  bestFree = false;

		for (int i = 0; i < slotCount; ++i)
		{
			const float d = DistSqXZ(pos, slots[i]);

			// 空きスロットを最優先。全部埋まっていれば一番近い所へ重ねる。
			const bool better = used[i]
				? (!bestFree && d < bestDist)
				: (!bestFree || d < bestDist);

			if (!better) continue;

			bestIndex = i;
			bestDist = d;
			bestFree = !used[i];
		}

		if (bestIndex < 0) continue;

		Math::Vector3 point = slots[bestIndex];

		// 円の外向きへ個体差ぶんずらす。真円に並ぶと作り物に見える。
		Math::Vector3 outward = point - _center;
		outward.y = 0.0f;
		if (outward.LengthSquared() > 0.0f)
		{
			outward.Normalize();
			// 重なった敵はさらに外へ下げて、内側の敵と押し合わないようにする
			const float extra = used[bestIndex] ? m_tuning.outerOffset : 0.0f;
			point += outward * (enemy->GetSlotDistanceBias() + extra);
		}

		used[bestIndex] = true;
		enemy->SetApproachPoint(point);
	}
}

//============================================================
// 部隊の展開
//============================================================
bool EnemyManager::PickSquadAnchor(Math::Vector3& _out) const
{
	Math::Vector3 best = Math::Vector3::Zero;
	float bestScore = -1.0f;

	for (int attempt = 0; attempt < m_tuning.anchorTries; ++attempt)
	{
		const float angle = KdRandom::GetFloat(0.0f, DirectX::XM_2PI);
		const float radius = KdRandom::GetFloat(m_tuning.spawnRadiusMin, m_tuning.spawnRadiusMax);
		const Math::Vector3 candidate(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);

		// 「一番近い相手との距離」を点数にする。他の部隊とプレイヤーの
		// 両方から離れているほど良い候補になる。
		float score = FLT_MAX;
		for (const auto& squad : m_squads)
		{
			if (squad.alive <= 0) continue;
			score = std::min(score, std::sqrt(DistSqXZ(candidate, squad.anchor)));
		}
		if (m_lastHasTarget)
			score = std::min(score, std::sqrt(DistSqXZ(candidate, m_lastTargetPos)));

		if (score >= m_tuning.squadSeparation) { _out = candidate; return true; }

		if (score > bestScore) { bestScore = score; best = candidate; }
	}
	_out = best;
	return false;
}

void EnemyManager::SpawnMember(const Squad& _squad, EnemyFactory _make)
{
	SpawnMember(_squad, _make, m_tuning.squadSpread);
}

void EnemyManager::SpawnMember(const Squad& _squad, EnemyFactory _make, float _spread)
{
	auto world = m_wpWorld.lock();
	if (!world) return;

	auto enemy = _make(m_cm, world);
	enemy->Init();
	enemy->SetSquadId(_squad.id);

	float groundY = world->RaycastStage(_squad.anchor, -Math::Vector3::Up * 10).hitpos.y;

	// 部隊の中で散らす
	const float angle = KdRandom::GetFloat(0.0f, DirectX::XM_2PI);
	const float radius = KdRandom::GetFloat(0.0f, _spread);

	Math::Vector3 pos = _squad.anchor
		+ Math::Vector3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);

	// 散らした後の位置で地面を取る。
	// アンカーで取ると、斜面では部隊全員が同じ高さに並んで浮く。
	const Math::Vector3 from = pos + Math::Vector3::Up * m_tuning.groundRayUp;
	const Math::Vector3 to = from - Math::Vector3::Up * m_tuning.groundRayLength;

	const auto hit = world->RaycastStage(from, to);
	pos.y = hit.isHit ? hit.hitpos.y : _squad.anchor.y;

	m_cm.get<TransformComponent>(enemy->GetEntity()).SetPosition(pos);

	m_spEnemies.push_back(enemy);
	// 名前は spec 側に一本化する。テーブルの文字列とズレないようにするため。
	Game::SetEntityName(m_cm, enemy->GetEntity(), enemy->GetTypeName(), (int)m_spEnemies.size());

	++m_spawnedTotal;
}

void EnemyManager::SpawnSquad()
{
	if (!m_wpWorld.lock()) return;

	const SquadTemplate& tmpl = PickSquadTemplate();

	Squad squad;
	squad.id = m_nextSquadId++;
	squad.name = tmpl.name;
	PickSquadAnchor(squad.anchor);
	squad.center = squad.anchor;

	int spawned = 0;
	for (const auto& unit : tmpl.units)
	{
		if (unit.make == nullptr) continue;

		for (int i = 0; i < unit.count; ++i)
		{
			SpawnMember(squad, unit.make);
			++spawned;
		}
	}

	squad.alive = spawned;
	squad.peak = spawned;

	m_squads.push_back(squad);

	// 増援が来たなら「最後の1体」はやり直し
	m_lastKillDone = false;
}

// PickSquadAnchor で場所を探す代わりに、指定した地点・範囲で部隊を1つ展開する。
// デバッグ配置(SaveDebugSpawns/LoadDebugSpawns)からの復元専用。
void EnemyManager::SpawnSquadAt(const Math::Vector3& _anchor, const std::string& _squadName, float _range)
{
	if (!m_wpWorld.lock()) return;

	const SquadTemplate* tmpl = FindSquadTemplateByName(_squadName);
	if (!tmpl) return;	// 編成表に無い名前(リネーム等)は無視する

	Squad squad;
	squad.id = m_nextSquadId++;
	squad.name = tmpl->name;
	squad.anchor = _anchor;
	squad.center = _anchor;

	int spawned = 0;
	for (const auto& unit : tmpl->units)
	{
		if (unit.make == nullptr) continue;

		for (int i = 0; i < unit.count; ++i)
		{
			SpawnMember(squad, unit.make, _range);
			++spawned;
		}
	}

	squad.alive = spawned;
	squad.peak = spawned;

	m_squads.push_back(squad);

	m_lastKillDone = false;
}

EnemyManager::Squad* EnemyManager::FindSquad(int _id)
{
	for (auto& squad : m_squads)
	{
		if (squad.id == _id) return &squad;
	}
	return nullptr;
}

const EnemyManager::Squad* EnemyManager::FindSquad(int _id) const
{
	for (const auto& squad : m_squads)
	{
		if (squad.id == _id) return &squad;
	}
	return nullptr;
}

bool EnemyManager::IsSquadEngaged(int _id) const
{
	const Squad* squad = FindSquad(_id);
	return squad ? squad->engaged : false;
}

int EnemyManager::GetEngagedSquadCount() const
{
	int count = 0;
	for (const auto& squad : m_squads)
	{
		if (squad.engaged && squad.alive > 0) ++count;
	}
	return count;
}

void EnemyManager::EngageSquad(Squad& _squad, const Math::Vector3& _alertPos)
{
	if (_squad.engaged) return;
	_squad.engaged = true;

	// 1体ずつ気づいて順番に歩いてくると、群れの圧が出ない。
	// 呼ぶ先は「見つけたもの」の位置。ひよこを見つけたのにプレイヤーへ向かうと嘘になる。
	CallSquadMembers(m_spEnemies, _squad.id, _alertPos);
}

void EnemyManager::UpdateSquads()
{
	//------------------------------------------
	// 集計
	//------------------------------------------
	for (auto& squad : m_squads)
	{
		squad.alive = 0;
		squad.center = Math::Vector3::Zero;
	}

	for (const auto& enemy : m_spEnemies)
	{
		if (!enemy->IsTargetable()) continue;

		Squad* squad = FindSquad(enemy->GetSquadId());
		if (!squad) continue;

		++squad->alive;
		squad->center += enemy->GetPosition();
	}

	for (auto& squad : m_squads)
	{
		if (squad.alive > 0) squad.center /= static_cast<float>(squad.alive);
		else                 squad.center = squad.anchor;

		squad.peak = std::max(squad.peak, squad.alive);
		squad.morale = (squad.peak > 0)
			? static_cast<float>(squad.alive) / static_cast<float>(squad.peak) : 0.0f;
	}

	//------------------------------------------
	// 交戦の開始 + 士気の配布
	//------------------------------------------
	for (const auto& enemy : m_spEnemies)
	{
		Squad* squad = FindSquad(enemy->GetSquadId());
		if (!squad) continue;

		enemy->SetSquadMorale(squad->morale);

		// 猶予中は誰も交戦に入らない
		if (!m_exposed) continue;
		if (squad->engaged) continue;
		if (!enemy->IsTargetable()) continue;

		// プレイヤーだけでなく、ひよこを見つけても交戦に入る
		bool found = enemy->IsTargetInSight();
		Math::Vector3 alertPos = m_lastTargetPos;

		if (!found && m_chickTargets)
		{
			for (const auto& [id, pos] : m_chickTargets())
			{
				if (!enemy->IsNear(pos, m_tuning.chickNoticeRadius)) continue;
				found = true;
				alertPos = pos;
				break;
			}
		}

		if (!found) continue;

		EngageSquad(*squad, alertPos);
	}
}

//============================================================
// 開始地点の猶予
//============================================================
void EnemyManager::UpdateStartZone(const Math::Vector3& _playerPos)
{
	if (!m_zoneEnabled || m_exposed) return;

	// 中心を最初のフレームで確定する
	if (!m_zoneCentered)
	{
		m_zoneCenter = _playerPos;
		m_zoneCentered = true;
		return;
	}

	if (DistSqXZ(_playerPos, m_zoneCenter) <= m_zoneRadius * m_zoneRadius) return;

	Expose("left zone");
}

void EnemyManager::Expose(const char* _reason)
{
	if (m_exposed) return;

	m_exposed = true;
	m_exposeReason = _reason;

	// 先に置き去りのひよこを標的へ戻す。襲撃の目的地はひよこの位置で決めるため。
	if (m_onExposed) m_onExposed();

	DispatchRaid();
}

bool EnemyManager::FindRaidPoint(Math::Vector3& _out) const
{
	if (!m_chickTargets) return false;

	const float r2 = m_zoneRadius * m_zoneRadius;

	Math::Vector3 sum = Math::Vector3::Zero;
	int count = 0;
	for (const auto& [id, pos] : m_chickTargets())
	{
		if (DistSqXZ(pos, m_zoneCenter) > r2) continue;
		sum += pos;
		++count;
	}

	if (count <= 0) return false;

	_out = sum / static_cast<float>(count);
	return true;
}

void EnemyManager::DispatchRaid()
{
	m_raidSquads.clear();
	m_raidActive = false;
	m_raidPending = false;

	// 全員連れて出たなら襲う理由が無い。何も起こさない。
	if (!FindRaidPoint(m_raidPos)) return;

	// 開始地点に近い部隊から選ぶ
	std::vector<std::pair<float, int>> order;
	for (const auto& squad : m_squads)
	{
		if (squad.alive <= 0) continue;
		order.emplace_back(DistSqXZ(squad.center, m_raidPos), squad.id);
	}

	// まだ1部隊も湧いていない。湧いたら改めて送る。
	if (order.empty())
	{
		m_raidPending = true;
		return;
	}

	std::sort(order.begin(), order.end());

	const int count = std::min(m_tuning.raidSquadCount, static_cast<int>(order.size()));
	for (int i = 0; i < count; ++i)
	{
		const int id = order[i].second;
		m_raidSquads.push_back(id);

		// 交戦扱いにして、目的地へ呼ぶ
		if (Squad* squad = FindSquad(id)) squad->engaged = true;
		CallSquadMembers(m_spEnemies, id, m_raidPos);
	}

	m_raidActive = true;
	m_raidTimer = 0.0f;
	m_raidRecall = m_tuning.raidRecallSec;
}

bool EnemyManager::IsRaider(int _squadId) const
{
	if (!m_raidActive) return false;
	return std::find(m_raidSquads.begin(), m_raidSquads.end(), _squadId) != m_raidSquads.end();
}

void EnemyManager::UpdateRaid()
{
	// 露見した時に部隊が居なかった。湧くのを待って送り直す。
	if (m_raidPending)
	{
		DispatchRaid();
		return;
	}

	if (!m_raidActive) return;

	const float dt = Time::Instance().GetDeltaTime();
	m_raidTimer += dt;

	// 置き去りのひよこが居なくなった(連れ出された/攫った)なら打ち切る
	Math::Vector3 point;
	if (!FindRaidPoint(point)) { m_raidActive = false; return; }
	m_raidPos = point;

	// 到着と全滅を見る
	const float arrive = m_zoneRadius * m_tuning.raidArriveRatio;
	bool anyAlive = false;
	bool allArrived = true;

	for (int id : m_raidSquads)
	{
		const Squad* squad = FindSquad(id);
		if (!squad || squad->alive <= 0) continue;

		anyAlive = true;
		if (DistSqXZ(squad->center, m_raidPos) > arrive * arrive) allArrived = false;
	}

	// 着いた後は普通の索敵に任せる。ひよこは見える距離に居る。
	if (!anyAlive || allArrived || m_raidTimer >= m_tuning.raidMaxSec)
	{
		m_raidActive = false;
		return;
	}

	// 捜索は数秒で諦めるので、着くまで呼び直す
	m_raidRecall -= dt;
	if (m_raidRecall > 0.0f) return;
	m_raidRecall = m_tuning.raidRecallSec;

	for (int id : m_raidSquads) CallSquadMembers(m_spEnemies, id, m_raidPos);
}

void EnemyManager::SpownLogic()
{
	if (!m_autoReinforce) return;

	// 全滅した部隊を片付ける。死体が残っていても構わない
	// (所属が引けなくなるだけで、死体側は部隊を見ていない)。
	m_squads.erase(
		std::remove_if(m_squads.begin(), m_squads.end(),
			[](const Squad& _s) { return _s.peak > 0 && _s.alive <= 0; }),
		m_squads.end());

	if (static_cast<int>(m_squads.size()) >= m_tuning.maxSquads) return;

	// 交戦中の部隊が残っている間は送り込まない。
	// 常時補充すると戦闘に区切りが無くなり、山場を作れない。
	if (GetEngagedSquadCount() > 0) return;

	SpawnSquad();
}

//============================================================
// デバッグ配置(部隊のセーブ＆ロード)
//============================================================
void EnemyManager::SaveDebugSpawns() const
{
	nlohmann::json json = nlohmann::json::array();

	for (const auto& entry : m_debugSpawnEntries)
	{
		json.push_back({
			{ "squad", entry.squadName },
			{ "pos",   { entry.anchor.x, entry.anchor.y, entry.anchor.z } },
			{ "range", entry.range },
			{ "phase", entry.phase },
			});
	}

	std::ofstream ofs(m_tuning.debugSpawnFile);
	if (!ofs.is_open()) return;

	constexpr int kJsonIndent = 4;
	ofs << json.dump(kJsonIndent);
}

void EnemyManager::LoadDebugSpawns()
{
	m_debugSpawnEntries.clear();

	std::ifstream ifs(m_tuning.debugSpawnFile);
	if (!ifs.is_open()) return;

	nlohmann::json json;
	try
	{
		ifs >> json;
	}
	catch (const nlohmann::json::exception&)
	{
		return;	// 壊れたJSONは無視する
	}

	if (!json.is_array()) return;

	for (const auto& item : json)
	{
		// 手編集や仕様変更でおかしな要素が混ざっていても、その1件だけ捨てて続行する。
		try
		{
			if (!item.is_object()) continue;

			const std::string squadName = item.value("squad", std::string());
			if (squadName.empty()) continue;

			DebugSpawnEntry entry;
			entry.squadName = squadName;
			entry.range = item.value("range", m_tuning.squadSpread);

			const auto pos = item.value("pos", std::vector<float>{ 0.0f, 0.0f, 0.0f });
			if (pos.size() == 3) entry.anchor = Math::Vector3(pos[0], pos[1], pos[2]);

			entry.phase = item.value("phase", -1);

			m_debugSpawnEntries.push_back(entry);
		}
		catch (const nlohmann::json::exception&)
		{
			continue;
		}
	}
}

void EnemyManager::SpawnSavedDebugSquads()
{
	for (const auto& entry : m_debugSpawnEntries)
		SpawnSquadAt(entry.anchor, entry.squadName, entry.range);
}

//============================================================
// デバッグ配置UI
//============================================================
void EnemyManager::DrawDebugSpawnPlacer()
{
	if (!ImGui::CollapsingHeader("Debug Squad Placement"))
	{
		// 畳んだら仮スポーンを片付けておく。放置すると通常のプレイに紛れ込む。
		ClearDebugPreviewMarkers();
		return;
	}


	// 表示を絞る。編集中のフェーズだけ見たい時に使う。
	static const char* kViewNames[] = { "全部", "Phase 1", "Phase 2", "Phase FINAL" };
	int view = m_debugViewPhase + 1;
	if (ImGui::Combo("表示", &view, kViewNames, IM_ARRAYSIZE(kViewNames)))
	{
		m_debugViewPhase = view - 1;
	}

	// フェーズごとの配置数。置き忘れをここで見つける。
	int count[3] = {};
	for (const auto& e : m_debugSpawnEntries)
	{
		if (e.phase < 0) { count[0]++; count[1]++; count[2]++; continue; }
		if (e.phase >= 0 && e.phase < 3) ++count[e.phase];
	}
	ImGui::Text("配置数  P1:%d  P2:%d  FINAL:%d", count[0], count[1], count[2]);

	ImGui::Separator();

	//------------------------------------------
	// 配置地点
	//------------------------------------------
	if (ImGui::Button(U8("プレイヤー位置を取得")))
	{
		if (m_lastHasTarget) m_debugAnchor = m_lastTargetPos;
	}

	ImGui::Text("Anchor  %.1f, %.1f, %.1f", m_debugAnchor.x, m_debugAnchor.y, m_debugAnchor.z);
	constexpr float kUiAnchorStep = 0.5f;	// ドラッグ1ピクセルあたりの移動量
	ImGui::DragFloat3(U8("手動調整##DebugAnchor"), &m_debugAnchor.x, kUiAnchorStep);

	//------------------------------------------
	// 範囲と部隊
	//------------------------------------------
	ImGui::SliderFloat("範囲##DebugRange", &m_debugRange, 1.0_m, 30.0_m, "%.1f m");

	// 編成表の名前一覧(コンボ用)。テーブルは静的なので一度だけ作る。
	static const std::vector<const char*> kSquadNames = [] {
		std::vector<const char*> names;
		for (const auto& t : kSquadTable) names.push_back(t.name);
		return names;
		}();

	ImGui::Combo(U8("部隊##DebugSquad"), &m_debugSquadIndex, kSquadNames.data(), static_cast<int>(kSquadNames.size()));

	//------------------------------------------
	// 範囲プレビュー(端4点にノーマルを仮スポーン)
	//------------------------------------------
	ImGui::Checkbox(U8("範囲プレビュー(仮スポーン)"), &m_debugPreviewEnabled);

	if (m_debugPreviewEnabled) UpdateDebugPreviewMarkers();
	else                       ClearDebugPreviewMarkers();

	static const char* kPhaseNames[] = { "Phase 1", "Phase 2", "Phase FINAL", "全フェーズ" };

	int sel = (m_debugPhase < 0) ? 3 : std::clamp(m_debugPhase, 0, 2);
	if (ImGui::Combo("フェーズ", &sel, kPhaseNames, IM_ARRAYSIZE(kPhaseNames)))
	{
		m_debugPhase = (sel >= 3) ? -1 : sel;
	}

	//------------------------------------------
	// 保存
	//------------------------------------------
	ImGui::Separator();

	if (ImGui::Button(U8("この配置を保存")))
	{
		DebugSpawnEntry entry;
		entry.squadName = kSquadNames[m_debugSquadIndex];
		entry.anchor = m_debugAnchor;
		entry.range = m_debugRange;
		entry.phase = m_debugPhase;

		m_debugSpawnEntries.push_back(entry);
		SaveDebugSpawns();
	}

	ImGui::Text(U8("保存済み配置 (%d)"), static_cast<int>(m_debugSpawnEntries.size()));

	int eraseIndex = -1;
	for (int i = 0; i < static_cast<int>(m_debugSpawnEntries.size()); ++i)
	{
		ImGui::PushID(i);

		const auto& e = m_debugSpawnEntries[i];

		if (m_debugViewPhase >= 0 && e.phase >= 0 && e.phase != m_debugViewPhase)
		{
			// PushID した分は必ず戻す。continue で飛ばすとIDが積み上がる。
			ImGui::PopID();
			continue;
		}

		// フェーズも一緒に出す。どの波のものか一覧で分かるように。
		const char* pn = (e.phase < 0) ? "ALL" : kPhaseNames[std::clamp(e.phase, 0, 2)];
		ImGui::Text("[%s] %s  r=%.1f", pn, e.squadName.c_str(), e.range);

		const auto& entry = m_debugSpawnEntries[i];
		ImGui::Text("%-10s (%.1f, %.1f, %.1f)  r=%.1f",
			entry.squadName.c_str(), entry.anchor.x, entry.anchor.y, entry.anchor.z, entry.range);

		ImGui::SameLine();
		if (ImGui::Button(U8("削除"))) eraseIndex = i;

		ImGui::PopID();
	}

	if (eraseIndex >= 0)
	{
		m_debugSpawnEntries.erase(m_debugSpawnEntries.begin() + eraseIndex);
		SaveDebugSpawns();
	}
}

void EnemyManager::UpdateDebugPreviewMarkers()
{
	// 「一番端4つ」= 範囲(半径)ぶんX/Z軸それぞれの端。
	// 円として配りたい場合はここを角度分割に変えればよい。
	const std::array<Math::Vector3, 4> corners = {
		m_debugAnchor + Math::Vector3(m_debugRange, 0.0f, 0.0f),
		m_debugAnchor + Math::Vector3(-m_debugRange, 0.0f, 0.0f),
		m_debugAnchor + Math::Vector3(0.0f, 0.0f,  m_debugRange),
		m_debugAnchor + Math::Vector3(0.0f, 0.0f, -m_debugRange),
	};

	// 数が合っていなければ作り直す(プレビューON直後など)。
	if (m_debugPreviewEnemies.size() != corners.size())
	{
		ClearDebugPreviewMarkers();
		for (const auto& pos : corners) SpawnDebugPreviewMember(pos);
		return;
	}

	// 既にあれば位置だけ追従させる。スライダー操作のたびに湧き直さない。
	for (size_t i = 0; i < corners.size(); ++i)
	{
		m_cm.get<TransformComponent>(m_debugPreviewEnemies[i]->GetEntity()).SetPosition(corners[i]);
	}
}

void EnemyManager::SpawnDebugPreviewMember(const Math::Vector3& _pos)
{
	auto world = m_wpWorld.lock();
	if (!world) return;

	// 仮スポーンは実在のNormalを使う。あくまで見た目の目安なので
	// m_spawnedTotal は増やさない(HUDの集計を汚さないため)。
	auto enemy = MakeEnemy<Normal>(m_cm, world);
	enemy->Init();
	enemy->SetSquadId(kDebugPreviewSquadId);
	m_cm.get<TransformComponent>(enemy->GetEntity()).SetPosition(_pos);

	m_spEnemies.push_back(enemy);
	m_debugPreviewEnemies.push_back(enemy);

	Game::SetEntityName(m_cm, enemy->GetEntity(), "DebugPreview", (int)m_debugPreviewEnemies.size());
}

void EnemyManager::ClearDebugPreviewMarkers()
{
	if (m_debugPreviewEnemies.empty()) return;

	for (const auto& preview : m_debugPreviewEnemies)
	{
		m_spEnemies.erase(
			std::remove(m_spEnemies.begin(), m_spEnemies.end(), preview),
			m_spEnemies.end());
	}
	m_debugPreviewEnemies.clear();
}