#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class StateMachineSystem;

// ===========================================================
// 意味のある固定値
// 調整用の値(遷移上限や色など)は StateMachineConfig 側。ここは仕様として決まっている値だけ
// ===========================================================
namespace SMConst
{
	// まだ一度も数えていないことを表す値(アニメ同期や履歴の初回判定に使う)
	inline constexpr uint32_t kUnsetCount = 0xFFFFFFFFu;

	// 優先度の最大。強制遷移と初期遷移はこれで予約する
	inline constexpr uint8_t kForcedPriority = 0xFF;

	// 予約の寿命を「無期限」にするときの値(負なら無期限)
	inline constexpr float kInfiniteBuffer = -1.0f;

	// allowFrom / blockFrom をビットマスクにできるステート数(uint64_t のビット数)
	inline constexpr size_t kGuardMaskBits = sizeof(uint64_t) * 8;

	// StateBuilder::Anim() を引数省略で呼んだときの値。
	// コンパイル時に決まる必要があるので、ここだけは設定ファイルから読めない
	inline constexpr float kDefaultBlendFrame = 10.0f;
	inline constexpr bool  kDefaultAnimLoop = true;
}

// ===========================================================
// params の予約キー
// 文字列を何か所にも直書きしていたので1か所にまとめた。
// ハッシュ値は初回に1回だけ計算する
// ===========================================================
namespace SMParamKey
{
	inline constexpr const char* kAnim = "anim";
	inline constexpr const char* kBlend = "blend";
	inline constexpr const char* kLoop = "loop";
	inline constexpr const char* kSpeed = "speed";
	inline constexpr const char* kOnEnd = "onEnd";

	inline StateId AnimId() { static const StateId id = MakeStateId(kAnim);  return id; }
	inline StateId BlendId() { static const StateId id = MakeStateId(kBlend); return id; }
	inline StateId LoopId() { static const StateId id = MakeStateId(kLoop);  return id; }
	inline StateId SpeedId() { static const StateId id = MakeStateId(kSpeed); return id; }
	inline StateId OnEndId() { static const StateId id = MakeStateId(kOnEnd); return id; }
}

// ===========================================================
// StateMachineConfig
// StateMachineConfig.json から読む調整値。
// ここに書いてある値は「JSONが無い・読めなかったとき」の保険で、前のコードの値と同じ
// ===========================================================
struct StateMachineConfig
{
	using Color = std::array<float, 4>;	// RGBA(0〜1)

	// 1回の TryApplyReserved で続けて遷移してよい回数。
	// Enter の中で ChangeState を呼び合うような定義ミスで固まらないための上限
	int maxTransitionsPerFrame = 8;

	// params に blend / speed / loop が無いときの値
	float defaultBlendFrame = SMConst::kDefaultBlendFrame;	// フレーム単位(秒ではない)
	float defaultAnimSpeed = 1.0f;
	bool  defaultAnimLoop = SMConst::kDefaultAnimLoop;

	// デバッグ表示に残す遷移履歴の件数
	size_t historyMax = 16;

	// デバッグ表示の色
	Color colorBase = { 0.55f, 0.80f, 1.00f, 1.0f };
	Color colorState = { 0.60f, 1.00f, 0.60f, 1.0f };
	Color colorDim = { 0.60f, 0.60f, 0.60f, 1.0f };
	Color colorEvent = { 1.00f, 0.85f, 0.40f, 1.0f };
	Color colorWarn = { 1.00f, 0.55f, 0.45f, 1.0f };
};

struct StateContext
{
	StateMachineComponent* pMachine = nullptr;
	StateMachineSystem* pSystem = nullptr;
	const struct StateDesc* pDesc = nullptr;	// 実行中ステートの定義
	void* pUser = nullptr;						// TestEntity* など所有者
	uint32_t entity = 0;			// エンティティ識別子。kd::ecs::Entity なら .index を渡す
	float deltaTime = 0.0f;			// OnUpdate のときだけ入る。Enter/Exit では 0

	// pUserを目的の型で取り出す
	template<class T> T* As() const { return static_cast<T*>(pUser); }

	// ---- 以下はシステム定義後にインライン実装(このヘッダ末尾) ----
	StateResult ChangeState(StateId id, uint8_t priority = 0) const;
	StateResult ChangeStateForced(StateId id) const;

