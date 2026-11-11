#pragma once
#include "BattleResult.h"
#include "../../Inc/Math/Damp.h"
#include "../../../Framework/Shader/UiShader/KdUiInputHint.h"

class ResultHud
{
public:

	enum Choice : int
	{
		ChoiceRetry = 0,
		ChoiceTitle = 1,
		ChoiceCount = 2,
	};

	// 決定されたときに呼ばれる。シーン側で遷移する。
	using DecideFn = std::function<void(Choice)>;

	bool Init()
	{
		if (!m_shader.Init()) return false;
		if (!m_ui.Init(16)) return false;
		if (!m_plate.Init()) return false;
		if (!m_rank.Init()) return false;

		m_time = 0.0f;

		LoadTextures();
		return true;
	}

	void Release()
	{
		m_shader.Release();
		m_ui.Release();
		m_plate.Release();
		m_rank.Release();
	}

	void LoadTextures()
	{
		auto& tex = RES_MGR.GetTexList();
		if (!tex) return;

		m_bg = nullptr;
		m_ornament = nullptr;

		// 0=D 1=C 2=B 3=A 4=S
		static const char* kRankName[5] = {
			"RankD", "RankC", "RankB", "RankA", "RankS"
		};

		for (int i = 0; i < 5; ++i) m_rankTex[i] = tex->GetTex(kRankName[i]);
	}

	void SetDiedTex(bool _isChickDied)
	{
		if (_isChickDied) 
		{
			m_headTex[1] = RES_MGR.GetTexList()->GetTex("ChickDied");
		}
		else 
		{
			m_headTex[1] = RES_MGR.GetTexList()->GetTex("ChickenDied");
		}
	}

	void SetDecideCallback(DecideFn _fn) { m_decide = std::move(_fn); }
	void SetStandard(const BattleResult::Standard& _s) { m_standard = _s; }

	//------------------------------------------
	// 開始
	//------------------------------------------
	// 戦闘が終わったら1回だけ呼ぶ。
	void Begin(const BattleResult& _r)
	{
		m_result = _r;
		m_active = true;
		m_time = 0.0f;
		m_choice = ChoiceRetry;
		m_prevChoice = ChoiceRetry;
		m_ghost = 0.0f;
		m_press = 0.0f;
		m_decided = false;
		m_skipped = false;
	}

	void Close()
	{
		m_active = false;
		m_decided = false;
	}

	bool IsActive() const { return m_active; }

	void Update();
	void Draw();

private:

	//------------------------------------------
	// 段取り
	//------------------------------------------
	static constexpr float kAtHeadline = 0.35f;
	static constexpr float kAtCombo = 0.80f;
	static constexpr float kAtTime = 1.15f;
	static constexpr float kAtLife = 1.50f;
	static constexpr float kAtRank = 2.00f;
	static constexpr float kAtButton = 3.40f;	// 孵化を見てから

	static constexpr float kAppearSec = 0.40f;	// 1要素が出切るまで
	static constexpr float kHatchSec = 1.80f;	// 卵が孵り切るまで
	static constexpr float kVeilSec = 0.60f;
	static constexpr float kPressSec = 0.30f;
	static constexpr float kGhostSec = 0.30f;	// 移動の尾が消えるまで

	static constexpr float kDeadSpace = 0.60f;		// 字間(字の半幅に対する比)
	static constexpr float kDeadWordGap = 2.20f;	// 語間(同上)

	// 見出しをテクスチャで出すときの大きさ
	static constexpr float kHeadTexHalfW = 520.0f;
	static constexpr float kHeadTexAspect = 0.44f;	// 高さ / 幅

	float Appear(float _at) const
	{
		if (m_skipped) return 1.0f;

		const float span = (_at == kAtRank) ? kHatchSec : kAppearSec;

		// 負けたときは成績を出さないので、ボタンまでの待ちを詰める。
		// 見るものが無い画面で待たされるのは、ただの足止めになる。
		const float at = m_result.cleared ? _at : std::min(_at, kAtHeadline + 0.5f);

		return std::clamp((m_time - at) / span, 0.0f, 1.0f);
	}

