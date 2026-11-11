#pragma once

#if __has_include(<imgui.h>)
#include <imgui.h>
#ifndef KD_ECS_IMGUI
#define KD_ECS_IMGUI 1
#endif
#elif __has_include(<imgui/imgui.h>)
#include <imgui/imgui.h>
#ifndef KD_ECS_IMGUI
#define KD_ECS_IMGUI 1
#endif
#endif

// ---- DirectXMath ----
#if __has_include(<DirectXMath.h>)
#include <DirectXMath.h>
#ifndef KD_ECS_HAS_DXMATH
#define KD_ECS_HAS_DXMATH 1
#endif
#endif

// ---- SimpleMath ----
#if __has_include(<SimpleMath.h>)
#include <SimpleMath.h>
#ifndef KD_ECS_HAS_SIMPLEMATH
#define KD_ECS_HAS_SIMPLEMATH 1
#endif
#endif