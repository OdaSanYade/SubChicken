#pragma once
#include "../../../IObject.h"
#include "EnemyTuning.h"

// 部隊・陣形・連鎖などの調整値(EnemyTuning.json の "EnemyManager" 区画)
struct EnemyManagerTuning
{
	//---- 標的 ----
	float retargetCooldown = 1.0f;		// 標的を切り替えてから、次に切り替えられるまで
	float chickNoticeRadius = 20.0_m;	// ひよこに気付く距離
	float chickRingRadius = 5.0_m;		// ひよこを囲む円の半径
	float chickAimHeight = 0.9_m;
	float commitStandOff = 3.0_m;
	float carryPickRadius = 30.0_m;		// 倒れたひよこを咥えに行く距離

	//---- 部隊 ----
	int   maxSquads = 6;				// 自動増援の上限
	float squadSpread = 8.0_m;			// 部隊内の散らばり(密集させて連鎖の的にする)
	float squadSeparation = 16.0_m;		// 部隊同士とプレイヤーから離す距離
	int   anchorTries = 24;				// 展開地点の試行回数
	float spawnRadiusMin = 20.0_m;		// 湧く円の内側
	float spawnRadiusMax = 70.0_m;		// 湧く円の外側
	float groundRayUp = 30.0_m;			// 地面を拾うレイ
	float groundRayLength = 100.0_m;

	//---- 同時に攻撃できる数 ----
	// 本命を1体に絞ることで、避けるべき攻撃が常に1つになる
	int maxCommit = 1;					// 溜めてから踏み込む本命
	int maxHarass = 2;					// 溜め無しで小突く牽制

	//---- 囲み ----
	int   slotCount = 8;				// 囲みの分割数
	float ringRadius = 10.0_m;			// 待機する円の半径
	float outerOffset = 3.5_m;			// スロットが埋まった敵が下がる距離

	//---- 硬化の連鎖 ----
	float shatterChainRadius = 9.0_m;	// 連鎖が届く最大距離
	float shatterChainHeight = 4.0_m;	// 高さ方向の許容差
	float shatterFullTransfer = 140.0f;	// この配布量で範囲が最大になる
	float shatterRadiusMin = 0.55f;		// 配布量が少ない時の半径倍率
	float shatterFalloffMin = 0.35f;	// 円の縁でのダメージ倍率
	float shatterHitStopBase = 0.10f;	// 連鎖のヒットストップ = base + 巻き込んだ数 × perHit
	float shatterHitStopPerHit = 0.02f;
	float shatterMaxHitStop = 0.22f;	// 連鎖が重なった時の停止上限
	float shatterTraumaBase = 0.3f;
	float shatterTraumaPerHit = 0.08f;
	int   chainSlowCount = 3;			// これだけ巻き込んだら減速を入れる
	float chainSlowScale = 0.35f;		// 減速の強さ
	float chainSlowSec = 0.45f;			// 続く時間

	//---- 死体のなぎ倒し ----
	float corpseSlamRadius = 3.5_m;		// 巻き込む距離(体格で伸びる)
	float corpseSlamHeight = 3.0_m;		// 高さ方向の許容差
	float corpseSlamDamage = 35.0f;		// 轢かれた側のダメージ
	float corpseSlamCone = -0.2f;		// 進行方向の判定角(-1で全方向)
	float corpseSlamHitStopBase = 0.06f;
	float corpseSlamHitStopPerHit = 0.02f;
	float corpseSlamTraumaBase = 0.25f;
	float corpseSlamTraumaPerHit = 0.06f;

	//---- 呼び声 ----
	float callTraumaBase = 0.10f;		// 呼び声で動いた数に応じた揺れ
	float callTraumaPerHit = 0.04f;
	float callTraumaMax = 0.45f;

	//---- ラストキル ----
	float lastKillHitStop = 0.55f;		// 最後の1体を倒した時の停止
	float lastKillTrauma = 0.9f;
	Math::Vector3 lastKillVibration = { 1.0f, 0.9f, 0.45f };	// 左, 右, 秒
	std::string lastKillEffect = "Destroy";

