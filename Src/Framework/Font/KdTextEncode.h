#pragma once
#include <string>

//=====================================================================
// 文字コード変換
//
// ソースはUTF-8で保存しているが、KdFontManager は
// SHIFTJIS_CHARSET と isSJIS() を前提に組まれている。
// そのまま渡すとUTF-8のバイト列を2バイト文字と誤判定して化けるので
// 描画の直前でここを通す。
//=====================================================================
namespace KdText
{
	// UTF-8 → ワイド文字
	std::wstring Utf8ToWide(std::string_view _utf8);

	// ワイド文字 → Shift-JIS(CP932)
	std::string WideToSjis(std::wstring_view _wide);

	// UTF-8 → Shift-JIS(CP932)
	// KdFontManager へ文字列を渡す前に必ず通す
	std::string Utf8ToSjis(std::string_view _utf8);
}