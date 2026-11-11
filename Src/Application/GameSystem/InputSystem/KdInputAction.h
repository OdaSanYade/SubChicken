#pragma once

#include "Inputs/KdInputGamepad.h"
#include "Inputs/KdInputKeyNames.h"
#include "Inputs/KdInputMouseLock.h"

#include <functional>
#include <unordered_map>
#include <string>
#include <array>
#include <chrono>

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// KdInputActionSystem
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// KdInputManagerを土台にした「アクション名」ベースの入力システム
//
// 【できること】
//   ・1つのアクション名にキーボードとゲームパッドの両方を紐づけて登録できる
//     （KdInputManagerの「同名入力はデバイス間でOR合成される」仕組みをそのまま活用）
//   ・ポーリング方式（IsPress等）とイベント方式（AddListener）の両方で入力を取得できる
//   ・実行時のキー/ボタンのリバインドに対応
//   ・アクション定義そのものをJSONで記述可能（コードを書かずにキー割り当てを変更できる）
//   ・バインド情報のセーブ／ロードに対応（JSON形式）
//   ・ゲームパッドの接続監視・振動（バイブレーション）に対応
//
// 【使い方の概要】
//   1) ゲーム起動時に Init() を呼ぶ
//   2) RegisterButtonAction() / RegisterAxisAction() 等でアクションを登録する
//   3) 毎フレーム Update() を呼ぶ（内部でKdInputManager::Update()も呼ばれる）
//   4) IsPress("Jump") 等で状態を取得、または AddListener() でイベント登録する
//
// 詳しい使用例は KdInputActionExample.cpp を参照
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////

// ボタンイベントの種類
enum class KdInputEventType
{
	Press,		// 押された瞬間
	Hold,		// 押されている間（毎フレーム発火）
	Release,	// 離された瞬間
};

class KdInputActionSystem
{
public:

	// ----- 内部データ（外部ツールからの読み取り用に公開） -----
	// メンバ関数の戻り値等で使うため、クラスの先頭で定義しておく

	struct ActionBinding
	{
		bool isAxis = false;
		bool isTriggerButton = false;
		bool isTriggerAxis = false;

		std::vector<int> defaultKeyboardKeys;
		std::vector<int> currentKeyboardKeys;

		WORD defaultGamepadMask = 0;
		WORD currentGamepadMask = 0;

		KdGamepadStick   stick = KdGamepadStick::Left;
		KdGamepadTrigger triggerSide = KdGamepadTrigger::Left;
		float            triggerThreshold = 0.5f;

		float innerDeadZone = -1.0f;
		float outerDeadZone = 1.0f;
		bool  invertY = false;

		int playerIndex = 0;
	};

	static KdInputActionSystem& Instance()
	{
		static KdInputActionSystem instance;
		return instance;
	}

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// 初期化・更新
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----

	// maxGamepadCount : 同時に扱うゲームパッドの最大数（0〜XUSER_MAX_COUNT）
	void Init(int maxGamepadCount = 4);

	// 毎ループの初めに呼び出す
	// deltaTime を省略した場合は内部で自動計測する（振動タイマー用）
	void Update(float deltaTime = -1.0f);

	void Release();

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// アクション登録
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----

	// ボタンアクション登録：キーボードキー（複数可）とゲームパッドボタンマスクを1つの名前に紐づける
	// keyboardKeys が空の場合はキーボード未割り当て、gamepadButtonMask が0の場合はゲームパッド未割り当て
	void RegisterButtonAction(std::string_view actionName,
		const std::vector<int>& keyboardKeys,
		WORD gamepadButtonMask = 0,
		int playerIndex = 0);

	// トリガー(LT/RT)をボタンとして扱うアクションを登録
	void RegisterTriggerButtonAction(std::string_view actionName,
		KdGamepadTrigger trigger,
		float threshold = 0.5f,
		int playerIndex = 0);

	// 軸アクション登録：キーボードの上下左右キーとゲームパッドのスティックを1つの名前に紐づける
	// 使わない方向のキーには 0 を渡す
	// innerDeadZone : 0未満を渡すとゲームパッド機種標準のデッドゾーンを使用（通常はこれでよい）
	void RegisterAxisAction(std::string_view actionName,
		int upKey, int rightKey, int downKey, int leftKey,
		KdGamepadStick stick,
		int playerIndex = 0,
		float innerDeadZone = -1.0f,
		float outerDeadZone = 1.0f,
		bool invertY = false);

	// マウス移動量を軸アクションとして登録（fixKey を押している間は軸の中心を固定＝疑似スティック）
	void RegisterMouseAxisAction(std::string_view actionName, int fixKey = 0);

	// トリガー(LT/RT)をアナログ軸(x値のみ)として登録
	void RegisterTriggerAxisAction(std::string_view actionName, KdGamepadTrigger trigger, int playerIndex = 0);

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// 状態取得（ポーリング方式）
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----

