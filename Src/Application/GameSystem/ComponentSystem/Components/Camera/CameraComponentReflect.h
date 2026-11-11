// =============================================================================
//  CameraComponentReflect.h
//  CameraComponent を ComponentRegistry に繋ぐアダプタ。本体は一切変更しない。
//
//  ・private メンバなので必ず getter/setter 経由 (m_projDirty 等を正しく立てるため)
//  ・Orbit の pivot は毎フレーム外部から注入される実行時値なので保存しない。
//    yaw/pitch/distance も同様に実行時値扱いとし、インスペクタ編集のみ対応する
//    (setter が SetOrbit(pivot,...) しか無いため、差分を Add～ に渡して反映)
//  ・Mode は setter が無いので read-only 表示
// =============================================================================
#pragma once
#include "../../KdComponentReflect.h"
#include "CameraComponent.h"

namespace kdcam_detail {
	// CameraComponent::Mode → 表示名。分岐ではなくテーブル引き。
	// ★ Mode に値を追加したらここも足すこと。
	inline constexpr const char* kModeNames[] = { "Manual", "Follow", "Orbit" };
	inline constexpr int kModeCount =
		static_cast<int>(sizeof(kModeNames) / sizeof(kModeNames[0]));

	inline const char* ModeName(CameraComponent::Mode m) {
		const int i = static_cast<int>(m);
		return (i >= 0 && i < kModeCount) ? kModeNames[i] : "(unknown)";
	}
}

inline void RegisterCamera(kd::ecs::ComponentRegistry& reg, std::string name = "Camera") {

	reg.registerCustom<CameraComponent>(std::move(name),
		// ---- save: getter のあるものだけ ----
		[](const CameraComponent& c, nlohmann::json& j) {
			j["fovDeg"] = c.GetFovY();
			j["nearZ"] = c.GetNearZ();
			j["farZ"] = c.GetFarZ();
			j["focusDist"] = c.GetFocusDistance();
			j["focusFore"] = c.GetFocusForeRange();
			j["focusBack"] = c.GetFocusBackRange();
			j["vcamId"] = c.GetVcamId();
			j["priority"] = c.GetPriority();
			j["vcamEnabled"] = c.IsVcamEnabled();
			j["targetPull"] = c.IsTargetPullEnabled();
		},
		// ---- load: setter 経由 ----
		[](const nlohmann::json& j, CameraComponent& c) {
			const float fov = j.value("fovDeg", c.GetFovY());
			const float nz = j.value("nearZ", c.GetNearZ());
			const float fz = j.value("farZ", c.GetFarZ());
			c.SetFovY(fov);
			c.SetClip(nz, fz);

			c.SetFocus(j.value("focusDist", c.GetFocusDistance()),
				j.value("focusFore", c.GetFocusForeRange()),
				j.value("focusBack", c.GetFocusBackRange()));

			c.SetVcamId(j.value("vcamId", c.GetVcamId()));
			c.SetPriority(j.value("priority", c.GetPriority()));
			c.SetVcamEnabled(j.value("vcamEnabled", c.IsVcamEnabled()));
			c.SetTargetPull(j.value("targetPull", c.IsTargetPullEnabled()));
		},
		// ---- draw ----
		[](CameraComponent& c) -> bool {
#ifdef KD_ECS_IMGUI
			bool changed = false;

			ImGui::Text("mode : %s", kdcam_detail::ModeName(c.GetMode()));
			const Math::Vector3 wp = c.GetWorldPosition();
			ImGui::Text("world: %.2f, %.2f, %.2f", wp.x, wp.y, wp.z);
			ImGui::Separator();

			// ---- 射影 ----
			float fov = c.GetFovY();
			if (ImGui::DragFloat("FovY (deg)", &fov, 0.2f, 1.0f, 179.0f)) { c.SetFovY(fov); changed = true; }

			float clip[2] = { c.GetNearZ(), c.GetFarZ() };
			if (ImGui::DragFloat2("Near / Far", clip, 0.05f)) {
				if (clip[0] < 0.001f) clip[0] = 0.001f;
				if (clip[1] <= clip[0]) clip[1] = clip[0] + 0.001f;
				c.SetClip(clip[0], clip[1]);
				changed = true;
			}
			ImGui::Separator();

			// ---- Orbit (差分を Add～ に渡して setter のクランプを通す) ----
			if (ImGui::TreeNodeEx("Orbit", ImGuiTreeNodeFlags_DefaultOpen)) {
				const float oldYaw = c.GetOrbitYaw();
				const float oldPitch = c.GetOrbitPitch();
				float deg[2] = { DirectX::XMConvertToDegrees(oldYaw),
								 DirectX::XMConvertToDegrees(oldPitch) };
				if (ImGui::DragFloat2("Yaw / Pitch (deg)", deg, 0.5f)) {
					c.AddOrbitAngles(DirectX::XMConvertToRadians(deg[0]) - oldYaw,
						DirectX::XMConvertToRadians(deg[1]) - oldPitch);
					changed = true;
				}

				const float oldDist = c.GetOrbitDistance();
				float dist = oldDist;
				if (ImGui::DragFloat("Distance", &dist, 0.05f)) {
					c.AddOrbitDistance(dist - oldDist);
					changed = true;
				}
				ImGui::TreePop();
			}

			// ---- 被写界深度 ----
			if (ImGui::TreeNode("Focus")) {
				float f[3] = { c.GetFocusDistance(), c.GetFocusForeRange(), c.GetFocusBackRange() };
				if (ImGui::DragFloat3("Dist / Fore / Back", f, 0.05f)) {
					c.SetFocus(f[0], f[1], f[2]);
					changed = true;
				}
				ImGui::TreePop();
			}

			// ---- ターゲットプル ----
			if (ImGui::TreeNode("Target Pull")) {
				bool en = c.IsTargetPullEnabled();
				if (ImGui::Checkbox("Enabled", &en)) { c.SetTargetPull(en); changed = true; }
				ImGui::Text("active   : %s", c.IsTargetPullActive() ? "yes" : "no");
				ImGui::Text("lockedId : %llu",
					static_cast<unsigned long long>(c.GetTargetPullLockedId()));
				ImGui::TreePop();
			}

			// ---- vcam メタ ----
			if (ImGui::TreeNode("Virtual Camera")) {
				int id = static_cast<int>(c.GetVcamId());
				if (ImGui::DragInt("Vcam Id", &id)) { c.SetVcamId(static_cast<std::uint32_t>(id < 0 ? 0 : id)); changed = true; }

				int prio = c.GetPriority();
				if (ImGui::DragInt("Priority", &prio)) { c.SetPriority(prio); changed = true; }

				bool en = c.IsVcamEnabled();
				if (ImGui::Checkbox("Enabled", &en)) { c.SetVcamEnabled(en); changed = true; }
				ImGui::TreePop();
			}

			ImGui::Separator();
			if (ImGui::Button("Snap To Target")) { c.SnapToTarget(); changed = true; }

			return changed;
#else
			(void)c; return false;
#endif
		});
}

KD_AUTO_REGISTER_WITH(RegisterCamera)