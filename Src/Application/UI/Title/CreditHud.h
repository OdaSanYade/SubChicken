#pragma once
#include <cmath>
#include <filesystem>
#include "../../../Framework/Shader/UiShader/KdUiInputHint.h"
#include "../../../Framework/Shader/CreditShader/KdCreditShader.h"
#include "../../../Framework/Font/KdSdfFont.h"
#include "Credit/CreditLayout.h"

//===================================================
//
// クレジット画面
//
// タイトル画面の上に開くオーバーレイ。シーンは切り替えない。
//  ・3Dの農場とBGMがそのまま続くので、画面が途切れない
//  ・戻った時にカーソルを Credit に残すのが自然にできる
//
// 流れ
//   credits.json ─読込→ CreditData ─組版→ CreditLayout ─焼く→ SDFアトラス + 1文字ずつの板
//   毎フレーム変わるのはスクロール量だけ。文字の並びは GPU に置いたまま。
//
// 開いている間は credits.json の更新を見張り、保存されたら組み直す。
// 新しく増えた文字も、その時にアトラスへ焼き足される(文字集めの手作業は要らない)。
//
//===================================================
class CreditHud
{
public:

	bool Init()
	{
		if (!m_shader.Init()) return false;
		if (!m_btnShader.Init(4)) return false;

		m_open = false;
		m_finished = false;
		m_appear = 0.0f;
		m_hasFileTime = false;
		m_needBake = true;
		return true;
	}

	void Release()
	{
		m_shader.Release();
		m_btnShader.Release();
		m_font.Release();
		m_insts.clear();
		m_spans.clear();
	}

	// 開く。タイトルのロゴが剥がれ切ってから呼ぶ。
	void Open()
	{
		// 前回から JSON が変わっていれば読み直す。初回は必ず読む。
		if (!m_hasFileTime || IsFileChanged()) Reload();

		m_open = true;
		m_closing = false;
		m_finished = false;

		m_appear = 0.0f;
		m_press = 0.0f;

		m_scroll = 0.0f;
		m_delay = m_data.startDelay;
		m_endTimer = 0.0f;
		m_watchTimer = 0.0f;

		// ★誤爆防止。タイトルで Credit を決めた入力をここで拾わない。
		//   1. 一定時間は決定を受けない
		//   2. 決定キーが一度離されるまで受けない(押しっぱなし対策)
		//   3. マウスは開いた瞬間の押下状態を「前回」として覚える
		m_guard = kGuardSec;
		m_needRelease = true;
		m_mouseWasDown = IsMouseDown();
	}

	bool IsOpen() const { return m_open; }

	// 閉じ切ったら true。呼び出し側はこれを見てタイトルへ戻す。
	bool IsFinished() const { return m_finished; }

	void Update();
	void Draw();

private:

	struct Rect
	{
		float x = 0.0f, y = 0.0f, w = 0.0f, h = 0.0f;	// スクリーン座標(左上原点)

		bool Contains(float _px, float _py) const
		{
			return _px >= x && _px <= x + w && _py >= y && _py <= y + h;
		}
	};

	// 文字が属する行の縦の範囲(基準解像度px)。画面に入る文字を探すのに使う。
	struct Span { float y = 0.0f; float h = 0.0f; };

	static bool IsMouseDown() { return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; }

	bool PollMouseClick();

	//---- データ ----
	bool GetFileTime(std::filesystem::file_time_type& _out) const;
	bool IsFileChanged() const;
	void Reload();

	//---- 文字 ----
	void Bake();

	//---- スクロール ----
	void UpdateScroll(float _dt);
	float ViewHeight() const { return m_screen.y / std::max(m_uiScale, 1e-3f); }
	float StopScroll() const;
	float ExitScroll() const;
	bool  VisibleRange(UINT& _first, UINT& _count) const;

	float Px(float _v) const { return _v * m_uiScale; }

	//------------------------------------------
	// データ
	//------------------------------------------
	KdCreditShader     m_shader;
	KdSdfFont          m_font;
	KdUiShader         m_btnShader;
	std::vector<KdUiShader::Instance> m_buttons;

	CreditData   m_data;
	CreditLayout m_layout;

	// 1文字ずつの板。行の順(y の昇順)に並ぶ。
	std::vector<KdCreditShader::TextInst> m_insts;
	std::vector<Span> m_spans;

