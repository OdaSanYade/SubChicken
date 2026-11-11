#pragma once

enum class LightType : uint8_t
{
	Point = 0,
	Spot = 1,
};

struct LightComponent
{
	LightType         type = LightType::Point;
	bool              enabled = true;
	float             range = 10.0f;
	float             power = 10.0f;
	DirectX::XMFLOAT3 color = { 1.0f, 1.0f, 1.0f };
	float             innerDeg = 15.0f;  
	float             outerDeg = 25.0f;   
	DirectX::XMFLOAT3 offset = { 0, 0, 0 };   
};

static_assert(std::is_trivially_copyable_v<LightComponent>, "LightComponent must be trivially copyable");