	// 自ステートのparamsを引く。無ければベース、それも無ければ既定値。
	float Param(StateId key, float defaultValue = 0.0f) const;
	const char* ParamStr(StateId key, const char* defaultValue = "") const;

	bool ConsumeEvent(StateId id) const { return pMachine->ConsumeEvent(id); }
	float Time() const { return pMachine->GetStateTime(); }
};

using StateFn = void(*)(StateContext&);
using StateCondFn = bool(*)(const StateContext&);

// ===========================================================
// StateParams : JSON/ビルダーから流し込む可変パラメータ
// ※GetString() が返すポインタは、同じ params に SetString() するまでの間だけ有効
// ===========================================================
class StateParams
{
public:
	void SetFloat(std::string_view name, float value);
	void SetString(std::string_view name, std::string value);

	float GetFloat(StateId key, float defaultValue = 0.0f) const;
	const char* GetString(StateId key, const char* defaultValue = "") const;

	bool HasFloat(StateId key) const;
	bool HasString(StateId key) const;

	// デバッグ表示用(キー名を保持しているので名前で出せる)
	size_t GetFloatCount() const { return m_floats.size(); }
	const char* GetFloatName(size_t i) const { return m_floats[i].name.c_str(); }
	float GetFloatAt(size_t i) const { return m_floats[i].value; }

	size_t GetStringCount() const { return m_strings.size(); }
	const char* GetStringName(size_t i) const { return m_strings[i].name.c_str(); }
	const char* GetStringAt(size_t i) const { return m_strings[i].value.c_str(); }

private:
	struct FloatEntry { StateId key; std::string name; float value; };
	struct StringEntry { StateId key; std::string name; std::string value; };

	// 1ステートあたり数個しか持たない想定なので線形探索で十分
	std::vector<FloatEntry> m_floats;
	std::vector<StringEntry> m_strings;
};

// ===========================================================
// StateDesc : ステート1つ分の定義
// ===========================================================
struct StateDesc
{
	std::string name;
	std::string baseName;			// 空ならこのステート自身がベース

	StateId id = SM::kInvalidStateId;
	StateId baseId = SM::kInvalidStateId;

	uint16_t index = SM::kInvalidStateIndex;
	uint16_t baseIndex = SM::kInvalidStateIndex;		// ベース自身の場合は自分のindex
	uint16_t defaultChild = SM::kInvalidStateIndex;	// ベースが直接指定されたときの飛び先

	// ---- 遷移ガード ----
	uint32_t tag = 0;				// このステートが持つビットフラグ
	uint32_t blockTagMask = 0;		// 現在ステートがこのタグを持つなら入れない
	float minTime = 0.0f;			// このステートに入ってから出られない時間(秒)
	float bufferTime = SMConst::kInfiniteBuffer;	// 予約の寿命(秒)。負なら無期限

	std::vector<StateId> allowFrom;	// 空なら全ステートから遷移可
	std::vector<StateId> blockFrom;	// ここに含まれるステートからは遷移不可

	// ---- 上記2つを高速判定用に変換したもの(BuildGuardMasksで一括計算) ----
	// ビット位置は各ステートのindexに対応する。システムが64ステートを超える場合は
	// 計算されず0のままになり、その場合は allowFrom/blockFrom の線形探索にフォールバックする。
	uint64_t allowFromBits = 0;
	uint64_t blockFromBits = 0;

	// ---- 振る舞い ----
	StateFn onEnter = nullptr;
	StateFn onUpdate = nullptr;
	StateFn onExit = nullptr;
	StateCondFn canEnter = nullptr;	// Update内でのみ評価される

	StateParams params;

	bool IsBase() const { return baseIndex == index; }
	bool IsAllowedFrom(StateId from) const;
};

// 4つのコールバックをまとめて渡すための箱(JSON登録で使う)
struct StateBehavior
{
	StateFn onEnter = nullptr;
	StateFn onUpdate = nullptr;
	StateFn onExit = nullptr;
	StateCondFn canEnter = nullptr;
};

// ===========================================================
// StateBuilder
// ステート定義を流れるように書くための補助。
// Build()を呼ばなくてもデストラクタで自動登録される。
// Build() 後に設定系を呼んでも無視される(前は null 参照で落ちていた)
// ===========================================================
class StateBuilder
{
public:
	StateBuilder(StateMachineSystem& system, std::string_view name);
	~StateBuilder();

