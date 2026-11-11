#pragma once

struct ShaderParam
{
	std::string name;
	std::string type;
	std::array<float, 4> defaultValue{ 0,0,0,0 };
	float minVal = 0.0f;
	float maxVal = 1.0f;
};

struct VertexElement
{
	std::string semantic;
	int index = 0;
	std::string format;
	int offset = -1;
};

struct ShaderAsset
{
	std::string type;
	std::string name;
	std::vector<ShaderParam> params;
	std::vector<VertexElement> vertexLayout;
	std::string vs;
	std::string ps;
	unsigned int writeMask = 0xF;

	bool LoadFromFile(const std::string& path);
	bool LoadFromString(const std::string& json);
};