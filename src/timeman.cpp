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
TimeManagement::init(const Search::LimitsType &limits, Color us, int ply)
{
  start_time_          = limits.start_time;
  optimum_search_time_ = limits.time[us];
  maximum_search_time_ = limits.time[us];

  const int optimum_move_factor = 40;
  const int maximum_move_factor = 10;
  optimum_search_time_ = optimum_search_time_ / optimum_move_factor;
  maximum_search_time_ = maximum_search_time_ / maximum_move_factor;

  if (Options["USI_Ponder"])
    optimum_search_time_ += optimum_search_time_ / 4;

  int byoyomi = limits.byoyomi - Options["ByoyomiMargin"];
  if (limits.byoyomi > 0)
  {
    if (limits.time[us] == 0)
      only_byoyomi_ = true;

    optimum_search_time_ += byoyomi;
    maximum_search_time_ += byoyomi;
  }

  if (limits.inc[us] > 0)
  {
    optimum_search_time_ += limits.inc[us];
    maximum_search_time_ += limits.inc[us];
  }
}
