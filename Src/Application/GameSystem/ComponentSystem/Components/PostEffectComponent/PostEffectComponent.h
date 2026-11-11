#pragma once

enum class EaseType : uint8_t
{
	Linear,
	EaseInQuad,
	EaseOutQuad,
	EaseInOutQuad,
	EaseInCubic,
	EaseOutCubic,
	EaseInOutCubic,
};

namespace PostHelper
{
	inline float ApplyEase(EaseType type, float t)
	{
		t = std::clamp(t, 0.0f, 1.0f);
		switch (type)
		{
		case EaseType::Linear:        return t;
		case EaseType::EaseInQuad:    return t * t;
		case EaseType::EaseOutQuad:   return t * (2.0f - t);
		case EaseType::EaseInOutQuad: return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
		case EaseType::EaseInCubic:   return t * t * t;
		case EaseType::EaseOutCubic:
		{
			float f = t - 1.0f;
			return f * f * f + 1.0f;
		}
		case EaseType::EaseInOutCubic:
			return t < 0.5f
				? 4.0f * t * t * t
				: 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
		}
		return t;
	}

	inline float LerpValue(float a, float b, float t) { return a + (b - a) * t; }

	template <typename T>
	inline void SetDefault(T& postType)
	{
		T defaultData = {};
		postType = defaultData;
	}
}

// 数値1つを「現在の値→目標値」へイージング付きで遷移させる。
struct Tween
{
	float start = 0.0f;
	float current = 0.0f;
	float target = 0.0f;
	float duration = 0.3f;   // ON/OFF遷移にかける時間(秒)。呼び出し側（コンポーネント）が設定する
	float elapsed = 0.0f;
	EaseType easeType = EaseType::EaseOutQuad;

	// 目標値を設定する。
	void SetTarget(float newTarget)
	{
		if (std::fabs(newTarget - target) < 1e-6f) return;
		start = current;
		target = newTarget;
		elapsed = 0.0f;
	}

	// dt秒進める。戻り値は「まだ遷移中か」
	bool Update(float dt)
	{
		if (duration <= 1e-5f)
		{
			current = target;
			elapsed = duration;
			return false;
		}
		elapsed = std::min(elapsed + dt, duration);
		float t = PostHelper::ApplyEase(easeType, elapsed / duration);
		current = PostHelper::LerpValue(start, target, t);
		return elapsed < duration;
	}

	bool IsAnimating() const { return elapsed < duration; }
	// 「遷移中」または「現在値がまだ0(オフ状態)ではない」ならまだ効果を適用すべき
	bool IsActive() const { return IsAnimating() || std::fabs(current) > 1e-4f; }
};

struct ScreenTone
{
	//画面全体の色味
	bool enabled = false;
	float saturation = 0.0f;         
	EaseType easeType = EaseType::EaseOutQuad;
	float easeDuration = 0.3f;       
	Tween tween;                      
};

struct Abberation
{
	//色収差
	bool enabled = false;
	float strength = 0.0f;
	EaseType easeType = EaseType::EaseOutQuad;
	float easeDuration = 0.3f;
	Tween tween;
};

struct RadialBlur
{
	//放射状ブラー
	bool enabled = false;
	float strength = 0.0f;
	float falloff = 1.0f;
	EaseType easeType = EaseType::EaseOutQuad;
	float easeDuration = 0.3f;
	Tween tween;
};

struct Vignette
{
	//ビネット
	bool enabled = false;
	float strength = 0.0f;
	DirectX::XMFLOAT4 color = { 0.0f, 0.0f, 0.0f, 1.0f };
	EaseType easeType = EaseType::EaseOutQuad;
	float easeDuration = 0.3f;
	Tween tween;
};

struct PostEffectComponent
{
	bool isEditMode = false;

	//画面全体の色味
	ScreenTone screenTone = {};

	//色収差
	Abberation abberation = {};

	//放射状ブラー
	RadialBlur radialBlur = {};

	//ビネット
	Vignette vignette = {};

	//すべてのポストエフェクトを止める
	void AllStopEnabled() 
	{
		screenTone.enabled = false;
		abberation.enabled = false;
		radialBlur.enabled = false;
		vignette.enabled = false;
	}
};

static_assert(std::is_trivially_copyable_v<PostEffectComponent>, "PostEffectComponent must be trivially copyable");