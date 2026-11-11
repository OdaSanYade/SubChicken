#pragma once

using TrailId = uint32_t;
static constexpr TrailId kInvalidTrailId = static_cast<TrailId>(-1);

constexpr TrailId MakeTrailId(std::string_view name)
{
	uint32_t h = 2166136261u;
	for (char c : name)
	{
		h ^= static_cast<uint32_t>(static_cast<unsigned char>(c));
		h *= 16777619u;
	}
	return (h == kInvalidTrailId) ? (h - 1u) : h;
}

// 幅の決め方
enum class TrailWidthMode : int
{
	Billboard = 0,   // 進行方向とカメラから幅方向を決める（従来）
	UpVector = 1,    // ups[i] を正規化して幅方向に使う。幅は width 固定
	RootTip = 2,     // ★ups[i] を「半幅ベクトル」として使う（正規化しない）
	//   root = points[i] - ups[i] / tip = points[i] + ups[i]
	//   幅がノードごとに変わるので、剣の根元と先端をそのまま渡せる
};

// 点をどこから取るか
enum class TrailSource : int
{
	Transform = 0,   // TransformComponent の位置から打つ（従来）
	External = 1,    // 外から与えられた根元/先端を打つ
};

struct TrailComponent
{
	static const int kMaxPoints = 32;

	// ---- 見た目（TrailRenderer が持つアセットを参照する）----
	TrailId trailId = kInvalidTrailId;   // 無効なら従来どおりの単色
	float   widthScale = 1.0f;           // アセットの幅への倍率

	// ---- 挙動（ここはコンポーネントが持つ）----
	int               widthMode = 0;        // TrailWidthMode
	float             width = 0.3f;         // 基準の幅
	float             lifeTime = 0.5f;      // 点の寿命（秒）
	float             minDist = 0.05f;      // 点を打つ最小移動距離
	DirectX::XMFLOAT4 color = { 1, 1, 1, 1 };
	bool              emitting = false;

	// ---- 内部状態 ----
	int           head = 0;
	int           count = 0;
	Math::Vector3 points[kMaxPoints] = {};
	// widthMode によって意味が変わる：
	//   Billboard  未使用
	//   UpVector   幅方向（正規化して使う）
	//   RootTip    半幅ベクトル（正規化しない）
	Math::Vector3 ups[kMaxPoints] = {};
	float         ages[kMaxPoints] = {};
	Math::Vector3 lastPos = {};
	bool          hasLast = false;

	// ---- 外部から与える点（source = External のとき）----
	int           source = 0;                  // TrailSource
	Math::Vector3 emitRoot = {};
	Math::Vector3 emitTip = {};
	bool          emitDirty = false;           // このフレームに新しい値が入った

	// 見た目アセットの指定。呼ぶ側はこれだけ知っていればよい。
	void SetAsset(std::string_view name) { trailId = MakeTrailId(name); }

	// 根元と先端を与える。次の更新で1点打たれる。
	// 剣や脚のように「線分が動く」ものは、位置ではなくこの形で渡す。
	void Emit(const Math::Vector3& root, const Math::Vector3& tip)
	{
		emitRoot = root;
		emitTip = tip;
		emitDirty = true;
	}

	// 溜まった帯を捨てる。攻撃の段が変わるときに使う。
	void ResetPoints() { head = 0; count = 0; hasLast = false; emitDirty = false; }
};

static_assert(std::is_trivially_copyable_v<TrailComponent>,
	"TrailComponent must be trivially copyable");