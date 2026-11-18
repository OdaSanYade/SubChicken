#include "AudioManager.h"


using Microsoft::WRL::ComPtr;

// UTF-8(std::string) -> wide(std::wstring)。
// JSONのパスはUTF-8なので、MFCreateSourceReaderFromURL用に変換する。
static std::wstring Utf8ToWide(const std::string& s)
{
	if (s.empty()) return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(n, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
	return w;
}

static std::wstring ToWString(const std::string& str, UINT codePage = CP_UTF8)
{
	if (str.empty()) return std::wstring();

	int sizeNeeded = MultiByteToWideChar(codePage, 0, str.data(), (int)str.size(), nullptr, 0);
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(codePage, 0, str.data(), (int)str.size(), result.data(), sizeNeeded);
	return result;
}

// std::wstring(ワイド文字) → std::string(マルチバイト)
static std::string ToString(const std::wstring& wstr, UINT codePage = CP_UTF8)
{
	if (wstr.empty()) return std::string();

	int sizeNeeded = WideCharToMultiByte(codePage, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(codePage, 0, wstr.data(), (int)wstr.size(), result.data(), sizeNeeded, nullptr, nullptr);
	return result;
}

//============================================================
// SoundData::Load  (Media Foundation でデコード)
//============================================================
bool SoundData::Load(const std::wstring& path)
{
	const std::wstring fullPath = ToWString(RES_MGR.GetFilePath(ToString(path)));

	ComPtr<IMFSourceReader> reader;
	HRESULT hr = MFCreateSourceReaderFromURL(fullPath.c_str(), nullptr, reader.GetAddressOf());
	if (FAILED(hr)) return false;

	reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
	reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

	ComPtr<IMFMediaType> partialType;
	MFCreateMediaType(partialType.GetAddressOf());
	partialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
	partialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
	hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, partialType.Get());
	if (FAILED(hr)) return false;

	ComPtr<IMFMediaType> fullType;
	reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, fullType.GetAddressOf());

	WAVEFORMATEX* wfx = nullptr;
	UINT32 wfxSize = 0;
	hr = MFCreateWaveFormatExFromMFMediaType(fullType.Get(), &wfx, &wfxSize);
	if (FAILED(hr)) return false;
	m_format.assign(reinterpret_cast<BYTE*>(wfx), reinterpret_cast<BYTE*>(wfx) + wfxSize);
	CoTaskMemFree(wfx);

	for (;;)
	{
		DWORD flags = 0;
		ComPtr<IMFSample> sample;
		hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
			0, nullptr, &flags, nullptr, sample.GetAddressOf());
		if (FAILED(hr)) return false;
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
		if (!sample) continue;

		ComPtr<IMFMediaBuffer> buffer;
		sample->ConvertToContiguousBuffer(buffer.GetAddressOf());
		BYTE* audio = nullptr;
		DWORD cb = 0;
		buffer->Lock(&audio, nullptr, &cb);
		m_buffer.insert(m_buffer.end(), audio, audio + cb);
		buffer->Unlock();
	}
	return !m_buffer.empty();
}

//============================================================
// VoicePool
//============================================================
IXAudio2SourceVoice* VoicePool::Acquire(const WAVEFORMATEX* fmt)
{
	if (!m_xaudio) return nullptr;   // 未初期化/破棄後は作らない

	auto& list = m_idle[MakeKey(fmt)];
	IXAudio2SourceVoice* voice = nullptr;

	if (!list.empty())
	{
		voice = list.back();   // 使い回し
		list.pop_back();
		return voice;
	}

	// 4.0f = ピッチ/ドップラーの最大上げ幅(2オクターブ)
	HRESULT hr = m_xaudio->CreateSourceVoice(&voice, fmt, 0, 4.0f, nullptr, nullptr, nullptr);
	if (FAILED(hr))
	{
		wchar_t buf[64];
		swprintf_s(buf, L"CreateSourceVoice failed: 0x%08X\n", hr);
		OutputDebugStringW(buf);
		return nullptr;
	}
	++m_created;
	return voice;
}

void VoicePool::Release(IXAudio2SourceVoice* voice, const WAVEFORMATEX* fmt)
{
	if (!voice) return;
	// エンジンが既に破棄されている場合、このボイスもエンジンと一緒に
	// 解放済み(=dangling)。触ると落ちるので、何もせず捨てる。
	if (!m_xaudio) return;
	voice->Stop();
	voice->FlushSourceBuffers();
	m_idle[MakeKey(fmt)].push_back(voice);
}

