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

#ifndef _MOVE_PICKER_H_
#define _MOVE_PICKER_H_

#include <algorithm>
#include <cstring>

#include "move_generator.h"
#include "position.h"
#include "search.h"
#include "types.h"
#include "stats.h"

class MovePicker 
{
public:

  MovePicker(const MovePicker &) = delete;
  MovePicker(const Position &, Move, const HistoryStats &, Value);
  MovePicker(const Position &, Move, Depth, const HistoryStats &, Square);
  MovePicker
  (
    const Position &,
    Move,
    Depth,
    const HistoryStats &,
    const CounterMoveStats &,
    const CounterMoveStats &,
    Move,
    SearchStack *
  );

  MovePicker &
  operator=(const MovePicker &) = delete;

  Move
  next_move();

private:
  template<GenType> void
  score();

  void
  generate_next_stage();

  ExtMove *
  begin()
  {
    return moves_;
  }

  ExtMove *
  end()
  {
    return end_moves_;
  }

  const Position         &pos_;
  const HistoryStats     &history_;
  const CounterMoveStats *counter_move_history_;
  const CounterMoveStats *followup_move_history_;
  SearchStack            *ss_;
  Move                    countermove_;
  Depth                   depth_;
  Move                    tt_move_;
  ExtMove                 killers_[3];
  Square                  recapture_square_;
  Value                   capture_threshold_;
  int                     stage_;
  ExtMove                *end_quiets_;
  ExtMove                *end_bad_captures_ = moves_ + kMaxMoves - 1;
  ExtMove                 moves_[kMaxMoves];
  ExtMove                *cur_       = moves_;
  ExtMove                *end_moves_ = moves_;
};

#endif
