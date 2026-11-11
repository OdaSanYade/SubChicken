#pragma once
#include "../../IChara.h"
#include "../State/EnemyStateSetup.h"
#include "EnemyTuning.h"
#include <Framework/Direct3D/KdModel.h>

// 入力/状態フラグ用のビット
namespace EnemyFlag
{
	constexpr uint16_t kAlertRequested = 1u << 0;	// 警戒に入りたい(被弾など)
	constexpr uint16_t kLanded = 1u << 1;	// このフレームで着地した
	constexpr uint16_t kReactionRequested = 1u << 2;
	constexpr uint16_t kFreezeRequested = 1u << 3;// 硬化させられた
	constexpr uint16_t kShatterRequested = 1u << 4;
	constexpr uint16_t kShatterBurst = 1u << 5;
	constexpr uint16_t kDeathRequested = 1u << 6;	// HPが0になった(Dieステートへ落とす)
	constexpr uint16_t kCallRequested = 1u << 7;	// 仲間を呼んだ(マネージャが拾って配る)
	constexpr uint16_t kCallReceived = 1u << 8;	// 呼ばれた(捜索へ向かう)
	constexpr uint16_t kRoarRequested = 1u << 9;	// 咆哮の段階が上がった
	constexpr uint16_t kIntroDone = 1u << 10;	// 登場演出が終わった
	constexpr uint16_t kNestHitDone = 1u << 11;	// 巣を1回殴った(重複防止)
	constexpr uint16_t kDeathCounted = 1u << 12;
	constexpr uint16_t kCarryStarted = 1u << 13;
}

struct EnemySpec
{
	//---- 識別 ----
	const char* typeName = "Normal";	// デバッグ表示とエンティティ名に使う

	//---- 見た目 ----
	const char* defaultMaterial = "EnemyDefault";
	const char* modelName = "Enemy";
	float bodyScale = 1.0f;		// 体格
	bool isBoss = false;

	//---- 体力 ----
	float hp = 200.0f;

	//---- コリジョン(カプセル) ----
	float capsuleRadius = 1.2_m;
	float capsuleHeight = 2.6_m;

	//---- 索敵 ----
	float eyeHeight = 0.5f;
	float sightOffset = 2.0f;
	float sightRadius = 14.0f;
	float sightHeight = 6.0f;
	float loseHysteresis = 1.0f;

	//---- 体幹(怯み耐性) ----
	float poiseMax = 60.0f;
	float poiseDecay = 25.0f;
	float knockbackDecay = 6.0f;	// 大きいほど飛ばない

	//---- ステートパラメータの倍率 ----
	float speedScale = 1.0f;	// 移動速度
	float turnScale = 1.0f;	// 旋回速度
	float windupScale = 1.0f;	// 溜めの長さ(長い=読みやすい)
	float intervalScale = 1.0f;	// 攻撃間隔
	float damageScale = 1.0f;	// 攻撃力
	float reachScale = 1.0f;	// 間合い(攻撃判定と攻撃開始距離)
	float knockbackScale = 1.0f;	// 与えるノックバック

	//---- 陣形 ----
	float slotDistanceOffset = 0.0f;	// +で外周寄り、-で前へ出る

	//---- 索敵の共有(仲間を呼ぶ) ----
	bool  canCall = false;
	float callRadius = 35.0_m;	// 声が届く距離
	float callInterval = 8.0f;		// 次に呼べるまでの時間

	//---- 攻撃の役割 ----
	bool neverCommits = false;

	//---- 士気 ----
	float fleeMorale = 0.0f;
	float fleeHealth = 1.0f;	// 自分のHP割合がこれ以下の時だけ折れる

	//---- 攻撃通知 ----
	float       noticeTime = 0.5f;
	const char* noticeMaterial = nullptr;
	const char* noticeEffect = nullptr;

	//---- 断末魔 ----
	bool  deathBurst = false;
	float burstDamage = 22.0f;
	float burstRadius = 5.0_m;
	float burstKnockback = 70.0f;

	//---- ひよこの連れ去り ----
	bool  canCarry = true;

