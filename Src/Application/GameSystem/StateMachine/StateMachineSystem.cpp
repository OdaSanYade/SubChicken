#include "StateMachineSystem.h"

// ===========================================================
// StateResult
// ===========================================================
const char* ToString(StateResult result)
{
	// enumの並び順と1対1で対応する
	static constexpr const char* kNames[] =
	{
		"Accepted",
		"Deferred",
		"RejectedUnknown",
		"RejectedSameState",
		"RejectedPriority",
		"RejectedForbidden",
	};

	// enum に値を足したのにここを忘れると、前は nullptr が返って printf の %s で落ちていた。
	// 数が合わなければコンパイルの時点で止める
	static_assert(std::size(kNames) == static_cast<size_t>(StateResult::Count),
		"StateResult と kNames の数が合っていません");

	const size_t i = static_cast<size_t>(result);
	return (i < std::size(kNames)) ? kNames[i] : "?";
}

// ===========================================================
// StateParams
// ===========================================================
void StateParams::SetFloat(std::string_view name, float value)
{
	const StateId key = MakeStateId(name);

	for (auto& e : m_floats)
	{
		if (e.key == key) { e.value = value; return; }
	}
	m_floats.push_back({ key, std::string(name), value });
}

void StateParams::SetString(std::string_view name, std::string value)
{
	const StateId key = MakeStateId(name);

	for (auto& e : m_strings)
	{
		if (e.key == key) { e.value = std::move(value); return; }
	}
	m_strings.push_back({ key, std::string(name), std::move(value) });
}

float StateParams::GetFloat(StateId key, float defaultValue) const
{
	for (const auto& e : m_floats) { if (e.key == key) return e.value; }
	return defaultValue;
}

const char* StateParams::GetString(StateId key, const char* defaultValue) const
{
	for (const auto& e : m_strings) { if (e.key == key) return e.value.c_str(); }
	return defaultValue;
}

bool StateParams::HasFloat(StateId key) const
{
	for (const auto& e : m_floats) { if (e.key == key) return true; }
	return false;
}

bool StateParams::HasString(StateId key) const
{
	for (const auto& e : m_strings) { if (e.key == key) return true; }
	return false;
}

// ===========================================================
// StateDesc
// ===========================================================
bool StateDesc::IsAllowedFrom(StateId from) const
{
	// ループ変数を id にすると StateDesc::id を隠して C4458 が出るため e を使う
	for (StateId e : blockFrom) { if (e == from) return false; }

	if (allowFrom.empty()) return true;	// 空なら全許可

	for (StateId e : allowFrom) { if (e == from) return true; }
	return false;
}

// ===========================================================
// StateBuilder
// ===========================================================
StateBuilder::StateBuilder(StateMachineSystem& system, std::string_view name)
	: m_pSystem(&system)
	, m_desc(std::make_unique<StateDesc>())
{
	m_desc->name = std::string(name);
}

StateBuilder::StateBuilder(StateBuilder&& other) noexcept
	: m_pSystem(other.m_pSystem)
	, m_desc(std::move(other.m_desc))
	, m_index(other.m_index)
	, m_built(other.m_built)
{
	other.m_pSystem = nullptr;
	other.m_built = true;
}

StateBuilder::~StateBuilder()
{
	Build();
}

uint16_t StateBuilder::Build()
{
	if (m_built || !m_pSystem || !m_desc) return m_index;

	m_built = true;
	m_index = m_pSystem->Register(std::move(m_desc));
	return m_index;
}

StateDesc* StateBuilder::EditableDesc()
{
	// Build() のあとは m_desc がシステム側にムーブされて空になっている。
	// そのまま -> で触ると null 参照で落ちるので、ここで弾いて呼び出しを無視する
	if (m_built || !m_desc)
	{
		assert(false && "StateBuilder: Build() 済みのビルダーに設定しようとしています");
		return nullptr;
	}
	return m_desc.get();
}

StateBuilder& StateBuilder::Base(std::string_view baseName)
{
	if (StateDesc* d = EditableDesc()) { d->baseName = std::string(baseName); }
	return *this;
}

StateBuilder& StateBuilder::Tag(uint32_t tag)
{
	if (StateDesc* d = EditableDesc()) { d->tag = tag; }
	return *this;
}

StateBuilder& StateBuilder::OnEnter(StateFn fn)
{
	if (StateDesc* d = EditableDesc()) { d->onEnter = fn; }
	return *this;
}

StateBuilder& StateBuilder::OnUpdate(StateFn fn)
{
	if (StateDesc* d = EditableDesc()) { d->onUpdate = fn; }
	return *this;
}

StateBuilder& StateBuilder::OnExit(StateFn fn)
{
	if (StateDesc* d = EditableDesc()) { d->onExit = fn; }
	return *this;
}

StateBuilder& StateBuilder::CanEnter(StateCondFn fn)
{
	if (StateDesc* d = EditableDesc()) { d->canEnter = fn; }
	return *this;
}

StateBuilder& StateBuilder::Behavior(const StateBehavior& behavior)
{
	if (StateDesc* d = EditableDesc())
	{
		d->onEnter = behavior.onEnter;
		d->onUpdate = behavior.onUpdate;
		d->onExit = behavior.onExit;
		d->canEnter = behavior.canEnter;
	}
	return *this;
}

StateBuilder& StateBuilder::AllowFrom(std::initializer_list<StateId> ids)
{
	if (StateDesc* d = EditableDesc()) { d->allowFrom.assign(ids.begin(), ids.end()); }
	return *this;
}

StateBuilder& StateBuilder::BlockFrom(std::initializer_list<StateId> ids)
{
	if (StateDesc* d = EditableDesc()) { d->blockFrom.assign(ids.begin(), ids.end()); }
	return *this;
}