	void PushVeil(const Math::Vector2& _screen);
	void PushHeadline(const Math::Vector2& _screen);
	void PushStat(const Math::Vector2& _screen, int _kind, float _y,
		int _a, int _b, bool _perfect, float _appear);
	void PushRank(const Math::Vector2& _screen);
	void DrawRankPlate(const Math::Vector2& _screen);
	void PushButton(const Math::Vector2& _screen, int _kind, float _x);
	void PushHint(const Math::Vector2& _screen);

	float Px(float _v) const { return _v * m_uiScale; }

	//------------------------------------------
	// データ
	//------------------------------------------
	KdResultShader m_shader;
	std::vector<KdResultShader::Instance> m_instances;

	KdUiShader m_ui;
	std::vector<KdUiShader::Instance> m_uiInstances;

	//------------------------------------------
	// テクスチャ
	//------------------------------------------
	std::shared_ptr<KdTexture> m_headTex[2];
	bool m_useHeadTex = true;

	KdTitleShader m_plate;
	KdRankShader  m_rank;

	std::shared_ptr<KdTexture> m_bg;			// 背景の質感
	std::shared_ptr<KdTexture> m_rankTex[5];	// D C B A S
	std::shared_ptr<KdTexture> m_ornament;		// 隅の装飾(羽根など)

	BattleResult m_result;
	BattleResult::Standard m_standard;

	DecideFn m_decide;

	bool  m_active = false;
	bool  m_decided = false;
	bool  m_skipped = false;

	float m_time = 0.0f;
	float m_uiScale = 1.0f;

	int   m_choice = ChoiceRetry;

	// 直前まで選択されていた項目。移動の尾を引かせる。
	int   m_prevChoice = ChoiceRetry;
	float m_ghost = 0.0f;
	float m_press = 0.0f;

	// 選択の移動。入力の連続を弾く。
	float m_repeatCool = 0.0f;
	static constexpr float kRepeatSec = 0.22f;

	static constexpr float kHeadlineY = 0.20f;	// 画面高に対する比

	// ランク。左に大きく置く。
	static constexpr float kRankX = 0.30f;
	static constexpr float kRankY = 0.52f;
	static constexpr float kRankHalf = 150.0f;	// 卵(SDF)の大きさ

	// 成績。右に3行。
	static constexpr float kStatX = 0.66f;
	static constexpr float kStatTopY = 0.42f;
	// 帯を敷くので、行の高さは帯の高さ。
	// 間隔は帯どうしが触れない程度に空ける。
	static constexpr float kStatStepY = 78.0f;
	static constexpr float kStatHalfW = 300.0f;
	static constexpr float kStatHalfH = 30.0f;

	// ボタン。画面下部を埋める。
	static constexpr float kButtonY = 0.84f;
	static constexpr float kButtonHalfW = 190.0f;
	static constexpr float kButtonHalfH = 44.0f;
	static constexpr float kButtonGap = 215.0f;

	// 負けたときは1行だけ。画面中央に置く。
	static constexpr float kDeadY = 0.44f;

	static constexpr float kBgAlpha = 0.55f;	// 背景の主張。強いと文字が沈む。
	// ランクの絵。動きが外へ出るので、板を大きく取る。
	// 足りないとテクスチャの縁で切れる。
	static constexpr float kRankTexScale = 2.20f;

	//------------------------------------------
	// 配色
	//------------------------------------------
	static inline const Math::Vector4 kVeilCol = { 0.052f, 0.05f, 0.055f, 0.985f };
	static inline const Math::Vector4 kClearCol = { 1.00f, 0.88f, 0.46f, 1.00f };
	static inline const Math::Vector4 kDeadCol = { 0.86f, 0.24f, 0.22f, 1.00f };
	static inline const Math::Vector4 kStatCol = { 0.96f, 0.94f, 0.88f, 1.00f };
	static inline const Math::Vector4 kStatLabel = { 0.96f, 0.88f, 0.92f, 1.00f };
	static inline const Math::Vector4 kBtnOn = { 1.00f, 0.86f, 0.42f, 1.00f };
	static inline const Math::Vector4 kBtnOff = { 0.72f, 0.74f, 0.78f, 1.00f };