	const char* mouthNode = "Fox_ Head_016";

	float mouthOffset = 1.8_m;
	float mouthHeight = 0.2_m;

	float carrySpeedScale = 1.15f;

	//------------------------------------------------------
	// EnemyTuning.json の "types" で上書きできる項目。
	// 文字列(typeName やモデル名)はコード側に持つ。JSON から書き換えると
	// const char* の指す先を誰が持つかがややこしくなるので対象外にしている
	//------------------------------------------------------
	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("bodyScale", bodyScale);
		v("isBoss", isBoss);
		v("hp", hp);
		v("capsuleRadius", capsuleRadius, Unit::Meter);
		v("capsuleHeight", capsuleHeight, Unit::Meter);
		v("eyeHeight", eyeHeight);
		v("sightOffset", sightOffset);
		v("sightRadius", sightRadius);
		v("sightHeight", sightHeight);
		v("loseHysteresis", loseHysteresis);
		v("poiseMax", poiseMax);
		v("poiseDecay", poiseDecay);
		v("knockbackDecay", knockbackDecay);
		v("speedScale", speedScale);
		v("turnScale", turnScale);
		v("windupScale", windupScale);
		v("intervalScale", intervalScale);
		v("damageScale", damageScale);
		v("reachScale", reachScale);
		v("knockbackScale", knockbackScale);
		v("slotDistanceOffset", slotDistanceOffset, Unit::Meter);
		v("canCall", canCall);
		v("callRadius", callRadius, Unit::Meter);
		v("callInterval", callInterval);
		v("neverCommits", neverCommits);
		v("fleeMorale", fleeMorale);
		v("fleeHealth", fleeHealth);
		v("noticeTime", noticeTime);
		v("deathBurst", deathBurst);
		v("burstDamage", burstDamage);
		v("burstRadius", burstRadius, Unit::Meter);
		v("burstKnockback", burstKnockback);
		v("canCarry", canCarry);
		v("mouthOffset", mouthOffset, Unit::Meter);
		v("mouthHeight", mouthHeight, Unit::Meter);
		v("carrySpeedScale", carrySpeedScale);
	}
};

// 攻撃の役割。マネージャがトークンと一緒に配る。
enum class AttackRole : uint8_t
{
	None,
	Commit,
	Harass,
};

struct AttackSwing
{
	const char* asset = "Slash";	// 使うトレイルアセット
	float lifeTime = 0.22f;	// 帯の残る時間
	float widthScale = 1.0f;

	// 上下は「高さ」で持つ
	float topY = 4.0_m;	// 振り始めの高さ(origin基準)
	float bottomY = -1.5_m;	// 振り終わりの高さ

	float yawDeg = 90.0f;	// 横の振れ幅。判定もこれで回る
	float swingSec = 0.14f;	// 振りにかける時間

	float inner = 0.60f;	// 帯の内側(reach比)
	float outer = 1.45f;	// 帯の外側(reach比)

	float previewReach = 10.0f;
};

class EnemyBase : public IChara
{
public:

	using IChara::IChara;

	EnemyBase() = default;
	~EnemyBase()override;

	void Init() override;
	void Update() override;
	void PostUpdate() override;

	std::string GetID() override { return "Enemy"; }

	// コンポーネントの中身への参照を返すと ECS の再配置で宙に浮くので値で返す
	Math::Vector3 GetPosition() const;
	bool IsActive() const { return !IsRemovable(); }

	KdModelWork* GetWork() const;

	//------------------------------------------
	// 種類ごとの個性
	//------------------------------------------
	const EnemySpec& GetSpec() const { return m_spec; }

	float MoveSpeed(float _base)       const { return _base * m_spec.speedScale; }
	float TurnRate(float _base)        const { return _base * m_spec.turnScale; }
	float WindupTime(float _base)      const { return _base * m_spec.windupScale; }
	float IntervalTime(float _base)    const { return _base * m_spec.intervalScale; }
	float AttackDamage(float _base)    const { return _base * m_spec.damageScale; }
	float AttackReach(float _base)     const { return _base * m_spec.reachScale; }
	float AttackKnockback(float _base) const { return _base * m_spec.knockbackScale; }

