#pragma once
#include "../IChara.h"
#include "../../Config/Tuning.h"
#include "State/ChickStateSetup.h"

// ひよこ関連(Chick / ChickManager / ステート)の調整値ファイル
namespace ChickTuningKey
{
	inline constexpr const char* kFile = "ChickTuning";
	inline constexpr const char* kStatesFile = "ChickStates";
}

// ひよこ1羽の調整値(ChickTuning.json の "Chick" 区画)
struct ChickTuning
{
	//---- 見た目 ----
	std::string modelName = "Chick";
	std::string materialName = "ChickDefault";
	float bodyScale = 0.3f;

	//---- コリジョン ----
	float capsuleRadius = 0.8_m;
	float capsuleHeight = 1.4_m;
	float inverseMass = 2.0f;		// 押し合いの軽さ(大きいほど押されやすい)

	//---- 移動 ----
	float brakeDistance = 2.5_m;	// この距離から減速を始める
	float turnRate = 8.0f;			// ステートから旋回の速さが渡されるまでの初期値
	float groundStick = 0.1f;		// 接地中に地面へ張り付かせる最小限の下向きの力

	//---- 体力 / ダウン ----
	float maxHp = 80.0f;
	float downSec = 8.0f;			// 倒れてから自分で起き上がるまで
	float hurtSec = 0.5f;			// ダメージを受けてからの無敵時間

	//---- ライト(足元をほのかに照らす。色はステートが変える) ----
	float lightRange = 1.0f;
	float lightPower = 0.01f;
	Math::Vector3 lightOffset = { 0.0f, 0.5f, 0.0f };	// 足元に近い位置に光源を置く

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("modelName", modelName);
		v("materialName", materialName);
		v("bodyScale", bodyScale);
		v("capsuleRadius", capsuleRadius, Unit::Meter);
		v("capsuleHeight", capsuleHeight, Unit::Meter);
		v("inverseMass", inverseMass);
		v("brakeDistance", brakeDistance, Unit::Meter);
		v("turnRate", turnRate);
		v("groundStick", groundStick);
		v("maxHp", maxHp);
		v("downSec", downSec);
		v("hurtSec", hurtSec);
		v("lightRange", lightRange);
		v("lightPower", lightPower);
		v("lightOffset", lightOffset);
	}
};

// 状態フラグ用のビット。ステートが消費して遷移の合図にする。
namespace ChickFlag
{
	constexpr uint16_t kDownRequested  = 1u << 0;	// HPが0になった
	constexpr uint16_t kCarryRequested = 1u << 1;	// 咥えられた
	constexpr uint16_t kHurt           = 1u << 2;	// 攻撃を受けた
}

class Chick : public IChara
{
public:

	using IChara::IChara;

	Chick() = default;
	~Chick() override = default;

	void Init() override;
	void Update() override;
	void PostUpdate() override;

	std::string GetID() override { return "Chick"; }

	// コンポーネントの中身への参照を返すと ECS の再配置で宙に浮くので値で返す
	Math::Vector3 GetPosition() const;

	// 調整値を渡す。Init より前に呼ぶ(マネージャが全員ぶんまとめて読んで渡す)
	void SetTuning(const ChickTuning& _tuning) { m_tuning = _tuning; m_turnRate = _tuning.turnRate; }
	KdModelWork* GetWork() const;
	void AdvanceAnim();

	//------------------------------------------
	// マネージャから配る情報
	//------------------------------------------
	void SetTarget(const Math::Vector3& _pos, bool _valid = true)
	{
		m_targetPos = _pos;
		m_hasTarget = _valid;
	}
	void ClearTarget() { m_hasTarget = false; }
	bool HasTarget() const { return m_hasTarget; }

	// 隠れているか
	void StopHide() { m_isHide = false; }
	bool IsHide() const { return m_isHide; }

	// 生きているか。運び去られて消えた子だけが false になる。
	bool IsAlive() const { return m_isActive; }

	//------------------------------------------
	// ダウン / 連れ去り
	//------------------------------------------
	bool IsDowned()  const { return m_isDowned; }
	bool IsCarried() const { return m_carrierId >= 0; }
	int  GetCarrierId() const { return m_carrierId; }

	// 自分の足で動ける状態か。追従も位置取りもこれが前提。
	bool IsActing() const { return m_isActive && !m_isDowned && !IsCarried(); }

	// 咥えられた。_carrierId は運び手のエンティティ番号。
	void BeginCarried(int _carrierId);
	// 咥えられている間の位置。マネージャが運び手の口元を毎フレーム渡す。
	void SetCarriedPosition(const Math::Vector3& _pos);
	// 落とされた/振りほどいた。指定地点にHP満タンで立ち上がる。
	void EndCarried(const Math::Vector3& _pos);

	// 運び手が走り去っている間の縮み。_scale は 1→0。
	void SetCarriedScale(float _scale, bool _leaving);
	bool IsCarryLeaving() const { return m_carryLeaving; }

	// 運び去られて消えた。以後は死んだ扱いで、マネージャが片付ける。
	void Vanish();
	bool IsVanished() const { return m_isVanished; }

	// HPを戻して立ち上がる。判定と押し返しもここで復帰させる。
	void Revive();

	//ダメージ受けから復帰
	void RecoverHurt();

	// 倒れている時間を進める。切れたら true。
	bool IsDown() { return m_isDowned; }
	bool TickDown(float _dt);
	float GetDownRatio() const;