	// ランクごとの色。上ほど熱く。
	static Math::Vector4 RankColor(int _rank)
	{
		// 分岐ではなくテーブル引き。
		static const Math::Vector4 kTable[5] = {
			{ 0.55f, 0.57f, 0.60f, 1.00f },	// D
			{ 0.70f, 0.74f, 0.78f, 1.00f },	// C
			{ 0.52f, 0.80f, 0.92f, 1.00f },	// B
			{ 1.00f, 0.62f, 0.24f, 1.00f },	// A
			{ 1.00f, 0.88f, 0.36f, 1.00f },	// S
		};
		return kTable[std::clamp(_rank, 0, 4)];
	}

private:
	ResultHud() = default;
	~ResultHud() = default;
public:
	static ResultHud& Instance()
	{
		static ResultHud instance;
		return instance;
	}
};

#define RESULT ResultHud::Instance()

//===================================================
// 更新
//===================================================
inline void ResultHud::Update()
{
	if (!m_active) return;

	const float dt = Time::Instance().GetDeltaTime();

	KdUiInputHint::Update();

	m_time += dt;
	m_press = std::max(0.0f, m_press - dt / kPressSec);
	m_ghost = std::max(0.0f, m_ghost - dt / kGhostSec);
	m_repeatCool = std::max(0.0f, m_repeatCool - dt);

	if (m_decided) return;

	auto& input = KdInputActionSystem::Instance();

	//------------------------------------------
	// 選択
	//------------------------------------------
	const Math::Vector2 axis = input.GetAxis("Move");

	if (m_repeatCool <= 0.0f)
	{
		// 横で移動。縦は無視する。ボタンが横並びなので、
		// 縦でも動かせると入力と結果が噛み合わない。
		int next = m_choice;

		if (axis.x > 0.5f)       next = std::min(m_choice + 1, ChoiceCount - 1);
		else if (axis.x < -0.5f) next = std::max(m_choice - 1, 0);

		if (next != m_choice)
		{
			m_prevChoice = m_choice;
			m_ghost = 1.0f;

			m_choice = next;
			m_repeatCool = kRepeatSec;
		}
	}

	//------------------------------------------
	// 決定 / スキップ
	//------------------------------------------
	const bool submit = input.IsPress("Dicede");

	if (submit)
	{
		if (!m_skipped && m_time < kAtButton + kAppearSec)
		{
			m_skipped = true;
			return;
		}

		if (!m_decided)AudioManager::Instance().Play("button");

		m_press = 1.0f;
		m_decided = true;

		if (m_decide) m_decide(static_cast<Choice>(m_choice));
	}
}

//===================================================
// 描画
//===================================================
inline void ResultHud::PushVeil(const Math::Vector2& _screen)
{
	KdUiShader::Instance inst;
	inst.kind = KdUiShader::Veil;
	inst.center = { _screen.x * 0.5f, _screen.y * 0.5f };
	inst.halfSize = { _screen.x * 0.5f, _screen.y * 0.5f };
	inst.color = kVeilCol;

	const float t = m_skipped ? 1.0f : std::clamp(m_time / kVeilSec, 0.0f, 1.0f);
	inst.params = { t, 0.0f, 0.0f, 0.0f };

	m_uiInstances.push_back(inst);
}

