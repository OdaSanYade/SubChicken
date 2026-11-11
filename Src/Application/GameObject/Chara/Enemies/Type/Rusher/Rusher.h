#pragma once

#include "../../Core/EnemyBase.h"

// ===========================================================
// Rusher（突撃型）
//
// 速く寄って手数で押す。そのぶん脆く、殴れば必ず崩れる。
// 溜めが短いので単体でも圧があるが、体幹が低いので
// 「見えたら潰す」で処理できる。数で来た時の緊張感を作る役。
// ===========================================================
class Rusher : public EnemyBase
{
public:

	using EnemyBase::EnemyBase;

	Rusher() = default;
	~Rusher()override = default;

protected:

	// ここに書いた値が既定値。EnemyTuning.json の "types" → "Rusher" に書いた項目で上書きされる
	// (調整はまず JSON で行い、決まったらここへ戻すと差分が追いやすい)
	EnemySpec MakeSpec() const override
	{
		EnemySpec s;
		s.typeName = "Rusher";

		s.bodyScale = 6.0f;
		s.hp = 120.0f;

		s.sightRadius = 6.5f;		// 気づくのが早い
		s.loseHysteresis = 1.5f;

		s.poiseMax = 32.0f;		// 弱攻撃2発で崩れる
		s.poiseDecay = 30.0f;
		s.knockbackDecay = 3.0f;		// よく飛ぶ

		s.speedScale = 1.35f;
		s.turnScale = 1.25f;
		s.windupScale = 0.70f;		// 溜めが短い＝距離を取らないと避けにくい
		s.intervalScale = 0.65f;		// 手数で押す
		s.damageScale = 0.70f;
		s.reachScale = 0.90f;
		s.knockbackScale = 0.80f;

		s.slotDistanceOffset = 1.5_m;	// 囲みでも前に出たがる

		s.noticeTime = 0.35f;

		s.fleeMorale = 0.0f;

		// 倒れ際に一撃返す。手数の圧に「倒した後」の緊張を足す。
		// 溜めが見えるので、倒した直後に離れれば避けられる。
		s.deathBurst = true;
		s.burstDamage = 18.0f;
		s.burstRadius = 4.5_m;
		s.burstKnockback = 65.0f;

		return s;
	}
};