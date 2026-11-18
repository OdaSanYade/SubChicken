#pragma once

#include "UiShader/KdUiShader.h"
#include "HudShader/KdHudShader.h"
#include "TitleShader/KdTitleShader.h"
#include "ResultShader/KdResultShader.h"
#include "ResultShader/RankShader/KdRankShader.h"

//==========================
//
// 各パイプラインステートの呼び出しID
//
//==========================
// 深度情報の影響・書き込みの制御ステート
enum class KdDepthStencilState
{
	ZEnable,		// 手前の物に隠れる　○、自分の奥の物を隠す　○（基本の3D描画ステート
	ZWriteDisable,	// 手前の物に隠れる　○、自分の奥の物を隠す　×
	ZDisable,		// 手前の物に隠れる　×、自分の奥の物を隠す　×
	Max
};

// ポリゴンの面描画の扱い制御ステート
enum class KdRasterizerState
{
	CullNone,		// 描画省略なし
	CullFront,		// 表面描画省略
	CullBack,		// 裏面描画省略
	Max
};

// ピクセルの色合成方法の制御ステート
enum class KdBlendState
{
	Alpha,			// 透明度による色合成
	Add,			// 加算による色合成
	Max
};

// ピクセルの色取得方法の制御ステート
enum class KdSamplerState
{
	Anisotropic_Wrap,	// 異方性補間：テクスチャ繰り返し
	Anisotropic_Clamp,	// 異方性補間：テクスチャ範囲外は端のピクセルを延長
	Linear_Clamp,		// 線形補間：　テクスチャ範囲外は端のピクセルを延長
	Linear_Clamp_Cmp,	// 線形補間：　テクスチャ範囲外は端のピクセルを延長・比較機能付き
	Point_Wrap,			// 補間なし：　テクスチャ繰り返し
	Point_Clamp,		// 補間なし：　テクスチャ範囲外は端のピクセルを延長
	Max
};

//==========================================================
//
// シェーダ関係をまとめたクラス
//
//==========================================================
class KdShaderManager
{
public:

	static KdShaderManager& Instance()
	{
		static KdShaderManager instance;
		return instance;
	}

	// シェーダー本体の生成・シェーダーで利用する定数バッファの生成・影用の光からの深度情報テクスチャを生成
	void Init();

	//==========================
	//
	// パイプラインステートの変更
	//
	//==========================
	// 深度情報をどのように扱うかを変更：変更したかどうかが返ってくる
	void ChangeDepthStencilState(KdDepthStencilState stateId);
	void UndoDepthStencilState();

	// ポリゴンの面をどのように描画するのかを変更：変更したかどうかが返ってくる
	void ChangeRasterizerState(KdRasterizerState stateId);
	void UndoRasterizerState();

	// 画面の色をどのように合成するのかを変更：変更したかどうかが返ってくる
	void ChangeBlendState(KdBlendState stateId);
	void UndoBlendState();

	// テクスチャの色をどのように取ってくるのかを変更：変更したかどうかが返ってくる
	void ChangeSamplerState(KdSamplerState stateId, int slot = 0);
	void UndoSamplerState(int slot = 0);

	// 解放
	void Release();

private:

	KdShaderManager() {}

	~KdShaderManager() {}


	bool m_pixelArtStyle = true;

	//==========================
	//
	// パイプラインステート
	//
	//==========================
	 
	//深度ステンシル（奥行情報の使い方・手前にあるものを無視して描画したりできる
	ID3D11DepthStencilState* m_depthStencilStates[(int)KdDepthStencilState::Max] = {};
	std::stack<ID3D11DepthStencilState*> m_ds_Undo;

	// ラスタライズステート（ポリゴンの任意の面の描画を省略できる、処理を軽減する目的で運用する事が多い
	ID3D11RasterizerState* m_rasterizerStates[(int)KdRasterizerState::Max] = {};
	std::stack<ID3D11RasterizerState*> m_rs_Undo;

	// ブレンドステート（ピクセルの最終色を決めるときに既に塗られている色と、どう合成するのかの選択ができる
	ID3D11BlendState* m_blendStates[(int)KdBlendState::Max] = {};
	std::stack<ID3D11BlendState*> m_bs_Undo;

	// サンプラーステート（テクスチャのピクセル色を取得する際にどのような補間や扱いで取得するかを選択できる
	ID3D11SamplerState* m_samplerStates[(int)KdSamplerState::Max] = {};
	std::stack<ID3D11SamplerState*> m_ss_Undo;
};