	// SDF は大きさに依存しないので、焼き直すのは JSON が変わった時だけ。
	bool  m_needBake = true;

	std::filesystem::file_time_type m_fileTime{};
	bool  m_hasFileTime = false;
	float m_watchTimer = 0.0f;

	bool  m_open = false;
	bool  m_closing = false;
	bool  m_finished = false;

	float m_appear = 0.0f;
	float m_press = 0.0f;

	float m_scroll = 0.0f;		// 基準解像度px
	float m_delay = 0.0f;		// 流れ始めるまでの残り
	float m_endTimer = 0.0f;	// loop 時、流れ切ってからの経過

	float m_guard = 0.0f;
	bool  m_needRelease = false;
	bool  m_mouseWasDown = false;

	// 描画時の画面。マウスの座標変換・当たり判定・スクロールの終わりに使う。
	Math::Vector2 m_screen = { 0.0f, 0.0f };
	float m_uiScale = 1.0f;
	Rect  m_btnRect;

	//------------------------------------------
	// ファイル
	//------------------------------------------
	// ★作業ディレクトリからの相対。プロジェクトのアセット配置に合わせて直すこと。
	static constexpr const char* kJsonPath = "Asset/Credit/credits.json";
	static constexpr float kWatchSec = 0.5f;

	//------------------------------------------
	// 段取り
	//------------------------------------------
	static constexpr float kOpenSec = 0.55f;
	static constexpr float kCloseSec = 0.45f;
	static constexpr float kPressSec = 0.30f;
	static constexpr float kGuardSec = 0.25f;

	// 上下の入力で速さを変える。下で早送り、上で少し巻き戻す。
	static constexpr float kFastMul = 4.0f;
	static constexpr float kRewindMul = -2.0f;

	//------------------------------------------
	// 配置
	//------------------------------------------
	// タイトルの操作ヒントと同じ余白。画面比なのでアスペクト比に依存しない。
	static constexpr float kSafeArea = 0.035f;

	static constexpr float kBtnHalfW = 170.0f;
	static constexpr float kBtnHalfH = 36.0f;

	// 左右組みの、中央からの隙間(基準解像度px)
	static constexpr float kPairGutter = 22.0f;

	// 画面上下で薄れる幅(画面高に対する比)
	static constexpr float kFade = 0.12f;

	//------------------------------------------
	// 配色(タイトルのボタンと揃える)
	//------------------------------------------
	static inline const Math::Vector4 kBtnOn = { 1.00f, 0.86f, 0.36f, 1.00f };
	static inline const Math::Vector4 kBtnOff = { 0.58f, 0.52f, 0.50f, 1.00f };
};

//===================================================
// データ
//===================================================
inline bool CreditHud::GetFileTime(std::filesystem::file_time_type& _out) const
{
	std::error_code ec;
	_out = std::filesystem::last_write_time(kJsonPath, ec);
	return !ec;
}

inline bool CreditHud::IsFileChanged() const
{
	std::filesystem::file_time_type t;
	if (!GetFileTime(t)) return false;
	return t != m_fileTime;
}

// 読み直して組み直す。
// 失敗しても空のまま続ける。戻るボタンはデータに依存しないので必ず使える。
inline void CreditHud::Reload()
{
	m_hasFileTime = GetFileTime(m_fileTime);

	const bool ok = LoadCreditData(kJsonPath, m_data);	// 失敗時は既定値(空)が入る
	m_layout = BuildCreditLayout(m_data);

	// 実際に探した場所を絶対パスで出す。作業ディレクトリの食い違いがすぐ分かる。
	{
		std::error_code ec;
		const auto abs = std::filesystem::absolute(kJsonPath, ec);
		CreditDetail::Log(std::string(ok ? "loaded " : "FAILED ") + (ec ? kJsonPath : abs.string())
			+ " : sections " + std::to_string(m_data.sections.size())
			+ ", lines " + std::to_string(m_layout.lines.size()));
	}

	// 焼くのは描画の時。D3D の資源を作るので、描画と同じ流れでまとめて行う。
	m_needBake = true;
}