	StateBuilder(const StateBuilder&) = delete;
	StateBuilder& operator=(const StateBuilder&) = delete;
	StateBuilder(StateBuilder&&) noexcept;

	StateBuilder& Base(std::string_view baseName);
	StateBuilder& Tag(uint32_t tag);

	StateBuilder& OnEnter(StateFn fn);
	StateBuilder& OnUpdate(StateFn fn);
	StateBuilder& OnExit(StateFn fn);
	StateBuilder& CanEnter(StateCondFn fn);
	StateBuilder& Behavior(const StateBehavior& behavior);

	// ---- 遷移ガード ----
	StateBuilder& AllowFrom(std::initializer_list<StateId> ids);
	StateBuilder& BlockFrom(std::initializer_list<StateId> ids);
	StateBuilder& BlockTag(uint32_t mask);
	StateBuilder& MinTime(float seconds);
	StateBuilder& Buffer(float seconds);	// 負値で無期限

	// ---- パラメータ ----
	StateBuilder& Param(std::string_view key, float value);
	StateBuilder& Param(std::string_view key, std::string_view value);

	// アニメ連携用のショートカット
	StateBuilder& Anim(std::string_view animName,
		float blendFrame = SMConst::kDefaultBlendFrame, bool loop = SMConst::kDefaultAnimLoop);
	StateBuilder& AnimSpeed(float speed);
	StateBuilder& OnEnd(std::string_view nextStateName);

	uint16_t Build();

private:
	// 登録前の定義を返す。Build 済み・ムーブ済みなら nullptr
	StateDesc* EditableDesc();

	StateMachineSystem* m_pSystem = nullptr;
	std::unique_ptr<StateDesc> m_desc;
	uint16_t m_index = SM::kInvalidStateIndex;
	bool m_built = false;
};

// ===========================================================
// StateMachineSystem
// ===========================================================
class StateMachineSystem
{
public:
	explicit StateMachineSystem(std::string_view name) : m_name(name) {}

	const std::string& GetName() const { return m_name; }
	uint16_t GetId() const { return m_id; }

	// =======================================================
	// 調整値
	// 普段は StateMachineManager::ApplyConfigJson() 経由で全システムに配られる
	// =======================================================
	void SetConfig(const StateMachineConfig& config);
	const StateMachineConfig& GetConfig() const { return m_config; }

	// =======================================================
	// 定義の登録
	// =======================================================

	// ---- 方法A : プログラム側で決め打ち ----
	StateBuilder State(std::string_view name) { return StateBuilder(*this, name); }

	// ---- 方法B : JSON ----
	// ラムダはJSONに書けないため、先に振る舞いを名前で登録しておく。
	void RegisterBehavior(std::string_view key, const StateBehavior& behavior);

	// nlohmann::json は inline名前空間内のtypedefで前方宣言できないため、
	// テンプレートで受けて cpp 側で明示的実体化している。
	// これによりこのヘッダは nlohmann に一切依存しない。
	// 型が違う項目はスキップして読み進める(例外は投げない)
	template<class JsonT> bool LoadFromJson(const JsonT& root);

	// 既存を破棄して再読込。
	// ※ステートの index が振り直されるので、使っているコンポーネントは Reset() し直すこと
	template<class JsonT> bool ReloadFromJson(const JsonT& root);

	// パース込み。nlohmann を知らなくても呼べる。
	bool LoadFromJsonText(std::string_view text);

	// ビルダー/JSONローダから呼ばれる最終登録口
	uint16_t Register(std::unique_ptr<StateDesc> desc);

	void SetDefaultChild(std::string_view baseName, std::string_view childName);
	void SetEntryState(std::string_view name);
	uint16_t GetEntryIndex() const { return m_entryIndex; }

	// 定義の整合性チェック。問題があれば false。
	// 非const版は、チェックに加えて遷移ガード用のビットマスクも作り直す。
	// (前は const の中で const_cast して作っていた)
	bool Validate(std::string* outError = nullptr);
	bool Validate(std::string* outError = nullptr) const;

	void BuildGuardMasks();

	void ClearStates();

	// =======================================================
	// 参照
	// =======================================================
	uint16_t Find(StateId id) const;
	uint16_t Find(std::string_view name) const { return Find(MakeStateId(name)); }

	const StateDesc* Get(uint16_t index) const
	{
		return (index < m_states.size()) ? m_states[index].get() : nullptr;
	}
	StateDesc* GetMutable(uint16_t index)
	{
		return (index < m_states.size()) ? m_states[index].get() : nullptr;
	}
	const StateDesc* GetById(StateId id) const { return Get(Find(id)); }

