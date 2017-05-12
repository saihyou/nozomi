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
#ifdef APERY_BOOK
AperyBook     BookManager;
#else
Book          BookManager;
#endif
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
inline Value
futility_margin(Depth d, bool pv_node)
{
  int v = 150 * d / kOnePly;
  if (pv_node)
    v += 100;
  return Value(v);
}

// move count pruningで使用するテーブル
int FutilityMoveCounts[2][16]; // [improving][depth]

// 探索深さをどのくらい減らすか
Depth Reductions[2][2][64][64][2] = {kDepthZero}; // [pv][improving][depth][move_number][gives_check]

int LMRMoveCounts[16];

template <bool PvNode>
inline Depth
reduction(bool i, Depth d, int mn, bool gives_check)
{
  return Reductions[PvNode][i][std::min(int(d), 63)][std::min(mn, 63)][gives_check];
}

struct EasyMoveManager
{
  void
  clear()
  {
    stable_count = 0;
    expected_position_key = 0;
    pv[0] = pv[1] = pv[2] = kMoveNone;
  }

  Move
  get(Key key) const
  {
    return expected_position_key == key ? pv[2] : kMoveNone;
  }

  void
  update(Position &pos, const std::vector<Move> &new_pv)
  {
    assert(new_pv.size() >= 3);

    stable_count = (new_pv[2] == pv[2]) ? stable_count + 1 : 0;

    if (!std::equal(new_pv.begin(), new_pv.begin() + 3, pv))
    {
      std::copy(new_pv.begin(), new_pv.begin() + 3, pv);

      StateInfo st[2];
      pos.do_move(new_pv[0], st[0]);
      pos.do_move(new_pv[1], st[1]);
      expected_position_key = pos.key();
      pos.undo_move(new_pv[1]);
      pos.undo_move(new_pv[0]);
    }
  }

  int stable_count;
  Key expected_position_key;
  Move pv[3];
};

const int kSkipSize[] = { 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4 };
const int kSkipPhase[] = { 0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7 };

int
stat_bonus(Depth depth)
{
  int d = depth / kOnePly;
  return d * d + 2 * d - 2;
}

EasyMoveManager EasyMove;
Value DrawValue[kNumberOfColor];

template <NodeType NT>
Value 
search
(
  Position &pos,
  SearchStack *ss,
  Value alpha,
  Value beta,
  Depth depth,
  bool cut_node,
  bool skip_early_pruning,
  bool skip_mate=false
);

template <NodeType NT, bool InCheck>
Value
qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth, bool skip_mate=false);

Value
value_to_tt(Value v, int ply);
Value
value_from_tt(Value v, int ply);
void
update_pv(Move *pv, Move move, Move *child_pv);

void
update_cm_stats(SearchStack *ss, Piece pc, Square s, int bonus);

void 
update_stats
(
  const Position &pos,
  SearchStack *ss,
  Move move,
  Move *quiets,
  int quiets_count,
  int bonus
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
        for (int move_count = 3; move_count < 64; ++move_count)
        {
          double r = k[pv][0] + log(depth) * log(move_count) / k[pv][1];

          if (r >= 1.5)
          {
            Reductions[pv][improving][depth][move_count][0] = int(r) * kOnePly;
            Reductions[pv][improving][depth][move_count][1] = std::max(0, int(r) - 1) * kOnePly;
          }

          // NonPVで評価値が悪くなる場合はよりreductionするようにする
          if (pv == 0 && improving == 0 && Reductions[pv][improving][depth][move_count][0] >= 2 * kOnePly)
            Reductions[pv][improving][depth][move_count][0] += kOnePly;
        }
      }
    }
  }

  for (int depth = 0; depth < 16; ++depth)
  {
    FutilityMoveCounts[0][depth] = int(15.0 + 0.74 * pow(depth, 1.50));
    FutilityMoveCounts[1][depth] = int(17.0 + 1.00 * pow(depth, 1.60));
  }

  LMRMoveCounts[0] = 15;
  for (int d = 1; d < 16; ++d)
    LMRMoveCounts[d] = int(LMRMoveCounts[d - 1] + 2 * log(d));
}