//===================================================
// 文字を焼く
//===================================================
inline void CreditHud::Bake()
{
	m_needBake = false;

	m_insts.clear();
	m_spans.clear();

	const auto& lines = m_layout.lines;

	//------------------------------------------
	// 文字集め
	//------------------------------------------
	// 組版に出てくる文字だけを焼く。日本語でも数百字に収まる。
	std::vector<char32_t> chars;
	for (const auto& l : lines)
		for (wchar_t c : l.text) chars.push_back(static_cast<char32_t>(c));

	//------------------------------------------
	// 書体
	//------------------------------------------
	KdSdfFont::Desc desc;
	desc.file = m_data.font.file;
	desc.face = m_data.font.face;
	desc.pixel = m_data.font.pixel;
	desc.emPx = m_data.font.emPx;

	// 距離の幅は em の 3/16。ドット字なら3ドット。
	// 縁取り1ドット + 光2ドットが収まる。
	desc.spreadPx = std::max(1, desc.emPx * 3 / 16);

	if (!m_font.Build(desc, chars))
	{
		CreditDetail::Log("font build FAILED (no text)");
		m_shader.SetTextLines(m_insts);	// 空にする
		return;
	}

	const float ascent = m_font.GetAscent();
	const float descent = m_font.GetDescent();
	const float fieldPerEm = m_font.GetFieldPerEm();

	//------------------------------------------
	// 1文字ずつの板
	//------------------------------------------
	// 横位置の決め方。揃え方ごとに「中央からの行の左端」を出す。
	using AlignFn = float (*)(float _w);
	static constexpr AlignFn kAlign[] = {
		[](float _w) { return -_w * 0.5f; },				// Center
		[](float _w) { return -kPairGutter - _w; },			// Right
		[](float) { return kPairGutter; },				// Left
	};

	for (const auto& l : lines)
	{
		const auto& def = GetCreditStyle(l.style);
		const float size = def.size;

		// 行の幅(送りの合計)
		float width = 0.0f;
		for (wchar_t c : l.text)
		{
			if (const KdSdfFont::Glyph* g = m_font.Find(static_cast<char32_t>(c)))
				width += g->advance * size;
		}

		float pen = kAlign[static_cast<size_t>(l.align)](width);

		// 行の箱の中で、字の上下(アセンダ〜ディセンダ)が縦に中央へ来る位置をベースラインにする
		const float baseline = l.y + (l.h - (ascent + descent) * size) * 0.5f + ascent * size;

		// 見える範囲の判定は行の単位。光や余白のぶん少し広げておく。
		const Span span = { l.y - size * 0.25f, l.h + size * 0.5f };

		const Math::Vector4 params = { def.outlineEm * fieldPerEm, def.glow, 0.0f, 0.0f };

		for (wchar_t c : l.text)
		{
			const KdSdfFont::Glyph* g = m_font.Find(static_cast<char32_t>(c));
			if (!g) continue;

			if (g->hasImage)
			{
				KdCreditShader::TextInst t;
				t.rect = { pen + g->x * size, baseline + g->y * size, g->w * size, g->h * size };
				t.uv = { g->u0, g->v0, g->u1, g->v1 };
				t.color = def.color;
				t.params = params;

				m_insts.push_back(t);
				m_spans.push_back(span);
			}

			pen += g->advance * size;
		}
	}

	m_shader.SetTextLines(m_insts);

	CreditDetail::Log("baked " + std::to_string(m_insts.size()) + " glyph quads");
}

//===================================================
// スクロール
//===================================================
// 座標の約束
//   画面の y = 画面の下端 + (行の y - スクロール量) × 倍率
// スクロール 0 で先頭の行が画面の下端のすぐ外にいる。

// 止める位置。締めの行が画面の中央に来たところ。
// 最後の行が上へ抜け切るまで流すと、何も無い画面に戻るボタンだけが残る。
inline float CreditHud::StopScroll() const
{
	const auto& lines = m_layout.lines;
	if (lines.empty()) return 0.0f;

	const int fi = m_layout.footerIndex;
	const CreditLine& last = (fi >= 0 && fi < static_cast<int>(lines.size()))
		? lines[static_cast<size_t>(fi)] : lines.back();

	return last.y + last.h * 0.5f + ViewHeight() * 0.5f;
}

// 流れ切る位置。最後の行が画面の上端の外へ出たところ。loop 時に使う。
inline float CreditHud::ExitScroll() const
{
	return m_layout.totalHeight + ViewHeight();
}

