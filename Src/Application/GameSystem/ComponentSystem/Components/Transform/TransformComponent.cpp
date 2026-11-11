#include "Pch.h"
#include "TransformComponent.h"

namespace
{
	constexpr float kPi = 3.14159265358979323846f;
	constexpr float k2Pi = kPi * 2.0f;

	// 角度を [-π, π] に正規化する
	inline float WrapAnglePi(float _rad)
	{
		_rad = fmodf(_rad + kPi, k2Pi);
		if (_rad < 0.0f) _rad += k2Pi;
		return _rad - kPi;
	}

	inline Math::Quaternion ToQuat(const Math::Vector4& _v)
	{
		return Math::Quaternion(_v.x, _v.y, _v.z, _v.w);
	}
	inline Math::Vector4 ToVec4(const Math::Quaternion& _q)
	{
		return Math::Vector4(_q.x, _q.y, _q.z, _q.w);
	}
}

void TransformComponent::SetScale(const Math::Vector3& _scale)
{
	//constexpr float kMinScale = 0.0001f;

	//auto clampAxis = [](float v) {
	//	if (fabsf(v) < kMinScale) return (v < 0.0f) ? -kMinScale : kMinScale;
	//	return v;
	//	};

	//m_scale = { clampAxis(_scale.x), clampAxis(_scale.y), clampAxis(_scale.z) };

	m_scale = _scale;
	SetDirty();
}

void TransformComponent::SetRotationEuler(const Math::Vector3& _eulerRad)
{
	Math::Quaternion q = Math::Quaternion::CreateFromYawPitchRoll(_eulerRad.y, _eulerRad.x, _eulerRad.z);
	m_rotation = { q.x, q.y, q.z, q.w };
	SetDirty();
}

Math::Vector3 TransformComponent::GetRotationEuler() const
{
	Math::Quaternion q(m_rotation.x, m_rotation.y, m_rotation.z, m_rotation.w);
	Math::Matrix m = Math::Matrix::CreateFromQuaternion(q);

	// 簡易分解(ジンバルロック付近で精度が落ちる点に注意)
	float pitch = asinf(-m._32);
	float yaw = atan2f(m._31, m._33);
	float roll = atan2f(m._12, m._22);
	return { pitch, yaw, roll };
}

Math::Matrix TransformComponent::GetTargetLocalMatrix(const Math::Matrix& _target) const
{
	UpdateMatrix();
	Math::Matrix localMat = m_world * _target.Invert();
	return localMat;
}

void TransformComponent::SetWorldMatrix(const Math::Matrix& _world)
{
	Math::Matrix parentWorld = m_parentMatrix;
	SetFromLocalMatrix(_world * parentWorld.Invert());
}

void TransformComponent::SetFromLocalMatrix(const Math::Matrix& _local)
{
	Math::Matrix localCopy = _local;
	Math::Vector3 scale;
	Math::Quaternion rot;
	Math::Vector3 trans;

	if (!localCopy.Decompose(scale, rot, trans))
	{
		// 分解失敗(縮退行列など)。安全のため何もしない
		return;
	}

	m_scale = scale;
	m_rotation = { rot.x, rot.y, rot.z, rot.w };
	m_position = trans;
	SetDirty();
}

void TransformComponent::UpdateMatrix() const
{
	if (!m_isDirty)return;

	// mW = mS * mR * mT * 親行列
	m_local =
		Math::Matrix::CreateScale(m_scale) *
		Math::Matrix::CreateFromQuaternion(m_rotation) *
		Math::Matrix::CreateTranslation(m_position);

	m_world = m_local * m_parentMatrix;

	m_isDirty = false;
}

//====================================================
// 相対操作
//====================================================
void TransformComponent::TranslateLocal(const Math::Vector3& _delta)
{
	Math::Matrix rot = Math::Matrix::CreateFromQuaternion(m_rotation);
	m_position += Math::Vector3::TransformNormal(_delta, rot);
	SetDirty();
}

void TransformComponent::RotateAxis(const Math::Vector3& _axis, float _angleRad)
{
	// ワールド空間の軸を基準に回転(ローカル軸基準にしたい場合は current * delta の順に変更)
	Math::Quaternion delta = Math::Quaternion::CreateFromAxisAngle(_axis, _angleRad);
	Math::Quaternion current(m_rotation.x, m_rotation.y, m_rotation.z, m_rotation.w);
	Math::Quaternion result = delta * current;

	m_rotation = { result.x, result.y, result.z, result.w };
	SetDirty();
}

