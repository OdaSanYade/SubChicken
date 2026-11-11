#pragma once

#include "../../Core/EnemyBase.h"

// ===========================================================
// Normal
//
// 全ての基準になる個体。他の種類はこの値を基準に伸縮させる。
// 数値を調整する時は、まずここを触ってから他の種類の倍率を見直すこと。
// ===========================================================
class Normal : public EnemyBase
{
public:

	using EnemyBase::EnemyBase;

	Normal() = default;
	~Normal()override = default;

protected:

	// ここに書いた値が既定値。EnemyTuning.json の "types" → "Normal" に書いた項目で上書きされる
	// (調整はまず JSON で行い、決まったらここへ戻すと差分が追いやすい)
	EnemySpec MakeSpec() const override
	{
		EnemySpec s;
		s.typeName = "Normal";

		s.bodyScale = 5.0f;
		s.fleeMorale = 0.35f;
		s.fleeHealth = 0.5f;

		return s;
	}
};