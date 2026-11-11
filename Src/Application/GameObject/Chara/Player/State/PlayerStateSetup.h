#pragma once

// ===========================================================
// プレイヤーのステート定義を StateMachineManager へ登録する。
//
// 何度呼んでも安全(CreateSystemは同名なら既存を返し、
// 登録済みステートは二重登録されない)。
// ===========================================================
void SetupPlayerStates();

// ステートタグ。blockTag で「この状態からは入れない」を作るのに使う。
namespace PlayerTag
{
	constexpr unsigned int kGrounded = 1u << 0;
	constexpr unsigned int kAirborne = 1u << 1;
	constexpr unsigned int kAction   = 1u << 2;	
}

// よろけの終盤から回避で抜けられる割合は、Hurt ステートの "recoverFrom" パラメータへ移した