inline void CreditHud::UpdateScroll(float _dt)
{
	// 一度も描画していないと画面の大きさが分からない。終わりの位置が出せない。
	if (m_screen.y <= 0.0f) return;
	if (m_layout.lines.empty()) return;

	if (m_delay > 0.0f)
	{
		m_delay -= _dt;
		return;
	}

	//------------------------------------------
	// 速さ
	//------------------------------------------
	// 上下の入力で変える。読み飛ばしたい人と、読み返したい人のため。
	float mul = 2.0f;
	{
		const Math::Vector2 axis = KdInputActionSystem::Instance().GetAxis("Move");

		// タイトルと同じ向き。+Y が上。
		if (axis.y < -0.5f) mul = kFastMul;
		else if (axis.y > 0.5f) mul = kRewindMul;
	}

	m_scroll = std::max(0.0f, m_scroll + m_data.scrollSpeed * mul * _dt);

	//------------------------------------------
	// 終わり
	//------------------------------------------
	if (!m_data.loop)
	{
		m_scroll = std::min(m_scroll, StopScroll());
		return;
	}

	const float exitAt = ExitScroll();
	if (m_scroll < exitAt)
	{
		m_endTimer = 0.0f;
		return;
	}

	// 流れ切った。少し間を置いて先頭へ戻す。
	m_scroll = exitAt;
	m_endTimer += _dt;

	if (m_endTimer >= m_data.endHold)
	{
		m_scroll = 0.0f;
		m_endTimer = 0.0f;
	}
}

// 画面に入る文字の範囲。行は y の昇順なので、入る文字は続いた1区間になる。
inline bool CreditHud::VisibleRange(UINT& _first, UINT& _count) const
{
	_first = 0;
	_count = 0;
	if (m_spans.empty()) return false;

	const float viewTop = m_scroll - ViewHeight();	// 画面上端に当たる行の y
	const float viewBottom = m_scroll;				// 画面下端に当たる行の y

	int first = -1;
	int last = -1;

	for (int i = 0; i < static_cast<int>(m_spans.size()); ++i)
	{
		const Span& s = m_spans[static_cast<size_t>(i)];
		if (s.y + s.h < viewTop) continue;
		if (s.y > viewBottom) break;

		if (first < 0) first = i;
		last = i;
	}

	if (first < 0) return false;

	_first = static_cast<UINT>(first);
	_count = static_cast<UINT>(last - first + 1);
	return true;
}

//===================================================
// マウス
//===================================================
// 押した瞬間に、戻るボタンの上にいれば true。
// 押下の前回値はここで毎フレーム更新するので、毎フレーム必ず呼ぶこと。
inline bool CreditHud::PollMouseClick()
{
	const bool down = IsMouseDown();
	const bool edge = down && !m_mouseWasDown;
	m_mouseWasDown = down;

	if (!edge) return false;

	// 自分のウィンドウが前面にある時だけ。裏でクリックしても反応させない。
	HWND hwnd = GetActiveWindow();
	if (!hwnd || GetForegroundWindow() != hwnd) return false;

	POINT pt = {};
	if (!GetCursorPos(&pt)) return false;
	if (!ScreenToClient(hwnd, &pt)) return false;

	RECT rc = {};
	if (!GetClientRect(hwnd, &rc)) return false;

	const float cw = static_cast<float>(rc.right - rc.left);
	const float ch = static_cast<float>(rc.bottom - rc.top);
	if (cw <= 0.0f || ch <= 0.0f) return false;
	if (m_screen.x <= 0.0f || m_screen.y <= 0.0f) return false;

	// クライアント座標 → ビューポート座標。
	// ウィンドウを伸縮しても判定がずれない。
	const float px = static_cast<float>(pt.x) * m_screen.x / cw;
	const float py = static_cast<float>(pt.y) * m_screen.y / ch;

	return m_btnRect.Contains(px, py);
}

