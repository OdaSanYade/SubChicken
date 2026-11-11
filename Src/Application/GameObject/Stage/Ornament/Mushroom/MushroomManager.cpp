#include "Pch.h"
#include "MushroomManager.h"
#include "Mushroom.h"

namespace {
	// 配置の再現用の乱数(線形合同法)。KdRandom を使うと保存した配置が毎回変わってしまう。
	// 係数は Numerical Recipes の定番値。変えると保存済みの散らばりが全部変わるので触らないこと
	struct Lcg
	{
		static constexpr unsigned int kMultiplier = 1664525u;
		static constexpr unsigned int kIncrement = 1013904223u;
		static constexpr int          kMantissaBits = 24;	// float に落とすときに使う上位ビット数

		unsigned int state = 1u;

		explicit Lcg(unsigned int _seed) : state(_seed ? _seed : 1u) {}

		unsigned int Next()
		{
			state = state * kMultiplier + kIncrement;
			return state;
		}
		float NextFloat(float _min, float _max)
		{
			// 上位24ビットを 0〜1 にする(下位ビットは周期が短いので使わない)
			const float t = static_cast<float>(Next() >> (32 - kMantissaBits))
				/ static_cast<float>(1u << kMantissaBits);
			return _min + (_max - _min) * t;
		}
	};

	constexpr int kSeedMax = 0x7FFFFFFF;	// 0 は Lcg 側で 1 に置き換わるので 1 から引く

	unsigned int MakeSeed()
	{
		return static_cast<unsigned int>(KdRandom::GetInt(1, kSeedMax));
	}

	// 配置UIの「プレイヤー位置を取得」で掛ける倍率。
	// 保存する座標は縮尺前(GameScale を掛ける前)の値なので、今の座標を元に戻す。
	// ワールド縮尺(1/5)を変えたらここも合わせること
	constexpr float kWorldToPlacement = 5.0f;

	// 配置UIのスライダーの範囲(見た目の操作感だけで、保存値の制限ではない)
	constexpr float kUiAnchorStep = 0.5f;
	constexpr float kUiRangeMin = 1.0f, kUiRangeMax = 40.0f;
	constexpr float kUiScaleMin = 0.2f, kUiScaleMax = 3.0f;
	constexpr float kUiPowerMin = 0.0f, kUiPowerMax = 10.0f;
	constexpr float kUiLightRangeMin = 0.5f, kUiLightRangeMax = 30.0f;
	constexpr float kUiOffsetMin = -0.5f, kUiOffsetMax = 0.5f;
	constexpr int   kJsonIndent = 4;
}

void MushroomManager::Init()
{
	// 調整値は全員ぶんここで1回だけ読む
	Tuning::Load(m_tuning, StageTuningKey::kFile, "MushroomManager");
	Tuning::Load(m_mushroomTuning, StageTuningKey::kFile, "Mushroom");

	LoadSpawns();

	m_edit = MakeDefaultEntry();
	m_edit.seed = MakeSeed();

	SpawnAll();
}

MushroomManager::SpawnEntry MushroomManager::MakeDefaultEntry() const
{
	SpawnEntry e;
	e.range = m_tuning.defaultRange;
	e.count = m_tuning.defaultCount;
	e.lightPower = m_tuning.defaultLightPower;
	e.lightRange = m_tuning.defaultLightRange;
	e.groundOffset = m_tuning.defaultGroundOffset;
	return e;
}

void MushroomManager::Update()
{
	for (auto& mushroom : m_spMushrooms) mushroom->Update();
	for (auto& mushroom : m_spPreview)   mushroom->Update();
}

// ===========================================================
// 生成
// ===========================================================
std::shared_ptr<Mushroom> MushroomManager::MakeMushroom(const SpawnEntry& _entry,
	const Math::Vector3& _pos, float _scale, float _yaw)
{
	auto world = m_wpWorld.lock();
	if (!world) return nullptr;

	auto mushroom = std::make_shared<Mushroom>(m_cm, world);
	mushroom->SetTuning(m_mushroomTuning);
	mushroom->Init();

	mushroom->SetPosition(_pos);
	mushroom->SetYaw(_yaw);

	// めり込ませ量を決めてから大きさ。どちらも中で接地し直す。
	mushroom->SetGroundOffset(_entry.groundOffset);
	mushroom->SetBodyScale(_scale);

	mushroom->SetLightPower(_entry.lightPower);
	mushroom->SetLightRange(_entry.lightRange);

	return mushroom;
}