void VoicePool::Clear()
{
	for (auto& kv : m_idle)
		for (auto v : kv.second)
			v->DestroyVoice();
	m_idle.clear();
	m_created = 0;
	m_xaudio = nullptr;   // 以後 Acquire/Release は無効として扱う(クラッシュ防止)
}

//============================================================
// SoundInstance
//============================================================
SoundInstance::SoundInstance(VoicePool* pool, IXAudio2SubmixVoice* destSubmix,
	std::shared_ptr<SoundData> data, SoundCategory category,
	bool loop, const std::wstring& key)
	: m_pool(pool), m_destSubmix(destSubmix), m_data(std::move(data)),
	m_category(category), m_key(key), m_loop(loop)
{
	m_voice = pool->Acquire(m_data->GetFormat());
	if (!m_voice) return;

	// 出力先(カテゴリのサブミックス)へルーティング。
	// SetOutputVoices は出力マトリクスを既定に戻すので、
	// 使い回しのボイスに残った3D定位もここでリセットされる。
	XAUDIO2_SEND_DESCRIPTOR sendDesc = { 0, destSubmix };
	XAUDIO2_VOICE_SENDS sends = { 1, &sendDesc };
	m_voice->SetOutputVoices(&sends);

	// ピッチ/ドップラーをリセット
	m_voice->SetFrequencyRatio(1.0f);

	XAUDIO2_BUFFER buf = {};
	buf.AudioBytes = m_data->GetSize();
	buf.pAudioData = m_data->GetData();
	buf.Flags = XAUDIO2_END_OF_STREAM;
	buf.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
	m_voice->SubmitSourceBuffer(&buf);
}

SoundInstance::~SoundInstance()
{
	// ボイスは破棄せずプールへ返却する
	if (m_voice && m_pool)
		m_pool->Release(m_voice, m_data->GetFormat());
	m_voice = nullptr;
}

void SoundInstance::Play() { if (m_voice) m_voice->Start(); }
void SoundInstance::Pause() { if (m_voice) m_voice->Stop(); }
void SoundInstance::Resume() { if (m_voice) m_voice->Start(); }

void SoundInstance::Stop()
{
	if (m_voice)
	{
		m_voice->Stop();
		m_voice->FlushSourceBuffers();
	}
}

void SoundInstance::SetVolume(float v)
{
	m_fading = false;
	m_volume = v;
	if (m_voice) m_voice->SetVolume(v);
}

//--- フェード -----------------------------------------------
void SoundInstance::FadeTo(float target, float seconds)
{
	if (seconds <= 0.0f) { SetVolume(target); return; }
	m_fadeFrom = m_volume;
	m_fadeTo = target;
	m_fadeTime = seconds;
	m_fadeElapsed = 0.0f;
	m_fading = true;
	m_stopAfterFade = false;
}

void SoundInstance::FadeIn(float seconds, float target)
{
	SetVolume(0.0f);
	FadeTo(target, seconds);
}

void SoundInstance::FadeOut(float seconds, bool stopAtEnd)
{
	FadeTo(0.0f, seconds);
	m_stopAfterFade = stopAtEnd;
}

void SoundInstance::UpdateFade(float dt)
{
	if (!m_fading) return;

	m_fadeElapsed += dt;
	float t = (m_fadeTime > 0.0f) ? (m_fadeElapsed / m_fadeTime) : 1.0f;
	if (t >= 1.0f) { t = 1.0f; m_fading = false; }

	m_volume = m_fadeFrom + (m_fadeTo - m_fadeFrom) * t;
	if (m_voice) m_voice->SetVolume(m_volume);

	if (!m_fading && m_stopAfterFade)
	{
		m_stopAfterFade = false;
		Stop();
	}
}

//--- ピッチ -------------------------------------------------
void SoundInstance::SetPitch(float ratio)
{
	m_pitch = ratio;
	if (!m_is3D) ApplyFrequency();
}

void SoundInstance::ApplyFrequency()
{
	if (!m_voice) return;
	float f = m_pitch * m_dopplerFreq;
	if (f < XAUDIO2_MIN_FREQ_RATIO) f = XAUDIO2_MIN_FREQ_RATIO;
	if (f > 4.0f)                   f = 4.0f;
	m_voice->SetFrequencyRatio(f);
}

//--- ループ -------------------------------------------------
void SoundInstance::ExitLoop()
{
	m_loop = false;
	if (m_voice) m_voice->ExitLoop();
}

