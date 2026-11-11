#pragma once
//==============================================================================
// TrailAsset — トレイル（帯）の見た目パラメータ
//==============================================================================

struct TrailAsset
{
	std::string name = "trail";

	// ---- 基本 ----
	float lifetime = 0.35f;      // ノードが消えるまでの秒数。長いほど帯が伸びる
	float minDistance = 0.02f;   // これ未満しか動いていなければノードを足さない
	int   maxNodes = 64;         // 保持するノード数の上限
	int   smoothing = 2;         // 0=直線 1〜3=Catmull-Rom で細分化して滑らかにする

	// ---- 幅 ----
	//  根元と先端の位置から幅は決まるが、その中心を基準に伸縮させる
	float widthScale = 1.0f;

	// ---- カーブ（幅・アルファ）----
	static const int kMaxCurvePoints = 16;
	// 帯に沿ったサンプル（0=最新 1=最古）。実際に使うのは先頭 curvePointCount 個だけ。
	// 既存アセット（8点のみ記録されたJSON）はそのまま8点として読める。
	float widthCurve[kMaxCurvePoints] = {
		1.0f, 1.0f, 0.95f, 0.85f, 0.7f, 0.5f, 0.3f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	float alphaCurve[kMaxCurvePoints] = {
		1.0f, 1.0f, 0.9f, 0.75f, 0.55f, 0.35f, 0.15f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	int   curvePointCount = 8;   // 2〜kMaxCurvePoints。既存アセットは8のまま動く
	int   curveInterp = 0;       // 0=Linear（従来どおり） 1=Smooth（Catmull-Rom）

	// ---- 色 ----
	int   colorKeyCount = 2;
	// rgb + 位置(0〜1) を4キー分
	float colorKeys[16] = {
		1.0f, 1.0f, 1.0f, 0.0f,
		0.5f, 0.7f, 1.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f,
		0.0f, 0.0f, 0.0f, 1.0f };
	// 帯の1点ごとに色をランダムにずらす（0で従来どおり無効）
	float hueJitter = 0.0f, satJitter = 0.0f, valJitter = 0.0f;

	float intensity = 1.0f;      // 全体の明るさ（HDR前提なので1超えも可）

	// ---- 質感（以前は TrailRenderer.cpp のシェーダ内に固定値だった6定数）----
	float fadePow = 1.5f;        // 大きいほど先端が早く消える
	float coreWidth = 0.35f;     // 芯の幅 0..1（幅方向）
	float coreWhite = 0.6f;      // 芯の白熱化度 0..1
	float coreBoost = 4.0f;      // 芯のHDR倍率
	float rootBoost = 2.0f;      // 根本の明るさ倍率
	float depthFade = 0.5f;      // 深度遮蔽のフェード距離

	// ---- ノイズ変位（面白い機能2。粒子CSの CurlNoise をCPU移植して流用）----
	float noiseStrength = 0.0f;  // 0で従来どおり無効。帯の中心線を暴れさせる強さ
	float noiseScale = 0.5f;     // ノイズの細かさ（大きいほど細かく波打つ）
	float noiseSpeed = 1.0f;     // 時間に対する流れの速さ

	// ---- 分岐トレイル（面白い機能3。稲妻のように細い筋が枝分かれする）----
	int   branchOn = 0;          // 1で有効
	float branchChance = 0.15f;  // ノードごとに分岐が生える確率 0〜1
	float branchLength = 0.3f;   // 分岐1本の長さ
	float branchWidth = 0.05f;   // 分岐の付け根の幅
	int   branchSegments = 3;    // 分岐のジグザグ分割数

	// ---- ノイズ拡張（フラクタル化・UV歪み・ディゾルブ・エッジのみ）----
	int   noiseOctaves = 1;           // 1で従来どおり単層。2以上でCurlNoiseを重ねて複雑な乱流にする
	float uvNoiseStrength = 0.0f;     // 0で無効。UVをノイズで歪ませる
	float uvNoiseScale = 3.0f;
	float uvNoiseSpeed = 1.0f;
	float dissolveAmount = 0.0f;      // 0で無効。ノイズでジリジリと不規則にちぎれるように消える
	float dissolveNoiseScale = 4.0f;
	float dissolveEdgeWidth = 0.15f;
	float edgeNoiseStrength = 0.0f;   // 0で無効。輪郭線だけをノイズで揺らす
	float edgeNoiseScale = 5.0f;

	// ---- 色拡張（時間巡回・速度連動・経年劣化・温度）----
	float hueSpeed = 0.0f;            // 0で無効。色相を時間で回転させる
	float speedColorAmount = 0.0f;    // 0で無効。速く動くほどspeedColorHotに寄せる
	float speedColorMax = 3.0f;
	DirectX::XMFLOAT3 speedColorHot = { 1.0f, 1.0f, 1.0f };
	float ageDesaturate = 0.0f;       // 0で無効。古い点ほど彩度を落とす
	float tempAmount = 0.0f;          // 0で無効。先端=tempHotColor 根本=tempCoolColor
	DirectX::XMFLOAT3 tempHotColor = { 1.0f, 0.9f, 0.6f };
	DirectX::XMFLOAT3 tempCoolColor = { 0.3f, 0.4f, 1.0f };

	// ---- パターン（パルス波・ドット/破線・疑似鏡面）----
	float pulseAmp = 0.0f;            // 0で無効
	float pulseSpeed = 4.0f;
	float pulseWidth = 0.2f;
	float dashDuty = 1.0f;            // 1.0で無効（隙間なし）
	float dashFreq = 8.0f;
	float specBoost = 0.0f;           // 0で無効
	float specWidth = 0.08f;
	float specSpeed = 1.5f;

	// ---- 特殊質感（セル状パターン=ひび割れ/結晶/溶岩共通・織り目・電撃・群衆）----
	float cellAmount = 0.0f;          // 0で無効。ボロノイ状のセルパターン
	float cellScale = 6.0f;
	float cellEdgeWidth = 0.08f;
	float cellGlow = 1.5f;
	float weaveStrength = 0.0f;       // 0で無効。横方向の織り目パターン
	float weaveScale = 10.0f;
	float electricChance = 0.0f;      // 0で無効。ノードごとに間引いて稲妻の走り方を再現
	float electricScale = 3.0f;
	int   crowdCount = 1;             // 1で無効（複製なし）
	float crowdSpacing = 0.15f;
	float crowdJitter = 0.0f;

	// ---- トレイル→パーティクル発生（面白い機能10。GPUParticleSystemと連携し、軌道上に火花を自動発生）----
	//   ★見た目（頂点データ）には一切影響しない、本番側RenderHelperの挙動フック用パラメータ。
	//   なのでTrailRenderer::Asset（描画用）とは違い、EditorTrailRenderer::Asset側には複製しない
	//   （エディタプレビューは絵作りだけなので、GPUParticleSystemへの発生は行わない）。
	int         particleOn = 0;        // 1で有効
	std::string particleEffect;        // GPUParticleSystem::PlayEffectに渡すエフェクト名（LoadEffectFolder等で登録済みのもの）
	float       particleChance = 0.3f; // ノードが1つ打たれるたびに発生する確率 0〜1

	// ---- 見た目 ----
	int   additive = 1;          // 1=加算 0=アルファ
	int   textureIndex = -1;     // -1 = 手続き（テクスチャを使わない）
	float edgeSoft = 0.25f;      // 幅方向のぼかし。0で硬い縁、1で光の帯
	float edgeCore = 0.0f;       // 中心線を明るくする
	int   uvMode = 0;            // 0=帯全体に引き伸ばす 1=長さに応じてタイル
	float uvTiling = 1.0f;
	float uvScroll = 0.0f;       // UVを流す速さ

	bool LoadFromFile(const std::string& path);
	bool LoadFromString(const std::string& jsonStr);
	std::string ToJsonString() const;

	// 帯に沿った位置 t(0〜1) の値
	float  EvalWidth(float t) const;
	float  EvalAlpha(float t) const;
	// seed を変えると hueJitter/satJitter/valJitter ぶんだけ色がずれる。
	// seed=0 かつジッター全て0なら従来と完全に同じ結果になる。
	void   EvalColor(float t, float outRGB[3], float seed = 0.0f) const;
};

//==============================================================================
// カーブプリセット
//   widthCurve/alphaCurve をワンクリックで差し替えるための既定形。
//   curvePointCount=8 / 先頭8点のみを対象にしている
//   （プリセット適用後、9点目以降は0クリアされ curvePointCount も8に戻る）。
//==============================================================================
struct TrailCurvePreset
{
	const char* name;
	float width[8];
	float alpha[8];
};

extern const TrailCurvePreset kTrailCurvePresets[];
extern const int kTrailCurvePresetCount;

// asset.widthCurve/alphaCurve の先頭8点・curvePointCount(=8)を差し替える。
// curveInterp や質感パラメータ等、カーブ以外は変更しない。
void ApplyTrailCurvePreset(TrailAsset& asset, int presetIndex);