void MushroomManager::SpawnCluster(const SpawnEntry& _entry,
	std::vector<std::shared_ptr<Mushroom>>& _out, const char* _name)
{
	Lcg rng(_entry.seed);

	const int count = std::clamp(_entry.count, 0, m_tuning.maxCount);

	for (int i = 0; i < count; ++i)
	{
		// 面積で一様に散らす。半径を直接乱数にすると中心に寄って団子になる。
		const float angle = rng.NextFloat(0.0f, DirectX::XM_2PI);
		const float radius = _entry.range * std::sqrt(rng.NextFloat(0.0f, 1.0f));

		const Math::Vector3 pos = _entry.anchor
			+ Math::Vector3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);

		const float scale = _entry.scale * rng.NextFloat(m_tuning.scaleJitterMin, m_tuning.scaleJitterMax);
		const float yaw = rng.NextFloat(0.0f, DirectX::XM_2PI);

		// Y はここでは決めない。Mushroom が散らした後の位置で地面を拾う。
		auto mushroom = MakeMushroom(_entry, pos, scale, yaw);
		if (!mushroom) return;

		_out.push_back(mushroom);
		Game::SetEntityName(m_cm, mushroom->GetEntity(),
			_name, static_cast<int>(_out.size()));
	}
}

void MushroomManager::SpawnAll()
{
	Clear();

	for (const auto& entry : m_spawnEntries)
		SpawnCluster(entry, m_spMushrooms, "Mushroom");
}

void MushroomManager::Clear()
{
	m_spMushrooms.clear();
}

// ===========================================================
// 配置データの入出力
// ===========================================================
void MushroomManager::SaveSpawns() const
{
	nlohmann::json json = nlohmann::json::array();

	for (const auto& entry : m_spawnEntries)
	{
		json.push_back({
			{ "pos",    { entry.anchor.x, entry.anchor.y, entry.anchor.z } },
			{ "range",  entry.range },
			{ "count",  entry.count },
			{ "scale",  entry.scale },
			{ "seed",   entry.seed  },
			{ "power",  entry.lightPower },
			{ "light",  entry.lightRange },
			{ "offset", entry.groundOffset },
			});
	}

	std::ofstream ofs(m_tuning.spawnFile);
	if (!ofs.is_open()) return;

	ofs << json.dump(kJsonIndent);
}

void MushroomManager::LoadSpawns()
{
	m_spawnEntries.clear();

	std::ifstream ifs(m_tuning.spawnFile);
	if (!ifs.is_open()) return;

	nlohmann::json json;
	try
	{
		ifs >> json;
	}
	catch (const nlohmann::json::exception&)
	{
		return;	// 壊れたJSONは無視する
	}

	if (!json.is_array()) return;

	for (const auto& item : json)
	{
		// 手編集や仕様変更でおかしな要素が混ざっていても、その1件だけ捨てて続行する。
		try
		{
			if (!item.is_object()) continue;

			const SpawnEntry def = MakeDefaultEntry();
			SpawnEntry entry = def;

			const auto pos = item.value("pos", std::vector<float>{ 0.0f, 0.0f, 0.0f });
			if (pos.size() == 3) entry.anchor = Math::Vector3(pos[0], pos[1], pos[2]);

			entry.range = item.value("range", def.range);
			entry.count = std::clamp(item.value("count", def.count), 0, m_tuning.maxCount);
			entry.scale = item.value("scale", def.scale);
			entry.seed = item.value("seed", def.seed);

			// 旧データには無いので、欠けていれば既定値を使う
			entry.lightPower = item.value("power", def.lightPower);
			entry.lightRange = item.value("light", def.lightRange);
			entry.groundOffset = item.value("offset", def.groundOffset);

			m_spawnEntries.push_back(entry);
		}
		catch (const nlohmann::json::exception&)
		{
			continue;
		}
	}
}

// ===========================================================
// 配置ビューア
// ===========================================================
void MushroomManager::DrawDebugImGui()
{
	if (!ImGui::Begin("Mushroom"))
	{
		ImGui::End();
		return;
	}

	int total = 0;
	for (const auto& entry : m_spawnEntries) total += entry.count;

	ImGui::Text(U8("群生 %d 箇所 / 本数 %d (現在 %d 本)"),
		static_cast<int>(m_spawnEntries.size()), total, GetCount());

	if (ImGui::Button(U8("配置をすべて生やし直す"))) SpawnAll();
	ImGui::SameLine();
	if (ImGui::Button(U8("全部消す"))) Clear();

	ImGui::Separator();

	DrawDebugPlacer();

	ImGui::End();
}

