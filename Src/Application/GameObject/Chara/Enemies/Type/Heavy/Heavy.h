#pragma once

#include "../../Core/EnemyBase.h"

// ===========================================================
// Heavy（重装型）
//
// 硬くて痛いが、溜めが長く動きが鈍い。
// 「見てから避けられるが、無視すると事故る」位置づけ。
// 体幹が高いので、崩すにはコンボの最終段や必殺が要る。
// 硬化させて砕く(Frozen → Shatter)の主な標的でもある。
// ===========================================================
class Heavy : public EnemyBase
{
public:

	using EnemyBase::EnemyBase;

	Heavy() = default;
	~Heavy()override = default;

protected:

	// ここに書いた値が既定値。EnemyTuning.json の "types" → "Heavy" に書いた項目で上書きされる
	// (調整はまず JSON で行い、決まったらここへ戻すと差分が追いやすい)
	EnemySpec MakeSpec() const override
	{
		EnemySpec s;
		s.typeName = "Heavy";

		s.bodyScale = 8.0f;		// 見た目で「硬い」と分かるようにする
		s.hp = 300.0f;

		s.eyeHeight = 0.8f;		// 体格に合わせて被弾演出の高さも上げる
		s.sightRadius = 4.5f;		// 鈍い。気づくのが遅い
		s.sightOffset = 2.5f;
		s.loseHysteresis = 0.5f;		// 一度見失うと諦めやすい

		s.poiseMax = 100.0f;	// 弱攻撃では崩れない
		s.poiseDecay = 18.0f;
		s.knockbackDecay = 9.0f;		// 押しても下がらない

		s.speedScale = 0.65f;
		s.turnScale = 0.55f;		// 回り込みが有効になる
		s.windupScale = 1.60f;		// 長い溜め＝読み切れる
		s.intervalScale = 1.45f;
		s.damageScale = 1.90f;
		s.reachScale = 1.35f;		// 間合いが広い
		s.knockbackScale = 1.80f;

		// 長い予告。見てから避けられるが、当たると痛い、を成立させる。
		s.noticeTime = 0.62f;

		// 折れることも無いので、最後まで正面から来る。
		s.fleeMorale = 0.0f;

		return s;
	}
};