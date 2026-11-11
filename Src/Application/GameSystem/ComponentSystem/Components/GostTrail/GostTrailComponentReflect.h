#pragma once
#include "../../KdComponentReflect.h"
#include "GhostTrailComponent.h"
#include "../Render/RenderComponent.h"

KD_REFLECT(GhostTrailComponent, materialId, spacing, lifeTime, recording, color, rimColor, params)

KD_AUTO_REGISTER_COMPONENT(GhostTrailComponent, "GhostTrail")