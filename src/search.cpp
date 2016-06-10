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

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>

#include "evaluate.h"
#include "move_generator.h"
#include "move_picker.h"
#include "search.h"
#include "timeman.h"
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"
#include "stats.h"

namespace Search
{
SignalsType   Signals;
LimitsType    Limits;
StateStackPtr SetupStates;
Book          BookManager;
} // namespace Search

using std::string;
using Eval::evaluate;
using namespace Search;

namespace
{
// ノードの種類
enum NodeType
{
  kPV,
  kNonPV
};

// razor margin
// 今のStockfishとは異なるが、最新にしても強くはならない(多分)
inline Value
razor_margin(Depth d)
{
  return Value(512 + 32 * d);
}

// futility margin
// kNonPVでしかfutility pruningは使わないためStockfishよりmarginは少なめにする
inline Value
futility_margin(Depth d)
{
  return Value(180 * d);
}

// move count pruningで使用するテーブル
int FutilityMoveCounts[2][16]; // [improving][depth]

// 探索深さをどのくらい減らすか
Depth Reductions[2][2][64][64]; // [pv][improving][depth][move_number]

template <bool PvNode>
inline Depth
reduction(bool i, Depth d, int mn)
{
  return Reductions[PvNode][i][std::min(int(d), 63)][std::min(mn, 63)];
}

// Lazy SMPで各slave threadの探索深さを決定するために使用するテーブル
typedef std::vector<int> Row;
// これを採用しないとYBWCより弱くなる
const Row
HalfDensity[] =
{
  { 0, 1 },
  { 1, 0 },
  { 0, 0, 1, 1 },
  { 0, 1, 1, 0 },
  { 1, 1, 0, 0 },
  { 1, 0, 0, 1 },
  { 0, 0, 0, 1, 1, 1 },
  { 0, 0, 1, 1, 1, 0 },
  { 0, 1, 1, 1, 0, 0 },
  { 1, 1, 1, 0, 0, 0 },
  { 1, 1, 0, 0, 0, 1 },
  { 1, 0, 0, 0, 1, 1 },
  { 0, 0, 0, 0, 1, 1, 1, 1 },
  { 0, 0, 0, 1, 1, 1, 1, 0 },
  { 0, 0, 1, 1, 1, 1, 0 ,0 },
  { 0, 1, 1, 1, 1, 0, 0 ,0 },
  { 1, 1, 1, 1, 0, 0, 0 ,0 },
  { 1, 1, 1, 0, 0, 0, 0 ,1 },
  { 1, 1, 0, 0, 0, 0, 1 ,1 },
  { 1, 0, 0, 0, 0, 1, 1 ,1 },
};

const size_t HalfDensitySize = std::extent<decltype(HalfDensity)>::value;

Value                   DrawValue[kNumberOfColor];
#ifndef LEARN
CounterMoveHistoryStats CounterMoveHistory;
#endif

template <NodeType NT>
Value 
search
(
  Position &pos,
  SearchStack *ss,
  Value alpha,
  Value beta,
  Depth depth,
  bool cut_node
#ifdef LEARN
  ,
  CounterMoveHistoryStats &CounterMoveHistory
#endif
);

template <NodeType NT, bool InCheck>
Value
qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth);

Value
value_to_tt(Value v, int ply);
Value
value_from_tt(Value v, int ply);
void
update_pv(Move *pv, Move move, Move *child_pv);

void 
update_stats
(
  const Position &pos,
  SearchStack *ss,
  Move move,
  Depth depth,
  Move *quiets,
  int quiets_count
#ifdef LEARN
  ,
  CounterMoveHistoryStats &CounterMovesHistory
#endif
);

void
check_time();
} // namespace

string
usi_pv(const Position &pos, Depth depth, Value alpha, Value beta);

// ReductionsとFutilityMoveCountの初期化
void
Search::init()
{
  const double k[][2] = {{0.90, 2.25}, {0.50, 3.00}};

  for (int pv = 0; pv <= 1; ++pv)
  {
    for (int improving = 0; improving <= 1; ++improving)
    {
      for (int depth = 1; depth < 64; ++depth)
      {
        for (int move_count = 1; move_count < 64; ++move_count)
        {
          double r = k[pv][0] + log(depth) * log(move_count) / k[pv][1];

          if (r >= 1.5)
            Reductions[pv][improving][depth][move_count] = int(r) * kOnePly;

          // NonPVで評価値が悪くなる場合はよりreductionするようにする
          if (pv == 0 && improving == 0 && Reductions[pv][improving][depth][move_count] >= 2 * kOnePly)
            Reductions[pv][improving][depth][move_count] += kOnePly;
        }
      }
    }
  }

  for (int depth = 0; depth < 16; ++depth)
  {
    FutilityMoveCounts[0][depth] = int(2.4 + 0.773 * pow(depth + 0.00, 1.8));
    FutilityMoveCounts[1][depth] = int(2.9 + 1.045 * pow(depth + 0.49, 1.8));
  }
}

void
Search::clear()
{
  TT.clear();
#ifndef LEARN
  CounterMoveHistory.clear();
#endif
  for (Thread *th : Threads)
  {
    th->history_.clear();
    th->counter_moves_.clear();
  }

  Threads.main()->previous_score = kValueInfinite;
}

