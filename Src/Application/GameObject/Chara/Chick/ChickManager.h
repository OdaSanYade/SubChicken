#pragma once
#include "../../IObject.h"
#include "Chick.h"	// ChickTuning

class IChara;

// 出現数・巣・候補地点(ChickTuning.json の "ChickManager" 区画)
struct ChickManagerTuning
{
	int   chickCount = 5;			// 出現数(候補地点の数を超えたら候補の数に丸める)
	float nestRadius = 30.0_m;		// ここに入れば「連れ帰った」
	float slotRadius = 5.0_m;		// 巣の中で並ぶ円の半径

	// 地面を拾うレイ。十分上から撃つので、埋まっていても拾える
	float groundRayUp = 30.0_m;
	float groundRayLength = 100.0_m;

	// 候補地点。ここから重複なく chickCount 箇所を選ぶ。1箇所につき1体まで
	std::vector<Math::Vector3> spawnPoints = {
		{ 8.0f, 0.0f, -115.0f },
		{ 10.0f, 0.0f, -114.0f },
		{ 6.0f, 0.0f, -114.0f },
		{ 10.2f, 0.0f, -113.5f },
		{ 9.2f, 0.0f, -113.5f },
	};

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("chickCount", chickCount);
		v("nestRadius", nestRadius, Unit::Meter);
		v("slotRadius", slotRadius, Unit::Meter);
		v("groundRayUp", groundRayUp, Unit::Meter);
		v("groundRayLength", groundRayLength, Unit::Meter);
		v("spawnPoints", spawnPoints);
	}
};

class ChickManager : public IObject
{
public:

	using IObject::IObject;

	ChickManager() = default;
	~ChickManager() = default;

	void Init() override;
	void Update() override;
	void PostUpdate() override;

	std::string GetID() override { return "ChickManager"; }

	void SetTarget(const kd::ecs::Entity& _target) { m_targetEntity = _target; }

	// 巣の中心。プレイヤーのスポーン位置を渡す。
	// 渡さなかった場合は最初のフレームのプレイヤー位置を巣とみなす。
	void SetNestPosition(const Math::Vector3& _pos)
	{
		m_nestPos = _pos;
		m_hasNest = true;
	}
	const Math::Vector3& GetNestPosition() const { return m_nestPos; }

	// 開始時の配置。候補地点から重複なく chickCount 体を選ぶ。
	void SpawnAll();

	// 隠れたままのひよこを敵に見えるようにする。
	// 開始地点の猶予が切れた時に呼ぶ。置き去りにした子が狙われるようになる。
	// 返り値は新たに見えるようにした数。
	int RevealIdleChicks();

	// 敵の標的にできるひよこの座標
	std::vector<std::pair<int, Math::Vector3>> GetEnemyTargets() const;

	// HUDの画面外インジケータ用。
	// 追従中の子と、走り去られている子は含めない。連れ去り中は carried=true。
	struct IndicatorTarget
	{
		int id = 0;
		Math::Vector3 position = Math::Vector3::Zero;
		bool carried = false;
	};
	std::vector<IndicatorTarget> GetIndicatorTargets() const;
	int GetIndicatorTargetCount() const { return GetTotalCount() - static_cast<int>(GetIndicatorTargets().size()); }

	// HUD用
	// 生き残っている数。運び去られて消えた子は配列から外れるので含まない。
	int  GetTotalCount() const { return static_cast<int>(m_spChicks.size()); }
	int  GetAliveCount() const;
	int  GetRescuedCount() const;	// 生きていて巣に収まった数
	// 生き残っている子が全員巣に着いたか
	bool IsAllRescued() const
	{
		return !m_spChicks.empty() && GetRescuedCount() >= GetTotalCount();
	}

	// 運び去られて消えた数
	int  GetLostCount() const { return m_lostCount; }
	// 1体も残っていない。クリアできなくなったので負け扱いにする。
	bool IsAllLost() const { return m_spawnedCount > 0 && m_spChicks.empty(); }

	bool GetLastDeathPosition(Math::Vector3& _out) const
	{
		if (!m_hasLastDeath) return false;
		_out = m_lastDeathPos;
		return true;
	}

	//============================================================
	// 連れ去り
	//============================================================
	// 運び手の状態
	struct CarrierPose
	{
		Math::Vector3 mouth = Math::Vector3::Zero;
		Math::Vector3 foot = Math::Vector3::Zero;
		float scale = 1.0f;		// 走り去りの縮み(1→0)
		bool  leaving = false;	// 走り去り中
		bool  gone = false;		// 咥えたまま消えた
	};

	// 運び手の指名を頼む
	using RequestCarrierFn = std::function<int(const Math::Vector3&, int)>;
	// まだ咥えていれば true。false で gone なら運び去られて消えた
	using CarrierPoseFn = std::function<bool(int, int, CarrierPose&)>;

	void SetCarrierHooks(RequestCarrierFn _request, CarrierPoseFn _pose)
	{
		m_requestCarrier = std::move(_request);
		m_carrierPose = std::move(_pose);
	}

	// 倒れている/運ばれている数
	int GetDownedCount() const;
	int GetCarriedCount() const;

	// デバッグビューア
	void DrawDebugImGui();

private:

	// 巣に入ったひよこへ持ち場を配る。一度配った枠は取り上げない。
	void UpdateNestSlots();
	// 巣の円周上に持ち場を作る。体数ぶんだけ均等に割る。
	Math::Vector3 MakeSlotPosition(int _index) const;
	// 地面へ落とす。斜面で浮かないよう、置く位置ごとに取り直す。
	float SampleGroundY(const Math::Vector3& _pos, float _fallbackY) const;

	kd::ecs::Entity m_targetEntity;
	std::vector<std::shared_ptr<Chick>> m_spChicks;

	// Update で拾ったターゲット情報。他のパスでも使う。
	Math::Vector3 m_lastTargetPos = Math::Vector3::Zero;
	bool          m_lastHasTarget = false;

	//　最後の死亡位置
	Math::Vector3 m_lastDeathPos = Math::Vector3::Zero;
	bool          m_hasLastDeath = false;

	//------------------------------------------
	// 連れ去り
	//------------------------------------------
	void UpdateCarry();

	RequestCarrierFn m_requestCarrier;
	CarrierPoseFn    m_carrierPose;

	int m_spawnedCount = 0;
	int m_lostCount = 0;

	//------------------------------------------
	// 巣
	//------------------------------------------
	Math::Vector3 m_nestPos = Math::Vector3::Zero;
	bool          m_hasNest = false;

	// どの持ち場が埋まっているか。index は MakeSlotPosition と対応する。
	std::vector<bool> m_slotUsed;

	//------------------------------------------
	// 調整値(Init で読む)
	//------------------------------------------
	ChickManagerTuning m_tuning;
	ChickTuning        m_chickTuning;	// 生成する全員に配る

	// 実際に出す数。候補地点より多くは置けないので丸めた値
	int SpawnCount() const;
};