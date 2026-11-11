#pragma once
#include "StateMachineSystem.h"

class StateMachineManager
{
public:
	// システムIDの無効値。
	// 前は SM::kInvalidStateIndex(ステート用)をそのまま流用していたので名前を分けた。値は同じ
	static constexpr uint16_t kInvalidSystemId = SM::kInvalidStateIndex;

	static StateMachineManager& Instance()
	{
		static StateMachineManager inst;
		return inst;
	}

	StateMachineManager(const StateMachineManager&) = delete;
	StateMachineManager& operator=(const StateMachineManager&) = delete;

	// =======================================================
	// 調整値
	// StateMachineConfig.json の中身を渡すと、全システムに反映される。
	// 後から CreateSystem したシステムにも同じ値が入る。
	//   SM_MGR.ApplyConfigJson(RES_MGR.GetDataList()->Get("StateMachineConfig"));
	// 読めない項目は今の値のまま
	// =======================================================
	template<class JsonT> void ApplyConfigJson(const JsonT& root);

	void SetConfig(const StateMachineConfig& config);
	const StateMachineConfig& GetConfig() const { return m_config; }

	// =======================================================
	// システムの作成・取得
	// =======================================================

	// 既に同名があればそれを返す(二重登録の防止)
	// 返したポインタは Clear() を呼ぶまで有効
	StateMachineSystem* CreateSystem(std::string_view name);

	StateMachineSystem* Get(uint16_t systemId);
	const StateMachineSystem* Get(uint16_t systemId) const;

	StateMachineSystem* Get(std::string_view name) { return Get(FindId(name)); }
	const StateMachineSystem* Get(std::string_view name) const { return Get(FindId(name)); }

	// コンポーネントが属するシステムを引く
	StateMachineSystem* Get(const StateMachineComponent& sm) { return Get(sm.systemId); }
	const StateMachineSystem* Get(const StateMachineComponent& sm) const { return Get(sm.systemId); }

	uint16_t FindId(std::string_view name) const;
	bool Exists(std::string_view name) const { return FindId(name) != kInvalidSystemId; }

	// ---- 全システムへのアクセス ----
	size_t GetSystemCount() const { return m_systems.size(); }
	StateMachineSystem* GetAt(size_t i) { return (i < m_systems.size()) ? m_systems[i].get() : nullptr; }
	const StateMachineSystem* GetAt(size_t i) const { return (i < m_systems.size()) ? m_systems[i].get() : nullptr; }

	// 全システムを走査する。 fn: void(StateMachineSystem&)
	template<class Fn>
	void ForEach(Fn&& fn)
	{
		for (auto& up : m_systems) { if (up) fn(*up); }
	}

	// =======================================================
	// コンポーネント操作のショートカット
	// システムを自分で引かなくても済むようにした薄いラッパー。
	// =======================================================
	StateMachineComponent MakeComponent(std::string_view systemName) const;
	StateMachineComponent MakeComponent(uint16_t systemId) const;

	void Update(StateMachineComponent& sm, float deltaTime,
		void* pUser = nullptr, uint32_t entity = 0);

	StateResult ChangeState(StateMachineComponent& sm, StateId id, uint8_t priority = 0);
	StateResult ChangeStateForced(StateMachineComponent& sm, StateId id);
	bool TryChangeState(StateMachineComponent& sm, StateId id, uint8_t priority = 0);

	const char* GetStateName(const StateMachineComponent& sm) const;
	const char* GetBaseName(const StateMachineComponent& sm) const;

	// =======================================================
	// 一括処理
	// =======================================================

	// 全システムの定義を検証する。1つでも問題があれば false。
	// 非const版は各システムの遷移ガード用ビットマスクも作り直す
	bool ValidateAll(std::string* outError = nullptr);
	bool ValidateAll(std::string* outError = nullptr) const;

	// 全システムをまとめて表示するImGuiウィンドウ。
	// 中で ImGui::Begin/End を呼ぶので、他のウィンドウの Begin/End の外で呼ぶこと。
	void DrawImGuiAll();

	// 全システムと定義を破棄する(シーン切り替え時など)
	// ※CreateSystem で受け取ったポインタと、既存コンポーネントの systemId は無効になる。
	//   IDは0から振り直されるので、古いコンポーネントを残したまま作り直すと別のシステムを指してしまう
	void Clear();

private:
	StateMachineManager() = default;

	std::vector<std::unique_ptr<StateMachineSystem>> m_systems;
	std::unordered_map<StateId, uint16_t> m_lookup;
	StateMachineConfig m_config;
};

// 短縮マクロ(RES_MGR等と同じ流儀)
#define SM_MGR StateMachineManager::Instance()
