// =============================================================================
//  KdComponentReflect.h
//  KdComponentManager 用の「登録レジストリ + JSONシリアライズ + ImGuiインスペクタ」
//
//  1つのフィールド記述 (KD_REFLECT) から以下を自動生成:
//    - kdToJson / kdFromJson  : JSON への保存・復元
//    - kdDraw / drawFields     : ImGui インスペクタ (imgui.h があれば自動有効)
//  ComponentRegistry に register_<T>("型名") すると、ワールド全体の
//  saveWorld / loadWorld / drawEntityInspector が使えるようになる。
//
//  依存:
//    - KdComponentManager.h / KdReflect.h
//    - nlohmann/json.hpp
//    - <imgui.h> があれば ImGui 機能を自動で有効化 (無ければ JSON のみ)
//    - <DirectXMath.h> があれば XMFLOAT2/3/4/4X4 を自動対応
// =============================================================================
#pragma once
#include "EcsConfig.h"  
#include "KdComponentManager.h"
#include "Reflect/KdReflect.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <algorithm>

#if __has_include(<DirectXMath.h>)
#include <DirectXMath.h>
#define KD_ECS_HAS_DXMATH 1
#endif

#if __has_include(<imgui.h>)
#include <imgui.h>
#define KD_ECS_IMGUI 1
#endif

namespace kd::ecs {

	using nlohmann::json;
	using kd::reflect::HasReflect_v;

	// =============================================================================
	//  フィールド ⇔ JSON  (DirectXMath 特殊化を先に宣言 → テンプレートより優先)
	// =============================================================================
#ifdef KD_ECS_HAS_DXMATH
	inline void kdToJson(json& j, const DirectX::XMFLOAT2& v) { j = { v.x, v.y }; }
	inline void kdToJson(json& j, const DirectX::XMFLOAT3& v) { j = { v.x, v.y, v.z }; }
	inline void kdToJson(json& j, const DirectX::XMFLOAT4& v) { j = { v.x, v.y, v.z, v.w }; }
	inline void kdToJson(json& j, const DirectX::XMFLOAT4X4& v) {
		j = json::array();
		for (int i = 0; i < 16; ++i) j.push_back((&v._11)[i]);
	}
	inline void kdFromJson(const json& j, DirectX::XMFLOAT2& v) { v = { j.at(0), j.at(1) }; }
	inline void kdFromJson(const json& j, DirectX::XMFLOAT3& v) { v = { j.at(0), j.at(1), j.at(2) }; }
	inline void kdFromJson(const json& j, DirectX::XMFLOAT4& v) { v = { j.at(0), j.at(1), j.at(2), j.at(3) }; }
	inline void kdFromJson(const json& j, DirectX::XMFLOAT4X4& v) {
		for (int i = 0; i < 16; ++i) (&v._11)[i] = j.at(i).get<float>();
	}
#endif

	// ---- 汎用 (arithmetic / enum / ネスト reflected) ----
	template<class T>
	inline void kdToJson(json& j, const T& v) {
		if constexpr (std::is_enum_v<T>)             j = static_cast<std::underlying_type_t<T>>(v);
		else if constexpr (std::is_arithmetic_v<T>)  j = v;
		else if constexpr (HasReflect_v<T>) {
			j = json::object();
			kdReflect(v, [&](const char* n, const auto& f) { kdToJson(j[n], f); });
		}
		else static_assert(sizeof(T) == 0, "kdToJson: 未対応のフィールド型 (kdToJson の overload を追加してください)");
	}

	template<class T>
	inline void kdFromJson(const json& j, T& v) {
		if constexpr (std::is_enum_v<T>)             v = static_cast<T>(j.get<std::underlying_type_t<T>>());
		else if constexpr (std::is_arithmetic_v<T>)  v = j.get<T>();
		else if constexpr (HasReflect_v<T>) {
			kdReflect(v, [&](const char* n, auto& f) { if (j.contains(n)) kdFromJson(j.at(n), f); });
		}
		else static_assert(sizeof(T) == 0, "kdFromJson: 未対応のフィールド型");
	}

	// 反射構造体まるごと ⇔ JSON
	template<class T> inline json  kdStructToJson(const T& v) { json j; kdToJson(j, v); return j; }
	template<class T> inline void  kdStructFromJson(const json& j, T& v) { kdFromJson(j, v); }

