#pragma once
#include <string>

//==========================================================
// 調整値を JSON から読むための小さな仕組み。
//==========================================================
namespace Tuning
{
	enum class Unit
	{
		None,
		Meter,			// _m で書いていた値
		MeterPerSec,	// _mps で書いていた値
	};

	// 単位ごとの倍率。_m / _mps リテラルと同じ値になるので、縮尺を変えてもここは触らなくていい
	inline float UnitScale(Unit _unit)
	{
		if (_unit == Unit::Meter)       return 1.0_m;
		if (_unit == Unit::MeterPerSec) return 1.0_mps;
		return 1.0f;
	}

	//------------------------------------------------------
	// JSON → 値
	// 読めた項目だけ上書きする。読めなかったら何もしない
	//------------------------------------------------------
	class Reader
	{
	public:
		explicit Reader(const nlohmann::json& _object) : m_object(_object) {}

		void operator()(const char* _key, float& _value, Unit _unit = Unit::None) const
		{
			const nlohmann::json* v = Find(_key);
			if (v && v->is_number()) _value = v->get<float>() * UnitScale(_unit);
		}

		void operator()(const char* _key, int& _value) const
		{
			const nlohmann::json* v = Find(_key);
			if (v && v->is_number_integer()) _value = v->get<int>();
		}

		void operator()(const char* _key, bool& _value) const
		{
			const nlohmann::json* v = Find(_key);
			if (v && v->is_boolean()) _value = v->get<bool>();
		}

		void operator()(const char* _key, std::string& _value) const
		{
			const nlohmann::json* v = Find(_key);
			if (v && v->is_string()) _value = v->get<std::string>();
		}

		// [x, y, z] で書く。要素が足りない・数値でないときは丸ごと無視
		void operator()(const char* _key, Math::Vector3& _value, Unit _unit = Unit::None) const
		{
			const nlohmann::json* v = Find(_key);
			if (!v || !v->is_array() || v->size() != 3) return;

			float xyz[3] = {};
			for (size_t i = 0; i < 3; ++i)
			{
				if (!(*v)[i].is_number()) return;
				xyz[i] = (*v)[i].get<float>() * UnitScale(_unit);
			}
			_value = Math::Vector3(xyz[0], xyz[1], xyz[2]);
		}

		// [[x,y,z], [x,y,z], ...] で書く。1つでも壊れていたら丸ごと無視(途中まで読んだ状態にしない)
		void operator()(const char* _key, std::vector<Math::Vector3>& _value, Unit _unit = Unit::None) const
		{
			const nlohmann::json* v = Find(_key);
			if (!v || !v->is_array()) return;

			std::vector<Math::Vector3> points;
			points.reserve(v->size());
			for (const auto& e : *v)
			{
				if (!e.is_array() || e.size() != 3) return;
				if (!e[0].is_number() || !e[1].is_number() || !e[2].is_number()) return;

				const float s = UnitScale(_unit);
				points.emplace_back(e[0].get<float>() * s, e[1].get<float>() * s, e[2].get<float>() * s);
			}
			_value = std::move(points);
		}

		// Visit を持つ構造体は入れ子のオブジェクトとして読む
		template<class T>
			requires requires(T& _t, Reader& _r) { _t.Visit(_r); }
		void operator()(const char* _key, T& _object) const
		{
			Reader child = Child(_key);
			_object.Visit(child);
		}

		// 入れ子のオブジェクトを読むとき用
		Reader Child(const char* _key) const
		{
			const nlohmann::json* v = Find(_key);
			return Reader((v && v->is_object()) ? *v : EmptyObject());
		}

	private:
		const nlohmann::json* Find(const char* _key) const
		{
			if (!m_object.is_object()) return nullptr;
			const auto it = m_object.find(_key);
			return (it != m_object.end()) ? &*it : nullptr;
		}

		static const nlohmann::json& EmptyObject()
		{
			static const nlohmann::json empty = nlohmann::json::object();
			return empty;
		}

		const nlohmann::json& m_object;
	};

	//------------------------------------------------------
	// 値 → JSON
	// 今の値を書き出す。メートルの項目は縮尺を戻して書くので、そのまま読み直せる
	//------------------------------------------------------
	// float をそのまま書くと 0.800000011920929 のような誤差が見えるので、小数4桁の double にして書く
	inline double RoundForJson(float _value)
	{
		constexpr double kRound = 10000.0;
		return std::round(static_cast<double>(_value) * kRound) / kRound;
	}

	class Writer
	{
	public:
		void operator()(const char* _key, float _value, Unit _unit = Unit::None)
		{
			m_object[_key] = RoundForJson(_value / UnitScale(_unit));
		}
		void operator()(const char* _key, int _value) { m_object[_key] = _value; }
		void operator()(const char* _key, bool _value) { m_object[_key] = _value; }
		void operator()(const char* _key, const std::string& _value) { m_object[_key] = _value; }
		void operator()(const char* _key, const Math::Vector3& _value, Unit _unit = Unit::None)
		{
			const float s = UnitScale(_unit);
			m_object[_key] = { RoundForJson(_value.x / s), RoundForJson(_value.y / s), RoundForJson(_value.z / s) };
		}

		void operator()(const char* _key, const std::vector<Math::Vector3>& _value, Unit _unit = Unit::None)
		{
			const float s = UnitScale(_unit);
			nlohmann::ordered_json list = nlohmann::ordered_json::array();
			for (const auto& p : _value) list.push_back({ RoundForJson(p.x / s), RoundForJson(p.y / s), RoundForJson(p.z / s) });
			m_object[_key] = list;
		}

		// Visit を持つ構造体は入れ子のオブジェクトとして書く
		template<class T>
			requires requires(T& _t, Writer& _w) { _t.Visit(_w); }
		void operator()(const char* _key, T& _object)
		{
			Writer child;
			_object.Visit(child);
			m_object[_key] = child.Result();
		}

		const nlohmann::ordered_json& Result() const { return m_object; }

	private:
		// 構造体に書いた順で出したいので ordered_json
		nlohmann::ordered_json m_object = nlohmann::ordered_json::object();
	};

	//------------------------------------------------------
	// 設定ファイルの中身を取る
	// _fileKey : ResourceConfig の configFiles に書いたキー
	// _section : ファイルの中の1区画(省略でファイル全体)
	// 登録されていない・区画が無いときは空のオブジェクト
	//------------------------------------------------------
	inline nlohmann::json LoadSection(const char* _fileKey, const char* _section = nullptr)
	{
		const auto& dataList = RES_MGR.GetDataList();
		if (!dataList || !dataList->Contains(_fileKey))
		{
			OutputDebugStringA(("[Tuning] 設定ファイルが登録されていないので既定値で動きます : "
				+ std::string(_fileKey) + "\n").c_str());
			return nlohmann::json::object();
		}

		nlohmann::json root = dataList->Get(_fileKey);
		if (!_section) return root;

		const auto it = root.find(_section);
		return (it != root.end() && it->is_object()) ? *it : nlohmann::json::object();
	}

	// 構造体へまとめて読み込む
	template<class T>
	void Load(T& _tuning, const char* _fileKey, const char* _section = nullptr)
	{
		const nlohmann::json section = LoadSection(_fileKey, _section);
		Reader reader(section);
		_tuning.Visit(reader);
	}

	// 構造体の今の値を JSON にする
	template<class T>
	nlohmann::ordered_json Dump(T& _tuning)
	{
		Writer writer;
		_tuning.Visit(writer);
		return writer.Result();
	}
}
