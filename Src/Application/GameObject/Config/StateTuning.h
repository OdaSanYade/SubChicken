#pragma once
#include <initializer_list>
#include <string>
#include "Tuning.h"
#include "../../GameSystem/StateMachine/StateMachineSystem.h"

//==========================================================
// StateTuning
// ステートの params / minTime / buffer を JSON で上書きする。
//
// Setup*States() の中で、ステートを全部登録したあと・Validate の前に呼ぶ。
// (GameScale::ApplyToStateParams より前に呼ぶこと。順番を逆にすると縮尺が二重に掛かる)
//
// JSON の形
//   {
//     "states": {
//       "Attack1": {
//         "minTime": 0.12,
//         "buffer": 0.3,
//         "params": { "damage": 15.0, "reach": 15.0, "anim": "ClawAttack" }
//       }
//     }
//   }
//
// ・JSON に書いたステート・キーだけ上書きされる。書いていないものはコードの値のまま
// ・"reach" のようにメートルで持つキーは、コード側の単位表(ParamUnits)に書いておく。
//   JSON には縮尺前の値を書けばいい
//==========================================================
namespace StateTuning
{
	// キーごとの単位。キーの意味はコードが決めるので、表もコード側に置く
	struct ParamUnits
	{
		std::initializer_list<const char*> meter;
		std::initializer_list<const char*> meterPerSec;

		Tuning::Unit Find(std::string_view _key) const
		{
			for (const char* k : meter)       { if (_key == k) return Tuning::Unit::Meter; }
			for (const char* k : meterPerSec) { if (_key == k) return Tuning::Unit::MeterPerSec; }
			return Tuning::Unit::None;
		}
	};

	inline void Report(const std::string& _message)
	{
		OutputDebugStringA(("[StateTuning] " + _message + "\n").c_str());
	}

	//------------------------------------------------------
	// ステート1つ分を上書きする
	//------------------------------------------------------
	inline void ApplyState(StateDesc& _desc, const nlohmann::json& _entry, const ParamUnits& _units)
	{
		if (const auto it = _entry.find("minTime"); it != _entry.end() && it->is_number())
			_desc.minTime = it->get<float>();

		if (const auto it = _entry.find("buffer"); it != _entry.end() && it->is_number())
			_desc.bufferTime = it->get<float>();

		const auto params = _entry.find("params");
		if (params == _entry.end() || !params->is_object()) return;

		for (auto p = params->begin(); p != params->end(); ++p)
		{
			const std::string& key = p.key();
			const auto& v = p.value();

			// bool は 0/1 の float で持つ(StateParams の決まり)
			if (v.is_boolean())     _desc.params.SetFloat(key, v.get<bool>() ? 1.0f : 0.0f);
			else if (v.is_number()) _desc.params.SetFloat(key, v.get<float>() * Tuning::UnitScale(_units.Find(key)));
			else if (v.is_string()) _desc.params.SetString(key, v.get<std::string>());
			else Report("'" + _desc.name + "' の " + key + " は数値か文字列で書いてください");
		}
	}

	//------------------------------------------------------
	// JSON 全体を当てる
	//------------------------------------------------------
	inline void Apply(StateMachineSystem& _sys, const nlohmann::json& _root, const ParamUnits& _units)
	{
		const auto states = _root.find("states");
		if (states == _root.end() || !states->is_object()) return;

		for (auto it = states->begin(); it != states->end(); ++it)
		{
			if (!it.value().is_object()) continue;

			// 名前の打ち間違いは黙って飛ばすと気づけないので出しておく
			StateDesc* desc = _sys.GetMutable(_sys.Find(std::string_view(it.key())));
			if (!desc)
			{
				Report("[" + _sys.GetName() + "] '" + it.key() + "' というステートはありません");
				continue;
			}

			ApplyState(*desc, it.value(), _units);
		}
	}

	// 設定ファイルのキーを指定する版
	inline void Apply(StateMachineSystem& _sys, const char* _fileKey, const ParamUnits& _units)
	{
		Apply(_sys, Tuning::LoadSection(_fileKey), _units);
	}

	//------------------------------------------------------
	// 今の定義を JSON にする(初期ファイルを作るとき用)
	//------------------------------------------------------
	// 登録した順に並べたいので ordered_json で返す
	inline nlohmann::ordered_json Dump(const StateMachineSystem& _sys, const ParamUnits& _units)
	{
		auto round4 = [](float _v) { return Tuning::RoundForJson(_v); };

		nlohmann::ordered_json ordered = nlohmann::ordered_json::object();

		for (size_t i = 0; i < _sys.GetStateCount(); ++i)
		{
			const StateDesc* d = _sys.Get(static_cast<uint16_t>(i));
			if (!d) continue;

			nlohmann::ordered_json entry = nlohmann::ordered_json::object();
			if (d->minTime > 0.0f) entry["minTime"] = round4(d->minTime);
			if (d->bufferTime >= 0.0f) entry["buffer"] = round4(d->bufferTime);

			nlohmann::ordered_json params = nlohmann::ordered_json::object();
			const StateParams& p = d->params;
			for (size_t s = 0; s < p.GetStringCount(); ++s) params[p.GetStringName(s)] = p.GetStringAt(s);
			for (size_t f = 0; f < p.GetFloatCount(); ++f)
			{
				const char* key = p.GetFloatName(f);
				params[key] = round4(p.GetFloatAt(f) / Tuning::UnitScale(_units.Find(key)));
			}
			if (!params.empty()) entry["params"] = params;

			if (!entry.empty()) ordered[d->name] = entry;
		}

		nlohmann::ordered_json root = nlohmann::ordered_json::object();
		root["states"] = ordered;
		return root;
	}
}
