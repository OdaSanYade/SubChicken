#include "KdTextEncode.h"

namespace
{
	// CP932 = Shift-JIS
	// 932 を直接書くとどの表か分からなくなるので名前を付けておく
	constexpr UINT kCodePageSjis = 932;
}

// ----------------------------------------------------------
std::wstring KdText::Utf8ToWide(std::string_view _utf8)
{
	if (_utf8.empty()) { return std::wstring(); }

	const int srcLen = static_cast<int>(_utf8.size());

	// 必要な長さを問い合わせる
	const int len = MultiByteToWideChar(
		CP_UTF8, 0, _utf8.data(), srcLen, nullptr, 0);

	if (len <= 0) { return std::wstring(); }

	std::wstring out(static_cast<size_t>(len), L'\0');

	MultiByteToWideChar(
		CP_UTF8, 0, _utf8.data(), srcLen, out.data(), len);

	return out;
}

// ----------------------------------------------------------
std::string KdText::WideToSjis(std::wstring_view _wide)
{
	if (_wide.empty()) { return std::string(); }

	const int srcLen = static_cast<int>(_wide.size());

	const int len = WideCharToMultiByte(
		kCodePageSjis, 0, _wide.data(), srcLen,
		nullptr, 0, nullptr, nullptr);

	if (len <= 0) { return std::string(); }

	std::string out(static_cast<size_t>(len), '\0');

	// 変換できない文字は既定の代替文字になる
	// ロード画面のラベルは自分で書くので、ここで落ちる字は使わない
	WideCharToMultiByte(
		kCodePageSjis, 0, _wide.data(), srcLen,
		out.data(), len, nullptr, nullptr);

	return out;
}

// ----------------------------------------------------------
std::string KdText::Utf8ToSjis(std::string_view _utf8)
{
	return WideToSjis(Utf8ToWide(_utf8));
}