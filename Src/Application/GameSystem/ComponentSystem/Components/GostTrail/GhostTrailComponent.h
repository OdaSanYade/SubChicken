#pragma once

//History...残像
//XRay...透過表示
enum class GhostMode
{
	History,
	XRay,
};

struct GhostTrailComponent
{
	static const int kMaxFrames = 8;
	static const int kMaxBones = 300;

	GhostMode    mode = GhostMode::History;

	MaterialId   materialId;
	float        spacing = 0.5f;
	float        lifeTime = 0.4f;
	bool         recording = false;

	int          head = 0;
	int          count = 0;
	Math::Vector3 lastPos = {};
	bool         hasLast = false;
	float        fadeTimer = 0.0f;

	Math::Matrix worlds[kMaxFrames] = {};
	float        ages[kMaxFrames] = {};

	bool                boneValid[kMaxFrames] = {};
	DirectX::XMFLOAT4X4 bonePoses[kMaxFrames][kMaxBones] = {};

	DirectX::XMFLOAT4 color = { 0.4f, 0.7f, 1.0f, 1.0f };
	DirectX::XMFLOAT3 rimColor = { 1.0f, 1.0f, 1.0f };   // ★輪郭色(2トーン用)
	float             hitPulse = 0.0f;                    // ★ヒット演出パルス(0~1、時間で減衰)
	DirectX::XMFLOAT4 params[4] = {};
};

inline void BeginGhostTrail(GhostTrailComponent& g,
	const std::string& materialName)
{
	SetMaterialId(g.materialId, materialName);
	g.recording = true;

	if (g.color.x == 0 && g.color.y == 0 && g.color.z == 0 && g.color.w == 0)
		g.color = { 0.4f, 0.7f, 1.0f, 1.0f };
}

inline void EndGhostTrail(GhostTrailComponent& g)
{
	g.recording = false;
}

inline void ClearGhostTrail(GhostTrailComponent& g)
{
	g.recording = false;
	g.count = 0;
	g.head = 0;
}

// ★攻撃ヒットなど、外部から呼んで一瞬派手にするためのトリガー
inline void TriggerGhostHitBurst(GhostTrailComponent& g)
{
	g.hitPulse = 1.0f;
}

static_assert(std::is_trivially_copyable_v<GhostTrailComponent>, "GhostTrailComponent must be trivially copyable");