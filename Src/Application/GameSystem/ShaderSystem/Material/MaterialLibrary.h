#pragma once

class ShaderRuntime;

// マテリアル名 ⇔ 番号の対応表
//  ・番号は登録ファイル（MaterialIds.json）に残し、マテリアルを増やしても既存の番号はずれない
//  ・0 番は「標準マテリアル」
class MaterialLibrary
{
public:
	static constexpr const char* kDefaultRegistryPath = "Asset/Data/Shader/Material/MaterialIds.json";

	void BuildFromRuntime(ShaderRuntime* rt, const std::string& registryPath = kDefaultRegistryPath);

	// ランタイムに増えたマテリアル（ライブリンクで追加されたものなど）を表に足す。足したら true
	bool SyncFromRuntime(ShaderRuntime* rt);

	uint32_t GetId(const std::string& name) const;
	const std::string& GetName(uint32_t id) const;
	size_t Count() const { return m_names.size(); }

private:
	bool LoadRegistry(std::unordered_map<std::string, uint32_t>& out) const;
	void SaveRegistry() const;
	uint32_t Assign(const std::string& name);

	std::vector<std::string> m_names;
	std::unordered_map<std::string, uint32_t> m_nameToId;
	std::string m_registryPath;
};
