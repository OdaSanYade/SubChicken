#include "KdAnimation.h"

bool GetBoneWorldMatrix(const std::vector<KdModelWork::Node>& rNodes, int nodeIndex, Math::Matrix& rDst)
{
	if (nodeIndex < 0 || nodeIndex >= (int)rNodes.size()) { return false; }
	rDst = rNodes[nodeIndex].m_worldTransform;
	return true;
}

template<class T>
int BinarySearchNextAnimKey(const std::vector<T>& list, float time)
{
	int low = 0;
	int high = (int)list.size();
	while (low < high)
	{
		int mid = (low + high) / 2;
		float midTime = list[mid].m_time;

		if (midTime <= time) low = mid + 1;
		else high = mid;
	}
	return low;
}

bool KdAnimationData::Node::InterpolateTranslations(Math::Vector3& result, float time)
{
	if (m_translations.size() == 0)return false;

	// キー位置検索
	UINT keyIdx = BinarySearchNextAnimKey(m_translations, time);

	// 先頭のキーなら、先頭のデータを返す
	if (keyIdx == 0) {
		result = m_translations.front().m_vec;
		return true;
	}
	// 配列外のキーなら、最後のデータを返す
	else if (keyIdx >= m_translations.size()) {
		result = m_translations.back().m_vec;
		return true;
	}
	// それ以外(中間の時間)なら、その時間の値を補間計算で求める
	else {
		auto& prev = m_translations[keyIdx - 1];	// 前のキー
		auto& next = m_translations[keyIdx];		// 次のキー
		// 前のキーと次のキーの時間から、0～1間の時間を求める
		float f = (time - prev.m_time) / (next.m_time - prev.m_time);
		// 補間
		result = DirectX::XMVectorLerp(
			prev.m_vec,
			next.m_vec,
			f
		);
	}

	return true;
}

bool KdAnimationData::Node::InterpolateRotations(Math::Quaternion& result, float time)
{
	if (m_rotations.size() == 0)return false;

	// キー位置検索
	UINT keyIdx = BinarySearchNextAnimKey(m_rotations, time);
	// 先頭のキーなら、先頭のデータを返す
	if (keyIdx == 0) {
		result = m_rotations.front().m_quat;
	}
	// 配列外のキーなら、最後のデータを返す
	else if (keyIdx >= m_rotations.size()) {
		result = m_rotations.back().m_quat;
	}
	// それ以外(中間の時間)なら、その時間の値を補間計算で求める
	else {
		auto& prev = m_rotations[keyIdx - 1];	// 前のキー
		auto& next = m_rotations[keyIdx];		// 次のキー
		// 前のキーと次のキーの時間から、0～1間の時間を求める
		float f = (time - prev.m_time) / (next.m_time - prev.m_time);
		// 補間
		result = DirectX::XMQuaternionSlerp(
			prev.m_quat,
			next.m_quat,
			f
		);
	}

	return true;
}

bool KdAnimationData::Node::InterpolateScales(Math::Vector3& result, float time)
{
	if (m_scales.size() == 0)return false;

	// キー位置検索
	UINT keyIdx = BinarySearchNextAnimKey(m_scales, time);

	// 先頭のキーなら、先頭のデータを返す
	if (keyIdx == 0) {
		result = m_scales.front().m_vec;
		return true;
	}
	// 配列外のキーなら、最後のデータを返す
	else if (keyIdx >= m_scales.size()) {
		result = m_scales.back().m_vec;
		return true;
	}
	// それ以外(中間の時間)なら、その時間の値を補間計算で求める
	else {
		auto& prev = m_scales[keyIdx - 1];	// 前のキー
		auto& next = m_scales[keyIdx];		// 次のキー
		// 前のキーと次のキーの時間から、0～1間の時間を求める
		float f = (time - prev.m_time) / (next.m_time - prev.m_time);
		// 補間
		result = DirectX::XMVectorLerp(
			prev.m_vec,
			next.m_vec,
			f
		);
	}

	return true;
}

void KdAnimationData::Node::Interpolate(Math::Matrix& rDst, float time)
{
	// ベクターによる拡縮補間
	bool isChange = false;
	Math::Matrix scale;
	Math::Vector3 resultVec;
	if (InterpolateScales(resultVec, time))
	{
		scale = scale.CreateScale(resultVec);
		isChange = true;
	}

	// クォタニオンによる回転補間
	Math::Matrix rotate;
	Math::Quaternion resultQuat;
	if (InterpolateRotations(resultQuat, time))
	{
		rotate = rotate.CreateFromQuaternion(resultQuat);
		isChange = true;
	}

	// ベクターによる座標補間
	Math::Matrix trans;
	if (InterpolateTranslations(resultVec, time))
	{
		trans = trans.CreateTranslation(resultVec);
		isChange = true;
	}

	if (isChange)
	{
		rDst = scale * rotate * trans;
	}
}

