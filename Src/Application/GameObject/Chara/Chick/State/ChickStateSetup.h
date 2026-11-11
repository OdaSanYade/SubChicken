#pragma once

void SetupChickStates();

// ステートタグ
namespace ChickTag
{
	constexpr unsigned int kIdle = 1u << 0;	// 待機
	constexpr unsigned int kFollow = 1u << 1;	// プレイヤーについていく
	constexpr unsigned int kEscape = 1u << 2;	// 元の位置へ帰る
	constexpr unsigned int kDown = 1u << 3;	// 倒れている(復帰する)
	constexpr unsigned int kCarried = 1u << 4;	// 咥えられて運ばれている
}