inline void ResultHud::PushHeadline(const Math::Vector2& _screen)
{
	const float appear = Appear(kAtHeadline);
	if (appear <= 0.0f) return;

	const int kind = m_result.cleared ? 0 : 1;

	// 勝ちは上に置いて下に成績を並べる。
	// 負けは見せるものが無いので、中央へ置いて画面の主役にする。
	const float y = m_result.cleared ? kHeadlineY : kDeadY;

	//------------------------------------------
	// テクスチャ版
	//------------------------------------------
	if (m_useHeadTex && m_headTex[kind])
	{
		const float halfW = Px(kHeadTexHalfW);

		KdTitleShader::Params p;
		p.center = { _screen.x * 0.5f, _screen.y * y };
		p.halfSize = { halfW, halfW * kHeadTexAspect };
		p.appear = appear;
		p.tint = m_result.cleared ? kClearCol : kDeadCol;

		// 勝ちだけ光らせる。負けた画面で光らせても嘘になる。
		p.sheen = m_result.cleared ? 1.0f : 0.0f;
		p.heat = m_result.cleared ? 0.6f : 0.0f;
		p.edgeWave = 0.4f;
		p.dust = 0.0f;
		p.breath = 0.0f;
		p.shake = 0.0f;
		p.dissolve = 0.0f;

		m_plate.Draw(*m_headTex[kind], p);
		return;
	}

	//------------------------------------------
	// SDF版
	//------------------------------------------
	KdResultShader::Instance inst;
	inst.kind = KdResultShader::Headline;

	inst.center = { _screen.x * 0.5f, _screen.y * y };
	inst.halfSize = { _screen.x * 0.44f, Px(80.0f) };
	inst.color = m_result.cleared ? kClearCol : kDeadCol;
	inst.subColor = kStatLabel;

	// z=語間 w=字間。見出しでは空いている枠を使う。
	inst.params = { static_cast<float>(kind), appear, kDeadWordGap, kDeadSpace };

	m_instances.push_back(inst);
}

inline void ResultHud::PushStat(const Math::Vector2& _screen, int _kind, float _y,
	int _a, int _b, bool _perfect, float _appear)
{
	if (_appear <= 0.0f) return;

	KdResultShader::Instance inst;
	inst.kind = KdResultShader::Stat;

	inst.center = { _screen.x * kStatX, _y };
	inst.halfSize = { Px(kStatHalfW), Px(kStatHalfH) };
	inst.color = kStatCol;
	inst.subColor = kStatLabel;
	inst.params = { static_cast<float>(_kind), _appear,
					static_cast<float>(_a), static_cast<float>(_b) };
	inst.extra = { _perfect ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };

	m_instances.push_back(inst);
}

inline void ResultHud::PushRank(const Math::Vector2& _screen)
{
	const float appear = Appear(kAtRank);
	if (appear <= 0.0f) return;

	const int rank = static_cast<int>(m_result.GetRank(m_standard));

	KdResultShader::Instance inst;
	inst.kind = KdResultShader::Rank;
	inst.center = { _screen.x * kRankX, _screen.y * kRankY };
	inst.halfSize = { Px(kRankHalf), Px(kRankHalf) };
	inst.color = RankColor(rank);
	inst.subColor = kStatLabel;
	inst.params = { static_cast<float>(rank), appear, 0.0f, 0.0f };

	m_instances.push_back(inst);
}

inline void ResultHud::PushButton(const Math::Vector2& _screen, int _kind, float _x)
{
	const float appear = Appear(kAtButton);
	if (appear <= 0.0f) return;

	const bool on = (m_choice == _kind);

	KdUiShader::Instance inst;

	// 帳票の欄。タイトルの筆致とは意図して別の見た目にする。
	// 同じ形だと、勝敗の記録を見る画面という手触りが出ない。
	inst.kind = KdUiShader::ButtonPlate;

	inst.center = { _x, _screen.y * kButtonY };
	inst.halfSize = { Px(kButtonHalfW), Px(kButtonHalfH) };
	inst.color = kBtnOn;
	inst.subColor = kBtnOff;

	// 語は文字コードで渡す。enum で持つと語を足すたびにシェーダを触ることになる。
	KdUiShader::SetWord(inst, (_kind == ChoiceRetry) ? "RETRY" : "TITLE");

	// 直前まで選択されていた項目だけ、尾を引かせる。
	const float ghost = (!on && _kind == m_prevChoice) ? m_ghost : 0.0f;

	KdUiShader::SetButtonState(inst, on, on ? m_press : 0.0f, appear, ghost);
	KdUiInputHint::SetButtonIcon(inst, "Dicede");

	m_uiInstances.push_back(inst);
}