	// -----------------------------------------------------------------------------
	//  コンテナ ⇔ JSON配列 (ストレージ非依存)
	//    CompPool 等でも Data()/Create() を通じてそのまま使える:
	//      json arr = saveComponents(pool.Data());
	//      loadComponents<CollisionComponent>(arr, [&](const auto& c){ pool.Create(c); });
	// -----------------------------------------------------------------------------
	template<class T>
	inline json saveComponents(const std::vector<T>& items) {
		json arr = json::array();
		for (const T& c : items) arr.push_back(kdStructToJson(c));
		return arr;
	}
	template<class T, class AddFn>
	inline void loadComponents(const json& arr, AddFn&& add) {
		for (const auto& j : arr) { T c{}; kdStructFromJson(j, c); add(c); }
	}

	// =============================================================================
	//  フィールド → ImGui  (imgui.h がある時のみ)
	// =============================================================================
#ifdef KD_ECS_IMGUI
	inline bool kdDraw(const char* label, float& v) { return ImGui::DragFloat(label, &v, 0.05f); }
	inline bool kdDraw(const char* label, double& v) { float f = (float)v; bool c = ImGui::DragFloat(label, &f, 0.05f); if (c)v = f; return c; }
	inline bool kdDraw(const char* label, int& v) { return ImGui::DragInt(label, &v); }
	inline bool kdDraw(const char* label, unsigned& v) { int t = (int)v; bool c = ImGui::DragInt(label, &t); if (c)v = (unsigned)(t < 0 ? 0 : t); return c; }
	inline bool kdDraw(const char* label, bool& v) { return ImGui::Checkbox(label, &v); }
#ifdef KD_ECS_HAS_DXMATH
	inline bool kdDraw(const char* label, DirectX::XMFLOAT2& v) { return ImGui::DragFloat2(label, &v.x, 0.05f); }
	inline bool kdDraw(const char* label, DirectX::XMFLOAT3& v) { return ImGui::DragFloat3(label, &v.x, 0.05f); }
	inline bool kdDraw(const char* label, DirectX::XMFLOAT4& v) { return ImGui::DragFloat4(label, &v.x, 0.05f); }
#endif

	template<class T>
	inline bool kdDraw(const char* label, T& v) {
		if constexpr (std::is_enum_v<T>) {
			int t = static_cast<int>(v); bool c = ImGui::InputInt(label, &t); if (c) v = static_cast<T>(t); return c;
		}
		else if constexpr (HasReflect_v<T>) {
			bool changed = false;
			if (ImGui::TreeNode(label)) {
				kdReflect(v, [&](const char* n, auto& f) { changed |= kdDraw(n, f); });
				ImGui::TreePop();
			}
			return changed;
		}
		else { ImGui::Text("%s: (未対応)", label); return false; }
	}

	// 反射構造体の全フィールドを描画。編集されたら true
	template<class T>
	inline bool drawFields(T& v) {
		bool changed = false;
		kdReflect(v, [&](const char* n, auto& f) { changed |= kdDraw(n, f); });
		return changed;
	}
#endif // KD_ECS_IMGUI

	// =============================================================================
	//  ComponentRegistry: 型名 ⇔ コンポーネント型 の登録
	//    register_<T>("Transform") で JSON/インスペクタ操作が型消去で使えるようになる
	// =============================================================================
	class ComponentRegistry {
	public:
		using CB = ComponentManager::CommandBuffer;

		// ---- 通常登録: KD_REFLECT 済みの public フィールドを自動処理 ----
		template<Component T>
		void register_(std::string name) {
			Entry e = makeBase<T>(name);
			e.saveOne = [](ComponentManager& m, Entity ent, json& out) {
				kdReflect(m.peek<T>(ent), [&](const char* n, const auto& f) { kdToJson(out[n], f); });
				};
			e.loadOne = [](ComponentManager& m, Entity ent, const json& in) {
				T comp{};
				kdReflect(comp, [&](const char* n, auto& f) { if (in.contains(n)) kdFromJson(in.at(n), f); });
				m.add<T>(ent, comp);
				};
#ifdef KD_ECS_IMGUI
			e.draw = [name](ComponentManager& m, Entity ent) {
				if (!m.has<T>(ent)) return false;
				bool changed = false;
				if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
					changed = drawFields(m.get<T>(ent)); // get<T> は write 側 → 編集で dirty
				return changed;
				};
#endif
			commit(name, std::move(e));
		}

