#pragma once
#include <atomic>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <system_error>
#include "JsonUtil.h"

//==========================================================
// JsonResource
// JSONファイル1つ分の中身を持っておくクラス。
// Poll() を定期的に呼ぶと、ファイルが書き換わっていたときに読み直す。
//
// 誰がどこから触るか
//   Poll()              … 監視スレッド(JsonResourceManager)からだけ
//   Get系 / Dirty系     … メインスレッドから呼んでOK
//
// ロックの範囲
//   m_data, m_onReload  … m_mutex で守る
//   m_lastWriteTime 等  … Poll() とコンストラクタしか触らないのでロック不要
//==========================================================
class JsonResource
{
public:

	using json = nlohmann::json;
	using onReloadCallBack = std::function<void(const json&)>;

	// 作った時点で一度読んでおく。
	// ファイルが壊れていても登録自体はしておき、直ったら Poll() で拾う
	explicit JsonResource(std::string _filePath)
		: m_filePath(std::move(_filePath))
	{
		std::error_code ec;
		const auto writeTime = std::filesystem::last_write_time(m_filePath, ec);
		if (!ec) { TryLoad(writeTime); }
	}

	JsonResource(const JsonResource&) = delete;
	JsonResource& operator=(const JsonResource&) = delete;

	//------------------------------------------------------
	// 更新チェック(監視スレッドから呼ばれる)
	//------------------------------------------------------
	void Poll()
	{
		// 例外版の last_write_time はファイルが一瞬消えただけで投げるので error_code 版を使う。
		// エディタによっては保存時に「消して作り直す」ので、取れないときは次の周回で見ればいい
		std::error_code ec;
		const auto newTime = std::filesystem::last_write_time(m_filePath, ec);
		if (ec) { return; }

		// 前回読んだときから変わっていない
		if (m_isLoaded.load() && newTime == m_lastWriteTime) { return; }

		// 壊れたまま変わっていないファイルを毎回パースし直さない
		if (m_hasFailedWriteTime && newTime == m_failedWriteTime) { return; }

		// 読み込みに失敗したときは Dirty もコールバックも動かさない
		// (前はここを見ていなかったので、保存途中のファイルでも「更新あり」になっていた)
		if (!TryLoad(newTime)) { return; }

		m_isDirty.store(true);
		NotifyReload();
	}

	// JSON全体を取る(コピーで返すのでスレッドをまたいでも安全)
	json GetAll() const
	{
		std::lock_guard lock(m_mutex);
		return m_data;
	}

	// 直下のキーを1つ取る。無ければ空の json{}
	json Get(const std::string& _key) const
	{
		std::lock_guard lock(m_mutex);
		const auto it = m_data.find(_key);
		return (it != m_data.end()) ? *it : json{};
	}

	// JSON Pointer で値を1つ取る。全体をコピーしないので数値1個だけ欲しいときはこっち
	//   例) GetValue<float>("/debug/historyMax", 16.0f)
	template<class T>
	T GetValue(const std::string& _pointer, const T& _fallback) const
	{
		std::lock_guard lock(m_mutex);
		return JsonUtil::ValueAt(m_data, _pointer, _fallback);
	}

	//------------------------------------------------------
	// Dirty フラグ
	// IsDirty() → ClearDirty() の間にリロードが挟まると、その更新を取りこぼす。
	// 「見て、消す」を1回でやりたいときは ConsumeDirty() を使う
	//------------------------------------------------------
	bool IsDirty() const { return m_isDirty.load(); }
	void ClearDirty() { m_isDirty.store(false); }
	bool ConsumeDirty() { return m_isDirty.exchange(false); }

	// 一度でも読めたかどうか
	bool IsLoaded() const { return m_isLoaded.load(); }

	//------------------------------------------------------
	// リロード時のコールバック
	// ※監視スレッドの上で呼ばれる。ゲームオブジェクトを直接いじると競合するので、
	//   メインスレッド側の処理は ConsumeDirty() を見て行うほうが安全
	//------------------------------------------------------
	void SetOnReload(onReloadCallBack _cb)
	{
		std::lock_guard lock(m_mutex);
		m_onReload = std::move(_cb);
	}

	const std::string& GetFilePath() const { return m_filePath; }

private:

	//------------------------------------------------------
	// ファイルを読んで m_data を差し替える。成功したら true
	// _writeTime は「読み始める前」に取った時刻を渡すこと。
	// 読んでいる最中に書き換えられても、次の Poll() で時刻のズレから拾い直せる
	//------------------------------------------------------
	bool TryLoad(const std::filesystem::file_time_type& _writeTime)
	{
		std::ifstream file(m_filePath);

		// 他のアプリが掴んでいて開けないだけかもしれないので、失敗扱いにはせず次を待つ
		if (!file.is_open()) { return false; }

		// 例外を投げない版でパースする
		json loaded = json::parse(file, nullptr, false);
		if (loaded.is_discarded())
		{
			m_failedWriteTime = _writeTime;
			m_hasFailedWriteTime = true;
			OutputDebugStringA(("[JsonResource] JSONの解析に失敗 : " + m_filePath + "\n").c_str());
			return false;
		}

		{
			std::lock_guard lock(m_mutex);
			m_data = std::move(loaded);
		}

		m_lastWriteTime = _writeTime;
		m_hasFailedWriteTime = false;
		m_isLoaded.store(true);
		return true;
	}

	//------------------------------------------------------
	// コールバックはロックの外で呼ぶ。
	// ロックを持ったまま呼ぶと、コールバックの中で GetAll() した瞬間に自分で自分を待って止まる
	//------------------------------------------------------
	void NotifyReload()
	{
		onReloadCallBack callback;
		json snapshot;
		{
			std::lock_guard lock(m_mutex);
			if (!m_onReload) { return; }
			callback = m_onReload;
			snapshot = m_data;
		}

		// 監視スレッドの中で例外が漏れると std::terminate で落ちるので、ここで止める
		try
		{
			callback(snapshot);
		}
		catch (const std::exception& e)
		{
			OutputDebugStringA(("[JsonResource] リロードコールバックで例外 : " + std::string(e.what()) + "\n").c_str());
		}
		catch (...)
		{
			OutputDebugStringA("[JsonResource] リロードコールバックで不明な例外\n");
		}
	}

	std::string         m_filePath;

	mutable std::mutex  m_mutex;
	json                m_data;
	onReloadCallBack    m_onReload;

	std::atomic<bool>   m_isDirty{ false };
	std::atomic<bool>   m_isLoaded{ false };

	// ここから下は Poll() 側だけが触る
	std::filesystem::file_time_type m_lastWriteTime{};
	std::filesystem::file_time_type m_failedWriteTime{};
	bool                            m_hasFailedWriteTime = false;
};