	size_t GetStateCount() const { return m_states.size(); }
	const StateBehavior* FindBehavior(StateId key) const;

	// コンポーネントから名前を引く(コンポーネント自身は名前を持たない)
	const char* GetStateName(const StateMachineComponent& sm) const;
	const char* GetBaseName(const StateMachineComponent& sm) const;
	const char* GetNameOf(StateId id) const;

	// =======================================================
	// コンポーネント操作
	// =======================================================

	// このシステムに紐づいたコンポーネントを生成する
	StateMachineComponent MakeComponent() const;

	// 初期ステートから開始し直す
	void Reset(StateMachineComponent& sm) const;

	// 毎フレーム呼ぶ。ベース→子 の順に OnUpdate を回す。
	void Update(StateMachineComponent& sm, float deltaTime,
		void* pUser = nullptr, uint32_t entity = 0);

	// ---- 遷移要求 ----
	// 予約寿命はステート定義の buffer 値を使う
	StateResult ChangeState(StateMachineComponent& sm, StateId id, uint8_t priority = 0) const;

	// 予約寿命を明示する。0を渡すと「今すぐ通らなければ破棄」
	StateResult ChangeStateBuffered(StateMachineComponent& sm, StateId id,
		float bufferTime, uint8_t priority = 0) const;

	// 全ガードを無視する。被弾ノックバックやデバッグ用。
	StateResult ChangeStateForced(StateMachineComponent& sm, StateId id) const;

	bool TryChangeState(StateMachineComponent& sm, StateId id, uint8_t priority = 0) const
	{
		return IsAccepted(ChangeState(sm, id, priority));
	}

	// 遷移させずに可否だけ調べる(canEnterは評価されない)
	StateResult IsChangePossible(const StateMachineComponent& sm, StateId id) const;

	// paramsを引く。子に無ければベースから引き継ぐ。
	float FindParam(const StateMachineComponent& sm, StateId key, float defaultValue = 0.0f) const;
	const char* FindParamStr(const StateMachineComponent& sm, StateId key, const char* defaultValue = "") const;

	// =======================================================
	// アニメーション連携
	//
	// params の予約キー(SMParamKey):
	//   anim  : アニメーション名
	//   blend : ブレンド時間【フレーム単位】(※秒ではない)
	//   loop  : ループするか
	//   speed : 再生速度
	//   onEnd : 非ループ終了時の自動遷移先
	// blend / loop / speed が無いときは StateMachineConfig の既定値を使う
	// =======================================================
	using AnimResolver = std::function<std::shared_ptr<KdAnimationData>(const char*)>;

	// アニメーション名から実データを引く関数を登録する(システムに1つ)
	void SetAnimResolver(AnimResolver resolver) { m_animResolver = std::move(resolver); }

	// Update直後に呼ぶ。遷移していればアニメを差し替える。
	//
	// 実装はこのヘッダ末尾にテンプレートとして置いてある。
	// KdAnimator の完全な定義を必要とするのは「呼び出した場所」だけになるため、
	// このヘッダはアニメーション関連のヘッダを include しなくて済み、
	// cpp とのバージョン不一致によるリンクエラーも起こらない。
	template<class Animator, class Resolver>
	void SyncAnimation(StateMachineComponent& sm, Animator& animator, Resolver&& resolve);

	// 描画前に呼ぶ。AdvanceTime + イベント移送 + onEndの自動遷移。
	// rNodes には work->WorkNodes() をそのまま渡す。
	template<class Animator, class NodeArray>
	void AdvanceAnimation(StateMachineComponent& sm, Animator& animator, NodeArray& rNodes);


	// =======================================================
	// デバッグ
	// =======================================================

	// アニメーションイベント名の逆引き登録(デバッグ表示用)
	void RegisterEventName(std::string_view name);
	const char* GetEventName(StateId id) const;

	// 中で ImGui::Begin/End を呼ぶので、他のウィンドウの Begin/End の外で呼ぶこと。
	// historyKey に entity を渡すと、エンティティごとに履歴を保持する。
	void DrawImGui(StateMachineComponent& sm, uint32_t historyKey = 0,
		bool allowForceChange = true);

	void ClearHistory(uint32_t historyKey);

private:
	friend class StateMachineManager;