void
Search::clear()
{
  TT.clear();

  for (Thread *th : Threads)
  {
    th->history_.clear();
    th->counter_moves_.clear();
    th->from_to_.clear();
    th->counter_move_history_.clear();
    th->counter_move_history_[kEmpty][0].fill(kCounterMoveThreshold - 1);
  }

  Threads.main()->previous_score = kValueInfinite;
}

void
MainThread::search()
{
  Color us = root_pos_.side_to_move();
  Time.init(Limits, us, root_pos_.game_ply());
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
    goto exit;
  }

  if (root_pos_.is_decralation_win())
    goto exit;

  if (Options["OwnBook"] && !Limits.infinite && !Limits.mate)
  {
#ifdef APERY_BOOK
    std::tuple<Move, Value> book_move_score = BookManager.probe(root_pos_, Options["BookFile"], Options["Best_Book_Move"]);
    Move book_move = std::get<0>(book_move_score);
#else
    Move book_move = BookManager.get_move(root_pos_);
#endif
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
  if (Options["MultiPV"] == 1 && search_best_thread && !this->easy_move_played)
  {
    for (Thread *th : Threads)
    {
      Depth depth_diff = th->completed_depth_ - best_thread->completed_depth_;
      Value score_diff = th->root_moves_[0].score - best_thread->root_moves_[0].score;
      if
      (
        (depth_diff > 0 && score_diff >= 0)
        ||
        (score_diff > 0 && depth_diff >= 0)
      )
        best_thread = th;
    }
  }

  if (root_pos_.is_decralation_win())
  {
    sync_cout << "bestmove win" << sync_endl;
    return;
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

void
Thread::search()
{
  SearchStack stack[kMaxPly + 7];
  SearchStack *ss = stack + 4; // (ss - 4)から(ss + 2)までアクセス可能
  Value best_value = -kValueInfinite;
  Value alpha      = -kValueInfinite;
  Value beta       = kValueInfinite;
  Value delta      = -kValueInfinite;
  Move easy_move   = kMoveNone;
  MainThread *main_thread = (this == Threads.main() ? Threads.main() : nullptr);

  std::memset(ss - 4, 0, 8 * sizeof(SearchStack));
  for (int i = 4; i > 0; --i)
    (ss - i)->counter_moves = &this->counter_move_history_[kEmpty][0];

  completed_depth_ = kDepthZero;

  if (main_thread)
  {
    easy_move = EasyMove.get(root_pos_.key());
    EasyMove.clear();
    main_thread->easy_move_played = false;
    main_thread->failed_low = false;
    main_thread->best_move_changes = 0;
    TT.new_search();
  }

  size_t multi_pv = Options["MultiPV"];

  while (++root_depth_ < kDepthMax && !Signals.stop && (!Limits.depth || root_depth_ < Limits.depth))
  {
    if (index_ > 0)
    {
      int i = (index_ - 1) % 20;
      if (((root_depth_ / kOnePly + root_pos_.game_ply() + kSkipPhase[i]) / kSkipSize[i]) % 2)
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
        best_value = ::search<kPV>(root_pos_, ss, alpha, beta, root_depth_, false, false);

        std::stable_sort(root_moves_.begin() + pv_index_, root_moves_.end());

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

    if (Limits.use_time_management() && !Time.only_byoyomi())
    {
      if (!Signals.stop && !Signals.stop_on_ponder_hit)
      {
        int failed_low = static_cast<int>(main_thread->failed_low);
        int previous_score = static_cast<int>(best_value - main_thread->previous_score);
        int improving_factor =
          std::max
          (
            229,
            std::min(715, 357 + 119 * failed_low - 6 * previous_score)
          );
        double unstable_pv_factor = 1 + main_thread->best_move_changes;
        bool do_easy_move =
          root_moves_[0].pv[0] == easy_move
          &&
          main_thread->best_move_changes < 0.03
          &&
          Time.elapsed() > Time.optimum() * 5 / 42;

        if
        (
          root_moves_.size() == 1
          ||
          Time.elapsed() > Time.optimum() * unstable_pv_factor * improving_factor / 628
          ||
          (main_thread->easy_move_played = do_easy_move, do_easy_move)
        )
        {
          if (Limits.ponder)
            Signals.stop_on_ponder_hit = true;
          else
            Signals.stop = true;
        }
      }

      if (root_moves_[0].pv.size() >= 3)
        EasyMove.update(root_pos_, root_moves_[0].pv);
      else
        EasyMove.clear();
    }
  }

  if (!main_thread)
    return;

  if (EasyMove.stable_count < 6 || main_thread->easy_move_played)
    EasyMove.clear();
}

Value
Search::search(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth)
{
  return ::search<kPV>(pos, ss, alpha, beta, depth, false, false);
}

Value
Search::qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta)
{
  if (pos.in_check())
    return ::qsearch<kPV, true>(pos, ss, alpha, beta, kDepthZero);
  else
    return ::qsearch<kPV, false>(pos, ss, alpha, beta, kDepthZero);
}

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
  bool cut_node,
  bool skip_early_pruning,
  bool skip_mate
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
  Value best_value;
  Value value;
  Value tt_value;
  Value eval;
  Value null_value;
  bool in_check;
  bool gives_check;
  bool singular_extension_node;
  bool improving;
  bool capture;
  bool do_full_depth_search;
  bool tt_hit;
  bool move_count_pruning;
  Piece moved_piece;
  int move_count;
  int quiet_count;

  // Initialize node
  Thread *this_thread = pos.this_thread();
  in_check = pos.in_check();
  move_count = quiet_count = ss->move_count = 0;
  best_value = -kValueInfinite;
  ss->ply = (ss - 1)->ply + 1;
  ss->history = 0;

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
    // 宣言勝ち
    if (pos.is_decralation_win())
      return mate_in(ss->ply - 1);

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

  ss->current_move  = (ss + 1)->excluded_move = best_move = kMoveNone;
  ss->counter_moves = nullptr;
  (ss + 2)->killers[0] = (ss + 2)->killers[1] = kMoveNone;
  Square prev_sq = move_to((ss - 1)->current_move);
  Piece  prev_piece = move_piece((ss - 1)->current_move, ~pos.side_to_move());

  // Transposition table lookup
  excluded_move = ss->excluded_move;
  if (excluded_move == kMoveNone)
  {
    position_key = pos.key();
    tte = TT.probe(position_key, &tt_hit);
    tt_move = root_node
      ?
      this_thread->root_moves_[this_thread->pv_index_].pv[0]
      :
      (
        tt_hit
        ?
        tte->move()
        :
        kMoveNone
      );
    tt_value = tt_hit ? value_from_tt(tte->value(), ss->ply) : kValueNone;
  }
  else
  {
    tt_move = kMoveNone;
    tt_value = kValueZero;
    tt_hit = false;
    tte = nullptr;
    position_key = 0;
  }

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
    if (tt_move)
    {
      if (tt_value >= beta)
      {
        if (!move_is_capture(tt_move))
          update_stats
          (
            pos,
            ss,
            tt_move,
            nullptr,
            0,
            stat_bonus(depth)
          );

        if ((ss - 1)->move_count == 1 && !move_is_capture((ss - 1)->current_move))
          update_cm_stats
          (
            ss - 1,
            prev_piece,
            prev_sq,
            -stat_bonus(depth + kOnePly)
          );
      }
      else if (!move_is_capture(tt_move))
      {
        int penalty  = -stat_bonus(depth);
        Piece tt_piece = move_piece(tt_move, pos.side_to_move());
        Square tt_to   = move_to(tt_move);
        this_thread->history_.update(tt_piece, tt_to, penalty);
        this_thread->from_to_.update(pos.side_to_move(), tt_move, penalty);
        update_cm_stats(ss, tt_piece, tt_to, penalty);
      }
    }

    return tt_value;
  }

  // 1手詰め判定
  // 処理が重いのでなるべくなら呼び出さないようにしたい
  if
  (
    !root_node
    &&
    !skip_mate
    &&
    !tt_hit
    &&
    !in_check
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
        TT.generation()
      );

      return best_value;
    }
  }

  // 現局面の静的評価
  ss->static_eval = evaluate(pos, ss);
  if (in_check)
  {
    ss->static_eval = eval = kValueNone;
    goto moves_loop;
  }
  else if (tt_hit)
  {
    eval = ss->static_eval;

    if (tt_value != kValueNone)
    {
      if (tte->bound() & (tt_value > eval ? kBoundLower : kBoundUpper))
        eval = tt_value;
    }
  }
  else
  {
    eval = ss->static_eval;
  }

  if (skip_early_pruning)
    goto moves_loop;

  // Razoring
  if
  (
    !pv_node
    &&
    depth < 4 * kOnePly
    &&
    eval + razor_margin(depth) <= alpha
  )
  {
    if (depth <= kOnePly)
      return qsearch<kNonPV, false>(pos, ss, alpha, beta, kDepthZero, true);

    Value ralpha = alpha - razor_margin(depth);
    Value v = qsearch<kNonPV, false>(pos, ss, ralpha, ralpha + 1, kDepthZero, true);
    if (v <= ralpha)
      return v;
  }

  // Futility pruning: child node
  if
  (
    !root_node
    &&
    depth < 7 * kOnePly
    &&
    eval - futility_margin(depth, pv_node) >= beta
    &&
    eval < kValueKnownWin
  )
    return eval;

  // Null move search
  if
  (
    !pv_node
    &&
    eval >= beta
    &&
    (ss->static_eval >= beta - 35 * (depth / kOnePly - 6) || depth >= 13 * kOnePly)
  )
  {
    ss->current_move  = kMoveNull;
    ss->counter_moves = &this_thread->counter_move_history_[kEmpty][0];

    assert(eval - beta >= 0);

    Depth re
      = ((823 + 67 * depth) / 256 + std::min(int(eval - beta) / Eval::kPawnValue, 3)) * kOnePly;

    pos.do_null_move(st);
    (ss + 1)->evaluated = false;
    null_value =
      depth - re < kOnePly
      ?
      -qsearch<kNonPV, false>(pos, ss + 1, -beta, -beta + 1, kDepthZero, true)
      : 
      -search<kNonPV>
      (
        pos,
        ss + 1,
        -beta,
        -beta + 1,
        depth - re,
        !cut_node,
        true,
        true
      );
    pos.undo_null_move();

    if (null_value >= beta)
    {
      if (null_value >= kValueMateInMaxPly)
        null_value = beta;

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
    MovePicker mp(pos, tt_move, v);
    CheckInfo ci(pos);

    while ((move = mp.next_move()) != kMoveNone)
    {
      if (pos.legal(move, ci.pinned))
      {
        ss->current_move  = move;
        ss->counter_moves = &this_thread->counter_move_history_[move_piece(move, pos.side_to_move())][move_to(move)];
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
            !cut_node,
            false,
            rdepth < 3 * kOnePly
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
    depth >= 6 * kOnePly
    &&
    !tt_move
    &&
    (pv_node || ss->static_eval + 256 >= beta)
  )
  {
    Depth d = (3 * depth / (4 * kOnePly) - 2) * kOnePly;
    search<NT>
    (
      pos,
      ss,
      alpha,
      beta,
      d,
      cut_node,
      true,
      true
    );

    tte = TT.probe(position_key, &tt_hit);
    tt_move = tt_hit ? tte->move() : kMoveNone;
  }

moves_loop:
  const CounterMoveStats &cmh = *(ss - 1)->counter_moves;
  const CounterMoveStats &fmh = *(ss - 2)->counter_moves;
  const CounterMoveStats &fm2 = *(ss - 4)->counter_moves;

  MovePicker mp(pos, tt_move, depth, ss);
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
    &&
    tt_value != kValueNone
    &&
    !excluded_move
    &&
    (tte->bound() & kBoundLower)
    &&
    tte->depth() >= depth - 3 * kOnePly;

  Move current_best_move = kMoveNone;
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
#if 0
    if (root_node && this_thread == Threads.main() && Time.elapsed() > 3000)
    {
      sync_cout << "info depth " << depth
                << " currmove " << USI::format_move(move)
                << " currmovenumber " << move_count + this_thread->pv_index_ << sync_endl;
    }
#endif
    if (pv_node)
      (ss + 1)->pv = nullptr;

    ext = kDepthZero;

    // Stockfishだとpromotionも入っているが将棋としてはpromotionなんて普通の手なので除外
    capture = move_is_capture(move);
    moved_piece = move_piece(move, pos.side_to_move());

    gives_check = pos.gives_check(move, ci);
    move_count_pruning = (depth < 16 * kOnePly && move_count >= FutilityMoveCounts[improving][depth / kOnePly]);

    // Singular extension search
    if
    (
      singular_extension_node
      &&
      move == tt_move
      &&
      pos.legal(move, ci.pinned)
    )
    {
      Value r_beta = std::max(tt_value - 8 * depth / kOnePly, -kValueMate);
      Depth d = (depth / (2 * kOnePly)) * kOnePly;
      ss->excluded_move = move;
      value = search<kNonPV>(pos, ss, r_beta - 1, r_beta, d, cut_node, true, true);
      ss->excluded_move = kMoveNone;

      if (value < r_beta)
        ext = kOnePly;
    }
    else if
    (
      gives_check
      &&
      (
        pos.see_ge(move, kValueZero)
        ||
        pos.continuous_checks(pos.side_to_move()) > 2
      )
    )
    {
      ext = kOnePly;
    }

    new_depth = depth - kOnePly + ext;

    // Pruning at shallow depth
    if
    (
      !pv_node
      &&
      best_value > kValueMatedInMaxPly
    )
    {
      if (!capture && !gives_check)
      {
        // Move count based pruning
        if (move_count_pruning)
        {
          if
          (
            is_ok(current_best_move)
            &&
            is_ok((ss - 1)->current_move)
            &&
            move_to(current_best_move) == move_to((ss - 1)->current_move)
          )
            break;

          continue;
        }

        int lmr_depth = std::max(new_depth - reduction<pv_node>(improving, depth, move_count, gives_check), kDepthZero) / kOnePly;

        // Countermove based pruning
        if
        (
          lmr_depth < 3
          &&
          (cmh[moved_piece][move_to(move)] < kCounterMoveThreshold)
          &&
          (fmh[moved_piece][move_to(move)] < kCounterMoveThreshold)
        )
          continue;

        // Futility pruning: parent node
        if 
        (
          lmr_depth < 7
          &&
          !in_check
          &&
          ss->static_eval + 256 + 200 * lmr_depth <= alpha
        )
          continue;

        if 
        (
          lmr_depth < 8
          &&
          !pos.see_ge(move, Value(-35 * lmr_depth * lmr_depth))
        )
          continue;
      }
      else if (depth < 7 * kOnePly && !ext)
      {
        Value v = -Value(400 - 100 * pv_node + 35 * depth / kOnePly * depth / kOnePly);
        if (!pos.see_ge(move, v))
          continue;
      }
    }

    prefetch(TT.first_entry(pos.key_after(move)));

    if (!root_node && !pos.legal(move, ci.pinned))
    {
      ss->move_count = --move_count;
      continue;
    }

    ss->current_move  = move;
    ss->counter_moves = &this_thread->counter_move_history_[moved_piece][move_to(move)];

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
      (
        !capture 
        ||
        move_count > LMRMoveCounts[std::min(static_cast<int>(depth), 15)]
      )
    )
    {
      Depth r = reduction<pv_node>(improving, depth, move_count, gives_check);

      if (capture)
      {
        r -= r ? kOnePly : kDepthZero;
      }
      else
      {
        // cut_nodeの場合はreductionを増やす
        if (cut_node)
          r += kOnePly;
        else if (!pos.see_ge_reverse_move(move, kValueZero))
          r -= kOnePly;

        ss->history =
          this_thread->history_[moved_piece][move_to(move)]
          +
          cmh[moved_piece][move_to(move)]
          +
          fmh[moved_piece][move_to(move)]
          +
          fm2[moved_piece][move_to(move)]
          +
          this_thread->from_to_.get(~pos.side_to_move(), move)
          -
          4000;

        if (ss->history > 0 && (ss - 1)->history < 0)
          r -= kOnePly;
        else if (ss->history < 0 && (ss - 1)->history > 0)
          r += kOnePly;

        r = std::max(kDepthZero, (r / kOnePly - ss->history / 20000) * kOnePly);
      }

      Depth d = std::max(new_depth - r, kOnePly);

      value = 
        -search<kNonPV>
        (
          pos,
          ss + 1,
          -(alpha + 1),
          -alpha,
          d,
          true,
          false,
          d < 3 * kOnePly
        );

      do_full_depth_search = (value > alpha && d != kDepthZero);
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
          !cut_node,
          false,
          new_depth < 3 * kOnePly
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
          false,
          false,
          new_depth < 3 * kOnePly
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
      current_best_move = move;
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
  else if (best_move != kMoveNone)
  {
    if (!move_is_capture(best_move))
      update_stats
      (
        pos,
        ss,
        best_move,
        quiets_searched,
        quiet_count,
        stat_bonus(depth)
      );
    
    if ((ss - 1)->move_count == 1 && !move_is_capture((ss - 1)->current_move))
      update_cm_stats
      (
        ss - 1,
        prev_piece,
        prev_sq,
        -stat_bonus(depth + kOnePly)
      );
  }
  else if
  (
    depth >= 3 * kOnePly
    &&
    !move_is_capture((ss - 1)->current_move)
    &&
    is_ok((ss - 1)->current_move)
  )
  {
    update_cm_stats
    (
      ss - 1,
      prev_piece,
      prev_sq,
      stat_bonus(depth)
    );
  }

  if (excluded_move == kMoveNone)
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
      TT.generation()
    );

  assert(best_value > -kValueInfinite && best_value < kValueInfinite);

  return best_value;
}

// 静止探索
template <NodeType NT, bool InCheck>
Value
qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta, Depth depth, bool skip_mate)
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
  tt_move = tt_hit ? tte->move() : kMoveNone;
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
    if (!skip_mate && !tt_hit)
    {
      Move mate_move = search_mate1ply(pos);
      if (mate_move != kMoveNone)
      {
        tte->save
        (
          position_key,
          value_to_tt(mate_in(ss->ply + 1), ss->ply),
          kBoundExact,
          tt_depth,
          mate_move,
          TT.generation()
        );
        return mate_in(ss->ply + 1);
      }
    }

    ss->static_eval = evaluate(pos, ss);
    if (tt_hit)
    {
      best_value = ss->static_eval;

      if (tt_value != kValueNone)
      {
        if (tte->bound() & (tt_value > best_value ? kBoundLower : kBoundUpper))
          best_value = tt_value;
      }
    }
    else
    {
      best_value = ss->static_eval;
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
          TT.generation()
        );
      }

      return best_value;
    }

    if (PvNode && best_value > alpha)
      alpha = best_value;

    futility_base = best_value + 128;
  }

  MovePicker mp(pos, tt_move, depth, move_to((ss - 1)->current_move));
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

      if (futility_base <= alpha && !pos.see_ge(move, kValueZero + 1))
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
      !pos.see_ge(move, kValueZero)
    )
    {
      if (!gives_check)
        continue;

      if ((move_capture(move) & 0xF) < kSilver)
        continue;
    }

    prefetch(TT.first_entry(pos.key_after(move)));

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
update_cm_stats(SearchStack *ss, Piece pc, Square s, int bonus)
{
  for (int i : {1, 2, 4})
    if (is_ok((ss - i)->current_move))
      (ss - i)->counter_moves->update(pc, s, bonus);
}