void MushroomManager::DrawDebugPlacer()
{
	if (!ImGui::CollapsingHeader(U8("配置")))
	{
		// 畳んだらプレビューを片付ける。放置すると通常のプレイに紛れ込む。
		ClearPreview();
		return;
	}

	//------------------------------------------
	// 配置地点
	//------------------------------------------
	// 対象が設定されていなければボタンを押しても何もしない(空のエンティティを読まない)
	if (ImGui::Button(U8("プレイヤー位置を取得")) && m_cm.has<TransformComponent>(m_targetEntity))
	{
		m_edit.anchor = m_cm.get<TransformComponent>(m_targetEntity).GetPosition() * kWorldToPlacement;
		m_previewDirty = true;
	}

	ImGui::Text("Anchor  %.1f, %.1f, %.1f",
		m_edit.anchor.x, m_edit.anchor.y, m_edit.anchor.z);

	if (ImGui::DragFloat3(U8("手動調整##MushroomAnchor"), &m_edit.anchor.x, kUiAnchorStep))
		m_previewDirty = true;

	//------------------------------------------
	// 広さと本数
	//------------------------------------------
	if (ImGui::SliderFloat(U8("範囲##MushroomRange"), &m_edit.range, kUiRangeMin, kUiRangeMax, "%.1f m"))
		m_previewDirty = true;

	if (ImGui::SliderInt(U8("本数##MushroomCount"), &m_edit.count, 1, m_tuning.maxCount))
		m_previewDirty = true;

	if (ImGui::SliderFloat(U8("大きさ##MushroomScale"), &m_edit.scale, kUiScaleMin, kUiScaleMax, "x%.2f"))
		m_previewDirty = true;

	ImGui::Text("Seed  %u", m_edit.seed);
	ImGui::SameLine();
	if (ImGui::Button(U8("散らばりを引き直す")))
	{
		m_edit.seed = MakeSeed();
		m_previewDirty = true;
	}

	//------------------------------------------
	// 光り方(作り直さずにプレビューへ反映する)
	//------------------------------------------
	ImGui::Separator();

	bool lightChanged = false;

	lightChanged |= ImGui::SliderFloat(U8("明るさ##MushroomPower"),
		&m_edit.lightPower, kUiPowerMin, kUiPowerMax, "%.2f");

	lightChanged |= ImGui::SliderFloat(U8("光の届く距離##MushroomLightRange"),
		&m_edit.lightRange, kUiLightRangeMin, kUiLightRangeMax, "%.1f m");

	lightChanged |= ImGui::SliderFloat(U8("埋め込み##MushroomOffset"),
		&m_edit.groundOffset, kUiOffsetMin, kUiOffsetMax, "%.2f m");

	if (lightChanged) ApplyLightToPreview();

	//------------------------------------------
	// プレビュー
	//------------------------------------------
	if (ImGui::Checkbox(U8("プレビュー"), &m_previewEnabled)) m_previewDirty = true;

	if (m_previewEnabled) UpdatePreview();
	else                  ClearPreview();

	//------------------------------------------
	// 保存
	//------------------------------------------
	ImGui::Separator();

	if (ImGui::Button(U8("この配置を保存")))
	{
		m_spawnEntries.push_back(m_edit);
		SaveSpawns();

		// 保存したぶんは本番として生やし、プレビューは引き継がない
		SpawnCluster(m_edit, m_spMushrooms, "Mushroom");
		ClearPreview();

		// 次の群生が同じ形にならないように種だけ送る
		m_edit.seed = MakeSeed();
	}

	ImGui::Text(U8("保存済み配置 (%d)"), static_cast<int>(m_spawnEntries.size()));

	int eraseIndex = -1;
	for (int i = 0; i < static_cast<int>(m_spawnEntries.size()); ++i)
	{
		ImGui::PushID(i);

		const auto& entry = m_spawnEntries[i];
		ImGui::Text("(%.1f, %.1f, %.1f)  r=%.1f  x%d  s=%.2f  p=%.2f",
			entry.anchor.x, entry.anchor.y, entry.anchor.z,
			entry.range, entry.count, entry.scale, entry.lightPower);

		ImGui::SameLine();
		if (ImGui::Button(U8("編集へ")))
		{
			m_edit = entry;
			m_previewDirty = true;
		}

		ImGui::SameLine();
		if (ImGui::Button(U8("削除"))) eraseIndex = i;

		ImGui::PopID();
	}

	if (eraseIndex >= 0)
	{
		m_spawnEntries.erase(m_spawnEntries.begin() + eraseIndex);
		SaveSpawns();

		// 消した1件だけを取り除く手段が無いので、まとめて生やし直す
		SpawnAll();
	}
}

void MushroomManager::UpdatePreview()
{
	if (!m_previewDirty) return;
	m_previewDirty = false;

	ClearPreview();

	SpawnCluster(m_edit, m_spPreview, "MushroomPreview");
}

void MushroomManager::ApplyLightToPreview()
{
	for (auto& mushroom : m_spPreview)
	{
		mushroom->SetLightPower(m_edit.lightPower);
		mushroom->SetLightRange(m_edit.lightRange);
		mushroom->SetGroundOffset(m_edit.groundOffset);	// 中で接地し直す
	}
}

void MushroomManager::ClearPreview()
{
	m_spPreview.clear();
}