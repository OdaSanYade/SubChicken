#include "AssetManager.h"
#include "../DataList/JsonUtil.h"

namespace
{
	// ファイル名の中でAddressableNameの始まりを示す記号  例) Player@PlayerModel.gltf
	constexpr char kAddressableMark = '@';

	// メタファイルのキー
	constexpr const char* kMetaKeySourceFile = "SourceFile";
	constexpr const char* kMetaKeyAddressable = "AddressableName";

	// _path が _dir の下にあるか。
	// 文字列の find だと "Fonts2" も "Fonts" に引っかかるし、区切り文字の / と \ の違いでも外れるので、
	// 正規化してからディレクトリ1段ずつ比べる
	bool IsUnderDirectory(const std::filesystem::path& _path, const std::filesystem::path& _dir)
	{
		const std::filesystem::path path = _path.lexically_normal();
		std::filesystem::path dir = _dir.lexically_normal();

		// "a/b/" のような末尾の区切りは落としておく
		if (!dir.has_filename()) { dir = dir.parent_path(); }
		if (dir.empty()) { return false; }

		auto pathIt = path.begin();
		for (const auto& dirPart : dir)
		{
			if (pathIt == path.end() || *pathIt != dirPart) { return false; }
			++pathIt;
		}
		return true;
	}
}

// ===========================================================
// 設定
// ===========================================================
C_AssetManager::Config C_AssetManager::Config::FromJson(const nlohmann::json& _json)
{
	Config config;

	JsonUtil::TryRead(_json, "assetRoot", config.assetRoot);
	JsonUtil::TryRead(_json, "metaExtension", config.metaExtension);
	JsonUtil::TryRead(_json, "logFile", config.logFile);
	JsonUtil::TryRead(_json, "fontDirectory", config.fontDirectory);
	JsonUtil::TryRead(_json, "regenerateMetaFiles", config.regenerateMetaFiles);

	// 拡張子リストは1つでも文字列以外が混ざっていたら丸ごと既定値のままにする
	std::vector<std::string> extensions;
	if (JsonUtil::TryRead(_json, "supportedExtensions", extensions) && !extensions.empty())
	{
		config.supportedExtensions = std::move(extensions);
	}

	return config;
}

// ===========================================================
// 初期化
// ===========================================================
void C_AssetManager::Init()
{
	Init(Config{});
}

void C_AssetManager::Init(const nlohmann::json& _configJson)
{
	Init(Config::FromJson(_configJson));
}

// Assetの追加が終わったら処理を消す
void C_AssetManager::Init(const Config& _config)
{
	m_config = _config;

	// ログはここで一度だけ作り直して、あとの工程はすべて追記にする。
	// 前は工程ごとに std::ofstream(ファイル名) で開いていたので、毎回上書きされて
	// 最後の工程のログしか残っていなかった
	if (m_log.is_open()) { m_log.close(); }
	m_log.open(m_config.logFile, std::ios::out | std::ios::trunc);

	// 作り直す設定のときだけ消す。消さない設定でも、新しく増えたファイルの分は下で作られる
	if (m_config.regenerateMetaFiles) { DeleteAllMetaFiles(); }
	CreateMetaFileForAllFiles();

	m_addressables.clear();
	CreateAddressablesList();
}

// ===========================================================
// Metaファイルを探してAddressablesListを作成する
// ===========================================================
void C_AssetManager::CreateAddressablesList()
{
	ForEachAssetEntry([this](const std::filesystem::directory_entry& entry)
		{
			if (!IsMetaFile(entry)) { return; }

			std::string addressable;
			MetaData metaData;
			if (!ReadMetaFile(entry.path(), addressable, metaData)) { return; }

			// Addressableの被りは許さず。先に見つかったほうを残す
			if (m_addressables.contains(addressable))
			{
				WriteLog("error! : " + addressable + " This AddressableName is Conflict! filePath : " + metaData.filePath);
				assert(0 && "AddressableNameが被っています！！ Logファイルを参照して下さい");
				return;
			}

			// AddressableNameをキーにしてデータを覚えておく
			m_addressables.emplace(std::move(addressable), std::move(metaData));
		});
}

