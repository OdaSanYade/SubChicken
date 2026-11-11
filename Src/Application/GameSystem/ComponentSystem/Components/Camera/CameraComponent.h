#pragma once

class CameraComponent
{
public:
	enum class Mode { Manual, Follow, Orbit };

	// pivot(from)→望むeye(to)へのレイ。hitしたらtrue・fromからの距離をoutDistへ返す
	// ※メンバに保持せずUpdate()の引数で渡す(trivially copyableを崩さないため)
	using RayCastFn = std::function<bool(const Math::Vector3& _from, const Math::Vector3& _to, float& _outDist)>;

	// ターゲットプル候補: {ID(フレームをまたいだ同一性判定用。エンティティのハンドル等を想定), ワールド座標}
	using PullCandidate = std::pair<std::uint64_t, Math::Vector3>;

	//==================== 射影 ====================
	void SetPerspective(float _fovDeg, float _nearZ = 0.01f, float _farZ = 2000.0f, float _aspect = 0.0f);
	void SetFovY(float _fovDeg) { m_fovDeg = _fovDeg; m_fovLerping = false; m_projDirty = true; }
	void SetClip(float _nearZ, float _farZ) { m_nearZ = _nearZ; m_farZ = _farZ; m_projDirty = true; }
	void SetAspect(float _aspect) { m_aspect = _aspect; m_projDirty = true; } // 0以下で自動
	float GetFovY()  const { return m_fovDeg; }
	float GetNearZ() const { return m_nearZ; }
	float GetFarZ()  const { return m_farZ; }

	void SetTargetFovY(float _fovDeg, float _lambda = 10.0f) { m_targetFovDeg = _fovDeg; m_fovLambda = _lambda; m_fovLerping = true; }

	//==================== ビュー(姿勢) ====================
	void SetCameraMatrix(const Math::Matrix& _cameraWorld);
	void SetLookAt(const Math::Vector3& _eye, const Math::Vector3& _target, const Math::Vector3& _up = Math::Vector3::Up);

	// Transform連動: 生ポインタを保持せず、毎フレーム外部からワールド行列を注入(Manual扱い)
	void FollowTransformMatrix(const Math::Matrix& _world) { m_mode = Mode::Manual; m_baseWorld = _world; }

	//==================== 追従(Follow) ====================
	void SetFollow(const Math::Vector3& _eye, const Math::Vector3& _lookTarget);
	void SetFollowResponse(float _posLambda, float _lookLambda) { m_posLambda = _posLambda; m_lookLambda = _lookLambda; }

	//==================== 軌道(Orbit / 三人称) ====================
	void SetOrbit(const Math::Vector3& _pivot, float _yawRad, float _pitchRad, float _distance);
	void SetOrbitPivot(const Math::Vector3& _pivot) { m_pivot = _pivot; m_mode = Mode::Orbit; }
	void AddOrbitAngles(float _yawRad, float _pitchRad);
	void AddOrbitDistance(float _delta);
	void SetOrbitPitchLimit(float _minRad, float _maxRad) { m_pitchMin = _minRad; m_pitchMax = _maxRad; }
	void SetOrbitDistanceLimit(float _min, float _max) { m_distMin = _min; m_distMax = _max; }
	float GetOrbitYaw()      const { return m_yaw; }
	float GetOrbitPitch()    const { return m_pitch; }
	float GetOrbitDistance() const { return m_distance; }

	void SnapToTarget() { m_smoothInit = false; m_framingInit = false; m_occInit = false; }

