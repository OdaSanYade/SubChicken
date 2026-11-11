#pragma once
#include "CombatFx.h"
#include "../../Inc/Math/Damp.h"

class ComboTracker
{
public:

	void Update()
	{
		const float dt = CombatFx::Instance().RealDelta();

		//------------------------------------------
		// 命中を数える
		//------------------------------------------
		for (const auto& ev : CombatFx::Instance().Events())
		{
			if (ev.attackerType != Collision::ObjectType::PlayerAttack) continue;
			Add(ev.damage);
		}

		//------------------------------------------
		// 途切れ判定
		//------------------------------------------
		if (m_count > 0)
		{
			m_timer -= dt;
			if (m_timer <= 0.0f) Finish();
		}

		// 表示の伸縮を戻す
		m_punch = Damp::Value(m_punch, 1.0f, kPunchLambda, dt);
		// 途切れた後のフェードアウト
		if (m_count == 0 && m_alpha > 0.0f)
			m_alpha = std::max(0.0f, m_alpha - dt / kFadeOutSec);

		m_pop = std::max(0.0f, m_pop - dt / kPopSec);
	}

	// プレイヤーが被弾したら途切れさせる
	void Break()
	{
		if (m_count <= 0) return;
		Finish();
		m_alpha = 0.0f;	// 途切れたときはフェードせず即座に消す
	}

	void Reset()
	{
		m_count = 0;
		m_bestCount = 0;
		m_totalDamage = 0.0f;
		m_timer = 0.0f;
		m_punch = 1.0f;
		m_alpha = 0.0f;
	}

	int   GetCount()       const { return m_count; }
	int   GetBestCount()   const { return m_bestCount; }
	float GetTotalDamage() const { return m_totalDamage; }
	// 残り時間の割合
	float GetRemainRatio() const { return (m_count > 0) ? std::clamp(m_timer / kResetSec, 0.0f, 1.0f) : 0.0f; }

	float GetPopRatio() const { return std::clamp(m_pop, 0.0f, 1.0f); }

	void DrawImGui();

private:

	void Add(float _damage)
	{
		++m_count;
		m_totalDamage += _damage;
		m_timer = kResetSec;
		m_alpha = 1.0f;
		m_pop = 1.0f;

		// 桁が増えるほど大きく跳ねさせる。伸びている実感を出す。
		m_punch = std::min(kPunchBase + static_cast<float>(m_count) * 0.01f, kPunchMax);

		if (m_count > m_bestCount) m_bestCount = m_count;
	}

	void Finish()
	{
		m_count = 0;
		m_timer = 0.0f;
		m_totalDamage = 0.0f;
		m_punch = 1.0f;
	}

	static constexpr float kResetSec = 2.5f;	// 最後の命中からこの秒数で途切れる
	static constexpr float kFadeOutSec = 0.6f;
	static constexpr float kPunchLambda = 11.0f;	// 伸縮が戻る速さ
	static constexpr float kPunchBase = 1.35f;
	static constexpr float kPunchMax = 1.7f;
	static constexpr float kPopSec = 0.3f;

	float m_pop = 0.0f;
	int   m_count = 0;
	int   m_bestCount = 0;
	float m_totalDamage = 0.0f;
	float m_timer = 0.0f;
	float m_punch = 1.0f;
	float m_alpha = 0.0f;

private:
	ComboTracker() = default;
	~ComboTracker() = default;
public:
	static ComboTracker& Instance()
	{
		static ComboTracker instance;
		return instance;
	}
};

namespace combo_detail {

	// ヒット数 → 評価。分岐ではなくテーブル引き。上から順に最初に満たしたものを採用。
	// ★ 段位を増やすときはここへ足すだけでよい。
	struct RankEntry { int threshold; const char* label; ImU32 color; };

	inline const RankEntry kRankTable[] = {
		{ 30, "S", IM_COL32(255, 210,  90, 255) },
		{ 20, "A", IM_COL32(255, 130,  90, 255) },
		{ 12, "B", IM_COL32(150, 200, 255, 255) },
		{  6, "C", IM_COL32(200, 220, 220, 255) },
		{  0, "D", IM_COL32(180, 180, 180, 255) },
	};

