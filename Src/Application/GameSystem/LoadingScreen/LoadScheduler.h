#pragma once
#include "Task/LoadTask.h"

// ----------------------------------------------------------
// 初期化タスクの実行器
//
// Start() の後、IsFinished() になるまで毎フレーム Step() を呼ぶ
// Step() は指定ミリ秒だけ処理を進めて返るので
// 呼び出し側はその合間にロード画面を描画できる
// ----------------------------------------------------------
class LoadScheduler
{
public:

	LoadScheduler() = default;

	LoadScheduler(const LoadScheduler&) = delete;
	LoadScheduler& operator = (const LoadScheduler&) = delete;

	// タスクテーブルを渡して開始する
	// テーブルは静的配列を想定。実行中は生存している必要がある
	void Start(const LoadTask* _tasks, size_t _count);

	// _budgetMs ミリ秒を目安に処理を進める
	// 予算は1件の処理が終わるたびに確認するので
	// 単発タスクが重い場合は予算を超える
	void Step(float _budgetMs);

	bool IsFinished() const { return m_finished; }

	// --- 進捗の取得(描画側が読む) ---

	// 0.0 〜 1.0
	float GetProgress() const;

	// 今処理しているタスクの表示名
	const char* GetCurrentLabel() const;

	// 分割タスクの進み具合(単発タスクなら 0 / 0)
	size_t GetCurrentSlot()  const { return m_slotCursor; }
	size_t GetCurrentTotal() const { return m_slotCount; }

	// 失敗したスロットの件数
	size_t GetFailedCount() const { return m_failedCount; }

private:

	// 現在のタスクを1件ぶん進める。全て終わったら false
	bool Advance();

	// 次のタスクへ移る
	void GotoNextTask();

	// 現在のタスクの prepare まで済ませる
	void BeginCurrentTask();

	const LoadTask* m_tasks = nullptr;
	size_t m_taskCount = 0;

	size_t m_taskCursor = 0;	// 実行中のタスク
	size_t m_slotCursor = 0;	// 実行中のスロット
	size_t m_slotCount = 0;		// 現タスクのスロット総数

	bool m_taskBegun = false;	// 現タスクの prepare 済みか
	bool m_funcDone = false;	// 現タスクの func 済みか
	bool m_commitDone = false;	// 現タスクの commit 済みか
	bool m_finished = true;

	// 進捗計算用
	float m_totalWeight = 0.0f;
	float m_doneWeight = 0.0f;

	size_t m_failedCount = 0;
};