	// ImGuiデバッグ表示
	void DrawDebugImGui();

	//------------------------------------------
	// 追跡対象（毎フレーム外部から供給する）
	//------------------------------------------
	void SetTarget(const Math::Vector3& _pos, bool _valid = true)
	{
		m_targetPos = _pos;
		m_hasTarget = _valid;
	}
	void ClearTarget() { m_hasTarget = false; }

	// =======================================================
	// 以下、ステート(ラムダ)から ctx.As<EnemyBase>() 経由で呼ばれる公開API
	// =======================================================

	// ---- 索敵 ----
	bool  IsTargetInSight();			// 追跡範囲(円)の内側か
	bool  CanSee(const Math::Vector3& _pos);
	bool  IsNear(const Math::Vector3& _pos, float _radius) const;
	float GetTargetDistance();			// XZ距離。ターゲット不在なら巨大値
	void  MarkLastKnown() { if (m_hasTarget) m_lastKnownPos = m_targetPos; }
	bool  IsGrounded() const { return m_isGrounded; }
	void  SetTargetAimHeight(float _h) { m_targetAimHeight = _h; }
	bool IsRetargetLocked() const
	{
		auto& sm = m_cm.get<StateMachineComponent>(m_entity);
		return sm.HasTag(EnemyTag::kAttack) || sm.HasTag(EnemyTag::kNotice)
			|| sm.HasTag(EnemyTag::kFlee) || sm.HasTag(EnemyTag::kDamage)
			|| sm.HasTag(EnemyTag::kFrozen) || sm.HasTag(EnemyTag::kDie)
			|| sm.HasTag(EnemyTag::kCarry);
	}

	// ---- 仲間を呼ぶ ----
	bool WantsToCall() const { return m_spec.canCall && m_hasTarget && m_callCooldown <= 0.0f; }
	void BeginCall();
	bool ConsumeCallRequest() { return ConsumeFlag(EnemyFlag::kCallRequested); }
	void ApplyCallPose(float _t01);
	void EndCallPose();

	// 呼ばれる側。既に見えている敵には効かないので、届いたら true。
	bool ReceiveCall(const Math::Vector3& _pos);
	bool ConsumeCallReceived() { return ConsumeFlag(EnemyFlag::kCallReceived); }

	// ---- 捜索(最後に知っている位置へ向かう) ----
	void  FaceLastKnown(float _turnRate);
	float GetLastKnownDistance();

	// ---- 向き ----
	Math::Vector3 GetFacing();

	void SetTurnRate(float _rate) { m_turnRate = _rate; }
	void FaceTarget(float _turnRate);	// 相手の方を向く
	void RefreshWanderDir();			// うろつき方向をランダム抽選＋タイマー再設定
	void BeginLookAround();				// 見失った方向を見回しの基準にする
	void RefreshLookAroundDir();		// 基準方向から左右へ首を振る

	// ---- タイマー(切れたら true を返して自動リセット) ----
	bool TickWanderTimer(float _dt);
	bool TickLookTimer(float _dt);

	// ---- 移動 ----
	// 前進 + 重力 + ノックバック減衰。速度0でも毎フレーム呼ぶこと。
	void ApplyMotion(float _dt, float _speed);
	// 追跡用。旋回の途中でも相手の方向へ直接進むので間合いを詰め損ねない。
	void ApplyChaseMotion(float _dt, float _speed, bool _applyGravity = true);
	void StopMotion() { m_move = Math::Vector3::Zero; }

	Math::Vector3 CalcSightCenter();
	void ApplyRotation(float _dt);
	void MoveAlong(const Math::Vector3& _dir, float _dt, float _speed, bool _applyGravity = true);
	static Math::Vector3 RotateYaw(const Math::Vector3& _dir, float _deg);

	// ---- 攻撃前の予備動作(テレグラフ) ----
	void BeginWindup();
	void ApplyWindupPose(float _t01);
	void EndWindup();