	// ---- 内部処理 ----
	uint16_t ResolveTarget(uint16_t index) const;
	StateResult CheckGuard(const StateMachineComponent& sm, uint16_t targetIndex) const;
	StateResult Reserve(StateMachineComponent& sm, uint16_t index,
		uint8_t priority, float bufferTime, bool force) const;

	void Invoke(StateFn fn, StateMachineComponent& sm, const StateDesc* desc,
		void* pUser, uint32_t entity, float deltaTime);
	bool EvalCanEnter(StateMachineComponent& sm, const StateDesc* desc,
		void* pUser, uint32_t entity);

	void TryApplyReserved(StateMachineComponent& sm, void* pUser, uint32_t entity);
	void DoTransition(StateMachineComponent& sm, uint16_t nextIndex,
		void* pUser, uint32_t entity);

	void RecordHistory(const StateMachineComponent& sm, uint32_t key);

	// DrawImGui の中身を分けたもの
	void DrawCurrentState(const StateMachineComponent& sm);
	void DrawReservation(StateMachineComponent& sm);
	void DrawEvents(const StateMachineComponent& sm);
	void DrawGuardAndParams(const StateMachineComponent& sm);
	void DrawHistory(uint32_t historyKey);
	void DrawForceChange(StateMachineComponent& sm);

	std::string m_name;
	uint16_t m_id = SM::kInvalidStateIndex;

	StateMachineConfig m_config;

	// unique_ptrで包むのは、登録時のvector再確保でDescのアドレスが
	// 変わると StateContext::pDesc が宙に浮くため。
	std::vector<std::unique_ptr<StateDesc>> m_states;
	std::unordered_map<StateId, uint16_t> m_lookup;
	std::unordered_map<StateId, StateBehavior> m_behaviors;
	uint16_t m_entryIndex = SM::kInvalidStateIndex;

	// BuildGuardMasks() が成功して allowFromBits/blockFromBits が使える状態かどうか。
	// falseの間はCheckGuardが従来のvector線形探索にフォールバックする。
	// ステートを追加したら作り直しが必要なので Register() で false に戻す
	bool m_guardMasksReady = false;

	AnimResolver m_animResolver;

	// SyncAnimationで一度リゾルバが見つけられなかったアニメ名(StateId化したもの)。
	// 同じ名前を毎フレーム(毎遷移)リゾルバに投げ直してリークさせないためのキャッシュ。
	std::unordered_set<StateId> m_missingAnims;

	// ---- デバッグ ----
	std::unordered_map<StateId, std::string> m_eventNames;

	struct HistoryEntry
	{
		std::string baseName;
		std::string stateName;
		float duration = 0.0f;
		uint32_t order = 0;
	};
	struct HistoryTrack
	{
		std::vector<HistoryEntry> entries;	// 先頭が最新
		uint32_t lastTransition = SMConst::kUnsetCount;
		float lastStateTime = 0.0f;
		std::string lastBaseName;
		std::string lastStateName;
	};
	std::unordered_map<uint32_t, HistoryTrack> m_histories;
};

// ===========================================================
// StateContext のインライン実装(システム定義後に置く必要がある)
// ===========================================================
inline StateResult StateContext::ChangeState(StateId id, uint8_t priority) const
{
	return pSystem->ChangeState(*pMachine, id, priority);
}

inline StateResult StateContext::ChangeStateForced(StateId id) const
{
	return pSystem->ChangeStateForced(*pMachine, id);
}

inline float StateContext::Param(StateId key, float defaultValue) const
{
	if (pDesc && pDesc->params.HasFloat(key)) return pDesc->params.GetFloat(key);
	return pSystem->FindParam(*pMachine, key, defaultValue);
}

inline const char* StateContext::ParamStr(StateId key, const char* defaultValue) const
{
	if (pDesc && pDesc->params.HasString(key)) return pDesc->params.GetString(key);
	return pSystem->FindParamStr(*pMachine, key, defaultValue);
}

// ===========================================================
// スコープを抜けるときに必ず走らせたい後片付け用
// ===========================================================
namespace SMDetail
{
	template<class Fn>
	class ScopeExit
	{
	public:
		explicit ScopeExit(Fn fn) : m_fn(std::move(fn)) {}
		~ScopeExit() { m_fn(); }

		ScopeExit(const ScopeExit&) = delete;
		ScopeExit& operator=(const ScopeExit&) = delete;

