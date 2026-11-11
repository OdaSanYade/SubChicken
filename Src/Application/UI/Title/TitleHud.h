#pragma once
#include "../../Inc/Math/Damp.h"
#include "../../../Framework/Shader/UiShader/KdUiInputHint.h"

class TitleHud
{
public:

	enum Choice : int
	{
		ChoiceStart = 0,
		ChoiceCredit = 1,
		ChoiceExit = 2,
		ChoiceCount = 3,
	};

	using DecideFn = std::function<void(Choice)>;

	bool Init()
	{
		m_logo = RES_MGR.GetTexList()->GetTex("TitleLogo");

		if (!m_titleShader.Init()) return false;
		if (!m_btnShader.Init(8)) return false;

		Reset();
		return true;
	}

	void Release()
	{
		m_titleShader.Release();
		m_btnShader.Release();
	}

	void Reset()
	{
		m_time = 0.0f;
		m_choice = ChoiceStart;
		m_press = 0.0f;
		m_prevChoice = ChoiceStart;
		m_ghost = 0.0f;
		m_dissolve = 0.0f;
		m_decided = false;
		m_repeatCool = 0.0f;

		// ★前の画面で押した決定キーを持ち込まない。
		//   一度離されるまで決定を受けない。押していなければ次の更新ですぐ解ける。
		m_needRelease = true;
	}

	// クレジットから戻った時に呼ぶ。
	// ロゴの登場からやり直し、カーソルは指定した項目に置く。
	void Resume(Choice _keep)
	{
		Reset();
		m_choice = _keep;
		m_prevChoice = _keep;
	}

	void SetDecideCallback(DecideFn _fn) { m_decide = std::move(_fn); }

	void Update();
	void Draw();

private:

	void PushButton(const Math::Vector2& _screen, int _index, const char* _word);
	void PushHint(const Math::Vector2& _screen, int _slot, const char* _word);

	float Px(float _v) const { return _v * m_uiScale; }

	// 登場
	float LogoAppear() const { return std::clamp(m_time / kLogoAppearSec, 0.0f, 1.0f); }
	float ButtonAppear() const
	{
		return std::clamp((m_time - kBtnDelay) / kBtnAppearSec, 0.0f, 1.0f);
	}

	//------------------------------------------
	// データ
	//------------------------------------------
	KdTitleShader  m_titleShader;
	KdUiShader m_btnShader;
	std::vector<KdUiShader::Instance> m_buttons;

	std::shared_ptr<KdTexture> m_logo;

	DecideFn m_decide;

	float m_time = 0.0f;
	float m_uiScale = 1.0f;

	int   m_choice = ChoiceStart;

	// 直前まで選択されていた項目
	int   m_prevChoice = ChoiceStart;
	float m_ghost = 0.0f;
	float m_press = 0.0f;
	float m_dissolve = 0.0f;	// 決定で剥がれる
	bool  m_decided = false;

	float m_repeatCool = 0.0f;

	// 決定キーが一度離されるまで決定を受けない
	bool  m_needRelease = false;

	//------------------------------------------
	// 段取り
	//------------------------------------------
	static constexpr float kLogoAppearSec = 0.75f;
	static constexpr float kBtnDelay = 0.55f;
	static constexpr float kBtnAppearSec = 0.40f;

	static constexpr float kPressSec = 0.30f;
	static constexpr float kGhostSec = 0.30f;	// 移動の尾が消えるまで
	static constexpr float kDissolveSec = 0.60f;
	static constexpr float kRepeatSec = 0.20f;

	//------------------------------------------
	// 配置
	//------------------------------------------
	// ロゴは右上。3D空間の左下にニワトリを置く前提。
	// タイトルは画面の主役なので、幅の6割ほどを使って大きく出す。
	static constexpr float kLogoCx = 0.72f;		// 画面幅に対する比
	static constexpr float kLogoCy = 0.50f;		// 画面高に対する比
	static constexpr float kLogoHalfW = 450.0f;

	// ロゴの縦横比
	static constexpr float kLogoAspect = 0.50f;	// 高さ / 幅

