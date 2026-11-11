#include "ParticleEffect.h"

//------------------------------------------------------------------------------
// パスの評価
//------------------------------------------------------------------------------
DirectX::XMFLOAT3 ParticleEffect::EvalPath(const std::vector<PathKey>& keys, float t)
{
	if (keys.empty()) return { 0.0f, 0.0f, 0.0f };
	if (keys.size() == 1 || t <= keys.front().time) return keys.front().pos;
	if (t >= keys.back().time) return keys.back().pos;

	for (size_t i = 0; i + 1 < keys.size(); ++i)
	{
		if (t < keys[i].time || t > keys[i + 1].time) continue;

		float span = keys[i + 1].time - keys[i].time;
		float f = (span > 1e-4f) ? ((t - keys[i].time) / span) : 0.0f;
		return {
			keys[i].pos.x + (keys[i + 1].pos.x - keys[i].pos.x) * f,
			keys[i].pos.y + (keys[i + 1].pos.y - keys[i].pos.y) * f,
			keys[i].pos.z + (keys[i + 1].pos.z - keys[i].pos.z) * f };
	}
	return keys.back().pos;
}

void ParticleEffect::SortKeys()
{
	for (auto& L : layers)
	{
		std::sort(L.bursts.begin(), L.bursts.end(),
			[](const BurstEntry& a, const BurstEntry& b) { return a.time < b.time; });
		std::sort(L.path.begin(), L.path.end(),
			[](const PathKey& a, const PathKey& b) { return a.time < b.time; });
	}
}

float ParticleEffect::TotalDuration() const
{
	float longest = 0.0f;
	for (const auto& L : layers)
	{
		if (!L.enabled) continue;

		// このレイヤーが最後に粒を出しうる時刻
		float lastEmit = L.asset.delay;
		for (const auto& b : L.bursts)
			lastEmit = (std::max)(lastEmit, L.asset.delay + b.time);
		for (const auto& k : L.path)
			lastEmit = (std::max)(lastEmit, L.asset.delay + k.time);
		if (L.asset.duration > 0.0f)
			lastEmit = (std::max)(lastEmit, L.asset.delay + L.asset.duration);

		longest = (std::max)(longest, lastEmit + L.asset.lifeMax);
	}
	return longest;
}

//------------------------------------------------------------------------------
// 1レイヤーぶんの読み書き
//   params / texture の中身は ParticleAsset に丸投げする。
//   そのため「レイヤー1つぶんのJSON」は旧形式とまったく同じ構造になる。
//------------------------------------------------------------------------------
static void LayerFromJson(const nlohmann::json& j, ParticleLayer& L)
{
	L.name = j.value("name", L.name);
	L.enabled = j.value("enabled", L.enabled);

	if (j.contains("offset") && j["offset"].is_array() && j["offset"].size() >= 3)
	{
		L.offset.x = j["offset"][0].get<float>();
		L.offset.y = j["offset"][1].get<float>();
		L.offset.z = j["offset"][2].get<float>();
	}

	L.asset.LoadFromString(j.dump());

	L.bursts.clear();
	if (j.contains("bursts") && j["bursts"].is_array())
	{
		for (const auto& b : j["bursts"])
		{
			BurstEntry e;
			e.time = b.value("time", 0.0f);
			e.count = b.value("count", 10);
			if (e.time < 0.0f) e.time = 0.0f;
			if (e.count > 0) L.bursts.push_back(e);
		}
	}

	L.path.clear();
	if (j.contains("path") && j["path"].is_array())
	{
		for (const auto& k : j["path"])
		{
			PathKey p;
			p.time = k.value("time", 0.0f);
			if (k.contains("pos") && k["pos"].is_array() && k["pos"].size() >= 3)
			{
				p.pos.x = k["pos"][0].get<float>();
				p.pos.y = k["pos"][1].get<float>();
				p.pos.z = k["pos"][2].get<float>();
			}
			L.path.push_back(p);
		}
	}
}

static nlohmann::json LayerToJson(const ParticleLayer& L)
{
	// ParticleAsset の出力（name / texture / params）を土台にする
	nlohmann::json j = nlohmann::json::parse(L.asset.ToJsonString());
	j["name"] = L.name;
	j["enabled"] = L.enabled;
	j["offset"] = { L.offset.x, L.offset.y, L.offset.z };

	j["bursts"] = nlohmann::json::array();
	for (const auto& b : L.bursts)
		j["bursts"].push_back({ { "time", b.time }, { "count", b.count } });

	j["path"] = nlohmann::json::array();
	for (const auto& k : L.path)
		j["path"].push_back({ { "time", k.time },
							  { "pos", { k.pos.x, k.pos.y, k.pos.z } } });
	return j;
}

//------------------------------------------------------------------------------
bool ParticleEffect::LoadFromString(const std::string& jsonStr)
{
	try
	{
		nlohmann::json j = nlohmann::json::parse(jsonStr);
		name = j.value("name", name);
		layers.clear();

		if (j.contains("layers") && j["layers"].is_array())
		{
			// 新形式
			for (const auto& lj : j["layers"])
			{
				ParticleLayer L;
				LayerFromJson(lj, L);
				layers.push_back(L);
			}
		}
		else
		{
			// ★旧形式（params がルートにある1エミッタ形式）
			ParticleLayer L;
			L.name = name;
			L.asset.LoadFromString(jsonStr);
			layers.push_back(L);
		}

		if (layers.empty()) layers.push_back(ParticleLayer{});
		SortKeys();
		return true;
	}
	catch (...) { return false; }
}

bool ParticleEffect::LoadFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::stringstream ss; ss << f.rdbuf();
	return LoadFromString(ss.str());
}

std::string ParticleEffect::ToJsonString() const
{
	nlohmann::json j;
	j["version"] = 2;
	j["name"] = name;
	j["layers"] = nlohmann::json::array();
	for (const auto& L : layers)
		j["layers"].push_back(LayerToJson(L));
	return j.dump(2);
}