	// ---- 攻撃 ----
	void BeginAttack();
	// 確定済みの攻撃方向を向き続ける(相手を追尾しない)
	void FaceAttackDir(float _turnRate);
	void SpawnAttackHitbox(float _damage, float _reach, float _radius, float _knockback);

	// 溜め中は怯まない。
	void SetSuperArmor(bool _on) { m_superArmor = _on; }
	bool HasSuperArmor() const { return m_superArmor; }

	void SetSweepFlip(bool _flip) { m_sweepFlip = _flip; }
	bool GetSweepFlip() const { return m_sweepFlip; }

	// ---  陣形 / 攻撃トークン ---
	float GetAttackBias() const { return m_attackBias; }
	// 囲む円周上の持ち場を割り当てて散らす。
	void SetApproachPoint(const Math::Vector3& _p) { m_approachPoint = _p; m_hasApproachPoint = true; }
	void ClearApproachPoint() { m_hasApproachPoint = false; }
	float GetSlotDistanceBias() const { return m_slotDistanceBias; }
	float GetApproachDistance();	// 持ち場までのXZ距離

	// 持ち場に着いたか/離れたか
	bool IsAtApproachPoint() const;
	bool IsOffApproachPoint() const;

	// 持ち場へ寄る。向きはターゲットへ向けたままなので横歩きになる。
	void ApplyApproachMotion(float _dt, float _speed);

	// ---- 攻撃トークン ----
	void RequestAttackToken() { m_wantsAttack = true; }	// 攻撃したい意思。毎フレーム立て直す
	bool WantsAttackToken() const { return m_wantsAttack; }
	void ClearAttackWish() { m_wantsAttack = false; }
	void GrantAttackToken(AttackRole _role)
	{
		m_hasAttackToken = true;
		m_attackRole = _role;
	}
	void ReleaseAttackToken();
	bool HasAttackToken() const { return m_hasAttackToken; }
	bool IsTokenCooling() const { return m_tokenCooldown > 0.0f; }
	// 連続で殴りすぎたら他の敵へ順番を譲る
	bool ShouldYieldToken() const { return m_attackChain >= T().squad.maxAttackChain; }

	AttackRole GetAttackRole() const { return m_attackRole; }
	bool IsHarassRole()  const { return m_attackRole == AttackRole::Harass; }
	bool NeverCommits()  const { return m_spec.neverCommits; }

	// ---- 所属 ----
	void SetSquadId(int _id) { m_squadId = _id; }
	int  GetSquadId() const { return m_squadId; }

	// ---- ひよこの連れ去り ----
	bool IsCarrying() const { return m_carryChickId >= 0; }
	int  GetCarryChickId() const { return m_carryChickId; }

	// ---- 走り去り(ひよこを咥えたまま画面外へ) ----
	// 当たり判定を切って決まった方向へ走り、縮みながら消える。始めたら戻らない。
	void  BeginLeave();
	// 縮みを進める。消え切ったら true。
	bool  TickLeave(float _dt);
	bool  IsLeaving()  const { return m_isLeaving; }
	bool  IsVanished() const { return m_isVanished; }
	// 1(元の大きさ)→0(消滅)。咥えられたひよこも同じ割合で縮める。
	float GetLeaveScale() const { return m_leaveScale; }

	// 咥えられる状態か
	bool CanCarry() const
	{
		if (!m_spec.canCarry) return false;
		if (m_isDead || !m_isActive) return false;
		if (IsCarrying()) return false;
		if (IsFrozen() || m_staggerTimer > 0.0f) return false;
		return m_carryCooldown <= 0.0f;
	}

	void BeginCarry(int _chickId);
	// 離す
	void EndCarry();
	bool ConsumeCarryRequest() { return ConsumeFlag(EnemyFlag::kCarryStarted); }

	// 咥えている間、ひよこを置く位置
	Math::Vector3 GetMouthPosition();

	float CarrySpeed(float _base) const { return _base * m_spec.carrySpeedScale; }

