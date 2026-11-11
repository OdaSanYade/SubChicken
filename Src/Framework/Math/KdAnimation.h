#pragma once

// アニメーションキー(クォータニオン
struct KdAnimKeyQuaternion
{
	float				m_time = 0;		// 時間
	Math::Quaternion	m_quat;			// クォータニオンデータ
};

// アニメーションキー(ベクトル
struct KdAnimKeyVector3
{
	float				m_time = 0;		// 時間
	Math::Vector3		m_vec;			// 3Dベクトルデータ
};

// アニメーションのサンプリング結果（行列合成前のSRT）
struct KdAnimNodePose
{
	Math::Vector3    m_scale = { 1, 1, 1 };
	Math::Quaternion m_rotation = Math::Quaternion::Identity;
	Math::Vector3    m_translation = { 0, 0, 0 };
	bool             m_enable = false;
};

struct KdAnimEvent
{
	float       m_time = 0;
	std::string m_name;
};

//============================
// アニメーションデータ
//============================
struct KdAnimationData
{
	// アニメーション名
	std::string		m_name;
	// アニメの長さ
	float			m_maxLength = 0;

	// １ノードのアニメーションデータ
	struct Node
	{
		int			m_nodeOffset = -1;	// 対象モデルノードのOffset値

		// 各チャンネル
		std::vector<KdAnimKeyVector3>		m_translations;	// 位置キーリスト
		std::vector<KdAnimKeyQuaternion>	m_rotations;	// 回転キーリスト
		std::vector<KdAnimKeyVector3>		m_scales;		// 拡縮キーリスト

		void Interpolate(Math::Matrix& rDst, float time);
		bool InterpolateTranslations(Math::Vector3& result, float time);
		bool InterpolateRotations(Math::Quaternion& result, float time);
		bool InterpolateScales(Math::Vector3& result, float time);
		bool Evaluate(KdAnimNodePose& rPose, float time);
	};

	// 全ノード用アニメーションデータ
	std::vector<Node>	m_nodes;
	std::vector<KdAnimEvent> m_events;
};

// アニメーションデータが何fpsで作成されている前提かの基準値
// (DeltaTime を「フレーム数」に変換するために使用。m_time等の単位はこれまで通り"フレーム"のまま)
static constexpr float kAnimBaseFPS = 60.0f;

// 指定ノード(ボーン)のワールド行列を取得する
bool GetBoneWorldMatrix(const std::vector<KdModelWork::Node>& rNodes, int nodeIndex, Math::Matrix& rDst);

// アイテム等をボーンに追従させるためのアタッチ情報
struct KdBoneAttachment
{
	int         m_nodeIndex = -1;                 // アタッチ先ノードIndex
	Math::Matrix m_offset = Math::Matrix::Identity; // ボーンからのローカルオフセット(位置・回転調整用)

	bool IsValid() const { return m_nodeIndex >= 0; }

	// アタッチ先の現在のワールド行列(オフセット込み)を取得
	bool GetWorldMatrix(const std::vector<KdModelWork::Node>& rNodes, Math::Matrix& rDst) const
	{
		Math::Matrix boneMat;
		if (!GetBoneWorldMatrix(rNodes, m_nodeIndex, boneMat)) { return false; }
		rDst = m_offset * boneMat;
		return true;
	}
};

class KdAnimator
{
public:

	inline void SetAnimation(const std::shared_ptr<KdAnimationData>& rData, bool isLoop = true)
	{
		m_spAnimation = rData;
		m_isLoop = isLoop;

		m_time = 0.0f;
		m_isEnd = false;

		m_blendDuration = 0.0f;
		m_blendTime = 0.0f;
		m_needSnapshot = false;
	}

	void ChangeAnimation(const std::shared_ptr<KdAnimationData>& rData,
		float blendFrame = 10.0f, bool isLoop = true);

	// アニメーションが終了してる？（ループ中は常にfalse）
	bool IsAnimationEnd() const
	{
		if (m_spAnimation == nullptr) { return true; }
		if (m_isLoop) { return false; }
		if (m_spAnimation->m_maxLength <= 0.0f) { return true; }

		return m_isEnd;
	}