// メタファイル1つを読む
bool C_AssetManager::ReadMetaFile(const std::filesystem::path& _metaPath,
	std::string& _outAddressable, MetaData& _outMeta) const
{
	std::ifstream meta(_metaPath);
	if (!meta.is_open())
	{
		WriteLog("error! : Metaファイルが開けません " + _metaPath.string());
		return false;
	}

	// 手で書き換えたメタファイルが壊れていても起動は止めない(ログに残してスキップ)
	const nlohmann::json j = nlohmann::json::parse(meta, nullptr, false);
	if (j.is_discarded() || !j.is_object())
	{
		WriteLog("error! : Metaファイルの形式が不正です " + _metaPath.string());
		return false;
	}

	std::string sourceFile;
	if (!JsonUtil::TryRead(j, kMetaKeyAddressable, _outAddressable) ||
		!JsonUtil::TryRead(j, kMetaKeySourceFile, sourceFile) ||
		_outAddressable.empty() || sourceFile.empty())
	{
		WriteLog("error! : Metaファイルに必要なキーがありません " + _metaPath.string());
		return false;
	}

	// メタファイルと同じフォルダにある元ファイルのパスを今回の実行用に組み立てる
	_outMeta.filePath = (_metaPath.parent_path() / sourceFile).string();
	return true;
}

// ===========================================================
// AddressableNameからファイルパス取得
// ===========================================================
// 失敗を返せる版
bool C_AssetManager::TryGetFilePath(const std::string& addressableName, std::string& out) const
{
	const auto it = m_addressables.find(addressableName);
	if (it == m_addressables.end()) { return false; }

	out = it->second.filePath;
	return true;
}

// 登録確認
bool C_AssetManager::Contains(const std::string& addressableName) const
{
	return m_addressables.contains(addressableName);
}

std::string C_AssetManager::GetFilePath(const std::string& addressableName) const
{
	std::string path;
	if (TryGetFilePath(addressableName, path)) { return path; }

	// 見つからなかった名前を必ず出力する
	// assertだけだとどの名前で失敗したのか分からず、Releaseでは何も残らない
	const std::string msg = "[AssetManager] AddressableName not found : " + addressableName;
	OutputDebugStringA((msg + "\n").c_str());
	WriteLog(msg);

	assert(0 && "指定されたAddressableNameが見つかりません！");
	return std::string();
}

// ===========================================================
// Assetフォルダ以下の対応ファイル全てにMetaファイルを作っていく
// ===========================================================
void C_AssetManager::CreateMetaFileForAllFiles()
{
	ForEachAssetEntry([this](const std::filesystem::directory_entry& entry)
		{
			std::error_code ec;
			if (!entry.is_regular_file(ec) || !IsSupportedFile(entry.path())) { return; }

			// フォントファイルはFontManagerが管理するのでスキップ
			if (IsInFontDirectory(entry.path())) { return; }

			// メタファイルが有るか
			std::filesystem::path metaFilePath = entry.path();
			metaFilePath.replace_filename(entry.path().filename().string() + m_config.metaExtension);
			if (std::filesystem::exists(metaFilePath, ec)) { return; }

			// メタファイルがなかったら新規作成
			std::ofstream metaFile(metaFilePath);
			if (!metaFile.is_open())
			{
				WriteLog("error! : Metaファイルを作れません " + metaFilePath.string());
				return;
			}

			metaFile << CreateMetaFileForFile(entry.path());
			WriteLog("CreateMetaFile!  " + metaFilePath.string());
		});
}

// ファイル拡張子がサポートしている形式か調べる
bool C_AssetManager::IsSupportedFile(const std::filesystem::path& filePath) const
{
	const std::string extension = filePath.extension().string();
	for (const auto& supported : m_config.supportedExtensions)
	{
		if (extension == supported) { return true; }
	}
	return false;
}