StateBuilder& StateBuilder::BlockTag(uint32_t mask)
{
	if (StateDesc* d = EditableDesc()) { d->blockTagMask = mask; }
	return *this;
}

StateBuilder& StateBuilder::MinTime(float seconds)
{
	if (StateDesc* d = EditableDesc()) { d->minTime = seconds; }
	return *this;
}

StateBuilder& StateBuilder::Buffer(float seconds)
{
	if (StateDesc* d = EditableDesc()) { d->bufferTime = seconds; }
	return *this;
}

StateBuilder& StateBuilder::Param(std::string_view key, float value)
{
	if (StateDesc* d = EditableDesc()) { d->params.SetFloat(key, value); }
	return *this;
}

StateBuilder& StateBuilder::Param(std::string_view key, std::string_view value)
{
	if (StateDesc* d = EditableDesc()) { d->params.SetString(key, std::string(value)); }
	return *this;
}

StateBuilder& StateBuilder::Anim(std::string_view animName, float blendFrame, bool loop)
{
	if (StateDesc* d = EditableDesc())
	{
		d->params.SetString(SMParamKey::kAnim, std::string(animName));
		d->params.SetFloat(SMParamKey::kBlend, blendFrame);
		d->params.SetFloat(SMParamKey::kLoop, loop ? 1.0f : 0.0f);
	}
	return *this;
}

StateBuilder& StateBuilder::AnimSpeed(float speed)
{
	if (StateDesc* d = EditableDesc()) { d->params.SetFloat(SMParamKey::kSpeed, speed); }
	return *this;
}

StateBuilder& StateBuilder::OnEnd(std::string_view nextStateName)
{
	if (StateDesc* d = EditableDesc()) { d->params.SetString(SMParamKey::kOnEnd, std::string(nextStateName)); }
	return *this;
}

// ===========================================================
// StateMachineSystem : 調整値
// ===========================================================
void StateMachineSystem::SetConfig(const StateMachineConfig& config)
{
	m_config = config;

	// 履歴の上限を下げたときは、今ある分も切り詰めておく
	for (auto& [key, track] : m_histories)
	{
		if (track.entries.size() > m_config.historyMax) track.entries.resize(m_config.historyMax);
	}
}

// ===========================================================
// StateMachineSystem : 定義の登録
// ===========================================================
uint16_t StateMachineSystem::Register(std::unique_ptr<StateDesc> desc)
{
	if (!desc) return SM::kInvalidStateIndex;

	// index は uint16_t で、最大値は「無効」の印に使っている。そこまで埋まったら受け付けない
	if (m_states.size() >= static_cast<size_t>(SM::kInvalidStateIndex))
	{
		assert(false && "StateMachineSystem: ステート数が上限を超えました");
		return SM::kInvalidStateIndex;
	}

	const StateId id = MakeStateId(desc->name);

	// 同名の再登録は上書きせず既存を返す
	if (auto it = m_lookup.find(id); it != m_lookup.end())
	{
		// 名前が違うのにIDが同じ = ハッシュの衝突。同名登録とは原因が違うので分けて知らせる
		if (m_states[it->second]->name != desc->name)
			assert(false && "StateMachineSystem: 別名のステートとIDが衝突しています。名前を変えてください");
		else
			assert(false && "StateMachineSystem: 同名のステートが既に登録されています");
		return it->second;
	}

	desc->id = id;
	desc->index = static_cast<uint16_t>(m_states.size());

	if (desc->baseName.empty())
	{
		// ベースステート。自分自身をベースとして指す。
		desc->baseId = id;
		desc->baseIndex = desc->index;
	}
	else
	{
		const uint16_t baseIndex = Find(desc->baseName);
		if (baseIndex == SM::kInvalidStateIndex)
		{
			assert(false && "StateMachineSystem: ベースステートが先に登録されていません");
			return SM::kInvalidStateIndex;
		}

		desc->baseId = MakeStateId(desc->baseName);
		desc->baseIndex = baseIndex;

		if (StateDesc* base = GetMutable(baseIndex))
		{
			// 階層は2段まで。ベースのベースは自分自身であること。
			//assert(base->IsBase() && "StateMachineSystem: 階層は2段までです");

			// ベースのタグを子へ継承する。
			// 「Actionベースの子は全てAction扱い」を成立させるため。
			// blockTag はこの継承後のタグで判定される。
			desc->tag |= base->tag;

			// ベース側の既定の子が未設定なら、最初の子を既定にする
			if (base->defaultChild == SM::kInvalidStateIndex)
				base->defaultChild = desc->index;
		}
	}

	const uint16_t index = desc->index;
	const bool isChild = !desc->baseName.empty();

	m_lookup[id] = index;
	m_states.push_back(std::move(desc));

	// ステートが増えたので、作ってあったビットマスクはもう古い。
	// 古いまま使うと、新しいステートの allowFrom が反映されず遷移が弾かれてしまう
	m_guardMasksReady = false;

	// 最初に登録された子ステートを暫定の初期ステートにしておく
	if (m_entryIndex == SM::kInvalidStateIndex && isChild) m_entryIndex = index;

	return index;
}

void StateMachineSystem::RegisterBehavior(std::string_view key, const StateBehavior& behavior)
{
	m_behaviors[MakeStateId(key)] = behavior;
}

const StateBehavior* StateMachineSystem::FindBehavior(StateId key) const
{
	auto it = m_behaviors.find(key);
	return (it != m_behaviors.end()) ? &it->second : nullptr;
}

uint16_t StateMachineSystem::Find(StateId id) const
{
	auto it = m_lookup.find(id);
	return (it != m_lookup.end()) ? it->second : SM::kInvalidStateIndex;
}