//--- 再生時間 -----------------------------------------------
float SoundInstance::GetPlayTimeSec() const
{
	if (!m_voice) return 0.0f;
	XAUDIO2_VOICE_STATE st;
	m_voice->GetState(&st);
	UINT32 rate = m_data->GetSampleRate();
	if (rate == 0) return 0.0f;
	return static_cast<float>(st.SamplesPlayed) / static_cast<float>(rate);
}

float SoundInstance::GetDurationSec() const { return m_data->GetDurationSec(); }

//--- 3D -----------------------------------------------------
void SoundInstance::EnableEmitter(const X3DAUDIO_VECTOR& pos, float distanceScaler)
{
	m_is3D = true;
	m_emitter = {};

	const UINT32 ch = m_data->GetFormat()->nChannels;
	m_emitter.ChannelCount = ch;
	if (ch > 1)
	{
		// 多チャンネル音源(ステレオ等)は azimuth 配列が必須。
		// 全chをエミッタ位置に置く(azimuth=0)。3Dはモノラル推奨。
		m_azimuths.assign(ch, 0.0f);
		m_emitter.pChannelAzimuths = m_azimuths.data();
		m_emitter.ChannelRadius = 1.0f;
	}

	m_emitter.CurveDistanceScaler = (distanceScaler > 0.0f) ? distanceScaler : 1.0f;
	m_emitter.DopplerScaler = 1.0f;
	m_emitter.OrientFront = { 0.0f, 0.0f, 1.0f };
	m_emitter.OrientTop = { 0.0f, 1.0f, 0.0f };
	m_emitter.Position = pos;
	m_emitter.Velocity = { 0.0f, 0.0f, 0.0f };
}

void SoundInstance::SetPosition(const X3DAUDIO_VECTOR& pos) { m_emitter.Position = pos; }
void SoundInstance::SetVelocity(const X3DAUDIO_VECTOR& vel) { m_emitter.Velocity = vel; }

void SoundInstance::Calculate3D(const X3DAUDIO_HANDLE& engine,
	const X3DAUDIO_LISTENER& listener,
	UINT32 dstChannels,
	std::vector<float>& scratch)
{
	if (!m_is3D || !m_voice) return;
	if (m_emitter.ChannelCount == 0 || dstChannels == 0) return;   // 不正引数ガード

	scratch.assign(static_cast<size_t>(m_emitter.ChannelCount) * dstChannels, 0.0f);

	X3DAUDIO_DSP_SETTINGS dsp = {};
	dsp.SrcChannelCount = m_emitter.ChannelCount;
	dsp.DstChannelCount = dstChannels;
	dsp.pMatrixCoefficients = scratch.data();

	X3DAudioCalculate(engine, &listener, &m_emitter,
		X3DAUDIO_CALCULATE_MATRIX | X3DAUDIO_CALCULATE_DOPPLER, &dsp);

	// 第1引数は nullptr =「このボイスの唯一の送り先」に対して設定する。
	// 送り先のポインタを直接渡すよりルーティングに頑健で、落ちにくい。
	m_voice->SetOutputMatrix(nullptr,
		m_emitter.ChannelCount, dstChannels, scratch.data());

	m_dopplerFreq = dsp.DopplerFactor;
	ApplyFrequency();
}

bool SoundInstance::IsPlaying() const
{
	if (!m_voice) return false;
	XAUDIO2_VOICE_STATE st;
	m_voice->GetState(&st);
	return st.BuffersQueued > 0;
}

//============================================================
// AudioManager
//============================================================
bool AudioManager::Init()
{
	if (m_xaudio) return true;   // 二重初期化を防ぐ(エンジン差し替えによる無効ボイス対策)

	HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	m_coInitialized = SUCCEEDED(hrCo);

	if (FAILED(MFStartup(MF_VERSION)))                                  return false;
	if (FAILED(XAudio2Create(m_xaudio.GetAddressOf(), 0, XAUDIO2_DEFAULT_PROCESSOR))) return false;
	if (FAILED(m_xaudio->CreateMasteringVoice(&m_masterVoice)))         return false;

	m_pool.Init(m_xaudio.Get());

	XAUDIO2_VOICE_DETAILS md;
	m_masterVoice->GetVoiceDetails(&md);
	m_deviceChannels = md.InputChannels;
	UINT32 rate = md.InputSampleRate;

	m_xaudio->CreateSubmixVoice(&m_bgmSubmix, m_deviceChannels, rate);
	m_xaudio->CreateSubmixVoice(&m_seSubmix, m_deviceChannels, rate);

	DWORD channelMask = 0;
	m_masterVoice->GetChannelMask(&channelMask);
	X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, m_x3dHandle);

	m_listener.OrientFront = { 0.0f, 0.0f, 1.0f };
	m_listener.OrientTop = { 0.0f, 1.0f, 0.0f };
	m_listener.Position = { 0.0f, 0.0f, 0.0f };
	m_listener.Velocity = { 0.0f, 0.0f, 0.0f };

	m_rng.seed(std::random_device{}());

	return true;
}