		// ---- 独自登録: private メンバ / 不変条件(setter経由)を持つクラス向け ----
		//   saveFn : void(const T&, json&)        … getter で書き出す
		//   loadFn : void(const json&, T&)        … setter 経由で読み込む(SetDirty等を通す)
		//   drawFn : bool(T&) -> 変更されたら true … setter 経由で編集 (imgui無しなら未使用)
		template<Component T, class SaveFn, class LoadFn, class DrawFn>
		void registerCustom(std::string name, SaveFn saveFn, LoadFn loadFn, DrawFn drawFn) {
			Entry e = makeBase<T>(name);
			e.saveOne = [saveFn](ComponentManager& m, Entity ent, json& out) { saveFn(m.peek<T>(ent), out); };
			e.loadOne = [loadFn](ComponentManager& m, Entity ent, const json& in) {
				T comp{}; loadFn(in, comp); m.add<T>(ent, comp);
				};
#ifdef KD_ECS_IMGUI
			e.draw = [name, drawFn](ComponentManager& m, Entity ent) {
				if (!m.has<T>(ent)) return false;
				bool changed = false;
				if (ImGui::CollapsingHeader(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
					changed = drawFn(m.get<T>(ent)); // write 側 → dirty
				return changed;
				};
#else
			(void)drawFn;
#endif
			commit(name, std::move(e));
		}

		[[nodiscard]] const std::vector<std::string>& typeNames() const { return order_; }
		[[nodiscard]] bool contains(const std::string& name) const { return byName_.count(name) != 0; }

		// 登録型のいずれかを持つ全エンティティを収集 (重複除去)。エディタの一覧表示用。
		[[nodiscard]] std::vector<Entity> collectEntities(ComponentManager& cm) const {
			std::vector<Entity> all;
			for (const auto& name : order_) {
				auto es = byName_.at(name).entities(cm);
				all.insert(all.end(), es.begin(), es.end());
			}
			auto keyOf = [](Entity e) { return (static_cast<std::uint64_t>(e.index) << 32) | e.generation; };
			std::sort(all.begin(), all.end(), [&](Entity a, Entity b) { return keyOf(a) < keyOf(b); });
			all.erase(std::unique(all.begin(), all.end()), all.end());
			return all;
		}

		// ---- 名前でランタイム操作 (エディタ / データ駆動スポーン) ----
		//   登録済みの型なら、コンパイル時に型を知らなくても名前で追加/削除できる。
		//   未登録名・条件不一致なら false / no-op を返す。

		// デフォルト値のコンポーネントを追加
		bool addByName(ComponentManager& cm, Entity e, const std::string& name) const {
			auto it = byName_.find(name);
			if (it == byName_.end()) return false;
			it->second.addDefault(cm, e);
			return true;
		}
		// JSON 値つきでコンポーネントを追加 (プレハブ/セーブデータの1コンポーネント)
		bool addByName(ComponentManager& cm, Entity e, const std::string& name, const json& data) const {
			auto it = byName_.find(name);
			if (it == byName_.end()) return false;
			it->second.loadOne(cm, e, data);
			return true;
		}
		bool removeByName(ComponentManager& cm, Entity e, const std::string& name) const {
			auto it = byName_.find(name);
			if (it == byName_.end()) return false;
			it->second.remove(cm, e);
			return true;
		}
		[[nodiscard]] bool hasByName(ComponentManager& cm, Entity e, const std::string& name) const {
			auto it = byName_.find(name);
			return it != byName_.end() && it->second.has(cm, e);
		}

		// ---- 1エンティティ ⇔ JSON (プレハブ/クローンの基盤) ----
		//   { "Transform": {...}, "Health": {...} } 形式 (型名→フィールド)
		[[nodiscard]] json saveEntity(ComponentManager& cm, Entity ent) const {
			json out = json::object();
			for (const auto& name : order_) {
				const Entry& e = byName_.at(name);
				if (e.has(cm, ent)) { json d = json::object(); e.saveOne(cm, ent, d); out[name] = std::move(d); }
			}
			return out;
		}
		// JSON からエンティティを生成 (即時。並行フェーズ外で呼ぶこと)
		Entity spawnEntity(ComponentManager& cm, const json& comps) const {
			Entity ent = cm.createEntity();
			for (auto it = comps.begin(); it != comps.end(); ++it) {
				auto found = byName_.find(it.key());
				if (found != byName_.end()) found->second.loadOne(cm, ent, it.value());
			}
			return ent;
		}
		// 既存エンティティを複製 (プレハブ実体化にも使える)
		Entity cloneEntity(ComponentManager& cm, Entity src) const { return spawnEntity(cm, saveEntity(cm, src)); }

		// ---- ワールド全体を JSON へ (component-major) ----
		[[nodiscard]] json saveWorld(ComponentManager& cm) const {
			json root;
			root["version"] = 1;
			json comps = json::object();
			for (const auto& name : order_) {
				const Entry& e = byName_.at(name);
				json arr = json::array();
				for (Entity ent : e.entities(cm)) {
					json rec;
					rec["e"] = { ent.index, ent.generation };
					json d = json::object();
					e.saveOne(cm, ent, d);
					rec["d"] = std::move(d);
					arr.push_back(std::move(rec));
				}
				if (!arr.empty()) comps[name] = std::move(arr);
			}
			root["components"] = std::move(comps);
			return root;
		}

		// ---- JSON からワールドを復元 ----
		//   保存時の Entity ID は保持されず、新しく採番し直す (同一性はリマップ)。
		//   clearFirst=false で既存ワールドへ追記も可能。
		void loadWorld(ComponentManager& cm, const json& root, bool clearFirst = true) const {
			if (clearFirst) cm.clear();
			if (!root.contains("components")) return;
			const json& comps = root.at("components");

			std::unordered_map<std::uint64_t, Entity> remap;
			auto keyOf = [](std::uint32_t idx, std::uint32_t gen) {
				return (static_cast<std::uint64_t>(idx) << 32) | gen;
				};
			auto entityOf = [&](const json& rec) -> Entity {
				std::uint32_t idx = rec.at("e").at(0).get<std::uint32_t>();
				std::uint32_t gen = rec.at("e").at(1).get<std::uint32_t>();
				std::uint64_t k = keyOf(idx, gen);
				auto it = remap.find(k);
				if (it != remap.end()) return it->second;
				Entity ne = cm.createEntity();
				remap.emplace(k, ne);
				return ne;
				};

			// 1) 全レコードのエンティティを先に実体化
			for (auto it = comps.begin(); it != comps.end(); ++it)
				for (const auto& rec : it.value()) (void)entityOf(rec);

			// 2) コンポーネントを付与 (未登録の型名はスキップ = 前方互換)
			for (auto it = comps.begin(); it != comps.end(); ++it) {
				auto found = byName_.find(it.key());
				if (found == byName_.end()) continue;
				for (const auto& rec : it.value())
					found->second.loadOne(cm, entityOf(rec), rec.at("d"));
			}
			cm.sync(); // 復元結果を read 側にパブリッシュ
		}

#ifdef KD_ECS_IMGUI
		// ---- 1エンティティのインスペクタ (全コンポーネント + Add コンボ) ----
		bool drawEntityInspector(ComponentManager& cm, Entity ent) {
			if (!cm.isAlive(ent)) { ImGui::Text("(dead entity)"); return false; }
			ImGui::Text("Entity  index=%u  gen=%u", ent.index, ent.generation);
			ImGui::Separator();
			bool changed = false;
			for (const auto& name : order_) changed |= byName_[name].draw(cm, ent);

			ImGui::Separator();
			if (ImGui::BeginCombo("##add", "＋ Add Component")) {
				for (const auto& name : order_) {
					Entry& e = byName_[name];
					if (!e.has(cm, ent) && ImGui::Selectable(name.c_str())) { e.addDefault(cm, ent); changed = true; }
				}
				ImGui::EndCombo();
			}
			return changed;
		}
#endif

	private:
		struct Entry {
			std::string name;
			std::uint32_t typeId = 0;
			std::function<bool(ComponentManager&, Entity)> has;
			std::function<void(ComponentManager&, Entity)> remove;
			std::function<void(ComponentManager&, Entity)> addDefault;
			std::function<void(ComponentManager&, Entity, json&)> saveOne;
			std::function<void(ComponentManager&, Entity, const json&)> loadOne;
			std::function<std::vector<Entity>(ComponentManager&)> entities;
#ifdef KD_ECS_IMGUI
			std::function<bool(ComponentManager&, Entity)> draw;
#endif
		};
		// 型に依らない共通部分 (has/remove/addDefault/entities) を埋める
		template<Component T>
		Entry makeBase(const std::string& name) {
			Entry e;
			e.name = name;
			e.typeId = detail::componentTypeId<T>();
			e.has = [](ComponentManager& m, Entity ent) { return m.has<T>(ent); };
			e.remove = [](ComponentManager& m, Entity ent) { m.remove<T>(ent); };
			e.addDefault = [](ComponentManager& m, Entity ent) { if (!m.has<T>(ent)) m.add<T>(ent, T{}); };
			e.entities = [](ComponentManager& m) {
				std::vector<Entity> out;
				auto sp = m.pool<T>().entitiesSpan();
				out.assign(sp.begin(), sp.end());
				return out;
				};
			return e;
		}
		void commit(const std::string& name, Entry&& e) {
			const bool isNew = (byName_.find(name) == byName_.end());
			byName_[name] = std::move(e);
			if (isNew) order_.push_back(name);   // 同名再登録は上書き(order_は重複させない)
		}

		std::unordered_map<std::string, Entry> byName_;
		std::vector<std::string> order_;
	};

} // namespace kd::ecs

// =============================================================================
//  自己登録: 各コンポーネント定義(またはその .cpp)の近くに1行置くだけ。
//    中央の登録リストを編集する必要がなくなる → コンポーネントが増えてもスケール。
//
//    KD_REFLECT(Health, hp, max)                 // フィールド記述
//    KD_AUTO_REGISTER_COMPONENT(Health, "Health") // ← これ1行で登録される
//
//    // 独自アダプタ(registerCustom を使うクラス)は登録関数を渡す:
//    KD_AUTO_REGISTER_WITH(RegisterTransform)
//
//    使う側:
//    ComponentRegistry reg;
//    registerAllComponents(reg);                 // 自己登録された全型をまとめて適用
// =============================================================================
namespace kd::ecs {
	using ComponentRegistrar = void(*)(ComponentRegistry&);
	inline std::vector<ComponentRegistrar>& componentRegistrars() {
		static std::vector<ComponentRegistrar> v; return v;
	}
	inline void registerAllComponents(ComponentRegistry& reg) {
		for (auto fn : componentRegistrars()) fn(reg);
	}

