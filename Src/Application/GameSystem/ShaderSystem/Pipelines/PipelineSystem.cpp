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
}

void PipelineSystem::LoadDefaults(const std::string& materialPath)
{
	// ファイルロード＋プリセットロード
	m_material->LoadPipelineFromFile(materialPath);
	m_material->SetPresetsPath(PresetsPathFor(materialPath));
	m_material->LoadPresetsFromFile(PresetsPathFor(materialPath));

	// 最終パラメータをロードする
	m_material->LoadLastParams();

	// 起動時の値（何も編集していない素の値）
	m_material->CaptureDefaults();
}

void PipelineSystem::OnPipelineReceived(const std::string& json)
{
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

void PipelineSystem::DrawDebugUI(const std::string& assetFolder)
{
	ImGui::Begin("Pipeline System");

	ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.6f, 1.0f), "MATERIAL");
	if (ImGui::CollapsingHeader("Material##hdr", ImGuiTreeNodeFlags_DefaultOpen))
		m_material->DrawParamUI("material");

	ImGui::Separator();

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