//====================================================
// 向き制御
//====================================================
void TransformComponent::SlerpRotationTo(const Math::Vector4& _targetRot, float _t)
{
	_t = std::clamp(_t, 0.0f, 1.0f);

	Math::Quaternion result = Math::Quaternion::Slerp(ToQuat(m_rotation), ToQuat(_targetRot), _t);
	result.Normalize();

	m_rotation = ToVec4(result);
	SetDirty();
}

void TransformComponent::SmoothRotationTo(const Math::Vector4& _targetRot, float _sharpness, float _deltaTime)
{
	if (_sharpness <= 0.0f || _deltaTime <= 0.0f) return;

	// 1 - exp(-k*dt) : dtが変動しても同じ「見た目の速さ」になる補間率
	const float t = 1.0f - expf(-_sharpness * _deltaTime);
	SlerpRotationTo(_targetRot, t);
}

void TransformComponent::RotateTowards(const Math::Vector4& _targetRot, float _maxAngleRad)
{
	if (_maxAngleRad <= 0.0f) return;

	Math::Quaternion from = ToQuat(m_rotation);
	Math::Quaternion to = ToQuat(_targetRot);

	// 最短経路になるよう符号を揃える
	float dot = from.Dot(to);
	if (dot < 0.0f)
	{
		to = Math::Quaternion(-to.x, -to.y, -to.z, -to.w);
		dot = -dot;
	}
	dot = std::clamp(dot, -1.0f, 1.0f);

	const float angle = 2.0f * acosf(dot); // 2姿勢間の回転角
	if (angle < 1e-5f)
	{
		m_rotation = ToVec4(to); // ほぼ到達済み。スナップさせて終了
		SetDirty();
		return;
	}

	const float t = std::min(1.0f, _maxAngleRad / angle);
	Math::Quaternion result = Math::Quaternion::Slerp(from, to, t);
	result.Normalize();

	m_rotation = ToVec4(result);
	SetDirty();
}

bool TransformComponent::MakeLookRotation(const Math::Vector3& _forward, const Math::Vector3& _up,
	Math::Quaternion& _out)
{
	Math::Vector3 forward = _forward;
	if (forward.LengthSquared() < 1e-8f) return false;
	forward.Normalize();

	Math::Vector3 right = _up.Cross(forward);
	if (right.LengthSquared() < 1e-8f)
	{
		// upとforwardがほぼ平行 → 別軸で作り直す
		Math::Vector3 fallbackUp =
			(fabsf(forward.y) > 0.99f) ? Math::Vector3::Backward : Math::Vector3::Up;
		right = fallbackUp.Cross(forward);
		if (right.LengthSquared() < 1e-8f) return false;
	}
	right.Normalize();

	Math::Vector3 up = forward.Cross(right);

	Math::Matrix lookMat = Math::Matrix::Identity;
	lookMat.Right(right);
	lookMat.Up(up);
	lookMat.Backward(forward); // GetForward()がBackward()を返す既存仕様に合わせている

	_out = Math::Quaternion::CreateFromRotationMatrix(lookMat);
	return true;
}

void TransformComponent::LookAt(const Math::Vector3& _targetPos, const Math::Vector3& _up)
{
	Math::Quaternion q;
	if (!MakeLookRotation(_targetPos - m_position, _up, q)) return;

	m_rotation = ToVec4(q);
	SetDirty();
}

void TransformComponent::SmoothLookAt(const Math::Vector3& _targetPos, float _sharpness,
	float _deltaTime, const Math::Vector3& _up)
{
	Math::Quaternion look;
	if (!MakeLookRotation(_targetPos - m_position, _up, look)) return;

	SmoothRotationTo(ToVec4(look), _sharpness, _deltaTime);
}

void TransformComponent::RotateTowardsDirection(const Math::Vector3& _dir, float _maxAngleRad,
	const Math::Vector3& _up)
{
	Math::Quaternion look;
	if (!MakeLookRotation(_dir, _up, look)) return;

	RotateTowards(ToVec4(look), _maxAngleRad);
}