void
MainThread::search()
{
  Color us = root_pos_.side_to_move();
  Time.init(Limits, us);
  bool search_best_thread = true;
  int contempt = Options["Contempt"] * Eval::kPawnValue / 100; // From centipawns
  DrawValue[us] = kValueDraw - Value(contempt);
  DrawValue[~us] = kValueDraw + Value(contempt);

  if (root_moves_.empty())
  {
    root_moves_.push_back(RootMove(kMoveNone));
    sync_cout << "info depth 0 score "
      << USI::format_value(-kValueMate)
      << sync_endl;
    search_best_thread = false;
  }
  else
  {
    if (Options["OwnBook"] && !Limits.infinite && !Limits.mate)
    {
      Move book_move = BookManager.get_move(root_pos_);
      if (book_move != kMoveNone && std::count(root_moves_.begin(), root_moves_.end(), book_move))
      {
        std::swap(root_moves_[0], *std::find(root_moves_.begin(), root_moves_.end(), book_move));
        search_best_thread = false;
        goto exit;
      }
    }

    for (Thread *th : Threads)
    {
      th->max_ply_ = 0;
      th->root_depth_ = kDepthZero;
      if (th != this)
      {
        th->root_pos_ = Position(root_pos_, th);
        th->root_moves_ = root_moves_;
        th->start_searching();
      }
    }

    Thread::search();
  }

exit:
  if (!Signals.stop && (Limits.ponder || Limits.infinite))
  {
    Signals.stop_on_ponder_hit = true;
    wait(Signals.stop);
  }

  Signals.stop = true;

  for (Thread *th : Threads)
  {
    if (th != this)
      th->wait_for_search_finished();
  }

  Thread *best_thread = this;
  if (Options["MultiPV"] == 1 && search_best_thread)
  {
    for (Thread *th : Threads)
    {
      if
      (
        th->completed_depth_ > best_thread->completed_depth_
        &&
        th->root_moves_[0].score > best_thread->root_moves_[0].score
      )
        best_thread = th;
    }
  }

  if (best_thread->root_moves_[0].pv[0] != kMoveNone)
  {
    if (best_thread != this)
      sync_cout << usi_pv(best_thread->root_pos_, best_thread->completed_depth_, -kValueInfinite, kValueInfinite) << sync_endl;

    sync_cout << "bestmove " << USI::format_move(best_thread->root_moves_[0].pv[0]);

    if (best_thread->root_moves_[0].pv.size() > 1 || best_thread->root_moves_[0].extract_ponder_from_tt(root_pos_))
      std::cout << " ponder " << USI::format_move(best_thread->root_moves_[0].pv[1]);

    std::cout << sync_endl;
  }
  else
  {
    sync_cout << "bestmove resign" << sync_endl;
  }
}

#ifndef LEARN
void
Thread::search()
{
  SearchStack stack[kMaxPly + 4];
  SearchStack *ss = stack + 2; // (ss - 2)から(ss + 2)までアクセス可能
  Value best_value = -kValueInfinite;
  Value alpha      = -kValueInfinite;
  Value beta       = kValueInfinite;
  Value delta      = -kValueInfinite;
  MainThread *main_thread = (this == Threads.main() ? Threads.main() : nullptr);

  std::memset(ss - 2, 0, 5 * sizeof(SearchStack));

  completed_depth_ = kDepthZero;

  if (main_thread)
  {
    main_thread->best_move_changes = 0;
    main_thread->failed_low = false;
    TT.new_search();
  }

  size_t multi_pv = Options["MultiPV"];

  while (++root_depth_ < kDepthMax && !Signals.stop && (!Limits.depth || root_depth_ < Limits.depth))
  {
    if (!main_thread)
    {
      const Row &row = HalfDensity[(index_ - 1) % HalfDensitySize];
      if (row[(root_depth_ + root_pos_.game_ply()) % row.size()])
        continue;
    }

    if (main_thread)
    {
      main_thread->best_move_changes *= 0.505;
      main_thread->failed_low = false;
    }

    for (RootMove &rm : root_moves_)
      rm.previous_score = rm.score;

    for (pv_index_ = 0; pv_index_ < multi_pv && !Signals.stop; ++pv_index_)
    {
      if (root_depth_ >= 5 * kOnePly)
      {
        delta = Value(64);
        alpha = std::max(root_moves_[pv_index_].previous_score - delta, -kValueInfinite);
        beta  = std::min(root_moves_[pv_index_].previous_score + delta, kValueInfinite);
      }

      while (true)
      {
        best_value = ::search<kPV>(root_pos_, ss, alpha, beta, root_depth_, false);

        std::stable_sort(root_moves_.begin() + pv_index_, root_moves_.end());

        for (size_t i = 0; i <= pv_index_; ++i)
          root_moves_[i].insert_pv_in_tt(root_pos_);

        if (Signals.stop)
          break;

        if
        (
          main_thread
          &&
          multi_pv == 1
          &&
          (best_value <= alpha || best_value >= beta)
          &&
          Time.elapsed() > 3000
        )
          sync_cout << usi_pv(root_pos_, root_depth_, alpha, beta) << sync_endl;

        if (best_value <= alpha)
        {
          beta = (alpha + beta) / 2;
          alpha = std::max(best_value - delta, -kValueInfinite);

          if (main_thread)
          {
            main_thread->failed_low = true;
            Signals.stop_on_ponder_hit = false;
          }
        }
        else if (best_value >= beta)
        {
          alpha = (alpha + beta) / 2;
          beta = std::min(best_value + delta, kValueInfinite);
        }
        else
        {
          break;
        }

        delta += delta / 4 + 5;
        assert(alpha >= -kValueInfinite && beta <= kValueInfinite);
      }

      std::stable_sort(root_moves_.begin(), root_moves_.begin() + pv_index_ + 1);

      if (!main_thread)
        break;

      if (Signals.stop)
      {
        sync_cout << "info nodes " << Threads.nodes_searched()
          << " time " << Time.elapsed() << sync_endl;
      }
      else if (pv_index_ + 1 == multi_pv || Time.elapsed() > 3000)
      {
        sync_cout << usi_pv(root_pos_, root_depth_, alpha, beta) << sync_endl;
      }
    }

    if (!Signals.stop)
      completed_depth_ = root_depth_;

    if (!main_thread)
      continue;

    if
    (
      Limits.mate
      &&
      best_value >= kValueMateInMaxPly
      &&
      kValueMate - best_value <= 2 * Limits.mate
    )
      Signals.stop = true;

    if (Limits.use_time_management() && !Signals.stop && !Signals.stop_on_ponder_hit)
    {
      if (root_depth_ > 4 * kOnePly && multi_pv == 1)
        Time.pv_instability(main_thread->best_move_changes);

      if
      (
        root_moves_.size() == 1
        ||
        Time.elapsed() > Time.available_time()
      )
      {
        if (Limits.ponder)
          Signals.stop_on_ponder_hit = true;
        else
          Signals.stop = true;
      }
    }
  }
}
#else
void
Thread::search(){}

