#ifndef INC_KD_RESULT_SHADER_HLSLI
#define INC_KD_RESULT_SHADER_HLSLI

#include "../UiShader/inc_KdHudCommon.hlsli"

//===================================================
//
// リザルト画面 シェーダ 種別定義

//===================================================

#define RES_KIND_HEADLINE	0	// 見出し(CLEAR / THE CHICKEN DIED)
#define RES_KIND_STAT		1	// 成績の1行(項目名 + 値)
#define RES_KIND_RANK		2	// ランクの大文字

//===================================================
// 成績の帯
//===================================================
#define kStatPlateAlpha	0.38f

//===================================================
// 成績の種類。C++側の ResultHud::StatKind と一致させること。
//===================================================
#define RES_STAT_COMBO		0
#define RES_STAT_TIME		1
#define RES_STAT_LIFE		2

//===================================================
// 見出しの種類
//===================================================
#define RES_HEAD_CLEAR		0
#define RES_HEAD_DIED		1

#endif // INC_KD_RESULT_SHADER_HLSLI
