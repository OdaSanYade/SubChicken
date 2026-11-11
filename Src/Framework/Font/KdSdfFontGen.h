#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

//===================================================
//
// SDF の生成(計算だけ。Windows にも D3D にも依存しない)
//
// 白黒の字形(1=字の中)から、輪郭までの距離を 0〜1 に詰めた画像を作る。
//   0.5 がちょうど輪郭。内側ほど大きく、外側ほど小さい。
//   ±_spread ピクセルで 1 / 0 に張り付く。
//
// 距離の測り方は2つ。
//   Euclid    : ふつうの直線距離。曲線の多い書体向け。縁取りの角が丸くなる。
//   Chebyshev : マス目の距離(縦横斜めを同じ1歩と数える)。ドット字向け。
//               四角いドットの角が拡大しても丸まらず、縁取りも四角く付く。
//
//===================================================
namespace KdSdfGen
{
	enum class Metric : uint8_t
	{
		Euclid,
		Chebyshev,
	};

	//===================================================
	// ユークリッド距離(Felzenszwalb & Huttenlocher)
	//===================================================
	// 2乗距離を1次元ずつ求める。縦と横で2回通すと2次元になる。
	namespace Detail
	{
		constexpr double kInf = 1e20;

		inline void Edt1D(const double* _f, int _n, double* _d, int* _v, double* _z)
		{
			int k = 0;
			_v[0] = 0;
			_z[0] = -kInf;
			_z[1] = kInf;

			for (int q = 1; q < _n; ++q)
			{
				auto inter = [&](int _r)
					{
						return ((_f[q] + static_cast<double>(q) * q)
							- (_f[_r] + static_cast<double>(_r) * _r))
							/ (2.0 * q - 2.0 * _r);
					};

				double s = inter(_v[k]);
				while (s <= _z[k])
				{
					--k;
					s = inter(_v[k]);
				}
				++k;
				_v[k] = q;
				_z[k] = s;
				_z[k + 1] = kInf;
			}

			k = 0;
			for (int q = 0; q < _n; ++q)
			{
				while (_z[k + 1] < q) ++k;
				const double r = static_cast<double>(q - _v[k]);
				_d[q] = r * r + _f[_v[k]];
			}
		}

		// _target が 1 の画素までの距離(画素単位)
		inline void DistEuclid(const std::vector<uint8_t>& _target, int _w, int _h, std::vector<float>& _out)
		{
			const int n = std::max(_w, _h);
			std::vector<double> grid(static_cast<size_t>(_w) * _h);
			std::vector<double> f(n), d(n), z(n + 1);
			std::vector<int> v(n);

			for (size_t i = 0; i < grid.size(); ++i) grid[i] = _target[i] ? 0.0 : kInf;

			// 縦
			for (int x = 0; x < _w; ++x)
			{
				for (int y = 0; y < _h; ++y) f[y] = grid[static_cast<size_t>(y) * _w + x];
				Edt1D(f.data(), _h, d.data(), v.data(), z.data());
				for (int y = 0; y < _h; ++y) grid[static_cast<size_t>(y) * _w + x] = d[y];
			}
			// 横
			for (int y = 0; y < _h; ++y)
			{
				double* row = grid.data() + static_cast<size_t>(y) * _w;
				for (int x = 0; x < _w; ++x) f[x] = row[x];
				Edt1D(f.data(), _w, d.data(), v.data(), z.data());
				for (int x = 0; x < _w; ++x) row[x] = d[x];
			}

			_out.resize(grid.size());
			for (size_t i = 0; i < grid.size(); ++i)
				_out[i] = static_cast<float>(std::sqrt(std::min(grid[i], 1e12)));
		}

