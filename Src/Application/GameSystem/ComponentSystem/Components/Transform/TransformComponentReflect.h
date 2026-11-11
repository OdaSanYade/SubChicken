// =============================================================================
//  TransformComponentReflect.h
//  既存の TransformComponent を KdComponentManager のシリアライズ/インスペクタに繋ぐ
//  アダプタ。TransformComponent 本体は一切変更しない (getter/setter 経由)。
//
//  使い方:
//    #include "TransformComponentReflect.h"
//    ComponentRegistry reg;
//    RegisterTransform(reg);          // ← これで保存/ロード/インスペクタが有効に
//    reg.register_<OtherPodComp>("Other"); // 他は通常登録でOK
//
//  storage としては元々そのまま使える (trivially copyable)。この adapter は
//  「JSON保存/ロード」と「ImGui自動インスペクタ」を追加するためだけのもの。
//
//  ポイント:
//    - load / draw は必ず setter (SetTRS / SetPosition / SetRotationEuler / SetScale)
//      を通す → TransformComponent 内部の SetDirty() が呼ばれ、行列キャッシュが
//      正しく再計算される。生フィールドへ直接書くとキャッシュが壊れるので不可。
//    - 回転は保存/復元では quaternion(Vector4) をそのまま (ジンバルロック非依存)、
//      インスペクタ編集だけオイラー角(度) で扱う。
// =============================================================================
#pragma once
#include "TransformComponent.h"

#if __has_include(<DirectXMath.h>)
    #include <DirectXMath.h>
#endif

namespace kdtf_detail {
    inline nlohmann::json  v3ToJson(const Math::Vector3& v) { return { v.x, v.y, v.z }; }
    inline nlohmann::json  v4ToJson(const Math::Vector4& v) { return { v.x, v.y, v.z, v.w }; }
    inline Math::Vector3   toV3(const nlohmann::json& j) { return { j.at(0), j.at(1), j.at(2) }; }
    inline Math::Vector4   toV4(const nlohmann::json& j) { return { j.at(0), j.at(1), j.at(2), j.at(3) }; }
}

inline void RegisterTransform(kd::ecs::ComponentRegistry& reg, std::string name = "Transform") {
    using namespace kdtf_detail;

    reg.registerCustom<TransformComponent>(std::move(name),
        // ---- save: getter で書き出す ----
        [](const TransformComponent& t, nlohmann::json& j) {
            j["position"] = v3ToJson(t.GetPosition());
            j["rotation"] = v4ToJson(t.GetRotation());   // quaternion のまま
            j["scale"]    = v3ToJson(t.GetScale());
        },
        // ---- load: SetTRS 経由 (SetDirty が呼ばれる) ----
        [](const nlohmann::json& j, TransformComponent& t) {
            Math::Vector3 p = t.GetPosition();
            Math::Vector4 r = t.GetRotation();
            Math::Vector3 s = t.GetScale();
            if (j.contains("position")) p = toV3(j["position"]);
            if (j.contains("rotation")) r = toV4(j["rotation"]);
            if (j.contains("scale"))    s = toV3(j["scale"]);
            t.SetTRS(p, r, s);
        },
        // ---- draw: setter 経由で編集 (回転はオイラー角[度]) ----
        [](TransformComponent& t) -> bool {
#ifdef KD_ECS_IMGUI
            bool changed = false;

            Math::Vector3 p = t.GetPosition();
            if (ImGui::DragFloat3("Position", &p.x, 0.05f)) { t.SetPosition(p); changed = true; }

            Math::Vector3 e = t.GetRotationEuler();  // ラジアン
            float deg[3] = {
                DirectX::XMConvertToDegrees(e.x),
                DirectX::XMConvertToDegrees(e.y),
                DirectX::XMConvertToDegrees(e.z),
            };
            if (ImGui::DragFloat3("Rotation (deg)", deg, 0.5f)) {
                t.SetRotationEuler({
                    DirectX::XMConvertToRadians(deg[0]),
                    DirectX::XMConvertToRadians(deg[1]),
                    DirectX::XMConvertToRadians(deg[2]),
                });
                changed = true;
            }

            Math::Vector3 s = t.GetScale();
            if (ImGui::DragFloat3("Scale", &s.x, 0.05f)) { t.SetScale(s); changed = true; } // SetScale が0クランプ

            return changed;
#else
            (void)t; return false;
#endif
        });
}

KD_AUTO_REGISTER_WITH(RegisterTransform)
