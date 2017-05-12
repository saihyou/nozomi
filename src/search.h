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

#ifndef _SEARCH_H_
#define _SEARCH_H_

#include <atomic>
#include <memory>
#include <stack>
#include <vector>

#include "misc.h"
#include "position.h"
#include "types.h"
#ifdef APERY_BOOK
#include "apery_book.h"
#else
#include "book.h"
#endif
#include "stats.h"

struct SearchStack
{
  Move  *pv;
  int    ply;
  Move   current_move;
  Move   excluded_move;
  Move   killers[2];
  Value  static_eval;
  Eval::EvalParts eval_parts;
  Value  material;
  bool   evaluated;
  int    move_count;
  int    history;
  CounterMoveStats *counter_moves;
};
namespace Search 
{
struct RootMove 
{
  explicit RootMove(Move m) : pv(1, m) {}
    
  bool 
  operator<(const RootMove &m) const 
  { 
    return m.score < score; 
  }

  bool 
  operator==(const Move &m) const 
  { 
    return pv[0] == m;
  }

  bool
  extract_ponder_from_tt(Position &pos);

  Value score          = -kValueInfinite;
  Value previous_score = -kValueInfinite;
  std::vector<Move> pv;
};

typedef std::vector<RootMove> RootMoveVector;

struct LimitsType 
{
  LimitsType() 
  {
    nodes = 0;
    time[kWhite] = 0;
    time[kBlack] = 0;
    inc[kWhite] = 0;
    inc[kBlack] = 0;
    movestogo = 0;
    depth = 0;
    movetime = 0;
    mate = 0;
    infinite = 0;
    ponder = 0;
    byoyomi = 0;
  }

  bool 
  use_time_management() const 
  { 
    return !(mate | movetime | depth | nodes | infinite); 
  }

  std::vector<Move> searchmoves;
  int time[kNumberOfColor];
  int inc[kNumberOfColor];
  int movestogo;
  int depth;
  int64_t nodes;
  int movetime;
  int mate;
  int infinite;
  int ponder;
  int byoyomi;
  std::chrono::milliseconds::rep start_time;
};

struct SignalsType
{
  std::atomic_bool stop;
  std::atomic_bool stop_on_ponder_hit;
};

typedef std::unique_ptr<std::stack<StateInfo>> StateStackPtr;

extern SignalsType   Signals;
extern LimitsType    Limits;
extern StateStackPtr SetupStates;
#ifdef APERY_BOOK
extern AperyBook     BookManager;
#else
extern Book          BookManager;
#endif

void
init();

void
clear();

Value
search(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth);

Value
qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta);
} // namespace


#endif