Value
Search::search(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth, CounterMoveHistoryStats &CounterMoveHistory)
{
  return ::search<kPV>(pos, ss, alpha, beta, depth, false, CounterMoveHistory);
}
#endif

namespace
{
// 探索のメイン処理
template <NodeType NT>
Value 
search
(
  Position &pos,
  SearchStack *ss,
  Value alpha,
  Value beta,
  Depth depth,
  bool cut_node
#ifdef LEARN
  ,
  CounterMoveHistoryStats &CounterMoveHistory
#endif
)
{
  const bool pv_node   = NT == kPV;
  const bool root_node = pv_node && (ss - 1)->ply == 0;

  assert(-kValueInfinite <= alpha && alpha < beta && beta <= kValueInfinite);
  assert(pv_node || (alpha == beta - 1));
  assert(depth > kDepthZero);

  Move pv[kMaxPly + 1];
  Move quiets_searched[64];
  StateInfo st;
  TTEntry *tte;
  Key position_key;
  Move tt_move;
  Move move;
  Move excluded_move;
  Move best_move;
  Depth ext;
  Depth new_depth;
  Depth predicted_depth;
  Value best_value;
  Value value;
  Value tt_value;
  Value eval;
  Value null_value;
  Value futility_value;
  bool in_check;
  bool gives_check;
  bool singular_extension_node;
  bool improving;
  bool capture;
  bool do_full_depth_search;
  bool tt_hit;
  int move_count;
  int quiet_count;

  // Initialize node
  Thread *this_thread = pos.this_thread();
  in_check = pos.in_check();
  move_count = quiet_count = ss->move_count = 0;
  best_value = -kValueInfinite;
  ss->ply = (ss - 1)->ply + 1;

  // 残り時間
  if (this_thread->reset_calls_.load(std::memory_order_relaxed))
  {
    this_thread->reset_calls_ = false;
    this_thread->calls_count_ = 0;
  }
  if (++this_thread->calls_count_ > 4096)
  {
    for (Thread* th : Threads)
      th->calls_count_ = true;

    check_time();
  }

  // sel_depth用の情報を更新する
  if (pv_node && this_thread->max_ply_ < ss->ply)
    this_thread->max_ply_ = ss->ply;

  if (!root_node)
  {
    // 探索の中断と千日手チェック
    Repetition repetition = ((ss - 1)->current_move != kMoveNull) ? pos.in_repetition() : kNoRepetition;
    if (Signals.stop.load(std::memory_order_relaxed) || repetition == kRepetition || ss->ply >= kMaxPly)
      return ss->ply >= kMaxPly && !in_check ? evaluate(pos, ss) : DrawValue[pos.side_to_move()];

    // 連続王手千日手
    if (repetition == kPerpetualCheckWin)
    {
      return mate_in(ss->ply);
    }
    else if (repetition == kPerpetualCheckLose)
    {
      return mated_in(ss->ply);
    }

    // 盤上は同一だが、手駒だけ損するケース
    if (ss->ply != 2)
    {
      if (repetition == kBlackWinRepetition)
      {
        if (pos.side_to_move() == kWhite)
          return -kValueSamePosition;
        else
          return kValueSamePosition;
      }
      else if (repetition == kBlackLoseRepetition)
      {
        if (pos.side_to_move() == kBlack)
          return -kValueSamePosition;
        else
          return kValueSamePosition;
      }
    }

    // Mate distance pruning
    alpha = std::max(mated_in(ss->ply), alpha);
    beta = std::min(mate_in(ss->ply + 1), beta);
    if (alpha >= beta)
      return alpha;
  }

  assert(0 <= ss->ply && ss->ply < kMaxPly);

  ss->current_move = (ss + 1)->excluded_move = best_move = kMoveNone;
  (ss + 1)->skip_early_pruning = false;
  (ss + 2)->killers[0] = (ss + 2)->killers[1] = kMoveNone;

  // Transposition table lookup
  excluded_move = ss->excluded_move;
  position_key = excluded_move ? pos.exclusion_key() : pos.key();
  tte = TT.probe(position_key, &tt_hit);
  tt_move = root_node
            ?
            this_thread->root_moves_[this_thread->pv_index_].pv[0]
            :
            (
              tt_hit
              ?
              tte->move(pos)
              :
              kMoveNone
            );
  tt_value = tt_hit ? value_from_tt(tte->value(), ss->ply) : kValueNone;

  // PV nodeのときはtransposition tableの手を使用しない
  if
  (
    !pv_node
    &&
    tt_hit
    &&
    tte->depth() >= depth
    &&
    tt_value != kValueNone // transposition tableの値が壊れている時になりうる
    &&
    (
      tt_value >= beta
      ?
      (tte->bound() & kBoundLower)
      :
      (tte->bound() & kBoundUpper)
    )
  )
  {
    ss->current_move = tt_move; // tt_moveはkMoveNoneとなりうる

    if (tt_value >= beta && tt_move && !move_is_capture(tt_move))
      update_stats
      (
        pos,
        ss,
        tt_move,
        depth,
        nullptr,
        0
#ifdef LEARN
        ,
        CounterMoveHistory
#endif
      );

    return tt_value;
  }

  // 1手詰め判定
  // 処理が重いのでなるべくなら呼び出さないようにしたい
  if
  (
    !root_node
    &&
    // depthが2以下の場合は呼び出さない方が強い
    depth > 2 * kOnePly
    &&
    !tt_hit
    &&
    !in_check
    &&
    // null moveの直後は呼び出さない方が強い
    (ss - 1)->current_move != kMoveNull
  )
  {
    Move mate_move;
    if ((mate_move = search_mate1ply(pos)) != kMoveNone)
    {
      ss->static_eval = best_value = mate_in(ss->ply + 1);
      tte->save
      (
        position_key,
        value_to_tt(best_value, ss->ply),
        kBoundExact,
        depth,
        mate_move,
        ss->static_eval,
        TT.generation()
      );

      return best_value;
    }
  }

  // 現局面の静的評価
  if (in_check)
  {
    ss->static_eval = eval = kValueNone;
    goto moves_loop;
  }
  else if (tt_hit)
  {
    if ((ss->static_eval = eval = tte->eval_value()) == kValueNone)
      eval = ss->static_eval = evaluate(pos, ss);

    if (tt_value != kValueNone)
    {
      if (tte->bound() & (tt_value > eval ? kBoundLower : kBoundUpper))
        eval = tt_value;
    }
  }
  else
  {
    eval = ss->static_eval =
      (ss - 1)->current_move != kMoveNull
      ?
      evaluate(pos, ss)
      :
      -(ss - 1)->static_eval + 2 * Eval::kTempo;
    tte->save(position_key, kValueNone, kBoundNone, kDepthNone, kMoveNone, ss->static_eval, TT.generation());
  }

  if (ss->skip_early_pruning)
    goto moves_loop;

  // Razoring
  if
  (
    !pv_node
    &&
    depth < 4 * kOnePly
    &&
    eval + razor_margin(depth) <= alpha
    &&
    tt_move == kMoveNone
  )
  {
    if
    (
      depth <= kOnePly
      &&
      eval + razor_margin(3 * kOnePly) <= alpha
    )
      return qsearch<kNonPV, false>(pos, ss, alpha, beta, kDepthZero);

    Value ralpha = alpha - razor_margin(depth);
    Value v = qsearch<kNonPV, false>(pos, ss, ralpha, ralpha + 1, kDepthZero);
    if (v <= ralpha)
      return v;
  }

  // Futility pruning: child node
  if
  (
    // Stockfishは!root_nodeだが現状の評価関数だとpv_nodeもやると弱くなる
    !pv_node
    &&
    depth < 7 * kOnePly
    &&
    eval - futility_margin(depth) >= beta
    &&
    eval < kValueKnownWin
  )
    return eval - futility_margin(depth);

  // Null move search
  if
  (
    !pv_node
    &&
    depth >= 2 * kOnePly
    &&
    eval >= beta
  )
  {
    ss->current_move = kMoveNull;

    assert(eval - beta >= 0);

    Depth re
      = ((823 + 67 * depth) / 256 + std::min(int(eval - beta) / Eval::kPawnValue, 3)) * kOnePly;

    pos.do_null_move(st);
    (ss + 1)->evaluated = false;
    (ss + 1)->skip_early_pruning = true;
    null_value =
      depth - re < kOnePly
      ?
      -qsearch<kNonPV, false>(pos, ss + 1, -beta, -beta + 1, kDepthZero)
      : 
      -search<kNonPV>
      (
        pos,
        ss + 1,
        -beta,
        -beta + 1,
        depth - re,
        !cut_node
#ifdef LEARN
        ,
        CounterMoveHistory
#endif
      );

    (ss + 1)->skip_early_pruning = false;
    pos.undo_null_move();

    if (null_value >= beta)
    {
      if (null_value >= kValueMateInMaxPly)
        null_value = beta;

      if (depth < 12 * kOnePly && abs(beta) < kValueKnownWin)
        return null_value;

      ss->skip_early_pruning = true;
      Value v =
        depth - re < kOnePly
        ?
        qsearch<kNonPV, false>(pos, ss, beta - 1, beta, kDepthZero)
        :  
        search<kNonPV>
        (
          pos,
          ss,
          beta - 1,
          beta,
          depth - re,
          false
#ifdef LEARN
          ,
          CounterMoveHistory
#endif
        );

      ss->skip_early_pruning = false;

      if (v >= beta)
        return null_value;
    }
  }

  // ProbCut
  if
  (
    !pv_node
    &&
    depth >= 5 * kOnePly
    &&
    abs(beta) < kValueMateInMaxPly
  )
  {
    Value rbeta = std::min(beta + 200, kValueInfinite);
    Depth rdepth = depth - 4 * kOnePly;

    assert(rdepth >= kOnePly);
    assert((ss - 1)->current_move != kMoveNone);
    assert((ss - 1)->current_move != kMoveNull);

    Value v = static_cast<Value>(Eval::ExchangePieceValueTable[move_capture((ss - 1)->current_move)]);
    // 成る手ならそれをくわえておいた方がちょっとだけいい感じのようだ
    if (move_is_promote((ss - 1)->current_move))
      v += static_cast<Value>(Eval::PromotePieceValueTable[move_piece_type((ss - 1)->current_move)]);
    MovePicker mp(pos, tt_move, this_thread->history_, v);
    CheckInfo ci(pos);

    while ((move = mp.next_move()) != kMoveNone)
    {
      if (pos.legal(move, ci.pinned))
      {
        ss->current_move = move;
        pos.do_move(move, st);
        (ss + 1)->evaluated = false;
        value = 
          -search<kNonPV>
          (
            pos,
            ss + 1,
            -rbeta,
            -rbeta + 1,
            rdepth,
            !cut_node
#ifdef LEARN
            ,
            CounterMoveHistory
#endif
          );
        pos.undo_move(move);
        if (value >= rbeta)
          return value;
      }
    }
  }

  // Internal iterative deepening
  if
  (
    depth >= (pv_node ? 5 * kOnePly : 8 * kOnePly)
    &&
    !tt_move
    &&
    (pv_node || ss->static_eval + 256 >= beta)
  )
  {
    Depth d = depth - 2 * kOnePly - (pv_node ? kDepthZero : depth / 4);

    ss->skip_early_pruning = true;
    search<NT>
    (
      pos,
      ss,
      alpha,
      beta,
      d,
      true
#ifdef LEARN
      ,
      CounterMoveHistory
#endif
    );
    ss->skip_early_pruning = false;

    tte = TT.probe(position_key, &tt_hit);
    tt_move = tt_hit ? tte->move(pos) : kMoveNone;
  }

moves_loop:

  Square prev_move_square = move_to((ss - 1)->current_move);
  Square prev_own_square  = move_to((ss - 2)->current_move);
  Piece  prev_move_piece  = move_piece((ss - 1)->current_move, ~pos.side_to_move());
  Piece  prev_own_piece   = move_piece((ss - 2)->current_move, pos.side_to_move());
  Move   countermove      = this_thread->counter_moves_[prev_move_piece][prev_move_square];
  const CounterMoveStats &cmh = CounterMoveHistory[prev_move_piece][prev_move_square];
  const CounterMoveStats &fmh = CounterMoveHistory[prev_own_piece][prev_own_square];

  MovePicker mp(pos, tt_move, depth, this_thread->history_, cmh, fmh, countermove, ss);
  CheckInfo ci(pos);
  value = best_value;
  improving =
    ss->static_eval >= (ss - 2)->static_eval
    ||
    ss->static_eval == kValueNone
    ||
    (ss - 2)->static_eval == kValueNone;

  singular_extension_node =
    !root_node
    &&
    depth >= 8 * kOnePly
    &&
    tt_move != kMoveNone
    //  &&  tt_value != kValueNoneもチェックの必要があるが下の条件を調べればok
    &&
    abs(tt_value) < kValueKnownWin
    &&
    !excluded_move
    &&
    (tte->bound() & kBoundLower)
    &&
    tte->depth() >= depth - 3 * kOnePly;

  // Loop through moves
  while ((move = mp.next_move()) != kMoveNone)
  {
    assert(is_ok(move));

    if (move == excluded_move)
      continue;

    if
    (
      root_node
      &&
      !std::count
      (
        this_thread->root_moves_.begin() + this_thread->pv_index_,
        this_thread->root_moves_.end(),
        move
      )
    )
      continue;

    ss->move_count = ++move_count;

    if (root_node && this_thread == Threads.main() && Time.elapsed() > 3000)
    {
      sync_cout << "info depth " << depth
                << " currmove " << USI::format_move(move)
                << " currmovenumber " << move_count + this_thread->pv_index_ << sync_endl;
    }

    if (pv_node)
      (ss + 1)->pv = nullptr;

    ext = kDepthZero;

    // Stockfishだとpromotionも入っているが将棋としてはpromotionなんて普通の手なので除外
    capture = move_is_capture(move);

    gives_check = pos.gives_check(move, ci);

    // Extend checks
    if (gives_check && pos.see_sign(move) >= kValueZero)
      ext = kOnePly;

    // Singular extension search
    if
    (
      singular_extension_node
      &&
      move == tt_move
      &&
      !ext
      &&
      pos.legal(move, ci.pinned)
    )
    {
      Value r_beta = tt_value - 8 * depth / kOnePly;
      ss->excluded_move = move;
      ss->skip_early_pruning = true;
      value =
        search<kNonPV>
        (
          pos,
          ss,
          r_beta - 1,
          r_beta,
          depth / 2,
          cut_node
#ifdef LEARN
          ,
          CounterMoveHistory
#endif
        );
      ss->skip_early_pruning = false;
      ss->excluded_move = kMoveNone;

      if (value < r_beta)
        ext = kOnePly;
    }

    new_depth = depth - kOnePly + ext;

    // Pruning at shallow depth
    if
    (
      !pv_node
      &&
      !capture
      &&
      !in_check
      &&
      !gives_check
      &&
      best_value > kValueMatedInMaxPly
    )
    {
      // Move count based pruning
      if
      (
        depth < 16 * kOnePly
        &&
        move_count >= FutilityMoveCounts[improving][depth]
      )
        continue;

      // History based pruning
      if
      (
        depth <= 4 * kOnePly
        &&
        move != ss->killers[0]
        &&
        this_thread->history_[move_piece(move, pos.side_to_move())][move_to(move)] < kValueZero
        &&
        cmh[move_piece(move, pos.side_to_move())][move_to(move)] < kValueZero
      )
        continue;

      predicted_depth = std::max(new_depth - reduction<pv_node>(improving, depth, move_count), kDepthZero);

      // Futility pruning: parent node
      if (predicted_depth < 7 * kOnePly)
      {
        futility_value =
          ss->static_eval + futility_margin(predicted_depth) + 256;

        if (futility_value <= alpha)
        {
          best_value = std::max(best_value, futility_value);
          continue;
        }
      }

      if (predicted_depth < 4 * kOnePly && pos.see_sign(move) < kValueZero)
        continue;
    }

    if (!root_node && !pos.legal(move, ci.pinned))
    {
      ss->move_count = --move_count;
      continue;
    }

    ss->current_move = move;

    // Make the move
    pos.do_move(move, st, gives_check);
    (ss + 1)->evaluated = false;

    // Reduced depth search (LMR)
    if
    (
      depth >= 3 * kOnePly
      &&
      move_count > 1
      &&
      !capture
    )
    {
      Depth r = reduction<pv_node>(improving, depth, move_count);
      Value h_value = this_thread->history_[move_piece(move, ~pos.side_to_move())][move_to(move)];
      Value cmh_value = cmh[move_piece(move, ~pos.side_to_move())][move_to(move)];

      if
      (
        (!pv_node && cut_node)
        ||
        (
          is_ok((ss - 1)->current_move)
          &&
          h_value < kValueZero
          &&
          cmh_value <= kValueZero
        )
      )
        r += kOnePly;

      int r_hist = (h_value + cmh_value) / 14980;
      r = std::max(kDepthZero, r - r_hist * kOnePly);

      if (r && pos.see_reverse_move(move) < kValueZero)
        r = std::max(kDepthZero, r - kOnePly);

      Depth d = std::max(new_depth - r, kOnePly);

      value = 
        -search<kNonPV>
        (
          pos,
          ss + 1,
          -(alpha + 1),
          -alpha,
          d,
          true
#ifdef LEARN
          ,
          CounterMoveHistory
#endif
        );

      do_full_depth_search = (value > alpha && r != kDepthZero);
    }
    else
    {
      do_full_depth_search = !pv_node || move_count > 1;
    }

    // Full depth search
    if (do_full_depth_search)
    {
      value =
        new_depth < kOnePly
        ?
        (
          gives_check
          ?
          -qsearch<kNonPV,  true>(pos, ss + 1, -(alpha + 1), -alpha, kDepthZero)
          :
          -qsearch<kNonPV, false>(pos, ss + 1, -(alpha + 1), -alpha, kDepthZero)
        )
        : 
        -search<kNonPV>
        (
          pos,
          ss + 1,
          -(alpha + 1),
          -alpha,
          new_depth,
          !cut_node
#ifdef LEARN
          ,
          CounterMoveHistory
#endif
        );
    }

    if (pv_node && (move_count == 1 || (value > alpha && (root_node || value < beta))))
    {
      (ss + 1)->pv = pv;
      (ss + 1)->pv[0] = kMoveNone;

      value =
        new_depth < kOnePly
        ?
        (
          gives_check
          ?
          -qsearch<kPV,  true>(pos, ss + 1, -beta, -alpha, kDepthZero)
          :
          -qsearch<kPV, false>(pos, ss + 1, -beta, -alpha, kDepthZero)
        )                            
        : 
        -search<kPV>
        (
          pos,
          ss + 1,
          -beta,
          -alpha,
          new_depth,
          false
#ifdef LEARN
          ,
          CounterMoveHistory
#endif
        );
    }

    // Undo move
    pos.undo_move(move);

    assert(value > -kValueInfinite && value < kValueInfinite);

    // Check for new best move
    if (Signals.stop.load(std::memory_order_relaxed))
      return kValueZero;

    if (root_node)
    {
      RootMove &rm = *std::find(this_thread->root_moves_.begin(), this_thread->root_moves_.end(), move);

      if (move_count == 1 || value > alpha)
      {
        rm.score = value;
        rm.pv.resize(1);

        assert((ss + 1)->pv);

        for (Move *m = (ss + 1)->pv; *m != kMoveNone; ++m)
          rm.pv.push_back(*m);

        if (move_count > 1 && this_thread == Threads.main())
          ++static_cast<MainThread *>(this_thread)->best_move_changes;
      }
      else
      {
        rm.score = -kValueInfinite;
      }
    }

    if (value > best_value)
    {
      best_value = value;

      if (value > alpha)
      {
        best_move = move;

        if (pv_node && !root_node)
          update_pv(ss->pv, move, (ss + 1)->pv);

        if (pv_node && value < beta)
        {
          alpha = value;
        }
        else
        {
          assert(value >= beta);
          break;
        }
      }
    }

    if (!capture && move != best_move && quiet_count < 64)
      quiets_searched[quiet_count++] = move;
  }

  if (move_count == 0)
  {
    best_value =
      excluded_move
      ?
      alpha
      :
      mated_in(ss->ply - 1); // すでに合法手がないので1手前で詰んでいる
  }
  else if (best_value >= beta && !move_is_capture(best_move))
  {
    update_stats
    (
      pos,
      ss,
      best_move,
      depth,
      quiets_searched,
      quiet_count
#ifdef LEARN
      ,
      CounterMoveHistory
#endif
    );
  }
  else if
  (
    depth >= 3 * kOnePly
    &&
    !best_move
    &&
    !in_check
    &&
    !move_is_capture((ss - 1)->current_move)
    &&
    is_ok((ss - 1)->current_move)
    &&
    is_ok((ss - 2)->current_move)
  )
  {
    Value bonus = Value((depth / kOnePly) * (depth / kOnePly) + depth / kOnePly - 1);
    CounterMoveStats &prev_cmh = CounterMoveHistory[prev_own_piece][prev_own_square];
    prev_cmh.update(prev_move_piece, prev_move_square, bonus);
  }

  tte->save
  (
    position_key,
    value_to_tt(best_value, ss->ply),
    best_value >= beta
    ?
    kBoundLower
    :
    (
      pv_node && best_move
      ?
      kBoundExact
      :
      kBoundUpper
    ),
    depth,
    best_move,
    ss->static_eval,
    TT.generation()
  );

  assert(best_value > -kValueInfinite && best_value < kValueInfinite);

  return best_value;
}

// 静止探索
template <NodeType NT, bool InCheck>
Value
qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth)
{
  const bool PvNode = NT == kPV;

  assert(NT == kPV || NT == kNonPV);
  assert(alpha >= -kValueInfinite && alpha < beta && beta <= kValueInfinite);
  assert(PvNode || (alpha == beta - 1));
  assert(depth <= kDepthZero);

  Move pv[kMaxPly + 1];
  StateInfo st;
  TTEntry *tte;
  Key position_key;
  Move tt_move;
  Move move;
  Move best_move;
  Value best_value;
  Value value;
  Value tt_value;
  Value futility_value;
  Value futility_base;
  Value old_alpha;
  bool gives_check;
  bool evasion_prunable;
  Depth tt_depth;
  bool tt_hit;

  if (PvNode)
  {
    old_alpha = alpha;

    (ss + 1)->pv = pv;
    ss->pv[0] = kMoveNone;
  }

  ss->current_move = best_move = kMoveNone;
  ss->ply = (ss - 1)->ply + 1;

  Repetition repetition = ((ss - 1)->current_move != kMoveNull) ? pos.in_repetition() : kNoRepetition;
  if (repetition == kRepetition || ss->ply >= kMaxPly)
    return ss->ply >= kMaxPly && !InCheck ? evaluate(pos, ss) : DrawValue[pos.side_to_move()];

  assert(0 <= ss->ply && ss->ply < kMaxPly);

  if (repetition == kPerpetualCheckWin)
  {
    return mate_in(ss->ply);
  }
  else if (repetition == kPerpetualCheckLose)
  {
    return mated_in(ss->ply);
  }
  else if (repetition == kBlackWinRepetition)
  {
    if (pos.side_to_move() == kWhite)
      return -kValueSamePosition;
    else
      return kValueSamePosition;
  }
  else if (repetition == kBlackLoseRepetition)
  {
    if (pos.side_to_move() == kBlack)
      return -kValueSamePosition;
    else
      return kValueSamePosition;
  }

  tt_depth =
    InCheck || depth >= kDepthQsChecks
    ?
    kDepthQsChecks
    :
    kDepthQsNoChecks;

  // Transposition table lookup
  position_key = pos.key();
  tte = TT.probe(position_key, &tt_hit);
  tt_move = tt_hit ? tte->move(pos) : kMoveNone;
  tt_value = tt_hit ? value_from_tt(tte->value(),ss->ply) : kValueNone;

  if
  (
    !PvNode
    &&
    tt_hit
    &&
    tte->depth() >= tt_depth
    &&
    tt_value != kValueNone
    &&
    (
      tt_value >= beta
      ?
      (tte->bound() &  kBoundLower)
      :
      (tte->bound() &  kBoundUpper)
    )
  )
  {
    ss->current_move = tt_move;
    return tt_value;
  }

  // 現局面の静的評価
  if (InCheck)
  {
    ss->static_eval = kValueNone;
    best_value = futility_base = -kValueInfinite;
  }
  else
  {
    Move mate_move = search_mate1ply(pos);
    if
    (
      !tt_hit
      &&
      (ss - 1)->current_move != kMoveNull
      &&
      mate_move != kMoveNone
    )
    {
      tte->save
      (
        position_key,
        value_to_tt(mate_in(ss->ply + 1), ss->ply),
        kBoundExact,
        tt_depth,
        mate_move,
        kValueNone,
        TT.generation()
      );
      return mate_in(ss->ply + 1);
    }

    if (tt_hit)
    {
      if ((ss->static_eval = best_value = tte->eval_value()) == kValueNone)
        ss->static_eval = best_value = evaluate(pos, ss);

      if (tt_value != kValueNone)
      {
        if (tte->bound() & (tt_value > best_value ? kBoundLower : kBoundUpper))
          best_value = tt_value;
      }
    }
    else
    {
      ss->static_eval = best_value =
        (ss - 1)->current_move != kMoveNull
        ?
        evaluate(pos, ss)
        :
        -(ss - 1)->static_eval + 2 * Eval::kTempo;
    }

    if (best_value >= beta)
    {
      if (!tt_hit)
      {
        tte->save
        (
          pos.key(),
          value_to_tt(best_value, ss->ply),
          kBoundLower,
          kDepthNone,
          kMoveNone,
          ss->static_eval,
          TT.generation()
        );
      }

      return best_value;
    }

    if (PvNode && best_value > alpha)
      alpha = best_value;

    futility_base = best_value + 128;
  }

  MovePicker mp(pos, tt_move, depth, pos.this_thread()->history_, move_to((ss - 1)->current_move));
  CheckInfo ci(pos);

  while ((move = mp.next_move()) != kMoveNone)
  {
    assert(is_ok(move));

    gives_check = pos.gives_check(move, ci);

    // Futility pruning
    if
    (
      !InCheck
      &&
      !gives_check
      &&
      futility_base > -kValueKnownWin
    )
    {
      futility_value = futility_base + Eval::ExchangePieceValueTable[move_capture(move)];
      if (move_is_promote(move))
        futility_value += Eval::PromotePieceValueTable[move_piece_type(move)];

      if (futility_value <= alpha)
      {
        best_value = std::max(best_value, futility_value);
        continue;
      }

      if (futility_base <= alpha && pos.see(move) <= kValueZero)
      {
        best_value = std::max(best_value, futility_base);
        continue;
      }
    }

    evasion_prunable =
      InCheck
      &&
      best_value > kValueMatedInMaxPly
      &&
      !move_is_capture(move);

    if
    (
      (!InCheck || evasion_prunable)
      &&
      pos.see_sign(move) < kValueZero
    )
      continue;

    if (!pos.legal(move, ci.pinned))
      continue;

    ss->current_move = move;

    pos.do_move(move, st, gives_check);
    (ss + 1)->evaluated = false;

    value =
      gives_check
      ?
      -qsearch<NT,  true>(pos, ss + 1, -beta, -alpha, depth - kOnePly)
      :
      -qsearch<NT, false>(pos, ss + 1, -beta, -alpha, depth - kOnePly);
    pos.undo_move(move);

    assert(value > -kValueInfinite && value < kValueInfinite);

    if (value > best_value)
    {
      best_value = value;

      if (value > alpha)
      {
        if (PvNode)
          update_pv(ss->pv, move, (ss + 1)->pv);

        if (PvNode && value < beta)
        {
          alpha = value;
          best_move = move;
        }
        else
        {
          tte->save
          (
            position_key,
            value_to_tt(value, ss->ply),
            kBoundLower,
            tt_depth,
            move,
            ss->static_eval,
            TT.generation()
          );

          return value;
        }
      }
    }
  }

  if (InCheck && best_value == -kValueInfinite)
    return mated_in(ss->ply - 1);

  tte->save
  (
    position_key,
    value_to_tt(best_value, ss->ply),
    (
      PvNode && best_value > old_alpha
      ?
      kBoundExact
      :
      kBoundUpper
    ),
    tt_depth,
    best_move,
    ss->static_eval,
    TT.generation()
  );

  assert(best_value > -kValueInfinite && best_value < kValueInfinite);

  return best_value;
}