bool KdAnimationData::Node::Evaluate(KdAnimNodePose& rPose, float time)
{
	bool any = false;
	Math::Vector3 v;
	if (InterpolateScales(v, time)) { rPose.m_scale = v;       any = true; }
	Math::Quaternion q;
	if (InterpolateRotations(q, time)) { rPose.m_rotation = q;    any = true; }
	if (InterpolateTranslations(v, time)) { rPose.m_translation = v; any = true; }
	rPose.m_enable = any;
	return any;
}

void KdAnimator::ChangeAnimation(const std::shared_ptr<KdAnimationData>& rData,
	float blendFrame, bool isLoop)
{
	m_spAnimation = rData;
	m_isLoop = isLoop;
	m_time = 0.0f;
	m_isEnd = false;

	m_blendDuration = blendFrame;
	m_blendTime = 0.0f;
	m_needSnapshot = (blendFrame > 0.0f); // 次フレームで現在ポーズを記録

	// 新しい再生のため、イベントの発火済みフラグを作り直す
	m_eventFiredMask.assign(rData ? rData->m_events.size() : 0, false);

	// ルートモーションの基準サンプルも作り直す(切り替え直後の1フレーム目は差分を取らない)
	m_hasRootSample = false;
	m_rootMotionDelta = Math::Vector3::Zero;
}

void KdAnimator::ApplyPose(std::vector<KdModelWork::Node>& rNodes, float time, float blendRate)
{
	if (!m_spAnimation) { return; }

	const bool useBlend = (blendRate < 1.0f);

	for (auto& rAnimNode : m_spAnimation->m_nodes)
	{
		if (rAnimNode.m_nodeOffset < 0) { continue; }

		const size_t idx = static_cast<size_t>(rAnimNode.m_nodeOffset);
		if (idx >= rNodes.size()) { continue; }

		KdAnimNodePose dst;
		if (!rAnimNode.Evaluate(dst, time)) { continue; }

		Math::Vector3    s = dst.m_scale;
		Math::Quaternion r = dst.m_rotation;
		Math::Vector3    t = dst.m_translation;

		if (useBlend && idx < m_blendSrcPose.size() && m_blendSrcPose[idx].m_enable)
		{
			const KdAnimNodePose& src = m_blendSrcPose[idx];
			s = Math::Vector3::Lerp(src.m_scale, dst.m_scale, blendRate);
			r = Math::Quaternion::Slerp(src.m_rotation, dst.m_rotation, blendRate); // 回転はSlerp
			t = Math::Vector3::Lerp(src.m_translation, dst.m_translation, blendRate);
		}

		// ルートモーション対象ノードなら、ロック軸をメッシュ上では0(基準位置)に固定する
		// 実際の移動量は AdvanceTime 側で別途 m_rootMotionDelta に積んである
		if (rAnimNode.m_nodeOffset == m_rootNodeOffset)
		{
			if (m_rootLockX) t.x = 0.0f;
			if (m_rootLockY) t.y = 0.0f;
			if (m_rootLockZ) t.z = 0.0f;
		}

		rNodes[idx].m_localTransform =
			Math::Matrix::CreateScale(s) *
			Math::Matrix::CreateFromQuaternion(r) *
			Math::Matrix::CreateTranslation(t);
	}
}

bool KdAnimator::SampleRootLocalPos(float time, Math::Vector3& rOut) const
{
	if (!m_spAnimation || m_rootNodeOffset < 0) { return false; }

	for (auto& node : m_spAnimation->m_nodes)
	{
		if (node.m_nodeOffset != m_rootNodeOffset) { continue; }

		// InterpolateTranslations は non-const だが値は変更しないため const_cast で回避
		return const_cast<KdAnimationData::Node&>(node).InterpolateTranslations(rOut, time);
	}
	return false;
}

void KdAnimator::UpdateRootMotion(float prevTime, float newTime, bool looped, float maxLength)
{
	if (m_rootNodeOffset < 0) { return; }

	Math::Vector3 prevPos;
	if (!m_hasRootSample)
	{
		// 初回は差分を取れないので、今の位置を基準として記録するだけ
		if (SampleRootLocalPos(prevTime, prevPos))
		{
			m_rootPrevLocalPos = prevPos;
			m_hasRootSample = true;
		}
		return;
	}
	prevPos = m_rootPrevLocalPos;

	Math::Vector3 newPos;
	if (!SampleRootLocalPos(newTime, newPos)) { return; }

	Math::Vector3 delta;
	if (looped)
	{
		// ループで折り返した分は、終端までの残り + 先頭からの続き を合算する
		Math::Vector3 posAtEnd, posAtStart;
		SampleRootLocalPos(maxLength, posAtEnd);
		SampleRootLocalPos(0.0f, posAtStart);
		delta = (posAtEnd - prevPos) + (newPos - posAtStart);
	}
	else
	{
		delta = newPos - prevPos;
	}

	// ロックしている軸だけを実移動量として取り出す(ロックしていない軸はメッシュ側の動きに残す)
	Math::Vector3 extracted = Math::Vector3::Zero;
	if (m_rootLockX) extracted.x = delta.x;
	if (m_rootLockY) extracted.y = delta.y;
	if (m_rootLockZ) extracted.z = delta.z;

	m_rootMotionDelta += extracted;
	m_rootPrevLocalPos = newPos;
}

