#pragma once

#include "KdInputGamepad.h"

#include <string>
#include <vector>

// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
// KdInputKeyNames
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// JSON等の人間が読み書きする設定ファイルの中で、VK_SPACEやXINPUT_GAMEPAD_A といった
// 生のコードではなく "Space" "A" のような文字列でキー／ボタンを指定できるようにするための
// 名前⇔コード変換テーブル
// ----- ----- ----- ----- ----- ----- ----- ----- ----- -----
// このヘッダー自体はJSONライブラリに依存しない（文字列⇔コード変換のみを提供）
// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// ///// /////
namespace KdInputKeyNames
{
	// ----- キーボード（マウスボタンも含む） -----

	// 未知の名前が渡された場合はassertし、0を返す
	int KeyNameToVK(const std::string& name);

	// 未知のVKコードが渡された場合はassertし、空文字を返す
	std::string VKToKeyName(int vkCode);

	// ----- ゲームパッドボタン -----

	// 未知の名前が渡された場合はassertし、0を返す
	WORD ButtonNameToMask(const std::string& name);

	// mask内の全ビットを対応する名前に分解して返す（複数ボタンをORした場合に対応）
	std::vector<std::string> MaskToNames(WORD mask);

	// ----- スティック／トリガー -----

	KdGamepadStick StickNameToEnum(const std::string& name);
	std::string StickEnumToName(KdGamepadStick stick);

	KdGamepadTrigger TriggerNameToEnum(const std::string& name);
	std::string TriggerEnumToName(KdGamepadTrigger trigger);
}
