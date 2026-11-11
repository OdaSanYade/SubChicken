#pragma once
#include "../GhostRender/OverlayModelRenderer.h"

//　ゴーストトレイルと同じタイプ
//　ゴーストトレイルと違い履歴をスタックしない（事前にマスクされた GBufferE を使う）
class XRayRenderer : public OverlayModelRenderer
{
public:
	bool Init();
};