	bool IsFree(std::string_view actionName) const;
	bool IsPress(std::string_view actionName) const;
	bool IsHold(std::string_view actionName) const;
	bool IsRelease(std::string_view actionName) const;

	Math::Vector2 GetAxis(std::string_view actionName) const;

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// イベント方式
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----

	using Listener = std::function<void()>;

	// 戻り値：登録解除に使うハンドル
	int AddListener(std::string_view actionName, KdInputEventType type, Listener callback);
	void RemoveListener(int listenerHandle);
	void ClearListeners();

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// リバインド（実行時のキー/ボタン変更）
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// 戻り値：適用できた場合はtrue。allowDuplicate=falseで競合がある場合は何もせずfalseを返す
	// （事前に FindKeyboardKeyConflicts / FindGamepadButtonConflicts で確認してからUIで確認を取り、
	//   ユーザーが上書きを選んだ場合に allowDuplicate=true で呼び直す、という使い方を想定）

	// newKeys : ボタンアクションなら複数キー、軸アクションなら[上,右,下,左]の4要素
	bool RebindKeyboard(std::string_view actionName, const std::vector<int>& newKeys, bool allowDuplicate = false);

	bool RebindGamepadButton(std::string_view actionName, WORD newMask, int playerIndex = 0, bool allowDuplicate = false);

	// 登録時の初期バインドに戻す
	void ResetToDefault(std::string_view actionName);
	void ResetAllToDefault();

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// リバインド時の重複チェック
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// 指定したキー/ボタンを既に使っている他のアクション名一覧を返す（無ければ空）
	// actionNameToExclude自身は対象から除外する（自分自身との比較を避けるため）
	std::vector<std::string> FindKeyboardKeyConflicts(std::string_view actionNameToExclude, const std::vector<int>& keys) const;
	std::vector<std::string> FindGamepadButtonConflicts(std::string_view actionNameToExclude, WORD mask, int playerIndex) const;

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// アクション定義の読込（JSON形式）
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// "企画データ"としてのアクション一覧をJSONから読み込み、RegisterXxxAction群を自動で呼び出す
	// コード側で1つもRegisterXxxActionを書かず、全アクションをJSONだけで定義することも可能
	// フォーマットの詳細はKdInputActionExample.cppおよび同梱のサンプルJSONを参照
	bool LoadActionDefinitions(std::string_view filePath);

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// バインド情報の保存／読込（JSON形式）
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// こちらは「プレイヤーがキーコンフィグ画面で変更した内容」の保存・復元用
	// LoadActionDefinitions等で先にアクションを登録しておく必要がある（未登録のアクション名は無視される）
	bool SaveBindings(std::string_view filePath) const;
	bool LoadBindings(std::string_view filePath);

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// ゲームパッド関連機能
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----

	bool IsGamepadConnected(int playerIndex) const;

	// leftMotor/rightMotor : 0.0〜1.0　duration : 秒数（0以下で振動し続ける）
	void SetVibration(int playerIndex, float leftMotor, float rightMotor, float duration = 0.0f);
	void StopVibration(int playerIndex);

	using ConnectionListener = std::function<void(int playerIndex, bool connected)>;
	void SetGamepadConnectionListener(ConnectionListener callback) { m_connectionListener = std::move(callback); }

	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// デバッグ表示／キーコンフィグUI向けの状態取得
	// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
	// KdInputDebugUI 等の外部ツールがアクション一覧・現在のバインドを読み取るために公開している
	// ActionBindingは public ネスト構造体（クラス先頭で定義済み）

	const std::unordered_map<std::string, ActionBinding>& GetActionBindings() const { return m_actionBindings; }

	int GetMaxGamepadCount() const { return m_maxGamepadCount; }

private:

	KdInputActionSystem() {}
	~KdInputActionSystem() {}

	struct EventListenerEntry
	{
		int id = 0;
		std::string actionName;
		KdInputEventType type = KdInputEventType::Press;
		Listener callback;
	};

	struct VibrationState
	{
		float remainingTime = 0.0f;
		bool  infinite = false;
	};

	void PollGamepadConnections();
	void UpdateVibrationTimers(float deltaTime);
	void DispatchEvents();

	std::string GamepadDeviceName(int playerIndex) const { return "Gamepad" + std::to_string(playerIndex); }

	std::unordered_map<std::string, ActionBinding> m_actionBindings;

	std::vector<EventListenerEntry> m_listeners;
	int m_nextListenerId = 0;

	std::array<VibrationState, XUSER_MAX_COUNT> m_vibrationStates;
	std::array<bool, XUSER_MAX_COUNT>           m_gamepadConnected{};

	int m_maxGamepadCount = 4;

	ConnectionListener m_connectionListener;

	// deltaTime自動計測用
	bool m_hasLastTimePoint = false;
	std::chrono::steady_clock::time_point m_lastTimePoint;
};