	inline const RankEntry& FindRank(int _count)
	{
		for (const auto& e : kRankTable)
		{
			if (_count >= e.threshold) return e;
		}
		return kRankTable[std::size(kRankTable) - 1];
	}

	inline ImU32 WithAlpha(ImU32 _col, float _a)
	{
		const ImU32 a = static_cast<ImU32>(((_col >> IM_COL32_A_SHIFT) & 0xFF) * std::clamp(_a, 0.0f, 1.0f));
		return (_col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
	}
}

inline void ComboTracker::DrawImGui()
{
	// 1ヒットでは出さない。2以上で「繋がっている」と見せる。
	if (m_count < 2 && m_alpha <= 0.0f) return;
	if (m_alpha <= 0.0f) return;

	auto* dl = ImGui::GetForegroundDrawList();
	ImFont* font = ImGui::GetFont();
	const ImVec2 screen = ImGui::GetIO().DisplaySize;

	const auto& rank = combo_detail::FindRank(m_count);

	// 画面右上に寄せる。中央だと戦闘が見えなくなる。
	const float baseX = screen.x * 0.82f;
	const float baseY = screen.y * 0.22f;

	//------------------------------------------
	// ヒット数
	//------------------------------------------
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", m_count);

	const float numSize = ImGui::GetFontSize() * 3.4f * m_punch;
	const ImVec2 numDim = font->CalcTextSizeA(numSize, FLT_MAX, 0.0f, buf);
	const ImVec2 numPos(baseX - numDim.x, baseY - numDim.y * 0.5f);

	// 影を先に置いて視認性を確保する(背景が明るいと数字が沈む)
	dl->AddText(font, numSize, ImVec2(numPos.x + 3.0f, numPos.y + 3.0f),
		combo_detail::WithAlpha(IM_COL32(0, 0, 0, 200), m_alpha), buf);
	dl->AddText(font, numSize, numPos,
		combo_detail::WithAlpha(rank.color, m_alpha), buf);

	//------------------------------------------
	// "HIT" と評価
	//------------------------------------------
	const float labelSize = ImGui::GetFontSize() * 1.1f;
	const ImVec2 labelPos(baseX + 6.0f, baseY - labelSize * 0.5f);
	dl->AddText(font, labelSize, labelPos,
		combo_detail::WithAlpha(IM_COL32(240, 240, 240, 255), m_alpha), "HIT");

	const float rankSize = ImGui::GetFontSize() * 2.0f;
	const ImVec2 rankPos(baseX + 6.0f, baseY - rankSize * 0.5f + labelSize + 4.0f);
	dl->AddText(font, rankSize, rankPos,
		combo_detail::WithAlpha(rank.color, m_alpha), rank.label);

	//------------------------------------------
	// 残り時間ゲージ
	//------------------------------------------
	// 途切れる寸前が目で見えると、繋ぎに行く動機になる。
	const float gaugeW = 150.0f;
	const float gaugeH = 5.0f;
	const ImVec2 g0(baseX - gaugeW, baseY + numDim.y * 0.5f + 8.0f);
	const ImVec2 g1(g0.x + gaugeW, g0.y + gaugeH);

	dl->AddRectFilled(g0, g1, combo_detail::WithAlpha(IM_COL32(0, 0, 0, 160), m_alpha));
	dl->AddRectFilled(g0,
		ImVec2(g0.x + gaugeW * GetRemainRatio(), g1.y),
		combo_detail::WithAlpha(rank.color, m_alpha));

	//------------------------------------------
	// 累計ダメージ
	//------------------------------------------
	snprintf(buf, sizeof(buf), "%.0f dmg", m_totalDamage);
	const float dmgSize = ImGui::GetFontSize();
	const ImVec2 dmgDim = font->CalcTextSizeA(dmgSize, FLT_MAX, 0.0f, buf);
	dl->AddText(font, dmgSize, ImVec2(baseX - dmgDim.x, g1.y + 4.0f),
		combo_detail::WithAlpha(IM_COL32(220, 220, 220, 255), m_alpha), buf);
}