	//==================== ターゲットプル(センタリングアシスト/ソフトロック) ====================
	// 画面中央付近(視線からmaxAngle以内)に敵が入っている間、視点がその方向へ弱く引き寄せられる。
	// 強制ロックオンではなく、プレイヤーのAddOrbitAngles()による入力の後に
	// 追加でyaw/pitchへバイアスをかける形。Orbitモード専用。
	// 呼び出し順: AddOrbitAngles() → UpdateTargetPull() → Update() が想定順。
	void SetTargetPull(bool _enable) { m_targetPullEnabled = _enable; if (!_enable) { m_targetPullActive = false; m_targetPullLocked = false; } }
	bool IsTargetPullEnabled() const { return m_targetPullEnabled; }
	// _maxAngleRad : これより中心から離れた敵は無視(ロック中の対象もこれを超えたら解除)
	// _innerAngleRad : これより中心に近ければ最大強度(それ以上maxAngleへ近づくほど弱まる)
	// _strengthLambda : 引き寄せの速さ(指数減衰のλ。大きいほど速く追従)
	void SetTargetPullParams(float _maxAngleRad, float _strengthLambda, float _innerAngleRad = 0.0f);
	// プレイヤーが能動的に視点操作している度合い(0〜1)に応じて引き寄せを弱める量。既定0.85。
	void SetTargetPullInputSuppression(float _amount) { m_targetPullInputSuppression = std::clamp(_amount, 0.0f, 1.0f); }
	// スティッキー選択(フリッカー防止): 一度ロックした敵は、
	//  ・_switchMarginRadぶん明確に有利な候補が現れる か
	//  ・_minHoldTimeSec経過する
	// のどちらか(両方)を満たすまで乗り換えない。ロック対象がmaxAngleを超えて離れる/候補から消えた場合は即解除。
	void SetTargetPullStickiness(float _switchMarginRad, float _minHoldTimeSec)
	{
		m_targetPullSwitchMargin = _switchMarginRad;
		m_targetPullMinHoldTime = _minHoldTimeSec;
	}
	bool IsTargetPullActive() const { return m_targetPullActive; } // 直近のUpdateTargetPullで実際に吸着が働いたか
	// 地面貫通/上空への突き抜け防止: ターゲットプルが計算する目標pitchに対して、
	// 「今のdistanceでeyeがpivotからこの範囲を超えて上下しないように」動的に角度をクランプする安全弁。
	// eye.y = pivot.y - sin(pitch)*distance という式から逆算するため、distanceが変わっても正しく効く。
	// あくまでターゲットプル専用(プレイヤーの手動操作には影響しない)。
	// _minEyeHeightOffset : eyeがpivotよりこれ以上"下"にならないようにする(通常は負の値。例: -1.5でpivotの1.5下まで許容)
	// _maxEyeHeightOffset : eyeがpivotよりこれ以上"上"にならないようにする(既定は非常に大きい値=事実上無制限)
	void SetTargetPullHeightSafety(float _minEyeHeightOffset, float _maxEyeHeightOffset = 1000.0f)
	{
		m_targetPullMinEyeHeightOffset = _minEyeHeightOffset;
		m_targetPullMaxEyeHeightOffset = _maxEyeHeightOffset;
	}

	//---- ロック中の対象情報(UI・演出用。IsTargetPullActive()がfalseの間は値が古いままなので注意) ----
	std::uint64_t GetTargetPullLockedId() const { return m_targetPullLockedId; }
	const Math::Vector3& GetTargetPullTargetPosition() const { return m_targetPullTargetPos; } // 対象のワールド座標
	float GetTargetPullTargetDistance() const { return m_targetPullTargetDistance; } // カメラ推定位置からの距離
	float GetTargetPullTargetAngle() const { return m_targetPullTargetAngle; } // 画面中心からの角度(ラジアン)

	// _candidates : 敵候補の{ID, ワールド座標}リスト。中心に最も近い(かつmaxAngle以内)ものへ引き寄せる。
	//               ID はスティッキー選択のためにフレームをまたいで同一性を判定するのに使う。
	// _playerInputMagnitude : 0〜1程度。プレイヤーのLook入力の大きさ(任意、既定0=常に最大強度)。
	void UpdateTargetPull(float _dt, const std::vector<PullCandidate>& _candidates, float _playerInputMagnitude = 0.0f);

	// これより遠い敵はロックしない。画面中央に収めても吸着させない。
	// 遠くの敵に視点を引かれると、目の前の敵を捌けなくなる。
	void SetTargetPullMaxDistance(float _dist) { m_targetPullMaxDistance = _dist; }
	float GetTargetPullMaxDistance() const { return m_targetPullMaxDistance; }

