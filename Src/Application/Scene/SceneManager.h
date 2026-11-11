#pragma once

class BaseScene;

class SceneManager
{
public:

	enum class SceneType
	{
		Title,
		Game
	};
	//enumをハッシュ化
	struct EnumHush {
		template <typename T>
		size_t operator()(T t) const {
			return static_cast<size_t>(t);
		}
	};

	//初期化を明示的に行う
	void Init();

	void PreUpdate();
	void Update();
	void PostUpdate();

	void PreDraw();
	void Draw();
	void DrawSprite();
	void DrawDebug();

	//解放
	void Release();

	// 次のシーンをセット (次のフレームから切り替わる)
	void SetNextScene(SceneType _nextScene);

private:

	// シーン切り替え関数
	void ChangeScene();

	// 現在のシーンのインスタンスを保持しているポインタ
	std::unordered_map<SceneType, std::shared_ptr<BaseScene>, EnumHush> m_sceneMap;
	std::shared_ptr<BaseScene> m_currentScene = nullptr;
	//現在のシーンの種類を保持
	SceneType m_nowSceneType = SceneType::Title;
	// 次のシーンの種類を保持している変数
	SceneType m_nextSceneType = SceneType::Title;
	bool m_isChangeRequest = false;

private:

	// マネージャーの初期化
	SceneManager();
	~SceneManager() {}

public:

	// シングルトンパターン
	static SceneManager& Instance()
	{
		static SceneManager instance;
		return instance;
	}
};