void StateMachineSystem::SetDefaultChild(std::string_view baseName, std::string_view childName)
{
	const uint16_t baseIndex = Find(baseName);
	const uint16_t childIndex = Find(childName);
	if (baseIndex == SM::kInvalidStateIndex || childIndex == SM::kInvalidStateIndex)
	{
		assert(false && "StateMachineSystem: SetDefaultChild の名前が見つかりません");
		return;
	}

	if (StateDesc* base = GetMutable(baseIndex)) base->defaultChild = childIndex;
}

void StateMachineSystem::SetEntryState(std::string_view name)
{
	// 見つからなくても今までどおり無効値は入れる(Validate で報告される)。
	// ただ、名前の打ち間違いはここで気づけたほうが早いので assert も出す
	m_entryIndex = Find(name);
	assert(m_entryIndex != SM::kInvalidStateIndex && "StateMachineSystem: 初期ステートが見つかりません");
}

void StateMachineSystem::ClearStates()
{
	m_states.clear();
	m_lookup.clear();
	m_entryIndex = SM::kInvalidStateIndex;
	m_histories.clear();
	m_guardMasksReady = false;	// 状態をやり直すので、次のValidate()まではフォールバック判定を使う
	m_missingAnims.clear();	// アニメ未検出キャッシュも作り直す
}

const char* StateMachineSystem::GetNameOf(StateId id) const
{
	const StateDesc* d = GetById(id);
	return d ? d->name.c_str() : "";
}

const char* StateMachineSystem::GetStateName(const StateMachineComponent& sm) const
{
	const StateDesc* d = Get(sm.stateIndex);
	return d ? d->name.c_str() : "";
}

const char* StateMachineSystem::GetBaseName(const StateMachineComponent& sm) const
{
	const StateDesc* d = Get(sm.baseIndex);
	return d ? d->name.c_str() : "";
}

// ビットマスクを作り直してからチェックする(セットアップ時に呼ばれる想定)
bool StateMachineSystem::Validate(std::string* outError)
{
	BuildGuardMasks();
	return std::as_const(*this).Validate(outError);
}

// チェックだけ。何も書き換えない
bool StateMachineSystem::Validate(std::string* outError) const
{
	bool ok = true;

	auto report = [&](const std::string& msg)
		{
			ok = false;
			if (outError) { *outError += "[" + m_name + "] " + msg + "\n"; }
		};

	if (m_states.empty()) report("ステートが1つも登録されていません");
	if (m_entryIndex == SM::kInvalidStateIndex) report("初期ステート(entry)が設定されていません");

	for (const auto& up : m_states)
	{
		const StateDesc& d = *up;

		if (d.IsBase())
		{
			if (d.defaultChild == SM::kInvalidStateIndex)
				report("ベース '" + d.name + "' に子ステートがありません");
			continue;
		}

		if (d.baseIndex == SM::kInvalidStateIndex)
			report("'" + d.name + "' のベース '" + d.baseName + "' が見つかりません");

		auto checkList = [&](const std::vector<StateId>& list, const char* label)
			{
				for (StateId id : list)
				{
					if (Find(id) == SM::kInvalidStateIndex)
						report("'" + d.name + "' の" + label + "に未登録IDがあります");
				}
			};
		checkList(d.allowFrom, "allowFrom");
		checkList(d.blockFrom, "blockFrom");

		const char* onEnd = d.params.GetString(SMParamKey::OnEndId(), "");
		if (onEnd[0] != '\0' && Find(std::string_view(onEnd)) == SM::kInvalidStateIndex)
			report("'" + d.name + "' のonEnd '" + onEnd + "' が見つかりません");
	}

	return ok;
}

void StateMachineSystem::BuildGuardMasks()
{
	// uint64_t に収まらない数のステートは諦める。CheckGuard側は m_guardMasksReady を見て
	// 従来のvector線形探索(IsAllowedFrom)にフォールバックする。
	if (m_states.size() > SMConst::kGuardMaskBits)
	{
		m_guardMasksReady = false;
		return;
	}

	// id のリストを、index のビットを立てたマスクに変換する
	auto toMask = [this](const std::vector<StateId>& ids)
		{
			uint64_t mask = 0;
			for (StateId id : ids)
			{
				const uint16_t idx = Find(id);
				if (idx != SM::kInvalidStateIndex) mask |= (1ull << idx);
			}
			return mask;
		};

	for (auto& up : m_states)
	{
		if (!up) continue;
		up->allowFromBits = toMask(up->allowFrom);
		up->blockFromBits = toMask(up->blockFrom);
	}

	m_guardMasksReady = true;
}

// ===========================================================
// JSON
// ここは手書きのファイルを読むので、型が違っても例外を出さずに読み飛ばす。
// (前は j["name"].get<std::string>() などで、型違い1つで例外が飛んで落ちていた)
// ===========================================================
namespace
{
	// 文字列のキーを読む。無い・文字列でないときは false
	template<class JsonT>
	bool ReadString(const JsonT& j, const char* key, std::string& out)
	{
		const auto it = j.find(key);
		if (it == j.end() || !it->is_string()) return false;
		out = it->template get<std::string>();
		return true;
	}

	template<class JsonT>
	float ReadFloat(const JsonT& j, const char* key, float defaultValue)
	{
		const auto it = j.find(key);
		if (it == j.end() || !it->is_number()) return defaultValue;
		return it->template get<float>();
	}

	// ビットフラグ用。負の数は意味を持たないので既定値にする
	template<class JsonT>
	uint32_t ReadFlags(const JsonT& j, const char* key, uint32_t defaultValue)
	{
		const auto it = j.find(key);
		if (it == j.end()) return defaultValue;

		if (it->is_number_unsigned()) return it->template get<uint32_t>();

		if (it->is_number_integer() && it->template get<int64_t>() >= 0)
			return static_cast<uint32_t>(it->template get<int64_t>());

		assert(false && "StateMachineSystem: タグには0以上の整数を書いてください");
		return defaultValue;
	}

