#pragma once

class TransformComponent 
{
public:

	TransformComponent() = default;
	~TransformComponent() = default;

	static Math::Vector3 NormalizeAxis(const Math::Vector3& _v)
	{
		const float len2 = _v.LengthSquared();
		if (len2 < 1e-12f) return Math::Vector3::Zero;
		return _v / std::sqrt(len2);
	}

	//位置、回転、拡縮を設定
	void SetPosition(const Math::Vector3& _pos) { m_position = _pos; SetDirty();  }
	void SetRotation(const Math::Vector4& _rot) { m_rotation = _rot; SetDirty();  }
	void SetScale(const Math::Vector3& _scale);
	void SetTRS(const Math::Vector3& _pos, const Math::Vector4& _rot, const Math::Vector3& _scale)
	{m_position = _pos; m_rotation = _rot; m_scale = _scale; SetDirty();}

	//位置、回転、拡縮を取得
	const Math::Vector3& GetPosition() const { return m_position; }
	const Math::Vector4& GetRotation() const { return m_rotation; }
	const Math::Vector3& GetScale()    const { return m_scale; }

	void SetRotationEuler(const Math::Vector3& _eulerRad);
	Math::Vector3 GetRotationEuler() const;

	//親行列設定
	void SetParent(const Math::Matrix& _transform) { m_parentMatrix = _transform; SetDirty();};

	//行列を取得
	Math::Matrix GetWorldMatrix() const { UpdateMatrix(); return m_world; }
	Math::Matrix GetLocalMatrix() const { UpdateMatrix(); return m_local; }
	//ターゲットから見たローカル行列を取得
	Math::Matrix GetTargetLocalMatrix(const Math::Matrix& _target = Math::Matrix::Identity) const;

	void SetWorldMatrix(const Math::Matrix& _world);
	void SetFromLocalMatrix(const Math::Matrix& _local);

	//方向
	Math::Vector3 GetForward() const { UpdateMatrix(); return NormalizeAxis(m_world.Backward()); }
	Math::Vector3 GetUp()	   const { UpdateMatrix(); return NormalizeAxis(m_world.Up()); }
	Math::Vector3 GetRight()   const { UpdateMatrix(); return NormalizeAxis(m_world.Right()); }
	Math::Vector3 GetWorldPosition() const { UpdateMatrix(); return m_world.Translation(); }

	//相対操作
	void Translate(const Math::Vector3& _delta) { m_position += _delta; SetDirty(); }
	void TranslateLocal(const Math::Vector3& _delta);
	void RotateAxis(const Math::Vector3& _axis, float _angleRad);
	void AddScale(const Math::Vector3& _delta) { SetScale(m_scale + _delta); }

	//向き制御
	void LookAt(const Math::Vector3& _targetPos, const Math::Vector3& _up = Math::Vector3::Up);
	void SlerpRotationTo(const Math::Vector4& _targetRot, float _t);
	void SmoothRotationTo(const Math::Vector4& _targetRot, float _sharpness, float _deltaTime);
	void RotateTowards(const Math::Vector4& _targetRot, float _maxAngleRad);
	void SmoothLookAt(const Math::Vector3& _targetPos, float _sharpness, float _deltaTime,
		const Math::Vector3& _up = Math::Vector3::Up);
	void RotateTowardsDirection(const Math::Vector3& _dir, float _maxAngleRad,
		const Math::Vector3& _up = Math::Vector3::Up);

	//Y軸回転限定(ピッチ・ロールを捨てて水平を保つ)
	float GetYaw() const;
	void  SetYaw(float _yawRad);
	void SmoothYawTo(float _targetYawRad, float _sharpness, float _deltaTime);
	void RotateYawTowards(float _targetYawRad, float _maxAngleRad);
	void LookAtYaw(const Math::Vector3& _targetPos);
	void SmoothLookAtYaw(const Math::Vector3& _targetPos, float _sharpness, float _deltaTime);
	void RotateYawTowardsDirection(const Math::Vector3& _dir, float _maxAngleRad);

	// 方向ベクトルからYaw角を求める(XZ成分が極小なら false)
	static bool YawFromDirection(const Math::Vector3& _dir, float& _outYawRad);

	//座標変換ヘルパ
	Math::Vector3 TransformPoint(const Math::Vector3& _localPoint) const;
	Math::Vector3 InverseTransformPoint(const Math::Vector3& _worldPoint) const;
	Math::Vector3 TransformDirection(const Math::Vector3& _localDir) const;
	Math::Vector3 InverseTransformDirection(const Math::Vector3& _worldDir) const;

	float GetDistanceTo(const TransformComponent& _other) const;
	Math::Vector3 GetDirectionTo(const TransformComponent& _other) const;

private:

	static bool MakeLookRotation(const Math::Vector3& _forward, const Math::Vector3& _up,
		Math::Quaternion& _out);

	//トランスフォーム
	Math::Vector3 m_position = Math::Vector3::Zero;
	Math::Vector4 m_rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
	Math::Vector3 m_scale    = { 1.0f, 1.0f, 1.0f};

	//行列
	mutable Math::Matrix m_world  = Math::Matrix::Identity;
	mutable Math::Matrix m_local  = Math::Matrix::Identity;
	mutable bool m_isDirty = true;

	Math::Matrix m_parentMatrix = Math::Matrix::Identity;

	void SetDirty() { m_isDirty = true; }
	void UpdateMatrix() const;
};

static_assert(std::is_trivially_copyable_v<TransformComponent>,"TransfomComponent must be trivially copyable");