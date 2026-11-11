#include "KdInputGamepad.h"

#include <algorithm>
#include <cmath>

#pragma comment(lib, "Xinput9_1_0.lib")


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdXInputCache
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

const XINPUT_STATE& KdXInputCache::GetState(int playerIndex)
{
	static XINPUT_STATE dummy{};

	if (playerIndex < 0 || playerIndex >= (int)m_entries.size())
	{
		assert(0 && "KdXInputCache::GetState:プレイヤーIndexが範囲外です");
		return dummy;
	}

	Entry& entry = m_entries[playerIndex];

	// まだ今フレームで取得していなければ更新
	if (entry.generation != m_generation)
	{
		ZeroMemory(&entry.state, sizeof(XINPUT_STATE));

		entry.connected = (XInputGetState(playerIndex, &entry.state) == ERROR_SUCCESS);
		entry.generation = m_generation;
	}

	return entry.state;
}

bool KdXInputCache::IsConnected(int playerIndex)
{
	if (playerIndex < 0 || playerIndex >= (int)m_entries.size())
	{
		return false;
	}

	// GetStateを呼ぶことで接続状態も同時に更新される
	GetState(playerIndex);

	return m_entries[playerIndex].connected;
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputButtonForXInput
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

KdInputButtonForXInput::KdInputButtonForXInput(WORD buttonMask, int playerIndex)
	: m_buttonMask(buttonMask)
	, m_playerIndex(playerIndex)
{
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// KdInputButtonForWindowsと同じPress/Hold/Releaseのステートマシンで更新する
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdInputButtonForXInput::Update()
{
	if (!m_needUpdate) { return; }

	const XINPUT_STATE& state = KdXInputCache::Instance().GetState(m_playerIndex);

	bool isDown = (state.Gamepad.wButtons & m_buttonMask) != 0;

	if (isDown)
	{
		if (m_state & Hold)
		{
			m_state &= ~Press;
		}
		else
		{
			m_state |= Press | Hold;
		}
	}
	else
	{
		if (m_state & Hold)
		{
			m_state &= ~Press;
			m_state &= ~Hold;
			m_state |= Release;
		}
		else
		{
			m_state &= ~Release;
		}
	}

	m_needUpdate = false;
}

void KdInputButtonForXInput::GetCode(std::vector<int>& ret) const
{
	ret.push_back((int)m_buttonMask);
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputButtonForXInputTrigger
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

KdInputButtonForXInputTrigger::KdInputButtonForXInputTrigger(KdGamepadTrigger trigger, float threshold, int playerIndex)
	: m_trigger(trigger)
	, m_threshold(threshold)
	, m_playerIndex(playerIndex)
{
}

void KdInputButtonForXInputTrigger::Update()
{
	if (!m_needUpdate) { return; }

	const XINPUT_STATE& state = KdXInputCache::Instance().GetState(m_playerIndex);

	BYTE rawValue = (m_trigger == KdGamepadTrigger::Left) ? state.Gamepad.bLeftTrigger : state.Gamepad.bRightTrigger;

	bool isDown = ((rawValue / 255.0f) >= m_threshold);

	if (isDown)
	{
		if (m_state & Hold)
		{
			m_state &= ~Press;
		}
		else
		{
			m_state |= Press | Hold;
		}
	}
	else
	{
		if (m_state & Hold)
		{
			m_state &= ~Press;
			m_state &= ~Hold;
			m_state |= Release;
		}
		else
		{
			m_state &= ~Release;
		}
	}

	m_needUpdate = false;
}

void KdInputButtonForXInputTrigger::GetCode(std::vector<int>& ret) const
{
	// トリガーはキーコードを持たないため、識別用に-1を返す（Left）/-2を返す（Right）
	ret.push_back(m_trigger == KdGamepadTrigger::Left ? -1 : -2);
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputAxisForXInputStick
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

KdInputAxisForXInputStick::KdInputAxisForXInputStick(KdGamepadStick stick, int playerIndex)
	: m_stick(stick)
	, m_playerIndex(playerIndex)
{
	// XInputの標準デッドゾーン定数を初期値として使用（0.0〜1.0に正規化）
	float defaultDeadZone = (stick == KdGamepadStick::Left)
		? (float)XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE / 32767.0f
		: (float)XINPUT_GAMEPAD_RIGHT_THUMB_DEADZONE / 32767.0f;

	m_innerDeadZone = defaultDeadZone;
}

void KdInputAxisForXInputStick::SetDeadZone(float innerDeadZone, float outerDeadZone)
{
	m_innerDeadZone = std::clamp(innerDeadZone, 0.0f, 1.0f);
	m_outerDeadZone = std::clamp(outerDeadZone, m_innerDeadZone + 0.0001f, 1.0f);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 円形デッドゾーン処理
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// 入力ベクトルの長さ(magnitude)でデッドゾーン判定を行うことで、X軸・Y軸を別々に
// 判定する方式（十字型デッドゾーン）よりも自然な操作感になる
// 内側〜外側の間を0.0〜1.0へ滑らかに正規化することで、デッドゾーン境界での
// 値の飛び（スナップ）を防ぐ
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdInputAxisForXInputStick::Update()
{
	const XINPUT_STATE& state = KdXInputCache::Instance().GetState(m_playerIndex);

	SHORT rawX = (m_stick == KdGamepadStick::Left) ? state.Gamepad.sThumbLX : state.Gamepad.sThumbRX;
	SHORT rawY = (m_stick == KdGamepadStick::Left) ? state.Gamepad.sThumbLY : state.Gamepad.sThumbRY;

	float x = rawX / 32767.0f;
	float y = rawY / 32767.0f;

	float magnitude = std::sqrt(x * x + y * y);

	if (magnitude < m_innerDeadZone)
	{
		m_axis = Math::Vector2::Zero;
		return;
	}

	float clippedMag = std::min(magnitude, m_outerDeadZone);

	float normalizedMag = (clippedMag - m_innerDeadZone) / (m_outerDeadZone - m_innerDeadZone);
	normalizedMag = std::clamp(normalizedMag, 0.0f, 1.0f);

	float dirX = x / magnitude;
	float dirY = y / magnitude;

	m_axis.x = dirX * normalizedMag;
	m_axis.y = (m_invertY ? -dirY : dirY) * normalizedMag;
}


// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####
// KdInputAxisForXInputTrigger
// ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### ##### #####

KdInputAxisForXInputTrigger::KdInputAxisForXInputTrigger(KdGamepadTrigger trigger, int playerIndex)
	: m_trigger(trigger)
	, m_playerIndex(playerIndex)
{
}

void KdInputAxisForXInputTrigger::Update()
{
	const XINPUT_STATE& state = KdXInputCache::Instance().GetState(m_playerIndex);

	BYTE rawValue = (m_trigger == KdGamepadTrigger::Left) ? state.Gamepad.bLeftTrigger : state.Gamepad.bRightTrigger;

	m_axis.x = rawValue / 255.0f;
	m_axis.y = 0.0f;
}
