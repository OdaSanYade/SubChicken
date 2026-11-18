#include "LoadingScreen.h"

namespace
{
	// 画面端からの余白
	constexpr int kMarginX = 48;
	constexpr int kMarginY = 44;

	// 進捗バー
	constexpr int kBarHeight = 4;
	constexpr int kBarMarginY = 26;

	// 表示進捗の追従速度。大きいほど実値に張り付く
	constexpr float kFollowSpeed = 7.0f;

	// アンチエイリアス 0:なし 1:2x 2:4x 3:8x
	constexpr int kAntiAlias = 3;

	const Math::Color kColorLabel = { 0.86f, 0.86f, 0.88f, 1.0f };
	const Math::Color kColorValue = { 1.00f, 1.00f, 1.00f, 1.0f };
	const Math::Color kColorBarBg = { 1.00f, 1.00f, 1.00f, 0.14f };
	const Math::Color kColorBar = { 1.00f, 0.82f, 0.34f, 1.0f };

	//--- 走者 ---
	constexpr int   kRunnerSize = 10;	// 一辺
	constexpr float kRunnerHopSec = 0.42f;	// 1跳ねの周期
	constexpr float kRunnerHopHeight = 11.0f;	// 跳ねる高さ

	//--- 点列 ---
	constexpr int   kDotCount = 5;
	constexpr int   kDotSize = 6;
	constexpr int   kDotGap = 12;	// 中心間の距離
	constexpr float kDotCycleSec = 1.0f;	// 一巡の時間
	constexpr float kDotHopHeight = 7.0f;
	constexpr float kDotPhaseStep = 0.14f;	// 隣との位相差

	//--- ラベルの点 ---
	constexpr float kLabelDotSec = 0.45f;	// 1個増えるまでの時間

	const Math::Color kColorDot = { 1.00f, 0.82f, 0.34f, 0.85f };
}

// ----------------------------------------------------------
void LoadingScreen::Init(int _screenW, int _screenH,
	const std::string& _fontName,
	int _fontHeight, int _fontNo)
{
	m_screenW = _screenW;
	m_screenH = _screenH;
	m_fontNo = _fontNo;
	m_fontHeight = _fontHeight;

	// 矩形描画用の白テクスチャ
	m_whiteTex = std::make_shared<KdTexture>();
	m_whiteTex->Create(1, 1, DXGI_FORMAT_B8G8R8A8_UNORM);

	const UINT white = 0xffffffff;
	KdDirect3D::Instance().WorkDevContext()->UpdateSubresource(
		m_whiteTex->WorkResource(), 0, nullptr, &white, 4, 0);

	m_targetProgress = 0.0f;
	m_viewProgress = 0.0f;
	m_elapsed = 0.0f;
	m_timeValid = false;
}

// ----------------------------------------------------------
void LoadingScreen::Release()
{
	m_whiteTex.reset();
	m_labelUtf8.clear();
}

// ----------------------------------------------------------
void LoadingScreen::Update(float _progress, const char* _labelUtf8)
{
	// 経過時間は自前で測る
	// Time / FPSController はまだ動いていない可能性がある
	const auto now = std::chrono::steady_clock::now();

	float dt = 0.0f;
	if (m_timeValid)
	{
		dt = std::chrono::duration<float>(now - m_lastTime).count();
		dt = std::clamp(dt, 0.0f, 0.1f);	// 停止復帰時の飛びを抑える
	}
	m_lastTime = now;
	m_timeValid = true;

	m_elapsed += dt;

	// 目標は単調増加。重みの補正で戻ることがあるので必ず止める
	m_targetProgress = std::max(m_targetProgress, std::clamp(_progress, 0.0f, 1.0f));

	// 実値へ追従させる。生の値をそのまま出すとガタつく
	const float t = std::min(dt * kFollowSpeed, 1.0f);
	m_viewProgress += (m_targetProgress - m_viewProgress) * t;

	if (_labelUtf8) { m_labelUtf8 = _labelUtf8; }
}

// ----------------------------------------------------------
void LoadingScreen::Draw()
{
	const int left = -m_screenW / 2;
	const int right = m_screenW / 2;
	const int bottom = -m_screenH / 2;

	// 画面座標は中心が原点、Yは上が正
	const int baseY = bottom + kMarginY;

	if (m_showPercent)
	{
		// 進捗バー
		const int barY = baseY + m_fontHeight + kBarMarginY;
		const int barW = m_screenW - kMarginX * 2;
		const int barLeft = left + kMarginX;
		const int fillW = static_cast<int>(barW * m_viewProgress);

		// バーの先端を走る(進捗に連動)
		DrawRunner(barLeft, barY, barW);
	}
}

// ----------------------------------------------------------
// 走者
// ----------------------------------------------------------
void LoadingScreen::DrawRunner(int _barLeft, int _barY, int _barW)
{
	// 跳ねの位相
	const float phase =
		std::fmod(m_elapsed, kRunnerHopSec) / kRunnerHopSec;

	const float hop = std::sin(phase * 3.14159265f) * kRunnerHopHeight;

	// 着地の瞬間だけ横に潰す。跳ねに重さが出る
	const float squash = 1.0f - std::sin(phase * 3.14159265f) * 0.18f;

	const int w = static_cast<int>(kRunnerSize / squash);
	const int h = static_cast<int>(kRunnerSize * squash);

	// バーの先端。端で切れないよう半分ぶん内側へ寄せる
	const float travel = static_cast<float>(_barW - kRunnerSize);
	const int x = _barLeft + static_cast<int>(travel * m_viewProgress);

	// バーの上に乗せる
	const int y = _barY + kBarHeight + static_cast<int>(hop);
}

// ----------------------------------------------------------
// 点列
// ----------------------------------------------------------
void LoadingScreen::DrawDots(int _rightX, int _baseY)
{
	const float cycle = std::fmod(m_elapsed, kDotCycleSec) / kDotCycleSec;

	// 右端を基準に左へ並べる
	const int totalW = kDotGap * (kDotCount - 1) + kDotSize;
	const int startX = _rightX - totalW;

	for (int i = 0; i < kDotCount; ++i)
	{
		// 左の点ほど先に跳ねる
		float phase = cycle - static_cast<float>(i) * kDotPhaseStep;

		// 0未満は1周ぶん戻す
		if (phase < 0.0f) { phase += 1.0f; }

		// 跳ねるのは前半だけ。後半は休ませて波に間を作る
		float hop = 0.0f;
		float bright = 0.35f;

		if (phase < 0.5f)
		{
			const float t = phase * 2.0f;
			hop = std::sin(t * 3.14159265f) * kDotHopHeight;
			bright = 0.35f + std::sin(t * 3.14159265f) * 0.65f;
		}

		Math::Color color = kColorDot;
		color.A(color.A() * bright);
	}
}