//===================================================
// 更新
//===================================================
inline void CreditHud::Update()
{
	if (!m_open) return;

	const float dt = static_cast<float>(Time::Instance().GetUnScaledDeltaTime());

	KdUiInputHint::Update();

	m_press = std::max(0.0f, m_press - dt / kPressSec);
	m_guard = std::max(0.0f, m_guard - dt);

	//------------------------------------------
	// 閉じる途中
	//------------------------------------------
	if (m_closing)
	{
		m_appear = std::max(0.0f, m_appear - dt / kCloseSec);

		if (m_appear <= 0.0f)
		{
			m_open = false;
			m_finished = true;
		}
		return;
	}

	m_appear = std::min(1.0f, m_appear + dt / kOpenSec);

	//------------------------------------------
	// JSON の見張り
	//------------------------------------------
	// 保存されたら組み直す。スクロール位置は残すので、編集中の箇所を見たまま直せる。
	m_watchTimer += dt;
	if (m_watchTimer >= kWatchSec)
	{
		m_watchTimer = 0.0f;
		if (IsFileChanged()) Reload();
	}

	UpdateScroll(dt);

	//------------------------------------------
	// 決定
	//------------------------------------------
	auto& input = KdInputActionSystem::Instance();

	const bool pressed = input.IsPress("Dicede");
	if (m_needRelease && !pressed) m_needRelease = false;

	// マウスは前回値を保つため、受け付けない間も毎フレーム呼ぶ。
	const bool clicked = PollMouseClick();

	const bool ready = (m_guard <= 0.0f);
	const bool padOk = ready && !m_needRelease && pressed;
	const bool mouseOk = ready && clicked;

	if (padOk || mouseOk)
	{
		AudioManager::Instance().Play("button");

		m_press = 1.0f;
		m_closing = true;
	}
}

//===================================================
// 描画
//===================================================
inline void CreditHud::Draw()
{
	if (!m_open) return;

	const Math::Vector2 screen = KdHudShader::GetViewportSize();
	if (screen.x <= 0.0f || screen.y <= 0.0f) return;

	m_screen = screen;
	m_uiScale = std::min(screen.x / 1920.0f, screen.y / 1080.0f);

	//------------------------------------------
	// 背景
	//------------------------------------------
	{
		KdCreditShader::BgParams bp;
		bp.appear = m_appear;
		bp.scroll = m_scroll;
		m_shader.DrawBackground(bp);
	}

	//------------------------------------------
	// 本文
	//------------------------------------------
	// 初回と、JSON を組み直した時に焼く。
	// SDF は拡大しても縮小しても輪郭が保たれるので、解像度が変わっても焼き直さない。
	if (m_needBake) Bake();

	{
		KdCreditShader::TextParams tp;
		if (VisibleRange(tp.first, tp.count))
		{
			tp.atlas = m_font.GetSRV();
			tp.atlasW = m_font.GetWidth();
			tp.atlasH = m_font.GetHeight();

			// 影は右下へ1ドット(em の 1/16)。字の大きさに比例するので、大きい字ほど影も深い。
			tp.shadowPx = static_cast<float>(m_font.GetEmPx()) / 16.0f;

			tp.appear = m_appear;
			tp.scroll = m_scroll;
			tp.uiScale = m_uiScale;
			tp.origin = { screen.x * 0.5f, screen.y };
			tp.fade = kFade;

			m_shader.DrawLines(tp);
		}
	}

	//------------------------------------------
	// 戻るボタン
	//------------------------------------------
	// 常に1つ、常に選択中。迷わせる要素を置かない。
	// 語はタイトルの START / CREDIT / EXIT と同じ英字の書体で揃える。
	m_buttons.clear();
	{
		KdUiShader::Instance inst;
		inst.kind = KdUiShader::Button;
		inst.halfSize = { Px(kBtnHalfW), Px(kBtnHalfH) };

		const float safeX = screen.x * kSafeArea;
		const float safeY = screen.y * kSafeArea;

		inst.center = { screen.x - safeX - inst.halfSize.x,
						screen.y - safeY - inst.halfSize.y };

		inst.color = kBtnOn;
		inst.subColor = kBtnOff;

		KdUiShader::SetWord(inst, "BACK");
		KdUiShader::SetButtonState(inst, true, m_press, m_appear);

		// 決定に使うキーの記号を左に添える
		KdUiInputHint::SetButtonIcon(inst, "Dicede");

		m_buttons.push_back(inst);

		// 当たり判定はクアッド全体。見た目の枠より少し広いので押しやすい。
		m_btnRect.x = inst.center.x - inst.halfSize.x;
		m_btnRect.y = inst.center.y - inst.halfSize.y;
		m_btnRect.w = inst.halfSize.x * 2.0f;
		m_btnRect.h = inst.halfSize.y * 2.0f;
	}

	m_btnShader.Draw(m_buttons);
}