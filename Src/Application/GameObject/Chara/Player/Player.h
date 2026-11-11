#pragma once
#include "../IChara.h"
#include "../../../GameSystem/Combat/AttackTag.h"
#include "PlayerTuning.h"

class CameraBase;

// 入力リクエスト/状態フラグ用のビット
namespace TestEntityFlag
{
	constexpr uint16_t kDodgeRequested = 1u << 1;
	constexpr uint16_t kAttackRequested = 1u << 2;
	constexpr uint16_t kDashing = 1u << 3;
	constexpr uint16_t kCry = 1u << 4;
	constexpr uint16_t kDead = 1u << 5;	
	constexpr uint16_t kComboQueued = 1u << 6;	
	constexpr uint16_t kCounterHit    = 1u << 7;
	constexpr uint16_t kJumpImpulse = 1u << 8;
	constexpr uint16_t kHardLanding = 1u << 9;
	constexpr uint16_t kHeavyRequested = 1u << 10;
	constexpr uint16_t kHurt = 1u << 11;
}

class Player : public IChara
{
public:

	using IChara::IChara;

	Player() = default;
	~Player() override;

	void Init() override;
	void Update() override;
	void PostUpdate() override;

	void SetCamera(const std::shared_ptr<CameraBase>& _camera) { m_wpCamera = _camera; }

	using EnemyPositionProvider = std::function<std::vector<Math::Vector3>()>;
	void SetEnemyPositionProvider(EnemyPositionProvider _provider) { m_enemyPositionProvider = std::move(_provider); }

	std::string GetID() override { return "Player"; }

	bool IsDead() const;

	// ImGuiデバッグ表示
	void DrawDebugImGui();

	// =======================================================
	//ステート(ラムダ)から ctx.As<Player>() 経由で呼ばれる公開API
	// =======================================================

	// ---- 入力の参照 ----
	Math::Vector2 GetMoveInput() const;
	Math::Vector3 GetCameraForwardFlat() const;
	bool IsMoving() const { return GetMoveInput().LengthSquared() > 0.0f; }
	bool IsDashing() const { return HasFlag(TestEntityFlag::kDashing); }
	bool IsGrounded() const { return m_isGrounded; }

	// Press系リスナーが立てたフラグを消費する
	bool ConsumeDeadRequest();
	bool ConsumeDodgeRequest();
	bool ConsumeAttackRequest();
	bool ConsumeCryRequest();
	bool ConsumeHeavyRequest();

	// ---- 移動 ----
	void ApplyGroundedMotion(float deltaTime, float speed);
	void ApplyDodgeMotion(float deltaTime);
	void StopMotion() { m_move = Math::Vector3::Zero; m_currentSpeed = 0.0f; }
	void SetStepClimb(bool _enable);

	// --- 回避 ---
	bool IsDodgeReady() const { return m_dodgeCooldown <= 0.0f; }
	void BeginDodge(float speed);
	void EndDodge();

	// ---- キャンセル ----
	// 攻撃中、抜けてよい区間で毎フレーム呼ぶ。引数なしは調整値の保持時間
	void OpenCancelWindow() { OpenCancelWindow(m_tuning.cancelHoldTime); }
	void OpenCancelWindow(float _sec) { m_cancelWindow = std::max(m_cancelWindow, _sec); }
	bool IsCancelable() const { return !m_cancelLocked && m_cancelWindow > 0.0f; }

	// ---- ヒット確認 ----
	// 今出している攻撃が当たったか。当たっていればリカバリを短縮する。
	bool DidAttackLand() const { return m_attackLanded; }

	// ---- 被弾のよろけ ---
	bool  ConsumeHurtRequest() { return ConsumeFlag(TestEntityFlag::kHurt); }

	// よろけの強さ
	int   GetHurtLevel() const { return m_hurtLevel; }
	float GetHurtDuration() const;

	// よろけ中の移動
	void  ApplyHurtMotion(float _dt);

	// 起き上がりの姿勢
	void  ApplyHurtPose(float _t01);

	// ---- 着地 ----
	// 落下速度に応じた潰し
	void ApplyLandingPose();
	bool ConsumeHardLanding() { return ConsumeFlag(TestEntityFlag::kHardLanding); }

