#pragma once
#include "KdUiShader.h"

// ===========================================================
// 操作ヒントの解決
// ===========================================================
namespace KdUiInputHint
{
	//------------------------------------------
	// 最後に触ったデバイス
	//------------------------------------------
	inline bool& GamepadFlag() { static bool f = false; return f; }
	inline bool& DecidedFlag() { static bool f = false; return f; }

	inline bool IsGamepad() { return GamepadFlag(); }

	// 毎フレーム1回呼ぶ。どの画面から呼んでも同じ状態を共有する。
	inline void Update()
	{
		auto& input = KdInputActionSystem::Instance();

		// 初回は接続状況で決める
		if (!DecidedFlag())
		{
			GamepadFlag() = input.IsGamepadConnected(0);
			DecidedFlag() = true;
		}

		const XINPUT_STATE& st = KdXInputCache::Instance().GetState(0);

		const bool padMoved =
			(st.Gamepad.wButtons != 0)
			|| (st.Gamepad.bLeftTrigger > 40) || (st.Gamepad.bRightTrigger > 40)
			|| (std::abs(static_cast<int>(st.Gamepad.sThumbLX)) > 12000)
			|| (std::abs(static_cast<int>(st.Gamepad.sThumbLY)) > 12000)
			|| (std::abs(static_cast<int>(st.Gamepad.sThumbRX)) > 12000)
			|| (std::abs(static_cast<int>(st.Gamepad.sThumbRY)) > 12000);

		if (padMoved)
		{
			GamepadFlag() = true;
			return;
		}

		// 実際に割り当てられているキーだけを見る
		for (const auto& [name, bind] : input.GetActionBindings())
		{
			for (int vk : bind.currentKeyboardKeys)
			{
				if (vk == 0) continue;
				if (GetAsyncKeyState(vk) & 0x8000)
				{
					GamepadFlag() = false;
					return;
				}
			}
		}
	}

	//------------------------------------------
	// ゲームパッド
	//------------------------------------------
	// ★マスクはビットなので、JSONに書いた並び順は残っていない。
	//   この表の並びが優先順位になる。{A,B} なら A が出る。
	inline bool PadSymbol(WORD _mask, int& _shape, int& _code)
	{
		struct Entry { WORD mask; int shape; int code; };

		static const Entry kTable[] = {
			{ XINPUT_GAMEPAD_A,              KdUiShader::IconFace,      0 },	// A
			{ XINPUT_GAMEPAD_B,              KdUiShader::IconFace,      1 },	// B
			{ XINPUT_GAMEPAD_X,              KdUiShader::IconFace,     23 },	// X
			{ XINPUT_GAMEPAD_Y,              KdUiShader::IconFace,     24 },	// Y
			{ XINPUT_GAMEPAD_LEFT_SHOULDER,  KdUiShader::IconShoulder, 11 },	// LB
			{ XINPUT_GAMEPAD_RIGHT_SHOULDER, KdUiShader::IconShoulder, 17 },	// RB
			{ XINPUT_GAMEPAD_START,          KdUiShader::IconFace,     12 },	// Menu
		};

		for (const auto& e : kTable)
		{
			if ((_mask & e.mask) == 0) continue;

			_shape = e.shape;
			_code = e.code;
			return true;
		}
		return false;
	}

	//------------------------------------------
	// キーボード
	//------------------------------------------
	// こちらは配列なので、JSONの並び順がそのまま残る。
	// {Enter, Space} なら先頭の Enter が出る。
	inline bool KeySymbol(int _vk, int& _shape, int& _code)
	{
		if (_vk >= 'A' && _vk <= 'Z')
		{
			_shape = KdUiShader::IconKeycap;
			_code = _vk - 'A';
			return true;
		}

		struct Entry { int vk; int shape; int code; };

		static const Entry kTable[] = {
			{ VK_RETURN, KdUiShader::IconEnter,  -1 },
			{ VK_SPACE,  KdUiShader::IconSpace,  -1 },
		};

		for (const auto& e : kTable)
		{
			if (e.vk != _vk) continue;

			_shape = e.shape;
			_code = e.code;
			return true;
		}

		// 記号を用意していないキーは空のキーキャップにする。
		// 何も出さないより、「キーボードの何か」だと分かる方がまし。
		_shape = KdUiShader::IconKeycap;
		_code = -1;
		return true;
	}

	//------------------------------------------
	// アクション名 → 記号
	//------------------------------------------
	// 今触っているデバイスを優先し、割り当てが無ければもう一方へ落とす。
	// パッド専用/キーボード専用のアクションで記号が消えるのを防ぐ。
	inline bool Resolve(const char* _action, int& _shape, int& _code)
	{
		auto& input = KdInputActionSystem::Instance();

		const auto& binds = input.GetActionBindings();
		auto it = binds.find(_action);
		if (it == binds.end()) return false;

		const auto& b = it->second;

		const bool hasKey = !b.currentKeyboardKeys.empty()
			&& (b.currentKeyboardKeys[0] != 0);

		auto tryPad = [&]() -> bool
			{
				if (b.isTriggerButton)
				{
					_shape = KdUiShader::IconTrigger;
					_code = (b.triggerSide == KdGamepadTrigger::Left) ? 11 : 17;
					return true;
				}
				return PadSymbol(b.currentGamepadMask, _shape, _code);
			};

		auto tryKey = [&]() -> bool
			{
				return hasKey && KeySymbol(b.currentKeyboardKeys[0], _shape, _code);
			};

		if (IsGamepad())
			return tryPad() || tryKey();

		return tryKey() || tryPad();
	}

	//------------------------------------------
	// 書き込みの近道
	//------------------------------------------
	inline void SetButtonIcon(KdUiShader::Instance& _inst, const char* _action)
	{
		int shape = KdUiShader::IconNone;
		int code = -1;

		if (!Resolve(_action, shape, code)) return;

		KdUiShader::SetButtonIcon(_inst, shape, code);
	}

	inline void SetHint(KdUiShader::Instance& _inst, const char* _action,
		const char* _word, float _appear)
	{
		int shape = KdUiShader::IconNone;
		int code = -1;

		Resolve(_action, shape, code);
		KdUiShader::SetHint(_inst, shape, code, _word, _appear);
	}
}