	template<class JsonT>
	void ReadIdList(const JsonT& j, const char* key, std::vector<StateId>& out)
	{
		const auto it = j.find(key);
		if (it == j.end() || !it->is_array()) return;

		for (const auto& e : *it)
		{
			if (e.is_string()) out.push_back(MakeStateId(e.template get<std::string>()));
		}
	}

	template<class JsonT>
	void ReadParams(const JsonT& j, StateParams& out)
	{
		const auto it = j.find("params");
		if (it == j.end() || !it->is_object()) return;

		// boolはfloatの0/1として持つ
		for (auto p = it->begin(); p != it->end(); ++p)
		{
			const auto& v = p.value();

			if (v.is_boolean())     out.SetFloat(p.key(), v.template get<bool>() ? 1.0f : 0.0f);
			else if (v.is_number()) out.SetFloat(p.key(), v.template get<float>());
			else if (v.is_string()) out.SetString(p.key(), v.template get<std::string>());
		}
	}
}

template<class JsonT>
bool StateMachineSystem::LoadFromJson(const JsonT& root)
{
	if (!root.is_object()) return false;

	const auto statesIt = root.find("states");
	if (statesIt == root.end() || !statesIt->is_array()) return false;

	// ベースを先に登録する必要があるため2パスで回す。
	// 1周目: baseキーが無い(=ベース) / 2周目: 子
	constexpr int kPassBase = 0;
	constexpr int kPassCount = 2;

	for (int pass = 0; pass < kPassCount; ++pass)
	{
		for (const auto& j : *statesIt)
		{
			if (!j.is_object()) continue;

			std::string name;
			if (!ReadString(j, "name", name)) continue;

			std::string baseName;
			ReadString(j, "base", baseName);

			const bool isBase = baseName.empty();
			if ((pass == kPassBase) != isBase) continue;

			auto desc = std::make_unique<StateDesc>();
			desc->name = name;
			desc->baseName = baseName;
			desc->tag = ReadFlags(j, "tag", 0u);
			desc->blockTagMask = ReadFlags(j, "blockTag", 0u);
			desc->minTime = ReadFloat(j, "minTime", 0.0f);
			desc->bufferTime = ReadFloat(j, "buffer", SMConst::kInfiniteBuffer);

			// 振る舞いは RegisterBehavior() 済みのキーから引く
			std::string behaviorKey;
			if (ReadString(j, "behavior", behaviorKey))
			{
				if (const StateBehavior* b = FindBehavior(MakeStateId(behaviorKey)))
				{
					desc->onEnter = b->onEnter;
					desc->onUpdate = b->onUpdate;
					desc->onExit = b->onExit;
					desc->canEnter = b->canEnter;
				}
				else
				{
					assert(false && "StateMachineSystem: JSONのbehaviorキーが未登録です");
				}
			}

			ReadIdList(j, "allowFrom", desc->allowFrom);
			ReadIdList(j, "blockFrom", desc->blockFrom);
			ReadParams(j, desc->params);

			Register(std::move(desc));
		}
	}

	const auto childrenIt = root.find("defaultChildren");
	if (childrenIt != root.end() && childrenIt->is_object())
	{
		for (auto it = childrenIt->begin(); it != childrenIt->end(); ++it)
		{
			if (it.value().is_string())
				SetDefaultChild(it.key(), it.value().template get<std::string>());
		}
	}

	std::string entryName;
	if (ReadString(root, "entry", entryName)) SetEntryState(entryName);

	return true;
}

bool StateMachineSystem::LoadFromJsonText(std::string_view text)
{
	auto j = nlohmann::json::parse(text, nullptr, false);	// 例外を投げない
	if (j.is_discarded())
	{
		assert(false && "StateMachineSystem: JSONのパースに失敗しました");
		return false;
	}
	return LoadFromJson(j);
}

template<class JsonT>
bool StateMachineSystem::ReloadFromJson(const JsonT& root)
{
	ClearStates();
	return LoadFromJson(root);
}

// ---- 明示的実体化 ----
// ヘッダをnlohmann非依存にした代償として、使う型をここで宣言する必要がある。
// ordered_json 等を使う場合はその型も追加すること。
template bool StateMachineSystem::LoadFromJson<nlohmann::json>(const nlohmann::json&);
template bool StateMachineSystem::ReloadFromJson<nlohmann::json>(const nlohmann::json&);

// ===========================================================
// コンポーネント操作
// ===========================================================
StateMachineComponent StateMachineSystem::MakeComponent() const
{
	StateMachineComponent sm;
	sm.systemId = m_id;
	Reset(sm);
	return sm;
}

void StateMachineSystem::Reset(StateMachineComponent& sm) const
{
	sm.systemId = m_id;
	sm.baseIndex = SM::kInvalidStateIndex;
	sm.stateIndex = SM::kInvalidStateIndex;
	sm.baseId = SM::kInvalidStateId;
	sm.stateId = SM::kInvalidStateId;
	sm.prevStateId = SM::kInvalidStateId;
	sm.stateTag = 0;
	sm.transitionCount = 0;
	sm.stateTime = 0.0f;
	sm.baseTime = 0.0f;
	sm.lastResult = 0;
	sm.animSyncCount = SMConst::kUnsetCount;	// 初回の SyncAnimation で必ず同期させる
	sm.animEndFired = 0;
	sm.ClearEvents();
	sm.CancelReserved();

	if (m_entryIndex != SM::kInvalidStateIndex)
	{
		sm.reservedIndex = m_entryIndex;
		sm.reservedId = m_states[m_entryIndex]->id;
		sm.reservedForce = 1;		// 初期遷移はガードを通さない
		sm.reservedPriority = SMConst::kForcedPriority;
		sm.reservedTimer = SMConst::kInfiniteBuffer;
	}
}

