#pragma once
#include "../../GameSystem/Combat/CombatFx.h"
#include "../../GameSystem/Combat/AttackTag.h"
#include "../../GameSystem/Combat/ComboTracker.h"
#include "../../GameSystem/Combat/BreathGauge.h"
#include "CombatHudConfig.h"
#include "../../Inc/Math/Damp.h"

class CombatHud
{
public:

	struct EnemyInfo
	{
		std::uint64_t id = 0;
		Math::Vector3 position;			// 足元
		float height = 3.0f;			// ゲージを出す高さ
		float healthRatio = 1.0f;
		float healthValue = 0.0f;		// 現在HPの実数値
		float healthMax = 0.0f;			// 最大HP
		float poiseRatio = 0.0f;		// 0で無傷、1で崩れる寸前
		bool  windup = false;			// 攻撃の予備動作中
		float windupProgress = 0.0f;	// 溜めの進捗(0→1)。収束リングの半径になる。
		float strikeFlash = 0.0f;		// 発生の閃光(1→0)
		bool  frozen = false;			// 鳴き声で硬化中
		float breakFlash = 0.0f;		// 体幹を崩した瞬間(1→0)
		bool  staggered = false;		// のけぞり・ダウン中。攻めどきを伝える。
		bool  unblockable = false;		// 回避不能技。リングを破線にして区別する。
		float deathFlash = 0.0f;		// 撃破の爆散(1→0)。0で通常表示。
		float staggerRemain = 0.0f;		// 硬直の残り(0〜1)
		float frozenRemain = 0.0f;		// 凍結の残り(0〜1)
		float attackPower = 0.5f;		// 攻撃力の目安(0〜1)。リングの太さになる。
	};

	struct PlayerInfo
	{
		Math::Vector3 position;
		float healthRatio = 1.0f;
		float healthDisplay = 1.0f;		// 遅れて追う値(白帯)
		float healthValue = 100.0f;		// 実数値。数値表示に使う。
		float damageShock = 0.0f;		// 被弾直後の衝撃
		float breathRatio = 0.0f;
		float breathDisplay = 0.0f;
		float breathFlash = 0.0f;
		float counterRatio = 0.0f;		// カウンター受付の残り
		bool  parryWindow = false;		// 回避猶予中
		bool  dodgeReady = true;		// 回避のクールダウンが明けているか
		float cryWaveRatio = 0.0f;		// 鳴き声の波紋
		bool  cryWasFull = false;
		float dodgeCooldown = 1.0f;		// 0で使えない、1で使える
		bool  airborne = false;			// 空中にいるか(点表示の出し分け)
		bool  dead = false;				// 死亡。HUDが崩れる。
	};

	struct StageInfo
	{
		int   aliveCount = 0;
		int   totalCount = 0;
		int   killedCount = 0;		// 倒した数
		float elapsedSec = 0.0f;	// 戦闘の経過時間
	};

	// インジケータの対象になるひよこ。
	// 追従中の個体だけ渡さない。
	struct ChickInfo
	{
		std::uint64_t id = 0;
		Math::Vector3 position;			// 足元
		float height = 1.0f;			// 画面外判定に使う高さ
		bool  carried = false;			// 敵に連れ去られている(赤い点滅)
	};

	struct CameraInfo
	{
		Math::Matrix view = Math::Matrix::Identity;
		float fovDeg = 60.0f;
		float nearZ = 0.01f;
		float farZ = 2000.0f;
		Math::Vector3 position;
	};

	using EnemyProvider = std::function<std::vector<EnemyInfo>()>;
	using PlayerProvider = std::function<bool(PlayerInfo&)>;
	using StageProvider = std::function<bool(StageInfo&)>;
	using CameraProvider = std::function<bool(CameraInfo&)>;
	using ChickProvider = std::function<std::vector<ChickInfo>()>;

	void SetEnemyProvider(EnemyProvider _p) { m_enemyProvider = std::move(_p); }
	void SetPlayerProvider(PlayerProvider _p) { m_playerProvider = std::move(_p); }
	void SetStageProvider(StageProvider _p) { m_stageProvider = std::move(_p); }
	void SetCameraProvider(CameraProvider _p) { m_cameraProvider = std::move(_p); }
	void SetChickProvider(ChickProvider _p) { m_chickProvider = std::move(_p); }

	//------------------------------------------
	// 設定(JSONホットリロード)
	//------------------------------------------
	void SetConfigKey(const std::string& _key)
	{
		m_configKey = _key;
		ReloadConfig();
	}

	const CombatHudConfig& GetConfig() const { return m_cfg; }

	// 踏み込みが届く距離
	void SetStepRange(float _r) { m_stepRange = _r; }

	//------------------------------------------
	// プレビュー(調整用)
	//------------------------------------------
	void SetPreviewEnabled(bool _on) { m_preview = _on; }
	bool IsPreviewEnabled() const { return m_preview; }

	void DrawDebugImGui();

	bool Init()
	{
		ResetPreviewData();
		return m_shader.Init() && m_uiShader.Init();
	}
	void Release() { m_shader.Release(); m_uiShader.Release(); }

	void Reset()
	{
		m_hpFade = {};
		m_breathFade = {};
		m_countFade = {};

		m_prevLockId = 0;
		m_lockTargetValid = false;
		m_lockPunch = 1.0f;
		m_lockSpin = 0.0f;
		m_lockSpinVel = 0.0f;
		m_lockOpen = 0.0f;

		m_prevAlive = -1;
		m_killFlash = 0.0f;

		m_prevCombo = 0;
		m_comboPop = 0.0f;

		m_prevBreath = 0.0f;
		m_breathSpend = 0.0f;

		m_prevHp = 1.0f;
		m_hpNumShow = 0.0f;

		m_focus = 0.0f;
		m_counterHitFlash = 0.0f;
		m_victoryDip = 0.0f;
		m_comboDrop = 0.0f;
		m_healFlash = 0.0f;
		m_idleTime = 0.0f;
		m_deathT = 0.0f;
		m_desat = 0.0f;
		m_prevAir = 0;
		m_airUsed = 0.0f;
		ResetPreviewData();
		m_usingGamepad = false;
		m_deviceDecided = false;
		for (auto& r : m_guideRipple) r = 0.0f;
		m_damages.clear();
	}

	// カメラのソフトロック情報。毎フレーム設定する。
	void SetLockTarget(bool _valid, std::uint64_t _id, const Math::Vector3& _pos)
	{
		// 乗り換わった瞬間だけ跳ねさせ、羽根を速く回す。
		if (_valid && _id != m_prevLockId)
		{
			m_lockPunch = 1.5f;
			m_lockSpinVel = kLockSpinBurst;
		}

		m_lockTargetValid = _valid;
		if (_valid) m_lockLastPos = _pos;
		m_lockTargetPos = _valid ? _pos : m_lockLastPos;
		m_prevLockId = _valid ? _id : 0;
	}

	void Update();
	void Draw();

private:

	//------------------------------------------
	// 注目度の管理
	//------------------------------------------
	struct FadeTrack
	{
		float value = 0.0f;
		float focus = 0.0f;
	};

	static void  UpdateFade(FadeTrack& _t, float _now, float _dt, float _hold);
	static float FadeAlpha(const FadeTrack& _t, float _force);

	//------------------------------------------
	// ダメージ表示
	//------------------------------------------
	struct DamagePopup
	{
		Math::Vector3 world;
		float damage = 0.0f;
		float heavy = 0.0f;		// 強打の度合い(0〜1)
		bool  self = false;		// 自分が受けたダメージ。色を分ける。
		float age = 0.0f;		// 0→1で消える
		float seed = 0.0f;		// 横揺れの位相
	};

	void CollectDamage(float _dt);

	// 最後に動かしたデバイスを判定する。
	// 表記が実機と食い違うと、ガイドが逆に混乱の元になる。
	void UpdateInputDevice();

	//------------------------------------------
	// インスタンスの組み立て
	//------------------------------------------
	void PushLock(const Math::Vector2& _p, float _radius, bool _inRange);
	void PushEnemyBar(const Math::Vector2& _p, const EnemyInfo& _e, float _alpha, bool _locked);
	void PushWarn(const Math::Vector2& _p, float _radius, float _progress, float _flash,
		bool _unblockable, float _power);
	void PushArrow(const Math::Vector2& _p, float _angleRad);
	void PushRing(const Math::Vector2& _p, float _radius, float _ratio);
	void PushHealth(const Math::Vector2& _screen, const PlayerInfo& _pl);
	void PushWave(const Math::Vector2& _p, float _radius, float _ratio, bool _full);
	void PushCount(const Math::Vector2& _screen, const StageInfo& _st);
	void PushCompass(const Math::Vector2& _screen, float _angleRad, float _near,
		float _threat, float _rank);
	void PushChickMarker(const Math::Vector2& _pos, float _angleRad, bool _carried);
	void PushCombo(const Math::Vector2& _screen);
	void PushFocus(const Math::Vector2& _screen, float _counterRatio);
	void PushDamage(const Math::Vector2& _p, const DamagePopup& _d);
	void PushGuide(const Math::Vector2& _screen, const PlayerInfo& _pl);
	void PushPips(const Math::Vector2& _screen, const PlayerInfo& _pl);
	void PushScore(const Math::Vector2& _screen, const StageInfo& _st);
	//------------------------------------------
	// データ
	//------------------------------------------
	EnemyProvider  m_enemyProvider;
	PlayerProvider m_playerProvider;
	StageProvider  m_stageProvider;
	CameraProvider m_cameraProvider;
	ChickProvider  m_chickProvider;

	KdHudShader m_shader;
	std::vector<KdHudShader::Instance> m_instances;
	std::vector<DamagePopup> m_damages;

