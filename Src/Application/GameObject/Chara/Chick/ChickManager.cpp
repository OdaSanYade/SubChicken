#include "ChickManager.h"
#include "Chick.h"
#include "../IChara.h"

void ChickManager::Init()
{
	Tuning::Load(m_tuning, ChickTuningKey::kFile, "ChickManager");
	Tuning::Load(m_chickTuning, ChickTuningKey::kFile, "Chick");

	m_slotUsed.assign(SpawnCount(), false);
}

int ChickManager::SpawnCount() const
{
	const int points = static_cast<int>(m_tuning.spawnPoints.size());

	// 前は static_assert で止めていた条件。設定ファイルで変えられるようにしたので実行時に丸める
	assert(m_tuning.chickCount <= points && "ChickManager: 候補地点より多くのひよこは置けません");
	return std::clamp(m_tuning.chickCount, 0, points);
}

// ===========================================================
// 配置
// ===========================================================
void ChickManager::SpawnAll()
{
	auto world = m_wpWorld.lock();
	if (!world) return;

	// 2回呼ばれても数が増えていかないよう、前の分は片付けてから置く
	m_spChicks.clear();

	const int count = SpawnCount();
	const int pointCount = static_cast<int>(m_tuning.spawnPoints.size());

	//------------------------------------------
	// 候補地点から重複なく選ぶ(先頭 count 個だけのシャッフル)
	//------------------------------------------
	std::vector<int> order(pointCount);
	for (int i = 0; i < pointCount; ++i) order[i] = i;

	for (int i = 0; i < count; ++i)
	{
		const int j = KdRandom::GetInt(i, pointCount - 1);
		std::swap(order[i], order[j]);
	}

	//------------------------------------------
	// 生成
	//------------------------------------------
	for (int i = 0; i < count; ++i)
	{
		auto chick = std::make_shared<Chick>(m_cm, world);
		chick->SetTuning(m_chickTuning);
		chick->Init();

		Math::Vector3 pos = m_tuning.spawnPoints[order[i]];

		pos.y = SampleGroundY(pos, pos.y);

		m_cm.get<TransformComponent>(chick->GetEntity()).SetPosition(pos);

		// 置いた場所が「元の位置」
		chick->SetHomePosition(pos);

		m_spChicks.push_back(chick);
		Game::SetEntityName(m_cm, chick->GetEntity(), "Chick", i + 1);
	}

	m_slotUsed.assign(count, false);

	m_spawnedCount = count;
	m_lostCount = 0;
}

std::vector<std::pair<int, Math::Vector3>> ChickManager::GetEnemyTargets() const
{
	std::vector<std::pair<int, Math::Vector3>> out;
	for (const auto& chick : m_spChicks)
	{
		if (!chick->IsEnemyTarget()) continue;

		// 敵側では 0 を「プレイヤー」に使っているので、ひよこは +1 して重ならないようにしている
		out.emplace_back(static_cast<int>(chick->GetEntity().index) + 1, chick->GetPosition());
	}
	return out;
}

std::vector<ChickManager::IndicatorTarget> ChickManager::GetIndicatorTargets() const
{
	std::vector<IndicatorTarget> out;
	for (const auto& chick : m_spChicks)
	{
		if (!chick->IsAlive()) continue;
		if (chick->IsRescued())continue;

		const bool carried = chick->IsCarried();

		// 走り去っている子は追いかけようが無いので出さない
		if (carried && chick->IsCarryLeaving()) continue;

		// 消すのは追従中だけ。連れ去り中は常に出す。
		if (!carried && chick->IsFollowing()) continue;

		IndicatorTarget t;
		t.id = static_cast<int>(chick->GetEntity().index);
		t.position = chick->GetPosition();
		t.carried = carried || chick->IsDown();
		out.push_back(t);
	}
	return out;
}

int ChickManager::RevealIdleChicks()
{
	int count = 0;
	for (const auto& chick : m_spChicks)
	{
		if (!chick->IsAlive()) continue;
		if (chick->IsRescued()) continue;	// 巣に着いた子はもう関係ない
		if (!chick->IsHide()) continue;		// 一度拾われた子は既に見えている

		chick->StopHide();
		++count;
	}
	return count;
}

