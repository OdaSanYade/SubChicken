#pragma once

struct BattleResult
{
	//------------------------------------------
	// 成績
	//------------------------------------------
	bool  cleared = false;		// 全滅させたか
	float elapsedSec = 0.0f;	// クリアまでの時間
	float lifeRatio = 1.0f;		// 残りHPの割合(0〜1)
	int   maxCombo = 0;			// 最大コンボ数
	int   killed = 0;
	int   total = 0;

	//------------------------------------------
	// 評価の基準
	//------------------------------------------
	struct Standard
	{
		float targetSec = 90.0f;	// この時間なら満点
		float limitSec = 240.0f;	// これを超えると0点
		int   targetCombo = 25;		// このコンボ数で満点
	};

	//------------------------------------------
	// ランク
	//------------------------------------------
	enum Rank : int
	{
		RankD = 0,
		RankC = 1,
		RankB = 2,
		RankA = 3,
		RankS = 4,
	};

	// 各項目の点(0〜1)。リザルトの表示にも使う。
	float TimeScore(const Standard& _std) const
	{
		if (elapsedSec <= _std.targetSec) return 1.0f;

		const float span = std::max(_std.limitSec - _std.targetSec, 1.0f);
		return std::clamp(1.0f - (elapsedSec - _std.targetSec) / span, 0.0f, 1.0f);
	}

	float LifeScore() const
	{
		return std::clamp(lifeRatio, 0.0f, 1.0f);
	}

	float ComboScore(const Standard& _std) const
	{
		return std::clamp(static_cast<float>(maxCombo)
			/ static_cast<float>(std::max(_std.targetCombo, 1)), 0.0f, 1.0f);
	}

	// 総合点(0〜1)。
	// 残りHPを重くするのは、被弾を避ける遊びを主にしたいから。
	// 配点を変えると、プレイヤーの動き方がそのまま変わる。
	float TotalScore(const Standard& _std) const
	{
		return TimeScore(_std) * 0.35f
			+ LifeScore() * 0.25f
			+ ComboScore(_std) * 0.40f;
	}

	Rank GetRank(const Standard& _std) const
	{
		// 死んだら評価しない。負けは負け。
		if (!cleared) return RankD;

		const float s = TotalScore(_std);

		// 分岐ではなくテーブル引き。閾値の調整はここだけ見ればよい。
		struct Entry { float line; Rank rank; };

		static const Entry kTable[] = {
			{ 0.90f, RankS },
			{ 0.75f, RankA },
			{ 0.55f, RankB },
			{ 0.35f, RankC },
		};

		for (const auto& e : kTable)
		{
			if (s >= e.line) return e.rank;
		}
		return RankD;
	}

	//------------------------------------------
	// 満点の判定(項目ごとの印に使う)
	//------------------------------------------
	bool IsPerfectTime(const Standard& _std) const { return elapsedSec <= _std.targetSec; }
	bool IsPerfectLife() const { return lifeRatio >= 0.999f; }
	bool IsPerfectCombo(const Standard& _std) const { return maxCombo >= _std.targetCombo; }
};