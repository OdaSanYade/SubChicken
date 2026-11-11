#pragma once

// ===========================================================
// StateMachineComponent
//
// ECSに載せるコンポーネント。完全なPOD。
// ポインタ・文字列・std::function・可変長配列は一切持たない。
//
// 【設計方針】
// 状態の「問い合わせ」はこのファイルだけで完結する。
// 現在ID/ベースID/タグをコンポーネント自身が持っているため、
// IsState() や HasTag() を呼ぶのにシステムへのアクセスは不要。
//
// 状態の「変更」はシステムが行う。ガード判定にはステート定義が
// 必要なため、StateMachineSystem または ctx 経由で呼ぶこと。
//
// ヘッダ依存: 標準ライブラリのみ
// ===========================================================

// -----------------------------------------------------------
// StateId : 文字列のFNV-1aハッシュ
// -----------------------------------------------------------
using StateId = uint32_t;

// ---- 定数は名前空間 SM に入れ、必ず修飾して使う ----
// ECS側(kd::ecs)にも同名の定数がありうるため、
// グローバル名前空間には一切置かない。
// using namespace kd::ecs; が効いていても曖昧にならない。
namespace SM
{
	inline constexpr StateId kInvalidStateId = 0;
	inline constexpr uint16_t kInvalidStateIndex = 0xFFFF;
}

// コンパイル時ハッシュ。MakeStateId("Idle") は定数に畳み込まれる。
constexpr StateId MakeStateId(const char* str, uint32_t hash = 2166136261u)
{
	return (str == nullptr || *str == '\0')
		? hash
		: MakeStateId(str + 1,
			(hash ^ static_cast<uint32_t>(static_cast<unsigned char>(*str))) * 16777619u);
}

// 実行時用(JSONから読んだ文字列など)
inline StateId MakeStateId(std::string_view str)
{
	uint32_t hash = 2166136261u;
	for (char c : str)
	{
		hash ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
		hash *= 16777619u;
	}
	return hash;
}

// グローバル名前空間で "_xxx" は予約識別子のため名前空間に入れる。
// 使う側で using namespace StateLiterals; と書けば "Idle"_sid が使える。
namespace StateLiterals
{
	constexpr StateId operator""_sid(const char* str, size_t)
	{
		return MakeStateId(str);
	}
}

// -----------------------------------------------------------
// 遷移要求の結果
// -----------------------------------------------------------
enum class StateResult : uint8_t
{
	Accepted = 0,		// 予約成立。次のApplyで遷移する
	Deferred,			// 条件未達。成立するまで予約を保持して再試行する
	RejectedUnknown,	// 未登録のStateID
	RejectedSameState,	// 現在と同じステート(RestartStateを使うこと)
	RejectedPriority,	// より優先度の高い予約が既に入っている
	RejectedForbidden,	// allowFrom / blockFrom / blockTag により恒久的に禁止

	Count
};

const char* ToString(StateResult result);

inline bool IsAccepted(StateResult r)
{
	return r == StateResult::Accepted || r == StateResult::Deferred;
}

// ===========================================================
// コンポーネント本体
// ===========================================================
struct StateMachineComponent
{
	// 1フレームに受け取れるイベントの最大数
	static constexpr uint8_t kEventCapacity = 4;

	// ---- 所属システム ----
	uint16_t systemId = SM::kInvalidStateIndex;

	// ---- 現在の状態(インデックスはシステム内での定義位置) ----
	uint16_t baseIndex = SM::kInvalidStateIndex;
	uint16_t stateIndex = SM::kInvalidStateIndex;

	StateId baseId = SM::kInvalidStateId;
	StateId stateId = SM::kInvalidStateId;
	StateId prevStateId = SM::kInvalidStateId;

	uint32_t stateTag = 0;			// 現在ステートのタグ(問い合わせ高速化のためコピー)
	uint32_t transitionCount = 0;	// 遷移が確定するたびに増える

