#pragma once
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include "JsonResource.h"
#include "../LessPath/AssetManager.h"

//==========================================================
// JsonResourceManager
// JSONファイルをキーで管理して、監視スレッドで更新を拾う。
//
// ロックは2種類
//   m_mapMutex  … 登録表(m_resources)だけを守る。ファイルI/O中は持たない
//   m_stopMutex … 監視スレッドを止めるとき用(condition_variable とセット)
//==========================================================
class JsonResourceManager
{
public:

	using json = nlohmann::json;

	// 監視間隔の既定値と、設定ファイルで変えられる範囲
	static constexpr int kDefaultPollIntervalMs = 300;
	static constexpr int kMinPollIntervalMs = 16;		// 1フレームより短くしてもファイルI/Oが増えるだけ
	static constexpr int kMaxPollIntervalMs = 10000;	// 10秒以上空くとホットリロードの意味が薄い

	// ----------------------------------------------------------
	//  コンストラクタ
	//  _pollIntervalMs : 監視間隔(ミリ秒)。あとから SetPollIntervalMs() でも変えられる
	// ----------------------------------------------------------
	explicit JsonResourceManager(int _pollIntervalMs = kDefaultPollIntervalMs)
		: m_pollIntervalMs(ClampInterval(_pollIntervalMs))
	{
		// メンバが全部できあがってからスレッドを起こす。
		// 初期化子リストで起動すると、まだ作られていないメンバを監視スレッドが触る可能性がある
		m_watcherThread = std::thread([this]() { WatcherLoop(); });
	}

	//コピーガード
	JsonResourceManager(const JsonResourceManager&) = delete;
	JsonResourceManager& operator = (const JsonResourceManager&) = delete;

	~JsonResourceManager()
	{
		{
			std::lock_guard lock(m_stopMutex);
			m_isRunning = false;
		}
		// 待機中のスレッドをすぐ起こす。前は sleep_for だったので終了時に最大で監視間隔ぶん止まっていた
		m_stopCv.notify_all();

		if (m_watcherThread.joinable()) { m_watcherThread.join(); }
	}


	// ----------------------------------------------------------
	// 登録(アドレサブル名から)
	// キーを省略するとファイル名(拡張子なし)がキーになる。
	// 見つからないときは false。Register は assert で止め、TryRegister は止めない
	// ----------------------------------------------------------
	bool Register(const std::string& _addressable)
	{
		const bool ok = TryRegister(_addressable);
		assert(ok && "JsonResourceManager: 登録できないJSONがあります");
		return ok;
	}
	bool Register(const std::string& _addressable, const std::string& _key)
	{
		const bool ok = TryRegister(_addressable, _key);
		assert(ok && "JsonResourceManager: 登録できないJSONがあります");
		return ok;
	}

	bool TryRegister(const std::string& _addressable)
	{
		std::string path;
		if (!ASSET_MGR.TryGetFilePath(_addressable, path))
		{
			Report("アドレサブル名が見つからない : " + _addressable);
			return false;
		}
		return AddResource(path, StemOf(path));
	}
	bool TryRegister(const std::string& _addressable, const std::string& _key)
	{
		std::string path;
		if (!ASSET_MGR.TryGetFilePath(_addressable, path))
		{
			Report("アドレサブル名が見つからない : " + _addressable);
			return false;
		}
		return AddResource(path, _key);
	}

	// ----------------------------------------------------------
	// 登録(ファイルパス直指定)
	// AssetManager を初期化する前に読みたい設定ファイル用。
	// 普段のデータは Register(アドレサブル名) を使うこと
	// ----------------------------------------------------------
	bool TryRegisterByPath(const std::string& _filePath, const std::string& _key)
	{
		std::error_code ec;
		if (!std::filesystem::is_regular_file(_filePath, ec))
		{
			Report("ファイルが無い : " + _filePath);
			return false;
		}
		return AddResource(_filePath, _key);
	}


	// 全体のデータの取得
	json Get(const std::string& _key) const
	{
		const auto resource = Find(_key);
		return resource ? resource->GetAll() : json{};
	}
	// ----------------------------------------------------------
	// 特定のデータの取得
	// _key   : データのキー
	// _field : フィールド(直下のキー)
	// ----------------------------------------------------------
	json Get(const std::string& _key, const std::string& _field) const
	{
		const auto resource = Find(_key);
		return resource ? resource->Get(_field) : json{};
	}

	// ----------------------------------------------------------
	// 値を1つ取る。マジックナンバーを設定ファイルに逃がすとき用
	// _pointer  : "/animation/defaultBlendFrame" のような JSON Pointer
	// _fallback : キーが無い・型が違う・ファイル自体が無いときの値
	// ----------------------------------------------------------
	template<class T>
	T GetValue(const std::string& _key, const std::string& _pointer, const T& _fallback) const
	{
		const auto resource = Find(_key);
		return resource ? resource->GetValue(_pointer, _fallback) : _fallback;
	}


