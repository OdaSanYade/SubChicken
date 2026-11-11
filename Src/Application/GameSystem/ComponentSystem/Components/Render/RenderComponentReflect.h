#pragma once
#include "../../KdComponentReflect.h"
#include "RenderComponent.h"

namespace kdrc_detail {
	inline constexpr const char* kRenderKindNames[] = { "Model", "Work", "Static" };
	inline constexpr int kRenderKindCount =
		static_cast<int>(sizeof(kRenderKindNames) / sizeof(kRenderKindNames[0]));
}

namespace DirectX {

	template<size_t N>
	inline void kdToJson(nlohmann::json& j, const XMFLOAT4(&v)[N])
	{
		j = nlohmann::json::array();
		for (size_t i = 0; i < N; ++i)
			j.push_back({ v[i].x, v[i].y, v[i].z, v[i].w });
	}

	template<size_t N>
	inline void kdFromJson(const nlohmann::json& j, XMFLOAT4(&v)[N])
	{
		for (size_t i = 0; i < N && i < j.size(); ++i)
		{
			v[i].x = j.at(i).at(0).get<float>();
			v[i].y = j.at(i).at(1).get<float>();
			v[i].z = j.at(i).at(2).get<float>();
			v[i].w = j.at(i).at(3).get<float>();
		}
	}

#ifdef KD_ECS_IMGUI
	template<size_t N>
	inline bool kdDraw(const char* label, XMFLOAT4(&v)[N])
	{
		bool changed = false;
		if (ImGui::TreeNode(label))
		{
			for (size_t i = 0; i < N; ++i)
			{
				char buf[32];
				snprintf(buf, sizeof(buf), "[%zu]", i);
				changed |= ImGui::DragFloat4(buf, &v[i].x, 0.01f);
			}
			ImGui::TreePop();
		}
		return changed;
	}
#endif

}

inline void kdToJson(nlohmann::json& j, const MaterialId& v)
{
	j = MaterialNameFromId(v.value);
}
inline void kdFromJson(const nlohmann::json& j, MaterialId& v)
{
	if (j.is_string())      v.value = MaterialIdFromName(j.get<std::string>());
	else if (j.is_number()) v.value = j.get<uint32_t>();   // 旧データ互換
	else                    v.value = 0;
}

#ifdef KD_ECS_IMGUI
inline bool kdDraw(const char* label, MaterialId& v)
{
	const auto& names = MaterialNameTable();
	std::vector<const char*> items;
	items.reserve(names.size());
	for (const auto& n : names)
		items.push_back(n.empty() ? "(default)" : n.c_str());

	int idx = (int)v.value;
	if (idx < 0 || idx >= (int)items.size()) idx = 0;
	const bool changed = ImGui::Combo(label, &idx, items.data(), (int)items.size());
	if (changed) v.value = (uint32_t)idx;
	return changed;
}
#endif

KD_REFLECT(RenderComponent, kind, assetId, generation, materialId, params)

KD_AUTO_REGISTER_COMPONENT(RenderComponent, "Render")