	//---- 襲撃(開始地点に置き去りのひよこを狙う) ----
	int   raidSquadCount = 1;			// 送り込む部隊の数
	float raidRecallSec = 2.5f;			// 呼び直す間隔(捜索の諦めより短く)
	float raidMaxSec = 40.0f;			// これを過ぎたら襲撃を打ち切る
	float raidArriveRatio = 0.6f;		// 円の半径のこの割合まで来たら到着扱い

	//---- 開始地点の猶予 ----
	float startZoneRadius = 30.0_m;		// SetStartZone を呼ばなかったときの半径

	//---- デバッグ配置 ----
	std::string debugSpawnFile = "Asset/Data/DebugEnemySpawns.json";

	template<class V> void Visit(V& v)
	{
		using Tuning::Unit;
		v("retargetCooldown", retargetCooldown);
		v("chickNoticeRadius", chickNoticeRadius, Unit::Meter);
		v("chickRingRadius", chickRingRadius, Unit::Meter);
		v("chickAimHeight", chickAimHeight, Unit::Meter);
		v("commitStandOff", commitStandOff, Unit::Meter);
		v("carryPickRadius", carryPickRadius, Unit::Meter);
		v("maxSquads", maxSquads);
		v("squadSpread", squadSpread, Unit::Meter);
		v("squadSeparation", squadSeparation, Unit::Meter);
		v("anchorTries", anchorTries);
		v("spawnRadiusMin", spawnRadiusMin, Unit::Meter);
		v("spawnRadiusMax", spawnRadiusMax, Unit::Meter);
		v("groundRayUp", groundRayUp, Unit::Meter);
		v("groundRayLength", groundRayLength, Unit::Meter);
		v("maxCommit", maxCommit);
		v("maxHarass", maxHarass);
		v("slotCount", slotCount);
		v("ringRadius", ringRadius, Unit::Meter);
		v("outerOffset", outerOffset, Unit::Meter);
		v("shatterChainRadius", shatterChainRadius, Unit::Meter);
		v("shatterChainHeight", shatterChainHeight, Unit::Meter);
		v("shatterFullTransfer", shatterFullTransfer);
		v("shatterRadiusMin", shatterRadiusMin);
		v("shatterFalloffMin", shatterFalloffMin);
		v("shatterHitStopBase", shatterHitStopBase);
		v("shatterHitStopPerHit", shatterHitStopPerHit);
		v("shatterMaxHitStop", shatterMaxHitStop);
		v("shatterTraumaBase", shatterTraumaBase);
		v("shatterTraumaPerHit", shatterTraumaPerHit);
		v("chainSlowCount", chainSlowCount);
		v("chainSlowScale", chainSlowScale);
		v("chainSlowSec", chainSlowSec);
		v("corpseSlamRadius", corpseSlamRadius, Unit::Meter);
		v("corpseSlamHeight", corpseSlamHeight, Unit::Meter);
		v("corpseSlamDamage", corpseSlamDamage);
		v("corpseSlamCone", corpseSlamCone);
		v("corpseSlamHitStopBase", corpseSlamHitStopBase);
		v("corpseSlamHitStopPerHit", corpseSlamHitStopPerHit);
		v("corpseSlamTraumaBase", corpseSlamTraumaBase);
		v("corpseSlamTraumaPerHit", corpseSlamTraumaPerHit);
		v("callTraumaBase", callTraumaBase);
		v("callTraumaPerHit", callTraumaPerHit);
		v("callTraumaMax", callTraumaMax);
		v("lastKillHitStop", lastKillHitStop);
		v("lastKillTrauma", lastKillTrauma);
		v("lastKillVibration", lastKillVibration);
		v("lastKillEffect", lastKillEffect);
		v("raidSquadCount", raidSquadCount);
		v("raidRecallSec", raidRecallSec);
		v("raidMaxSec", raidMaxSec);
		v("raidArriveRatio", raidArriveRatio);
		v("startZoneRadius", startZoneRadius, Unit::Meter);
		v("debugSpawnFile", debugSpawnFile);
	}
};