		//===================================================
		// チェビシェフ距離
		//===================================================
		// 横に1次元の距離を出し、縦は「縦のずれ」と「その行の横の距離」の大きい方を取る。
		// 遠くは使わない(_cap で打ち切る)ので、縦は近傍だけ見れば足りる。
		inline void DistChebyshev(const std::vector<uint8_t>& _target, int _w, int _h, int _cap,
			std::vector<float>& _out)
		{
			const float cap = static_cast<float>(_cap);
			std::vector<float> g(static_cast<size_t>(_w) * _h);

			// 横
			for (int y = 0; y < _h; ++y)
			{
				const size_t row = static_cast<size_t>(y) * _w;

				float dist = cap;
				for (int x = 0; x < _w; ++x)
				{
					dist = _target[row + x] ? 0.0f : std::min(dist + 1.0f, cap);
					g[row + x] = dist;
				}

				dist = cap;
				for (int x = _w - 1; x >= 0; --x)
				{
					dist = _target[row + x] ? 0.0f : std::min(dist + 1.0f, cap);
					g[row + x] = std::min(g[row + x], dist);
				}
			}

			// 縦
			_out.assign(g.size(), cap);
			for (int x = 0; x < _w; ++x)
			{
				for (int y = 0; y < _h; ++y)
				{
					float best = cap;
					const int y0 = std::max(0, y - _cap);
					const int y1 = std::min(_h - 1, y + _cap);

					for (int yy = y0; yy <= y1; ++yy)
					{
						const float dy = static_cast<float>(std::abs(yy - y));
						best = std::min(best, std::max(dy, g[static_cast<size_t>(yy) * _w + x]));
					}
					_out[static_cast<size_t>(y) * _w + x] = best;
				}
			}
		}
	}

	//===================================================
	// 距離場を作る
	//===================================================
	//  _inside : 1=字の中 0=外。大きさ _w x _h。周りに _spread 以上の余白があること。
	//  _out    : 0〜255。128 付近が輪郭。
	//
	// 距離は「画素の中心」どうしで測るので、0.5 画素引いて「画素の縁」までにする。
	// こうすると、隣り合う中と外の画素の真ん中でちょうど 0.5 を跨ぐ。
	inline void BuildField(const std::vector<uint8_t>& _inside, int _w, int _h, int _spread,
		Metric _metric, std::vector<uint8_t>& _out)
	{
		const size_t n = static_cast<size_t>(_w) * _h;
		_out.assign(n, 0);
		if (n == 0 || _spread <= 0) return;

		std::vector<uint8_t> outside(n);
		for (size_t i = 0; i < n; ++i) outside[i] = _inside[i] ? 0 : 1;

		std::vector<float> toInside;	// 外の画素 → 一番近い中の画素
		std::vector<float> toOutside;	// 中の画素 → 一番近い外の画素

		// 測り方の表。足すならここに1つ。
		using DistFn = void (*)(const std::vector<uint8_t>&, int, int, int, std::vector<float>&);
		static const DistFn kDist[] = {
			[](const std::vector<uint8_t>& _t, int _ww, int _hh, int, std::vector<float>& _o)
				{ Detail::DistEuclid(_t, _ww, _hh, _o); },		// Euclid
			[](const std::vector<uint8_t>& _t, int _ww, int _hh, int _c, std::vector<float>& _o)
				{ Detail::DistChebyshev(_t, _ww, _hh, _c, _o); },	// Chebyshev
		};

		const DistFn dist = kDist[static_cast<size_t>(_metric)];
		const int cap = _spread + 2;
		dist(_inside, _w, _h, cap, toInside);
		dist(outside, _w, _h, cap, toOutside);

		const float inv = 1.0f / (2.0f * static_cast<float>(_spread));

		for (size_t i = 0; i < n; ++i)
		{
			const float signedDist = _inside[i]
				? (toOutside[i] - 0.5f)		// 中 : 正
				: -(toInside[i] - 0.5f);	// 外 : 負

			const float v = std::clamp(0.5f + signedDist * inv, 0.0f, 1.0f);
			_out[i] = static_cast<uint8_t>(std::lround(v * 255.0f));
		}
	}

	//===================================================
	// 棚詰め
	//===================================================
	// 背の高い順に、左から右へ棚に並べる。字の大きさはほぼ揃っているので、これで十分詰まる。
	struct PackItem
	{
		int w = 0, h = 0;
		int x = 0, y = 0;
	};

	// 並べた後の全体の高さを返す。_items の順番は変えない。
	inline int ShelfPack(std::vector<PackItem>& _items, int _atlasW, int _gap)
	{
		std::vector<size_t> order(_items.size());
		std::iota(order.begin(), order.end(), size_t(0));
		std::stable_sort(order.begin(), order.end(),
			[&](size_t _a, size_t _b) { return _items[_a].h > _items[_b].h; });

		int x = _gap;
		int y = _gap;
		int shelfH = 0;

		for (size_t idx : order)
		{
			PackItem& it = _items[idx];
			if (it.w <= 0 || it.h <= 0) continue;

			if (x + it.w + _gap > _atlasW)
			{
				x = _gap;
				y += shelfH + _gap;
				shelfH = 0;
			}

			it.x = x;
			it.y = y;
			x += it.w + _gap;
			shelfH = std::max(shelfH, it.h);
		}

		return y + shelfH + _gap;
	}
}