	// 士気
	// 群れの残存率
	void  SetSquadMorale(float _m) { m_morale = _m; }
	float GetSquadMorale() const { return m_morale; }
	// 折れたか
	bool  WantsToFlee();
	void  EndFlee() { m_fleeCooldown = T().squad.fleeCooldown; }
	bool  IsFleeCooling() const { return m_fleeCooldown > 0.0f; }
	// 相手に背を向ける
	void  FaceAwayFromTarget(float _turnRate);
	void  FaceDirection(const Math::Vector3& _dir, float _turnRate);

	// ---- 音楽との同期 ----
	bool IsAttackBeatReady() const;

	// ---- 被弾リアクション ----
	int   ConsumeReactionLevel();
	// 硬直を進める
	bool  TickStagger(float _dt);
	float GetStaggerProgress() const;	// 0(開始)→1(終了)
	void  ApplyHitPose();				// 被弾の潰し。硬直の進捗から自動で計算する
	void  EndHitPose();
	void  ResetGravityScale() { m_gravityScale = 1.0f; }
	float GetPoiseRatio() const { return (m_poiseMax > 0.0f) ? (m_poise / m_poiseMax) : 0.0f; }

	// ---- 硬化(鳴き声) ----
	// ダメージではなく行動停止を配る。無防備なのでコンボの起点になる。
	void Freeze(float _duration);
	bool ConsumeFreezeRequest() { return ConsumeFlag(EnemyFlag::kFreezeRequested); }
	bool TickFreeze(float _dt);
	float GetFreezeProgress() const;
	void  ApplyFrozenPose();
	void  EndFrozenPose();
	bool  IsFrozen() const { return m_freezeTimer > 0.0f; }

	// 硬化中に受けた累計ダメージ。周囲へ配る量の原資になる。
	float GetFrozenDamage() const { return m_frozenDamage; }

	float GetBreakRatio() const;

	bool  ConsumeShatterRequest() { return ConsumeFlag(EnemyFlag::kShatterRequested); }
	// 砕けた位置。連鎖の発生源に使う。
	const Math::Vector3& GetShatterOrigin() const { return m_shatterOrigin; }
	// 硬化の解除を遅らせる。全員同時に動き出すと圧が戻りすぎる。
	void AddFreezeDelay(float _sec) { m_freezeTimer += _sec; m_freezeDuration += _sec; }

	// 砕けて吹き飛ぶ
	void BurstFromFreeze(bool _broken = true);

	// 砕けたことをマネージャへ伝える(連鎖の発生源になる)
	bool ConsumeShatterBurst() { return ConsumeFlag(EnemyFlag::kShatterBurst); }
	// 周囲へ配るダメージ。マネージャがこの値を距離減衰させて配る。
	float GetShatterTransfer() const { return m_shatterTransfer; }
	// 連鎖の世代。上限に達したらそこで止める。
	int   GetChainDepth() const { return m_chainDepth; }

	// 連鎖で巻き込まれた。_depth は巻き込んだ側の世代。
	void ChainShatter(const Math::Vector3& _origin, float _damage, int _depth = 0);
	void ComputeShatterTransfer(bool _broken);
	void BlastByShatter(const Math::Vector3& _origin, float _damage, int _depth);

	// ---- 死 ----
	// HPが0になっても即座には消さない。倒れる姿を見せてから消す。
	bool IsDead() const { return m_isDead; }
	// 死亡した瞬間に1度だけ true。どのステートからでも Die へ落とすために使う。
	bool ConsumeDeathRequest() { return ConsumeFlag(EnemyFlag::kDeathRequested); }

