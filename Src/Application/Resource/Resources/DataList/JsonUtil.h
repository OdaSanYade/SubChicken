#pragma once
#include <string>

//==========================================================
// JsonUtil
// 設定ファイルから値を1つだけ取りたい、という場面用の小物。
//
// nlohmann::json の get<T>() は型が合わないと例外を投げるので、
// 設定ファイルを手で書き間違えただけでゲームが落ちてしまう。
// ここを通せば「読めなかったら既定値」で済む。
//==========================================================
namespace JsonUtil
{
	//------------------------------------------------------
	// JSON Pointer 形式で値を取る
	//   例) ValueAt<int>(root, "/jsonWatcher/pollIntervalMs", 300)
	// 途中のキーが無い・型が違う・ポインタの書式が変、のどれでも _fallback を返す
	//------------------------------------------------------
	template<class T>
	T ValueAt(const nlohmann::json& _root, const std::string& _pointer, const T& _fallback)
	{
		// json_pointer は書式が変だと構築時点で例外を投げるし、
		// get<T>() も型違いで投げる。どちらも「設定が読めなかった」扱いにしたいので
		// ここだけはまとめて try で受ける
		try
		{
			const nlohmann::json::json_pointer ptr(_pointer);
			if (!_root.contains(ptr)) { return _fallback; }
			return _root.at(ptr).get<T>();
		}
		catch (const nlohmann::json::exception&)
		{
			return _fallback;
		}
	}

	//------------------------------------------------------
	// オブジェクト直下のキーを読む。読めたら true
	// 読めなかったときは _out をいじらない(呼ぶ側で入れておいた既定値が残る)
	//------------------------------------------------------
	template<class T>
	bool TryRead(const nlohmann::json& _object, const char* _key, T& _out)
	{
		if (!_object.is_object()) { return false; }

		const auto it = _object.find(_key);
		if (it == _object.end()) { return false; }

		try
		{
			_out = it->get<T>();
			return true;
		}
		catch (const nlohmann::json::exception&)
		{
			return false;
		}
	}
}
