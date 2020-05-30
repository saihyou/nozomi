/*
  nozomi, a USI shogi playing engine
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

#ifndef NOZOMI_STATS_H_
#define NOZOMI_STATS_H_

#include <array>
#include <cstring>
#include <limits>
#include "move.h"
#include "types.h"

template <typename T, int D>
class StatsEntry {
  T entry;

 public:
  void operator=(const T &v) { entry = v; }
  T *operator&() { return &entry; }
  T *operator->() { return &entry; }
  operator const T &() const { return entry; }

  void operator<<(int bonus) {
    assert(abs(bonus) <= D);  // Ensure range is [-D, D]
    static_assert(D <= std::numeric_limits<T>::max(), "D overflows T");

    entry += bonus - entry * abs(bonus) / D;

    assert(abs(entry) <= D);
  }
};

template <typename T, int D, int Size, int... Sizes>
struct Stats : public std::array<Stats<T, D, Sizes...>, Size> {
  typedef Stats<T, D, Size, Sizes...> stats;

  void fill(const T &v) {
    // For standard-layout 'this' points to first struct member
    assert(std::is_standard_layout<stats>::value);

    typedef StatsEntry<T, D> entry;
    entry *p = reinterpret_cast<entry *>(this);
    std::fill(p, p + sizeof(*this) / sizeof(entry), v);
  }
};

template <typename T, int D, int Size>
struct Stats<T, D, Size> : public std::array<StatsEntry<T, D>, Size> {};

/// In stats table, D=0 means that the template parameter is not used
enum StatsParams { kNotUsed = 0 };
enum class StatsType : int { kNoCaptures, kCaptures };

/// ButterflyHistory records how often quiet moves have been successful or
/// unsuccessful during the current search, and is used for reduction and move
/// ordering decisions. It uses 2 tables (one for each color) indexed by
/// the move's from and to squares, see
/// chessprogramming.wikispaces.com/Butterfly+Boards
typedef Stats<int16_t, 10692, kNumberOfColor,
              int(kSquareHand) * int(kBoardSquare)>
    ButterflyHistory;

/// CounterMoveHistory stores counter moves indexed by [piece][to] of the
/// previous move, see chessprogramming.wikispaces.com/Countermove+Heuristic
typedef Stats<Move, kNotUsed, kPieceMax, kBoardSquare> CounterMoveHistory;

/// CapturePieceToHistory is addressed by a move's [piece][to][captured piece
/// type]
typedef Stats<int16_t, 10692, kPieceMax, kBoardSquare, kPieceTypeMax>
    CapturePieceToHistory;

/// PieceToHistory is like ButterflyHistory but is addressed by a move's
/// [piece][to]
typedef Stats<int16_t, 29952, kPieceMax, kBoardSquare> PieceToHistory;

/// ContinuationHistory is the combined history of a given pair of moves,
/// usually the current one given a previous one. The nested history table is
/// based on PieceToHistory instead of ButterflyBoards.
typedef Stats<PieceToHistory, kNotUsed, kPieceMax, kBoardSquare>
    ContinuationHistory;

/// LowPlyHistory at higher depths records successful quiet moves on plies 0 to
/// 3 and quiet moves which are/were in the PV (ttPv) It get cleared with each
/// new search and get filled during iterative deepening
constexpr int kMaxLowPlyHistory = 4;
typedef Stats<int16_t, 10692, kMaxLowPlyHistory,
              int(kBoardSquare) * int(kBoardSquare)>
    LowPlyHistory;


#endif