	private:
		Fn m_fn;
	};
}

// ===========================================================
// アニメーション連携の実装
//
// テンプレートにしてあるのは KdAnimator / KdModelWork の完全な定義を
// このヘッダで要求しないため。実体化は呼び出し側(PCHで型が見えている cpp)
// で行われるので、別TUのビルド結果に依存せずリンクエラーが起きない。
// ===========================================================
template<class Animator, class Resolver>
void StateMachineSystem::SyncAnimation(StateMachineComponent& sm, Animator& animator, Resolver&& resolve)
{
	// 遷移していなければ何もしない
	if (sm.animSyncCount == sm.transitionCount) return;
	sm.animSyncCount = sm.transitionCount;

	const char* animName = FindParamStr(sm, SMParamKey::AnimId(), "");

	// anim が未指定のステート(遷移判定だけを持つベース等)はアニメを触らない
	if (animName[0] == '\0') return;

	const StateId animKey = MakeStateId(std::string_view(animName));

	// 既に「見つからない」と分かっている名前は二度とリゾルバに投げない。
	// GetAnimation側の実装によっては、見つからない名前を渡すたびに
	// 新しいリソースを確保して返すことがあり、それが毎回のリークにつながるため。
	if (m_missingAnims.count(animKey) != 0) return;

	auto spAnim = resolve(animName);
	if (!spAnim)
	{
		char str[256];
		sprintf_s(str, "StateMachineSystem: アニメーションが見つかりません \nAnimName : %s\n", animName);
		m_missingAnims.insert(animKey);
		OutputDebugStringA(str);

		assert(false && "NotFoundAnimation");

		// 見つからない場合は何もしない(今のアニメを再生し続ける)。
		// 無理に何かを再生しようとしない。
		return;
	}

	// ここまで来て実際に切り替える時だけ、再生速度・ループ・終了遷移先を更新する。
	// (見つからなかった場合にこれらを更新すると、animEndFired 判定などが
	//  古いアニメーターの状態と噛み合わなくなり、意図しない強制遷移を招くため)
	const float defaultLoop = m_config.defaultAnimLoop ? 1.0f : 0.0f;
	sm.animPlaySpeed = FindParam(sm, SMParamKey::SpeedId(), m_config.defaultAnimSpeed);
	sm.animLoop = (FindParam(sm, SMParamKey::LoopId(), defaultLoop) != 0.0f) ? 1 : 0;

	const char* onEnd = FindParamStr(sm, SMParamKey::OnEndId(), "");
	sm.animOnEndId = (onEnd[0] != '\0') ? MakeStateId(std::string_view(onEnd)) : SM::kInvalidStateId;
	sm.animEndFired = 0;

	// blend はフレーム単位。KdAnimator::ChangeAnimation の blendFrame と同じ。
	const float blend = FindParam(sm, SMParamKey::BlendId(), m_config.defaultBlendFrame);

	animator.ChangeAnimation(spAnim, blend, sm.animLoop != 0);
}

template<class Animator, class NodeArray>
void StateMachineSystem::AdvanceAnimation(StateMachineComponent& sm, Animator& animator,
	NodeArray& rNodes)
{
	// コールバックは AdvanceTime の内部でのみ同期的に呼ばれる。
	// (コンポーネントは二重バッファでリロケートされうるため、sm のアドレスを跨いで持ってはいけない)
	animator.m_onEvent = [&sm](const std::string& name)
		{
			sm.PushEvent(MakeStateId(name));
		};

	{
		// AdvanceTime の途中で例外が出ても、sm を指したままのコールバックを残さないよう
		// スコープを抜けるときに必ず外す
		SMDetail::ScopeExit clearCallback([&animator]() { animator.m_onEvent = nullptr; });
		animator.AdvanceTime(rNodes, sm.animPlaySpeed);
	}

	// 非ループ再生の終了で自動遷移。
	// IsAnimationEnd() はループ中も一瞬 true になりうるため !animLoop を必ず見る。
	if (sm.animLoop != 0 || sm.animEndFired != 0) return;
	if (sm.animOnEndId == SM::kInvalidStateId) return;

	if (animator.IsAnimationEnd())
	{
		sm.animEndFired = 1;

		// 「必ず戻りたい」遷移なので minTime や canEnter を無視する
		ChangeStateForced(sm, sm.animOnEndId);
	}
}
