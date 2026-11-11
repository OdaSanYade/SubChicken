#include "KdInputKeyNames.h"

#include <unordered_map>
#include <Windows.h>

namespace
{
	// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
	// キーボード対応表
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// A-Z、0-9はASCIIコードとVKコードが一致するため、そのまま文字列化して自動生成する
	// それ以外の特殊キーのみ手動でテーブルに登録する
	// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
	const std::unordered_map<std::string, int>& GetKeyboardTable()
	{
		static std::unordered_map<std::string, int> table;

		if (table.empty())
		{
			// A-Z
			for (char c = 'A'; c <= 'Z'; ++c)
			{
				table[std::string(1, c)] = (int)c;
			}

			// 0-9
			for (char c = '0'; c <= '9'; ++c)
			{
				table[std::string(1, c)] = (int)c;
			}

			// F1-F12
			for (int i = 0; i < 12; ++i)
			{
				table["F" + std::to_string(i + 1)] = VK_F1 + i;
			}

			table["Space"]       = VK_SPACE;
			table["Enter"]       = VK_RETURN;
			table["Escape"]      = VK_ESCAPE;
			table["Tab"]         = VK_TAB;
			table["Backspace"]   = VK_BACK;
			table["Shift"]       = VK_SHIFT;
			table["LeftShift"]   = VK_LSHIFT;
			table["RightShift"]  = VK_RSHIFT;
			table["Ctrl"]        = VK_CONTROL;
			table["LeftCtrl"]    = VK_LCONTROL;
			table["RightCtrl"]   = VK_RCONTROL;
			table["Alt"]         = VK_MENU;
			table["LeftAlt"]     = VK_LMENU;
			table["RightAlt"]    = VK_RMENU;
			table["Up"]          = VK_UP;
			table["Down"]        = VK_DOWN;
			table["Left"]        = VK_LEFT;
			table["Right"]       = VK_RIGHT;
			table["LeftMouse"]   = VK_LBUTTON;
			table["RightMouse"]  = VK_RBUTTON;
			table["MiddleMouse"] = VK_MBUTTON;
		}

		return table;
	}

	const std::unordered_map<int, std::string>& GetKeyboardReverseTable()
	{
		static std::unordered_map<int, std::string> table;

		if (table.empty())
		{
			for (auto& [name, code] : GetKeyboardTable())
			{
				table[code] = name;
			}
		}

		return table;
	}

	// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
	// ゲームパッドボタン対応表
	// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
	const std::unordered_map<std::string, WORD>& GetGamepadButtonTable()
	{
		static const std::unordered_map<std::string, WORD> table =
		{
			{ "A",             XINPUT_GAMEPAD_A },
			{ "B",             XINPUT_GAMEPAD_B },
			{ "X",             XINPUT_GAMEPAD_X },
			{ "Y",             XINPUT_GAMEPAD_Y },
			{ "LeftShoulder",  XINPUT_GAMEPAD_LEFT_SHOULDER },
			{ "RightShoulder", XINPUT_GAMEPAD_RIGHT_SHOULDER },
			{ "LeftThumb",     XINPUT_GAMEPAD_LEFT_THUMB },
			{ "RightThumb",    XINPUT_GAMEPAD_RIGHT_THUMB },
			{ "Start",         XINPUT_GAMEPAD_START },
			{ "Back",          XINPUT_GAMEPAD_BACK },
			{ "DPadUp",        XINPUT_GAMEPAD_DPAD_UP },
			{ "DPadDown",      XINPUT_GAMEPAD_DPAD_DOWN },
			{ "DPadLeft",      XINPUT_GAMEPAD_DPAD_LEFT },
			{ "DPadRight",     XINPUT_GAMEPAD_DPAD_RIGHT },
		};

		return table;
	}
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputKeyNames
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

int KdInputKeyNames::KeyNameToVK(const std::string& name)
{
	auto& table = GetKeyboardTable();

	auto it = table.find(name);
	if (it == table.end())
	{
		assert(0 && "KdInputKeyNames::KeyNameToVK:未知のキー名です（対応表に追加してください）");
		return 0;
	}

	return it->second;
}

std::string KdInputKeyNames::VKToKeyName(int vkCode)
{
	auto& table = GetKeyboardReverseTable();

	auto it = table.find(vkCode);
	if (it == table.end())
	{
		assert(0 && "KdInputKeyNames::VKToKeyName:名前が登録されていないVKコードです");
		return "";
	}

	return it->second;
}

WORD KdInputKeyNames::ButtonNameToMask(const std::string& name)
{
	auto& table = GetGamepadButtonTable();

	auto it = table.find(name);
	if (it == table.end())
	{
		assert(0 && "KdInputKeyNames::ButtonNameToMask:未知のボタン名です（対応表に追加してください）");
		return 0;
	}

	return it->second;
}

std::vector<std::string> KdInputKeyNames::MaskToNames(WORD mask)
{
	std::vector<std::string> names;

	for (auto& [name, bit] : GetGamepadButtonTable())
	{
		if (mask & bit)
		{
			names.push_back(name);
		}
	}

	return names;
}

KdGamepadStick KdInputKeyNames::StickNameToEnum(const std::string& name)
{
	if (name == "RightStick") { return KdGamepadStick::Right; }

	if (name != "LeftStick")
	{
		assert(0 && "KdInputKeyNames::StickNameToEnum:未知のスティック名です（\"LeftStick\"/\"RightStick\"のみ対応）");
	}

	return KdGamepadStick::Left;
}

std::string KdInputKeyNames::StickEnumToName(KdGamepadStick stick)
{
	return (stick == KdGamepadStick::Left) ? "LeftStick" : "RightStick";
}

KdGamepadTrigger KdInputKeyNames::TriggerNameToEnum(const std::string& name)
{
	if (name == "RightTrigger") { return KdGamepadTrigger::Right; }

	if (name != "LeftTrigger")
	{
		assert(0 && "KdInputKeyNames::TriggerNameToEnum:未知のトリガー名です（\"LeftTrigger\"/\"RightTrigger\"のみ対応）");
	}

	return KdGamepadTrigger::Left;
}

std::string KdInputKeyNames::TriggerEnumToName(KdGamepadTrigger trigger)
{
	return (trigger == KdGamepadTrigger::Left) ? "LeftTrigger" : "RightTrigger";
}