float ChickManager::SampleGroundY(const Math::Vector3& _pos, float _fallbackY) const
{
	auto world = m_wpWorld.lock();
	if (!world) return _fallbackY;

	const Math::Vector3 from = _pos + Math::Vector3::Up * m_tuning.groundRayUp;
	const Math::Vector3 to = from - Math::Vector3::Up * m_tuning.groundRayLength;

	const auto hit = world->RaycastStage(from, to);
	return hit.isHit ? hit.hitpos.y : _fallbackY;
}

// ===========================================================
// 更新
// ===========================================================
void ChickManager::Update()
{
	//------------------------------------------
	// プレイヤー位置を全員へ配る
	//------------------------------------------
	Math::Vector3 targetPos = Math::Vector3::Zero;
	bool          hasTarget = false;

	// SetTarget 前は空のエンティティなので読まない。ひよこには「相手なし」を配る
	if (m_cm.has<TransformComponent>(m_targetEntity))
	{
		targetPos = m_cm.get<TransformComponent>(m_targetEntity).GetPosition();
		hasTarget = true;
	}

	m_lastTargetPos = targetPos;
	m_lastHasTarget = hasTarget;

	if (!m_hasNest && hasTarget)
	{
		m_nestPos = targetPos;
		m_hasNest = true;
	}

	// 持ち場は各ひよこを更新する前に決める
	UpdateNestSlots();

	UpdateCarry();

	for (auto& chick : m_spChicks)
	{
		chick->SetTarget(targetPos, hasTarget);
		chick->Update();
	}
}

void ChickManager::PostUpdate()
{
	for (auto& chick : m_spChicks) chick->PostUpdate();
}

// ===========================================================
// 巣の持ち場
// ===========================================================
Math::Vector3 ChickManager::MakeSlotPosition(int _index) const
{
	// 持ち場の数 = 出した数。0 割りにならないよう最低1
	const int count = std::max(static_cast<int>(m_slotUsed.size()), 1);

	const float a = DirectX::XM_2PI
		* static_cast<float>(_index) / static_cast<float>(count);

	Math::Vector3 point = m_nestPos
		+ Math::Vector3(std::cos(a), 0.0f, std::sin(a)) * m_tuning.slotRadius;

	point.y = SampleGroundY(point, m_nestPos.y);
	return point;
}

void ChickManager::UpdateNestSlots()
{
	if (!m_hasNest) return;

	const float r2 = m_tuning.nestRadius * m_tuning.nestRadius;

	for (const auto& chick : m_spChicks)
	{
		if (!chick->IsActing()) continue;
		// 既に枠を持っている/収まった個体は触らない
		if (chick->HasSlot() || chick->IsRescued()) continue;

		// 連れ出されている個体だけが対象
		if (!chick->IsFollowing()) continue;

		Math::Vector3 d = chick->GetPosition() - m_nestPos;
		d.y = 0.0f;
		if (d.LengthSquared() > r2) continue;

		// 一番近い空きスロットを取らせる
		int   bestIndex = -1;
		float bestDist = FLT_MAX;

		for (int i = 0; i < static_cast<int>(m_slotUsed.size()); ++i)
		{
			if (m_slotUsed[i]) continue;

			Math::Vector3 diff = MakeSlotPosition(i) - chick->GetPosition();
			diff.y = 0.0f;

			const float sq = diff.LengthSquared();
			if (sq >= bestDist) continue;

			bestIndex = i;
			bestDist = sq;
		}

		if (bestIndex < 0) continue;

		m_slotUsed[bestIndex] = true;
		chick->SetSlot(MakeSlotPosition(bestIndex));
	}
}