//Y軸限定回転制御
float TransformComponent::GetYaw() const
{
	Math::Matrix rot = Math::Matrix::CreateFromQuaternion(ToQuat(m_rotation));
	Math::Vector3 forward = rot.Backward(); // GetForward()の仕様に合わせる

	// 真上/真下を向いていると forward.xz が潰れる
	if (forward.x * forward.x + forward.z * forward.z < 1e-8f)
	{
		Math::Vector3 up = rot.Up();
		// 上を向いているときは up が「後ろ向き」に倒れるため符号を反転する
		if (forward.y > 0.0f) up = -up;
		return atan2f(up.x, up.z);
	}

	return atan2f(forward.x, forward.z);
}

void TransformComponent::SetYaw(float _yawRad)
{
	Math::Quaternion q = Math::Quaternion::CreateFromYawPitchRoll(_yawRad, 0.0f, 0.0f);
	m_rotation = ToVec4(q);
	SetDirty();
}

void TransformComponent::SmoothYawTo(float _targetYawRad, float _sharpness, float _deltaTime)
{
	if (_sharpness <= 0.0f || _deltaTime <= 0.0f) return;

	const float current = GetYaw();
	const float delta = WrapAnglePi(_targetYawRad - current); // 最短回り
	const float t = 1.0f - expf(-_sharpness * _deltaTime);

	SetYaw(current + delta * t);
}

void TransformComponent::RotateYawTowards(float _targetYawRad, float _maxAngleRad)
{
	if (_maxAngleRad <= 0.0f) return;

	const float current = GetYaw();
	float delta = WrapAnglePi(_targetYawRad - current);

	// このフレームで回れる量を超えていたら頭打ちにする
	if (delta > _maxAngleRad) delta = _maxAngleRad;
	if (delta < -_maxAngleRad) delta = -_maxAngleRad;

	SetYaw(current + delta);
}

bool TransformComponent::YawFromDirection(const Math::Vector3& _dir, float& _outYawRad)
{
	// Y成分は無視。水平方向だけで角度を決める
	if (_dir.x * _dir.x + _dir.z * _dir.z < 1e-8f) return false;

	_outYawRad = atan2f(_dir.x, _dir.z);
	return true;
}

void TransformComponent::LookAtYaw(const Math::Vector3& _targetPos)
{
	float yaw;
	if (!YawFromDirection(_targetPos - m_position, yaw)) return;

	SetYaw(yaw);
}

void TransformComponent::SmoothLookAtYaw(const Math::Vector3& _targetPos, float _sharpness, float _deltaTime)
{
	float yaw;
	if (!YawFromDirection(_targetPos - m_position, yaw)) return;

	SmoothYawTo(yaw, _sharpness, _deltaTime);
}

void TransformComponent::RotateYawTowardsDirection(const Math::Vector3& _dir, float _maxAngleRad)
{
	float yaw;
	if (!YawFromDirection(_dir, yaw)) return;

	RotateYawTowards(yaw, _maxAngleRad);
}

//====================================================
// 座標変換ヘルパー
//====================================================
Math::Vector3 TransformComponent::TransformPoint(const Math::Vector3& _localPoint) const
{
	UpdateMatrix();
	return Math::Vector3::Transform(_localPoint, m_world);
}

Math::Vector3 TransformComponent::InverseTransformPoint(const Math::Vector3& _worldPoint) const
{
	UpdateMatrix();
	return Math::Vector3::Transform(_worldPoint, m_world.Invert());
}

Math::Vector3 TransformComponent::TransformDirection(const Math::Vector3& _localDir) const
{
	UpdateMatrix();
	return Math::Vector3::TransformNormal(_localDir, m_world);
}

Math::Vector3 TransformComponent::InverseTransformDirection(const Math::Vector3& _worldDir) const
{
	UpdateMatrix();
	return Math::Vector3::TransformNormal(_worldDir, m_world.Invert());
}

float TransformComponent::GetDistanceTo(const TransformComponent& _other) const
{
	return Math::Vector3::Distance(GetWorldPosition(), _other.GetWorldPosition());
}

Math::Vector3 TransformComponent::GetDirectionTo(const TransformComponent& _other) const
{
	Math::Vector3 dir = _other.GetWorldPosition() - GetWorldPosition();
	dir.Normalize();
	return dir;
}