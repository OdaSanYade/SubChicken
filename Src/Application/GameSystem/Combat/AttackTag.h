#pragma once
namespace PlayerAttackTag
{
	constexpr uint32_t kNormal = 0;	// コンボ1・2段目
	constexpr uint32_t kCombo3 = 1;	// コンボ締め
	constexpr uint32_t kCounter = 2;	// ジャスト回避カウンター
	constexpr uint32_t kFinisher = 3;	// カウンター成功後の追撃
	constexpr uint32_t kHeavy = 4;	// 強攻撃(溜めて振る)
	constexpr uint32_t kSmash = 5;	// 硬化した敵を砕く一撃
	constexpr uint32_t kShatter = 6;	// 砕けた敵の破片が別の敵へ当たる(連鎖)
}

namespace CryTag
{
	constexpr uint32_t kShort = 0;	// チョイ鳴き
	constexpr uint32_t kFull = 1;	// 全力鳴き
}