void AudioManager::Release()
{
	m_instances.clear();   // 各ボイスはプールへ返却される
	m_pool.Clear();        // プールのボイスを破棄
	m_cache.clear();

	if (m_bgmSubmix) { m_bgmSubmix->DestroyVoice();   m_bgmSubmix = nullptr; }
	if (m_seSubmix) { m_seSubmix->DestroyVoice();    m_seSubmix = nullptr; }
	if (m_masterVoice) { m_masterVoice->DestroyVoice(); m_masterVoice = nullptr; }
	m_xaudio.Reset();

	MFShutdown();
	if (m_coInitialized) { CoUninitialize(); m_coInitialized = false; }
}

void AudioManager::Update(float deltaTime)
{
	m_time += deltaTime;

	// --- ビート ---
	// 曲の再生位置を控える。GetPlayTimeSec() は毎回GetStateを叩くので、
	// 参照が増えても1フレーム1回で済むようここでキャッシュする。
	if (auto music = m_musicClock.lock())
	{
		m_musicTime = music->GetPlayTimeSec() - m_musicOffset;
	}
	else if (m_bpm > 0.0f)
	{
		// 曲が終わった/破棄された。時計を止める。
		m_musicTime = 0.0f;
	}

	for (auto& inst : m_instances)
	{
		inst->UpdateFade(deltaTime);
		if (inst->Is3D())
			inst->Calculate3D(m_x3dHandle, m_listener, m_deviceChannels, m_matrixScratch);
	}

	// 鳴り終わった撃ちっぱなしを破棄(=ボイスをプールへ返却)
	for (auto it = m_instances.begin(); it != m_instances.end(); )
	{
		if (!(*it)->IsPlaying() && it->use_count() == 1)
			it = m_instances.erase(it);
		else
			++it;
	}
}

std::shared_ptr<SoundData> AudioManager::GetSoundData(const std::wstring& path)
{
	auto it = m_cache.find(path);
	if (it != m_cache.end()) return it->second;

	auto data = std::make_shared<SoundData>();
	if (!data->Load(path)) return nullptr;
	m_cache[path] = data;
	return data;
}

//--- 再生枠の確保 ------------------------------------------
bool AudioManager::TryReserve(const std::wstring& key, const SoundPolicy& policy)
{
	// クールダウン(最小再生間隔)
	if (policy.cooldown > 0.0f)
	{
		auto lit = m_lastPlay.find(key);
		if (lit != m_lastPlay.end() && (m_time - lit->second) < policy.cooldown)
			return false;
	}

	// 同一音の最大同時数
	if (policy.maxInstances > 0)
	{
		int count = 0;
		for (auto& i : m_instances) 
			if (i->GetKey() == key) 
				++count;
		
		if (count >= policy.maxInstances && !StealOne(&key))
		{
			for (auto& i : m_instances)
				if (i->GetKey() == key)
					OutputDebugStringW((L"stuck: " + key + L" refcount=" + std::to_wstring(i.use_count())).c_str());
			return false;
		}
	}

	// 全体の同時発音数
	if (static_cast<int>(m_instances.size()) >= m_maxVoices && !StealOne(nullptr))
		return false;

	return true;
}

bool AudioManager::StealOne(const std::wstring* sameKey)
{
	// 古い順に走査し、奪ってよい音を1つ止める
	for (auto it = m_instances.begin(); it != m_instances.end(); ++it)
	{
		SoundInstance* inst = it->get();
		if (inst->GetCategory() == SoundCategory::BGM) continue; // BGMは守る
		if (inst->IsLoop())                            continue; // ループは守る
		if (it->use_count() != 1)                      continue; // ユーザー保持中は不可
		if (sameKey && inst->GetKey() != *sameKey)     continue;

		inst->Stop();
		m_instances.erase(it);
		return true;
	}
	return false;
}