	FadeTrack m_hpFade;
	FadeTrack m_breathFade;
	FadeTrack m_countFade;
	float m_fadeDelta = 0.0f;

	// ロック
	std::uint64_t m_prevLockId = 0;
	bool m_lockTargetValid = false;
	Math::Vector3 m_lockTargetPos = Math::Vector3::Zero;
	float m_lockPunch = 1.0f;
	float m_lockSpin = 0.0f;
	float m_lockSpinVel = 0.0f;	// 捕捉直後だけ速く、指数で落ち着く

	float m_lockOpen = 0.0f;
	Math::Vector3 m_lockLastPos = Math::Vector3::Zero;

	float m_pulse = 0.0f;

	// 残数
	int   m_prevAlive = -1;
	float m_killFlash = 0.0f;

	// コンボ
	int   m_prevCombo = 0;
	float m_comboPop = 0.0f;

	// 肺活量の消費検出
	float m_prevBreath = 0.0f;
	float m_breathSpend = 0.0f;

	// HPの数値表示
	float m_prevHp = 1.0f;
	float m_hpNumShow = 0.0f;

	// ジャスト回避の集中演出
	float m_focus = 0.0f;
	float m_counterHitFlash = 0.0f;	// カウンターが当たった瞬間(1→0)

	// HP表示の画面位置。被弾ダメージをこの上に出す。
	Math::Vector2 m_healthAnchor = Math::Vector2::Zero;

	// ロック対象までの距離と、踏み込みが届く距離。
	// 射程外のロックはマーカーの色で示す。
	float m_lockPlayerDist = 0.0f;
	float m_stepRange = 25.0f;

	// 殲滅した瞬間、操作ガイドを一度沈める。戦闘の終わりを全体で示す。
	float m_victoryDip = 0.0f;

	// コンボが途切れて落ちる。切れたことに気付かせる。
	float m_comboDrop = 0.0f;
	int   m_dropCount = 0;
	float m_dropRemain = 0.0f;
	float m_dropRank = 0.0f;
	float m_dropDamage = 0.0f;

	// HPの回復。減る演出だけだと回復が無反応に見える。
	float m_healFlash = 0.0f;

	// 何もしていない時間。長いとHUDが眠って視界を空ける。
	float m_idleTime = 0.0f;

	// 死亡。HUD全体が崩れ落ちる。
	float m_deathT = 0.0f;

	// カウンター受付中の彩度落とし。
	float m_desat = 0.0f;

	// 空中攻撃を使った瞬間の点の弾け。
	int   m_prevAir = 0;
	float m_airUsed = 0.0f;

	//------------------------------------------
	// 設定とスケール
	//------------------------------------------
	CombatHudConfig m_cfg;
	std::string     m_configKey;

	// 基準解像度からの倍率。全てのピクセル値に掛ける。
	// これが無いと解像度を変えた途端にUIが相対的に縮む。
	float m_uiScale = 1.0f;

	void  ReloadConfig();
	float Px(float _v) const { return _v * m_uiScale; }

	// 全体の見え方
	Math::Vector4 Tint(const Math::Vector4& _c) const
	{
		Math::Vector4 c = _c;

		// カウンター受付中は彩度を落とす
		if (m_desat > 0.001f)
		{
			const float lum = c.x * 0.299f + c.y * 0.587f + c.z * 0.114f;
			c.x = std::lerp(c.x, lum, m_desat);
			c.y = std::lerp(c.y, lum, m_desat);
			c.z = std::lerp(c.z, lum, m_desat);
		}

		c.w *= GlobalAlpha();
		return c;
	}

	float GlobalAlpha() const
	{
		// 戦闘が途切れると視界を空ける
		float a = 1.0f;

		const float over = m_idleTime - m_cfg.fx.idleSleepSec;
		if (over > 0.0f)
		{
			const float t = std::clamp(over / 2.0f, 0.0f, 1.0f);
			a *= std::lerp(1.0f, m_cfg.fx.idleSleepAlpha, t);
		}

		// 崩壊
		a *= (1.0f - m_deathT);

		return a;
	}

	// 崩壊中は下へ落ちる
	Math::Vector2 Place(const Math::Vector2& _p, const Math::Vector2& _screen) const
	{
		Math::Vector2 p = _p;
		p.y += m_deathT * m_deathT * _screen.y * 0.35f;
		return p;
	}

	//------------------------------------------
	// プレビュー
	//------------------------------------------
	bool m_preview = false;

	// プレビュー用の仮データ。実際に遊ばずに全状態を確認できるようにする。
	// メンバ順の変更に巻き込まれないよう、集成体初期化ではなく明示的に入れる。
	PlayerInfo m_previewPlayer;
	StageInfo  m_previewStage;
	CameraInfo m_previewCamera;

	void ResetPreviewData()
	{
		m_previewPlayer = PlayerInfo{};
		m_previewPlayer.healthRatio = 0.72f;
		m_previewPlayer.healthDisplay = 0.85f;
		m_previewPlayer.healthValue = 72.0f;
		m_previewPlayer.breathRatio = 0.65f;
		m_previewPlayer.breathDisplay = 0.65f;
		m_previewPlayer.dodgeReady = true;

		m_previewStage = StageInfo{};
		m_previewStage.aliveCount = 6;
		m_previewStage.totalCount = 10;

		m_previewCamera = CameraInfo{};
	}

	// 今どちらのデバイスを触っているか。ガイドの表記を切り替える。
	bool  m_usingGamepad = false;
	bool  m_deviceDecided = false;	// 一度でも判定したか

	// 操作ガイドの押下波紋。行ごとに押した瞬間を1にして減衰させる。
	static constexpr int kGuideRowMax = 4;
	float m_guideRipple[kGuideRowMax] = {};

	//------------------------------------------
	// 定数
	//------------------------------------------
	// ここに残すのは「調整する必要が無いもの」だけ。
	// 見た目に関わる値は全て CombatHudConfig 側にある。
	static constexpr float kLockPunchLambda = 12.0f;
	static constexpr float kLockSpinBase = 0.35f;	// 平常の回転
	static constexpr float kLockSpinBurst = 7.0f;		// 捕捉直後の回転
	static constexpr float kLockSpinDecay = 4.5f;
	static constexpr float kLockOpenLambda = 14.0f;	// 鉤の開閉の速さ

	static constexpr float kPulseSpeed = 3.5f;
	static constexpr float kChickMarkerSize = 11.0f;	// ひよこ矢印の半サイズ(基準解像度px)。前回(34)の約1/3
	static constexpr float kChickMarkerAlpha = 0.8f;	// 通常時の不透明度
	static constexpr float kChickIconScale = 1.8f;	// アイコンのクアッド半サイズ(矢印のsize比)
	static constexpr float kChickIconOffset = 3.0f;	// 矢印からの距離(矢印のsize比)
	static constexpr float kKillFlashSec = 0.45f;
	static constexpr float kFocusLambda = 12.0f;
	static constexpr float kSpendSec = 0.8f;		// 肺活量の消費を見せる時間
	static constexpr float kHpNumSec = 1.6f;		// HP数値を出しておく時間

	//フェーズ
	KdUiShader m_uiShader;
	std::vector<KdUiShader::Instance> m_uiInstances;

	int   m_prevPhase = -1;
	float m_phaseBanner = 0.0f;
	static constexpr float kPhaseBannerSec = 2.4f;
	static constexpr float kBannerInSec = 0.22f;
	static constexpr float kBannerOutSec = 0.18f;

private:
	CombatHud() = default;
	~CombatHud() = default;
public:
	static CombatHud& Instance()
	{
		static CombatHud instance;
		return instance;
	}
};

#define HUD CombatHud::Instance()

//===================================================
// 内部ヘルパ
//===================================================
namespace hud_detail {

	// ワールド→クリップ。w<=0 はカメラ後方。
	inline bool ToClip(const Math::Vector3& _world, const Math::Matrix& _viewProj, Math::Vector4& _out)
	{
		_out = Math::Vector4::Transform(
			Math::Vector4(_world.x, _world.y, _world.z, 1.0f), _viewProj);
		return (_out.w > 0.0001f);
	}

	// クリップ→スクリーン(ピクセル、左上原点)
	inline Math::Vector2 ToScreen(const Math::Vector4& _clip, const Math::Vector2& _screen)
	{
		const float invW = 1.0f / _clip.w;
		return Math::Vector2(
			(_clip.x * invW * 0.5f + 0.5f) * _screen.x,
			(1.0f - (_clip.y * invW * 0.5f + 0.5f)) * _screen.y);
	}

	inline bool IsOnScreen(const Math::Vector2& _p, const Math::Vector2& _screen)
	{
		return (_p.x >= 0.0f && _p.x <= _screen.x && _p.y >= 0.0f && _p.y <= _screen.y);
	}

	// 画面外の対象を画面端のどこに出すか。中心からの方向で決める。
	inline Math::Vector2 ClampToEdge(const Math::Vector2& _p, const Math::Vector2& _screen, float _margin)
	{
		const Math::Vector2 center(_screen.x * 0.5f, _screen.y * 0.5f);
		Math::Vector2 d = _p - center;

		const float len = d.Length();
		if (len < 0.0001f) return center;
		d /= len;

		const float halfW = center.x - _margin;
		const float halfH = center.y - _margin;

		const float tx = (std::abs(d.x) > 0.0001f) ? (halfW / std::abs(d.x)) : FLT_MAX;
		const float ty = (std::abs(d.y) > 0.0001f) ? (halfH / std::abs(d.y)) : FLT_MAX;

		return center + d * std::min(tx, ty);
	}

