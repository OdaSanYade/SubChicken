#include "BootTaskTable.h"

#include "../../../main.h"
#include "../../../Scene/SceneManager.h"
#include "../../ShaderSystem/ShaderRender.h"

namespace
{
	// ------------------------------------------------------------
	// 各タスクの中身
	// ここに処理を書くのではなく、各システムの関数を呼ぶだけにする
	// ------------------------------------------------------------

	void Task_AssetMeta()
	{
		ASSET_MGR.Init();
	}

	void Task_Audio()
	{
		auto& audio = AudioManager::Instance();
		audio.Init();
		RES_MGR.GetDataList()->Register("sound_bank", "soundBank");
		audio.LoadBank(RES_MGR.GetDataList()->Get("soundBank"));
	}

	void Task_Input()
	{
		KdInputActionSystem::Instance().Init(2);

		const bool ok = KdInputActionSystem::Instance()
			.LoadActionDefinitions("Asset/Data/SystemData/Input/input_actions.json");

		if (!ok)
		{
			KdInputActionSystem::Instance()
				.RegisterButtonAction("Jump", { VK_SPACE }, XINPUT_GAMEPAD_A, 0);
		}

		// ゲームパッドの接続監視
		KdInputActionSystem::Instance().SetGamepadConnectionListener(
			[](int playerIndex, bool connected)
			{
				if (connected)
				{
					KdInputActionSystem::Instance()
						.SetVibration(playerIndex, 0.5f, 0.5f, 0.2f);
				}
			});

		// デフォルトコンフィグの上書き
		KdInputActionSystem::Instance().LoadBindings("Save/input_bindings.json");
	}

	// --- モデル ---
	// 3段に割ってあるので、スロット単位で進捗が出せる
	size_t Model_Prepare() { return RES_MGR.GetModelList()->Prepare(); }
	bool   Model_Slot(size_t i) { return RES_MGR.GetModelList()->LoadSlot(i); }
	void   Model_Commit() { RES_MGR.GetModelList()->Commit(); }

	// --- テクスチャ ---
	size_t Texture_Prepare() { return RES_MGR.GetTexList()->Prepare(); }
	bool   Texture_Slot(size_t i) { return RES_MGR.GetTexList()->LoadSlot(i); }
	void   Texture_Commit() { RES_MGR.GetTexList()->Commit(); }

	void Task_Scene()
	{
		// シングルトンの初回アクセスで暗黙に走っていた初期化を
		// ここで明示的に起こす
		SceneManager::Instance().Init();
	}

	void Task_ResourceSub()
	{
		RES_MGR.CreateSubSystems();
		RES_MGR.InitConfigs();
	}

	// --- シェーダ ---
	void   Shader_Runtime() { ShaderRenderSystem::InitRuntime(); }
	size_t Shader_CompilePrepare() { return ShaderRenderSystem::PrepareCompile(); }
	bool   Shader_CompileSlot(size_t i) { return ShaderRenderSystem::CompileSlot(i); }
	void   Shader_CompileCommit() { ShaderRenderSystem::CommitCompile(); }
	size_t Shader_LayoutPrepare() { return ShaderRenderSystem::PrepareLayout(); }
	bool   Shader_LayoutSlot(size_t i) { return ShaderRenderSystem::LayoutSlot(i); }
	void   Shader_Pipeline() { ShaderRenderSystem::InitPipeline(); }
	size_t Shader_MeshPrepare() { return ShaderRenderSystem::PrepareMesh(); }
	bool   Shader_MeshSlot(size_t i) { return ShaderRenderSystem::MeshSlot(i); }
	void   Shader_MeshCommit() { ShaderRenderSystem::CommitMesh(); }

	// ------------------------------------------------------------
	// タスク表
	//
	// 配列の並び順がそのまま実行順になる
	// weight は相対値。分割タスクはスロット数で自動的に細分化される
	// ------------------------------------------------------------
		// ------------------------------------------------------------
	// タスク表
	//
	// 配列の並び順がそのまま実行順になる
	// weight は相対値。分割タスクはスロット数で自動的に細分化される
	//
	// メンバ名を明示して書く。位置だけで並べると
	// LoadTask にメンバを足したときに全行が静かにずれる
	// ------------------------------------------------------------
	const LoadTask kTable[] =
	{
		{
			.label = U8("アセット情報を構築中"),
			.stage = 0,
			.func = &Task_AssetMeta,
			.weight = 8.0f,
		},
		{
			.label = U8("リソースを準備中"),
			.stage = 0,
			.func = &Task_ResourceSub,
			.weight = 2.0f,
		},
		{
			.label = U8("サウンドを初期化中"),
			.stage = 0,
			.func = &Task_Audio,
			.weight = 3.0f,
		},
		{
			.label = U8("入力設定を読み込み中"),
			.stage = 0,
			.func = &Task_Input,
			.weight = 2.0f,
		},
		{
			.label = U8("モデルを読み込み中"),
			.stage = 1,
			.prepare = &Model_Prepare,
			.slot = &Model_Slot,
			.commit = &Model_Commit,
			.weight = 40.0f,
		},
		{
			.label = U8("画像を読み込み中"),
			.stage = 1,
			.prepare = &Texture_Prepare,
			.slot = &Texture_Slot,
			.commit = &Texture_Commit,
			.weight = 25.0f,
		},
		{
			.label = U8("描画システムを起動中"),
			.stage = 2,
			.func = &Shader_Runtime,
			.weight = 2.0f,
		},
		{
			.label = U8("シェーダをコンパイル中"),
			.stage = 2,
			.prepare = &Shader_CompilePrepare,
			.slot = &Shader_CompileSlot,
			.commit = &Shader_CompileCommit,
			.weight = 30.0f,
		},
		{
			.label = U8("マテリアルを構築中"),
			.stage = 2,
			.prepare = &Shader_LayoutPrepare,
			.slot = &Shader_LayoutSlot,
			.weight = 6.0f,
		},
		{
			.label = U8("描画設定を読み込み中"),
			.stage = 2,
			.func = &Shader_Pipeline,
			.weight = 10.0f,
		},
		{
			.label = U8("メッシュを登録中"),
			.stage = 2,
			.prepare = &Shader_MeshPrepare,
			.slot = &Shader_MeshSlot,
			.commit = &Shader_MeshCommit,
			.weight = 20.0f,
		},
		{
			.label = U8("シーンを準備中"),
			.stage = 3,
			.func = &Task_Scene,
			.weight = 8.0f,
		},
	};
}

const LoadTask* BootTasks::GetTable() { return kTable; }
size_t          BootTasks::GetCount() { return std::size(kTable); }