uint16_t StateMachineSystem::ResolveTarget(uint16_t index) const
{
	const StateDesc* d = Get(index);
	if (!d) return SM::kInvalidStateIndex;

	// ベースが直接指定された場合は既定の子へ読み替える
	if (d->IsBase() && d->defaultChild != SM::kInvalidStateIndex) return d->defaultChild;

	return index;
}

// -----------------------------------------------------------
// 構造的なガード判定。canEnter以外の全てをここで見る。
// Deferred = 今は無理だが待てば通る / Rejected = 待っても無理
// -----------------------------------------------------------
StateResult StateMachineSystem::CheckGuard(const StateMachineComponent& sm, uint16_t targetIndex) const
{
	const StateDesc* target = Get(targetIndex);
	if (!target) return StateResult::RejectedUnknown;

	if (targetIndex == sm.stateIndex) return StateResult::RejectedSameState;

	const StateDesc* cur = Get(sm.stateIndex);
	if (!cur) return StateResult::Accepted;	// まだどのステートにも入っていない(初期遷移前)

	if (m_guardMasksReady && cur->index < SMConst::kGuardMaskBits)
	{
		const uint64_t curBit = (1ull << cur->index);

		// blockFromに含まれる = 1ビット立っていれば禁止
		if ((target->blockFromBits & curBit) != 0u) return StateResult::RejectedForbidden;

		// allowFromが空なら全許可(ビットマスクも空のままなので、
		// 「実際に指定されているか」はvectorのemptyで判定する)
		if (!target->allowFrom.empty() && (target->allowFromBits & curBit) == 0u)
			return StateResult::RejectedForbidden;
	}
	else
	{
		// マスクが無いとき(Validate 前・64ステート超え)のフォールバック(線形探索)
		if (!target->IsAllowedFrom(cur->id)) return StateResult::RejectedForbidden;
	}

	if ((cur->tag & target->blockTagMask) != 0u) return StateResult::RejectedForbidden;

	// 現在ステートの最低滞在時間。時間が経てば通るのでDeferred。
	if (cur->minTime > 0.0f && sm.stateTime < cur->minTime) return StateResult::Deferred;

	return StateResult::Accepted;
}

StateResult StateMachineSystem::Reserve(StateMachineComponent& sm, uint16_t index,
	uint8_t priority, float bufferTime, bool force) const
{
	auto store = [&sm](StateResult r) { sm.lastResult = static_cast<uint8_t>(r); return r; };

	if (index == SM::kInvalidStateIndex)
	{
		assert(false && "StateMachineSystem: 未登録のStateIDです");
		return store(StateResult::RejectedUnknown);
	}

	StateResult result = StateResult::Accepted;

	if (!force)
	{
		result = CheckGuard(sm, index);

		// 待っても通らないものは予約に積まない
		if (result != StateResult::Accepted && result != StateResult::Deferred)
			return store(result);

		// 既存の予約より優先度が低ければ上書きさせない
		if (sm.HasReserved() && priority < sm.reservedPriority)
			return store(StateResult::RejectedPriority);
	}

	sm.reservedIndex = index;
	sm.reservedId = m_states[index]->id;
	sm.reservedPriority = priority;
	sm.reservedForce = force ? 1 : 0;
	sm.reservedTimer = bufferTime;

	return store(result);
}

StateResult StateMachineSystem::ChangeState(StateMachineComponent& sm,
	StateId id, uint8_t priority) const
{
	const uint16_t index = ResolveTarget(Find(id));
	const StateDesc* target = Get(index);
	const float buffer = target ? target->bufferTime : SMConst::kInfiniteBuffer;

	return Reserve(sm, index, priority, buffer, false);
}

StateResult StateMachineSystem::ChangeStateBuffered(StateMachineComponent& sm,
	StateId id, float bufferTime, uint8_t priority) const
{
	return Reserve(sm, ResolveTarget(Find(id)), priority, bufferTime, false);
}

StateResult StateMachineSystem::ChangeStateForced(StateMachineComponent& sm, StateId id) const
{
	return Reserve(sm, ResolveTarget(Find(id)), SMConst::kForcedPriority, SMConst::kInfiniteBuffer, true);
}

StateResult StateMachineSystem::IsChangePossible(const StateMachineComponent& sm, StateId id) const
{
	return CheckGuard(sm, ResolveTarget(Find(id)));
}

float StateMachineSystem::FindParam(const StateMachineComponent& sm,
	StateId key, float defaultValue) const
{
	if (const StateDesc* d = Get(sm.stateIndex))
	{
		if (d->params.HasFloat(key)) return d->params.GetFloat(key);
	}
	if (const StateDesc* b = Get(sm.baseIndex))
	{
		if (b->params.HasFloat(key)) return b->params.GetFloat(key);
	}
	return defaultValue;
}

const char* StateMachineSystem::FindParamStr(const StateMachineComponent& sm,
	StateId key, const char* defaultValue) const
{
	if (const StateDesc* d = Get(sm.stateIndex))
	{
		if (d->params.HasString(key)) return d->params.GetString(key);
	}
	if (const StateDesc* b = Get(sm.baseIndex))
	{
		if (b->params.HasString(key)) return b->params.GetString(key);
	}
	return defaultValue;
}

