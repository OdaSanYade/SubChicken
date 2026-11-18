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


	// 矩形描画用の1x1白テクスチャ
	std::shared_ptr<KdTexture> m_whiteTex;
};