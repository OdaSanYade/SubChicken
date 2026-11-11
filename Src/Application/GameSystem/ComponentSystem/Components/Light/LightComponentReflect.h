#pragma once
#include "../../KdComponentReflect.h"
#include "LightComponent.h"

namespace kdlc_detail {
	inline constexpr const char* kLightTypeNames[] = { "Point", "Spot" };
	inline constexpr int kLightTypeCount =
		static_cast<int>(sizeof(kLightTypeNames) / sizeof(kLightTypeNames[0]));
}

#ifdef KD_ECS_IMGUI
inline bool kdDraw(const char* label, LightType& v) {
	int idx = static_cast<int>(v);
	if (idx < 0 || idx >= kdlc_detail::kLightTypeCount) idx = 0;
	const bool changed = ImGui::Combo(label, &idx,
		kdlc_detail::kLightTypeNames, kdlc_detail::kLightTypeCount);
	if (changed) v = static_cast<LightType>(idx);
	return changed;
}
#endif

KD_REFLECT(LightComponent, type, enabled, range, power, color,
	innerDeg, outerDeg, offset)

	KD_AUTO_REGISTER_COMPONENT(LightComponent, "Light")