// ===========================================================
// 更新
// ===========================================================
void StateMachineSystem::Invoke(StateFn fn, StateMachineComponent& sm,
	const StateDesc* desc, void* pUser, uint32_t entity, float deltaTime)
{
	if (!fn) return;

	StateContext ctx;
	ctx.pMachine = &sm;
	ctx.pSystem = this;
	ctx.pDesc = desc;
	ctx.pUser = pUser;
	ctx.entity = entity;
	ctx.deltaTime = deltaTime;	// 前は引数で受け取るだけで ctx に渡していなかった
	fn(ctx);
}

bool StateMachineSystem::EvalCanEnter(StateMachineComponent& sm,
	const StateDesc* desc, void* pUser, uint32_t entity)
{
	if (!desc || !desc->canEnter) return true;

	StateContext ctx;
	ctx.pMachine = &sm;
	ctx.pSystem = this;
	ctx.pDesc = desc;
	ctx.pUser = pUser;
	ctx.entity = entity;
	return desc->canEnter(ctx);
}

void StateMachineSystem::Update(StateMachineComponent& sm, float deltaTime,
	void* pUser, uint32_t entity)
{
	if (sm.systemId != m_id)
	{
		assert(false && "StateMachineSystem: 別システムのコンポーネントです");
		return;
	}

	// 1. 前フレームまでに積まれた予約を確定させる
	TryApplyReserved(sm, pUser, entity);

	// 2. 通らなかった予約の寿命を削る
	if (sm.HasReserved() && sm.reservedTimer >= 0.0f)
	{
		sm.reservedTimer -= deltaTime;
		if (sm.reservedTimer <= 0.0f) sm.CancelReserved();
	}

	// 3. 時間を進める
	sm.stateTime += deltaTime;
	sm.baseTime += deltaTime;

	// 4. ベースの OnUpdate(子より先に走る。割り込み判定はここに置く)
	//    このフレームでベースが新たに予約したかを見るため、直前の予約先を控える。
	const uint16_t reservedBefore = sm.reservedIndex;

	if (const StateDesc* base = Get(sm.baseIndex))
	{
		Invoke(base->onUpdate, sm, base, pUser, entity, deltaTime);
	}

	// 5. 子の OnUpdate
	//    スキップするのは「ベースがこのフレームで予約を積んだ」場合だけ。
	//    前フレームから持ち越された Deferred 予約でスキップしてしまうと、
	//    条件が整うまで子の処理(アニメイベント等)が丸ごと止まってしまう。
	const bool baseRequested = sm.HasReserved() && (sm.reservedIndex != reservedBefore);

	if (!baseRequested && sm.stateIndex != sm.baseIndex)
	{
		if (const StateDesc* state = Get(sm.stateIndex))
		{
			Invoke(state->onUpdate, sm, state, pUser, entity, deltaTime);
		}
	}

	// 6. 同フレーム内で積まれた予約を確定させる
	//    (Enterまでを同フレームで済ませ、1フレームの遅れを作らない)
	TryApplyReserved(sm, pUser, entity);

	// 7. イベントの寿命は1フレーム
	sm.ClearEvents();
}

void StateMachineSystem::TryApplyReserved(StateMachineComponent& sm, void* pUser, uint32_t entity)
{
	// Enter の中からさらに ChangeState されることがあるので、予約が尽きるまで回す。
	// ただし相互遷移で無限ループしないよう、1回あたりの遷移数に上限をつける。
	//
	// 前は「while (予約あり && guard-- > 0)」のあとで guard > 0 を assert していたので、
	// ちょうど上限の回数で収束したときにも assert が出ていた。回数は別で数える
	int appliedCount = 0;

	while (sm.HasReserved())
	{
		if (appliedCount >= m_config.maxTransitionsPerFrame)
		{
			// 予約は残したまま抜ける(次の呼び出しで続きから)
			assert(false && "StateMachineSystem: 遷移が収束しません(相互遷移の疑い)");
			return;
		}

		const uint16_t target = sm.reservedIndex;

		if (!sm.reservedForce)
		{
			const StateResult r = CheckGuard(sm, target);

			// 今は通らないが待てば通る → 予約を保持したまま抜ける
			if (r == StateResult::Deferred)
			{
				sm.lastResult = static_cast<uint8_t>(r);
				return;
			}

			// 状況が変わって永久に通らなくなった → 予約を捨てる
			if (r != StateResult::Accepted)
			{
				sm.lastResult = static_cast<uint8_t>(r);
				sm.CancelReserved();
				return;
			}

			if (!EvalCanEnter(sm, Get(target), pUser, entity))
			{
				sm.lastResult = static_cast<uint8_t>(StateResult::Deferred);
				return;
			}
		}

		sm.CancelReserved();
		DoTransition(sm, target, pUser, entity);
		sm.lastResult = static_cast<uint8_t>(StateResult::Accepted);
		++appliedCount;
	}
}

void StateMachineSystem::DoTransition(StateMachineComponent& sm, uint16_t nextIndex,
	void* pUser, uint32_t entity)
{
	const StateDesc* next = Get(nextIndex);
	if (!next) return;

	const uint16_t nextBase = next->baseIndex;
	const bool baseChanged = (nextBase != sm.baseIndex);

	// ---- Exit : 子 → ベース の順 ----
	if (const StateDesc* cur = Get(sm.stateIndex))
	{
		sm.prevStateId = cur->id;
		if (sm.stateIndex != sm.baseIndex)
			Invoke(cur->onExit, sm, cur, pUser, entity, 0.0f);
	}

	if (baseChanged)
	{
		if (const StateDesc* curBase = Get(sm.baseIndex))
			Invoke(curBase->onExit, sm, curBase, pUser, entity, 0.0f);
	}

	// ---- Enter : ベース → 子 の順 ----
	if (baseChanged)
	{
		sm.baseIndex = nextBase;
		sm.baseId = next->baseId;
		sm.baseTime = 0.0f;

		if (const StateDesc* newBase = Get(sm.baseIndex))
			Invoke(newBase->onEnter, sm, newBase, pUser, entity, 0.0f);
	}

	sm.stateIndex = nextIndex;
	sm.stateId = next->id;
	sm.stateTag = next->tag;
	sm.stateTime = 0.0f;
	++sm.transitionCount;

	if (sm.stateIndex != sm.baseIndex)
		Invoke(next->onEnter, sm, next, pUser, entity, 0.0f);
}


