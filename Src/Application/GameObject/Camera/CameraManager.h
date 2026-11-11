#pragma once
#include "CameraBase.h"

// ゲームカメラとデバッグカメラを一元管理し、デバッグモードで切り替える。
// カメラの所有はシーン側(shared_ptr)。ここではweakで参照するだけ。
class CameraManager
{
public:

	// 登録(所有はシーン)。登録時点でアクティブ状態を反映する。
	void RegisterGameCamera(const std::shared_ptr<CameraBase>& _cam) { m_wpGame = _cam; ApplyActiveState(); }
	void RegisterDebugCamera(const std::shared_ptr<CameraBase>& _cam) { m_wpDebug = _cam; ApplyActiveState(); }

	// デバッグモード切替
	void SetDebugMode(bool _on)
	{
		if (m_debugMode == _on) return;
		m_debugMode = _on;

		auto game = m_wpGame.lock();
		auto debug = m_wpDebug.lock();

		if (_on)
		{
			// ゲーム → デバッグ: 現在の描画姿勢をデバッグカメラへ引き継ぐ
			if (game && debug) debug->SyncPoseFromWorldMatrix(game->GetResolvedWorldMatrix());
		}
		else
		{
			// デバッグ → ゲーム: 復帰直後の滑り(カクつき)を防止
			if (game) game->SnapToTarget();
		}

		ApplyActiveState();
	}
	void ToggleDebugMode() { SetDebugMode(!m_debugMode); }
	bool IsDebugMode() const { return m_debugMode; }

	// 現在アクティブなカメラ(描画側でKdCamera等を参照したい場合)
	std::shared_ptr<CameraBase> GetActiveCamera() const
	{
		return m_debugMode ? m_wpDebug.lock() : m_wpGame.lock();
	}

	// デバッグ中のマウスロック切替(DebugCameraから呼ぶ)
	void ToggleDebugMouseLock() { m_debugMouseLocked = !m_debugMouseLocked; ApplyMouseLock(); }

	// ポーズ・OPTION等、ゲーム中にカーソルが要る場面で外す
	void SetGameMouseLocked(bool _locked){m_gameMouseLocked = _locked; ApplyMouseLock();}

private:

	// アクティブな側だけが描画・GPU転送する
	void ApplyActiveState()
	{
		if (auto game = m_wpGame.lock())  game->SetActive(!m_debugMode);
		if (auto debug = m_wpDebug.lock()) debug->SetActive(m_debugMode);
		ApplyMouseLock();
	}

	// マウスロックの適用
	void ApplyMouseLock()
	{
		KdMouseLock::Instance().SetLocked(m_debugMode ? m_debugMouseLocked : m_gameMouseLocked);
	}

private:

	std::weak_ptr<CameraBase> m_wpGame;
	std::weak_ptr<CameraBase> m_wpDebug;
	bool m_debugMode = false;

	bool m_gameMouseLocked = true;
	bool m_debugMouseLocked = false;

private:
	CameraManager() = default;
	~CameraManager() = default;
	CameraManager(const CameraManager&) = delete;
	CameraManager& operator=(const CameraManager&) = delete;
public:
	static CameraManager& Instance()
	{
		static CameraManager instance;
		return instance;
	}
};