	//==================== ルックアヘッド + デッドゾーン ====================
	void SetFraming(const Math::Vector3& _deadZoneHalf, float _lookAheadTime,
		float _lookAheadMax = 5.0f, float _velSmoothLambda = 8.0f);
	void EnableFraming(bool _on) { m_framingEnabled = _on; if (!_on) m_framingInit = false; }
	void SnapFraming() { m_framingInit = false; }

	//==================== Orbitオクルージョン回避 ====================
	void EnableOcclusion(bool _on) { m_occlusionEnabled = _on; if (!_on) m_occInit = false; }
	// _padding:壁からのマージン / _pullLambda:詰める速さ / _pushLambda:戻す速さ(遅め推奨)
	void SetOcclusionParams(float _padding, float _pullLambda, float _pushLambda);

	//==================== カメラシェイク ====================
	void AddTrauma(float _amount);
	void SetShakeParams(float _maxAngleRad, float _maxOffset, float _maxRollRad, float _freq, float _decayPerSec);
	float GetTrauma() const { return m_trauma; }

	//==================== 被写界深度(値として保持しApply時に反映) ====================
	void SetFocus(float _focusDist, float _foreRange, float _backRange)
	{
		m_focusDist = _focusDist; m_focusForeRange = _foreRange; m_focusBackRange = _backRange;
	}
	float GetFocusDistance()  const { return m_focusDist; }
	float GetFocusForeRange() const { return m_focusForeRange; }
	float GetFocusBackRange() const { return m_focusBackRange; }

	//==================== ディレクター用(vcamメタ) ====================
	// リロケート安全のためポインタでなくIDで識別する
	void     SetVcamId(std::uint32_t _id) { m_vcamId = _id; }
	std::uint32_t GetVcamId() const { return m_vcamId; }
	void SetPriority(int _p) { m_priority = _p; }
	int  GetPriority() const { return m_priority; }
	void SetVcamEnabled(bool _e) { m_vcamEnabled = _e; }
	bool IsVcamEnabled() const { return m_vcamEnabled; }

	//==================== 更新・反映 ====================
	// 毎フレーム1回。時間依存(追従/シェイク/FOV補間)を進め、最終ワールド行列を確定する。
	// _occlusionRay を渡すとOrbit時に壁抜け回避(渡さなければ無効)。
	void Update(float _dt, const RayCastFn& _occlusionRay = RayCastFn{});

	// 確定済みの姿勢・射影・焦点を任意のKdCameraへ反映(GPU転送は呼び出し側)
	void ApplyToKdCamera(KdCamera& _kd);

	//==================== 取得 ====================
	Mode GetMode() const { return m_mode; }
	const Math::Matrix& GetResolvedWorldMatrix() const { return m_finalWorld; }
	Math::Matrix        GetResolvedViewMatrix()  const { return m_finalWorld.Invert(); }
	Math::Vector3       GetWorldPosition()       const { return m_finalWorld.Translation(); }

private:
	Mode m_mode = Mode::Manual;

	// 射影
	float m_fovDeg = 60.0f, m_nearZ = 0.01f, m_farZ = 2000.0f, m_aspect = 0.0f;
	bool  m_projDirty = true;
	bool  m_fovLerping = false;
	float m_targetFovDeg = 60.0f, m_fovLambda = 10.0f;

	// 姿勢
	Math::Matrix m_baseWorld = Math::Matrix::Identity; // シェイク適用前の"素"の姿勢
	Math::Matrix m_finalWorld = Math::Matrix::Identity; // Updateで確定する最終姿勢

	// 追従/軌道
	Math::Vector3 m_desiredEye = { 0.0f, 0.0f, -10.0f };
	Math::Vector3 m_desiredLook = Math::Vector3::Zero;
	Math::Vector3 m_smoothEye = { 0.0f, 0.0f, -10.0f };
	Math::Vector3 m_smoothLook = Math::Vector3::Zero;
	bool  m_smoothInit = false;
	float m_posLambda = 10.0f, m_lookLambda = 12.0f;