//===================================================
// 操作ヒント
//===================================================
inline void ResultHud::PushHint(const Math::Vector2& _screen)
{
	const float appear = Appear(kAtButton);
	if (appear <= 0.0f) return;

	KdUiShader::Instance inst;
	inst.kind = KdUiShader::Hint;
	inst.halfSize = { Px(90.0f), Px(18.0f) };
	inst.center = { _screen.x - _screen.x * 0.035f - inst.halfSize.x,
					_screen.y - _screen.y * 0.035f - inst.halfSize.y };

	inst.color = { 0.82f, 0.80f, 0.76f, 0.72f };
	inst.subColor = inst.color;

	KdUiInputHint::SetHint(inst, "Dicede", "DECIDE", appear);

	m_uiInstances.push_back(inst);
}

//===================================================
// ランクの絵
//===================================================
inline void ResultHud::DrawRankPlate(const Math::Vector2& _screen)
{
	const int rank = std::clamp(static_cast<int>(m_result.GetRank(m_standard)), 0, 4);
	if (!m_rankTex[rank]) return;

	const float hatch = Appear(kAtRank);
	if (hatch <= 0.0f) return;

	// 殻と同じ大きさに合わせる。
	// 別々に決めると、卵から出てきた大きさが合わない。
	const float half = Px(kRankHalf) * kRankTexScale;

	KdRankShader::Params p;
	p.center = { _screen.x * kRankX, _screen.y * kRankY };
	p.halfSize = { half, half };
	p.tint = { 1.0f, 1.0f, 1.0f, 1.0f };

	// ★殻(PushRank)と同じ値を渡すこと。
	//   ずれると、割れる前に中身が透けたり、間が空いたりする。
	p.hatch = hatch;
	p.rank = rank;

	m_rank.Draw(*m_rankTex[rank], p);
}

inline void ResultHud::Draw()
{
	if (!m_active) return;

	m_instances.clear();

	const Math::Vector2 screen = KdHudShader::GetViewportSize();
	if (screen.x <= 0.0f || screen.y <= 0.0f) return;

	// 基準解像度からの倍率。CombatHud と同じ考え方。
	m_uiScale = std::min(screen.x / 1920.0f, screen.y / 1080.0f);

	//------------------------------------------
	// 背景
	//------------------------------------------
	PushVeil(screen);
	m_ui.Draw(m_uiInstances);
	m_uiInstances.clear();

	if (m_bg)
	{
		KdTitleShader::Params p;
		p.center = { screen.x * 0.5f, screen.y * 0.5f };
		p.halfSize = { screen.x * 0.5f, screen.y * 0.5f };

		// 効果は全部切る。ただのテクスチャ付きクアッドとして使う。
		p.heat = 0.0f;
		p.sheen = 0.0f;
		p.edgeWave = 0.0f;
		p.dust = 0.0f;
		p.breath = 0.0f;

		p.appear = std::min(m_time / kVeilSec, 1.0f);
		p.tint = { 1.0f, 1.0f, 1.0f, kBgAlpha };

		m_plate.Draw(*m_bg, p);
	}

	PushHeadline(screen);

	//------------------------------------------
	// 成績(クリア時のみ)
	//------------------------------------------
	if (m_result.cleared)
	{
		const float top = screen.y * kStatTopY;
		const float step = Px(kStatStepY);

		const int mm = std::min(static_cast<int>(m_result.elapsedSec) / 60, 99);
		const int ss = static_cast<int>(m_result.elapsedSec) % 60;

		PushStat(screen, KdResultShader::StatCombo, top, m_result.maxCombo, 0,
			m_result.IsPerfectCombo(m_standard), Appear(kAtCombo));

		PushStat(screen, KdResultShader::StatTime, top + step, mm, ss,
			m_result.IsPerfectTime(m_standard), Appear(kAtTime));

		const int life = static_cast<int>(std::floor(m_result.lifeRatio * 100.0f + 0.5f));
		PushStat(screen, KdResultShader::StatLife, top + step * 2.0f, life, 0,
			m_result.IsPerfectLife(), Appear(kAtLife));

		PushRank(screen);
		DrawRankPlate(screen);
	}

	//------------------------------------------
	// ボタン
	//------------------------------------------
	const float gap = Px(kButtonGap);
	PushButton(screen, ChoiceRetry, screen.x * 0.5f - gap);
	PushButton(screen, ChoiceTitle, screen.x * 0.5f + gap);

	PushHint(screen);


	m_shader.Draw(m_instances);
	m_ui.Draw(m_uiInstances);
}