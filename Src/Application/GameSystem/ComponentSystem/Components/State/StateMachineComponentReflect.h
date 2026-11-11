// =============================================================================
//  StateMachineComponentReflect.h
//  StateMachineComponent を ComponentRegistry に繋ぐアダプタ。
//
//  ・eventBuffer / padding は配列なので KD_REFLECT では扱えない → registerCustom
//  ・stateIndex / baseIndex / reservedIndex は「システム内の定義位置」であり
//    実行時にシステムが解決する値なので保存しない (ロード時は無効値のまま)
//  ・インスペクタは基本 read-only。書き換えて良いのは再生パラメータと、
//    コンポーネント自身が持つ安全な操作 (CancelReserved / RestartState) のみ。
// =============================================================================
#pragma once
#include "../../KdComponentReflect.h"
#include "StateMachineComponent.h"

inline void RegisterStateMachine(kd::ecs::ComponentRegistry& reg,
	std::string name = "StateMachine") {

	reg.registerCustom<StateMachineComponent>(std::move(name),
		// ---- save ----
		[](const StateMachineComponent& s, nlohmann::json& j) {
			j["systemId"] = s.systemId;
			j["baseId"] = s.baseId;
			j["stateId"] = s.stateId;
			j["stateTag"] = s.stateTag;
			j["animPlaySpeed"] = s.animPlaySpeed;
			j["animLoop"] = s.animLoop;
		},
		// ---- load ----
		[](const nlohmann::json& j, StateMachineComponent& s) {
			s = StateMachineComponent{};   // まず既定値 (インデックス類は無効値)
			s.systemId = j.value("systemId", s.systemId);
			s.baseId = j.value("baseId", s.baseId);
			s.stateId = j.value("stateId", s.stateId);
			s.stateTag = j.value("stateTag", s.stateTag);
			s.animPlaySpeed = j.value("animPlaySpeed", s.animPlaySpeed);
			s.animLoop = j.value("animLoop", s.animLoop);
			// stateIndex 等はシステムが stateId から解決し直す
		},
		// ---- draw ----
		[](StateMachineComponent& s) -> bool {
#ifdef KD_ECS_IMGUI
			bool changed = false;

			ImGui::Text("systemId   : %u", (unsigned)s.systemId);
			ImGui::Text("valid      : %s", s.IsValid() ? "yes" : "no");
			ImGui::Separator();

			ImGui::Text("base       : 0x%08X  (idx %u)", s.baseId, (unsigned)s.baseIndex);
			ImGui::Text("state      : 0x%08X  (idx %u)", s.stateId, (unsigned)s.stateIndex);
			ImGui::Text("prevState  : 0x%08X", s.prevStateId);
			ImGui::Text("tag        : 0x%08X", s.stateTag);
			ImGui::Text("stateTime  : %.3f", s.stateTime);
			ImGui::Text("baseTime   : %.3f", s.baseTime);
			ImGui::Text("transitions: %u", s.transitionCount);
			ImGui::Text("lastResult : %s", ToString(s.GetLastResult()));
			ImGui::Separator();

			if (s.HasReserved()) {
				ImGui::Text("reserved   : 0x%08X  (idx %u)", s.reservedId, (unsigned)s.reservedIndex);
				ImGui::Text("  timer=%.3f  prio=%u  force=%u",
					s.reservedTimer, (unsigned)s.reservedPriority, (unsigned)s.reservedForce);
			}
			else {
				ImGui::TextUnformatted("reserved   : (none)");
			}
			ImGui::Separator();

			ImGui::Text("events (%u/%u)", (unsigned)s.eventCount,
				(unsigned)StateMachineComponent::kEventCapacity);
			for (uint8_t i = 0; i < s.eventCount; ++i)
				ImGui::BulletText("0x%08X", s.eventBuffer[i]);
			ImGui::Separator();

			// ---- 編集して良い値 ----
			if (ImGui::DragFloat("animPlaySpeed", &s.animPlaySpeed, 0.01f, 0.0f, 10.0f))
				changed = true;

			bool loop = (s.animLoop != 0);
			if (ImGui::Checkbox("animLoop", &loop)) { s.animLoop = loop ? 1 : 0; changed = true; }

			// ---- 安全な操作 (コンポーネント自身の API 経由) ----
			if (ImGui::Button("Restart State")) { s.RestartState();   changed = true; }
			ImGui::SameLine();
			if (ImGui::Button("Cancel Reserved")) { s.CancelReserved(); changed = true; }
			ImGui::SameLine();
			if (ImGui::Button("Clear Events")) { s.ClearEvents();    changed = true; }

			return changed;
#else
			(void)s; return false;
#endif
		});
}

KD_AUTO_REGISTER_WITH(RegisterStateMachine)