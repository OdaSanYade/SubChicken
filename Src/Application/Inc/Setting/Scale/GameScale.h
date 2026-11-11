#pragma once

// ===========================================================
// GameScale
//
// シャドウマップや、パーティクルなど見た目に伴ってくる部分で、
// ワールドの縮尺を変えたい場合がある。
// 
// ワールドの縮尺を1箇所で決める。
// kWorld を変えるとサイズだけが変わり、見た目とタイミングは変わらない。
//
//   _m   : 長さ(m)        … 距離・半径・オフセット・モデルScale
//   _mps : 速度(m/秒)     … 移動速度・ジャンプ力・ノックバック
// 
// この実装の重力は「毎フレーム速度に倍率を掛ける」形なので、
// 速度を一律に縮めれば軌道だけが縮み、所要フレーム数は保たれる。
// ===========================================================
namespace GameScale
{
	inline constexpr float kWorld = 1.0f / 5.0f;	// ← 全部終わったらこちらへ

	inline constexpr float L(float _v) { return _v * kWorld; }

	// 生成直後に1回呼ぶ。モデルの大きさと初期位置をまとめて縮める。
	// 引数で型が決まるので include は不要。
	template<class Transform>
	inline void ApplyToTransform(Transform& _tc)
	{
		_tc.SetScale(_tc.GetScale() * kWorld);
		_tc.SetPosition(_tc.GetPosition() * kWorld);
	}

	// 段差・吸着。ここも長さしか入らない。
	template<class MoveConfig>
	inline void ApplyToMoveConfig(MoveConfig& _cfg, float _stepHeight, float _snapDistance)
	{
		_cfg.stepHeight = _stepHeight * kWorld;
		_cfg.snapDistance = _snapDistance * kWorld;
	}
}

constexpr float operator""_m(long double _v) { return static_cast<float>(_v) * GameScale::kWorld; }
constexpr float operator""_m(unsigned long long _v) { return static_cast<float>(_v) * GameScale::kWorld; }
constexpr float operator""_mps(long double _v) { return static_cast<float>(_v) * GameScale::kWorld; }
constexpr float operator""_mps(unsigned long long _v) { return static_cast<float>(_v) * GameScale::kWorld; }