std::shared_ptr<SoundInstance> AudioManager::Play(const std::wstring& path,
	SoundCategory category, float volume, bool loop)
{
	SoundPolicy policy{};
	auto pit = m_policies.find(path);
	if (pit != m_policies.end()) policy = pit->second;

	if (!TryReserve(path, policy)) return nullptr;

	auto data = GetSoundData(path);
	if (!data) return nullptr;

	auto inst = std::make_shared<SoundInstance>(
		&m_pool, GetSubmix(category), data, category, loop, path);
	inst->SetVolume(volume);
	inst->Play();
	m_instances.push_back(inst);
	m_lastPlay[path] = m_time;
	return inst;
}

std::shared_ptr<SoundInstance> AudioManager::Play3D(const std::wstring& path,
	const X3DAUDIO_VECTOR& position, SoundCategory category,
	float volume, bool loop, float distanceScaler)
{
	SoundPolicy policy{};
	auto pit = m_policies.find(path);
	if (pit != m_policies.end()) policy = pit->second;

	if (!TryReserve(path, policy)) return nullptr;

	auto data = GetSoundData(path);
	if (!data) return nullptr;

	auto inst = std::make_shared<SoundInstance>(
		&m_pool, GetSubmix(category), data, category, loop, path);
	inst->EnableEmitter(position, distanceScaler);
	inst->SetVolume(volume);
	inst->Calculate3D(m_x3dHandle, m_listener, m_deviceChannels, m_matrixScratch);
	inst->Play();
	m_instances.push_back(inst);
	m_lastPlay[path] = m_time;
	return inst;
}

//--- サウンドバンク ----------------------------------------
float AudioManager::RandRange(float a, float b)
{
	if (a >= b) return a;
	std::uniform_real_distribution<float> dist(a, b);
	return dist(m_rng);
}

void AudioManager::LoadBank(const nlohmann::json& bank)
{
	if (!bank.is_object()) return;

	for (auto it = bank.begin(); it != bank.end(); ++it)
	{
		const std::string& name = it.key();
		const nlohmann::json& v = it.value();
		if (!v.is_object()) continue;

		SoundDef def;
		def.path = Utf8ToWide(v.value("path", std::string{}));
		if (def.path.empty()) continue;   // pathは必須

		std::string cat = v.value("category", std::string("SE"));
		def.category = (cat == "BGM") ? SoundCategory::BGM : SoundCategory::SE;

		def.volume = v.value("volume", 1.0f);
		// pitchMin/Max が無ければ pitch、それも無ければ 1.0
		float pitch = v.value("pitch", 1.0f);
		def.pitchMin = v.value("pitchMin", pitch);
		def.pitchMax = v.value("pitchMax", pitch);
		def.loop = v.value("loop", false);
		def.cooldown = v.value("cooldown", 0.0f);
		def.maxInstances = v.value("maxInstances", 0);
		def.distanceScaler = v.value("distanceScaler", 10.0f);
		
		m_bank[name] = def;   // 追記/上書き
	}
}

void AudioManager::LoadBankFile(const std::string& path)
{
	std::ifstream file(path);
	if (!file.is_open()) return;
	try
	{
		nlohmann::json j;
		file >> j;
		LoadBank(j);
	}
	catch (const std::exception&) { /* パース失敗時は無視 */ }
}

std::shared_ptr<SoundInstance> AudioManager::PlayDef(
	const SoundDef& def, bool use3D, const X3DAUDIO_VECTOR& pos)
{
	SoundPolicy policy{ def.cooldown, def.maxInstances };
	if (!TryReserve(def.path, policy)) return nullptr;

	auto data = GetSoundData(def.path);
	if (!data) return nullptr;

	auto inst = std::make_shared<SoundInstance>(
		&m_pool, GetSubmix(def.category), data, def.category, def.loop, def.path);
	inst->SetVolume(def.volume);

	const float pitch = RandRange(def.pitchMin, def.pitchMax);

	if (use3D)
	{
		inst->EnableEmitter(pos, def.distanceScaler);
		inst->SetPitch(pitch);   // 3Dは次のCalculate3Dでdoppler込みで反映
		inst->Calculate3D(m_x3dHandle, m_listener, m_deviceChannels, m_matrixScratch);
	}
	else
	{
		inst->SetPitch(pitch);
	}

	inst->Play();
	m_instances.push_back(inst);
	m_lastPlay[def.path] = m_time;
	return inst;
}

std::shared_ptr<SoundInstance> AudioManager::Play(const std::string& name)
{
	auto it = m_bank.find(name);
	if (it == m_bank.end()) return nullptr;   // 未定義の論理名
	return PlayDef(it->second, false, { 0.0f, 0.0f, 0.0f });
}

