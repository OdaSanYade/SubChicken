#pragma once

// ===========================================================
// フレームレート非依存の補間
//
// lerp(a, b, rate * dt) は rate*dt が 1 を超えるとオーバーシュートし、
// フレームレートが揺れると追従速度そのものが変わってしまう。
// 1 - exp(-λ*dt) を使うと、どのdtでも「λ秒あたり同じ割合だけ近づく」
// 挙動になる。CameraComponent が既にこの方式なので、ゲーム側も揃える。
//
// λの目安: 5=ゆったり / 10=標準 / 20=機敏 / 30以上=ほぼ即時
// ===========================================================
namespace Damp
{
	// 補間係数。lerp/slerp の t にそのまま渡す。
	inline float Factor(float _lambda, float _dt)
	{
		return 1.0f - std::exp(-_lambda * _dt);
	}

	inline float Value(float _now, float _target, float _lambda, float _dt)
	{
		return _now + (_target - _now) * Factor(_lambda, _dt);
	}

	inline Math::Vector3 Vector(const Math::Vector3& _now, const Math::Vector3& _target,
		float _lambda, float _dt)
	{
		return Math::Vector3::Lerp(_now, _target, Factor(_lambda, _dt));
	}

	inline Math::Quaternion Rotation(const Math::Quaternion& _now, const Math::Quaternion& _target,
		float _lambda, float _dt)
	{
		return Math::Quaternion::Slerp(_now, _target, Factor(_lambda, _dt));
	}

	// ease-out。0→1 を、最初に速く動いて終わりで減速する曲線へ変換する。
	// 踏み込みや吸い込みなど「勢いよく出て、ぴたりと止まる」動きに使う。
	inline float EaseOut(float _t01, float _power = 3.0f)
	{
		const float t = std::clamp(_t01, 0.0f, 1.0f);
		return 1.0f - std::pow(1.0f - t, _power);
	}
}