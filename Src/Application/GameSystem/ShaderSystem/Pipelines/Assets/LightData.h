#pragma once
#pragma once

// C++ の光源データ（HLSL の PointLight と同じレイアウト・48バイト）
struct PointLightData
{
	DirectX::XMFLOAT3 pos;    float range;
	DirectX::XMFLOAT3 color;  float power;
	DirectX::XMFLOAT3 dir;    float cosInner;
	float cosOuter;           float isSpot;
	float _pad[2];
};

inline PointLightData MakePointLight(const DirectX::XMFLOAT3& pos, float range,
	const DirectX::XMFLOAT3& color, float power)
{
	PointLightData l = {};
	l.pos = pos; l.range = range; l.color = color; l.power = power;
	l.isSpot = 0.0f;
	return l;
}

inline PointLightData MakeSpotLight(const DirectX::XMFLOAT3& pos, float range,
	const DirectX::XMFLOAT3& color, float power,
	const DirectX::XMFLOAT3& dir, float innerDeg, float outerDeg)
{
	PointLightData l = {};
	l.pos = pos; l.range = range; l.color = color; l.power = power;
	DirectX::XMVECTOR d = DirectX::XMVector3Normalize(DirectX::XMLoadFloat3(&dir));
	DirectX::XMStoreFloat3(&l.dir, d);
	// 外角 <= 内角 にならないよう保険
	if (outerDeg < innerDeg + 0.5f) outerDeg = innerDeg + 0.5f;
	l.cosInner = cosf(DirectX::XMConvertToRadians(innerDeg));
	l.cosOuter = cosf(DirectX::XMConvertToRadians(outerDeg));
	l.isSpot = 1.0f;
	return l;
}