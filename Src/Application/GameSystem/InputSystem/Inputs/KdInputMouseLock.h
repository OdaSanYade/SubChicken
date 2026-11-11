#pragma once

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// KdMouseLock
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// マウスカーソルのロック(非表示＋ウィンドウ中央に固定)と、Raw Inputによる相対移動量の取得
//
// 【カーソル座標の差分では駄目な理由】
//   カーソルは画面端で止まるので、そこから先は差分が0になり視点が回らない。
//   Raw Inputはマウス本体の移動量(カウント)をそのまま受け取るので、
//   カーソルが固定されていても値が入る。OSのポインタ加速も乗らない。
//
// 【使い方】
//   1) ウィンドウ生成後に Init(hWnd)(ウィンドウプロシージャへの差し込みも内部で行う)
//   2) SetLocked() でロック要求
//   3) アクティブなカメラが毎フレーム ConsumeMoveDelta() / ConsumeWheelDelta() で取り出す
//      (取り出し式なので、呼び出し順やフレーム頭の処理に依存しない)
//   4) 解放はウィンドウ破棄時に自動。明示的に Release() を呼んでもよい
// ※ <Windows.h> は Pch.h に含まれている前提
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
class KdMouseLock
{
public:

	static KdMouseLock& Instance()
	{
		static KdMouseLock instance;
		return instance;
	}

	bool Init(HWND hWnd);
	void Release();

	void OnWindowMessage(UINT msg, WPARAM wParam, LPARAM lParam);

	// ロック要求。ウィンドウが非アクティブの間は自動で外れ、戻ると再びかかる
	void SetLocked(bool locked) { m_wantLocked = locked; Apply(); }
	bool IsLockRequested() const { return m_wantLocked; }
	bool IsLocked() const { return m_applied; } // 実際に効いているか

	// 前回取り出してから溜まったマウス移動量(カウント)を取り出し、0に戻す。右+/下+(画面座標と同じ向き)
	// ロック中のみ溜まる
	Math::Vector2 ConsumeMoveDelta();

	// 前回取り出してから溜まったホイール回転量(ノッチ数)を取り出し、0に戻す。上+/下-
	float ConsumeWheelDelta();

	// 診断用: 受け取ったマウスのWM_INPUTの累計。0のままならRaw Inputが届いていない
	unsigned int GetRawInputCount() const { return m_rawInputCount; }

private:

	KdMouseLock() = default;
	~KdMouseLock() = default;

	static LRESULT CALLBACK SubclassProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam,
		UINT_PTR idSubclass, DWORD_PTR refData);

	bool RegisterRawMouse() const;
	void Apply();
	void ClipToCenter() const;
	void SetCursorHidden(bool hidden);

	HWND m_hWnd = nullptr;
	bool m_subclassed = false;

	bool m_wantLocked = false;
	bool m_windowActive = false;
	bool m_applied = false;
	bool m_cursorHidden = false;

	long long m_accumDX = 0;
	long long m_accumDY = 0;
	int       m_accumWheel = 0;

	unsigned int m_rawInputCount = 0;
};