Value
value_to_tt(Value v, int ply)
{
  assert(v != kValueNone);

  return
    v >= kValueMateInMaxPly
    ?
    v + ply
    :
    (
      v <= kValueMatedInMaxPly
      ?
      v - ply
      :
      v
    );
}

Value
value_from_tt(Value v, int ply)
{
  return
    v == kValueNone
    ?
    kValueNone
    :
    (
      v >= kValueMateInMaxPly
      ?
      v - ply
      :
      (
        v <= kValueMatedInMaxPly
        ?
        v + ply
        :
        v
      )
    );
}

void
update_pv(Move *pv, Move move, Move *child_pv)
{
  for (*pv++ = move; child_pv && *child_pv != kMoveNone; )
    *pv++ = *child_pv++;
  *pv = kMoveNone;
}

void 
update_stats
(
  const Position &pos,
  SearchStack *ss,
  Move move,
  Depth depth,
  Move *quiets,
  int quiets_count
#ifdef LEARN
  ,
  CounterMoveHistoryStats &CounterMoveHistory
#endif
) 
{
  if (ss->killers[0] != move)
  {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = move;
  }

  Value  bonus       = Value((depth / kOnePly) * (depth / kOnePly) + depth / kOnePly - 1);
  Square prev_square = move_to((ss - 1)->current_move);
  Square prev_own_square = move_to((ss - 2)->current_move);
  Piece  prev_piece  = move_piece((ss - 1)->current_move, ~pos.side_to_move());
  Piece  prev_own_piece = move_piece((ss - 2)->current_move, pos.side_to_move());
  CounterMoveStats &cmh = CounterMoveHistory[prev_piece][prev_square];
  CounterMoveStats &fmh = CounterMoveHistory[prev_own_piece][prev_own_square];
  Thread *this_thread = pos.this_thread();

  this_thread->history_.update(move_piece(move, pos.side_to_move()), move_to(move), bonus);

  if (is_ok((ss - 1)->current_move))
  {
    this_thread->counter_moves_.update(prev_piece, prev_square, move);
    cmh.update(move_piece(move, pos.side_to_move()), move_to(move), bonus);
  }

  if (is_ok((ss - 2)->current_move))
    fmh.update(move_piece(move, pos.side_to_move()), move_to(move), bonus);

  for (int i = 0; i < quiets_count; ++i)
  {
    Move m = quiets[i];
    this_thread->history_.update(move_piece(m, pos.side_to_move()), move_to(m), -bonus);

    if (is_ok((ss - 1)->current_move))
      cmh.update(move_piece(m, pos.side_to_move()), move_to(m), -bonus);

    if (is_ok((ss - 2)->current_move))
      fmh.update(move_piece(m, pos.side_to_move()), move_to(m), -bonus);
  }

  if
  (
    is_ok((ss - 2)->current_move)
    &&
    (ss - 1)->move_count == 1
    &&
    move_capture((ss - 1)->current_move) == kPieceNone
  )
  {
    Square prev_prev_square = move_to((ss - 2)->current_move);
    Piece  prev_prev_piece  = move_piece((ss - 2)->current_move, pos.side_to_move());
    CounterMoveStats &prev_cmh = CounterMoveHistory[prev_prev_piece][prev_prev_square];
    prev_cmh.update(prev_piece, prev_square, -bonus - 2 * (depth + 1) / kOnePly);
  }
}