	//------------------------------------------
	// 記号用のパレット
	//------------------------------------------
	// hud_detail はCombatHudの外にあるので設定を直接見られない。
	// 起動時とリロード時に CombatHud から同期する。
	inline Math::Vector4 gPadA = { 0.42f, 0.72f, 0.30f, 1.00f };
	inline Math::Vector4 gPadB = { 0.84f, 0.24f, 0.22f, 1.00f };
	inline Math::Vector4 gPadX = { 0.20f, 0.46f, 0.82f, 1.00f };
	inline Math::Vector4 gPadY = { 0.92f, 0.72f, 0.18f, 1.00f };
	inline Math::Vector4 gPadNeutral = { 0.30f, 0.32f, 0.36f, 1.00f };
	inline Math::Vector4 gKeycap = { 0.26f, 0.27f, 0.31f, 1.00f };

	//------------------------------------------
	// キー/ボタン → 表示ラベル
	//------------------------------------------
	// 文字コードは 0=A .. 25=Z。負の値は「その桁は無し」。
	inline void SetLabel(Math::Vector3& _out, int _a, int _b = -1, int _c = -1)
	{
		_out = { static_cast<float>(_a), static_cast<float>(_b), static_cast<float>(_c) };
	}

	// LT/RT のラベル
	inline void TriggerLabel(bool _left, Math::Vector3& _out)
	{
		// L/R + T
		SetLabel(_out, _left ? 11 : 17, 19);
	}

	// ゲームパッドのビットマスクから、形状とラベルと色を決める。
	// 分岐ではなくテーブル引き。★ボタンを増やしたらここへ足す。
	inline int MaskToShape(WORD _mask, Math::Vector3& _outCodes, Math::Vector4& _outColor)
	{
		struct Entry
		{
			WORD mask;
			int  shape;
			int  c0, c1;
			const Math::Vector4* color;
		};

		static const Entry kTable[] = {
			{ XINPUT_GAMEPAD_A,              0,  0, -1, &gPadA },		// A
			{ XINPUT_GAMEPAD_B,              0,  1, -1, &gPadB },		// B
			{ XINPUT_GAMEPAD_X,              0, 23, -1, &gPadX },		// X
			{ XINPUT_GAMEPAD_Y,              0, 24, -1, &gPadY },		// Y
			{ XINPUT_GAMEPAD_LEFT_SHOULDER,  1, 11,  1, &gPadNeutral },	// LB
			{ XINPUT_GAMEPAD_RIGHT_SHOULDER, 1, 17,  1, &gPadNeutral },	// RB
			{ XINPUT_GAMEPAD_LEFT_THUMB,     0, 11, 18, &gPadNeutral },	// LS
			{ XINPUT_GAMEPAD_RIGHT_THUMB,    0, 17, 18, &gPadNeutral },	// RS
			{ XINPUT_GAMEPAD_START,          4, 12, -1, &gPadNeutral },	// Menu
			{ XINPUT_GAMEPAD_BACK,           4, 21, -1, &gPadNeutral },	// View
			{ XINPUT_GAMEPAD_DPAD_UP,        3, 20, -1, &gPadNeutral },
			{ XINPUT_GAMEPAD_DPAD_DOWN,      3,  3, -1, &gPadNeutral },
			{ XINPUT_GAMEPAD_DPAD_LEFT,      3, 11, -1, &gPadNeutral },
			{ XINPUT_GAMEPAD_DPAD_RIGHT,     3, 17, -1, &gPadNeutral },
		};

		for (const auto& e : kTable)
		{
			if ((_mask & e.mask) == 0) continue;

			SetLabel(_outCodes, e.c0, e.c1);
			_outColor = *e.color;
			return e.shape;
		}

		SetLabel(_outCodes, -1);
		_outColor = gPadNeutral;
		return 0;
	}

	// VkToLabel は下で定義するが、PickSymbol から先に呼ぶので宣言だけ置く。
	inline int VkToSymbol(int _vk, Math::Vector3& _out);

	// バインドから表示する記号を決める。
	// _wantPad : true でゲームパッド表記、false でキーボード表記。
	// 割り当てが無ければ false を返す。
	inline bool PickSymbol(const KdInputActionSystem::ActionBinding& _bind,
		bool _wantPad, int& _outShape, Math::Vector3& _outCodes, Math::Vector4& _outColor)
	{
		if (_wantPad)
		{
			if (_bind.isTriggerButton)
			{
				_outShape = 2;	// KdHudShader::BtnTrigger
				TriggerLabel(_bind.triggerSide == KdGamepadTrigger::Left, _outCodes);
				_outColor = gPadNeutral;
				return true;
			}

			if (_bind.currentGamepadMask == 0) return false;

			_outShape = MaskToShape(_bind.currentGamepadMask, _outCodes, _outColor);
			return (_outCodes.x >= 0.0f);
		}

		// キーボード
		const int vk = _bind.currentKeyboardKeys.empty() ? 0 : _bind.currentKeyboardKeys[0];
		if (vk == 0) return false;

		_outColor = gKeycap;

		// 形状はキーごとに決める。マウスは本体の形、修飾キーは語で出す。
		_outShape = VkToSymbol(vk, _outCodes);

		return (_outCodes.x >= 0.0f);
	}

	// VKコード → 最大3文字のラベル
	inline int VkToSymbol(int _vk, Math::Vector3& _out)
	{
		// 英字はキーキャップに1文字
		if (_vk >= 'A' && _vk <= 'Z')
		{
			SetLabel(_out, _vk - 'A');
			return KdHudShader::BtnKeycap;
		}

		// 分岐ではなくテーブル引き。★キーを増やしたらここへ足す。
		struct Entry { int vk; int shape; int c0, c1, c2; };

		static const Entry kTable[] = {
			// マウス。形で示し、名称を右に添える。
			{ VK_LBUTTON,  KdHudShader::BtnMouseL,  11, 12,  1 },	// LMB
			{ VK_RBUTTON,  KdHudShader::BtnMouseR,  17, 12,  1 },	// RMB
			{ VK_MBUTTON,  KdHudShader::BtnMouseM,  12, 12,  1 },	// MMB

			// 語で書くキー。c0 に語ID。略号では読めないものだけ。
			{ VK_SHIFT,    KdHudShader::BtnWordKey, KdHudShader::WordShift, -1, -1 },
			{ VK_LSHIFT,   KdHudShader::BtnWordKey, KdHudShader::WordShift, -1, -1 },
			{ VK_RSHIFT,   KdHudShader::BtnWordKey, KdHudShader::WordShift, -1, -1 },
			{ VK_CONTROL,  KdHudShader::BtnWordKey, KdHudShader::WordCtrl,  -1, -1 },
			{ VK_LCONTROL, KdHudShader::BtnWordKey, KdHudShader::WordCtrl,  -1, -1 },
			{ VK_RCONTROL, KdHudShader::BtnWordKey, KdHudShader::WordCtrl,  -1, -1 },
			{ VK_MENU,     KdHudShader::BtnWordKey, KdHudShader::WordAlt,   -1, -1 },
			{ VK_LMENU,    KdHudShader::BtnWordKey, KdHudShader::WordAlt,   -1, -1 },
			{ VK_RMENU,    KdHudShader::BtnWordKey, KdHudShader::WordAlt,   -1, -1 },
			{ VK_SPACE,    KdHudShader::BtnWordKey, KdHudShader::WordSpace, -1, -1 },
			{ VK_RETURN,   KdHudShader::BtnWordKey, KdHudShader::WordEnter, -1, -1 },
			{ VK_TAB,      KdHudShader::BtnWordKey, KdHudShader::WordTab,   -1, -1 },
			{ VK_ESCAPE,   KdHudShader::BtnWordKey, KdHudShader::WordEsc,   -1, -1 },

			// 残りはキーキャップに3文字
			{ VK_UP,       KdHudShader::BtnKeycap,  20, 15, -1 },	// UP
			{ VK_DOWN,     KdHudShader::BtnKeycap,   3, 14, 22 },	// DOW
			{ VK_LEFT,     KdHudShader::BtnKeycap,  11,  4,  5 },	// LEF
			{ VK_RIGHT,    KdHudShader::BtnKeycap,  17,  8,  6 },	// RIG
		};

		for (const auto& e : kTable)
		{
			if (e.vk != _vk) continue;

			SetLabel(_out, e.c0, e.c1, e.c2);
			return e.shape;
		}

		SetLabel(_out, -1);
		return KdHudShader::BtnKeycap;
	}
}

//===================================================
// 注目度
//===================================================
inline void CombatHud::UpdateFade(FadeTrack& _t, float _now, float _dt, float _hold)
{
	// 値が動いたら注目度を立てる
	if (std::abs(_now - _t.value) > 0.0005f)
	{
		_t.focus = 1.0f;
		_t.value = _now;
	}
	else
	{
		_t.focus = std::max(0.0f, _t.focus - _dt / _hold);
	}
}

inline float CombatHud::FadeAlpha(const FadeTrack& _t, float _force)
{
	// _force : 危険域や満タンなど、常に見せたい状態の強さ
	return std::max(_t.focus, std::clamp(_force, 0.0f, 1.0f));
}
//===================================================
// 設定
//===================================================
inline void CombatHud::ReloadConfig()
{
	if (m_configKey.empty()) return;

	auto& list = RES_MGR.GetDataList();
	if (!list || !list->Contains(m_configKey)) return;

	// パースはメインスレッドで行う。
	// 監視スレッドのコールバックから直接書き換えると、
	// 描画の途中で値が入れ替わって1フレームだけ崩れる。
	m_cfg.Load(list->Get(m_configKey));
	list->ClearDirty(m_configKey);

	// 記号用のパレットへ同期する。
	// hud_detail は設定を直接見られないので、ここで写す。
	hud_detail::gPadA = m_cfg.color.padA;
	hud_detail::gPadB = m_cfg.color.padB;
	hud_detail::gPadX = m_cfg.color.padX;
	hud_detail::gPadY = m_cfg.color.padY;
	hud_detail::gPadNeutral = m_cfg.color.padNeutral;
	hud_detail::gKeycap = m_cfg.color.keycap;
}

