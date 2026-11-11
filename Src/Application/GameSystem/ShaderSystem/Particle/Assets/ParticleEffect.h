#pragma once
//------------------------------------------------------------------------------
// ParticleEffect — .particle.json のルート
//
//   ParticleEffect
//    └ layers[]              複数エミッタ
//        ├ name / enabled
//        ├ offset            エフェクト原点からの相対位置
//        ├ asset             ParticleAsset（既存の全パラメータ）
//        ├ bursts[]          { 時刻, 個数 }
//        └ path[]            { 時刻, 位置 }
//
// 旧形式（params がルートにある1エミッタ形式）もそのまま読める。
// その場合は1レイヤーのエフェクトとして解釈する。
//------------------------------------------------------------------------------
#include "ParticleAsset.h"

// 時刻と個数の組
struct BurstEntry
{
	float time = 0.0f;    // レイヤー開始からの秒数
	int   count = 10;     // 一度に出す個数
};

// エミッタ自体の移動パスのキー
struct PathKey
{
	float time = 0.0f;
	DirectX::XMFLOAT3 pos = { 0.0f, 0.0f, 0.0f };
};

// エフェクトを構成する1レイヤー（＝1エミッタ）
struct ParticleLayer
{
	std::string name = "layer";
	bool enabled = true;
	DirectX::XMFLOAT3 offset = { 0.0f, 0.0f, 0.0f };

	ParticleAsset asset;
	std::vector<BurstEntry> bursts;   // 時刻の昇順を保つこと
	std::vector<PathKey>    path;     // 時刻の昇順を保つこと
};

struct ParticleEffect
{
	std::string name = "effect";
	std::vector<ParticleLayer> layers;
	DirectX::XMFLOAT3 angle = { 0.0f, 0.0f, 0.0f };

	bool LoadFromFile(const std::string& path);
	bool LoadFromString(const std::string& jsonStr);
	std::string ToJsonString() const;

	// 完全に消えるまでの目安（ワンショットの回収に使う）
	float TotalDuration() const;

	// パスを線形補間。キーが無ければ {0,0,0}
	static DirectX::XMFLOAT3 EvalPath(const std::vector<PathKey>& keys, float t);

	// bursts / path を時刻の昇順に並べ直す（編集後に呼ぶ）
	void SortKeys();
};