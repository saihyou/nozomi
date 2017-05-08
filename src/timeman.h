/*
  nozomi, a USI shogi playing engine derived from Stockfish (chess playing engin)
  Copyright (C) 2016 Yuhei Ohmori

  This code is based on Stockfish (Chess playing engin).
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef _TIMEMAN_H_
#define _TIMEMAN_H_

#include "misc.h"

class TimeManagement
{
public:
  void 
  init(const Search::LimitsType &limits, Color us, int ply);
  
  int
  optimum() const
  {
    return optimum_search_time_;
  }

  int 
  maximum() const 
  { 
    return maximum_search_time_; 
  }

  int
  elapsed() const
  {
    return int(now() - start_time_);
  }

  bool
  only_byoyomi() const
  {
    return only_byoyomi_;
  }

private:
  int optimum_search_time_;
  int maximum_search_time_;
  bool only_byoyomi_ = false;
  std::chrono::milliseconds::rep start_time_;
};

extern TimeManagement Time;

#endif