	bool TickHurt(float _dt);

	bool ConsumeDownRequest()  { return ConsumeFlag(ChickFlag::kDownRequested); }
	bool ConsumeCarryRequest() { return ConsumeFlag(ChickFlag::kCarryRequested); }
	bool ConsumeHurt()         { return ConsumeFlag(ChickFlag::kHurt); }

	// 敵の標的になるか。倒れている/運ばれている個体はもう狙わない。
	bool IsEnemyTarget() const
	{
		return IsActing() && !IsHide() && !IsFollowing();
	}

	// 元の位置
	void SetHomePosition(const Math::Vector3& _p) { m_homePos = _p; }
	const Math::Vector3& GetHomePosition() const { return m_homePos; }

	// 巣の中の持ち場
	void SetSlot(const Math::Vector3& _p) { m_slotPos = _p; m_hasSlot = true; }
	void ClearSlot() { m_hasSlot = false; }
	bool HasSlot() const { return m_hasSlot; }
	const Math::Vector3& GetSlotPosition() const { return m_slotPos; }
	float GetSlotDistance() const;

	//------------------------------------------
	// 距離
	//------------------------------------------
	float GetTargetDistance() const;	// プレイヤーまでの距離
	float GetHomeDistance() const;		// 元の位置までの距離
	bool  IsPlayerNear(float _radius) const
	{
		return m_hasTarget && GetTargetDistance() <= _radius;
	}

	//------------------------------------------
	// 連れ戻し
	//------------------------------------------
	bool IsRescued() const { return m_isRescued; }
	void SetRescued(bool _on) { m_isRescued = _on; }

	// 追従中か
	bool IsFollowing() const
	{
		if (!m_cm.has<StateMachineComponent>(m_entity)) return false;
		return m_cm.get<StateMachineComponent>(m_entity).HasTag(ChickTag::kFollow);
	}

	//------------------------------------------
	// 移動(ステートから毎フレーム呼ぶ)
	//------------------------------------------
	// 指定地点へ向かう
	void MoveTo(const Math::Vector3& _dest, float _dt, float _speed, float _turnRate);
	// プレイヤーへ向かう
	void FollowTarget(float _dt, float _speed, float _turnRate, float _keepDistance);
	// 向いている方向へ進む
	void ApplyMotion(float _dt, float _speed);
	void StopMotion() { m_move = Math::Vector3::Zero; }

	Math::Vector3 GetFacing() const;

	//------------------------------------------
	// デバッグ表示
	//------------------------------------------
	const char* GetStateLabel() const;
	void DrawDebugImGui();

	//------------------------------------------
	// HUD用
	//------------------------------------------
	float GetHealthRatio() const;

private:

	void ApplyRotation(float _dt);
	void MoveAlong(const Math::Vector3& _dir, float _dt, float _speed);
	float BrakeRatio(float _remain) const;

	// HPが0になった時の入口。倒れるだけで消えない。
	void BeginDown();
	// ダメージ
	void BeginHurt();

	// 判定と押し返しの付け外し。倒れている間は的にも壁にもしない。
	void SetVulnerable(bool _on);

	bool HasFlag(uint16_t _bit) const { return (m_flags & _bit) != 0u; }
	void SetFlag(uint16_t _bit) { m_flags |= _bit; }
	void ClearFlag(uint16_t _bit) { m_flags &= static_cast<uint16_t>(~_bit); }
	bool ConsumeFlag(uint16_t _bit)
	{
		const bool v = HasFlag(_bit);
		ClearFlag(_bit);
		return v;
	}

	KdAnimator m_anim;

	// ステートマシンの定義名
	static constexpr const char* kStateSystemName = "Chick";

	// モデルの正面の向き(モデルが -Z 向きに作られているので反転する)。データではなく作りの都合
	static constexpr float kFacingSign = -1.0f;

	// 縮める下限。0 にすると行列が潰れて逆行列が取れなくなる
	static constexpr float kMinScale = 0.001f;

	// これより短い距離は「着いた」とみなす(0 割り防止も兼ねる)
	static constexpr float kArriveEpsilon = 0.0001f;

	ChickTuning m_tuning;

	//------------------------------------------
	// ダウン / 連れ去り
	//------------------------------------------
	bool  m_isDowned = false;
	int   m_carrierId = -1;		// 運び手のエンティティ番号
	float m_downTimer = 0.0f;

	//ダメージタイマー
	bool  m_isHurt = false;
	float m_hurtTimer = 0.0f;

	Math::Vector3 m_baseScale = Math::Vector3::One;	// 縮みの基準
	bool  m_carryLeaving = false;
	bool  m_isVanished = false;

	uint16_t m_flags = 0;

	//------------------------------------------
	// 目標
	//------------------------------------------
	Math::Vector3 m_targetPos = Math::Vector3::Zero;
	bool          m_hasTarget = false;

	Math::Vector3 m_homePos = Math::Vector3::Zero;
	Math::Vector3 m_slotPos = Math::Vector3::Zero;
	bool          m_hasSlot = false;
	bool          m_isRescued = false;
	bool          m_isHide = true;

	//------------------------------------------
	// 移動制御
	//------------------------------------------
	Math::Vector3 m_rotateDir = Math::Vector3::Zero;
	float         m_turnRate = 0.0f;	
};