inline void CombatHud::Update()
{
	// 実時間で進める。ヒットストップ中もUIは動かす。
	const float dt = CombatFx::Instance().RealDelta();
	m_fadeDelta = dt;

	// JSONが書き換わっていれば読み直す。
	// ゲームを止めずに色や配置を詰められる。
	if (!m_configKey.empty())
	{
		auto& list = RES_MGR.GetDataList();
		if (list && list->IsDirty(m_configKey)) ReloadConfig();
	}

	//------------------------------------------
	// ロック
	//------------------------------------------
	m_lockPunch = Damp::Value(m_lockPunch, 1.0f, kLockPunchLambda, dt);

	// 捕捉直後だけ速く回り、指数で平常の速さへ落ち着く
	m_lockSpinVel = Damp::Value(m_lockSpinVel, kLockSpinBase, kLockSpinDecay, dt);
	m_lockSpin += m_lockSpinVel * dt;
	// 捕捉で開き、外すと閉じる。閉じ切るまでは描き続ける。
	m_lockOpen = Damp::Value(m_lockOpen, m_lockTargetValid ? 1.0f : 0.0f,
		kLockOpenLambda, dt);

	m_pulse += dt * kPulseSpeed;
	m_killFlash = std::max(0.0f, m_killFlash - dt / kKillFlashSec);

	//------------------------------------------
	// コンボ
	//------------------------------------------
	// 増えた瞬間を自前で検出する。ComboTracker側にgetterを増やさずに済ませる。
	const int now = ComboTracker::Instance().GetCount();
	if (now > m_prevCombo) m_comboPop = 1.0f;
	m_prevCombo = now;
	m_comboPop = std::max(0.0f, m_comboPop - dt / m_cfg.combo.popSec);

	//------------------------------------------
	// 肺活量の消費
	//------------------------------------------
	const float breath = BreathGauge::Instance().GetRatio();
	if (breath < m_prevBreath - 0.001f) m_breathSpend = 1.0f;
	m_prevBreath = breath;
	m_breathSpend = std::max(0.0f, m_breathSpend - dt / kSpendSec);

	//------------------------------------------
	// タイマー類
	//------------------------------------------
	m_hpNumShow = std::max(0.0f, m_hpNumShow - dt / m_cfg.health.numShowSec);

	// 操作ガイドの押下波紋
	for (auto& r : m_guideRipple)
		r = std::max(0.0f, r - dt / m_cfg.guide.rippleSec);

	// カウンター成立と殲滅の余韻
	m_counterHitFlash = std::max(0.0f, m_counterHitFlash - dt / m_cfg.fx.counterFlashSec);
	m_victoryDip = std::max(0.0f, m_victoryDip - dt / m_cfg.fx.victoryDipSec);
	m_healFlash = std::max(0.0f, m_healFlash - dt / m_cfg.fx.healFlashSec);
	m_airUsed = std::max(0.0f, m_airUsed - dt * 3.0f);

	// 何も起きていない時間
	m_idleTime += dt;

	CollectDamage(dt);
	UpdateInputDevice();
}

//===================================================
// ダメージ表示
//===================================================
inline void CombatHud::CollectDamage(float _dt)
{
	// 前フレームの命中を拾う
	for (const auto& ev : CombatFx::Instance().Events())
	{
		const bool byPlayer = (ev.attackerType == Collision::ObjectType::PlayerAttack);
		const bool onPlayer = (ev.attackerType == Collision::ObjectType::EnemyAttack);

		if (!byPlayer && !onPlayer) continue;

		// HUDを起こす
		m_idleTime = 0.0f;

		// カウンターが当たった瞬間
		if (byPlayer && ev.tag == PlayerAttackTag::kCounter) m_counterHitFlash = 1.0f;

		if (ev.damage <= 0.0f) continue;
		if (onPlayer) continue;

		// 同じ場所への連撃はまとめる
		bool merged = false;
		for (auto& e : m_damages)
		{
			if (e.self != onPlayer) continue;
			if (e.age > m_cfg.fx.mergeSec / m_cfg.damage.lifeSec) continue;

			const float r2 = m_cfg.fx.mergeRadius * m_cfg.fx.mergeRadius;
			if ((e.world - ev.position).LengthSquared() > r2) continue;

			e.damage += ev.damage;
			e.age = 0.0f;					// 出し直す
			e.heavy = std::max(e.heavy, 0.35f);
			merged = true;
			break;
		}
		if (merged) continue;

		if (static_cast<int>(m_damages.size()) >= m_cfg.damage.maxCount) break;

		DamagePopup d;
		d.self = onPlayer;
		d.world = ev.position;
		d.damage = ev.damage;
		// 止め時間が長い攻撃ほど強打
		d.heavy = std::clamp((static_cast<float>(ev.hitstopFrames) - 6.0f) / 12.0f, 0.0f, 1.0f);
		d.age = 0.0f;
		// 同じ位置に重ならないよう散らす種
		d.seed = static_cast<float>(m_damages.size()) * 0.37f
			+ static_cast<float>(ev.attackId % 17) * 0.059f;
		d.seed = std::fmod(d.seed, 1.0f);

		m_damages.push_back(d);
	}

	// 寿命の経過と回収
	for (auto& d : m_damages) d.age += _dt / m_cfg.damage.lifeSec;

	m_damages.erase(
		std::remove_if(m_damages.begin(), m_damages.end(),
			[](const DamagePopup& _d) { return _d.age >= 1.0f; }),
		m_damages.end());
}

//===================================================
// 入力デバイスの判定
//===================================================
inline void CombatHud::UpdateInputDevice()
{
	auto& input = KdInputActionSystem::Instance();

	// 初回は接続状況で決める
	if (!m_deviceDecided)
	{
		m_usingGamepad = input.IsGamepadConnected(0);
		m_deviceDecided = true;
	}

	// パッドに触れたらパッド、キーを押したらキーボードへ切り替える
	const XINPUT_STATE& st = KdXInputCache::Instance().GetState(0);

	const bool padMoved =
		(st.Gamepad.wButtons != 0)
		|| (st.Gamepad.bLeftTrigger > 40) || (st.Gamepad.bRightTrigger > 40)
		|| (std::abs(static_cast<int>(st.Gamepad.sThumbLX)) > 12000)
		|| (std::abs(static_cast<int>(st.Gamepad.sThumbLY)) > 12000)
		|| (std::abs(static_cast<int>(st.Gamepad.sThumbRX)) > 12000)
		|| (std::abs(static_cast<int>(st.Gamepad.sThumbRY)) > 12000);

	if (padMoved)
	{
		m_usingGamepad = true;
		return;
	}

	// 実際に割り当てられているキーだけを見る
	for (const auto& [name, bind] : input.GetActionBindings())
	{
		for (int vk : bind.currentKeyboardKeys)
		{
			if (vk == 0) continue;
			if (GetAsyncKeyState(vk) & 0x8000)
			{
				m_usingGamepad = false;
				return;
			}
		}
	}
}

//===================================================
// インスタンスの組み立て
//===================================================
inline void CombatHud::PushLock(const Math::Vector2& _p, float _radius, bool _inRange)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Lock;
	inst.center = _p;
	inst.halfSize = { _radius, _radius };
	inst.color = Tint(m_cfg.color.beak);
	inst.subColor = Tint(m_cfg.color.plume);
	inst.params = { m_lockOpen, _inRange ? 1.0f : 0.0f, m_lockSpin, 0.0f };
	m_instances.push_back(inst);
}

inline void CombatHud::PushEnemyBar(const Math::Vector2& _p, const EnemyInfo& _e,
	float _alpha, bool _locked)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Gauge;
	inst.center = _p;

	const float k = (_locked ? m_cfg.enemyBar.lockedScale : 1.0f)
		* (1.0f + _e.deathFlash * 0.9f);

	const float qw = Px(m_cfg.enemyBar.halfW) * k * KdHudShader::kEnemyBarExpandX;
	const float qh = Px(m_cfg.enemyBar.halfH) * k * KdHudShader::kEnemyBarExpandY;

	inst.halfSize = { qw, qh };

	const float barLocalY = qh - Px(m_cfg.enemyBar.halfH) * k * 1.20f;
	inst.center.y += barLocalY;

	const float alpha = _locked ? 1.0f : _alpha;

	inst.color = Tint(m_cfg.color.enemyHp);
	inst.subColor = Tint(m_cfg.color.enemyBg);
	inst.color.w *= alpha;
	inst.subColor.w *= alpha;

	// 凍結は残り時間の割合で渡す
	const float frozen = _e.frozen
		? std::max(_e.frozenRemain, 0.15f) : 0.0f;

	inst.params = { _e.healthRatio, _e.poiseRatio, frozen, _e.breakFlash };
	const float now = std::floor(std::clamp(_e.healthValue, 0.0f, 999.0f));
	const float mx = std::floor(std::clamp(_e.healthMax, 0.0f, 999.0f));
	const float packedHp = (mx > 0.0f) ? (now * 1000.0f + mx) : 0.0f;

	inst.extra = { _locked ? 1.0f : 0.0f,
				   _e.staggered ? std::max(_e.staggerRemain, 0.15f) : 0.0f,
				   _e.deathFlash, packedHp };

	m_instances.push_back(inst);
}