	// ロゴの不透明度
	static constexpr float kLogoAlpha = 0.95f;

	// ボタンはロゴの直下。
	// アイコンと語が横に並ぶので、横長にして余裕を持たせる。
	static constexpr float kBtnWidthRatio = 0.62f;	// ロゴ幅に対する比
	static constexpr float kBtnHalfH = 40.0f;
	// 3項目に増えたので詰める。104 のままだと EXIT が右下の操作ヒントに寄りすぎる。
	static constexpr float kBtnStepY = 92.0f;
	static constexpr float kBtnTopGap = 20.0f;		// ロゴ下端からの距離

	//------------------------------------------
	// 配色
	//------------------------------------------
	static constexpr float kSafeArea = 0.035f;

	// 操作ヒント。画面の端に添えるので小さく、控えめに。
	static constexpr float kHintHalfW = 90.0f;
	static constexpr float kHintHalfH = 18.0f;
	static constexpr float kHintStepY = 44.0f;

	static inline const Math::Vector4 kHintCol = { 0.82f, 0.80f, 0.76f, 0.72f };
	static inline const Math::Vector4 kBtnOn = { 1.00f, 0.86f, 0.36f, 1.00f };
	static inline const Math::Vector4 kBtnOff = { 0.58f, 0.52f, 0.50f, 1.00f };

private:
	TitleHud() = default;
	~TitleHud() = default;
public:
	static TitleHud& Instance()
	{
		static TitleHud instance;
		return instance;
	}
};

#define TITLE_UI TitleHud::Instance()

//===================================================
// 更新
//===================================================
inline void TitleHud::Update()
{
	const float dt = static_cast<float>(Time::Instance().GetUnScaledDeltaTime());
	
	KdUiInputHint::Update();

	m_time += dt;
	m_press = std::max(0.0f, m_press - dt / kPressSec);
	m_ghost = std::max(0.0f, m_ghost - dt / kGhostSec);
	m_repeatCool = std::max(0.0f, m_repeatCool - dt);

	// 決定後はロゴが剥がれ切るまで進める。
	// 剥がれ終わってから次のシーンへ行くと、画面が途切れない。
	if (m_decided)
	{
		m_dissolve = std::min(1.0f, m_dissolve + dt / kDissolveSec);
		return;
	}

	// ボタンが出る前は操作を受けない。
	if (ButtonAppear() < 1.0f) return;

	auto& input = KdInputActionSystem::Instance();

	//------------------------------------------
	// 選択
	//------------------------------------------
	Math::Vector2 axis = input.GetAxis("Move");

	if (m_repeatCool <= 0.0f)
	{
		int next = m_choice;
		axis.y *= -1;

		if (axis.y < -0.5f) next = std::max(m_choice - 1, 0);
		else if (axis.y > 0.5f) next = std::min(m_choice + 1, ChoiceCount - 1);

		if (next != m_choice)
		{
			m_prevChoice = m_choice;
			m_ghost = 1.0f;

			m_choice = next;
			m_repeatCool = kRepeatSec;
		}
	}

	//------------------------------------------
	// 決定
	//------------------------------------------
	const bool pressed = input.IsPress("Dicede");
	if (m_needRelease && !pressed) m_needRelease = false;

	if (pressed && !m_needRelease)
	{
		AudioManager::Instance().Play("button");

		m_press = 1.0f;
		m_decided = true;

		if (m_decide) m_decide(static_cast<Choice>(m_choice));
	}
}

