#pragma once

#include <Xinput.h>
#include <array>

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ゲームパッドのスティック／トリガーを区別するための識別子
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
enum class KdGamepadStick
{
	Left,
	Right,
};

enum class KdGamepadTrigger
{
	Left,
	Right,
};


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdXInputCache
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// XInputGetStateの呼び出しを1フレームにつき最大XUSER_MAX_COUNT回（プレイヤー数分）に抑えるための
// フレーム単位キャッシュ
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// ボタンや軸のクラスが個別にXInputGetStateを呼ぶと、同じプレイヤーの状態を何度も問い合わせる
// ことになり無駄が多いため、フレーム開始時にBeginFrame()を呼んで世代を進め、以後はキャッシュを再利用する
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
class KdXInputCache
{
public:

	static KdXInputCache& Instance()
	{
		static KdXInputCache instance;
		return instance;
	}

	// フレームの最初に呼び出し、キャッシュを無効化する（世代を進める）
	void BeginFrame() { m_generation++; }

	// 指定プレイヤーの現在の入力状態を取得（キャッシュ済みなら再利用）
	const XINPUT_STATE& GetState(int playerIndex);

	// 指定プレイヤーが接続されているか
	bool IsConnected(int playerIndex);

private:

	KdXInputCache() {}

	struct Entry
	{
		XINPUT_STATE state{};
		bool         connected = false;
		unsigned int generation = 0;
	};

	std::array<Entry, XUSER_MAX_COUNT> m_entries;

	unsigned int m_generation = 1;
};


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputButtonForXInput
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// XInputのボタンビットマスク（XINPUT_GAMEPAD_A 等）を使ったボタン入力
// KdInputButtonForWindowsと同じPress/Hold/Releaseのステートマシンを持つ
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
class KdInputButtonForXInput : public KdInputButtonBase
{
public:

	// buttonMask : XINPUT_GAMEPAD_A などのビットフラグ（OR で複数指定可）
	// playerIndex: 0〜3（XUSER_MAX_COUNT-1）
	KdInputButtonForXInput(WORD buttonMask, int playerIndex = 0);

	void Update() override;

	void GetCode(std::vector<int>& ret) const override;

	WORD GetMask() const { return m_buttonMask; }

private:

	WORD m_buttonMask;
	int  m_playerIndex;
};


// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// トリガー(LT/RT)をボタンとして扱うクラス
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// トリガー値(0.0〜1.0)がしきい値を超えたら「押されている」とみなす
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
class KdInputButtonForXInputTrigger : public KdInputButtonBase
{
public:

	KdInputButtonForXInputTrigger(KdGamepadTrigger trigger, float threshold = 0.5f, int playerIndex = 0);

	void Update() override;

	void GetCode(std::vector<int>& ret) const override;

	void SetThreshold(float threshold) { m_threshold = threshold; }
	float GetThreshold() const { return m_threshold; }

private:

	KdGamepadTrigger m_trigger;
	float            m_threshold;
	int              m_playerIndex;
};


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputAxisForXInputStick
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 左右いずれかのアナログスティックを軸情報として扱うクラス
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// 内側デッドゾーン／外側デッドゾーンによる正規化と、Y軸反転オプションを持つ
// スティックのニュートラル付近のノイズ(ドリフト)をデッドゾーンで除去しつつ、
// デッドゾーン境界での入力の飛び（スナップ）が起きないよう滑らかに正規化する
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
class KdInputAxisForXInputStick : public KdInputAxisBase
{
public:

	KdInputAxisForXInputStick(KdGamepadStick stick, int playerIndex = 0);

	void Update() override;

	// innerDeadZone: これより内側の入力は無視（0.0〜1.0）
	// outerDeadZone: これより外側は最大値として扱う（0.0〜1.0）
	void SetDeadZone(float innerDeadZone, float outerDeadZone = 1.0f);

	void SetInvertY(bool invert) { m_invertY = invert; }

private:

	KdGamepadStick m_stick;
	int            m_playerIndex;

	float m_innerDeadZone;
	float m_outerDeadZone = 1.0f;

	bool m_invertY = false;
};


// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// トリガー(LT/RT)をアナログ軸(x値のみ使用、0.0〜1.0)として扱うクラス
// アクセル/ブレーキの踏み込み具合や、押し込みの強さで演出を変えたい時などに使用
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
class KdInputAxisForXInputTrigger : public KdInputAxisBase
{
public:

	KdInputAxisForXInputTrigger(KdGamepadTrigger trigger, int playerIndex = 0);

	void Update() override;

private:

	KdGamepadTrigger m_trigger;
	int              m_playerIndex;
};