inline void CombatHud::PushWarn(const Math::Vector2& _p, float _radius,
	float _progress, float _flash, bool _unblockable, float _power)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Warn;
	inst.center = _p;

	// 衝撃波が外へ抜けるぶんクアッドを広く取る
	const float quad = _radius * m_cfg.warn.quadScale;
	inst.halfSize = { quad, quad };

	inst.color = Tint(m_cfg.color.alert);
	inst.subColor = Tint(m_cfg.color.alertHi);
	inst.params = { _progress, _radius, _flash, _unblockable ? 1.0f : 0.0f };

	// 攻撃力の目安。リングの太さで示す。
	inst.extra = { _power, 0.0f, 0.0f, 0.0f };
	m_instances.push_back(inst);
}

inline void CombatHud::PushArrow(const Math::Vector2& _p, float _angleRad)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Arrow;
	inst.center = _p;
	inst.halfSize = { Px(18.0f), Px(18.0f) };
	inst.color = Tint(m_cfg.color.alert);
	inst.subColor = Tint(m_cfg.color.enemyBg);
	inst.params = { 0.0f, 0.0f, _angleRad, 0.0f };
	m_instances.push_back(inst);
}

inline void CombatHud::PushRing(const Math::Vector2& _p, float _radius, float _ratio)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Ring;
	inst.center = _p;
	inst.halfSize = { _radius, _radius };
	inst.color = Tint(m_cfg.color.counter);
	inst.subColor = Tint(m_cfg.color.counterBg);
	inst.params = { _ratio, Px(3.5f), 0.0f, 0.0f };
	m_instances.push_back(inst);
}

inline void CombatHud::PushHealth(const Math::Vector2& _screen, const PlayerInfo& _pl)
{
	const float danger = std::clamp(
		(m_cfg.health.dangerLine - _pl.healthRatio) / m_cfg.health.dangerLine, 0.0f, 1.0f);
	const float shortLine = BreathGauge::kShortCost / BreathGauge::kMax;

	// 保持時間を情報の性質ごとに変える
	UpdateFade(m_hpFade, _pl.healthRatio, m_fadeDelta, m_cfg.fade.holdHp);
	UpdateFade(m_breathFade, _pl.breathRatio, m_fadeDelta, m_cfg.fade.holdBreath);

	// HPは下限を高く、肺活量は閾値でだけ濃く
	const float hpForce = std::max(danger, _pl.damageShock);
	const float brForce = (_pl.breathRatio >= 1.0f) ? 1.0f : 0.42f;

	const float hpA = m_cfg.fade.idleAlphaHp
		+ (1.0f - m_cfg.fade.idleAlphaHp) * FadeAlpha(m_hpFade, hpForce);
	const float brA = m_cfg.fade.idleAlphaOther
		+ (1.0f - m_cfg.fade.idleAlphaOther) * FadeAlpha(m_breathFade, brForce);

	const float alpha = std::max(hpA, brA);

	// HPが動いたら数値を出す
	if (std::abs(_pl.healthRatio - m_prevHp) > 0.0005f) m_hpNumShow = 1.0f;
	m_prevHp = _pl.healthRatio;

	// 左下
	const float safeX = _screen.x * m_cfg.safeAreaPercent;
	const float safeY = _screen.y * m_cfg.safeAreaPercent;

	const float w = _screen.x * m_cfg.health.widthRatio;
	const float h = Px(m_cfg.health.halfHeight);
	const Math::Vector2 center(safeX + _screen.x * m_cfg.health.marginX + w,
		_screen.y - safeY - Px(m_cfg.health.marginY));

	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Health;
	inst.center = center;
	inst.halfSize = { w + h * KdHudShader::kHealthPadX,
					  h * (1.0f + KdHudShader::kHealthPadY) };

	// Tiltはクアッド全体を回転させる。
	inst.tilt = 0.0f;

	inst.color = Tint(m_cfg.color.life);
	inst.subColor = Tint(m_cfg.color.lifeDark);
	inst.color.w *= alpha;
	inst.subColor.w *= alpha;

	// x=HP y=HP遅延 z=肺活量 w=肺活量遅延
	inst.params = { _pl.healthRatio, _pl.healthDisplay,
					_pl.breathRatio, _pl.breathDisplay };

	const float show = std::max(m_hpNumShow, danger);

	// 数値は割合
	const float pct = std::floor(std::clamp(_pl.healthDisplay, 0.0f, 1.0f) * 100.0f + 0.5f);
	const float packed = pct + std::clamp(show, 0.0f, 0.999f);

	// z は正で肺活量の消費、負でHPの回復。符号で2つの演出を運ぶ。
	const float spendOrHeal = (m_healFlash > 0.001f) ? -m_healFlash : m_breathSpend;

	inst.extra = { _pl.damageShock * m_cfg.health.shockScale,
				   packed, spendOrHeal, _pl.breathFlash };

	m_instances.push_back(inst);
}

inline void CombatHud::PushWave(const Math::Vector2& _p, float _radius, float _ratio, bool _full)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Wave;
	inst.center = _p;
	inst.halfSize = { _radius, _radius };
	inst.color = Tint(_full
		? Math::Vector4(1.00f, 0.94f, 0.78f, 0.95f)
		: Math::Vector4(0.86f, 0.93f, 0.98f, 0.80f));
	inst.subColor = Tint(m_cfg.color.plume);
	inst.params = { _ratio, Px(5.0f), 0.0f, 0.0f };
	m_instances.push_back(inst);
}

inline void CombatHud::PushCount(const Math::Vector2& _screen, const StageInfo& _st)
{
	if (_st.totalCount <= 0) return;

	// 倒した瞬間を検出して光らせる
	if (m_prevAlive >= 0 && _st.aliveCount < m_prevAlive) m_killFlash = 1.0f;
	m_prevAlive = _st.aliveCount;

	UpdateFade(m_countFade, static_cast<float>(_st.aliveCount), m_fadeDelta, m_cfg.fade.holdCount);

	// 残りわずかは常に濃く。終盤であることを伝える。
	const float force = (_st.aliveCount <= m_cfg.count.urgentCount) ? 0.7f : 0.0f;
	const float alpha = m_cfg.fade.idleAlphaOther
		+ (1.0f - m_cfg.fade.idleAlphaOther)
		* FadeAlpha(m_countFade, std::max(force, m_killFlash));

	// 左上のスコアの下
	const float dots = static_cast<float>(std::min(_st.totalCount, m_cfg.count.maxDots));
	const float w = std::min(dots * Px(m_cfg.count.dotWidth), Px(m_cfg.score.halfW));

	const float h = Px(m_cfg.count.halfH);

	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Count;
	const float safeX = _screen.x * m_cfg.safeAreaPercent;
	const float safeY = _screen.y * m_cfg.safeAreaPercent;

	inst.center = Place({ safeX + Px(m_cfg.score.marginX) + w,
						  safeY + Px(m_cfg.score.marginY)
						  + Px(m_cfg.score.halfH) * 2.0f + Px(m_cfg.count.posY) },
		_screen);
	inst.halfSize = { w, h };
	inst.color = Tint(m_cfg.color.plume);
	inst.subColor = Tint(m_cfg.color.plumeDark);
	inst.color.w *= alpha;
	inst.subColor.w *= alpha;
	inst.params = { static_cast<float>(_st.aliveCount), dots, m_killFlash, 0.0f };

	m_instances.push_back(inst);
}

inline void CombatHud::PushCompass(const Math::Vector2& _screen, float _angleRad,
	float _near, float _threat, float _rank)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Compass;
	inst.center = { _screen.x * 0.5f, _screen.y * 0.5f };

	const float r = Px(m_cfg.compass.radius + 24.0f);
	inst.halfSize = { r, r };

	inst.color = Tint(m_cfg.color.compass);
	inst.subColor = Tint(m_cfg.color.compassHi);
	inst.params = { _angleRad, _near, _threat, _rank };

	m_instances.push_back(inst);
}

// ひよこ用。画面中心を囲む円周上に、ひよこの方向を指す矢印を出す。
inline void CombatHud::PushChickMarker(const Math::Vector2& _pos, float _angleRad,
	bool _carried)
{
	Math::Vector4 face, outline;
	float size = Px(kChickMarkerSize);

	if (_carried)
	{
		// 赤〜白を往復
		const float t = 0.5f + 0.5f * std::sin(m_pulse * 3.0f);
		const float a = 0.7f + 0.2f * t;

		// 連れ去り中だけ50%大きくする
		size *= 1.5f;

		face = { 1.0f, std::lerp(0.1f, 1.0f, t), std::lerp(0.1f, 1.0f, t), a };
		outline = { 0.25f, 0.0f, 0.0f, a };
	}
	else
	{
		// 通常は黄色、不透明度80%
		face = { 1.0f, 0.9f, 0.1f, kChickMarkerAlpha };
		outline = { 0.2f, 0.12f, 0.0f, kChickMarkerAlpha };
	}

	// 縁取り(一回り大きい暗色)を先に積み、その上に本体を重ねる
	for (int layer = 0; layer < 2; ++layer)
	{
		const float k = (layer == 0) ? 1.3f : 1.0f;

		KdHudShader::Instance inst;
		inst.kind = KdHudShader::Arrow;
		inst.center = _pos;
		inst.halfSize = { size * k, size * k };
		inst.color = (layer == 0) ? outline : face;
		inst.subColor = outline;
		inst.params = { 0.0f, 0.0f, _angleRad, 0.0f };
		m_instances.push_back(inst);
	}

	// 矢印の背面側にひよこアイコンを添える。
	// 画面外 → 円周の内側(画面中心側)、画面内 → 矢印の上。
	{
		const Math::Vector2 dir(std::cos(_angleRad), std::sin(_angleRad));
		const float iconHalf = size * kChickIconScale;

		KdHudShader::Instance icon;
		icon.kind = KdHudShader::Arrow;
		icon.center = _pos - dir * (size * kChickIconOffset);
		icon.halfSize = { iconHalf, iconHalf };
		icon.color = face;
		icon.subColor = outline;
		// x: 進行方向が左ならアイコンも左向き / z: 未使用 / w: アイコンモード
		icon.params = { (dir.x < -0.1f) ? -1.0f : 1.0f, 0.0f, _angleRad, 1.0f };
		m_instances.push_back(icon);
	}
}