	//---------------------------------------------------------
	// IsDirty       : 指定キーのファイルが更新されているかどうか
	// ClearDirty    : 指定キーのフラグをリセット(Get()の後に呼ぶ)
	// ConsumeDirty  : 「更新されていたか」を返しつつフラグを下ろす(取りこぼしなし)
	// IsAnyDirty    : どれか1つでも更新されているか
	// ClearAllDirty : すべてのフラグをリセット
	//----------------------------------------------------------
	bool IsDirty(const std::string& _key) const
	{
		const auto resource = Find(_key);
		return resource && resource->IsDirty();
	}
	void ClearDirty(const std::string& _key)
	{
		if (const auto resource = Find(_key)) { resource->ClearDirty(); }
	}
	bool ConsumeDirty(const std::string& _key)
	{
		const auto resource = Find(_key);
		return resource && resource->ConsumeDirty();
	}
	bool IsAnyDirty() const
	{
		std::lock_guard lock(m_mapMutex);
		for (const auto& [key, resource] : m_resources)
		{
			if (resource->IsDirty()) { return true; }
		}
		return false;
	}
	void ClearAllDirty()
	{
		std::lock_guard lock(m_mapMutex);
		for (const auto& [key, resource] : m_resources) { resource->ClearDirty(); }
	}

	//---------------------------------------------------------
	// コールバック
	// Jsonが更新された時に自動で呼ばれる関数を登録
	// ※監視スレッド上で呼ばれる。中で Get() を呼んでも止まらないようにはしてあるが、
	//   ゲーム側のオブジェクトを触るなら ConsumeDirty() をメインスレッドで見るほうが安全
	//----------------------------------------------------------
	void SetOnReload(const std::string& _key, JsonResource::onReloadCallBack _cb)
	{
		if (const auto resource = Find(_key)) { resource->SetOnReload(std::move(_cb)); }
	}

	// 監視間隔の変更(範囲外は丸める)
	void SetPollIntervalMs(int _intervalMs) { m_pollIntervalMs.store(ClampInterval(_intervalMs)); }
	int  GetPollIntervalMs() const { return m_pollIntervalMs.load(); }

	//ユーティリティ
	//キーが存在するかどうか
	bool Contains(const std::string& _key) const
	{
		std::lock_guard lock(m_mapMutex);
		return m_resources.contains(_key);
	}
	//すべてのキーを取得
	std::vector<std::string> GetKeys() const
	{
		std::lock_guard lock(m_mapMutex);
		std::vector<std::string> keys;
		keys.reserve(m_resources.size());
		for (const auto& [key, resource] : m_resources) { keys.push_back(key); }
		return keys;
	}
	//登録しているデータ数を取得
	std::size_t Size() const
	{
		std::lock_guard lock(m_mapMutex);
		return m_resources.size();
	}

private:

	using ResourcePtr = std::shared_ptr<JsonResource>;

	// 登録の本体
	bool AddResource(const std::string& _filePath, const std::string& _key)
	{
		{
			std::lock_guard lock(m_mapMutex);
			const auto it = m_resources.find(_key);

			// 同じキー・同じファイルの二重登録は何もしない(Init を2回呼んだときなど)
			if (it != m_resources.end() && it->second->GetFilePath() == _filePath) { return true; }
		}

		// ファイルの読み込みはロックの外でやる。
		// 持ったまま読むと、その間メインスレッドの Get() が全部待たされる
		auto resource = std::make_shared<JsonResource>(_filePath);
		if (!resource->IsLoaded())
		{
			// 登録はしておく。ファイルを直せばホットリロードで読み込まれる
			Report("読み込めなかったので空のまま登録 : " + _filePath);
		}

		std::lock_guard lock(m_mapMutex);
		const auto it = m_resources.find(_key);
		if (it != m_resources.end())
		{
			Report("キー '" + _key + "' を別ファイルで上書き : " + _filePath);
		}
		m_resources[_key] = std::move(resource);
		return true;
	}

	// キーから取り出す。shared_ptr のコピーで返すので、ロックを外したあとも安全に使える
	ResourcePtr Find(const std::string& _key) const
	{
		std::lock_guard lock(m_mapMutex);
		const auto it = m_resources.find(_key);
		return (it != m_resources.end()) ? it->second : nullptr;
	}

	// 監視用に今の登録内容を写し取る
	std::vector<ResourcePtr> SnapshotResources() const
	{
		std::lock_guard lock(m_mapMutex);
		std::vector<ResourcePtr> list;
		list.reserve(m_resources.size());
		for (const auto& [key, resource] : m_resources) { list.push_back(resource); }
		return list;
	}

	//スレッド内動作
	void WatcherLoop()
	{
		while (true)
		{
			// 登録表のロックは写し取る間だけ。Poll(=ファイルI/Oとコールバック)はロックの外。
			// 前はロックを持ったまま回していたので、コールバックから Get() を呼ぶと止まっていた
			for (const auto& resource : SnapshotResources())
			{
				resource->Poll();
			}

			std::unique_lock lock(m_stopMutex);
			const auto interval = std::chrono::milliseconds(m_pollIntervalMs.load());
			const bool stopRequested = m_stopCv.wait_for(lock, interval, [this]() { return !m_isRunning; });
			if (stopRequested) { return; }
		}
	}

	static int ClampInterval(int _intervalMs)
	{
		return std::clamp(_intervalMs, kMinPollIntervalMs, kMaxPollIntervalMs);
	}

	static void Report(const std::string& _message)
	{
		OutputDebugStringA(("[JsonResourceManager] " + _message + "\n").c_str());
	}

	//送られてくるファイルパスからディレクトリと拡張子を除く
	static std::string StemOf(const std::string& _filePath) { return std::filesystem::path(_filePath).stem().string(); }

	std::unordered_map<std::string, ResourcePtr> m_resources;
	mutable std::mutex m_mapMutex;

	std::mutex              m_stopMutex;
	std::condition_variable m_stopCv;
	bool                    m_isRunning = true;		// m_stopMutex で守る

	std::atomic<int>        m_pollIntervalMs;

	// スレッドは最後に宣言しておく(上のメンバが先に作られる)
	std::thread             m_watcherThread;
};
