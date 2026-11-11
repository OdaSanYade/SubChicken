#pragma once

// ===========================================================
// 敵のステート定義を StateMachineManager へ登録する。
//
// 何度呼んでも安全(CreateSystemは同名なら既存を返し、
// 登録済みステートは二重登録されない)。
// ===========================================================
void SetupEnemyStates();

// ステートタグ。ベース単位で付ける。
namespace EnemyTag
{
	constexpr unsigned int kNormal = 1u << 0;	// うろつき
	constexpr unsigned int kChase = 1u << 1;	// 追跡
	constexpr unsigned int kAlert = 1u << 2;	// 警戒
	constexpr unsigned int kAttack = 1u << 3;	// 攻撃
	constexpr unsigned int kDamage = 1u << 4;
	constexpr unsigned int kFrozen = 1u << 5;
	constexpr unsigned int kDie = 1u << 6;	    // 死亡(復帰しない終端)
	constexpr unsigned int kFlee = 1u << 7;	    // 逃走(士気が折れた)
	constexpr unsigned int kBurst = 1u << 8;	// 断末魔の溜め(死体の姿勢を止める)
	constexpr unsigned int kNotice = 1u << 9;	// 攻撃通知(当たる直前の予告)
	constexpr unsigned int kCarry = 1u << 10;   // ひよこを咥えて逃げている
}