	// 軌道パラメータ
	Math::Vector3 m_pivot = Math::Vector3::Zero;
	float m_yaw = 0.0f, m_pitch = 0.3f, m_distance = 10.0f;
	float m_pitchMin = -1.4f, m_pitchMax = 1.4f;
	float m_distMin = 1.0f, m_distMax = 100.0f;

	// ターゲットプル(センタリングアシスト)
	bool  m_targetPullEnabled = false;
	bool  m_targetPullActive = false;
	float m_targetPullMaxAngle = 0.4f;    // 約23°
	float m_targetPullInnerAngle = 0.1f;  // 約6°
	float m_targetPullLambda = 6.0f;
	float m_targetPullInputSuppression = 0.85f;
	// スティッキー選択(フリッカー防止)
	bool  m_targetPullLocked = false;
	std::uint64_t m_targetPullLockedId = 0;
	float m_targetPullSwitchMargin = 0.08f; // 約4.6°: これ以上有利でなければ乗り換えない
	float m_targetPullMinHoldTime = 0.35f;  // 秒: ロック直後はこの時間だけ乗り換えを禁止
	float m_targetPullHoldTimer = 0.0f;
	// 地面貫通/上空突き抜け防止(距離に応じて動的にpitchをクランプする際の閾値)
	float m_targetPullMinEyeHeightOffset = -1.5f; // pivotよりこれ以上下にeyeを下げない
	float m_targetPullMaxEyeHeightOffset = 1000.0f; // pivotよりこれ以上上にeyeを上げない(既定は事実上無制限)
	// 直近で実際に採用された対象の情報(UI/演出用キャッシュ)
	Math::Vector3 m_targetPullTargetPos = Math::Vector3::Zero;
	float m_targetPullTargetDistance = 0.0f;
	float m_targetPullTargetAngle = 0.0f;

	// ルックアヘッド + デッドゾーン
	bool  m_framingEnabled = false;
	Math::Vector3 m_deadZoneHalf = Math::Vector3::Zero;
	float m_lookAheadTime = 0.0f, m_lookAheadMax = 5.0f, m_velSmoothLambda = 8.0f;
	Math::Vector3 m_framedTarget = Math::Vector3::Zero;
	Math::Vector3 m_prevRawTarget = Math::Vector3::Zero;
	Math::Vector3 m_smoothVel = Math::Vector3::Zero;
	bool  m_framingInit = false;

	// オクルージョン回避
	bool  m_occlusionEnabled = false;
	float m_occlusionPadding = 0.3f;
	float m_occlusionPullLambda = 30.0f; // 詰めるのは速く
	float m_occlusionPushLambda = 4.0f;  // 戻すのは遅く
	float m_curOcclusionDist = 0.0f;
	bool  m_occInit = false;

	// シェイク
	float m_trauma = 0.0f, m_traumaDecay = 1.5f, m_shakeTime = 0.0f;
	float m_shakeMaxAngleRad = 0.05f;
	float m_shakeMaxRollRad = 0.03f;
	float m_shakeMaxOffset = 0.15f;
	float m_shakeFreq = 25.0f;
	float m_shYaw = 0.0f, m_shPitch = 0.0f, m_shRoll = 0.0f, m_shOffX = 0.0f, m_shOffY = 0.0f;

	// 被写界深度(KdCameraへApply時に転送)
	float m_focusDist = 0.0f, m_focusForeRange = 0.0f, m_focusBackRange = 2000.0f;

	// vcamメタ
	std::uint32_t m_vcamId = 0;
	int  m_priority = 0;
	bool m_vcamEnabled = true;

	float m_targetPullMaxDistance = 40.0f;

	Math::Vector3 ProcessFraming(const Math::Vector3& _raw, float _dt);
	void RebuildFinalMatrix();
	void ApplyProjection(KdCamera& _kd);
	static Math::Matrix BuildLookMatrix(const Math::Vector3& _eye, const Math::Vector3& _target, const Math::Vector3& _up);
};

static_assert(std::is_trivially_copyable_v<CameraComponent>, "CameraComponent must be trivially copyable");