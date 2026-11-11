#pragma once
#include "../../KdComponentReflect.h"
#include "TrailComponent.h"

KD_REFLECT(TrailComponent, trailId, widthScale, widthMode,
	width, lifeTime, minDist, color, emitting)
	KD_AUTO_REGISTER_COMPONENT(TrailComponent, "Trail")