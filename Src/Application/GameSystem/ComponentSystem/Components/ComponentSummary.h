#pragma once
#include "../KdComponentReflect.h"

// ---- 各コンポーネント + そのリフレクションアダプタ ----
#include "Transform/TransformComponentReflect.h"
#include "Camera/CameraComponentReflect.h"          
#include "Render/RenderComponentReflect.h"          
#include "State/StateMachineComponentReflect.h"     
#include "GostTrail/GostTrailComponentReflect.h"
#include "Trail/TrailComponentReflect.h"
#include "Light/LightComponentReflect.h"
#include "PostEffectComponent/PostEffectComponentReflect.h"

// ---- 汎用コンポーネント群 (Velocity / Health / タグ 等) ----
#include "DefaultComponent.h"
#include "EntityName.h"
