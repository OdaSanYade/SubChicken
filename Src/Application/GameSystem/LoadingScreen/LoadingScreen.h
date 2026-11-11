#pragma once
#include "Task/LoadTask.h"

class LoadingScreen
{
public:

	LoadingScreen() = default;
	~LoadingScreen() { Release(); }

	LoadingScreen(const LoadingScreen&) = delete;
	LoadingScreen& operator = (const LoadingScreen&) = delete;

	//------------------------------------------------------------
	// _fontName : OSにインストール済みのフォント名
	// _fontNo   : KdFontManager の登録番号
	//------------------------------------------------------------
	void Init(int _screenW, int _screenH,
		const std::string& _fontName = "MS ゴシック",
		int _fontHeight = 26,
		int _fontNo = 9);

	void Release();

	//------------------------------------------------------------
	// 使用する文字を事前に焼いておく
	// ロード中に未生成の文字へ当たるとGDIラスタライズが走り
	// ロード画面自体がカクつくので必ず呼ぶ
	//------------------------------------------------------------
	void Warmup(const LoadTask* _tasks, size_t _count);

	//------------------------------------------------------------
	// 起動時 : パーセントと説明を出す
	// シーン遷移時 : 「ロード中…」だけ出す
	//------------------------------------------------------------
	void SetShowPercent(bool _show) { m_showPercent = _show; }

	// _progress : 0.0〜1.0  _labelUtf8 : 左に出す説明(UTF-8)
	void Update(float _progress, const char* _labelUtf8);

	void Draw();

	// 進捗バーの先端で跳ねる走者
	void DrawRunner(int _barLeft, int _barY, int _barW);

	// 位相をずらして跳ねる点列
	void DrawDots(int _rightX, int _baseY);

private:

	// UTF-8文字列からフォントスプライトを取得(生成済みなら使い回す)
	std::shared_ptr<KdFontSprite> GetSprite(const std::string& _utf8);

	// 左下を(_x,_y)として文字列を描く
	void DrawString(const std::shared_ptr<KdFontSprite>& _sprite,
		int _x, int _y, const Math::Color& _color);

	// 単色矩形。左下が(_x,_y)
	void DrawRect(int _x, int _y, int _w, int _h, const Math::Color& _color);

	int m_screenW = 1280;
	int m_screenH = 720;
	int m_fontNo = 9;
	int m_fontHeight = 26;

	bool m_showPercent = true;

	// 表示用の進捗。実値へ追従させて滑らかに見せる
	// 目標値は単調増加させ、絶対に戻らないようにする
	float m_targetProgress = 0.0f;
	float m_viewProgress = 0.0f;

	float m_elapsed = 0.0f;
	std::chrono::steady_clock::time_point m_lastTime;
	bool m_timeValid = false;

	std::string m_labelUtf8;

	// 生成済みスプライト(キーはUTF-8のまま)
	std::unordered_map<std::string, std::shared_ptr<KdFontSprite>> m_cache;

	// 矩形描画用の1x1白テクスチャ
	std::shared_ptr<KdTexture> m_whiteTex;
};