	// アニメーションの更新
	// speed … 再生速度倍率(1.0 = 等倍)。内部で Time::Instance().GetDeltaTime() を使い
	//         フレームレートに依存しない進行に変換する
	void AdvanceTime(std::vector<KdModelWork::Node>& rNodes, float speed = 1.0f);

	std::function<void(const std::string&)> m_onEvent = nullptr;

	// =======================================================
	// ルートモーション
	// 指定ノード(例:Hip)の移動をメッシュから引き剥がし、
	// 呼び出し側(キャラの実座標)に移動量として渡す機能。
	// ロックした軸はメッシュ上では常に基準位置(0)のまま動かなくなる。
	// 例: EnableRootMotion(hipOffset, true, false, true) … 水平(XZ)だけ実移動へ回し、上下動(Y)はそのままメッシュに残す
	// =======================================================
	void EnableRootMotion(int rootNodeOffset, bool lockX = true, bool lockY = false, bool lockZ = true)
	{
		m_rootNodeOffset = rootNodeOffset;
		m_rootLockX = lockX;
		m_rootLockY = lockY;
		m_rootLockZ = lockZ;
		m_hasRootSample = false;
	}
	void DisableRootMotion() { m_rootNodeOffset = -1; m_hasRootSample = false; }

	// このフレーム分のルートモーション移動量を取り出す(呼ぶと内部は0にリセットされる)
	// 呼び出し側で TransformComponent::Translate() 等に渡して実座標へ反映する
	Math::Vector3 ConsumeRootMotion()
	{
		Math::Vector3 v = m_rootMotionDelta;
		m_rootMotionDelta = Math::Vector3::Zero;
		return v;
	}

	// ブレンドの補間を線形ではなくイージング(EaseInOut/smoothstep)にするか
	void SetBlendEasing(bool enable) { m_useBlendEasing = enable; }

private:

	// 指定時刻のポーズをノードへ書き込む
	// blendRate … 1.0f以上でブレンド無し
	void ApplyPose(std::vector<KdModelWork::Node>& rNodes, float time, float blendRate);

	// ルートノードのローカル位置をサンプリングする(ルートモーション用)
	bool SampleRootLocalPos(float time, Math::Vector3& rOut) const;

	// prevTime→newTime の間に発生したルートモーション移動量を加算する
	void UpdateRootMotion(float prevTime, float newTime, bool looped, float maxLength);

	// events配列を毎フレーム見て、区間に入ったものだけ発火する。
	// m_eventFiredMask で「今回の周回で既に発火したIndex」を管理し、
	// 何らかの理由で同じ区間が2回評価されても同一イベントが二重発火しないようにする。
	void FireEvents(float prevTime, float newTime, bool looped, bool isEnd);

	std::shared_ptr<KdAnimationData>	m_spAnimation = nullptr;	// 再生するアニメーションデータ

	float m_time = 0.0f;

	bool m_isLoop = false;
	bool m_isEnd = false;   // 非ループ再生が終端に到達したか

	float m_blendTime = 0.0f;   // ブレンド経過
	float m_blendDuration = 0.0f;   // ブレンドにかける時間（m_timeと同じ単位＝フレーム）
	bool  m_needSnapshot = false;  // 次のAdvanceTimeで現在ポーズを記録するか
	bool  m_useBlendEasing = true;  // blendRateをEaseInOut(smoothstep)にするか
	std::vector<KdAnimNodePose> m_blendSrcPose; // 切替直前ポーズ（モデルノード数分）

	// ---- イベント二重発火防止 ----
	// index = m_spAnimation->m_events の要素Index。ChangeAnimation/SetAnimation・ループ時にクリアする
	std::vector<bool> m_eventFiredMask;

	// ---- ルートモーション ----
	int   m_rootNodeOffset = -1;	// -1で無効
	bool  m_rootLockX = true;
	bool  m_rootLockY = false;
	bool  m_rootLockZ = true;
	bool  m_hasRootSample = false;			// m_rootPrevLocalPos が有効か
	Math::Vector3 m_rootPrevLocalPos = Math::Vector3::Zero;
	Math::Vector3 m_rootMotionDelta = Math::Vector3::Zero;	// 未取得(Consume前)の蓄積移動量
};