inline void CombatHud::PushCombo(const Math::Vector2& _screen)
{
	// 途切れた後も、落ちて消えるまでは描き続ける。
	const bool dropping = (m_comboDrop > 0.001f);

	const int c = dropping ? m_dropCount : m_prevCombo;
	if (c < 2) return;

	const float rank = dropping ? m_dropRank
		: (c >= 30) ? 4.0f : (c >= 20) ? 3.0f
		: (c >= 12) ? 2.0f : (c >= 6) ? 1.0f : 0.0f;

	const float remain = dropping ? 0.0f : ComboTracker::Instance().GetRemainRatio();
	const float dmg = dropping ? m_dropDamage : ComboTracker::Instance().GetTotalDamage();

	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Combo;
	inst.center = Place({ _screen.x * m_cfg.combo.posXRatio,
						  _screen.y * m_cfg.combo.posYRatio }, _screen);
	inst.center.x = std::min(inst.center.x,
		_screen.x * (1.0f - m_cfg.safeAreaPercent) - Px(m_cfg.combo.halfW));
	inst.halfSize = { Px(m_cfg.combo.halfW), Px(m_cfg.combo.halfH) };
	inst.color = Tint(m_cfg.color.plume);
	inst.subColor = Tint(m_cfg.color.comboHot);
	inst.params = { static_cast<float>(c), remain, m_comboPop, rank };

	// x=落下量 y=累計ダメージ
	// 落下量は 0(通常)→1(落ち切る)。途切れていなければ常に0。
	const float drop = dropping ? (1.0f - m_comboDrop) : 0.0f;

	// 崩れ始め。猶予が閾値を切ると、切れる前から傾いて震え出す。
	// 切れてから動き出すと、見る前に消えてしまう。
	const float warnLine = m_cfg.fx.comboWarnLine;
	const float crumble = (!dropping && remain < warnLine && remain > 0.0f)
		? (1.0f - remain / warnLine) : (dropping ? 1.0f : 0.0f);

	inst.extra = { drop, std::floor(dmg), crumble, 0.0f };

	m_instances.push_back(inst);
}

inline void CombatHud::PushFocus(const Math::Vector2& _screen, float _counterRatio)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Focus;
	inst.center = { _screen.x * 0.5f, _screen.y * 0.5f };
	inst.halfSize = { _screen.x * 0.62f, _screen.y * 0.62f };
	inst.color = Tint(m_cfg.color.counter);
	inst.subColor = Tint(m_cfg.color.plume);
	inst.params = { m_focus, _counterRatio, m_counterHitFlash, 0.0f };

	m_instances.push_back(inst);
}

inline void CombatHud::PushDamage(const Math::Vector2& _p, const DamagePopup& _d)
{
	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Damage;
	inst.center = _p;
	const int digits = (_d.damage >= 100.0f) ? 3 : (_d.damage >= 10.0f) ? 2 : 1;
	const float w = Px(m_cfg.damage.halfW) * (0.45f + 0.22f * static_cast<float>(digits));

	inst.halfSize = { w, Px(m_cfg.damage.halfH) };
	// 自分が受けたダメージは赤系。与えたものと混同しないようにする。
	inst.color = Tint(_d.self
		? Math::Vector4(1.00f, 0.42f, 0.36f, 1.00f) : m_cfg.color.damage);
	inst.subColor = Tint(_d.self
		? Math::Vector4(1.00f, 0.22f, 0.18f, 1.00f) : m_cfg.color.damageHot);
	inst.params = { std::floor(_d.damage + 0.5f), _d.age, _d.heavy, _d.seed };

	// 与えたダメージは上へ、受けたダメージは下へ動く。向きで区別する。
	inst.extra = { _d.self ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };

	m_instances.push_back(inst);
}

//===================================================
// 撃破数と経過時間
//===================================================
inline void CombatHud::PushScore(const Math::Vector2& _screen, const StageInfo& _st)
{
	if (_st.totalCount <= 0) return;

	const float safeX = _screen.x * m_cfg.safeAreaPercent;
	const float safeY = _screen.y * m_cfg.safeAreaPercent;

	const float w = Px(m_cfg.score.halfW);
	const float h = Px(m_cfg.score.halfH);

	KdHudShader::Instance inst;
	inst.kind = KdHudShader::Score;
	inst.center = Place({ safeX + w + Px(m_cfg.score.marginX),
						  safeY + h + Px(m_cfg.score.marginY) }, _screen);
	inst.halfSize = { w, h };

	// 撃破の瞬間だけ濃く。平常時は沈めて視界を空ける。
	const float alpha = m_cfg.score.idleAlpha
		+ (1.0f - m_cfg.score.idleAlpha) * m_killFlash;

	// 主色=撃破数、副色=総数と時間。役割で色を分けて単調さを避ける。
	inst.color = Tint(m_cfg.color.score);
	inst.subColor = Tint(m_cfg.color.scoreSub);
	inst.color.w *= alpha;
	inst.subColor.w *= alpha;

	const float progress = static_cast<float>(_st.killedCount)
		/ static_cast<float>(std::max(_st.totalCount, 1));

	inst.params = { static_cast<float>(_st.killedCount),
					static_cast<float>(_st.totalCount),
					m_killFlash, std::floor(_st.elapsedSec) };

	// x=秒の小数部(コロンの明滅) y=進捗
	inst.extra = { _st.elapsedSec - std::floor(_st.elapsedSec), progress, 0.0f, 0.0f };

	m_instances.push_back(inst);
}

//===================================================
// 操作ガイド
//===================================================
inline void CombatHud::PushGuide(const Math::Vector2& _screen, const PlayerInfo& _pl)
{
	auto& input = KdInputActionSystem::Instance();
	const auto& bindings = input.GetActionBindings();

	struct Entry { const char* action; int icon; float state; };

	const float shortLine = BreathGauge::kShortCost / BreathGauge::kMax;

	const float fullLine = 1.0f - (BreathGauge::kFullEpsilon / BreathGauge::kMax);

	const float cryState = (_pl.breathRatio >= fullLine) ? 2.0f : 0.0f;

	// 回避は0〜1のクールダウン進捗をそのまま渡す。
	// 「使えない」だけでは、あと何秒待てばいいのか分からない。
	const Entry entries[] = {
		{ "Attack",      KdHudShader::ActAttack, 1.0f },
		{ "HeavyAttack", KdHudShader::ActHeavy,  1.0f },
		{ "Dodge",       KdHudShader::ActDodge,
			std::clamp(_pl.dodgeCooldown, 0.0f, 1.0f) },
		{ "Cry",         KdHudShader::ActCry,    cryState },
	};

	// 1枚の札。アイコンが上、キーが下。
	// アイコンの軌跡や放射が外へ出るので、余白を広めに取る。
	// 詰めるとクアッドの縁で切れる。
	const float cardW = Px(m_cfg.guide.cardW);
	const float cardH = Px(m_cfg.guide.cardH);
	const float gap = Px(m_cfg.guide.gap);
	const int   cols = std::max(m_cfg.guide.cols, 1);

	const int count = std::min(static_cast<int>(std::size(entries)), kGuideRowMax);
	const int rowCount = (count + cols - 1) / cols;

	// 右下に寄せる。安全余白のぶんだけ内側へ入れる。
	const float safeX = _screen.x * m_cfg.safeAreaPercent;
	const float safeY = _screen.y * m_cfg.safeAreaPercent;
	const float right = _screen.x - safeX;
	const float bottom = _screen.y - safeY;

	for (int i = 0; i < count; ++i)
	{
		auto it = bindings.find(entries[i].action);
		if (it == bindings.end()) continue;

		const auto& bind = it->second;

		//------------------------------------------
		// 実際に割り当てられているキーから記号を作る
		//------------------------------------------
		int shape = KdHudShader::BtnKeycap;
		Math::Vector3 codes = { -1.0f, -1.0f, -1.0f };
		Math::Vector4 padCol = m_cfg.color.keycap;

		// 今使っているデバイスの割り当てを優先する。
		// そちらに無ければもう一方へ落とす。
		// パッド専用/キーボード専用のアクションが消えるのを防ぐため。
		bool ok = hud_detail::PickSymbol(bind, m_usingGamepad, shape, codes, padCol);
		if (!ok) ok = hud_detail::PickSymbol(bind, !m_usingGamepad, shape, codes, padCol);

		// どちらにも割り当てが無ければ出さない。空の枠だけ出ても意味がない。
		if (!ok) continue;

		//------------------------------------------
		// 押下の検出
		//------------------------------------------
		if (input.IsPress(entries[i].action)) m_guideRipple[i] = 1.0f;
		const float pressed = input.IsHold(entries[i].action) ? 1.0f : 0.0f;

		//------------------------------------------
		// 2×2に配置。右下を起点に左上へ埋める。
		//------------------------------------------
		const int col = i % cols;
		const int row = i / cols;

		const float x = right - cardW * 0.5f - (cols - 1 - col) * (cardW + gap);
		const float y = bottom - cardH * 0.5f - (rowCount - 1 - row) * (cardH + gap);

		KdHudShader::Instance inst;
		inst.kind = KdHudShader::Guide;
		inst.center = Place({ x, y }, _screen);
		inst.halfSize = { cardW * 0.5f, cardH * 0.5f };
		inst.color = Tint(m_cfg.color.plume);
		inst.subColor = Tint(padCol);
		inst.params = { static_cast<float>(shape), codes.x, codes.y, codes.z };
		// Extra.w は正で押下の波紋、負で殲滅の余韻。
		// 符号で分けることで、チャンネルを増やさずに2つの一発演出を載せる。
		// 殲滅中は押下より優先する。戦闘は終わっているので押下を見せる意味がない。
		const float pulse = (m_victoryDip > 0.001f)
			? -(m_victoryDip * m_cfg.fx.victoryDipAmount)
			: m_guideRipple[i];

		inst.extra = { pressed, static_cast<float>(entries[i].icon),
					   entries[i].state, pulse };

		m_instances.push_back(inst);
	}
}

