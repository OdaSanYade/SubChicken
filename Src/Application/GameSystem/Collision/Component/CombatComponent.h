#pragma once
// =====================================================================
//  CombatComponent.h
//  被弾できるもののハートボックス＋被弾結果（プル型）。
//  攻撃側は CollisionWorld::SubmitHitbox() で攻撃を提出する。
// =====================================================================
#include "../Const/CollisionTypes.h"

namespace Collision
{
	struct CombatComponent
	{
		// ---- 設定／毎フレーム更新 ----
		Team       team = Team::Neutral;
		bool       hittable = true;      // 無敵中は false
		HitVolume  hurtVolume;               // オーナーが毎フレーム更新

		// ---- 出力（オーナーが読む）----
		std::vector<HitData> received;       // このフレームに受けた被弾（毎フレームクリア）
	};
}
