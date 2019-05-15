﻿/*
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
#include "stats.h"
#include "types.h"

class MovePicker {
  enum PickType { kNext, kBest };

 public:
  MovePicker(const MovePicker &) = delete;
  MovePicker &operator=(const MovePicker &) = delete;
  MovePicker(const Position &, Move, Value, const CapturePieceToHistory *);
  MovePicker(const Position &, Move, Depth, const ButterflyHistory *,
             const CapturePieceToHistory *, const PieceToHistory **, Square);
  MovePicker(const Position &, const CheckInfo *, Move, Depth, const ButterflyHistory *,
             const CapturePieceToHistory *, const PieceToHistory **, Move,
             Move *);
  Move NextMove(int *score = nullptr);

 private:
  template <PickType T, typename Pred>
  Move Select(Pred);
  template <GenType>
  void Score();
  ExtMove *begin() { return cur_; }
  ExtMove *end() { return end_moves_; }

  const Position &pos_;
  const CheckInfo *ci_;
  const ButterflyHistory *main_history_;
  const CapturePieceToHistory *capture_history_;
  const PieceToHistory **continuation_history_;
  Move tt_move_;
  ExtMove refutations_[3], *cur_, *end_moves_, *end_bad_captures_;
  int stage_;
  Move move_;
  int score_;
  int max_score_;
  Square recapture_square_;
  Value threshold_;
  Depth depth_;
  ExtMove moves_[kMaxMoves];
};


#endif