	// ---- 被弾 ----
	bool IsHitInvincible() const { return m_hitInvincibleTime > 0.0f; }

	// --- 生存 ---
	bool IsActive() const {return m_isActive;}
	void EndActive() { m_isActive = false; }

	// ---- ジャスト回避 ----
	bool IsInvincible()   const { return m_invincibleTime > 0.0f; }
	bool IsCounterReady() const { return m_counterWindow > 0.0f; }
	// スローと視野角を元に戻す
	void EndPerfectDodge();

	// ---- 攻撃 ----
	void BeginAttackStep(bool _useSoftLock, float _keepDistance, float _maxDistance, float _duration);
	// カウンター用
	void BeginCounterStep(float _keepDistance, float _maxDistance, float _duration);

	// 踏み込み中の移動
	bool IsStepping() const { return m_lungeActive; }
	void ApplyStepMotion(float _dt);

	// コンボの先行入力
	void QueueComboInput() { SetFlag(TestEntityFlag::kComboQueued); }
	bool ConsumeComboQueue() { return ConsumeFlag(TestEntityFlag::kComboQueued); }
	void ClearComboQueue() { ClearFlag(TestEntityFlag::kComboQueued); }
	
	// 攻撃の振り
	struct AttackSwing
	{
		const char* asset = "Slash";	
		float lifeTime = 0.22f;
		float widthScale = 1.0f;

		// 上下は「高さ」で持つ
		float topY = 4.0_m;	// 振り始めの高さ
		float bottomY = -1.5_m;	// 振り終わりの高さ

		float yawDeg = 90.0f;	// 横の振れ幅
		float swingSec = 0.14f;	// 振りにかける時間

		float inner = 0.60f;	// 帯の内側
		float outer = 1.45f;	// 帯の外側
	};

	// 攻撃の向き
	Math::Vector3 GetAttackDir() const;

	void BeginAttackTrail(const AttackSwing& _swing);

	// 攻撃判定の提出
	void SpawnAttackHitbox(float _damage = 15.0f, float _reach = 15.0f,
		float _radius = 2.0f, float _knockback = 100.0f,
		int _hitstopFrames = 6, uint32_t _tag = PlayerAttackTag::kNormal,
		float _launch = 1.0f);

	// 自分のカウンターが当たったか(CombatFx経由なので1フレーム遅れ)
	bool ConsumeCounterHit() { return ConsumeFlag(TestEntityFlag::kCounterHit); }

	// 視野角の一時変更。締めの一撃などで画面を寄せる。Push/Popは必ず対で。
	void PushCameraFocus(float _fovDeg, float _lambda = 14.0f);
	void PopCameraFocus();

	// ステートから直接シェイクを要求する
	void AddCameraTrauma(float _amount);

	// 硬化中の敵の座標。強攻撃は固まった相手を優先して狙う。
	using FrozenPositionProvider = std::function<std::vector<Math::Vector3>()>;
	void SetFrozenPositionProvider(FrozenPositionProvider _p) { m_frozenPositionProvider = std::move(_p); }

	// _preferFrozen : true なら硬化中の敵を最優先で狙う
	void BeginHeavyStep(bool _preferFrozen, float _keepDistance, float _maxDistance, float _duration);

	// 強攻撃中は回避で逃げられない
	void LockCancel() { m_cancelLocked = true; }
	void UnlockCancel() { m_cancelLocked = false; }

	bool GetAttackSegment(float _reach, Math::Vector3& _root, Math::Vector3& _tip) const;

	// 攻撃判定と軌跡を出すボーン
	void SetAttackBones(const char* _rootNode, const char* _tipNode = "");

	// ノード名を出力ウィンドウへ吐く
	void DumpNodeNames() const;

