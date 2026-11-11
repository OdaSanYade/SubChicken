#include "MaterialLibrary.h"
#include "../Shaders/ShaderRuntime.h"

bool MaterialLibrary::LoadRegistry(std::unordered_map<std::string, uint32_t>& out) const
{
	out.clear();
	std::ifstream f(m_registryPath);
	if (!f) return false;
	try
	{
		nlohmann::json j;
		f >> j;
		if (!j.contains("ids") || !j["ids"].is_object()) return false;
		for (auto& [name, id] : j["ids"].items())
		{
			if (!id.is_number_unsigned() && !id.is_number_integer()) continue;
			const int64_t v = id.get<int64_t>();
			if (v <= 0 || v > 0xFFFF || name.empty()) continue;   // 0 は標準マテリアル用
			out[name] = (uint32_t)v;
		}
	}
	catch (...) { return false; }
	return true;
}

void MaterialLibrary::SaveRegistry() const
{
	if (m_registryPath.empty()) return;

	nlohmann::json ids = nlohmann::json::object();
	for (const auto& [name, id] : m_nameToId)
		if (id != 0) ids[name] = id;

	nlohmann::json j;
	j["ids"] = ids;

	std::ofstream f(m_registryPath);
	if (!f)
	{
		OutputDebugStringA(("[Material] registry save failed: " + m_registryPath + "\n").c_str());
		return;
	}
	f << j.dump(2);
}

uint32_t MaterialLibrary::Assign(const std::string& name)
{
	auto it = m_nameToId.find(name);
	if (it != m_nameToId.end()) return it->second;

	const uint32_t id = (uint32_t)m_names.size();
	m_names.push_back(name);
	m_nameToId[name] = id;
	return id;
}

void MaterialLibrary::BuildFromRuntime(ShaderRuntime* rt, const std::string& registryPath)
{
	m_registryPath = registryPath;
	m_names.clear();
	m_nameToId.clear();
	m_names.push_back("");        // id 0 = デフォルト
	m_nameToId[""] = 0;

	// 1) 登録ファイルの番号をそのまま使う（今は無いマテリアルも番号は空けておく）
	std::unordered_map<std::string, uint32_t> reg;
	LoadRegistry(reg);
	uint32_t maxId = 0;
	for (const auto& [name, id] : reg) if (id > maxId) maxId = id;
	m_names.resize((size_t)maxId + 1);
	for (const auto& [name, id] : reg)
	{
		m_names[id] = name;
		m_nameToId[name] = id;
	}

	// 2) ファイルに無いマテリアルは後ろに足す（ファイルが無い初回は従来と同じ並び）
	bool added = false;
	for (auto& n : rt->GetMaterialNames())
	{
		if (m_nameToId.find(n) != m_nameToId.end()) continue;
		Assign(n);
		added = true;
	}

	if (added || reg.empty()) SaveRegistry();

	SetMaterialNameTable(m_names);
}

bool MaterialLibrary::SyncFromRuntime(ShaderRuntime* rt)
{
	bool added = false;
	for (auto& n : rt->GetMaterialNames())
	{
		if (m_nameToId.find(n) != m_nameToId.end()) continue;
		Assign(n);
		added = true;
	}
	if (added)
	{
		SaveRegistry();
		SetMaterialNameTable(m_names);
	}
	return added;
}

uint32_t MaterialLibrary::GetId(const std::string& name) const
{
	auto it = m_nameToId.find(name);
	return (it != m_nameToId.end()) ? it->second : 0;
}

const std::string& MaterialLibrary::GetName(uint32_t id) const
{
	static std::string empty;
	return (id < m_names.size()) ? m_names[id] : empty;
}
