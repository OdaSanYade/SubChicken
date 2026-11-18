#include "Framework/KdFramework.h"
#include "KdShaderManager.h"

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 全てのシェーダーオブジェクトの生成
// ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== ===== =====
// 描画するシェーダー本体の作成（メモリ確保）
// 全ての描画に必要であろうカメラ・ライトの定数バッファの作成
// 描画に関する様々なステートの作成（実行中に切り替える用：切り替えるたびに新しく作るのは無駄
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::Init()
{
	//============================================
	// パイプラインステート関係
	//============================================
	//深度ステンシルステート作成（奥行情報の使い方・手前にあるものを無視して描画したりできる
	m_depthStencilStates[(int)KdDepthStencilState::ZEnable]			= KdDirect3D::Instance().CreateDepthStencilState(true, true);
	m_depthStencilStates[(int)KdDepthStencilState::ZWriteDisable]	= KdDirect3D::Instance().CreateDepthStencilState(true, false);
	m_depthStencilStates[(int)KdDepthStencilState::ZDisable]		= KdDirect3D::Instance().CreateDepthStencilState(false, false);

	// 初期深度ステートの設定
	KdDirect3D::Instance().WorkDevContext()->OMSetDepthStencilState(m_depthStencilStates[(int)KdDepthStencilState::ZEnable], 0);

	// ラスタライザステート作成（ポリゴンの任意の面の描画を省略できる、処理を軽減する目的で運用する事が多い
	m_rasterizerStates[(int)KdRasterizerState::CullNone]	= KdDirect3D::Instance().CreateRasterizerState(D3D11_CULL_NONE, D3D11_FILL_SOLID, true, false);
	m_rasterizerStates[(int)KdRasterizerState::CullFront]	= KdDirect3D::Instance().CreateRasterizerState(D3D11_CULL_FRONT, D3D11_FILL_SOLID, true, false);
	m_rasterizerStates[(int)KdRasterizerState::CullBack]	= KdDirect3D::Instance().CreateRasterizerState(D3D11_CULL_BACK, D3D11_FILL_SOLID, true, false);

	// 初期ラスタライザステートの設定
	KdDirect3D::Instance().WorkDevContext()->RSSetState(m_rasterizerStates[(int)KdRasterizerState::CullBack]);

	// ブレンドステート作成（ピクセルの最終色を決めるときに既に塗られている色と、どう合成するのかの選択ができる
	m_blendStates[(int)KdBlendState::Alpha]	= KdDirect3D::Instance().CreateBlendState(KdBlendMode::Alpha);
	m_blendStates[(int)KdBlendState::Add]	= KdDirect3D::Instance().CreateBlendState(KdBlendMode::Add);

	// 初期ブレンドステートの設定
	KdDirect3D::Instance().WorkDevContext()->OMSetBlendState(m_blendStates[(int)KdBlendState::Alpha], Math::Color(0, 0, 0, 0), 0xFFFFFFFF);

	// サンプラーステート作成（テクスチャのピクセル色を取得する際にどのような補間や扱いで取得するかを選択できる
	m_samplerStates[(int)KdSamplerState::Anisotropic_Wrap]	= KdDirect3D::Instance().CreateSamplerState(KdSamplerFilterMode::Anisotropic, 4, KdSamplerAddressingMode::Wrap, false);
	m_samplerStates[(int)KdSamplerState::Anisotropic_Clamp] = KdDirect3D::Instance().CreateSamplerState(KdSamplerFilterMode::Anisotropic, 4, KdSamplerAddressingMode::Clamp, false);
	m_samplerStates[(int)KdSamplerState::Linear_Clamp]		= KdDirect3D::Instance().CreateSamplerState(KdSamplerFilterMode::Linear, 0, KdSamplerAddressingMode::Clamp, false);
	m_samplerStates[(int)KdSamplerState::Linear_Clamp_Cmp]	= KdDirect3D::Instance().CreateSamplerState(KdSamplerFilterMode::Linear, 0, KdSamplerAddressingMode::Clamp, true);
	m_samplerStates[(int)KdSamplerState::Point_Wrap]		= KdDirect3D::Instance().CreateSamplerState(KdSamplerFilterMode::Point, 0, KdSamplerAddressingMode::Wrap, false);
	m_samplerStates[(int)KdSamplerState::Point_Clamp]		= KdDirect3D::Instance().CreateSamplerState(KdSamplerFilterMode::Point, 0, KdSamplerAddressingMode::Clamp, false);
	
	// 初期サンプラーステートの設定
	if (m_pixelArtStyle)
	{
		KdDirect3D::Instance().WorkDevContext()->PSSetSamplers(0, 1, &m_samplerStates[(int)KdSamplerState::Point_Wrap]);
	}
	else
	{
		KdDirect3D::Instance().WorkDevContext()->PSSetSamplers(0, 1, &m_samplerStates[(int)KdSamplerState::Anisotropic_Wrap]);
	}
}