// ===========================================================
// デバッグ
// ===========================================================
namespace
{
	ImVec4 ToImVec4(const StateMachineConfig::Color& c)
	{
		return ImVec4(c[0], c[1], c[2], c[3]);
	}

	// ImGui の決まりで、幅に負の値を渡すと「残りの幅いっぱい」になる
	constexpr float kFillRemainingWidth = -1.0f;
}

void StateMachineSystem::RegisterEventName(std::string_view name)
{
	m_eventNames[MakeStateId(name)] = std::string(name);
}

const char* StateMachineSystem::GetEventName(StateId id) const
{
	auto it = m_eventNames.find(id);
	return (it != m_eventNames.end()) ? it->second.c_str() : nullptr;
}

void StateMachineSystem::ClearHistory(uint32_t historyKey)
{
	m_histories.erase(historyKey);
}

void StateMachineSystem::RecordHistory(const StateMachineComponent& sm, uint32_t key)
{
	HistoryTrack& track = m_histories[key];

	// 初回は履歴に積まず、基準だけ取る
	if (track.lastTransition == SMConst::kUnsetCount)
	{
		track.lastTransition = sm.transitionCount;
		track.lastBaseName = GetBaseName(sm);
		track.lastStateName = GetStateName(sm);
		track.lastStateTime = sm.stateTime;
		return;
	}

	if (track.lastTransition != sm.transitionCount)
	{
		HistoryEntry e;
		e.baseName = track.lastBaseName;
		e.stateName = track.lastStateName;
		e.duration = track.lastStateTime;
		e.order = track.lastTransition;

		track.entries.insert(track.entries.begin(), std::move(e));
		if (track.entries.size() > m_config.historyMax) track.entries.resize(m_config.historyMax);

		track.lastTransition = sm.transitionCount;
		track.lastBaseName = GetBaseName(sm);
		track.lastStateName = GetStateName(sm);
	}

	track.lastStateTime = sm.stateTime;
}

void StateMachineSystem::DrawImGui(StateMachineComponent& sm, uint32_t historyKey,
	bool allowForceChange)
{
	// 履歴はウィンドウを畳んでいる間も取り続ける(開いたときに途中が抜けないように)
	RecordHistory(sm, historyKey);

	// Begin が false(畳まれている)なら中身は描かない。End はどちらでも必ず呼ぶ
	if (!ImGui::Begin(U8("ステート管理")))
	{
		ImGui::End();
		return;
	}

	ImGui::PushID(static_cast<int>(historyKey));

	DrawCurrentState(sm);
	DrawReservation(sm);
	ImGui::Separator();
	DrawEvents(sm);
	DrawGuardAndParams(sm);
	DrawHistory(historyKey);
	if (allowForceChange) DrawForceChange(sm);

	ImGui::PopID();
	ImGui::End();
}

// ---- 現在の状態 ----
void StateMachineSystem::DrawCurrentState(const StateMachineComponent& sm)
{
	const ImVec4 colorBase = ToImVec4(m_config.colorBase);
	const ImVec4 colorState = ToImVec4(m_config.colorState);
	const ImVec4 colorDim = ToImVec4(m_config.colorDim);
	const ImVec4 colorWarn = ToImVec4(m_config.colorWarn);

	ImGui::TextColored(colorBase, "Base  : %s", GetBaseName(sm));
	ImGui::SameLine();
	ImGui::TextColored(colorDim, "(%.2fs)", sm.GetBaseTime());

	ImGui::TextColored(colorState, "State : %s", GetStateName(sm));
	ImGui::SameLine();
	ImGui::TextColored(colorDim, "(%.2fs)", sm.GetStateTime());

	ImGui::TextColored(colorDim, "Prev  : %s", GetNameOf(sm.GetPrevState()));
	ImGui::Text("Transitions : %u  /  Tag : 0x%08X",
		static_cast<unsigned>(sm.GetTransitionCount()), static_cast<unsigned>(sm.GetTag()));
	ImGui::Text("LastResult  : %s", ToString(sm.GetLastResult()));

	// minTime によるロック中は残り時間をバーで見せる
	if (const StateDesc* cur = Get(sm.stateIndex))
	{
		if (cur->minTime > 0.0f && sm.stateTime < cur->minTime)
		{
			// 日本語なので U8 を通す(前は付いておらず文字化けしていた)
			ImGui::TextColored(colorWarn, U8("minTime ロック中"));
			ImGui::SameLine();
			ImGui::ProgressBar(sm.stateTime / cur->minTime, ImVec2(kFillRemainingWidth, 0.0f));
		}
	}
}

// ---- 予約 ----
void StateMachineSystem::DrawReservation(StateMachineComponent& sm)
{
	const ImVec4 colorDim = ToImVec4(m_config.colorDim);
	const ImVec4 colorEvent = ToImVec4(m_config.colorEvent);

	if (!sm.HasReserved())
	{
		ImGui::TextColored(colorDim, U8("Next  : (なし)"));
		return;
	}

	ImGui::TextColored(colorEvent, U8("Next  : %s"), GetNameOf(sm.GetReservedState()));
	ImGui::SameLine();

	// %u に uint8_t をそのまま渡すと int に昇格して型が合わないので unsigned にしておく
	const unsigned priority = static_cast<unsigned>(sm.reservedPriority);
	if (sm.GetReservedTimer() < 0.0f)
		ImGui::TextColored(colorDim, U8("[無期限 / prio %u]"), priority);
	else
		ImGui::TextColored(colorDim, U8("[残り %.2fs / prio %u]"), sm.GetReservedTimer(), priority);

	ImGui::SameLine();
	if (ImGui::SmallButton(U8("予約取消"))) sm.CancelReserved();
}

