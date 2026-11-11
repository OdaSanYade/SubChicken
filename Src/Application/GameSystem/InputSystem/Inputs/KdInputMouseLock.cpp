#include "KdInputMouseLock.h"

#include <CommCtrl.h>
#pragma comment(lib, "Comctl32.lib")

namespace
{
	// HID Usage(hidusage.hを引かずに済ませる)
	constexpr USHORT kUsagePageGeneric = 0x01;
	constexpr USHORT kUsageMouse = 0x02;

	// サブクラスの識別子(同じウィンドウに他のサブクラスがあっても衝突しない値)
	constexpr UINT_PTR kSubclassId = 0x4B644D4C; // 'KdML'
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 初期化・解放
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
bool KdMouseLock::Init(HWND hWnd)
{
	m_hWnd = hWnd;

	if (!RegisterRawMouse())
	{
		assert(0 && "KdMouseLock::Init:Raw Inputの登録に失敗しました");
		return false;
	}

	// 既存のウィンドウプロシージャの前に割り込む。KdWindow側は無改造で済む
	if (!SetWindowSubclass(hWnd, &KdMouseLock::SubclassProc, kSubclassId, 0))
	{
		assert(0 && "KdMouseLock::Init:ウィンドウのサブクラス化に失敗しました");
		return false;
	}
	m_subclassed = true;

	m_windowActive = (GetForegroundWindow() == hWnd);
	Apply();
	return true;
}

void KdMouseLock::Release()
{
	// ClipCursorはOS全体に効くので必ず戻す
	m_wantLocked = false;
	Apply();

	if (m_hWnd)
	{
		if (m_subclassed)
		{
			RemoveWindowSubclass(m_hWnd, &KdMouseLock::SubclassProc, kSubclassId);
			m_subclassed = false;
		}

		RAWINPUTDEVICE rid{};
		rid.usUsagePage = kUsagePageGeneric;
		rid.usUsage = kUsageMouse;
		rid.dwFlags = RIDEV_REMOVE;
		rid.hwndTarget = nullptr;	// REMOVE時はnull必須
		RegisterRawInputDevices(&rid, 1, sizeof(rid));
	}

	m_hWnd = nullptr;
	m_accumDX = 0;
	m_accumDY = 0;
	m_accumWheel = 0;
}

bool KdMouseLock::RegisterRawMouse() const
{
	RAWINPUTDEVICE rid{};
	rid.usUsagePage = kUsagePageGeneric;
	rid.usUsage = kUsageMouse;
	rid.dwFlags = 0;	// フォアグラウンドの間だけ受け取る
	// RIDEV_NOLEGACYは付けない(WM_MOUSEMOVE/クリックが来なくなりImGuiが動かなくなる)
	rid.hwndTarget = m_hWnd;

	return RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// サブクラスプロシージャ
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// 全メッセージを覗いてから、元のウィンドウプロシージャへそのまま流す(消費はしない)
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
LRESULT CALLBACK KdMouseLock::SubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
	UINT_PTR /*idSubclass*/, DWORD_PTR /*refData*/)
{
	KdMouseLock& self = Instance();

	self.OnWindowMessage(msg, wParam, lParam);

	// ウィンドウ破棄の最終メッセージで後始末(Release呼び忘れでもクリップが残らない)
	if (msg == WM_NCDESTROY)
	{
		self.Release();
	}

	return DefSubclassProc(hWnd, msg, wParam, lParam);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ウィンドウメッセージ
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdMouseLock::OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
	if (msg == WM_INPUT)
	{
		RAWINPUT raw{};
		UINT size = sizeof(raw);
		const UINT read = GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT,
			&raw, &size, sizeof(RAWINPUTHEADER));
		if (read == static_cast<UINT>(-1) || raw.header.dwType != RIM_TYPEMOUSE) { return; }

		++m_rawInputCount;

		const RAWMOUSE& m = raw.data.mouse;

		// 相対移動のみ扱う(ペンタブレットやリモートデスクトップは絶対座標で来るので対象外)
		if (m_applied && (m.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
		{
			m_accumDX += m.lLastX;
			m_accumDY += m.lLastY;
		}

		if (m.usButtonFlags & RI_MOUSE_WHEEL)
		{
			m_accumWheel += static_cast<SHORT>(m.usButtonData);
		}
		return;
	}

	if (msg == WM_ACTIVATE)
	{
		// 最小化されたままのアクティブ化(HIWORD!=0)ではロックしない
		m_windowActive = (LOWORD(wParam) != WA_INACTIVE) && (HIWORD(wParam) == 0);
		Apply();
		return;
	}

	if (msg == WM_MOVE || msg == WM_SIZE)
	{
		if (m_applied) { ClipToCenter(); }
		return;
	}
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 取り出し
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
Math::Vector2 KdMouseLock::ConsumeMoveDelta()
{
	const Math::Vector2 delta(static_cast<float>(m_accumDX), static_cast<float>(m_accumDY));
	m_accumDX = 0;
	m_accumDY = 0;

	// UACダイアログやWin+L等、メッセージ無しにクリップが外れることがあるので毎フレーム張り直す
	if (m_applied) { ClipToCenter(); }

	return delta;
}

float KdMouseLock::ConsumeWheelDelta()
{
	const float wheel = static_cast<float>(m_accumWheel) / static_cast<float>(WHEEL_DELTA);
	m_accumWheel = 0;
	return wheel;
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 要求とウィンドウ状態から、実際のロック状態を合わせる
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdMouseLock::Apply()
{
	const bool shouldLock = m_wantLocked && m_windowActive && (m_hWnd != nullptr);

	if (shouldLock)
	{
		// 他所が後からマウスのRaw Inputを登録し直していても、ロックする瞬間に取り返す
		if (!m_applied) { RegisterRawMouse(); }

		ClipToCenter();
		SetCursorHidden(true);
	}
	else if (m_applied)
	{
		ClipCursor(nullptr);
		SetCursorHidden(false);
	}

	// 切り替えの瞬間に溜まっていた移動量で視点が跳ねないよう捨てる
	if (shouldLock != m_applied)
	{
		m_accumDX = 0;
		m_accumDY = 0;
	}

	m_applied = shouldLock;
}

void KdMouseLock::ClipToCenter() const
{
	RECT rc{};
	if (!GetClientRect(m_hWnd, &rc)) { return; }

	POINT center{ (rc.left + rc.right) / 2, (rc.top + rc.bottom) / 2 };
	if (!ClientToScreen(m_hWnd, &center)) { return; }

	// 1px四方に閉じ込める=中央に固定。SetCursorPosで毎フレーム戻す方式と違い、
	// 戻すたびの余計なWM_MOUSEMOVEが発生しない
	RECT clip{ center.x, center.y, center.x + 1, center.y + 1 };
	ClipCursor(&clip);
}

void KdMouseLock::SetCursorHidden(bool hidden)
{
	if (hidden == m_cursorHidden) { return; }

	// ShowCursorは参照カウント式(0以上で表示)。他所の呼び出しとずれていても
	// 確実に切り替わるよう、狙った側へ跨ぐまで回す
	if (hidden) { while (ShowCursor(FALSE) >= 0) {} }
	else { while (ShowCursor(TRUE) < 0) {} }

	m_cursorHidden = hidden;
}