	// ---- 死体のなぎ倒し ----
	bool IsCorpseFlying() const;
	const Math::Vector3& GetKnockbackVelocity() const { return m_knockbackVelocity; }
	// 巻き込んだ数を渡して勢いを削る。実際に当たった時だけ呼ぶこと。
	void OnCorpseSlammed(int _count);
	// 死体に轢かれた側。体幹を無視して確実に転ばせる。
	void SlamByCorpse(const Math::Vector3& _origin, const Math::Vector3& _velocity, float _damage);
	// 死亡演出が終わり、配列から取り除いてよいか
	bool IsRemovable() const { return (m_isDead || !m_isActive) && m_corpseTimer <= 0.0f; }
	// 撃破時の追加吹っ飛び倍率。残HPを超えたぶんだけ派手に飛ぶ。
	float GetOverkillScale() const { return m_overkillScale; }
	void  TickCorpse(float _dt)
	{
		if (m_corpseTimer > 0.0f) m_corpseTimer -= _dt;
		if (m_corpseSlamCooldown > 0.0f) m_corpseSlamCooldown -= _dt;
	}
	float GetCorpseProgress() const;	// 0(倒れた直後)→1(消滅)
	void  ApplyCorpsePose();			// 沈み込みと潰れ
	// 死体と走り去り中は狙われない。ソフトロックや踏み込み、鳴き声の対象から外す。
	bool IsTargetable() const { return m_isActive && !m_isDead && !m_isLeaving; }

	// 攻撃態勢
	void ApplyStrikePose(float _t01);

	// ---- 攻撃通知 ----
	float NoticeTime() const { return m_spec.noticeTime; }
	void  BeginAttackNotice();				// 冪等。二重に呼んでも増えない
	void  TickAttackNotice(float _progress01);	// 0→1の進み具合を渡す
	void  EndAttackNotice();
	bool  IsAttackNotice() const { return m_noticeActive; }
	float GetNoticeProgress() const { return m_noticeProgress; }

	// 通知は溜めの末尾を切り出したものなので、姿勢を続けるために総時間を引き継ぐ
	void  SetWindupTotal(float _t) { m_windupTotal = _t; }
	void  ClearWindupTotal() { m_windupTotal = 0.0f; }
	float GetWindupTotal() const { return m_windupTotal; }
	// 通知エフェクトを出す高さ
	Math::Vector3 GetNoticeOrigin();

	// ---- 断末魔 ----
	bool WantsDeathBurst() const { return m_spec.deathBurst; }
	void ApplyFusePose(float _t01);		// 破裂前の膨らみ
	void SpawnDeathBurst();				// 判定を出す。1度だけ呼ぶこと

	// ---- デバッグ表示 ----
	// タグから現在の大分類を引く。ImGuiの一覧用。
	virtual const char* GetStateLabel();
	const char* GetTypeName() const { return m_spec.typeName; }

	// ---- フラグ消費 ----
	bool ConsumeAlertRequest() { return ConsumeFlag(EnemyFlag::kAlertRequested); }
	bool ConsumeLandedFlag() { return ConsumeFlag(EnemyFlag::kLanded); }

	// ---- HUD用 ----
	float GetHealthRatio();
	bool  IsWindingUp() const { return m_windupActive; }
	// 溜めの進捗(0→1)。HUDの収束リングの半径になる。
	float GetWindupProgress() const { return m_windupProgress; }
	// 攻撃が出た瞬間の閃光(1→0)。HUDが光を出すのに使う。
	float GetStrikeFlash() const { return m_strikeFlash; }
	float GetEyeHeight() const { return m_eyeHeight; }
	float GetBreakFlash() const { return m_breakFlash; }
	bool  IsStaggered() const { return m_staggerTimer > 0.0f; }
	float GetDeathFlash() const { return m_deathFlash; }
	float GetStaggerRemainRatio() const
	{
		return (m_staggerDuration > 0.0f)
			? std::clamp(m_staggerTimer / m_staggerDuration, 0.0f, 1.0f) : 0.0f;
	}
	float GetFreezeRemainRatio() const
	{
		return (m_freezeDuration > 0.0f)
			? std::clamp(m_freezeTimer / m_freezeDuration, 0.0f, 1.0f) : 0.0f;
	}
	float GetHealthValue() const;
	float GetHealthMax() const;

	virtual bool IsInvincible() const { return false; }

	bool ConsumeDeathCount();

protected:

	// 全種類で共通の調整値(EnemyTuning.json の "EnemyBase")
	static const EnemyTuning& T() { return EnemyTuning::Shared(); }

