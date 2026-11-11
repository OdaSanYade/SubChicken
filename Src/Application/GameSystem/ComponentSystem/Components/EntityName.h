// =============================================================================
//  EntityName.h
//  Game::Name (char[32]) を安全に読み書きするヘルパー。
//  ・trivially copyable を保つため std::string ではなく固定長のまま扱う
//  ・std::strncpy は MSVC で C4996 が出るので自前でコピーする
// =============================================================================
#pragma once
#include "DefaultComponent.h"

namespace Game {

	// 文字列を Name に詰める (溢れた分は切り捨て、必ず終端する)
	inline Name MakeName(std::string_view text) {
		Name n{};
		const std::size_t cap = sizeof(n.value) - 1;
		const std::size_t len = (text.size() < cap) ? text.size() : cap;
		for (std::size_t i = 0; i < len; ++i) n.value[i] = text[i];
		n.value[len] = '\0';
		return n;
	}

	// 名前を設定 (未所持なら追加、所持済みなら上書き)
	inline void SetEntityName(kd::ecs::ComponentManager& cm,
		kd::ecs::Entity e, std::string_view text) {
		if (!cm.isAlive(e)) return;
		const Name n = MakeName(text);
		if (cm.has<Name>(e)) cm.get<Name>(e) = n;
		else                 cm.add<Name>(e, n);
	}

	// 同名が並ぶ場合の連番付き ("Enemy" + 3 → "Enemy_3")
	inline void SetEntityName(kd::ecs::ComponentManager& cm,
		kd::ecs::Entity e, std::string_view text, int index) {
		SetEntityName(cm, e, std::string(text) + "_" + std::to_string(index));
	}

	// 名前を取得。未所持なら nullptr (呼び出し側でフォールバックする)
	inline const char* GetEntityName(kd::ecs::ComponentManager& cm,
		kd::ecs::Entity e) {
		if (!cm.isAlive(e) || !cm.has<Name>(e)) return nullptr;
		const char* s = cm.peek<Name>(e).value;
		return (s[0] != '\0') ? s : nullptr;
	}

} // namespace Game