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

void 
TimeManagement::pv_instability(double best_move_changes) 
{
  unstable_pv_factor_ = 1 + best_move_changes;
}

void 
TimeManagement::init(const Search::LimitsType &limits, Color us)
{
  start_time_          = limits.start_time;
  unstable_pv_factor_  = 1;
  optimum_search_time_ = limits.time[us];
  maximum_search_time_ = limits.time[us];

  const int optimum_move_factor = 35;
  const int maximum_move_factor = 10;
  optimum_search_time_ = optimum_search_time_ / optimum_move_factor;
  maximum_search_time_ = maximum_search_time_ / maximum_move_factor;

  int byoyomi = limits.byoyomi - Options["ByoyomiMargin"];
  if (limits.byoyomi > 0)
  {
    optimum_search_time_ += byoyomi;
    maximum_search_time_ += byoyomi;

    if (optimum_search_time_ < byoyomi)
      optimum_search_time_ += byoyomi;

    if (maximum_search_time_ < byoyomi)
      maximum_search_time_ += byoyomi;
  }

  if (limits.inc[us] > 0)
  {
    optimum_search_time_ += limits.inc[us];
    maximum_search_time_ += limits.inc[us];
  }

  if (optimum_search_time_ < 1000)
    optimum_search_time_ = 900;
  if (maximum_search_time_ < 1000)
    maximum_search_time_ = 900;
}
