#include "StateMachineManager.h"
#include <algorithm>
#include <utility>

// ===========================================================
// 調整値
// ===========================================================
namespace
{
	// 設定ファイルで変えられる範囲。おかしな値が入っても動きが壊れないように丸める
	constexpr int    kMinTransitionsPerFrame = 1;		// 0 だと初期遷移すら通らない
	constexpr int    kMaxTransitionsPerFrame = 64;
	constexpr int    kMinHistory = 0;
	constexpr int    kMaxHistory = 256;				// これ以上は表示しても追えない
	constexpr float  kMinBlendFrame = 0.0f;
	constexpr size_t kColorChannels = 4;				// RGBA
	constexpr size_t kColorChannelsNoAlpha = 3;		// RGB だけ書かれていたら A は今の値のまま

	// オブジェクト直下の子オブジェクトを探す。無ければ nullptr
	template<class JsonT>
	const JsonT* FindObject(const JsonT& j, const char* key)
	{
		if (!j.is_object()) return nullptr;
		const auto it = j.find(key);
		return (it != j.end() && it->is_object()) ? &*it : nullptr;
	}

	// 数値を読む。数値でなければ何もしない
	template<class JsonT, class T>
	void ReadNumber(const JsonT* j, const char* key, T& inout)
	{
		if (!j) return;
		const auto it = j->find(key);
		if (it != j->end() && it->is_number()) inout = it->template get<T>();
	}

	template<class JsonT>
	void ReadBool(const JsonT* j, const char* key, bool& inout)
	{
		if (!j) return;
		const auto it = j->find(key);
		if (it != j->end() && it->is_boolean()) inout = it->template get<bool>();
	}

	// [r, g, b] か [r, g, b, a] を読む。0〜1 に丸める
	template<class JsonT>
	void ReadColor(const JsonT* j, const char* key, StateMachineConfig::Color& inout)
	{
		if (!j) return;
		const auto it = j->find(key);
		if (it == j->end() || !it->is_array()) return;
		if (it->size() != kColorChannels && it->size() != kColorChannelsNoAlpha) return;

		StateMachineConfig::Color color = inout;
		for (size_t i = 0; i < it->size(); ++i)
		{
			const auto& channel = (*it)[i];
			if (!channel.is_number()) return;	// 1つでも数値でなければ丸ごと無視
			color[i] = std::clamp(channel.template get<float>(), 0.0f, 1.0f);
		}
		inout = color;
	}
}

template<class JsonT>
void StateMachineManager::ApplyConfigJson(const JsonT& root)
{
	// 今の値をベースに、書いてある項目だけ上書きする
	StateMachineConfig config = m_config;

	const JsonT* transition = FindObject(root, "transition");
	const JsonT* animation = FindObject(root, "animation");
	const JsonT* debug = FindObject(root, "debug");
	const JsonT* colors = debug ? FindObject(*debug, "colors") : nullptr;

	// ---- 遷移 ----
	int maxTransitions = config.maxTransitionsPerFrame;
	ReadNumber(transition, "maxPerFrame", maxTransitions);
	config.maxTransitionsPerFrame = std::clamp(maxTransitions, kMinTransitionsPerFrame, kMaxTransitionsPerFrame);

	// ---- アニメーション ----
	ReadNumber(animation, "defaultBlendFrame", config.defaultBlendFrame);
	config.defaultBlendFrame = std::max(config.defaultBlendFrame, kMinBlendFrame);
	ReadNumber(animation, "defaultSpeed", config.defaultAnimSpeed);
	ReadBool(animation, "defaultLoop", config.defaultAnimLoop);

	// ---- デバッグ表示 ----
	// size_t に負の数を直接読むと巨大な値になるので、int で受けてから丸める
	int historyMax = static_cast<int>(config.historyMax);
	ReadNumber(debug, "historyMax", historyMax);
	config.historyMax = static_cast<size_t>(std::clamp(historyMax, kMinHistory, kMaxHistory));

	ReadColor(colors, "base", config.colorBase);
	ReadColor(colors, "state", config.colorState);
	ReadColor(colors, "dim", config.colorDim);
	ReadColor(colors, "event", config.colorEvent);
	ReadColor(colors, "warn", config.colorWarn);

	SetConfig(config);
}

// 使う型だけ明示的に実体化しておく(ヘッダを nlohmann 非依存にしているため)
template void StateMachineManager::ApplyConfigJson<nlohmann::json>(const nlohmann::json&);

void StateMachineManager::SetConfig(const StateMachineConfig& config)
{
	m_config = config;
	for (auto& up : m_systems)
	{
		if (up) up->SetConfig(m_config);
	}
}

