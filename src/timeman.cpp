/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

  nozomi is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  nozomi is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "search.h"
#include "timeman.h"
#include "usi.h"

TimeManagement Time;

namespace
{
enum TimeType
{
  kOptimumTime,
  kMaxTime
};

constexpr int kMoveHorizon = 160;
constexpr double kMaxRatio = 7.3;
constexpr double kStealRatio = 0.34;

double
move_importance(int ply)
{
  const double kXScale = 6.85;
  const double kXShift = 64.5;
  const double kSkew   = 0.171;

  return pow((1 + exp(ply - kXShift) / kXScale), -kSkew) + DBL_MIN;
}

template<TimeType T>
int
remaining(int my_time, int moves_to_go, int ply, int slow_mover)
{
  const double kTMaxRatio = (T == kOptimumTime ? 1 : kMaxRatio);
  const double kTStealRatio = (T == kOptimumTime ? 0 : kStealRatio);

  double importance = move_importance(ply) * slow_mover / 100;
  double other_moves_importance = 0;

  for (int i = 1; i < moves_to_go; ++i)
    other_moves_importance += move_importance(ply + 2 * i);
  
  double ratio1 = (kTMaxRatio * importance) / (kTMaxRatio * importance + other_moves_importance);
  double ratio2 = (importance + kTStealRatio * other_moves_importance) / (importance + other_moves_importance);

  return int(my_time * std::min(ratio1, ratio2));
}
}

void
TimeManagement::init(const Search::LimitsType &limits, Color us, int ply)
{
  start_time_ = limits.start_time;
  optimum_search_time_ = maximum_search_time_ = limits.time[us];
  const int kMaxMTG = std::min(258 - ply, kMoveHorizon);
  const int min_thinking_time = 20;
  const int move_overhead = 30;
  const int slow_mover = 50;

  int byoyomi = limits.byoyomi - Options["ByoyomiMargin"];
  if (limits.byoyomi > 0)
  {
    if (limits.time[us] == 0)
      only_byoyomi_ = true;

    optimum_search_time_ += byoyomi;
    maximum_search_time_ += byoyomi;
  }
  if (only_byoyomi_)
    return;

  for (int hyp_mtg = 1; hyp_mtg <= kMaxMTG; ++hyp_mtg)
  {
    int hyp_my_time = limits.time[us] + limits.inc[us] * (hyp_mtg - 1) - move_overhead * (2 + std::min(hyp_mtg, 40)) - Options["ByoyomiMargin"];
    hyp_my_time = std::max(hyp_my_time, 0);
    int t1 = min_thinking_time + remaining<kOptimumTime>(hyp_my_time, hyp_mtg, ply, slow_mover);
    int t2 = min_thinking_time + remaining<kMaxTime>(hyp_my_time, hyp_mtg, ply, slow_mover);

    optimum_search_time_ = std::min(t1, optimum_search_time_);
    maximum_search_time_ = std::min(t2, maximum_search_time_);
  }

  if (Options["USI_Ponder"])
    optimum_search_time_ += optimum_search_time_ / 4;
}