//===================================================
// プレビュー
//===================================================
inline void CombatHud::DrawDebugImGui()
{
#ifdef KD_ECS_IMGUI
	if (!ImGui::Begin("HUD")) { ImGui::End(); return; }

	ImGui::Checkbox("Preview", &m_preview);
	ImGui::SameLine();
	if (ImGui::Button("Reload Config")) ReloadConfig();

	ImGui::Text("scale : %.3f", m_uiScale);
	ImGui::Text("device: %s", m_usingGamepad ? "Gamepad" : "Keyboard");
	ImGui::Text("draw  : %d instances", static_cast<int>(m_instances.size()));

	if (ImGui::CollapsingHeader("Visible"))
	{
		// 撮影やデバッグで要素を切る。JSONを書き換えなくても試せる。
		ImGui::Checkbox("Health", &m_cfg.visible.health);
		ImGui::SameLine(); ImGui::Checkbox("Guide", &m_cfg.visible.guide);
		ImGui::Checkbox("EnemyBar", &m_cfg.visible.enemyBar);
		ImGui::SameLine(); ImGui::Checkbox("Warn", &m_cfg.visible.warn);
		ImGui::Checkbox("Lock", &m_cfg.visible.lock);
		ImGui::SameLine(); ImGui::Checkbox("Compass", &m_cfg.visible.compass);
		ImGui::Checkbox("Count", &m_cfg.visible.count);
		ImGui::SameLine(); ImGui::Checkbox("Combo", &m_cfg.visible.combo);
		ImGui::Checkbox("Damage", &m_cfg.visible.damage);
		ImGui::SameLine(); ImGui::Checkbox("Focus", &m_cfg.visible.focus);
		ImGui::Checkbox("CounterRing", &m_cfg.visible.counterRing);
		ImGui::SameLine(); ImGui::Checkbox("Wave", &m_cfg.visible.wave);
	}

	if (m_preview)
	{
		ImGui::Separator();
		ImGui::TextUnformatted("Player");
		ImGui::SliderFloat("HP", &m_previewPlayer.healthRatio, 0.0f, 1.0f);
		ImGui::SliderFloat("HP disp", &m_previewPlayer.healthDisplay, 0.0f, 1.0f);
		ImGui::SliderFloat("Shock", &m_previewPlayer.damageShock, 0.0f, 1.0f);
		ImGui::SliderFloat("Breath", &m_previewPlayer.breathRatio, 0.0f, 1.0f);
		ImGui::SliderFloat("Breath disp", &m_previewPlayer.breathDisplay, 0.0f, 1.0f);
		ImGui::SliderFloat("Counter", &m_previewPlayer.counterRatio, 0.0f, 1.0f);
		ImGui::Checkbox("Parry", &m_previewPlayer.parryWindow);
		ImGui::SameLine();
		ImGui::Checkbox("Dodge ready", &m_previewPlayer.dodgeReady);

		ImGui::Separator();
		ImGui::TextUnformatted("Stage");
		ImGui::SliderInt("Alive", &m_previewStage.aliveCount, 0, m_previewStage.totalCount);
		ImGui::SliderInt("Total", &m_previewStage.totalCount, 1, 20);

		// HPの実数値は割合から作る。数値表示の確認用。
		m_previewPlayer.healthValue = m_previewPlayer.healthRatio * 100.0f;
	}

	ImGui::End();
#endif
}

