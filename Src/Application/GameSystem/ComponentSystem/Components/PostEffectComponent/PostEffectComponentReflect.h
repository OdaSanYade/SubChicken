#pragma once
#include "PostEffectComponent.h"
#include "../../KdComponentReflect.h"

KD_REFLECT(ScreenTone, enabled, saturation)
KD_REFLECT(Abberation, enabled, strength)
KD_REFLECT(RadialBlur, enabled, strength, falloff)
KD_REFLECT(Vignette, enabled, strength, color)
KD_REFLECT(PostEffectComponent, isEditMode, screenTone, abberation, radialBlur, vignette)

KD_AUTO_REGISTER_COMPONENT(PostEffectComponent, "PostEffect")