// ===========================================================
// システムの作成・取得
// ===========================================================
StateMachineSystem* StateMachineManager::CreateSystem(std::string_view name)
{
	const StateId key = MakeStateId(name);

	// 既に同名があればそれを返す(二重登録の防止)
	if (auto it = m_lookup.find(key); it != m_lookup.end())
	{
		StateMachineSystem* existing = m_systems[it->second].get();

		// 名前が違うのに同じIDになった = ハッシュの衝突。黙って別のシステムを返すと原因が追えない
		assert(existing->GetName() == name && "StateMachineManager: 別名のシステムとIDが衝突しています");
		return existing;
	}

	// ID は uint16_t。最大値は無効値として使っているのでそこで打ち止め
	if (m_systems.size() >= static_cast<size_t>(kInvalidSystemId))
	{
		assert(false && "StateMachineManager: システム数が上限を超えました");
		return nullptr;
	}

	const uint16_t id = static_cast<uint16_t>(m_systems.size());

	auto sys = std::make_unique<StateMachineSystem>(name);
	sys->m_id = id;	// friend宣言によりここだけ触れる
	sys->SetConfig(m_config);

	StateMachineSystem* raw = sys.get();
	m_systems.push_back(std::move(sys));
	m_lookup[key] = id;

	return raw;
}

StateMachineSystem* StateMachineManager::Get(uint16_t systemId)
{
	return (systemId < m_systems.size()) ? m_systems[systemId].get() : nullptr;
}

const StateMachineSystem* StateMachineManager::Get(uint16_t systemId) const
{
	return (systemId < m_systems.size()) ? m_systems[systemId].get() : nullptr;
}

uint16_t StateMachineManager::FindId(std::string_view name) const
{
	auto it = m_lookup.find(MakeStateId(name));
	return (it != m_lookup.end()) ? it->second : kInvalidSystemId;
}

// ===========================================================
// コンポーネント操作のショートカット
// ===========================================================
StateMachineComponent StateMachineManager::MakeComponent(std::string_view systemName) const
{
	return MakeComponent(FindId(systemName));
}

StateMachineComponent StateMachineManager::MakeComponent(uint16_t systemId) const
{
	if (const StateMachineSystem* sys = Get(systemId)) return sys->MakeComponent();

	assert(false && "StateMachineManager: 未登録のシステムです");
	return StateMachineComponent{};
}

void StateMachineManager::Update(StateMachineComponent& sm, float deltaTime,
	void* pUser, uint32_t entity)
{
	if (StateMachineSystem* sys = Get(sm.systemId))
		sys->Update(sm, deltaTime, pUser, entity);
}

StateResult StateMachineManager::ChangeState(StateMachineComponent& sm,
	StateId id, uint8_t priority)
{
	if (StateMachineSystem* sys = Get(sm.systemId))
		return sys->ChangeState(sm, id, priority);

	return StateResult::RejectedUnknown;
}

StateResult StateMachineManager::ChangeStateForced(StateMachineComponent& sm, StateId id)
{
	if (StateMachineSystem* sys = Get(sm.systemId))
		return sys->ChangeStateForced(sm, id);

	return StateResult::RejectedUnknown;
}

bool StateMachineManager::TryChangeState(StateMachineComponent& sm,
	StateId id, uint8_t priority)
{
	return IsAccepted(ChangeState(sm, id, priority));
}

const char* StateMachineManager::GetStateName(const StateMachineComponent& sm) const
{
	const StateMachineSystem* sys = Get(sm.systemId);
	return sys ? sys->GetStateName(sm) : "";
}

const char* StateMachineManager::GetBaseName(const StateMachineComponent& sm) const
{
	const StateMachineSystem* sys = Get(sm.systemId);
	return sys ? sys->GetBaseName(sm) : "";
}

// ===========================================================
// 一括処理
// ===========================================================
bool StateMachineManager::ValidateAll(std::string* outError)
{
	bool ok = true;

	// 途中で失敗しても最後まで回して、全部のエラーを一度に出す
	for (auto& up : m_systems)
	{
		if (!up) continue;
		if (!up->Validate(outError)) ok = false;
	}

	return ok;
}

bool StateMachineManager::ValidateAll(std::string* outError) const
{
	bool ok = true;

	for (const auto& up : m_systems)
	{
		if (!up) continue;
		if (!std::as_const(*up).Validate(outError)) ok = false;
	}

	return ok;
}

void StateMachineManager::Clear()
{
	m_systems.clear();
	m_lookup.clear();
	// m_config は残す。シーンを跨いでも同じ調整値を使う
}

void StateMachineManager::DrawImGuiAll()
{
	if (!ImGui::Begin(U8("ステートマシン")))
	{
		ImGui::End();
		return;
	}

	ImGui::Text(U8("登録システム数 : %zu"), m_systems.size());
	ImGui::Separator();

	for (auto& up : m_systems)
	{
		if (!up) continue;

		const std::string label =
			up->GetName() + " (" + std::to_string(up->GetStateCount()) + " states)";

		if (!ImGui::CollapsingHeader(label.c_str())) continue;

		ImGui::PushID(up->GetId());

		// 定義一覧のみ表示(コンポーネントが無いので状態は出せない)
		for (size_t i = 0; i < up->GetStateCount(); ++i)
		{
			const StateDesc* d = up->Get(static_cast<uint16_t>(i));
			if (!d) continue;

			// 子は1段下げて表示
			if (!d->IsBase()) ImGui::Indent();
			ImGui::Text("%s", d->name.c_str());
			if (!d->IsBase()) ImGui::Unindent();
		}

		ImGui::PopID();
	}

	ImGui::End();
}