	// ---- 鳴き声 ----
	using FreezeFn = std::function<void(const Math::Vector3& _center, float _radius, float _duration, bool _nearestOnly)>;
	void SetFreezeFunction(FreezeFn _fn) { m_freezeFn = std::move(_fn); }
	void SetCry(bool _isCry) { m_isCry = _isCry; }
	bool IsCry() const { return m_isCry; }
	bool CanCryShort() const;
	bool CanCryFull() const;
	void EmitCry(bool _full);
	// 全力鳴きの溜め
	void ApplyCryCharge(float _t01);
	// 鳴き声の波紋の広がり(0で発生直後、1で消滅)。演出用。
	float GetCryWaveRatio() const { return (m_cryWaveTime > 0.0f) ? (1.0f - m_cryWaveTime / m_tuning.cryWaveSec) : 0.0f; }
	bool  IsCryFull() const { return m_cryWasFull; }

	//----------------------------------------------
	// 脅威の通知
	//----------------------------------------------
	using ThreatFn = std::function<bool()>;
	void SetThreatFunction(ThreatFn _fn) { m_threatFn = std::move(_fn); }

	// ---- HUD用 ----
	float GetHealthRatio();
	float GetHealthValue();
	float GetHealthDisplayRatio() const { return std::clamp(m_hpDisplay / m_maxHp, 0.0f, 1.0f); }
	float GetDamageShock() const { return m_damageShock * kHudShockScale; }
	float GetCounterRatio() const { return SafeRatio(m_counterWindow, m_tuning.counterWindow); }
	Math::Vector3 GetPosition() { return m_cm.get<TransformComponent>(m_entity).GetPosition(); }
	float GetDodgeCooldownRatio() const { return 1.0f - SafeRatio(m_dodgeCooldown, m_tuning.dodgeCooldown); }

	// 調整値(ステートのラムダから読むこともある)
	const PlayerTuning& GetTuning() const { return m_tuning; }

private:

	// HUD へ渡す揺れの縮小率(内部の揺れ 0〜1 を HUD では半分の振れ幅で見せる)
	static constexpr float kHudShockScale = 0.5f;

	// _now / _max を 0〜1 に丸める。_max が 0 以下なら 0
	static float SafeRatio(float _now, float _max)
	{
		return (_max > 0.0f) ? std::clamp(_now / _max, 0.0f, 1.0f) : 0.0f;
	}

	void SetKeyAction();
	void UpdateTimers(float deltaTime);
	void ApplyRotation(const Math::Vector3& moveDir, float deltaTime);

	// 入力(2D)をカメラ基準のワールド方向(正規化済み)に変換する
	Math::Vector3 MakeMoveDir(const Math::Vector2& input);

	// モデルワークの取得
	KdModelWork* GetWork() const;

	// ---- 踏み込み ----
	void BeginLunge(const Math::Vector3& _targetPos, float _keepDistance,
		float _maxDistance, float _duration);
	void FaceInstant(const Math::Vector3& _dir);

	// ---- 対象探索 ----
	bool FindNearestEnemy(float _maxDist, Math::Vector3& _out);
	bool FindEnemyAlongDir(const Math::Vector3& _dir, float _maxDist, Math::Vector3& _out);
	bool GetSoftLockPosition(Math::Vector3& _out);

	// ---- ジャスト回避 ----
	void  TriggerPerfectDodge(const Math::Vector3& _knockback);
	float GetCameraFovY();
	void  SetCameraFovY(float _deg, float _lambda);

	// 射程のわずか外にいる相手を引き寄せる。空振りの理不尽さを消す。
	bool SnapToNearMiss(float _reach, float _radius, Math::Vector3& _outTarget);
	void CaptureBaseScale();

	// ---- フラグ操作(TestEntityFlag のビットに対して使う) ----
	bool HasFlag(uint16_t bit) const { return (m_flags & bit) != 0u; }
	void SetFlag(uint16_t bit) { m_flags |= bit; }
	void ClearFlag(uint16_t bit) { m_flags &= static_cast<uint16_t>(~bit); }

	// 立っていれば消費してtrueを返す(読んだら即座に倒す)
	bool ConsumeFlag(uint16_t bit)
	{
		const bool v = HasFlag(bit);
		ClearFlag(bit);
		return v;
	}

	//死亡フラグ専用死亡フラグはずっと立っていてもいい
	bool ConsumeDeadFlag(uint16_t bit) 
	{
		return HasFlag(bit);
	}
	