void KdAnimator::FireEvents(float prevTime, float newTime, bool looped, bool isEnd)
{
	if (!m_onEvent || !m_spAnimation) { return; }

	if (m_eventFiredMask.size() != m_spAnimation->m_events.size())
	{
		m_eventFiredMask.assign(m_spAnimation->m_events.size(), false);
	}

	// ループで先頭に戻った周では、区間に入り直したイベントを再び発火させたいので
	// マスクを一旦クリアする(=1周につき1回だけ発火、という運用になる)
	if (looped)
	{
		std::fill(m_eventFiredMask.begin(), m_eventFiredMask.end(), false);
	}

	for (size_t i = 0; i < m_spAnimation->m_events.size(); ++i)
	{
		if (m_eventFiredMask[i]) { continue; } // 何らかの理由で区間が2回評価されても二重発火しない

		const auto& ev = m_spAnimation->m_events[i];

		bool hit;
		if (looped)
		{
			// 折り返しをまたいだ区間 [prevTime, maxLength) ∪ [0, newTime)
			hit = (ev.m_time >= prevTime) || (ev.m_time < newTime);
		}
		else if (isEnd)
		{
			// 最終フレームは終端も含める [prevTime, newTime]
			hit = (ev.m_time >= prevTime) && (ev.m_time <= newTime);
		}
		else
		{
			// 通常区間 [prevTime, newTime)
			hit = (ev.m_time >= prevTime) && (ev.m_time < newTime);
		}

		if (!hit) { continue; }

		m_eventFiredMask[i] = true;
		m_onEvent(ev.m_name);
	}
}

void KdAnimator::AdvanceTime(std::vector<KdModelWork::Node>& rNodes, float speed)
{
	if (!m_spAnimation) { return; }

	const float maxLength = m_spAnimation->m_maxLength;

	// --- 終了済み：最終ポーズを維持し続ける ---
	if (m_isEnd)
	{
		ApplyPose(rNodes, maxLength, 1.0f);
		return;
	}

	// DeltaTime(秒)を、アニメデータの単位(フレーム)に変換
	// speedは"1.0=等倍"の再生速度倍率として扱う
	const float deltaTime = Time::Instance().GetDeltaTime();
	const float frameAdvance = deltaTime * kAnimBaseFPS * speed;

	// --- 切替直後：現在ポーズ（前アニメの結果）をSRTで記録 ---
	if (m_needSnapshot)
	{
		m_blendSrcPose.assign(rNodes.size(), KdAnimNodePose{});
		for (size_t i = 0; i < rNodes.size(); ++i)
		{
			Math::Vector3 s, t; Math::Quaternion r;
			rNodes[i].m_localTransform.Decompose(s, r, t);
			m_blendSrcPose[i] = { s, r, t, true };
		}
		m_needSnapshot = false;
	}

	const bool  isBlending = (m_blendDuration > 0.0f) && (m_blendTime < m_blendDuration);
	float blendRate = isBlending ? (m_blendTime / m_blendDuration) : 1.0f;
	// EaseInOut(smoothstep)。線形より始まり/終わりが滑らかになり、切り替わりの唐突さが減る
	if (m_useBlendEasing) { blendRate = blendRate * blendRate * (3.0f - 2.0f * blendRate); }

	// --- 現在時刻のポーズを適用 ---
	ApplyPose(rNodes, m_time, blendRate);

	if (isBlending) { m_blendTime += deltaTime * kAnimBaseFPS; }

	// --- フレームを進める ---
	const float prevTime = m_time;
	m_time += frameAdvance;

	bool looped = false;
	if (maxLength <= 0.0f)
	{
		m_time = 0.0f;
		m_isEnd = !m_isLoop;
	}
	else if (m_time >= maxLength)
	{
		if (m_isLoop)
		{
			// 余りを繰り越してループ（speedがmaxLengthを超えてもズレない）
			m_time = fmodf(m_time, maxLength);
			looped = true;
		}
		else
		{
			m_time = maxLength;
			m_isEnd = true;

			// ★終端ポーズをこのフレーム内で確定させる
			//   （ここで書かないと1フレーム手前の補間ポーズで止まる）
			ApplyPose(rNodes, m_time, blendRate);
		}
	}

	// --- ルートモーション更新 ---
	UpdateRootMotion(prevTime, m_time, looped, maxLength);

	// --- イベント発火(1周につき1回だけ、マスクで二重発火を防止) ---
	FireEvents(prevTime, m_time, looped, m_isEnd);
}