	// 自己登録された全型が自動で載るグローバル既定レジストリ。
	//   main 開始後に初回アクセスすれば、全 KD_AUTO_REGISTER 済みの型が入っている。
	//   「コンポーネントシステム」からいつでも名前でコンポーネントを扱える入口。
	inline ComponentRegistry& defaultComponentRegistry() {
		static ComponentRegistry reg = [] {
			ComponentRegistry r;
			registerAllComponents(r);
			return r;
			}();
		return reg;
	}
}

#define KD_ECS_CONCAT2(a, b) a##b
#define KD_ECS_CONCAT(a, b)  KD_ECS_CONCAT2(a, b)

// KD_REFLECT 済みコンポーネントを自己登録
#define KD_AUTO_REGISTER_COMPONENT(Type, Name)                                    \
    namespace {                                                                    \
        const bool KD_ECS_CONCAT(kd_autoreg_, __COUNTER__) = []() {                \
            ::kd::ecs::componentRegistrars().push_back(                            \
                +[](::kd::ecs::ComponentRegistry& r) { r.register_<Type>(Name); });\
            return true; }();                                                      \
    }

// 独自登録関数 (void(ComponentRegistry&) 相当) を自己登録
#define KD_AUTO_REGISTER_WITH(RegisterFn)                                          \
    namespace {                                                                    \
        const bool KD_ECS_CONCAT(kd_autoreg_, __COUNTER__) = []() {                \
            ::kd::ecs::componentRegistrars().push_back(                            \
                +[](::kd::ecs::ComponentRegistry& r) { RegisterFn(r); });          \
            return true; }();                                                      \
    }