	ThreatFn m_threatFn;

	PlayerTuning m_tuning;

	KdAnimator m_chickenAnim;
	std::weak_ptr<CameraBase> m_wpCamera;
	EnemyPositionProvider     m_enemyPositionProvider;

	float m_currentSpeed = 0.0f;
	Math::Vector3 m_lastMoveDir = Math::Vector3::Zero;

	// ---- 攻撃 ----
	std::string m_attackRootNode;
	std::string m_attackTipNode;

	// ---- 振り ----
	AttackSwing m_swing;
	float m_swingPhase = 0.0f;	

	// ---- 入力リクエスト/状態フラグ ----
	uint16_t m_flags = 0;

	// ---- 状態に依存しないタイマー ----
	float m_jumpLockTime = 0.0f;

	float m_dodgeCooldown = 0.0f;

	// ---- 接地判定のチャタリング対策 ----
	float m_groundedLostTime = 0.0f;
	bool m_stepClimbEnabled = true;

	// ---- 回避中のパラメータ(BeginDodgeで確定) ----
	Math::Vector3 m_dodgeDir = {};
	float m_dodgeSpeed = 0.0f;
	bool m_cancelLocked = false;

	// ---- ジャスト回避 ----
	float m_invincibleTime = 0.0f;	// ゲーム内時間で減る
	float m_counterWindow = 0.0f;	// 実時間で減る
	bool  m_slowActive = false;
	bool  m_dodgeFovPushed = false;
	Math::Vector3 m_counterDir = Math::Vector3::Zero;
	bool  m_hasCounterDir = false;

	// ---- 視野角の一時変更(入れ子で呼ばれても壊れないよう深さで管理) ----
	int   m_fovDepth = 0;
	float m_savedFovY = 0.0f;	// 一番外側の Push で入る

	// ---- 判定のスイープ用(前フレームの根本位置) ----
	Math::Vector3 m_prevAttackOrigin = Math::Vector3::Zero;
	bool  m_hasPrevAttackOrigin = false;

	// ---- 踏み込み(瞬間接近) ----
	bool  m_lungeActive = false;
	Math::Vector3 m_lungeDir = Math::Vector3::Zero;
	float m_lungeTotal = 0.0f;	// 総移動距離
	float m_lungeElapsed = 0.0f;	// 経過時間
	float m_lungeDuration = 0.0f;
	float m_lungeMoved = 0.0f;

	// ---- 入力の猶予 ----
	float m_coyoteTime = 0.0f;	// 踏み外した直後でも踏み切れる猶予
	float m_cancelWindow = 0.0f;	// 攻撃を回避でキャンセルできる残り時間
	float m_hitInvincibleTime = 0.0f;	// 被弾後の無敵
	bool  m_prevGrounded = false;	// 接地の立ち下がり検出用
	bool  m_attackLanded = false;	// 今の攻撃が当たったか

	// ---- 対象選択の補正 ----
	Math::Vector3 m_lastAimDir = Math::Vector3::Zero;

	//---- 被弾のよろけ ----
	Math::Vector3 m_hurtVelocity = Math::Vector3::Zero;
	int   m_hurtLevel = 0;

	// ---- 着地 ----
	float m_landingSquash = 0.0f;	
	float m_fallSpeed = 0.0f;		
	Math::Vector3 m_baseScale = Math::Vector3::One;
	bool  m_scaleCaptured = false;

	// ---- 鳴き声 ----
	FrozenPositionProvider m_frozenPositionProvider;
	FreezeFn m_freezeFn;

	int m_canCryEffectID = -1;
	float m_cryWaveTime = 0.0f;
	bool  m_isCry = false;
	bool  m_cryWasFull = false;
	bool  m_cryFovPushed = false;
	bool  m_cryChargePushed = false;

	// ---- ダッシュの慣性 ----
	bool  m_prevDashing = false;


	// ---- HP表示 ----
	float m_maxHp = 100.0f;		// 初期値。Initで実値を控える。
	float m_hpDisplay = 100.0f;	// 遅れて追う値(削られた量を見せる)
	float m_damageShock = 0.0f;

};