//===================================================
// 描画
//===================================================
inline void TitleHud::PushButton(const Math::Vector2& _screen, int _index, const char* _word)
{
	const float appear = ButtonAppear();
	if (appear <= 0.0f) return;

	const bool on = (m_choice == _index);

	// ロゴの下端を基準にする。ロゴの大きさを変えてもボタンが追従する。
	const float logoHalfW = Px(kLogoHalfW);
	const float logoHalfH = logoHalfW * kLogoAspect;

	const float logoCx = _screen.x * kLogoCx;
	const float logoBottom = _screen.y * kLogoCy + logoHalfH;

	// 横幅はロゴより短く。同じだと箱が2つ重なって見えて主従が付かない。
	const float halfW = logoHalfW * kBtnWidthRatio;

	KdUiShader::Instance inst;
	inst.kind = KdUiShader::Button;
	inst.center = { logoCx,
					logoBottom + Px(kBtnTopGap) + Px(kBtnStepY) * static_cast<float>(_index) };
	inst.halfSize = { halfW, Px(kBtnHalfH) };
	inst.color = kBtnOn;
	inst.subColor = kBtnOff;

	// 語は文字コードで渡す。呼び出し側で文字列を書くだけで済む。
	KdUiShader::SetWord(inst, _word);
	// 直前まで選択されていた項目だけ、尾を引かせる。
	const float ghost = (!on && _index == m_prevChoice) ? m_ghost : 0.0f;

	KdUiShader::SetButtonState(inst, on, on ? m_press : 0.0f, appear, ghost);

	// 決定ボタンのアイコン。選択中だけ出る
	KdUiInputHint::SetButtonIcon(inst, "Dicede");

	m_buttons.push_back(inst);
}

//===================================================
// 操作ヒント
//===================================================
// 画面の右下に添える。何を押せば何が起きるかを1行で示す。
// 初めて触る人が迷わないための保険。
inline void TitleHud::PushHint(const Math::Vector2& _screen, int _slot, const char* _word)
{
	const float appear = ButtonAppear();
	if (appear <= 0.0f) return;

	const float safeX = _screen.x * kSafeArea;
	const float safeY = _screen.y * kSafeArea;

	KdUiShader::Instance inst;
	inst.kind = KdUiShader::Hint;
	inst.halfSize = { Px(kHintHalfW), Px(kHintHalfH) };
	inst.center = { _screen.x - safeX - inst.halfSize.x,
					_screen.y - safeY - inst.halfSize.y
					- Px(kHintStepY) * static_cast<float>(_slot) };

	inst.color = kHintCol;
	inst.subColor = kHintCol;

	KdUiInputHint::SetHint(inst, "Dicede", "DECIDE", appear);
	m_buttons.push_back(inst);
}

inline void TitleHud::Draw()
{
	const Math::Vector2 screen = KdHudShader::GetViewportSize();
	if (screen.x <= 0.0f || screen.y <= 0.0f) return;

	// 基準解像度からの倍率。他のUIと同じ考え方。
	m_uiScale = std::min(screen.x / 1920.0f, screen.y / 1080.0f);

	//------------------------------------------
	// ロゴ
	//------------------------------------------
	if (m_logo)
	{
		const float halfW = Px(kLogoHalfW);

		KdTitleShader::Params p;
		p.center = { screen.x * kLogoCx, screen.y * kLogoCy };
		p.halfSize = { halfW, halfW * kLogoAspect };
		p.appear = LogoAppear();

		// 完全に不透明だと背景から浮いて、貼り付けたように見える。
		p.tint = { 1.0f, 1.0f, 1.0f, kLogoAlpha };

		// 常時の効果。タイトルは長く見られる画面なので、
		// 一度きりの登場より止まっている間の生気が効く。
		p.heat = 1.0f;
		p.sheen = 1.0f;
		p.edgeWave = 1.0f;
		p.dust = 1.0f;

		// 呼吸は既定で切る。陽炎と重なると酔う。
		p.breath = 0.0f;

		// ロゴは震わせない。選択の反応はボタン側で十分伝わる。
		p.shake = 0.0f;
		p.dissolve = m_dissolve;

		m_titleShader.Draw(*m_logo, p);
	}

	//------------------------------------------
	// ボタン
	//------------------------------------------
	// 剥がれ始めたら消す。ロゴだけが散る画にする。
	m_buttons.clear();

	if (m_dissolve <= 0.001f)
	{
		PushButton(screen, ChoiceStart, "START");
		PushButton(screen, ChoiceCredit, "CREDIT");	// 6文字。kWordMax ちょうど
		PushButton(screen, ChoiceExit, "EXIT");

		// 操作ヒント。下から順に積む。
		PushHint(screen, 0, "DECIDE");
	}

	m_btnShader.Draw(m_buttons);
}