// ===========================================================
// 連れ去り
// ===========================================================
void ChickManager::UpdateCarry()
{
	for (const auto& chick : m_spChicks)
	{
		const int chickId = static_cast<int>(chick->GetEntity().index);

		// 運ばれている最中
		if (chick->IsCarried())
		{
			CarrierPose pose;
			pose.foot = chick->GetPosition();	// 運び手が消えていた時の保険

			const bool held = m_carrierPose
				&& m_carrierPose(chick->GetCarrierId(), chickId, pose);

			// 咥えられたまま走り去られた。もう戻らない。
			if (pose.gone)
			{
				chick->Vanish();
				++m_lostCount;
				continue;
			}

			// 運び手が倒された/固まった
			if (!held)
			{
				// 離すのは口元ではなく運び手の足元
				pose.foot.y = SampleGroundY(pose.foot, pose.foot.y);
				chick->EndCarried(pose.foot);
				continue;
			}

			// 時間では解放しない
			chick->SetCarriedPosition(pose.mouth);
			chick->SetCarriedScale(pose.scale, pose.leaving);
			continue;
		}

		// 倒れている
		if (!chick->IsDowned()) continue;
		if (!m_requestCarrier) continue;

		// 手が空いた敵がいなければ何も起きない
		const int carrierId = m_requestCarrier(chick->GetPosition(), chickId);
		if (carrierId < 0) continue;

		chick->BeginCarried(carrierId);
	}

	// 消えた子を片付ける。以後は数にも標的にも入らない。
	m_spChicks.erase(
		std::remove_if(m_spChicks.begin(), m_spChicks.end(),
			[](const auto& chick) { return chick->IsVanished(); }),
		m_spChicks.end());
}

int ChickManager::GetDownedCount() const
{
	int count = 0;
	for (const auto& chick : m_spChicks)
	{
		if (chick->IsDowned() && !chick->IsCarried()) ++count;
	}
	return count;
}

int ChickManager::GetCarriedCount() const
{
	int count = 0;
	for (const auto& chick : m_spChicks)
	{
		if (chick->IsCarried()) ++count;
	}
	return count;
}

int ChickManager::GetAliveCount() const
{
	// 倒れていても数には入る。運び去られて消えた子だけが抜ける。
	int count = 0;
	for (const auto& chick : m_spChicks)
	{
		if (chick->IsAlive()) ++count;
	}
	return count;
}

int ChickManager::GetRescuedCount() const
{
	int count = 0;
	for (const auto& chick : m_spChicks)
	{
		// 巣から攫われている最中の子は数えない。運ばれている間にクリアにならないように。
		if (chick->IsRescued() && !chick->IsCarried()) ++count;
	}
	return count;
}

// ===========================================================
// デバッグビューア
// ===========================================================
void ChickManager::DrawDebugImGui()
{
	if (!ImGui::Begin("Chick"))
	{
		ImGui::End();
		return;
	}

	ImGui::Text("Rescued %d / %d", GetRescuedCount(), GetTotalCount());

	const float ratio = (GetTotalCount() > 0)
		? static_cast<float>(GetRescuedCount()) / static_cast<float>(GetTotalCount())
		: 0.0f;
	constexpr float kFillWidth = -1.0f;	// ImGui の決まりで「残り幅いっぱい」
	ImGui::ProgressBar(ratio, ImVec2(kFillWidth, 0.0f));

	ImGui::Text("Nest   %.1f, %.1f, %.1f  (r=%.1f)",
		m_nestPos.x, m_nestPos.y, m_nestPos.z, m_tuning.nestRadius);

	ImGui::Text("Down %d   Carried %d   Lost %d / %d",
		GetDownedCount(), GetCarriedCount(), m_lostCount, m_spawnedCount);

	ImGui::Separator();

	int index = 0;
	for (const auto& chick : m_spChicks)
	{
		ImGui::PushID(index);

		const Math::Vector3 pos = chick->GetPosition();

		ImGui::Text("[%02d] %-7s %s  player %.1f m",
			index,
			chick->GetStateLabel(),
			chick->IsRescued() ? "RESCUED" : (chick->HasSlot() ? "settle " : "-      "),
			chick->GetTargetDistance());

		if (ImGui::TreeNode("detail"))
		{
			ImGui::Text("pos  %.1f, %.1f, %.1f", pos.x, pos.y, pos.z);

			const Math::Vector3 home = chick->GetHomePosition();
			ImGui::Text("home %.1f, %.1f, %.1f  (%.1f m)",
				home.x, home.y, home.z, chick->GetHomeDistance());

			chick->DrawDebugImGui();
			ImGui::TreePop();
		}

		ImGui::PopID();
		++index;
	}

	ImGui::End();
}