// ---- イベント ----
void StateMachineSystem::DrawEvents(const StateMachineComponent& sm)
{
	const ImVec4 colorDim = ToImVec4(m_config.colorDim);
	const ImVec4 colorEvent = ToImVec4(m_config.colorEvent);

	if (sm.eventCount == 0)
	{
		ImGui::TextColored(colorDim, "Events : none");
		return;
	}

	ImGui::TextColored(colorEvent, "Events : %d", static_cast<int>(sm.eventCount));
	for (uint8_t i = 0; i < sm.eventCount; ++i)
	{
		ImGui::SameLine();
		if (const char* n = GetEventName(sm.eventBuffer[i]))
			ImGui::TextColored(colorEvent, "%s", n);
		else
			ImGui::TextColored(colorEvent, "[%08X]", static_cast<unsigned>(sm.eventBuffer[i]));
	}
}

// ---- ガード設定とパラメータ ----
void StateMachineSystem::DrawGuardAndParams(const StateMachineComponent& sm)
{
	const StateDesc* d = Get(sm.stateIndex);
	if (!d) return;

	const ImVec4 colorDim = ToImVec4(m_config.colorDim);

	if (ImGui::TreeNode(U8("ガード設定")))
	{
		ImGui::Text(U8("minTime  : %.3f s"), d->minTime);

		if (d->bufferTime < 0.0f) ImGui::Text(U8("buffer   : 無期限"));
		else                      ImGui::Text(U8("buffer   : %.3f s"), d->bufferTime);

		ImGui::Text("blockTag : 0x%08X", static_cast<unsigned>(d->blockTagMask));

		if (d->allowFrom.empty()) ImGui::TextColored(colorDim, U8("allowFrom: (全て許可)"));
		else                      ImGui::Text(U8("allowFrom: %zu 件"), d->allowFrom.size());

		if (!d->blockFrom.empty()) ImGui::Text(U8("blockFrom: %zu 件"), d->blockFrom.size());

		ImGui::Text(U8("canEnter : %s"), d->canEnter ? U8("あり") : U8("なし"));
		ImGui::TreePop();
	}

	if (ImGui::TreeNode(U8("パラメータ")))
	{
		const StateParams& p = d->params;

		for (size_t i = 0; i < p.GetStringCount(); ++i)
			ImGui::Text("%-8s : %s", p.GetStringName(i), p.GetStringAt(i));

		for (size_t i = 0; i < p.GetFloatCount(); ++i)
			ImGui::Text("%-8s : %.3f", p.GetFloatName(i), p.GetFloatAt(i));

		if (p.GetStringCount() == 0 && p.GetFloatCount() == 0)
			ImGui::TextColored(colorDim, U8("(なし)"));

		ImGui::TreePop();
	}
}

// ---- 履歴 ----
void StateMachineSystem::DrawHistory(uint32_t historyKey)
{
	if (!ImGui::TreeNode(U8("履歴"))) return;

	const ImVec4 colorDim = ToImVec4(m_config.colorDim);

	// operator[] だと無いときに空の履歴を作ってしまうので find で引く
	const auto it = m_histories.find(historyKey);
	if (it == m_histories.end() || it->second.entries.empty())
	{
		ImGui::TextColored(colorDim, U8("(まだ遷移していません)"));
	}
	else
	{
		for (const auto& e : it->second.entries)
		{
			ImGui::Text("#%u  %s / %s", static_cast<unsigned>(e.order), e.baseName.c_str(), e.stateName.c_str());
			ImGui::SameLine();
			ImGui::TextColored(colorDim, "%.2fs", e.duration);
		}
	}

	// ボタンの処理は描画ループの外で。ループ中に消すと it が無効になる
	if (ImGui::Button(U8("履歴クリア"))) ClearHistory(historyKey);
	ImGui::TreePop();
}

// ---- 強制遷移 ----
void StateMachineSystem::DrawForceChange(StateMachineComponent& sm)
{
	if (!ImGui::TreeNode(U8("強制遷移"))) return;

	const ImVec4 colorState = ToImVec4(m_config.colorState);
	const ImVec4 colorDim = ToImVec4(m_config.colorDim);

	ImGui::TextColored(colorDim, U8("左クリック: 通常遷移 / 右クリック: ガード無視"));

	for (size_t i = 0; i < m_states.size(); ++i)
	{
		const StateDesc* d = m_states[i].get();
		if (!d) continue;

		// ベースはインデント無し、子は1段下げて階層を見せる
		const bool isChild = !d->IsBase();
		if (isChild) ImGui::Indent();

		// 今その遷移が通るかどうかで色を変える
		const StateResult can = IsChangePossible(sm, d->id);
		const bool ok = (can == StateResult::Accepted);

		ImGui::PushID(static_cast<int>(i));
		ImGui::PushStyleColor(ImGuiCol_Text, ok ? colorState : colorDim);

		if (ImGui::Button(d->name.c_str())) ChangeState(sm, d->id);

		ImGui::PopStyleColor();

		if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) ChangeStateForced(sm, d->id);
		if (ImGui::IsItemHovered() && !ok) ImGui::SetTooltip("%s", ToString(can));

		ImGui::PopID();

		if (isChild) ImGui::Unindent();
	}

	if (ImGui::Button(U8("現ステートを再突入"))) sm.RestartState();
	ImGui::TreePop();
}