void
check_time()
{
  static TimePoint last_info_time = now();

  int elapsed = Time.elapsed();
  TimePoint tick = Limits.start_time + elapsed;

  if (tick - last_info_time >= 1000)
  {
    last_info_time = tick;
  }

  if (Limits.ponder)
    return;

  if
  (
    (
      Limits.use_time_management()
      &&
      elapsed > Time.maximum() - 10
    )
    ||
    (Limits.movetime && elapsed >= Limits.movetime)
    ||
    (Limits.nodes && Threads.nodes_searched() >= Limits.nodes)
  )
    Signals.stop = true;
}

} // namespace

string
usi_pv(const Position &pos, Depth depth, Value alpha, Value beta)
{
  std::stringstream ss;
  int elapsed = Time.elapsed() + 1;
  const Search::RootMoveVector &root_moves = pos.this_thread()->root_moves_;
  size_t pv_index = pos.this_thread()->pv_index_;
  size_t multi_pv = std::min((size_t)Options["MultiPV"], root_moves.size());
  uint64_t nodes_searched = Threads.nodes_searched();

  for (size_t i = 0; i < multi_pv; ++i)
  {
    bool updated = (i <= pv_index);

    if (depth == kOnePly && !updated)
      continue;

    Depth d   = updated ? depth : depth - kOnePly;
    Value v = updated ? root_moves[i].score : root_moves[i].previous_score;

    if (ss.rdbuf()->in_avail())
      ss << "\n";

    ss << "info"
       << " depth " << d / kOnePly
       << " seldepth " << pos.this_thread()->max_ply_
       << " multipv " << i + 1
       << " score "     << USI::format_value(v);

    if (i == pv_index)
      ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

    ss << " nodes " << nodes_searched
       << " nps " << nodes_searched * 1000 / elapsed;
#if 0
    if (elapsed > 1000) // Earlier makes little sense
      ss << " hashfull " << TT.hashfull();
#endif
    ss << " time "      << elapsed
       << " pv";

    for (size_t j = 0; j < root_moves[i].pv.size(); ++j)
      ss << " " << USI::format_move(root_moves[i].pv[j]);
  }

  return ss.str();
}

void
RootMove::insert_pv_in_tt(Position& pos)
{
  StateInfo state[kMaxPly];
  StateInfo *st = state;
  TTEntry *tte;
  bool tt_hit;

  size_t idx;
  for (idx = 0; idx < pv.size(); ++idx)
  {
    tte = TT.probe(pos.key(), &tt_hit);

    if (!tt_hit || tte->move(pos) != pv[idx])
      tte->save(pos.key(), kValueNone, kBoundNone, kDepthNone, pv[idx], kValueNone, TT.generation());

    assert(MoveList<kLegal>(pos).contains(pv[idx]));

    pos.do_move(pv[idx], *st++);
  }

  while (idx)
    pos.undo_move(pv[--idx]);
}

bool
RootMove::extract_ponder_from_tt(Position &pos)
{
  StateInfo st;
  bool found;
  bool result = false;

  assert(pv.size() == 1);

  pos.do_move(pv[0], st);
  TTEntry *tte = TT.probe(pos.key(), &found);
  if (found)
  {
    Move m = tte->move(pos);
    if (MoveList<kLegal>(pos).contains(m))
    {
      pv.push_back(m);
      result = true;
    }
  }
  pos.undo_move(pv[0]);

  return result;
}
