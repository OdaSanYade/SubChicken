#include "ShaderAsset.h"

using json = nlohmann::json;

bool ShaderAsset::LoadFromString(const std::string& jsonStr)
{
	try {
		json j = json::parse(jsonStr);

		type = j.value("type", "effect");
		name = j.value("name", "unnamed");
		ps = j.value("ps", "");
		vs = j.value("vs", "");
		writeMask = j.value("writeMask", 0xFu);

		params.clear();
		if (j.contains("params"))
		{
			for (auto& pj : j["params"])
			{
				ShaderParam p;
				p.name = pj.value("name", "");
				p.type = pj.value("type", "float");
				p.minVal = pj.value("min", 0.0f);
				p.maxVal = pj.value("max", 1.0f);

				// default は float または配列
				if (pj.contains("default"))
				{
					auto& dv = pj["default"];
					if (dv.is_number())
						p.defaultValue[0] = dv.get<float>();
					else if (dv.is_array())
						for (int i = 0; i < (int)dv.size() && i < 4; ++i)
							p.defaultValue[i] = dv[i].get<float>();
				}
				params.push_back(p);
			}
		}
		vertexLayout.clear();
		if (j.contains("vertexLayout"))
		{
			for (auto& vj : j["vertexLayout"])
			{
				VertexElement e;
				e.semantic = vj.value("semantic", "POSITION");
				e.index = vj.value("index", 0);
				e.format = vj.value("format", "float3");
				e.offset = vj.value("offset", -1);   // 省略時 -1（自動）
				vertexLayout.push_back(e);
			}
		}
		return true;
	}
	catch (const std::exception& e) {
		OutputDebugStringA(("ShaderAsset parse error: " + std::string(e.what()) + "\n").c_str());
		return false;
	}
}

bool ShaderAsset::LoadFromFile(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return false;
	std::stringstream ss; ss << f.rdbuf();
	return LoadFromString(ss.str());
}