class EnemyBase;
class IChara;
//敵を一元管理するクラス、
class EnemyManager : public IObject
{
public:

	using IObject::IObject;

	EnemyManager() = default;
	~EnemyManager() = default;

	void Init()override;

	void PreUpdate()override;
	void Update()override;
	void PostUpdate()override;

	std::string GetID() override { return "EnemyManager"; }

	//追跡対象
	using TargetPoints = std::vector<std::pair<int, Math::Vector3>>;
	using TargetPointsFn = std::function<TargetPoints()>;
	void SetChickTargetProvider(TargetPointsFn _fn) { m_chickTargets = std::move(_fn); }
	void SetTargetPlayer(const kd::ecs::Entity& target) { m_targetPlayer = target; }

	//============================================================
	// 開始地点の猶予
	//============================================================
	// 開始地点を中心とした円。プレイヤーがここを出るまで、敵は誰にも気付かない。
	// 一度出たら(または敵に手を出したら)、戻ってきても隠れられない。
	void SetStartZone(const Math::Vector3& _center, float _radius)
	{
		m_zoneCenter = _center;
		m_zoneRadius = _radius;
		m_zoneEnabled = true;
		m_zoneCentered = true;
		m_exposed = false;
	}

	// 中心は最初のフレームのプレイヤー位置にする。
	// Init の順番に左右されないよう、位置は Update で拾う。
	void SetStartZoneRadius(float _radius)
	{
		m_zoneRadius = _radius;
		m_zoneEnabled = true;
		m_zoneCentered = false;
		m_exposed = false;
	}

	// 気付かれたか。猶予を使っていなければ常に true。
	bool IsExposed() const { return m_exposed; }

	// 外から猶予を終わらせる。鳴き声で固めた時など、当たり判定を通らない手出し用。
	void ForceExpose(const char* _reason) { Expose(_reason); }

	// 露見した瞬間に1度だけ呼ばれる。置き去りのひよこを標的に戻すのに使う。
	using ExposedFn = std::function<void()>;
	void SetOnExposed(ExposedFn _fn) { m_onExposed = std::move(_fn); }

	//============================================================
	// ひよこの連れ去り
	//============================================================
	int RequestCarrier(const Math::Vector3& _chickPos, int _chickId);

	// 運び手の状態。ひよこ側はこれを見て、位置・縮み・消滅を決める。
	struct CarrierInfo
	{
		Math::Vector3 mouth = Math::Vector3::Zero;
		Math::Vector3 foot = Math::Vector3::Zero;	// 離した時に置く位置。見つからなければ呼び出し側の値のまま
		float scale = 1.0f;		// 走り去りの縮み(1→0)
		bool  leaving = false;	// 走り去り中
		bool  gone = false;		// 咥えたまま消えた
	};

	// まだ咥えていれば true。false の時は gone で「離した」か「消えた」かを見分ける。
	bool GetCarrierPose(int _carrierId, int _chickId, CarrierInfo& _out) const;

	// 運び手の指名を解除する。ひよこ側から手放す場合に使う。
	void ReleaseCarrier(int _carrierId);

	const std::vector<std::shared_ptr<EnemyBase>>& GetEnemyies() const { return m_spEnemies; }

	// 生存している敵の数(死体を除く)
	int GetAliveCount() const;

	// HUD用。スポーンした総数と現在の生存数。
	int GetSpawnedTotal() const { return m_spawnedTotal; }

	// 全体の残存率(1=無傷)。HUD用。逃走判定は部隊ごとの士気を使う。
	float GetMorale() const { return m_morale; }

	//============================================================
	// 部隊
	//============================================================
	struct Squad
	{
		int   id = 0;
		const char* name = "-";			// 編成名(デバッグ表示)
		Math::Vector3 anchor = Math::Vector3::Zero;	// 展開した場所
		Math::Vector3 center = Math::Vector3::Zero;	// 生存者の重心

		int   alive = 0;
		int   peak = 0;
		float morale = 1.0f;
		bool  engaged = false;			// プレイヤーを認識したか
	};

