#include "X-RayRender.h"

#include "Shader/XRayPS.shaderInc"
#include "Shader/XRayVS.shaderInc"

// X 線：共通の重ね描き（OverlayModelRenderer）に X 線用シェーダを渡すだけ
bool XRayRenderer::Init()
{
	return InitShared(XRayVS, sizeof(XRayVS), XRayPS, sizeof(XRayPS));
}