	// 種類ごとのパラメータ。派生クラスはこれだけを上書きすればよい。
	// Init() の先頭で呼ばれるので、戻り値がそのまま個体の設定になる。
	virtual EnemySpec MakeSpec() const { return EnemySpec{}; }

	virtual void SetupStates() { SetupEnemyStates(); }

	virtual void SetRenderModel();

	// ステートマシンの定義名。派生で別グラフを使いたい場合は上書きする。
	const char* m_stateSystemName = "Enemy";

	//------------------------------------------
	// 攻撃
	//------------------------------------------
	Math::Vector3 m_attackDir = Math::Vector3::Zero;	// 踏み込み時に確定する攻撃方向
	float         m_attackBias = 0.0f;					// 攻撃間隔の個体差
	bool m_superArmor = false;							// スーパーアーマー
	AttackSwing m_swing;
	float m_swingPhase = 0.0f;		// 0→1。判定が出ている間に進む
	float m_previewPhase = 1.0f;	// 予告の進み具合。巻き戻ったら描き直す
	float m_previewReach = 10.0f;	// 予告で描く距離(攻撃ごとに AttackSwing から入る)

	//------------------------------------------
	// 索敵パラメータ（MakeSpec() から Init() で流し込まれる）
	//------------------------------------------
	float m_eyeHeight = 0.5f;	// 目の高さ
	float m_sightOffset = 2.0f;	// 視線方向へ円中心をずらす量
	float m_sightRadius = 5.0f;	// 追跡範囲(円の半径)
	float m_sightHeight = 6.0f;	// 高さ方向の許容差
	float m_loseHysteresis = 1.0f;	// 追跡中だけ半径を広げる量

	//------------------------------------------
	// 体幹（怯み耐性）
	//------------------------------------------
	float m_poiseMax = 60.0f;	// これを超えると大きく崩れる
	float m_poiseDecay = 25.0f;	// 毎秒の回復量

	//------------------------------------------
	// 移動制御
	//------------------------------------------
	Math::Vector3 m_rotateDir = Math::Vector3::Zero;	// 向きたい方向
	float         m_turnRate = 3.0f;

	void ApplySpec();

	bool HasFlag(uint16_t bit) const { return (m_flags & bit) != 0u; }
	void SetFlag(uint16_t bit) { m_flags |= bit; }
	void ClearFlag(uint16_t bit) { m_flags &= static_cast<uint16_t>(~bit); }
	bool ConsumeFlag(uint16_t bit)
	{
		const bool v = HasFlag(bit);
		ClearFlag(bit);
		return v;
	}

	void CaptureBaseScale();
	void RequestReaction(int _level, float _duration);

	// 体幹を崩されたら咥えていたひよこを離す。
	void ReleaseCarryByBreak();

	uint16_t m_flags = 0;

	//------------------------------------------
	// ターゲット
	//------------------------------------------
	float m_targetAimHeight = -1.0f;
	Math::Vector3 m_targetPos = Math::Vector3::Zero;
	Math::Vector3 m_lastKnownPos = Math::Vector3::Zero;	// 最後に見た位置
	Math::Vector3 m_lostDir = Math::Vector3::Zero;	// 見失った方向(見回しの基準)
	bool          m_hasTarget = false;


	//------------------------------------------
	// 死
	//------------------------------------------
	void Die(const Math::Vector3& _knockback, float _excess);
	bool  m_isDead = false;
	float m_corpseTimer = 0.0f;
	float m_overkillScale = 1.0f;
	Math::Vector3 m_deathPosition = Math::Vector3::Zero;

	float m_maxHp = 1.0f;

	EnemySpec m_spec{};

private:

	KdAnimator m_enemyAnim;

	//------------------------------------------
	// ひよこの連れ去り
	// (前は public に置かれていたので、外から直接書き換えられた)
	//------------------------------------------
	int   m_carryChickId = -1;
	float m_carryCooldown = 0.0f;
	bool  m_mouthNodeMissing = false;

	//------------------------------------------
	// アニメーション
	//------------------------------------------
	// 現在のステートが要求するクリップを引く
	std::shared_ptr<KdAnimationData> ResolveAnimation(const char* _name) const;