// 渡されたファイルに対してMetaファイルを作成する
nlohmann::json C_AssetManager::CreateMetaFileForFile(const std::filesystem::path& srcFile) const
{
	nlohmann::json j;
	j[kMetaKeySourceFile] = srcFile.filename().string();
	j[kMetaKeyAddressable] = AddressableName(srcFile);
	return j;
}

// AddressableName作成
//   "Player@PlayerModel.gltf" → "PlayerModel"
//   "@" が無い              → ファイルパスそのまま
std::string C_AssetManager::AddressableName(const std::filesystem::path& srcFile) const
{
	const std::string fileName = srcFile.filename().string();

	const size_t at = fileName.find(kAddressableMark);
	if (at == std::string::npos)
	{
		// Addressableが指定されていない場合、ファイルパスをそのまま
		return srcFile.relative_path().string();
	}

	// "." は "@" より後ろから探す。
	// 前はファイル名の先頭から探していたので "a.b@Name.json" のような名前で長さが負になっていた
	const size_t nameBegin = at + 1;
	const size_t dot = fileName.find('.', nameBegin);
	const size_t nameLength = (dot == std::string::npos) ? std::string::npos : dot - nameBegin;
	return fileName.substr(nameBegin, nameLength);
}

// ===========================================================
// Metaファイル全削除。めっちゃ危険なので regenerateMetaFiles が true のときしか呼ばない
// ===========================================================
void C_AssetManager::DeleteAllMetaFiles()
{
	// 回している最中に消すとイテレータの動きが処理系任せになるので、先に集めてから消す
	std::vector<std::filesystem::path> targets;
	ForEachAssetEntry([this, &targets](const std::filesystem::directory_entry& entry)
		{
			if (IsMetaFile(entry)) { targets.push_back(entry.path()); }
		});

	for (const auto& path : targets)
	{
		std::error_code ec;
		if (std::filesystem::remove(path, ec))
		{
			WriteLog("Delete Meta File! " + path.string());
		}
		else if (ec)
		{
			WriteLog("error! : Metaファイルを消せません " + path.string() + " (" + ec.message() + ")");
		}
	}
}

// ===========================================================
// 内部処理
// ===========================================================
void C_AssetManager::ForEachAssetEntry(const std::function<void(const std::filesystem::directory_entry&)>& _fn) const
{
	std::error_code ec;
	std::filesystem::recursive_directory_iterator it(
		m_config.assetRoot, std::filesystem::directory_options::skip_permission_denied, ec);

	// Assetフォルダが無いと例外版のイテレータはここで投げて落ちていた
	if (ec)
	{
		WriteLog("error! : Assetフォルダを開けません " + m_config.assetRoot + " (" + ec.message() + ")");
		return;
	}

	const std::filesystem::recursive_directory_iterator end;
	for (; it != end; it.increment(ec))
	{
		if (ec) { break; }
		_fn(*it);
	}

	if (ec)
	{
		WriteLog("error! : Assetフォルダの走査中にエラー (" + ec.message() + ")");
	}
}

bool C_AssetManager::IsMetaFile(const std::filesystem::directory_entry& _entry) const
{
	std::error_code ec;
	return _entry.is_regular_file(ec) && _entry.path().extension().string() == m_config.metaExtension;
}

bool C_AssetManager::IsInFontDirectory(const std::filesystem::path& _path) const
{
	if (m_config.fontDirectory.empty()) { return false; }
	return IsUnderDirectory(_path, m_config.fontDirectory);
}

void C_AssetManager::WriteLog(const std::string& _message) const
{
	// Init より前に呼ばれたときは追記で開く
	if (!m_log.is_open())
	{
		m_log.open(m_config.logFile, std::ios::out | std::ios::app);
		if (!m_log.is_open()) { return; }
	}
	m_log << _message << std::endl;
}
