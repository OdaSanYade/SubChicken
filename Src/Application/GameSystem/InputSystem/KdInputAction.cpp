#include "KdInputAction.h"

#include <algorithm>
#include <fstream>
#include <sstream>

using Json = nlohmann::json;


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// 初期化・更新・解放
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

void KdInputActionSystem::Init(int maxGamepadCount)
{
	m_maxGamepadCount = std::clamp(maxGamepadCount, 0, (int)XUSER_MAX_COUNT);

	auto& mgr = KdInputManager::Instance();

	// キーボード／マウス入力をまとめる1つのコレクターを登録
	auto pKeyboard = std::make_unique<KdInputCollector>();
	mgr.AddDevice("Keyboard", pKeyboard);

	// プレイヤー数分のゲームパッド用コレクターを登録
	for (int i = 0; i < m_maxGamepadCount; ++i)
	{
		auto pPad = std::make_unique<KdInputCollector>();
		mgr.AddDevice(GamepadDeviceName(i), pPad);
	}

	m_gamepadConnected.fill(false);
	m_hasLastTimePoint = false;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 毎フレームの更新
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// 1. XInputのフレームキャッシュをリセット
// 2. ゲームパッドの接続状況を監視
// 3. KdInputManagerの入力状況を更新
// 4. 登録済みリスナーへイベントを発火
// 5. 振動タイマーを更新
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdInputActionSystem::Update(float deltaTime)
{
	// deltaTimeが未指定の場合は自前で計測（TimeSystem等と併用する場合は明示的に渡すこと推奨）
	if (deltaTime < 0.0f)
	{
		auto now = std::chrono::steady_clock::now();

		if (m_hasLastTimePoint)
		{
			deltaTime = std::chrono::duration<float>(now - m_lastTimePoint).count();
		}
		else
		{
			deltaTime = 0.0f;
		}

		m_lastTimePoint = now;
		m_hasLastTimePoint = true;
	}

	KdXInputCache::Instance().BeginFrame();

	PollGamepadConnections();

	KdInputManager::Instance().Update();

	DispatchEvents();

	UpdateVibrationTimers(deltaTime);
}

void KdInputActionSystem::Release()
{
	for (int i = 0; i < m_maxGamepadCount; ++i)
	{
		StopVibration(i);
	}

	KdMouseLock::Instance().Release();
	m_actionBindings.clear();
	m_listeners.clear();
	m_nextListenerId = 0;

	KdInputManager::Instance().Release();
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// アクション登録
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

void KdInputActionSystem::RegisterButtonAction(std::string_view actionName,
	const std::vector<int>& keyboardKeys, WORD gamepadButtonMask, int playerIndex)
{
	std::string name(actionName);
	auto& mgr = KdInputManager::Instance();

	if (!keyboardKeys.empty())
	{
		mgr.WorkDevice("Keyboard")->AddButton(name, new KdInputButtonForWindows(keyboardKeys));
	}

	if (gamepadButtonMask != 0 && playerIndex < m_maxGamepadCount)
	{
		mgr.WorkDevice(GamepadDeviceName(playerIndex))->AddButton(name, new KdInputButtonForXInput(gamepadButtonMask, playerIndex));
	}

	ActionBinding binding;
	binding.isAxis = false;
	binding.defaultKeyboardKeys = keyboardKeys;
	binding.currentKeyboardKeys = keyboardKeys;
	binding.defaultGamepadMask = gamepadButtonMask;
	binding.currentGamepadMask = gamepadButtonMask;
	binding.playerIndex = playerIndex;

	m_actionBindings[name] = std::move(binding);
}

void KdInputActionSystem::RegisterTriggerButtonAction(std::string_view actionName,
	KdGamepadTrigger trigger, float threshold, int playerIndex)
{
	std::string name(actionName);
	auto& mgr = KdInputManager::Instance();

	if (playerIndex < m_maxGamepadCount)
	{
		mgr.WorkDevice(GamepadDeviceName(playerIndex))->AddButton(name,
			new KdInputButtonForXInputTrigger(trigger, threshold, playerIndex));
	}

	ActionBinding binding;
	binding.isAxis = false;
	binding.isTriggerButton = true;
	binding.triggerSide = trigger;
	binding.triggerThreshold = threshold;
	binding.playerIndex = playerIndex;

	m_actionBindings[name] = std::move(binding);
}

void KdInputActionSystem::RegisterAxisAction(std::string_view actionName,
	int upKey, int rightKey, int downKey, int leftKey, KdGamepadStick stick, int playerIndex,
	float innerDeadZone, float outerDeadZone, bool invertY)
{
	std::string name(actionName);
	auto& mgr = KdInputManager::Instance();

	if (upKey != 0 || rightKey != 0 || downKey != 0 || leftKey != 0)
	{
		mgr.WorkDevice("Keyboard")->AddAxis(name, new KdInputAxisForWindows(upKey, rightKey, downKey, leftKey));
	}

	if (playerIndex < m_maxGamepadCount)
	{
		auto pStickAxis = new KdInputAxisForXInputStick(stick, playerIndex);

		// innerDeadZoneが0未満の場合はコンストラクタが設定する機種標準値をそのまま使う
		if (innerDeadZone >= 0.0f)
		{
			pStickAxis->SetDeadZone(innerDeadZone, outerDeadZone);
		}

		pStickAxis->SetInvertY(invertY);

		mgr.WorkDevice(GamepadDeviceName(playerIndex))->AddAxis(name, pStickAxis);
	}

	ActionBinding binding;
	binding.isAxis = true;
	binding.currentKeyboardKeys = { upKey, rightKey, downKey, leftKey };
	binding.defaultKeyboardKeys = binding.currentKeyboardKeys;
	binding.stick = stick;
	binding.playerIndex = playerIndex;
	binding.innerDeadZone = innerDeadZone;
	binding.outerDeadZone = outerDeadZone;
	binding.invertY = invertY;

	m_actionBindings[name] = std::move(binding);
}

void KdInputActionSystem::RegisterMouseAxisAction(std::string_view actionName, int fixKey)
{
	std::string name(actionName);
	auto& mgr = KdInputManager::Instance();

	if (fixKey != 0)
	{
		mgr.WorkDevice("Keyboard")->AddAxis(name, new KdInputAxisForWindowsMouse(fixKey));
	}
	else
	{
		mgr.WorkDevice("Keyboard")->AddAxis(name, new KdInputAxisForWindowsMouse());
	}

	// マウス軸はキー/ボタンのリバインド対象外のため ActionBinding には登録しない
}

void KdInputActionSystem::RegisterTriggerAxisAction(std::string_view actionName, KdGamepadTrigger trigger, int playerIndex)
{
	std::string name(actionName);
	auto& mgr = KdInputManager::Instance();

	if (playerIndex < m_maxGamepadCount)
	{
		mgr.WorkDevice(GamepadDeviceName(playerIndex))->AddAxis(name, new KdInputAxisForXInputTrigger(trigger, playerIndex));
	}

	ActionBinding binding;
	binding.isAxis = true;
	binding.isTriggerAxis = true;
	binding.triggerSide = trigger;
	binding.playerIndex = playerIndex;

	m_actionBindings[name] = std::move(binding);
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// 状態取得（ポーリング方式）
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

bool KdInputActionSystem::IsFree(std::string_view actionName) const
{
	return KdInputManager::Instance().IsFree(actionName);
}

bool KdInputActionSystem::IsPress(std::string_view actionName) const
{
	return KdInputManager::Instance().IsPress(actionName);
}

bool KdInputActionSystem::IsHold(std::string_view actionName) const
{
	return KdInputManager::Instance().IsHold(actionName);
}

bool KdInputActionSystem::IsRelease(std::string_view actionName) const
{
	return KdInputManager::Instance().IsRelease(actionName);
}

Math::Vector2 KdInputActionSystem::GetAxis(std::string_view actionName) const
{
	return KdInputManager::Instance().GetAxisState(actionName);
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// イベント方式
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

int KdInputActionSystem::AddListener(std::string_view actionName, KdInputEventType type, Listener callback)
{
	EventListenerEntry entry;
	entry.id = m_nextListenerId++;
	entry.actionName = std::string(actionName);
	entry.type = type;
	entry.callback = std::move(callback);

	int id = entry.id;

	m_listeners.push_back(std::move(entry));

	return id;
}

void KdInputActionSystem::RemoveListener(int listenerHandle)
{
	m_listeners.erase(
		std::remove_if(m_listeners.begin(), m_listeners.end(),
			[listenerHandle](const EventListenerEntry& e) { return e.id == listenerHandle; }),
		m_listeners.end());
}

void KdInputActionSystem::ClearListeners()
{
	m_listeners.clear();
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 登録済みリスナーの発火判定
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// コールバック内でAddListener/RemoveListenerが呼ばれる可能性を考慮し、
// コピーを取ってから回すことでイテレータ破壊を防ぐ
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdInputActionSystem::DispatchEvents()
{
	if (m_listeners.empty()) { return; }

	auto& mgr = KdInputManager::Instance();

	auto listenersCopy = m_listeners;

	for (auto& listener : listenersCopy)
	{
		bool fire = false;

		switch (listener.type)
		{
		case KdInputEventType::Press:   fire = mgr.IsPress(listener.actionName);   break;
		case KdInputEventType::Hold:    fire = mgr.IsHold(listener.actionName);    break;
		case KdInputEventType::Release: fire = mgr.IsRelease(listener.actionName); break;
		}

		if (fire && listener.callback)
		{
			listener.callback();
		}
	}
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// リバインド
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

bool KdInputActionSystem::RebindKeyboard(std::string_view actionName, const std::vector<int>& newKeys, bool allowDuplicate)
{
	std::string name(actionName);

	auto it = m_actionBindings.find(name);
	if (it == m_actionBindings.end())
	{
		assert(0 && "KdInputActionSystem::RebindKeyboard:未登録のアクションです");
		return false;
	}

	// 重複チェック：allowDuplicateがfalseの場合、他アクションと被っていたら何もせず失敗を返す
	if (!allowDuplicate)
	{
		auto conflicts = FindKeyboardKeyConflicts(actionName, newKeys);
		if (!conflicts.empty())
		{
			return false;
		}
	}

	ActionBinding& binding = it->second;
	auto& mgr = KdInputManager::Instance();

	if (binding.isAxis)
	{
		if (newKeys.size() != 4)
		{
			assert(0 && "KdInputActionSystem::RebindKeyboard:軸アクションには上/右/下/左の4キーが必要です");
			return false;
		}

		binding.currentKeyboardKeys = newKeys;

		// AddAxisは同名キーで上書きされるため、既存の登録がそのまま差し替わる
		mgr.WorkDevice("Keyboard")->AddAxis(name, new KdInputAxisForWindows(newKeys[0], newKeys[1], newKeys[2], newKeys[3]));
	}
	else
	{
		binding.currentKeyboardKeys = newKeys;

		mgr.WorkDevice("Keyboard")->AddButton(name, new KdInputButtonForWindows(newKeys));
	}

	return true;
}

bool KdInputActionSystem::RebindGamepadButton(std::string_view actionName, WORD newMask, int playerIndex, bool allowDuplicate)
{
	std::string name(actionName);

	auto it = m_actionBindings.find(name);
	if (it == m_actionBindings.end())
	{
		assert(0 && "KdInputActionSystem::RebindGamepadButton:未登録のアクションです");
		return false;
	}

	if (playerIndex >= m_maxGamepadCount)
	{
		assert(0 && "KdInputActionSystem::RebindGamepadButton:プレイヤーIndexが範囲外です");
		return false;
	}

	// 重複チェック：allowDuplicateがfalseの場合、他アクションと被っていたら何もせず失敗を返す
	if (!allowDuplicate)
	{
		auto conflicts = FindGamepadButtonConflicts(actionName, newMask, playerIndex);
		if (!conflicts.empty())
		{
			return false;
		}
	}

	ActionBinding& binding = it->second;
	binding.currentGamepadMask = newMask;
	binding.playerIndex = playerIndex;
	binding.isTriggerButton = false; // トリガーはマスクではなく閾値式のため、通常ボタンへの変更として扱う

	auto& mgr = KdInputManager::Instance();
	mgr.WorkDevice(GamepadDeviceName(playerIndex))->AddButton(name, new KdInputButtonForXInput(newMask, playerIndex));

	return true;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// リバインド時の重複チェック
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// 対象アクション自身は除外して、他のアクションが同じキー/ボタンを使っていないか調べる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
std::vector<std::string> KdInputActionSystem::FindKeyboardKeyConflicts(std::string_view actionNameToExclude, const std::vector<int>& keys) const
{
	std::vector<std::string> result;
	std::string exclude(actionNameToExclude);

	for (auto& [name, binding] : m_actionBindings)
	{
		if (name == exclude) { continue; }

		bool conflict = false;

		for (int existingKey : binding.currentKeyboardKeys)
		{
			if (existingKey == 0) { continue; }

			for (int newKey : keys)
			{
				if (newKey != 0 && existingKey == newKey)
				{
					conflict = true;
					break;
				}
			}

			if (conflict) { break; }
		}

		if (conflict) { result.push_back(name); }
	}

	return result;
}

std::vector<std::string> KdInputActionSystem::FindGamepadButtonConflicts(std::string_view actionNameToExclude, WORD mask, int playerIndex) const
{
	std::vector<std::string> result;
	std::string exclude(actionNameToExclude);

	for (auto& [name, binding] : m_actionBindings)
	{
		if (name == exclude) { continue; }
		if (binding.isAxis || binding.isTriggerButton) { continue; } // 通常ボタン同士のみ比較対象
		if (binding.playerIndex != playerIndex) { continue; }

		if (binding.currentGamepadMask & mask)
		{
			result.push_back(name);
		}
	}

	return result;
}

void KdInputActionSystem::ResetToDefault(std::string_view actionName)
{
	std::string name(actionName);

	auto it = m_actionBindings.find(name);
	if (it == m_actionBindings.end()) { return; }

	ActionBinding binding = it->second; // コピー（Register内で再設定されるため）

	if (binding.isAxis && !binding.isTriggerAxis)
	{
		RegisterAxisAction(name,
			binding.defaultKeyboardKeys.size() == 4 ? binding.defaultKeyboardKeys[0] : 0,
			binding.defaultKeyboardKeys.size() == 4 ? binding.defaultKeyboardKeys[1] : 0,
			binding.defaultKeyboardKeys.size() == 4 ? binding.defaultKeyboardKeys[2] : 0,
			binding.defaultKeyboardKeys.size() == 4 ? binding.defaultKeyboardKeys[3] : 0,
			binding.stick, binding.playerIndex,
			binding.innerDeadZone, binding.outerDeadZone, binding.invertY);
	}
	else if (!binding.isAxis && !binding.isTriggerButton)
	{
		RegisterButtonAction(name, binding.defaultKeyboardKeys, binding.defaultGamepadMask, binding.playerIndex);
	}
	// トリガー系は閾値のみのため基本的にリバインド対象外（必要であれば個別に再登録する）
}

void KdInputActionSystem::ResetAllToDefault()
{
	// キーだけ集めてから回す（ResetToDefault内でmapが書き換わるため）
	std::vector<std::string> names;
	names.reserve(m_actionBindings.size());

	for (auto& [name, binding] : m_actionBindings)
	{
		names.push_back(name);
	}

	for (auto& name : names)
	{
		ResetToDefault(name);
	}
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// アクション定義の読込（JSON）
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// JSONフォーマット例：
// {
//   "actions": [
//     { "name": "Jump", "type": "button", "keyboard": ["Space","Enter"], "gamepad": ["A"], "player": 0 },
//     { "name": "Dash",  "type": "triggerButton", "gamepadTrigger": "RightTrigger", "threshold": 0.6, "player": 0 },
//     { "name": "Move",  "type": "axis", "keyboard": { "up":"W","right":"D","down":"S","left":"A" },
//       "gamepadStick": "LeftStick", "player": 0,
//       "deadzone": { "inner": 0.2, "outer": 0.98 }, "invertY": false },
//     { "name": "Look",  "type": "mouseAxis", "fixKey": "RightMouse" },
//     { "name": "Accel", "type": "triggerAxis", "gamepadTrigger": "RightTrigger", "player": 0 }
//   ]
// }
// キー名／ボタン名は KdInputKeyNames.h の対応表を参照（"Space" "W" "A"(ゲームパッド) "LeftStick" 等）
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdInputActionSystem::LoadActionDefinitions(std::string_view filePath)
{
	std::ifstream ifs(filePath.data());
	if (!ifs.is_open()) { return false; }

	Json j;
	try
	{
		ifs >> j;
	}
	catch (const Json::parse_error&)
	{
		assert(0 && "KdInputActionSystem::LoadActionDefinitions:JSONの解析に失敗しました");
		return false;
	}

	if (!j.contains("actions") || !j["actions"].is_array())
	{
		assert(0 && "KdInputActionSystem::LoadActionDefinitions:\"actions\"配列が見つかりません");
		return false;
	}

	for (auto& item : j["actions"])
	{
		std::string name = item.value("name", "");
		std::string type = item.value("type", "button");
		int         player = item.value("player", 0);

		if (name.empty()) { continue; }

		if (type == "button")
		{
			std::vector<int> keys;
			if (item.contains("keyboard") && item["keyboard"].is_array())
			{
				for (auto& keyName : item["keyboard"])
				{
					keys.push_back(KdInputKeyNames::KeyNameToVK(keyName.get<std::string>()));
				}
			}

			WORD gamepadMask = 0;
			if (item.contains("gamepad") && item["gamepad"].is_array())
			{
				for (auto& buttonName : item["gamepad"])
				{
					gamepadMask |= KdInputKeyNames::ButtonNameToMask(buttonName.get<std::string>());
				}
			}

			RegisterButtonAction(name, keys, gamepadMask, player);
		}
		else if (type == "triggerButton")
		{
			std::string triggerName = item.value("gamepadTrigger", "RightTrigger");
			float       threshold = item.value("threshold", 0.5f);

			RegisterTriggerButtonAction(name, KdInputKeyNames::TriggerNameToEnum(triggerName), threshold, player);
		}
		else if (type == "axis")
		{
			int up = 0, right = 0, down = 0, left = 0;

			if (item.contains("keyboard") && item["keyboard"].is_object())
			{
				auto& kb = item["keyboard"];
				if (kb.contains("up"))    { up    = KdInputKeyNames::KeyNameToVK(kb["up"].get<std::string>()); }
				if (kb.contains("right")) { right = KdInputKeyNames::KeyNameToVK(kb["right"].get<std::string>()); }
				if (kb.contains("down"))  { down  = KdInputKeyNames::KeyNameToVK(kb["down"].get<std::string>()); }
				if (kb.contains("left"))  { left  = KdInputKeyNames::KeyNameToVK(kb["left"].get<std::string>()); }
			}

			std::string    stickName = item.value("gamepadStick", "LeftStick");
			KdGamepadStick stick = KdInputKeyNames::StickNameToEnum(stickName);

			float innerDeadZone = -1.0f, outerDeadZone = 1.0f;
			if (item.contains("deadzone") && item["deadzone"].is_object())
			{
				innerDeadZone = item["deadzone"].value("inner", -1.0f);
				outerDeadZone = item["deadzone"].value("outer", 1.0f);
			}

			bool invertY = item.value("invertY", false);

			RegisterAxisAction(name, up, right, down, left, stick, player, innerDeadZone, outerDeadZone, invertY);
		}
		else if (type == "mouseAxis")
		{
			std::string fixKeyName = item.value("fixKey", "");
			int fixKey = fixKeyName.empty() ? 0 : KdInputKeyNames::KeyNameToVK(fixKeyName);

			RegisterMouseAxisAction(name, fixKey);
		}
		else if (type == "triggerAxis")
		{
			std::string triggerName = item.value("gamepadTrigger", "RightTrigger");

			RegisterTriggerAxisAction(name, KdInputKeyNames::TriggerNameToEnum(triggerName), player);
		}
		else
		{
			assert(0 && "KdInputActionSystem::LoadActionDefinitions:未知のtypeです");
		}
	}

	return true;
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// バインド情報の保存／読込（JSON）
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// LoadActionDefinitionsと同じJSONスキーマを使用する
// こちらは「現在のバインド状態」を保存／復元するためのもので、未登録のアクション名は無視する
// （初期定義はLoadActionDefinitions側で行っておくこと）
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdInputActionSystem::SaveBindings(std::string_view filePath) const
{
	Json j;
	j["actions"] = Json::array();

	for (auto& [name, binding] : m_actionBindings)
	{
		Json item;
		item["name"] = name;
		item["player"] = binding.playerIndex;

		if (binding.isTriggerButton)
		{
			item["type"] = "triggerButton";
			item["gamepadTrigger"] = KdInputKeyNames::TriggerEnumToName(binding.triggerSide);
			item["threshold"] = binding.triggerThreshold;
		}
		else if (binding.isTriggerAxis)
		{
			item["type"] = "triggerAxis";
			item["gamepadTrigger"] = KdInputKeyNames::TriggerEnumToName(binding.triggerSide);
		}
		else if (binding.isAxis)
		{
			item["type"] = "axis";

			Json kb;
			if (binding.currentKeyboardKeys.size() == 4)
			{
				if (binding.currentKeyboardKeys[0]) { kb["up"]    = KdInputKeyNames::VKToKeyName(binding.currentKeyboardKeys[0]); }
				if (binding.currentKeyboardKeys[1]) { kb["right"] = KdInputKeyNames::VKToKeyName(binding.currentKeyboardKeys[1]); }
				if (binding.currentKeyboardKeys[2]) { kb["down"]  = KdInputKeyNames::VKToKeyName(binding.currentKeyboardKeys[2]); }
				if (binding.currentKeyboardKeys[3]) { kb["left"]  = KdInputKeyNames::VKToKeyName(binding.currentKeyboardKeys[3]); }
			}
			item["keyboard"] = kb;
			item["gamepadStick"] = KdInputKeyNames::StickEnumToName(binding.stick);

			if (binding.innerDeadZone >= 0.0f)
			{
				item["deadzone"] = { { "inner", binding.innerDeadZone }, { "outer", binding.outerDeadZone } };
			}
			item["invertY"] = binding.invertY;
		}
		else
		{
			item["type"] = "button";

			Json kbArr = Json::array();
			for (int key : binding.currentKeyboardKeys)
			{
				kbArr.push_back(KdInputKeyNames::VKToKeyName(key));
			}
			item["keyboard"] = kbArr;

			Json gpArr = Json::array();
			for (auto& buttonName : KdInputKeyNames::MaskToNames(binding.currentGamepadMask))
			{
				gpArr.push_back(buttonName);
			}
			item["gamepad"] = gpArr;
		}

		j["actions"].push_back(item);
	}

	std::ofstream ofs(filePath.data());
	if (!ofs.is_open()) { return false; }

	ofs << j.dump(2); // インデント2で人が読める形式に整形出力

	return true;
}

bool KdInputActionSystem::LoadBindings(std::string_view filePath)
{
	std::ifstream ifs(filePath.data());
	if (!ifs.is_open()) { return false; }

	Json j;
	try
	{
		ifs >> j;
	}
	catch (const Json::parse_error&)
	{
		assert(0 && "KdInputActionSystem::LoadBindings:JSONの解析に失敗しました");
		return false;
	}

	if (!j.contains("actions") || !j["actions"].is_array()) { return false; }

	for (auto& item : j["actions"])
	{
		std::string name = item.value("name", "");
		if (name.empty()) { continue; }

		// 未登録のアクション名はスキップ（先にLoadActionDefinitions等で登録しておく必要がある）
		auto it = m_actionBindings.find(name);
		if (it == m_actionBindings.end()) { continue; }

		std::string type = item.value("type", "");

		if (type == "axis")
		{
			int up = 0, right = 0, down = 0, left = 0;

			if (item.contains("keyboard") && item["keyboard"].is_object())
			{
				auto& kb = item["keyboard"];
				if (kb.contains("up"))    { up    = KdInputKeyNames::KeyNameToVK(kb["up"].get<std::string>()); }
				if (kb.contains("right")) { right = KdInputKeyNames::KeyNameToVK(kb["right"].get<std::string>()); }
				if (kb.contains("down"))  { down  = KdInputKeyNames::KeyNameToVK(kb["down"].get<std::string>()); }
				if (kb.contains("left"))  { left  = KdInputKeyNames::KeyNameToVK(kb["left"].get<std::string>()); }
			}

			RebindKeyboard(name, { up, right, down, left }, /*allowDuplicate=*/true);
		}
		else if (type == "button")
		{
			if (item.contains("keyboard") && item["keyboard"].is_array())
			{
				std::vector<int> keys;
				for (auto& keyName : item["keyboard"])
				{
					keys.push_back(KdInputKeyNames::KeyNameToVK(keyName.get<std::string>()));
				}
				if (!keys.empty()) { RebindKeyboard(name, keys, /*allowDuplicate=*/true); }
			}

			if (item.contains("gamepad") && item["gamepad"].is_array())
			{
				WORD mask = 0;
				for (auto& buttonName : item["gamepad"])
				{
					mask |= KdInputKeyNames::ButtonNameToMask(buttonName.get<std::string>());
				}
				if (mask != 0) { RebindGamepadButton(name, mask, it->second.playerIndex, /*allowDuplicate=*/true); }
			}
		}
		// トリガー系は閾値のみのため現状リバインド対象外
	}

	return true;
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// ゲームパッド関連機能
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

void KdInputActionSystem::PollGamepadConnections()
{
	for (int i = 0; i < m_maxGamepadCount; ++i)
	{
		bool connectedNow = KdXInputCache::Instance().IsConnected(i);

		if (connectedNow != m_gamepadConnected[i])
		{
			m_gamepadConnected[i] = connectedNow;

			if (m_connectionListener)
			{
				m_connectionListener(i, connectedNow);
			}

			// 切断された場合は振動状態もリセットしておく
			if (!connectedNow)
			{
				m_vibrationStates[i] = {};
			}
		}
	}
}

bool KdInputActionSystem::IsGamepadConnected(int playerIndex) const
{
	if (playerIndex < 0 || playerIndex >= m_maxGamepadCount) { return false; }

	return m_gamepadConnected[playerIndex];
}

void KdInputActionSystem::SetVibration(int playerIndex, float leftMotor, float rightMotor, float duration)
{
	if (playerIndex < 0 || playerIndex >= m_maxGamepadCount) { return; }

	XINPUT_VIBRATION vibration{};
	vibration.wLeftMotorSpeed = (WORD)(std::clamp(leftMotor, 0.0f, 1.0f) * 65535.0f);
	vibration.wRightMotorSpeed = (WORD)(std::clamp(rightMotor, 0.0f, 1.0f) * 65535.0f);

	XInputSetState(playerIndex, &vibration);

	VibrationState& v = m_vibrationStates[playerIndex];
	v.infinite = (duration <= 0.0f);
	v.remainingTime = duration;
}

void KdInputActionSystem::StopVibration(int playerIndex)
{
	if (playerIndex < 0 || playerIndex >= m_maxGamepadCount) { return; }

	XINPUT_VIBRATION vibration{};
	XInputSetState(playerIndex, &vibration);

	m_vibrationStates[playerIndex] = {};
}

void KdInputActionSystem::UpdateVibrationTimers(float deltaTime)
{
	for (int i = 0; i < m_maxGamepadCount; ++i)
	{
		VibrationState& v = m_vibrationStates[i];

		if (v.infinite) { continue; }
		if (v.remainingTime <= 0.0f) { continue; }

		v.remainingTime -= deltaTime;

		if (v.remainingTime <= 0.0f)
		{
			StopVibration(i);
		}
	}
}
