#include "MaterialNameTable.h"

static std::vector<std::string> g_materialNames = { "" };  

const std::vector<std::string>& MaterialNameTable() { return g_materialNames; }

void SetMaterialNameTable(std::vector<std::string> names)
{
	g_materialNames = std::move(names);
	if (g_materialNames.empty()) g_materialNames.push_back("");
}

uint32_t MaterialIdFromName(const std::string& name)
{
	for (size_t i = 0; i < g_materialNames.size(); ++i)
		if (g_materialNames[i] == name) return (uint32_t)i;
	return 0;
}

const std::string& MaterialNameFromId(uint32_t id)
{
	static const std::string empty;
	return (id < g_materialNames.size()) ? g_materialNames[id] : empty;
}