	const std::vector<Squad>& GetSquads() const { return m_squads; }
	int  GetEngagedSquadCount() const;

	// 全滅した部隊の代わりを送り込むか。既定は切ってある。
	void SetAutoReinforce(bool _on) { m_autoReinforce = _on; }

	// AIデバッグビューア。毎フレーム好きな場所から呼ぶ。
	void DrawDebugImGui();

	// フェーズ
	void SpawnPhase(int _phaseIndex);
	// このフェーズで展開した総数。HUDの残数表示に使う。
	int GetPhaseTotal() const { return m_phaseTotal; }
	int GetPhaseKilled() const { return m_phaseKilled; }

private:

	//適正生成
	void SpawnSquad();				// 部隊を1つ展開する
	// _spread を省略すると調整値の squadSpread
	void SpawnMember(const Squad& _squad,
		std::shared_ptr<EnemyBase>(*_make)(kd::ecs::ComponentManager&, const std::shared_ptr<CollisionWorld>&));
	void SpawnMember(const Squad& _squad,
		std::shared_ptr<EnemyBase>(*_make)(kd::ecs::ComponentManager&, const std::shared_ptr<CollisionWorld>&),
		float _spread);
	void SpownLogic();

	// 部隊の生存数/重心/士気を集計し、交戦の開始を検知する。
	void UpdateSquads();
	// 他の部隊とプレイヤーから離れた展開地点を選ぶ。
	bool PickSquadAnchor(Math::Vector3& _out) const;
	// 部隊全員を見つけた地点へ向かわせ、交戦状態にする。
	// _alertPos は見つけたもの(プレイヤーかひよこ)の位置。
	void EngageSquad(Squad& _squad, const Math::Vector3& _alertPos);

	Squad* FindSquad(int _id);
	const Squad* FindSquad(int _id) const;
	bool IsSquadEngaged(int _id) const;

	// 同時に攻撃してよい敵を制限する。囲まれて一斉に殴られると理不尽になるため。
	void UpdateAttackTokens(bool _hasTarget, int _targetCount, const std::vector<int>& _assign);
	// 標的ごとに囲みを作る。_assign[i] = m_spEnemies[i] が狙っている標的の番号
	void UpdateFormation(const std::vector<Math::Vector3>& _targets,
		const std::vector<int>& _assign);
	void UpdateFormationAround(const Math::Vector3& _center, float _radius,
		int _targetIndex, const std::vector<int>& _assign);
	// 呼び声を周囲へ配る。呼んだ本人は Call ステートで足を止めている。
	void UpdateCalls();

	//============================================================
	// 開始地点の猶予
	//============================================================
	// 円を出たか、手を出したかを見る。露見したら以後は何もしない。
	void UpdateStartZone(const Math::Vector3& _playerPos);
	// 猶予を終わらせる。_reason はデバッグ表示用。
	void Expose(const char* _reason);
	// 開始地点へ一番近い部隊を送り込む。置き去りのひよこを襲わせる。
	void DispatchRaid();
	// 襲撃の目的地。円の中に置き去りのひよこが居なければ false(襲う理由が無い)。
	bool FindRaidPoint(Math::Vector3& _out) const;
	// 襲撃隊を目的地まで歩かせ続ける。捜索は数秒で諦めるので呼び直す。
	void UpdateRaid();
	bool IsRaider(int _squadId) const;

	//============================================================
	// デバッグ配置(部隊のセーブ＆ロード)
	//============================================================
	struct DebugSpawnEntry
	{
		std::string   squadName;			// kSquadTable の name と対応
		Math::Vector3 anchor = Math::Vector3::Zero;	// 配置地点
		float         range = 0.0f;					// 配置範囲(半径)。0 なら調整値の squadSpread
		int           phase = 0;                    // 配置フェーズ
	};

	// デバッグ配置UI本体。DrawDebugImGui() から呼ぶ。
	void DrawDebugSpawnPlacer();

	// 範囲プレビュー(仮スポーン4体)。編集中の値に追従させる/片付ける。
	void UpdateDebugPreviewMarkers();
	void ClearDebugPreviewMarkers();
	void SpawnDebugPreviewMember(const Math::Vector3& _pos);

