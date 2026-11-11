#include "inc_PhaseShader.hlsli"

static const float2 kCorner[4] =
{
	float2(-1, -1), float2(-1, 1), float2(1, -1), float2(1, 1)
};

float hash1(float x)
{
	return frac(sin(x) * 43758.5453f);
}

// ===========================================================
// キーフレーム
//
// x = 時刻 / y = 横位置(画面幅に対する比) / z = 横の伸び / w = 縦の伸び
//
// 進行方向(横)に縮んで直角方向(縦)に伸びると「ぶつかって止まった」に見える。
// 逆にすると、ただ伸びただけの絵になる。

// ===========================================================
#define KEY_COUNT 7

static const float4 kKey[KEY_COUNT] =
{
	float4(0.00f, -1.45f, 1.90f, 0.68f), // 画面外。横に伸びきって飛んでくる
	float4(0.18f, 0.00f, 1.40f, 0.84f), // 中央へ到達。まだ伸びている
	float4(0.26f, 0.07f, 0.52f, 1.48f), // 急停止。横に潰れて縦に伸びる
	float4(0.36f, -0.03f, 1.28f, 0.80f), // 反動。横へ弾けて少し戻る
	float4(0.44f, 0.00f, 1.00f, 1.00f), // 収まる
	float4(0.82f, 0.00f, 1.00f, 1.00f), // 静止。読む時間
	float4(1.00f, 1.85f, 2.20f, 0.62f), // 右へ加速して抜ける
};

// 区間ごとの効き方。0=線形(鋭い) 1=減速 2=加速
static const int kEase[KEY_COUNT] = { 1, 0, 0, 1, 1, 2, 2 };

float ApplyEase(float k, int mode)
{
	if (mode == 1)
		return 1.0f - pow(1.0f - k, 3.0f); // 減速
	if (mode == 2)
		return k * k; // 加速
	return k; // 線形
}

// 時刻から位置と伸びを引く。
float3 SampleKey(float t)
{
	// 端は折り返す
	if (t <= kKey[0].x)
		return kKey[0].yzw;
	if (t >= kKey[KEY_COUNT - 1].x)
		return kKey[KEY_COUNT - 1].yzw;

	for (int i = 0; i < KEY_COUNT - 1; ++i)
	{
		float t0 = kKey[i].x;
		float t1 = kKey[i + 1].x;
		if (t < t0 || t >= t1)
			continue;

		float k = (t - t0) / max(t1 - t0, 1e-5f);
		k = ApplyEase(k, kEase[i + 1]);

		return lerp(kKey[i].yzw, kKey[i + 1].yzw, k);
	}

	return kKey[KEY_COUNT - 1].yzw;
}

VSOutput VSMain(uint vid : SV_VertexID)
{
	float2 corner = kCorner[vid];

	const float t = 1.0f - g_progress;

	float3 cur = SampleKey(t);

	// 速度は位置の差分から取る。
	// キーを動かしても勝手に追従するので、二重に管理しなくて済む。
	const float eps = 0.012f;
	float3 nxt = SampleKey(t + eps);
	float speed = saturate(abs(nxt.x - cur.x) / eps * 0.35f);

	// 潰れ量。1より縦が伸びているほど大きい。
	float squash = saturate((cur.z < 1.0f) ? (1.0f - cur.z) * 2.0f : 0.0f);

	// 止まっている間だけ震える。飛んでいる間は震わせない。
	float shakeAmount = squash * 16.0f;
	float2 shake = float2(hash1(g_time * 37.0f) - 0.5f,
						  hash1(g_time * 59.0f + 11.0f) - 0.5f) * shakeAmount;

	float2 pos = g_center;
	pos.x += cur.x * g_screenSize.x;
	pos += shake + corner * g_halfSize * float2(cur.y, cur.z);

	VSOutput Out;
	Out.Pos = float4(
		pos.x / g_screenSize.x * 2.0f - 1.0f,
		1.0f - pos.y / g_screenSize.y * 2.0f,
		0.0f, 1.0f);

	float2 uv = corner * 0.5f + 0.5f;
	uv.y = lerp(g_uvRange.x, g_uvRange.y, uv.y);

	Out.UV = uv;
	Out.Speed = speed;
	Out.Squash = squash;
	return Out;
}