	// ステートのアニメを1フレーム進める
	void AdvanceAnim();

	// モデルの正面の向き
	static constexpr float kFacingSign = -1.0f;
	float         m_wanderTimer = 0.0f;
	float         m_lookTimer = 0.0f;

	//------------------------------------------
	// 仲間を呼ぶ
	//------------------------------------------
	float m_callCooldown = 0.0f;

	//------------------------------------------
	// 攻撃前の予備動作
	//------------------------------------------
	Math::Vector3 m_baseScale = Math::Vector3::One;
	bool  m_scaleCaptured = false;
	bool  m_windupActive = false;
	bool m_sweepFlip = false;

	//------------------------------------------
	// 陣形 / 攻撃トークン
	//------------------------------------------
	Math::Vector3 m_approachPoint = Math::Vector3::Zero;
	bool  m_hasApproachPoint = false;
	float m_slotDistanceBias = 0.0f;	// 個体ごとの間合いのばらつき

	bool  m_wantsAttack = false;
	bool  m_hasAttackToken = false;
	AttackRole m_attackRole = AttackRole::None;
	float m_tokenCooldown = 0.0f;
	int   m_attackChain = 0;			// 連続で殴った回数

	//------------------------------------------
	// 攻撃通知
	//------------------------------------------
	bool  m_noticeActive = false;
	float m_noticeProgress = 0.0f;
	int   m_noticeEffectId = -1;		// 硬化の常時エフェクトとは別枠で持つ
	float m_windupTotal = 0.0f;

	//------------------------------------------
	// 走り去り(ひよこを咥えたまま去る)
	//------------------------------------------
	bool  m_isLeaving = false;
	bool  m_isVanished = false;
	float m_leaveTimer = 0.0f;
	float m_leaveScale = 1.0f;

	static constexpr float kLeaveMinScale = 0.001f;	// 0にすると行列が潰れるので下限を置く

	//------------------------------------------
	// 士気
	//------------------------------------------
	int   m_squadId = -1;			// 所属部隊。-1は無所属
	float m_morale = 1.0f;			// 群れの残存率(マネージャが配る)
	float m_fleeCooldown = 0.0f;		// 逃げ切った後、戦い直すまでの時間




	//------------------------------------------
	// 被弾リアクション
	//------------------------------------------
	float   m_poise = 0.0f;
	uint8_t m_reactionLevel = 0;
	float   m_staggerTimer = 0.0f;
	float   m_staggerDuration = 0.0f;
	float   m_gravityScale = 1.0f;	// 打ち上げ中は落下を緩くする


	//------------------------------------------
	// ノックバック
	//------------------------------------------
	Math::Vector3 m_knockbackVelocity = Math::Vector3::Zero;	// 減衰していく初速
	float m_knockbackDecay = 6.0f;	// 大きいほど早く減衰
	bool  m_isKnockback = false;
	float m_knockbackTimer = 0.0f;


	bool m_wasGroundedLastFrame = true;

	//------------------------------------------
	// 硬化
	//------------------------------------------
	float m_freezeTimer = 0.0f;
	float m_freezeDuration = 0.0f;
	float m_frozenDamage = 0.0f;			// 硬化中に受けた累計
	Math::Vector3 m_shatterOrigin = Math::Vector3::Zero;

	// ---- ブレイク ----
	float m_breakGauge = 0.0f;
	float m_shatterTransfer = 0.0f;		// 砕けた時に周囲へ配るダメージ
	int   m_chainDepth = 0;				// 連鎖の世代
	bool  m_shatterFromChain = false;	// 巻き込まれて砕けたか(配る量の計算が変わる)



	//　---- 死体　----

	//------------------------------------------
	// 死体のなぎ倒し
	//------------------------------------------
	float m_corpseSlamCooldown = 0.0f;	// 連続で巻き込まないための間隔


	// ---- HUD用 ----
	float m_windupProgress = 0.0f;
	float m_strikeFlash = 0.0f;

	float m_breakFlash = 0.0f;

	float m_deathFlash = 0.0f;
};