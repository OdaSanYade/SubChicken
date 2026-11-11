#pragma once
#include "../../../IObject.h"
#include "Mushroom.h"	// MushroomTuning

// 配置の既定値と、散らし方(StageTuning.json の "MushroomManager" 区画)
struct MushroomManagerTuning
{
	float defaultRange = 6.0f;			// 群生の既定の広さ
	int   defaultCount = 12;			// 群生の既定の本数
	int   maxCount = 64;				// 1箇所あたりの上限
	float defaultLightPower = 1.0f;
	float defaultLightRange = 6.0f;
	float defaultGroundOffset = -0.05_m;

	// 1本ずつの大きさのばらつき(群生の基準値に掛ける倍率の範囲)
	float scaleJitterMin = 0.75f;
	float scaleJitterMax = 1.35f;

	// 保存先
	std::string spawnFile = "Asset/Data/MushroomSpawns.json";

	template<class V> void Visit(V& v)
	{
		v("defaultRange", defaultRange);
		v("defaultCount", defaultCount);
		v("maxCount", maxCount);
		v("defaultLightPower", defaultLightPower);
		v("defaultLightRange", defaultLightRange);
		v("defaultGroundOffset", defaultGroundOffset, Tuning::Unit::Meter);
		v("scaleJitterMin", scaleJitterMin);
		v("scaleJitterMax", scaleJitterMax);
		v("spawnFile", spawnFile);
	}
};

// 光るキノコの配置管理。
// 当たり判定は持たない装飾物。接地は Mushroom 側が自分で取る。
class MushroomManager : public IObject
{
public:

	using IObject::IObject;

	MushroomManager() = default;
	~MushroomManager() = default;

	void Init() override;
	void Update() override;

	std::string GetID() override { return "MushroomManager"; }

	// 配置UIの「プレイヤー位置を取得」で使う
	void SetTarget(const kd::ecs::Entity& _target) { m_targetEntity = _target; }

	// 保存済みの配置をすべて生やす。シーン開始時に1度だけ呼ぶ。
	void SpawnAll();
	void Clear();

	int GetCount() const { return static_cast<int>(m_spMushrooms.size()); }

	// 配置ビューア。毎フレーム好きな場所から呼ぶ。
	void DrawDebugImGui();

private:

	//============================================================
	// 配置データ
	//============================================================
	// 保存の1件は「キノコ1本」ではなく「群生1箇所」。
	// 1本ずつ持つと数百件になって、置くのも読むのも現実的でなくなる。
	// 既定値は MakeDefaultEntry() で調整値から作る
	struct SpawnEntry
	{
		Math::Vector3 anchor = Math::Vector3::Zero;	// 群生の中心
		float         range = 0.0f;					// 散らばる半径
		int           count = 0;					// 本数
		float         scale = 1.0f;					// 大きさの基準値
		unsigned int  seed = 1u;					// 散らばりの再現用

		//------------------------------------------
		// ライト
		//------------------------------------------
		float lightPower = 0.0f;	// 明るさ
		float lightRange = 0.0f;	// 届く距離
		float groundOffset = 0.0f;	// 地面へのめり込ませ量
	};

	SpawnEntry MakeDefaultEntry() const;

	// 1箇所ぶんを生やす。seed から散らばりを作るので毎回同じ形になる。
	void SpawnCluster(const SpawnEntry& _entry,
		std::vector<std::shared_ptr<Mushroom>>& _out, const char* _name);

	// 1本ぶん生やす。位置・大きさ・向き・光り方は呼び出し側が決める。
	std::shared_ptr<Mushroom> MakeMushroom(const SpawnEntry& _entry,
		const Math::Vector3& _pos, float _scale, float _yaw);

	void SaveSpawns() const;
	void LoadSpawns();

	//============================================================
	// 配置UI
	//============================================================
	void DrawDebugPlacer();

	// 編集中の値に追従するプレビュー。確定前の見た目を確かめる用。
	void UpdatePreview();
	void ClearPreview();
	// 作り直さずに済む値(明るさ・接地)だけをプレビューへ流す。
	void ApplyLightToPreview();

	kd::ecs::Entity m_targetEntity;

	std::vector<std::shared_ptr<Mushroom>> m_spMushrooms;
	std::vector<std::shared_ptr<Mushroom>> m_spPreview;	// プレビュー専用(本体リストには混ぜない)

	std::vector<SpawnEntry> m_spawnEntries;

	//------------------------------------------
	// 編集中の値
	//------------------------------------------
	SpawnEntry m_edit;
	bool       m_previewEnabled = false;
	bool       m_previewDirty = true;	// 配置が変わった時だけ作り直す

	//------------------------------------------
	// 調整値
	//------------------------------------------
	MushroomManagerTuning m_tuning;
	MushroomTuning        m_mushroomTuning;	// 生やす全員に配る(1本ずつ読み直さない)
};