	// 保存済み配置のJSON入出力。
	void SaveDebugSpawns() const;
	void LoadDebugSpawns();
	void SpawnSavedDebugSquads();			// m_debugSpawnEntries をまとめて展開する

	// 任意の地点・範囲で部隊を1つ展開する(PickSquadAnchor を使わない版)。
	void SpawnSquadAt(const Math::Vector3& _anchor, const std::string& _squadName, float _range);

	int m_spawnedTotal = 0;

	// Update で拾ったターゲット情報
	Math::Vector3 m_lastTargetPos = Math::Vector3::Zero;
	bool          m_lastHasTarget = false;

	std::unordered_map<int, float> m_retargetTimer;

	// ---- 士気 ----
	int   m_peakAlive = 0;		// これまでの最大生存数。残存率の分母。
	float m_morale = 1.0f;

	// ---- ラストキル ----
	bool  m_lastKillDone = false;	// 決めの間は1回だけ

	// ---- 部隊 ----
	std::vector<Squad> m_squads;
	int  m_nextSquadId = 0;
	bool m_autoReinforce = false;


	// 同時に攻撃できる数を役割ごとに分ける。
	// 本命を1体に絞ることで、避けるべき攻撃が常に1つになる。






	// ---- 死体のなぎ倒し ----

	// ---- 開始地点の猶予 ----
	bool          m_zoneEnabled = false;	// 猶予を使うか
	bool          m_zoneCentered = false;	// 中心を確定したか
	bool          m_exposed = true;			// 気付かれたか。猶予を使わなければ最初から true
	Math::Vector3 m_zoneCenter = Math::Vector3::Zero;
	float         m_zoneRadius = 0.0f;			// Init で startZoneRadius が入る
	const char* m_exposeReason = "-";		// 露見した理由(デバッグ表示)
	ExposedFn     m_onExposed;

	// ---- 襲撃 ----
	std::vector<int> m_raidSquads;		// 開始地点へ向かわせた部隊のid
	bool  m_raidActive = false;
	bool  m_raidPending = false;		// 露見時に部隊が居なかった。次に湧いたら送る
	float m_raidTimer = 0.0f;
	float m_raidRecall = 0.0f;
	Math::Vector3 m_raidPos = Math::Vector3::Zero;	// 向かう先(置き去りのひよこの重心)


	// ---- デバッグ配置 ----
	std::vector<DebugSpawnEntry> m_debugSpawnEntries;	// 保存済みの配置一覧(編集画面のリストにも使う)

	Math::Vector3 m_debugAnchor = Math::Vector3::Zero;	// 編集中の配置地点
	float         m_debugRange = 0.0f;					// 編集中の範囲(Init で squadSpread が入る)
	int           m_debugSquadIndex = 0;				// kSquadTable内のインデックス(コンボ選択)
	bool          m_debugPreviewEnabled = false;		// 範囲プレビュー(仮スポーン)のON/OFF

	std::vector<std::shared_ptr<EnemyBase>> m_debugPreviewEnemies;	// 範囲プレビュー用の仮スポーン(4体)

	static constexpr int kDebugPreviewSquadId = -1;	// 仮スポーンの部隊ID(実部隊のidと衝突しない)

	TargetPointsFn m_chickTargets;
	std::unordered_map<int, int> m_targetOf;

	// 走り去って消えた運び手 → 咥えていたひよこ。
	// 敵を配列から消した後でも、ひよこ側が「消えた」と分かるように残す。
	std::unordered_map<int, int> m_vanishedCarriers;

	kd::ecs::Entity m_targetPlayer;

	EnemyManagerTuning m_tuning;
	std::vector<std::shared_ptr<EnemyBase>> m_spEnemies = {};

	//フェーズ
	int m_phaseTotal = 0;
	int m_phaseKilled = 0;
	int m_debugPhase = 0;		// 編集中のフェーズ
	int m_debugViewPhase = -1;	// 一覧で表示するフェーズ(-1で全部)


};