void 
update_stats
(
  const Position &pos,
  SearchStack *ss,
  Move move,
  Move *quiets,
  int quiets_count,
  int bonus
) 
{
  if (ss->killers[0] != move)
  {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = move;
  }

  Color c = pos.side_to_move();
  Piece moved_piece = move_piece(move, pos.side_to_move());

  Piece prev_piece = move_piece((ss - 1)->current_move, ~pos.side_to_move());
  Thread *this_thread = pos.this_thread();

  this_thread->history_.update(moved_piece, move_to(move), bonus);
  this_thread->from_to_.update(c, move, bonus);
  update_cm_stats(ss, moved_piece, move_to(move), bonus);
  if (is_ok((ss - 1)->current_move))
  {
    Square prev_sq = move_to((ss - 1)->current_move);
    this_thread->counter_moves_.update(prev_piece, prev_sq, move);
  }

  for (int i = 0; i < quiets_count; ++i)
  {
    Move m = quiets[i];
    this_thread->history_.update(move_piece(m, pos.side_to_move()), move_to(m), -bonus);
    this_thread->from_to_.update(c, m, -bonus);
    update_cm_stats(ss, move_piece(m, pos.side_to_move()), move_to(m), -bonus);
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
    Move m = tte->move();
    if (MoveList<kLegal>(pos).contains(m))
    {
      pv.push_back(m);
      result = true;
    }
  }
  pos.undo_move(pv[0]);

  return result;
}
