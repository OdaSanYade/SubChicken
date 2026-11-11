#pragma once

class StateMachineSystem;

namespace GameScale
{
	// システム内の全ステートを走査し、「長さ・速度」に分類される
	void ApplyToStateParams(StateMachineSystem* _sys);
}