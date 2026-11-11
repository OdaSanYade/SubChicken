#include "PipelineSystem.h"

static std::string PresetsPathFor(const std::string& pipelinePath)
{
	std::string p = pipelinePath;
	size_t pos = p.find(".pipeline.json");
	if (pos != std::string::npos)
		return p.substr(0, pos) + ".presets.json";
	return p + ".presets.json";
}

void PipelineSystem::Init(ID3D11Device* device, ID3D11DeviceContext* context)
{
	m_device = device;
	m_context = context;

	m_material = std::make_shared<MultiPassRuntime>();
	m_material->Init(device, context);

	m_postEffect = std::make_shared<MultiPassRuntime>();
	m_postEffect->Init(device, context);
}

void PipelineSystem::LoadDefaults(const std::string& materialPath,
	const std::string& effectPath)
{
	m_material->LoadPipelineFromFile(materialPath);
	m_material->SetPresetsPath(PresetsPathFor(materialPath));
	m_material->LoadPresetsFromFile(PresetsPathFor(materialPath));

	m_postEffect->LoadPipelineFromFile(effectPath);
	m_postEffect->SetPresetsPath(PresetsPathFor(effectPath));
	m_postEffect->LoadPresetsFromFile(PresetsPathFor(effectPath));

	m_material->LoadLastParams();

	// 起動時の値（"__last__" 適用後）を「効果なし」のときに戻す既定値にする
	m_material->CaptureDefaults();
	m_postEffect->CaptureDefaults();
}

void PipelineSystem::LoadAuto(const std::string& path)
{
	std::ifstream f(path);
	if (!f) return;
	std::string json((std::istreambuf_iterator<char>(f)),
		std::istreambuf_iterator<char>());

	std::string presetsPath = PresetsPathFor(path);

	if (MultiPassRuntime::IsPostEffectJson(json))
	{
		m_postEffect->LoadPipeline(json);
		m_postEffect->SetPresetsPath(presetsPath);
		m_postEffect->LoadPresetsFromFile(presetsPath);
	}
	else
	{
		m_material->LoadPipeline(json);
		m_material->SetPresetsPath(presetsPath);
		m_material->LoadPresetsFromFile(presetsPath);
	}
}

void PipelineSystem::OnPipelineReceived(const std::string& json)
{
	// 中身で振り分け（MultiPassRuntime の判定を使う）
	if (MultiPassRuntime::IsPostEffectJson(json))
		m_postEffect->LoadPipeline(json);
	else
		m_material->LoadPipeline(json);
}

void PipelineSystem::RenderMaterial(float time, float bass, float mid, float treble,
	const DirectX::XMMATRIX& wvp, const DirectX::XMMATRIX& worldView, const DirectX::XMMATRIX& world,
	int screenW, int screenH, DrawMeshCallback drawMesh)
{
	m_material->Execute(time, bass, mid, treble, wvp, worldView,world,
		screenW, screenH, drawMesh);
	m_material->DrawToScreen(screenW, screenH);

	ID3D11ShaderResourceView* nullSRVs[4] = { nullptr, nullptr, nullptr, nullptr };
	m_context->PSSetShaderResources(20, 4, nullSRVs);
}

void PipelineSystem::RenderPostEffect(float time, float bass, float mid, float treble,
	int screenW, int screenH)
{
	ID3D11ShaderResourceView* screenSRV = m_postEffect->CaptureScreen();
	if (!screenSRV) return;

	m_postEffect->Execute(time, bass, mid, treble,
		DirectX::XMMatrixIdentity(), DirectX::XMMatrixIdentity(),DirectX::XMMatrixIdentity(),
		screenW, screenH, nullptr, screenSRV);
	m_postEffect->DrawToScreen(screenW, screenH);
}

void PipelineSystem::DrawDebugUI(const std::string& assetFolder)
{
	ImGui::Begin("Pipeline System");

	ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "MATERIAL");
	if (ImGui::CollapsingHeader("Material##hdr", ImGuiTreeNodeFlags_DefaultOpen))
		m_material->DrawParamUI("material");

	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "POST EFFECT");
	if (ImGui::CollapsingHeader("Post Effect##hdr", ImGuiTreeNodeFlags_DefaultOpen))
		m_postEffect->DrawParamUI("posteffect");

	ImGui::Separator();
	DrawLoader(assetFolder);

	ImGui::End();
}

void PipelineSystem::SetCamera(const DirectX::XMFLOAT3& camPos,
	const DirectX::XMMATRIX& viewProj)
{
	if (m_material) m_material->SetCamera(camPos, viewProj);
}

void PipelineSystem::UseStaticVS()
{
	if (m_material) m_material->UseStaticVS();
}

void PipelineSystem::UseSkinnedVS()
{
	if (m_material)m_material->UseSkinnedVS();
}

void PipelineSystem::SetBones(const DirectX::XMFLOAT4X4* bones, int count)
{
	if (m_material)m_material->SetBones(bones,count);
}



void PipelineSystem::RefreshCache(const std::string& folder)
{
	m_cache.clear();
	namespace fs = std::filesystem;
	std::error_code ec;
	if (!fs::exists(folder, ec)) return;

	for (auto& entry : fs::directory_iterator(folder, ec))
	{
		if (!entry.is_regular_file()) continue;
		std::string fname = entry.path().filename().string();
		if (fname.find(".pipeline.json") == std::string::npos) continue;

		std::string path = entry.path().string();
		std::ifstream f(path);
		std::string json((std::istreambuf_iterator<char>(f)),
			std::istreambuf_iterator<char>());

		m_cache.push_back({ path, fname,
			MultiPassRuntime::IsPostEffectJson(json) });
	}
	m_cacheDirty = false;
}

void PipelineSystem::DrawLoader(const std::string& folder)
{
	if (m_cacheDirty)
		RefreshCache(folder);

	if (ImGui::Button("Refresh"))
		RefreshCache(folder);
	ImGui::SameLine();

	static char searchBuf[64] = "";
	ImGui::SetNextItemWidth(-1);
	ImGui::InputTextWithHint("##pipesearch", "Search pipelines...",
		searchBuf, sizeof(searchBuf));
	std::string query = searchBuf;
	for (auto& c : query) c = (char)tolower(c);

	auto match = [&](const std::string& fname) {
		if (query.empty()) return true;
		std::string fl = fname;
		for (auto& c : fl) c = (char)tolower(c);
		return fl.find(query) != std::string::npos;
		};

	auto drawRow = [&](const CachedPipeline& pf) {
		ImGui::PushID(pf.path.c_str());
		if (pf.isEffect)
			ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[FX]");
		else
			ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "[MAT]");
		ImGui::SameLine();

		std::string display = pf.fname;
		size_t dot = display.find(".pipeline.json");
		if (dot != std::string::npos) display = display.substr(0, dot);
		ImGui::TextUnformatted(display.c_str());

		ImGui::SameLine();
		float avail = ImGui::GetContentRegionAvail().x;
		ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - 60.0f);
		if (ImGui::Button("Load", ImVec2(56, 0)))
			LoadAuto(pf.path);
		ImGui::PopID();
		};

	int matCount = 0, fxCount = 0;
	for (auto& c : m_cache) { if (c.isEffect) fxCount++; else matCount++; }

	ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "Material (%d)", matCount);
	ImGui::Separator();
	for (auto& c : m_cache)
		if (!c.isEffect && match(c.fname)) drawRow(c);

	ImGui::Spacing(); ImGui::Spacing();

	ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "Post Effect (%d)", fxCount);
	ImGui::Separator();
	for (auto& c : m_cache)
		if (c.isEffect && match(c.fname)) drawRow(c);
}