// =============================================================================
//  KdMathReflect.h
//  SimpleMath 型 (Vector2/3/4 / Quaternion) を 1 回だけリフレクション対応させる
//  共有部品。ADL で見つかるよう、変換関数は型の名前空間に置く。
// =============================================================================
#pragma once
#include "../EcsConfig.h"   // ★ 必ず先頭。KD_ECS_IMGUI はここで確定する

#ifdef KD_ECS_HAS_SIMPLEMATH
namespace DirectX {
	namespace SimpleMath {

		// ---- Vector2 ----
		inline void kdToJson(::nlohmann::json& j, const Vector2& v) { j = { v.x, v.y }; }
		inline void kdFromJson(const ::nlohmann::json& j, Vector2& v) { v.x = j.at(0); v.y = j.at(1); }

		// ---- Vector3 ----
		inline void kdToJson(::nlohmann::json& j, const Vector3& v) { j = { v.x, v.y, v.z }; }
		inline void kdFromJson(const ::nlohmann::json& j, Vector3& v) { v.x = j.at(0); v.y = j.at(1); v.z = j.at(2); }

		// ---- Vector4 ----
		inline void kdToJson(::nlohmann::json& j, const Vector4& v) { j = { v.x, v.y, v.z, v.w }; }
		inline void kdFromJson(const ::nlohmann::json& j, Vector4& v) { v.x = j.at(0); v.y = j.at(1); v.z = j.at(2); v.w = j.at(3); }

		// ---- Quaternion ----
		inline void kdToJson(::nlohmann::json& j, const Quaternion& v) { j = { v.x, v.y, v.z, v.w }; }
		inline void kdFromJson(const ::nlohmann::json& j, Quaternion& v) { v.x = j.at(0); v.y = j.at(1); v.z = j.at(2); v.w = j.at(3); }

#ifdef KD_ECS_IMGUI
		inline bool kdDraw(const char* label, Vector2& v) { return ImGui::DragFloat2(label, &v.x, 0.05f); }
		inline bool kdDraw(const char* label, Vector3& v) { return ImGui::DragFloat3(label, &v.x, 0.05f); }
		inline bool kdDraw(const char* label, Vector4& v) { return ImGui::DragFloat4(label, &v.x, 0.05f); }
		inline bool kdDraw(const char* label, Quaternion& v) { return ImGui::DragFloat4(label, &v.x, 0.05f); }
#endif

	}
} // namespace DirectX::SimpleMath
#endif // KD_ECS_HAS_SIMPLEMATH