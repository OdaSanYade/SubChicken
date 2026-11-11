#include "Pch.h"
#include "GameScaleStates.h"
#include "GameScale.h"
#include "../../../GameSystem/StateMachine/StateMachineManager.h"

namespace
{
	// ここに載っている名前のパラメータだけ縮尺が掛かる。分岐ではなくテーブル引き。
	constexpr std::string_view kScaledParamNames[] = {
		// ---- 速度(m/秒) ----
		"moveSpeed", "jumpPower", "dodgeSpeed", "stepSpeed", "backSpeed",
		"maxStep", "knockback",
		// ---- 長さ(m) ----
		"reach", "radius", "keepDist",
		"attackRange", "wishRange", "arriveSlack", "leaveSlack",
		// ---- ボス ----
		"stopRange", "leaveRange", "keepRange", "loseDistance",
	};

	bool IsScaledParam(std::string_view _name)
	{
		for (const auto& n : kScaledParamNames)
		{
			if (n == _name) return true;
		}
		return false;
	}
}

void GameScale::ApplyToStateParams(StateMachineSystem* _sys)
{
	if (!_sys) return;
	if (GameScale::kWorld == 1.0f) return;	// 等倍なら触らない

	for (size_t i = 0; i < _sys->GetStateCount(); ++i)
	{
		StateDesc* desc = _sys->GetMutable(static_cast<uint16_t>(i));
		if (!desc) continue;

		auto& params = desc->params;

		for (size_t k = 0; k < params.GetFloatCount(); ++k)
		{
			// SetFloat へ渡す前に名前を複製する。
			// GetFloatName は内部の文字列を指しているため、
			// そのまま渡すと自己参照になって読みにくい。
			const std::string name = params.GetFloatName(k);
			if (!IsScaledParam(name)) continue;

			// 同じキーなので上書きになる(要素は増えない)
			params.SetFloat(name, params.GetFloatAt(k) * GameScale::kWorld);
		}
	}
}