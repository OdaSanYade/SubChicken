#pragma once
#include "../KdComponentReflect.h"
#include <cstring>  
#include <string>

namespace Game {

	// ---------------------------------------------------------------------------
	//  移動・物理
	// ---------------------------------------------------------------------------
	struct Velocity { Math::Vector3 value{}; };                     // ワールド速度 (単位/秒)
	struct MoveSpeed { float value = 5.0f; };                        // 入力移動の基準速度
	struct Gravity { float scale = 1.0f; bool grounded = false; }; // 重力スケール + 接地(実行時)
	struct Jump { float power = 8.0f; int maxJumps = 1; int jumpsLeft = 1; };

	// ---------------------------------------------------------------------------
	//  生存・戦闘
	// ---------------------------------------------------------------------------
	struct Health { float hp = 100.0f; float maxHp = 100.0f; bool invulnerable = false; float iFrame = 0.0f; };
	struct Lifetime { float seconds = 5.0f; };                      // 0 以下で破棄する運用
	struct Damage { float amount = 10.0f; int hitstopFrames = 8; };

	// ---------------------------------------------------------------------------
	//  描画
	// ---------------------------------------------------------------------------
	struct RenderModel { std::uint32_t modelId = 0; std::uint32_t materialId = 0; bool visible = true; Math::Vector4 tint{ 1,1,1,1 }; };
	struct AnimationState { std::uint32_t clipId = 0; float time = 0.0f; float speed = 1.0f; bool loop = true; };

	// ---------------------------------------------------------------------------
	//  カメラ・入力・AI
	// ---------------------------------------------------------------------------
	struct CameraTarget { Math::Vector3 offset{ 0.0f, 2.0f, -6.0f }; float followLerp = 0.12f; };
	struct PlayerInput { Math::Vector3 moveDir{}; bool jump = false; bool attack = false; };
	struct AIState { int state = 0; float timer = 0.0f; kd::ecs::Entity target{}; };

	// ---------------------------------------------------------------------------
	//  タグ (データ無し。view のフィルタに使う)
	// ---------------------------------------------------------------------------
	struct PlayerTag {};
	struct EnemyTag {};
	struct ProjectileTag {};
	struct PickupTag {};

	// ---------------------------------------------------------------------------
	//  名前 (エディタ表示用。固定長で trivially copyable)
	// ---------------------------------------------------------------------------
	struct Name { char value[32] = {}; };

	// ---- フィールド記述 (保存したい/見せたいフィールドだけ列挙。実行時値は外す) ----
	KD_REFLECT(Velocity, value)
		KD_REFLECT(MoveSpeed, value)
		KD_REFLECT(Gravity, scale)                 // grounded は実行時なので外す
		KD_REFLECT(Jump, power, maxJumps)          // jumpsLeft は実行時
		KD_REFLECT(Health, hp, maxHp, invulnerable)// iFrame は実行時
		KD_REFLECT(Lifetime, seconds)
		KD_REFLECT(Damage, amount, hitstopFrames)
		KD_REFLECT(RenderModel, modelId, materialId, visible, tint)
		KD_REFLECT(AnimationState, clipId, speed, loop) // time は実行時
		KD_REFLECT(CameraTarget, offset, followLerp)
		KD_REFLECT(PlayerInput, moveDir, jump, attack)  // デバッグ表示用に全部
		KD_REFLECT(AIState, state, timer)          // target(Entity参照) は保存しない
		KD_REFLECT_EMPTY(PlayerTag)
		KD_REFLECT_EMPTY(EnemyTag)
		KD_REFLECT_EMPTY(ProjectileTag)
		KD_REFLECT_EMPTY(PickupTag)

		// ---- Name は char[32] を string として扱うため registerCustom で登録 ----
		inline void RegisterName(kd::ecs::ComponentRegistry& r) {
		r.registerCustom<Name>("Name",
			[](const Name& n, kd::ecs::json& j) { j["value"] = std::string(n.value); },
			[](const kd::ecs::json& j, Name& n) {
				std::string s = j.value("value", std::string{});
				std::strncpy(n.value, s.c_str(), sizeof(n.value) - 1);
				n.value[sizeof(n.value) - 1] = '\0';
			},
			[](Name& n) -> bool {
#ifdef KD_ECS_IMGUI
				return ImGui::InputText("name", n.value, sizeof(n.value));
#else
				(void)n; return false;
#endif
			});
	}

} // namespace Game

// ---- 自己登録 (定義の外・修飾名で。1コンポーネント1行) ----
KD_AUTO_REGISTER_COMPONENT(Game::Velocity, "Velocity")
KD_AUTO_REGISTER_COMPONENT(Game::MoveSpeed, "MoveSpeed")
KD_AUTO_REGISTER_COMPONENT(Game::Gravity, "Gravity")
KD_AUTO_REGISTER_COMPONENT(Game::Jump, "Jump")
KD_AUTO_REGISTER_COMPONENT(Game::Health, "Health")
KD_AUTO_REGISTER_COMPONENT(Game::Lifetime, "Lifetime")
KD_AUTO_REGISTER_COMPONENT(Game::Damage, "Damage")
KD_AUTO_REGISTER_COMPONENT(Game::RenderModel, "RenderModel")
KD_AUTO_REGISTER_COMPONENT(Game::AnimationState, "AnimationState")
KD_AUTO_REGISTER_COMPONENT(Game::CameraTarget, "CameraTarget")
KD_AUTO_REGISTER_COMPONENT(Game::PlayerInput, "PlayerInput")
KD_AUTO_REGISTER_COMPONENT(Game::AIState, "AIState")
KD_AUTO_REGISTER_COMPONENT(Game::PlayerTag, "PlayerTag")
KD_AUTO_REGISTER_COMPONENT(Game::EnemyTag, "EnemyTag")
KD_AUTO_REGISTER_COMPONENT(Game::ProjectileTag, "ProjectileTag")
KD_AUTO_REGISTER_COMPONENT(Game::PickupTag, "PickupTag")
KD_AUTO_REGISTER_WITH(Game::RegisterName)