//==========================
//
// パイプラインステートの変更
//
//==========================

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 深度ステンシルステートの変更（現行と同じステートの場合はキャンセル
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::ChangeDepthStencilState(KdDepthStencilState stateId)
{
	ID3D11DepthStencilState* pNowDs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->OMGetDepthStencilState(&pNowDs, 0);

	if (pNowDs != m_depthStencilStates[(int)stateId])
	{
		KdDirect3D::Instance().WorkDevContext()->OMSetDepthStencilState(m_depthStencilStates[(int)stateId], 0);
	}

	m_ds_Undo.push(pNowDs);

	KdSafeRelease(pNowDs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// 深度ステンシルステートを変更直前のステートに戻す
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::UndoDepthStencilState()
{
	if (!m_ds_Undo.size()) { return; }

	ID3D11DepthStencilState* pNowDs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->OMGetDepthStencilState(&pNowDs, 0);

	if (pNowDs != m_ds_Undo.top())
	{
		KdDirect3D::Instance().WorkDevContext()->OMSetDepthStencilState(m_ds_Undo.top(), 0);
	}

	m_ds_Undo.pop();

	KdSafeRelease(pNowDs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ラスタライザステートの変更（現行と同じステートの場合はキャンセル
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::ChangeRasterizerState(KdRasterizerState stateId)
{
	ID3D11RasterizerState* pNowRs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->RSGetState(&pNowRs);

	if (pNowRs != m_rasterizerStates[(int)stateId])
	{
		KdDirect3D::Instance().WorkDevContext()->RSSetState(m_rasterizerStates[(int)stateId]);
	}

	m_rs_Undo.push(pNowRs);

	KdSafeRelease(pNowRs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ラスタライザステンシルステートを変更直前のステートに戻す
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::UndoRasterizerState()
{
	if (!m_rs_Undo.size()) { return; }

	ID3D11RasterizerState* pNowRs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->RSGetState(&pNowRs);

	if (pNowRs != m_rs_Undo.top())
	{
		KdDirect3D::Instance().WorkDevContext()->RSSetState(m_rs_Undo.top());
	}

	m_rs_Undo.pop();

	KdSafeRelease(pNowRs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ブレンドステートの変更（現行と同じステートの場合はキャンセル
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::ChangeBlendState(KdBlendState stateId)
{
	ID3D11BlendState* pNowBs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->OMGetBlendState(&pNowBs, nullptr, nullptr);

	if (pNowBs != m_blendStates[(int)stateId])
	{
		KdDirect3D::Instance().WorkDevContext()->OMSetBlendState(m_blendStates[(int)stateId], Math::Color(0, 0, 0, 0), 0xFFFFFFFF);
	}

	m_bs_Undo.push(pNowBs);

	KdSafeRelease(pNowBs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// ブレンドステートを変更直前のステートに戻す
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::UndoBlendState()
{
	if (!m_bs_Undo.size()) { return; }

	ID3D11BlendState* pNowBs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->OMGetBlendState(&pNowBs, nullptr, nullptr);
	
	if (pNowBs != m_bs_Undo.top())
	{
		KdDirect3D::Instance().WorkDevContext()->OMSetBlendState(m_bs_Undo.top(), Math::Color(0, 0, 0, 0), 0xFFFFFFFF);
	}

	m_bs_Undo.pop();

	KdSafeRelease(pNowBs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// サンプラーステートの変更（現行と同じステートの場合はキャンセル
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::ChangeSamplerState(KdSamplerState stateId, int slot)
{
	ID3D11SamplerState* pNowSs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->PSGetSamplers(slot, 1, &pNowSs);

	if (pNowSs != m_samplerStates[(int)stateId])
	{
		KdDirect3D::Instance().WorkDevContext()->PSSetSamplers(slot, 1, &m_samplerStates[(int)stateId]);
	}

	m_ss_Undo.push(pNowSs);

	KdSafeRelease(pNowSs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// サンプラーステートを変更直前のステートに戻す
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
void KdShaderManager::UndoSamplerState(int slot)
{
	if (!m_ss_Undo.size()) { return; }

	ID3D11SamplerState* pNowSs = nullptr;
	KdDirect3D::Instance().WorkDevContext()->PSGetSamplers(0, 1, &pNowSs);

	if (pNowSs != m_ss_Undo.top())
	{
		KdDirect3D::Instance().WorkDevContext()->PSSetSamplers(slot, 1, &m_ss_Undo.top());
	}

	m_ss_Undo.pop();

	KdSafeRelease(pNowSs);
}

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// パラメータの解放：シェーダー本体・共通の定数バッファ・各パイプラインステート
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
#pragma warning(disable:4239)
void KdShaderManager::Release()
{
	//深度ステンシルステート開放
	for (auto& state : m_depthStencilStates)
	{
		KdSafeRelease(state);
	}

	auto ds = std::stack<ID3D11DepthStencilState*>();
	m_ds_Undo.swap(ds);

	// ラスタライザステート解放
	for (auto& state : m_rasterizerStates)
	{
		KdSafeRelease(state);
	}

	auto rs = std::stack<ID3D11RasterizerState*>();
	m_rs_Undo.swap(rs);

	// ブレンドステート解放
	for (auto& state : m_blendStates)
	{
		KdSafeRelease(state);
	}

	auto bs = std::stack<ID3D11BlendState*>();
	m_bs_Undo.swap(bs);

	// サンプラーステート解放
	for (auto& state : m_samplerStates)
	{
		KdSafeRelease(state);
	}

	auto ss = std::stack<ID3D11SamplerState*>();
	m_ss_Undo.swap(ss);
}
#pragma warning(default:4239)