//===================================================
// 本体
//===================================================
inline void CombatHud::Draw()
{
	m_instances.clear();

	CameraInfo cam;

	if (m_preview)
	{
		cam = m_previewCamera;
	}
	else
	{
		if (!m_cameraProvider) return;
		if (!m_cameraProvider(cam)) return;
	}

	// 画面サイズはシェーダ側と共有する
	const Math::Vector2 screen = KdHudShader::GetViewportSize();
	if (screen.x <= 0.0f || screen.y <= 0.0f) return;

	// 基準解像度からの倍率
	m_uiScale = std::min(screen.x / std::max(m_cfg.baseWidth, 1.0f),
		screen.y / std::max(m_cfg.baseHeight, 1.0f));

	// 射影は毎フレーム組み直す
	const Math::Matrix proj = DirectX::XMMatrixPerspectiveFovLH(
		DirectX::XMConvertToRadians(cam.fovDeg),
		screen.x / screen.y, cam.nearZ, cam.farZ);
	const Math::Matrix viewProj = cam.view * proj;

	const Math::Vector2 screenCenter(screen.x * 0.5f, screen.y * 0.5f);

	//------------------------------------------
	// ロックマーカーの半径
	//------------------------------------------
	float lockRadius = 0.0f;

	if (m_lockTargetValid || m_lockOpen > 0.01f)
	{
		const float d = std::max((m_lockTargetPos - cam.position).Length(), 0.01f);

		lockRadius = Px(std::clamp(
			m_cfg.lock.refSize * m_cfg.lock.refDistance / d,
			m_cfg.lock.minRadius, m_cfg.lock.maxRadius)) * m_lockPunch;
	}

	//------------------------------------------
	// 敵
	//------------------------------------------
	struct OffTarget
	{
		float score = 0.0f;
		float dist = 0.0f;
		float angle = 0.0f;
		Math::Vector2 pos;
		bool  carried = false;
	};
	std::vector<OffTarget> offs;

	// 一度ためてから距離順に選び、近すぎるものを間引く
	struct BarCandidate
	{
		Math::Vector2 screen;
		float dist = 0.0f;
		float alpha = 1.0f;
		bool  locked = false;
		EnemyInfo info;
	};
	std::vector<BarCandidate> bars;

	if (m_enemyProvider && !m_preview)
	{
		for (const auto& e : m_enemyProvider())
		{
			const Math::Vector3 anchor = e.position + Math::Vector3(0.0f, e.height, 0.0f);

			Math::Vector4 clip;
			const bool front = hud_detail::ToClip(anchor, viewProj, clip);
			const Math::Vector2 sp = front
				? hud_detail::ToScreen(clip, screen) : Math::Vector2::Zero;
			const bool visible = front && hud_detail::IsOnScreen(sp, screen);

			//------------------------------------------
			// 画面外
			//------------------------------------------
			if (!visible)
			{
				// 背後の対象は投影が反転するので、中心対称へ写す
				Math::Vector2 dirPoint = sp;
				if (!front)
				{
					const Math::Vector2 mirrored = hud_detail::ToScreen(
						Math::Vector4(-clip.x, -clip.y, clip.z, -clip.w), screen);
					dirPoint = screenCenter - (mirrored - screenCenter);
				}

				// 敵のコンパスは出さない(ひよこのインジケータに置き換え済み)。

				// 攻撃予兆は画面端にも出す
				if (m_cfg.visible.warn && e.windup)
				{
					const Math::Vector2 edge =
						hud_detail::ClampToEdge(dirPoint, screen, Px(m_cfg.warn.edgeMargin));
					const Math::Vector2 d = edge - screenCenter;
					PushArrow(edge, std::atan2(d.y, d.x));
				}
				continue;
			}

			//------------------------------------------
			// 画面内
			//------------------------------------------
			const float dist = (e.position - cam.position).Length();
			if (dist > m_cfg.enemyBar.range) continue;

			// 遠くても読めるよう下限を高めに取る
			float alpha = (e.deathFlash > 0.0f) ? 1.0f
				: std::clamp(1.0f - (dist / m_cfg.enemyBar.range) * 0.5f,
					m_cfg.enemyBar.minAlpha, 1.0f);

			// 画面中心から離れた相手は薄める
			if (e.deathFlash <= 0.0f)
			{
				const float half = std::min(screen.x, screen.y) * 0.5f;
				const float off = std::clamp((sp - screenCenter).Length() / half, 0.0f, 1.0f);
				alpha *= std::lerp(1.0f, m_cfg.enemyBar.offCenterFade, off * off);
			}

			// 無傷で体幹も削れていない敵だけ隠す
			const bool untouched = (e.healthRatio >= 1.0f)
				&& (e.poiseRatio <= 0.01f)
				&& !e.frozen
				&& !e.staggered
				&& (e.deathFlash <= 0.0f);
			const bool locked = m_lockTargetValid && (e.id == m_prevLockId);
			if (untouched && !locked) continue;

			// 候補に積む
			BarCandidate c;
			// ロック中はマーカーの上へ逃がす
			float lift = Px(m_cfg.enemyBar.offsetY);

			if (locked)
			{
				const float quadH = Px(m_cfg.enemyBar.halfH)
					* m_cfg.enemyBar.lockedScale
					* KdHudShader::kEnemyBarExpandY;

				lift += lockRadius + quadH + Px(m_cfg.enemyBar.lockedGap);
			}

			// 溜め中と、攻撃が出た直後の閃光
			//if (m_cfg.visible.warn && (e.windup || e.strikeFlash > 0.001f))
			//{
			//	PushWarn(Math::Vector2(sp.x, sp.y - lift - Px(m_cfg.warn.offsetY)),
			//		Px(m_cfg.warn.radius), e.windupProgress, e.strikeFlash,
			//		e.unblockable, e.attackPower);
			//}

			c.screen = Math::Vector2(sp.x, sp.y - lift);
			c.dist = dist;
			c.alpha = alpha;
			c.locked = locked;
			c.info = e;
			bars.push_back(c);
		}
	}

	//------------------------------------------
	// 頭上ゲージの間引き
	//------------------------------------------
	if (m_cfg.visible.enemyBar && !bars.empty())
	{
		std::sort(bars.begin(), bars.end(),
			[](const BarCandidate& _a, const BarCandidate& _b)
			{
				if (_a.locked != _b.locked) return _a.locked;	// ロックを最優先
				return _a.dist < _b.dist;
			});

		const float minSp = Px(m_cfg.enemyBar.minSpacing);
		const float minSp2 = minSp * minSp;

		std::vector<Math::Vector2> placed;
		placed.reserve(bars.size());

		for (const auto& c : bars)
		{
			if (static_cast<int>(placed.size()) >= m_cfg.enemyBar.maxVisible) break;

			// 爆散中は間引かない。倒した手応えを消してしまう。
			if (!c.locked && c.info.deathFlash <= 0.0f)
			{
				bool tooClose = false;
				for (const auto& p : placed)
				{
					const Math::Vector2 d = c.screen - p;
					if (d.LengthSquared() < minSp2) { tooClose = true; break; }
				}
				if (tooClose) continue;
			}

			PushEnemyBar(c.screen, c.info, c.alpha, c.locked);
			placed.push_back(c.screen);
		}
	}

	//------------------------------------------
	// 画面外のひよこのインジケータ
	//------------------------------------------
	if (m_cfg.visible.compass && m_chickProvider && !m_preview)
	{
		for (const auto& c : m_chickProvider())
		{
			const Math::Vector3 anchor = c.position + Math::Vector3(0.0f, c.height, 0.0f);

			Math::Vector4 clip;
			const bool front = hud_detail::ToClip(anchor, viewProj, clip);
			const Math::Vector2 sp = front
				? hud_detail::ToScreen(clip, screen) : Math::Vector2::Zero;

			// 画面内なら、そのひよこの頭上から真下へ矢印を刺す。
			// 消えるのは追従中だけ(提供側で除外済み)。
			if (front && hud_detail::IsOnScreen(sp, screen))
			{
				// 上下にゆっくり揺らして、刺さっているのを目で追えるようにする
				const float bob = std::sin(m_pulse * 2.0f) * Px(kChickMarkerSize * 0.25f);

				OffTarget o;
				o.dist = (c.position - cam.position).Length();
				o.score = o.dist - (c.carried ? 10000.0f : 0.0f);
				o.angle = DirectX::XM_PIDIV2;	// 画面座標はYが下向き。真下を指す。
				o.pos = Math::Vector2(sp.x, sp.y - Px(kChickMarkerSize * 2.2f) + bob);
				o.carried = c.carried;
				offs.push_back(o);
				continue;
			}

			// 画面外は、画面中心を囲む円周上から方向を示す
			Math::Vector2 dirPoint = sp;
			if (!front)
			{
				// 背後は投影が反転するので中心対称へ写す
				const Math::Vector2 mirrored = hud_detail::ToScreen(
					Math::Vector4(-clip.x, -clip.y, clip.z, -clip.w), screen);
				dirPoint = screenCenter - (mirrored - screenCenter);
			}

			Math::Vector2 d = dirPoint - screenCenter;
			if (d.LengthSquared() < 0.0001f) d = { 0.0f, -1.0f };	// 真正面は上向きで代用
			d.Normalize();

			OffTarget o;
			o.dist = (c.position - cam.position).Length();
			// 連れ去り中を最優先。同条件なら近い順。
			o.score = o.dist - (c.carried ? 10000.0f : 0.0f);
			o.angle = std::atan2(d.y, d.x);
			// 画面中心を囲む円周上に置く
			o.pos = screenCenter + d * Px(m_cfg.compass.radius);
			o.carried = c.carried;
			offs.push_back(o);
		}

		std::sort(offs.begin(), offs.end(),
			[](const OffTarget& _a, const OffTarget& _b) { return _a.score < _b.score; });

		const int n = std::min(static_cast<int>(offs.size()), m_cfg.compass.maxMarkers);
		for (int i = 0; i < n; ++i)
			PushChickMarker(offs[i].pos, offs[i].angle, offs[i].carried);
	}

	//------------------------------------------
	// プレイヤー
	//------------------------------------------
	{
		PlayerInfo pl;
		const bool ok = m_preview
			? (pl = m_previewPlayer, true)
			: (m_playerProvider && m_playerProvider(pl));

		if (ok)
		{
			// ロック対象までの距離。マーカーの射程表示に使う。
			m_lockPlayerDist = m_lockTargetValid
				? (m_lockTargetPos - pl.position).Length() : 0.0f;

			// 回復の検出。減る演出だけだと回復が無反応に見える。
			if (pl.healthRatio > m_prevHp + 0.0005f) m_healFlash = 1.0f;

			const float wantDesat = (pl.counterRatio > 0.0f || pl.parryWindow)
				? m_cfg.fx.desatOnCounter : 0.0f;
			m_desat = Damp::Value(m_desat, wantDesat, 10.0f, m_fadeDelta);

			// 死亡。HUD全体が崩れ落ちる。
			const float wantDeath = pl.dead ? 1.0f : 0.0f;
			m_deathT = Damp::Value(m_deathT, wantDeath,
				1.0f / std::max(m_cfg.fx.deathCollapseSec, 0.01f),
				m_fadeDelta);

			if (m_cfg.visible.health) PushHealth(screen, pl);
			if (m_cfg.visible.guide) PushGuide(screen, pl);

			// 鳴き声の波紋
			if (m_cfg.visible.wave && pl.cryWaveRatio > 0.0f)
			{
				Math::Vector4 clip;
				if (hud_detail::ToClip(pl.position + Math::Vector3(0, 2.5f, 0), viewProj, clip))
				{
					const float dist = std::max((pl.position - cam.position).Length(), 1.0f);
					const float r = (pl.cryWasFull ? 26000.0f : 5200.0f) / dist;
					PushWave(hud_detail::ToScreen(clip, screen), r, pl.cryWaveRatio, pl.cryWasFull);
				}
			}

			// ジャスト回避の集中演出。
			// 猶予中と受付中に立ち上げ、途切れたら自然に閉じる。
			//const float want = pl.parryWindow ? 1.0f : 0.0f;
			//m_focus = Damp::Value(m_focus, want, kFocusLambda, m_fadeDelta);

			//if (m_cfg.visible.focus && (m_focus > 0.01f || m_counterHitFlash > 0.01f))
			//	PushFocus(screen, pl.counterRatio);
		}
	}

	//------------------------------------------
	// 戦況
	//------------------------------------------
	StageInfo st;
	const bool ok = m_preview
		? (st = m_previewStage, true)
		: (m_stageProvider && m_stageProvider(st));
	if (ok)
	{
		// 最後の1体を倒した瞬間。操作ガイドを一度沈めて区切りを示す。
		if (m_prevAlive > 0 && st.aliveCount == 0) m_victoryDip = 1.0f;

		if (m_cfg.visible.count) PushCount(screen, st);
		if (m_cfg.visible.score) PushScore(screen, st);
	}

	if (m_cfg.visible.combo) PushCombo(screen);

	//------------------------------------------
	// ロックマーカー
	//------------------------------------------
	if (m_cfg.visible.lock && (m_lockTargetValid || m_lockOpen > 0.01f))
	{
		Math::Vector4 clip;
		if (hud_detail::ToClip(m_lockTargetPos, viewProj, clip))
		{
			// 踏み込みが届くか。届かない相手をロックしていると空振りする。
			const bool inRange = (m_lockPlayerDist <= m_stepRange);

			PushLock(hud_detail::ToScreen(clip, screen), lockRadius, inRange);
		}
	}

	//------------------------------------------
	// ダメージの数値
	//------------------------------------------
	if (m_cfg.visible.damage)
	{
		// コンボ表示の領域。ここへ数字が入ると重なって両方読めない。
		const Math::Vector2 comboC(screen.x * m_cfg.combo.posXRatio,
			screen.y * m_cfg.combo.posYRatio);
		const Math::Vector2 comboHalf(Px(m_cfg.combo.halfW), Px(m_cfg.combo.halfH));

		for (const auto& d : m_damages)
		{
			Math::Vector2 sp;

			if (d.self)
			{
				// 受けたダメージはHPバーの上に出す
				sp = m_healthAnchor;
			}
			else
			{
				Math::Vector4 clip;
				if (!hud_detail::ToClip(d.world, viewProj, clip)) continue;

				sp = hud_detail::ToScreen(clip, screen);
				if (!hud_detail::IsOnScreen(sp, screen)) continue;
			}

			// コンボ表示に重なったら横へ逃がす
			if (!d.self)
			{
				const Math::Vector2 rel = sp - comboC;
				if (std::abs(rel.x) < comboHalf.x * 1.2f &&
					std::abs(rel.y) < comboHalf.y * 1.4f)
				{
					sp.x -= comboHalf.x * 1.3f;
				}
			}

			PushDamage(sp, d);
		}
	}

	m_shader.Draw(m_instances);
}