	float stateTime = 0.0f;			// 現在の状態に入ってからの経過秒
	float baseTime = 0.0f;			// 現在のベースに入ってからの経過秒

	// ---- 予約(nextState) ----
	uint16_t reservedIndex = SM::kInvalidStateIndex;
	StateId reservedId = SM::kInvalidStateId;
	float reservedTimer = -1.0f;	// 残り寿命(秒)。負なら無期限
	uint8_t reservedPriority = 0;
	uint8_t reservedForce = 0;		// 1ならガードを全て無視する

	// ---- イベント ----
	uint8_t eventCount = 0;
	uint8_t lastResult = 0;			// 直近の遷移要求の結果(StateResult)
	StateId eventBuffer[kEventCapacity] = {};

	// ---- アニメーション同期用(StateMachineSystemが使う) ----
	uint32_t animSyncCount = 0xFFFFFFFFu;	// 最後にアニメを同期した transitionCount
	StateId animOnEndId = SM::kInvalidStateId;
	float animPlaySpeed = 1.0f;
	uint8_t animLoop = 1;
	uint8_t animEndFired = 0;
	uint8_t padding[2] = {};

	// =======================================================
	// 状態の取得API(システム不要で完結する)
	// =======================================================
	StateId GetState() const { return stateId; }
	StateId GetBase() const { return baseId; }
	StateId GetPrevState() const { return prevStateId; }

	bool IsState(StateId id) const { return stateId == id; }
	bool IsBase(StateId id) const { return baseId == id; }

	float GetStateTime() const { return stateTime; }
	float GetBaseTime() const { return baseTime; }

	uint32_t GetTag() const { return stateTag; }
	bool HasTag(uint32_t mask) const { return (stateTag & mask) != 0u; }

	uint32_t GetTransitionCount() const { return transitionCount; }
	StateResult GetLastResult() const { return static_cast<StateResult>(lastResult); }

	bool IsValid() const { return stateIndex != SM::kInvalidStateIndex; }

	// ---- 予約の参照 ----
	bool HasReserved() const { return reservedIndex != SM::kInvalidStateIndex; }
	StateId GetReservedState() const { return reservedId; }
	float GetReservedTimer() const { return reservedTimer; }

	void CancelReserved()
	{
		reservedIndex = SM::kInvalidStateIndex;
		reservedId = SM::kInvalidStateId;
		reservedPriority = 0;
		reservedForce = 0;
		reservedTimer = -1.0f;
	}

	// 同じ状態への再突入(アニメの頭出し等)。ガードを通さない。
	void RestartState()
	{
		if (stateIndex == SM::kInvalidStateIndex) return;

		reservedIndex = stateIndex;
		reservedId = stateId;
		reservedPriority = 255;
		reservedForce = 1;
		reservedTimer = -1.0f;
	}

	// =======================================================
	// イベント(アニメーションイベント等の外部通知)
	// =======================================================
	bool PushEvent(StateId id)
	{
		if (eventCount >= kEventCapacity) return false;	// 溢れは黙って捨てる
		eventBuffer[eventCount++] = id;
		return true;
	}

	bool HasEvent(StateId id) const
	{
		for (uint8_t i = 0; i < eventCount; ++i)
		{
			if (eventBuffer[i] == id) return true;
		}
		return false;
	}

	// 見つけたら取り除いてtrue。同じイベントで二重発火させないため。
	bool ConsumeEvent(StateId id)
	{
		for (uint8_t i = 0; i < eventCount; ++i)
		{
			if (eventBuffer[i] != id) continue;

			eventBuffer[i] = eventBuffer[eventCount - 1];
			--eventCount;
			return true;
		}
		return false;
	}

	void ClearEvents() { eventCount = 0; }
};

static_assert(sizeof(StateMachineComponent) <= 96,
	"StateMachineComponent が肥大化しています");