std::shared_ptr<SoundInstance> AudioManager::Play3D(const std::string& name,
	const X3DAUDIO_VECTOR& position)
{
	auto it = m_bank.find(name);
	if (it == m_bank.end()) return nullptr;
	return PlayDef(it->second, true, position);
}

//--- ボリューム(バス) --------------------------------------
void AudioManager::SetMasterVolume(float v) { m_masterVolume = v; if (m_masterVoice) m_masterVoice->SetVolume(v); }
void AudioManager::SetBgmVolume(float v) { m_bgmVolume = v; if (m_bgmSubmix)   m_bgmSubmix->SetVolume(v); }
void AudioManager::SetSeVolume(float v) { m_seVolume = v; if (m_seSubmix)    m_seSubmix->SetVolume(v); }

//--- 停止 ---------------------------------------------------
void AudioManager::StopAll()
{
	for (auto& i : m_instances) i->Stop();
	m_instances.clear();
}

void AudioManager::StopAll(SoundCategory category)
{
	for (auto it = m_instances.begin(); it != m_instances.end(); )
	{
		if ((*it)->GetCategory() == category)
		{
			(*it)->Stop();
			it = m_instances.erase(it);
		}
		else ++it;
	}
}

//--- 3Dリスナー --------------------------------------------
//============================================================
// ビート(音楽同期)
//============================================================
void AudioManager::SetMusicClock(const std::shared_ptr<SoundInstance>& bgm, float bpm, float offsetSec)
{
	m_musicClock = bgm;
	m_bpm = (bpm > 0.0f) ? bpm : 0.0f;
	m_musicOffset = offsetSec;
	m_musicTime = 0.0f;
}

void AudioManager::ClearMusicClock()
{
	m_musicClock.reset();
	m_bpm = 0.0f;
	m_musicOffset = 0.0f;
	m_musicTime = 0.0f;
}

float AudioManager::GetBeatDuration(float subdivision) const
{
	if (m_bpm <= 0.0f) return 0.0f;
	if (subdivision <= 0.0f) subdivision = 1.0f;

	return (60.0f / m_bpm) / subdivision;
}

float AudioManager::GetBeatPhase(float subdivision) const
{
	const float beat = GetBeatDuration(subdivision);
	if (beat <= 0.0f) return 0.0f;

	// 曲頭より前(オフセット待ち)は拍頭に張り付かせる
	if (m_musicTime <= 0.0f) return 0.0f;

	const float phase = std::fmod(m_musicTime, beat) / beat;
	return (phase < 0.0f) ? (phase + 1.0f) : phase;
}

float AudioManager::TimeToNextBeat(float subdivision) const
{
	const float beat = GetBeatDuration(subdivision);
	if (beat <= 0.0f) return 0.0f;

	return beat * (1.0f - GetBeatPhase(subdivision));
}

bool AudioManager::IsNearBeat(float window, float subdivision) const
{
	const float beat = GetBeatDuration(subdivision);
	if (beat <= 0.0f) return true;	// テンポ未設定なら常に許可

	// 窓が1拍より広いなら常時true。指定ミスで固まらないようにする。
	if (window * 2.0f >= beat) return true;

	const float phase = GetBeatPhase(subdivision);
	const float w = window / beat;

	// 拍頭の直前(次の拍へ食い込む側)と直後の両方を拾う
	return (phase <= w) || (phase >= 1.0f - w);
}

int AudioManager::GetBeatIndex(float subdivision) const
{
	const float beat = GetBeatDuration(subdivision);
	if (beat <= 0.0f) return 0;
	if (m_musicTime <= 0.0f) return 0;

	return static_cast<int>(m_musicTime / beat);
}

void AudioManager::SetListener(const X3DAUDIO_VECTOR& pos, const X3DAUDIO_VECTOR& front,
	const X3DAUDIO_VECTOR& up, const X3DAUDIO_VECTOR& velocity)
{
	m_listener.Position = pos;
	m_listener.OrientFront = front;
	m_listener.OrientTop = up;
	m_listener.Velocity = velocity;
}

//--- ボイス制限 / ポリシー ---------------------------------
void AudioManager::SetMaxVoices(int n) { m_maxVoices = (n > 0) ? n : 1; }

void AudioManager::SetSoundPolicy(const std::wstring& path, float cooldownSec, int maxInstances)
{
	m_policies[path] = { cooldownSec, maxInstances };
}