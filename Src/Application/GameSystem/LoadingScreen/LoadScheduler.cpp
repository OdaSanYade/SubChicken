#include "LoadScheduler.h"

namespace
{
	// 単発タスクを実行した扱いにするときの粒度
	// 分割タスクはスロット単位で進むので、単発タスクも同じ土俵に乗せる
	constexpr float kSingleTaskUnit = 1.0f;
}

// ----------------------------------------------------------
void LoadScheduler::Start(const LoadTask* _tasks, size_t _count)
{
	m_tasks = _tasks;
	m_taskCount = (_tasks != nullptr) ? _count : 0;

	m_taskCursor = 0;
	m_slotCursor = 0;
	m_slotCount = 0;
	m_taskBegun = false;
	m_doneWeight = 0.0f;
	m_failedCount = 0;

	// 総重みを先に出しておく
	// 分割タスクのスロット数はまだ分からないので
	// prepare が走った時点で m_totalWeight を補正する
	m_totalWeight = 0.0f;
	for (size_t i = 0; i < m_taskCount; ++i)
	{
		m_totalWeight += m_tasks[i].weight;
	}

	m_finished = (m_taskCount == 0);
}

// ----------------------------------------------------------
void LoadScheduler::Step(float _budgetMs)
{
	if (m_finished) { return; }

	const auto begin = std::chrono::steady_clock::now();

	while (!m_finished)
	{
		if (!Advance()) { break; }

		const auto now = std::chrono::steady_clock::now();
		const float elapsedMs =
			std::chrono::duration<float, std::milli>(now - begin).count();

		// 予算を超えたら抜ける
		// 1件が予算より重い場合は超過するが、少なくとも
		// 次の重い処理を同じフレームに巻き込まない
		if (elapsedMs >= _budgetMs) { break; }
	}
}

// ----------------------------------------------------------
// 1件ぶん進める
// ----------------------------------------------------------
bool LoadScheduler::Advance()
{
	if (m_taskCursor >= m_taskCount)
	{
		m_finished = true;
		return false;
	}

	const LoadTask& task = m_tasks[m_taskCursor];

	// prepare がまだなら先に済ませる
	if (!m_taskBegun)
	{
		BeginCurrentTask();
		return true;
	}

	// 分割タスクのスロットが残っているなら1件処理する
	if (m_slotCursor < m_slotCount)
	{
		if (task.slot)
		{
			if (!task.slot(m_slotCursor)) { ++m_failedCount; }
		}

		++m_slotCursor;
		m_doneWeight += task.weight / static_cast<float>(m_slotCount);
		return true;
	}

	// 単発処理。ここで返して、commit は次のステップへ回す
	if (task.func && !m_funcDone)
	{
		task.func();
		m_funcDone = true;

		if (m_slotCount == 0) { m_doneWeight += task.weight; }
		return true;
	}

	// 後処理
	if (task.commit && !m_commitDone)
	{
		task.commit();
		m_commitDone = true;
		return true;
	}

	// 重みの加算漏れを拾う(funcもcommitも無い分割タスク)
	if (m_slotCount == 0 && !m_funcDone)
	{
		m_doneWeight += task.weight;
		m_funcDone = true;
	}

	GotoNextTask();
	return true;
}
// ----------------------------------------------------------
void LoadScheduler::BeginCurrentTask()
{
	const LoadTask& task = m_tasks[m_taskCursor];

	m_slotCursor = 0;
	m_slotCount = 0;
	m_funcDone = false;
	m_commitDone = false;

	if (task.prepare)
	{
		m_slotCount = task.prepare();
	}

	m_taskBegun = true;
}

void LoadScheduler::GotoNextTask()
{
	++m_taskCursor;
	m_slotCursor = 0;
	m_slotCount = 0;
	m_taskBegun = false;
	m_funcDone = false;
	m_commitDone = false;

	if (m_taskCursor >= m_taskCount)
	{
		m_finished = true;
		m_doneWeight = m_totalWeight;
	}
}

// ----------------------------------------------------------
float LoadScheduler::GetProgress() const
{
	if (m_totalWeight <= 0.0f) { return 1.0f; }

	const float p = m_doneWeight / m_totalWeight;
	return std::clamp(p, 0.0f, 1.0f);
}

// ----------------------------------------------------------
const char* LoadScheduler::GetCurrentLabel() const
{
	if (m_taskCursor >= m_taskCount) { return ""; }
	return m_tasks[m_taskCursor].label;
}