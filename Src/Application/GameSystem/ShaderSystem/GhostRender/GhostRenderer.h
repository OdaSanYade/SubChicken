#pragma once
#include "OverlayModelRenderer.h"

// 残像（GBufferC を使って遮蔽する）
class GhostRenderer : public OverlayModelRenderer
{
public:
	bool Init();
};
