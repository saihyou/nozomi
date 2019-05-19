/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

  This code is based on Stockfish (Chess playing engin).
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord
  Romstad

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

#include "search.h"
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
#include "stats.h"
#include "thread.h"
#include "timeman.h"
#include "transposition_table.h"
#include "usi.h"

namespace Search {
SignalsType Signals;
LimitsType Limits;
StateStackPtr SetupStates;
#ifdef APERY_BOOK
AperyBook     BookManager;
#else
Book          BookManager;
#endif
Value DrawValue[kNumberOfColor];
}  // namespace Search

using Eval::evaluate;
using std::string;
using namespace Search;

namespace {
// ノードの種類
enum NodeType { kPV, kNonPV };

// razor margin
constexpr int kRazorMargin = 600;

// futility margin
inline Value futility_margin(Depth d, bool improving) {
  return Value((175 - 50 * improving) * d / kOnePly);
}

constexpr int FutilityMoveCount(bool improving, int depth) {
  return 8 + (5 + depth * depth) * (1 + improving) / 2;
}

int Reductions[kMaxMoves];

template <bool PvNode>
inline Depth Reduction(bool i, Depth d, int mn) {
  int r = Reductions[d / kOnePly] * Reductions[mn] / 1024;
  return ((r + 512) / 1024 + (!i && r > 1024) - PvNode) * kOnePly;
}

int stat_bonus(Depth depth) {
  int d = depth / kOnePly;
  return d > 17 ? 0 : 29 * d * d + 138 * d - 134;
}

template <NodeType NT>
Value search(Position &pos, SearchStack *ss, Value alpha, Value beta,
             Depth depth, bool cut_node, bool skip_mate = false);

template <NodeType NT, bool InCheck>
Value qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta,
              Depth depth, bool skip_mate = false);

Value value_to_tt(Value v, int ply);
Value value_from_tt(Value v, int ply);
void update_pv(Move *pv, Move move, Move *child_pv);

void update_cm_stats(SearchStack *ss, Piece pc, Square s, int bonus);

void update_stats(const Position &pos, SearchStack *ss, Move move, Move *quiets,
                  int quiets_count, int bonus);

void update_capture_stats(const Position &pos, Move move, Move *captures,
                          int capture_count, int bonus);

void check_time();
}  // namespace

string usi_pv(const Position &pos, Depth depth, Value alpha, Value beta);

// ReductionsとFutilityMoveCountの初期化
void Search::init() {
  for (int i = 1; i < kMaxMoves; ++i) {
    if (i < 3) {
      Reductions[i] = 0;
    } else {
      Reductions[i] = int(1024 * std::log(std::min(i, 256)) / std::sqrt(2.25));
    }
  }
}

void Search::clear() {
  TT.Clear();

  for (Thread *th : Threads) {
    th->counter_moves_.fill(kMoveNone);
    th->main_history_.fill(0);
    th->capture_history_.fill(0);
  }

  Threads.main()->previous_score = kValueInfinite;
  Threads.main()->previous_time_reduction = 1;
}

void MainThread::search() {
  Color us = root_pos_.side_to_move();
  Time.init(Limits, us, root_pos_.game_ply());
  TT.NewSearch();
  bool search_best_thread = true;

  int contempt = Options["Contempt"];
  DrawValue[us] = kValueDraw - Value(contempt);
  DrawValue[~us] = kValueDraw + Value(contempt);

  if (root_moves_.empty()) {
    root_moves_.push_back(RootMove(kMoveNone));
    sync_cout << "info depth 0 score " << USI::format_value(-kValueMate)
              << sync_endl;
    search_best_thread = false;
    goto exit;
  }

  if (root_pos_.is_decralation_win()) goto exit;

  if (Options["OwnBook"] && !Limits.infinite && !Limits.mate) {
#ifdef APERY_BOOK
    std::tuple<Move, Value> book_move_score = BookManager.probe(root_pos_, Options["BookFile"], Options["Best_Book_Move"]);
    Move book_move = std::get<0>(book_move_score);
#else
    Move book_move = BookManager.get_move(root_pos_);
#endif
    if (book_move != kMoveNone &&
        std::count(root_moves_.begin(), root_moves_.end(), book_move)) {
      std::swap(root_moves_[0],
                *std::find(root_moves_.begin(), root_moves_.end(), book_move));
      root_moves_[0].score = kValueZero;
      search_best_thread = false;
      goto exit;
    }
  }

  for (Thread *th : Threads) {
    th->max_ply_ = 0;
    th->root_depth_ = kDepthZero;
    th->best_move_changes_ = 0;
    if (th != this) {
      th->root_pos_ = Position(root_pos_, th);
      th->root_moves_ = root_moves_;
      th->start_searching();
    }
  }

  Thread::search();

exit:
  if (!Signals.stop && (Limits.ponder || Limits.infinite)) {
    Signals.stop_on_ponder_hit = true;
    wait(Signals.stop);
  }

  Signals.stop = true;

  for (Thread *th : Threads) {
    if (th != this) th->wait_for_search_finished();
  }

  Thread *best_thread = this;
  if (Options["MultiPV"] == 1 && search_best_thread) {
    std::map<Move, int64_t> votes;
    Value min_score = this->root_moves_[0].score;

    for (Thread *th : Threads)
      min_score = std::min(min_score, th->root_moves_[0].score);

    // Vote according to score and depth
    for (Thread *th : Threads) {
      int64_t s = th->root_moves_[0].score - min_score + 1;
      votes[th->root_moves_[0].pv[0]] +=
          200 + s * s * int(th->completed_depth_);
    }

    // Select best thread
    auto best_vote = votes[this->root_moves_[0].pv[0]];
    for (Thread *th : Threads) {
      if (votes[th->root_moves_[0].pv[0]] > best_vote) {
        best_vote = votes[th->root_moves_[0].pv[0]];
        best_thread = th;
      }
    }
  }

  if (root_pos_.is_decralation_win()) {
    sync_cout << "bestmove win" << sync_endl;
    return;
  }

  if (best_thread->root_moves_[0].pv[0] != kMoveNone) {
    previous_score = best_thread->root_moves_[0].score;

    sync_cout << usi_pv(best_thread->root_pos_, best_thread->completed_depth_,
                        -kValueInfinite, kValueInfinite)
              << sync_endl;

    sync_cout << "bestmove "
              << USI::format_move(best_thread->root_moves_[0].pv[0]);

    if (best_thread->root_moves_[0].pv.size() > 1 ||
        best_thread->root_moves_[0].extract_ponder_from_tt(root_pos_))
      std::cout << " ponder "
                << USI::format_move(best_thread->root_moves_[0].pv[1]);

    std::cout << sync_endl;
  } else {
    sync_cout << "bestmove resign" << sync_endl;
  }
}

void Thread::search() {
  SearchStack stack[kMaxPly + 10];
  SearchStack *ss = stack + 7;
  Move pv[kMaxPly + 1];
  Value best_value = -kValueInfinite;
  Value alpha = -kValueInfinite;
  Value beta = kValueInfinite;
  Value delta = -kValueInfinite;
  Move last_best_move = kMoveNone;
  Depth last_best_move_depth = kDepthZero;
  MainThread *main_thread = (this == Threads.main() ? Threads.main() : nullptr);
  double time_reduction = 1.0;
  double total_best_move_changes = 0;

  std::memset(ss - 7, 0, 10 * sizeof(SearchStack));
  for (int i = 7; i > 0; i--)
    (ss - i)->continuation_history =
        &this->continuation_history_[kPieceNone][0];  // Use as sentinel

  ss->pv = pv;
  completed_depth_ = kDepthZero;

  size_t multi_pv = Options["MultiPV"];

  while (++root_depth_ < kDepthMax && !Signals.stop &&
         (!Limits.depth || root_depth_ < Limits.depth)) {
    if (main_thread) {
      total_best_move_changes /= 2;
    }

    for (RootMove &rm : root_moves_) rm.previous_score = rm.score;

    for (pv_index_ = 0; pv_index_ < multi_pv && !Signals.stop; ++pv_index_) {
      if (root_depth_ >= 5 * kOnePly) {
        delta = Value(64);
        alpha = std::max(root_moves_[pv_index_].previous_score - delta,
                         -kValueInfinite);
        beta = std::min(root_moves_[pv_index_].previous_score + delta,
                        kValueInfinite);
      }

      int failed_high_count = 0;
      while (true) {
        Depth adjusted_depth =
            std::max(kOnePly, root_depth_ - failed_high_count * kOnePly);
        best_value = ::search<kPV>(root_pos_, ss, alpha, beta, adjusted_depth,
                                   false, false);

        std::stable_sort(root_moves_.begin() + pv_index_, root_moves_.end());

        if (Signals.stop) break;

        if (main_thread && multi_pv == 1 &&
            (best_value <= alpha || best_value >= beta) &&
            Time.elapsed() > 3000)
          sync_cout << usi_pv(root_pos_, root_depth_, alpha, beta) << sync_endl;

        if (best_value <= alpha) {
          beta = (alpha + beta) / 2;
          alpha = std::max(best_value - delta, -kValueInfinite);

          if (main_thread) {
            failed_high_count = 0;
            Signals.stop_on_ponder_hit = false;
          }
        } else if (best_value >= beta) {
          beta = std::min(best_value + delta, kValueInfinite);
        } else {
          break;
        }

        delta += delta / 4 + 5;
        assert(alpha >= -kValueInfinite && beta <= kValueInfinite);
      }

      std::stable_sort(root_moves_.begin(),
                       root_moves_.begin() + pv_index_ + 1);

      if (main_thread && (Signals.stop || pv_index_ + 1 == multi_pv ||
                          Time.elapsed() > 3000)) {
        sync_cout << usi_pv(root_pos_, root_depth_, alpha, beta) << sync_endl;
      }
    }

    if (!Signals.stop) completed_depth_ = root_depth_;

    if (root_moves_[0].pv[0] != last_best_move) {
      last_best_move = root_moves_[0].pv[0];
      last_best_move_depth = root_depth_;
    }

    if (Limits.mate && best_value >= kValueMateInMaxPly &&
        kValueMate - best_value <= 2 * Limits.mate)
      Signals.stop = true;

    if (root_depth_ > 10 && (best_value >= kValueMate - root_depth_ ||
                             best_value <= -kValueMate + root_depth_))
      break;

    if (!main_thread) continue;

    if (Limits.use_time_management() && !Time.only_byoyomi()) {
      if (!Signals.stop && !Signals.stop_on_ponder_hit) {
        double falling_eval =
            (314 + 9 * (main_thread->previous_score - best_value)) / 581.0;
        falling_eval = (falling_eval < 0.5)
                           ? 0.5
                           : (falling_eval > 1.5) ? 1.5 : falling_eval;

        time_reduction =
            last_best_move_depth + 10 * kOnePly < completed_depth_ ? 1.95 : 1.0;
        double reduction =
            std::pow(main_thread->previous_time_reduction, 0.528) /
            time_reduction;

        for (auto th : Threads) {
          total_best_move_changes += th->best_move_changes_;
          th->best_move_changes_ = 0;
        }

        double best_move_instability =
            1 + total_best_move_changes / Threads.size();

        if (root_moves_.size() == 1 ||
            Time.elapsed() > Time.optimum() * falling_eval * reduction *
                                 best_move_instability) {
          if (Limits.ponder)
            Signals.stop_on_ponder_hit = true;
          else
            Signals.stop = true;
        }
      }
    }
  }

  if (!main_thread) return;

  main_thread->previous_time_reduction = time_reduction;
}

Value Search::search(Position &pos, SearchStack *ss, Value alpha, Value beta,
                     Depth depth) {
  return ::search<kPV>(pos, ss, alpha, beta, depth, false, false);
}

Value Search::qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta) {
  if (pos.in_check())
    return ::qsearch<kPV, true>(pos, ss, alpha, beta, kDepthZero);
  else
    return ::qsearch<kPV, false>(pos, ss, alpha, beta, kDepthZero);
}

bool Search::pv_is_draw(Position &pos) {
  StateInfo st[kMaxPly];
  auto &pv = pos.this_thread()->root_moves_[0].pv;

  for (size_t i = 0; i < pv.size(); ++i) pos.do_move(pv[i], st[i]);

  Repetition in_repetition = pos.in_repetition();

  for (size_t i = pv.size(); i > 0; --i) pos.undo_move(pv[i - 1]);

  return in_repetition == kRepetition;
}

namespace {
// 探索のメイン処理
template <NodeType NT>
Value search(Position &pos, SearchStack *ss, Value alpha, Value beta,
             Depth depth, bool cut_node, bool skip_mate) {
  const bool pv_node = NT == kPV;
  const bool root_node = pv_node && (ss - 1)->ply == 0;

  assert(-kValueInfinite <= alpha && alpha < beta && beta <= kValueInfinite);
  assert(pv_node || (alpha == beta - 1));
  assert(depth > kDepthZero);

  Move pv[kMaxPly + 1];
  Move quiets_searched[64];
  Move capture_searched[32];
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
  Value pure_static_eval;
  Value null_value;
  bool in_check;
  bool gives_check;
  bool singular_extension_node;
  bool capture;
  bool do_full_depth_search;
  bool tt_hit;
  bool pv_hit;
  bool move_count_pruning;
  bool improving;
  Piece moved_piece;
  int move_count;
  int quiet_count;
  int capture_count;

  // Initialize node
  Thread *this_thread = pos.this_thread();
  in_check = pos.in_check();
  Color us = pos.side_to_move();
  move_count = quiet_count = capture_count = ss->move_count = 0;
  best_value = -kValueInfinite;
  ss->ply = (ss - 1)->ply + 1;
  position_key = pos.key();

  // 残り時間
  if (this_thread->reset_calls_.load(std::memory_order_relaxed)) {
    this_thread->reset_calls_ = false;
    this_thread->calls_count_ = 0;
  }
  if (++this_thread->calls_count_ > 4096) {
    for (Thread *th : Threads) th->calls_count_ = true;

    check_time();
  }

  // sel_depth用の情報を更新する
  if (pv_node && this_thread->max_ply_ < ss->ply)
    this_thread->max_ply_ = ss->ply;

  if (!root_node) {
    // 宣言勝ち
    if (pos.is_decralation_win()) return MateIn(ss->ply - 1);

    // 探索の中断と千日手チェック
    Repetition repetition = ((ss - 1)->current_move != kMoveNull)
                                ? pos.in_repetition()
                                : kNoRepetition;
    if (Signals.stop.load(std::memory_order_relaxed) ||
        repetition == kRepetition || ss->ply >= kMaxPly)
      return ss->ply >= kMaxPly && !in_check ? evaluate(pos, ss)
                                             : DrawValue[pos.side_to_move()];

    // 連続王手千日手
    if (repetition == kPerpetualCheckWin) {
      return MateIn(ss->ply);
    } else if (repetition == kPerpetualCheckLose) {
      return MatedIn(ss->ply);
    }

    // 盤上は同一だが、手駒だけ損するケース
    if (ss->ply != 2) {
      if (repetition == kBlackWinRepetition) {
        if (pos.side_to_move() == kWhite)
          return -kValueSamePosition;
        else
          return kValueSamePosition;
      } else if (repetition == kBlackLoseRepetition) {
        if (pos.side_to_move() == kBlack)
          return -kValueSamePosition;
        else
          return kValueSamePosition;
      }
    }

    // Mate distance pruning
    alpha = std::max(MatedIn(ss->ply), alpha);
    beta = std::min(MateIn(ss->ply + 1), beta);
    if (alpha >= beta) return alpha;
  }

  assert(0 <= ss->ply && ss->ply < kMaxPly);

  ss->current_move = (ss + 1)->excluded_move = best_move = kMoveNone;
  ss->continuation_history = &this_thread->continuation_history_[kPieceNone][0];
  (ss + 2)->killers[0] = (ss + 2)->killers[1] = kMoveNone;
  (ss + 2)->stat_score = 0;
  Square prev_sq = move_to((ss - 1)->current_move);
  Piece prev_piece = move_piece((ss - 1)->current_move, ~pos.side_to_move());

  // Transposition table lookup
  excluded_move = ss->excluded_move;
  if (excluded_move == kMoveNone) {
    tte = TT.Probe(position_key, &tt_hit);
    tt_move = root_node ? this_thread->root_moves_[this_thread->pv_index_].pv[0]
                        : (tt_hit ? tte->move() : kMoveNone);
    tt_value = tt_hit ? value_from_tt(tte->value(), ss->ply) : kValueNone;
    pv_hit = (tt_hit && tte->pv_hit()) || (pv_node && depth > 4 * kOnePly);
  } else {
    tt_move = kMoveNone;
    tt_value = kValueZero;
    tt_hit = false;
    pv_hit = false;
    tte = nullptr;
  }

  // PV nodeのときはtransposition tableの手を使用しない
  if (!pv_node && tt_hit && tte->depth() >= depth &&
      tt_value != kValueNone  // transposition tableの値が壊れている時になりうる
      && (tt_value >= beta ? (tte->bound() & kBoundLower)
                           : (tte->bound() & kBoundUpper))) {
    if (tt_move) {
      if (tt_value >= beta) {
        if (!move_is_capture(tt_move))
          update_stats(pos, ss, tt_move, nullptr, 0, stat_bonus(depth));
        else
          update_capture_stats(pos, tt_move, nullptr, 0, stat_bonus(depth));

        if ((ss - 1)->move_count == 1 &&
            !move_is_capture((ss - 1)->current_move))
          update_cm_stats(ss - 1, prev_piece, prev_sq,
                          -stat_bonus(depth + kOnePly));
      } else if (!move_is_capture(tt_move)) {
        int penalty = -stat_bonus(depth);
        Piece tt_piece = move_piece(tt_move, pos.side_to_move());
        Square tt_to = move_to(tt_move);
        this_thread->main_history_[us][FromTo(tt_move)] << penalty;
        update_cm_stats(ss, tt_piece, tt_to, penalty);
      }
    }

    return tt_value;
  }

  // 1手詰め判定
  // 処理が重いのでなるべくなら呼び出さないようにしたい
  if (!root_node && !skip_mate && !tt_hit && !in_check) {
    Move mate_move;
    if ((mate_move = search_mate1ply(pos)) != kMoveNone) {
      ss->static_eval = best_value = MateIn(ss->ply + 1);
      tte->Save(position_key, value_to_tt(best_value, ss->ply), pv_hit,
                kBoundExact, depth, mate_move, TT.generation());

      return best_value;
    }
  }

  // 現局面の静的評価
  ss->static_eval = pure_static_eval = evaluate(pos, ss);
  if (in_check) {
    ss->static_eval = eval = pure_static_eval = kValueNone;
    improving = false;
    goto moves_loop;
  } else if (tt_hit) {
    eval = ss->static_eval;

    if (tt_value != kValueNone) {
      if (tte->bound() & (tt_value > eval ? kBoundLower : kBoundUpper))
        eval = tt_value;
    }
  } else {
    if ((ss - 1)->current_move != kMoveNull) {
      int bonus = -(ss - 1)->stat_score / 512;
      ss->static_eval = eval = pure_static_eval + bonus;
    } else {
      eval = ss->static_eval;
    }
    if (tte != nullptr)
      tte->Save(position_key, kValueNone, pv_hit, kBoundNone, kDepthNone,
                kMoveNone, TT.generation());
  }

  // Razoring
  if (!root_node && depth < 2 * kOnePly && eval <= alpha - kRazorMargin) {
    return qsearch<NT, false>(pos, ss, alpha, beta, kDepthZero, true);
  }

  improving = ss->static_eval >= (ss - 2)->static_eval ||
              (ss - 2)->static_eval == kValueNone;

  // Futility pruning: child node
  if (!pv_node && depth < 7 * kOnePly &&
      eval - futility_margin(depth, improving) >= beta && eval < kValueKnownWin)
    return eval;

  // Null move search
  if (!pv_node && (ss - 1)->current_move != kMoveNull &&
      (ss - 1)->stat_score < 23200 && eval >= beta &&
      (pure_static_eval >= beta - 36 * depth / kOnePly + 225) &&
      !excluded_move) {
    ss->current_move = kMoveNull;
    ss->continuation_history = &this_thread->continuation_history_[kEmpty][0];

    assert(eval - beta >= 0);

    Depth re = ((823 + 67 * depth) / 256 +
                std::min(int(eval - beta) / Eval::kPawnValue, 3)) *
               kOnePly;

    pos.do_null_move(st);
    (ss + 1)->evaluated = false;
    null_value = depth - re < kOnePly
                     ? -qsearch<kNonPV, false>(pos, ss + 1, -beta, -beta + 1,
                                               kDepthZero, true)
                     : -search<kNonPV>(pos, ss + 1, -beta, -beta + 1,
                                       depth - re, !cut_node, true);
    pos.undo_null_move();

    if (null_value >= beta) {
      if (null_value >= kValueMateInMaxPly) null_value = beta;

      return null_value;
    }
  }

  // ProbCut
  if (!pv_node && depth >= 5 * kOnePly && abs(beta) < kValueMateInMaxPly) {
    Value rbeta = std::min(beta + 216 - 48 * improving, kValueInfinite);
    assert((ss - 1)->current_move != kMoveNone);
    assert((ss - 1)->current_move != kMoveNull);

    MovePicker mp(pos, tt_move, rbeta - ss->static_eval,
                  &this_thread->capture_history_);
    CheckInfo ci(pos);
    int prob_cut_count = 0;

    while ((move = mp.NextMove()) != kMoveNone && prob_cut_count < 3) {
      if (pos.legal(move, ci.pinned)) {
        ++prob_cut_count;

        ss->current_move = move;
        ss->continuation_history =
            &this_thread
                 ->continuation_history_[move_piece(move, us)][move_to(move)];
        pos.do_move(move, st);
        (ss + 1)->evaluated = false;
        value = pos.in_check()
                    ? -qsearch<kNonPV, true>(pos, ss + 1, -rbeta, -rbeta + 1,
                                             kDepthZero, false)
                    : -qsearch<kNonPV, false>(pos, ss + 1, -rbeta, -rbeta + 1,
                                              kDepthZero, false);

        if (value >= rbeta)
          value = -search<kNonPV>(pos, ss + 1, -rbeta, -rbeta + 1,
                                  depth - 4 * kOnePly, !cut_node, true);
        pos.undo_move(move);
        if (value >= rbeta) return value;
      }
    }
  }

  // Internal iterative deepening
  if (depth >= 6 * kOnePly && !tt_move &&
      (pv_node || ss->static_eval + 128 >= beta)) {
    Depth d = (3 * depth / (4 * kOnePly) - 2) * kOnePly;
    search<NT>(pos, ss, alpha, beta, d, cut_node, true);

    tte = TT.Probe(position_key, &tt_hit);
    tt_move = tt_hit ? tte->move() : kMoveNone;
    pv_hit = tt_hit && tte->pv_hit();
  }

moves_loop:
  const PieceToHistory *cont_hist[] = {(ss - 1)->continuation_history,
                                       (ss - 2)->continuation_history,
                                       nullptr,
                                       (ss - 4)->continuation_history,
                                       nullptr,
                                       (ss - 6)->continuation_history};
  Move countermove = this_thread->counter_moves_[prev_piece][prev_sq];
  CheckInfo ci(pos);
  MovePicker mp(pos, &ci, tt_move, depth, &this_thread->main_history_,
                &this_thread->capture_history_, cont_hist, countermove,
                ss->killers);
  value = best_value;
  singular_extension_node = !root_node && depth >= 8 * kOnePly &&
                            tt_move != kMoveNone && tt_value != kValueNone &&
                            !excluded_move && (tte->bound() & kBoundLower) &&
                            tte->depth() >= depth - 3 * kOnePly;

  Move current_best_move = kMoveNone;
  bool tt_capture = false;
  int move_value = 0;
  // Loop through moves
  while ((move = mp.NextMove(&move_value)) != kMoveNone) {
    assert(is_ok(move));

    if (move == excluded_move) continue;

    if (root_node &&
        !std::count(this_thread->root_moves_.begin() + this_thread->pv_index_,
                    this_thread->root_moves_.end(), move))
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
    if (pv_node) (ss + 1)->pv = nullptr;

    ext = kDepthZero;

    // Stockfishだとpromotionも入っているが将棋としてはpromotionなんて普通の手なので除外
    capture = move_is_capture(move);
    moved_piece = move_piece(move, pos.side_to_move());

    gives_check = pos.gives_check(move, ci);
    move_count_pruning =
        move_count >= FutilityMoveCount(improving, depth / kOnePly);

    // Singular extension search
    if (singular_extension_node && move == tt_move &&
        pos.legal(move, ci.pinned)) {
      Value singular_beta =
          std::max(tt_value - 8 * depth / kOnePly, -kValueMate);
      Depth d = (depth / (2 * kOnePly)) * kOnePly;
      ss->excluded_move = move;
      value = search<kNonPV>(pos, ss, singular_beta - 1, singular_beta, d,
                             cut_node, true);
      ss->excluded_move = kMoveNone;

      if (value < singular_beta) {
        ext = kOnePly;
      } else if (cut_node && singular_beta > beta) {
        return beta;
      }
    } else if (gives_check && ((move_capture(move) & 0xF) >= kSilver ||
                               pos.continuous_checks(pos.side_to_move()) > 2)) {
      ext = kOnePly;
    }

    new_depth = depth - kOnePly + ext;

    // Pruning at shallow depth
    if (!pv_node && best_value > kValueMatedInMaxPly) {
      if (!capture && !gives_check) {
        // Move count based pruning
        if (move_count_pruning) {
          if (is_ok(current_best_move) && is_ok((ss - 1)->current_move) &&
              move_to(current_best_move) == move_to((ss - 1)->current_move))
            break;

          continue;
        }

        int lmr_depth = std::max(new_depth - Reduction<pv_node>(
                                                 improving, depth, move_count),
                                 kDepthZero) /
                        kOnePly;

        // Countermove based pruning
        if (lmr_depth < 3 + ((ss - 1)->stat_score > 0) &&
            ((*cont_hist[0])[moved_piece][move_to(move)] <
             kCounterMovePruneThreshold) &&
            ((*cont_hist[1])[moved_piece][move_to(move)] <
             kCounterMovePruneThreshold))
          continue;

        // Futility pruning: parent node
        if (lmr_depth < 7 && !in_check &&
            ss->static_eval + 256 + 200 * lmr_depth <= alpha)
          continue;

        if (lmr_depth < 8 &&
            !pos.see_ge(move, Value(-35 * lmr_depth * lmr_depth)))
          continue;
      } else if (depth < 7 * kOnePly && !ext &&
                 ((move_capture(move) & 0xF) < kSilver)) {
        Value v = -Value(400 + 100 * depth / kOnePly);
        if (!pos.see_ge(move, v)) continue;
      }
    }

    prefetch(TT.FirstEntry(pos.key_after(move)));

    if (!root_node && !pos.legal(move, ci.pinned)) {
      ss->move_count = --move_count;
      continue;
    }

    if (move == tt_move && move_is_capture(tt_move)) tt_capture = true;

    ss->current_move = move;
    ss->continuation_history =
        &this_thread->continuation_history_[moved_piece][move_to(move)];

    // Make the move
    pos.do_move(move, st, gives_check);
    (ss + 1)->evaluated = false;

    // Reduced depth search (LMR)
    if (depth >= 3 * kOnePly && move_count > 1) {
      Depth r = Reduction<pv_node>(improving, depth, move_count);

      if (pv_hit) r -= kOnePly;

      if (gives_check) r -= kOnePly;

      if (capture) {
        r -= kOnePly;
      } else {
        if (tt_capture) r += kOnePly;

        // cut_nodeの場合はreductionを増やす
        if (cut_node)
          r += 2 * kOnePly;
        else if (!pos.see_ge_reverse_move(move, kValueZero))
          r -= kOnePly;

        ss->stat_score = this_thread->main_history_[us][move_to(move)] +
                         (*cont_hist[0])[moved_piece][move_to(move)] +
                         (*cont_hist[1])[moved_piece][move_to(move)] +
                         (*cont_hist[3])[moved_piece][move_to(move)] - 4000;

        if (ss->stat_score >= 0 && (ss - 1)->stat_score < 0)
          r -= kOnePly;
        else if ((ss - 1)->stat_score >= 0 && ss->stat_score < 0)
          r += kOnePly;

        r = std::max(kDepthZero,
                     (r / kOnePly - ss->stat_score / 20000) * kOnePly);
      }

      if ((pv_node && move_value < -6000) || (!pv_node && move_value < -3000))
        r += kOnePly;

      Depth d = std::max(new_depth - std::max(r, kDepthZero), kOnePly);

      value = -search<kNonPV>(pos, ss + 1, -(alpha + 1), -alpha, d, true,
                              d < 3 * kOnePly);

      do_full_depth_search = (value > alpha && d != kDepthZero);
    } else {
      do_full_depth_search = !pv_node || move_count > 1;
    }

    // Full depth search
    if (do_full_depth_search) {
      value =
          new_depth < kOnePly
              ? (gives_check
                     ? -qsearch<kNonPV, true>(pos, ss + 1, -(alpha + 1), -alpha,
                                              kDepthZero)
                     : -qsearch<kNonPV, false>(pos, ss + 1, -(alpha + 1),
                                               -alpha, kDepthZero))
              : -search<kNonPV>(pos, ss + 1, -(alpha + 1), -alpha, new_depth,
                                !cut_node, new_depth < 3 * kOnePly);
    }

    if (pv_node &&
        (move_count == 1 || (value > alpha && (root_node || value < beta)))) {
      (ss + 1)->pv = pv;
      (ss + 1)->pv[0] = kMoveNone;

      value = new_depth < kOnePly
                  ? (gives_check ? -qsearch<kPV, true>(pos, ss + 1, -beta,
                                                       -alpha, kDepthZero)
                                 : -qsearch<kPV, false>(pos, ss + 1, -beta,
                                                        -alpha, kDepthZero))
                  : -search<kPV>(pos, ss + 1, -beta, -alpha, new_depth, false,
                                 new_depth < 3 * kOnePly);
    }

    // Undo move
    pos.undo_move(move);

    assert(value > -kValueInfinite && value < kValueInfinite);

    // Check for new best move
    if (Signals.stop.load(std::memory_order_relaxed)) return kValueZero;

    if (root_node) {
      RootMove &rm = *std::find(this_thread->root_moves_.begin(),
                                this_thread->root_moves_.end(), move);

      if (move_count == 1 || value > alpha) {
        rm.score = value;
        rm.pv.resize(1);

        assert((ss + 1)->pv);

        for (Move *m = (ss + 1)->pv; *m != kMoveNone; ++m) rm.pv.push_back(*m);

        if (move_count > 1) this_thread->best_move_changes_++;
      } else {
        rm.score = -kValueInfinite;
      }
    }

    if (value > best_value) {
      current_best_move = move;
      best_value = value;

      if (value > alpha) {
        best_move = move;

        if (pv_node && !root_node) update_pv(ss->pv, move, (ss + 1)->pv);

        if (pv_node && value < beta) {
          alpha = value;
        } else {
          assert(value >= beta);
          ss->stat_score = 0;
          break;
        }
      }
    }

    if (!capture && move != best_move && quiet_count < 64)
      quiets_searched[quiet_count++] = move;
    else if (capture && move != best_move && capture_count < 32)
      capture_searched[capture_count++] = move;
  }

  if (move_count == 0) {
    best_value =
        excluded_move
            ? alpha
            : MatedIn(ss->ply - 1);  // すでに合法手がないので1手前で詰んでいる
  } else if (best_move != kMoveNone) {
    if (!move_is_capture(best_move))
      update_stats(pos, ss, best_move, quiets_searched, quiet_count,
                   stat_bonus(depth));

    update_capture_stats(pos, best_move, capture_searched, capture_count,
                         stat_bonus(depth));

    if ((ss - 1)->move_count == 1 && !move_is_capture((ss - 1)->current_move))
      update_cm_stats(ss - 1, prev_piece, prev_sq,
                      -stat_bonus(depth + kOnePly));
  } else if (depth >= 3 * kOnePly && !move_is_capture((ss - 1)->current_move) &&
             is_ok((ss - 1)->current_move)) {
    update_cm_stats(ss - 1, prev_piece, prev_sq, stat_bonus(depth));
  }

  if (excluded_move == kMoveNone && best_value != DrawValue[pos.side_to_move()])
    tte->Save(position_key, value_to_tt(best_value, ss->ply), pv_hit,
              best_value >= beta
                  ? kBoundLower
                  : (pv_node && best_move ? kBoundExact : kBoundUpper),
              depth, best_move, TT.generation());

  assert(best_value > -kValueInfinite && best_value < kValueInfinite);

  return best_value;
}

// 静止探索
template <NodeType NT, bool InCheck>
Value qsearch(Position &pos, SearchStack *ss, Value alpha, Value beta,
              Depth depth, bool skip_mate) {
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
  bool pv_hit;
  int move_count = 0;

  if (PvNode) {
    old_alpha = alpha;

    (ss + 1)->pv = pv;
    ss->pv[0] = kMoveNone;
  }

  Thread *this_thread = pos.this_thread();
  ss->current_move = best_move = kMoveNone;
  ss->continuation_history = &this_thread->continuation_history_[kPieceNone][0];
  ss->ply = (ss - 1)->ply + 1;

  if (pos.is_decralation_win()) return MateIn(ss->ply - 1);

#if REPETITION_CHECK_FULL
  Repetition repetition = ((ss - 1)->current_move != kMoveNull)
                              ? pos.in_repetition()
                              : kNoRepetition;
  if (repetition == kRepetition || ss->ply >= kMaxPly)
    return ss->ply >= kMaxPly && !InCheck ? evaluate(pos, ss)
                                          : DrawValue[pos.side_to_move()];

  assert(0 <= ss->ply && ss->ply < kMaxPly);

  if (repetition == kPerpetualCheckWin) {
    return mate_in(ss->ply);
  } else if (repetition == kPerpetualCheckLose) {
    return mated_in(ss->ply);
  } else if (repetition == kBlackWinRepetition) {
    if (pos.side_to_move() == kWhite)
      return -kValueSamePosition;
    else
      return kValueSamePosition;
  } else if (repetition == kBlackLoseRepetition) {
    if (pos.side_to_move() == kBlack)
      return -kValueSamePosition;
    else
      return kValueSamePosition;
  }
#else
  if (ss->ply >= kMaxPly) return evaluate(pos, ss);
#endif
  tt_depth =
      InCheck || depth >= kDepthQsChecks ? kDepthQsChecks : kDepthQsNoChecks;

  // Transposition table lookup
  position_key = pos.key();
  tte = TT.Probe(position_key, &tt_hit);
  tt_move = tt_hit ? tte->move() : kMoveNone;
  tt_value = tt_hit ? value_from_tt(tte->value(), ss->ply) : kValueNone;
  pv_hit = tt_hit && tte->pv_hit();

  if (!PvNode && tt_hit && tte->depth() >= tt_depth && tt_value != kValueNone &&
      (tt_value >= beta ? (tte->bound() & kBoundLower)
                        : (tte->bound() & kBoundUpper))) {
    return tt_value;
  }

  ss->static_eval = evaluate(pos, ss);

  // 現局面の静的評価
  if (InCheck) {
    ss->static_eval = kValueNone;
    best_value = futility_base = -kValueInfinite;
  } else {
    if (!skip_mate && !tt_hit) {
      Move mate_move = search_mate1ply(pos);
      if (mate_move != kMoveNone) {
        tte->Save(position_key, value_to_tt(MateIn(ss->ply + 1), ss->ply),
                  pv_hit, kBoundExact, tt_depth, mate_move, TT.generation());
        return MateIn(ss->ply + 1);
      }
    }

    if (tt_hit) {
      best_value = ss->static_eval;

      if (tt_value != kValueNone) {
        if (tte->bound() & (tt_value > best_value ? kBoundLower : kBoundUpper))
          best_value = tt_value;
      }
    } else {
      best_value = ss->static_eval;
    }

    if (best_value >= beta) {
      if (!tt_hit) {
        tte->Save(pos.key(), value_to_tt(best_value, ss->ply), pv_hit,
                  kBoundLower, kDepthNone, kMoveNone, TT.generation());
      }

      return best_value;
    }

    if (PvNode && best_value > alpha) alpha = best_value;

    futility_base = best_value + 128;
  }

  const PieceToHistory *cont_hist[] = {(ss - 1)->continuation_history,
                                       (ss - 2)->continuation_history,
                                       nullptr,
                                       (ss - 4)->continuation_history,
                                       nullptr,
                                       (ss - 6)->continuation_history};

  CheckInfo ci(pos);
  MovePicker mp(pos, tt_move, depth, &this_thread->main_history_,
                &this_thread->capture_history_, cont_hist,
                move_to((ss - 1)->current_move));

  while ((move = mp.NextMove()) != kMoveNone) {
    assert(is_ok(move));

    gives_check = pos.gives_check(move, ci);

    ++move_count;

    // Futility pruning
    if (!InCheck && !gives_check && futility_base > -kValueKnownWin) {
      futility_value =
          futility_base + Eval::ExchangePieceValueTable[move_capture(move)];
      if (move_is_promote(move))
        futility_value += Eval::PromotePieceValueTable[move_piece_type(move)];

      if (futility_value <= alpha) {
        best_value = std::max(best_value, futility_value);
        continue;
      }

      if (futility_base <= alpha && !pos.see_ge(move, kValueZero + 1)) {
        best_value = std::max(best_value, futility_base);
        continue;
      }
    }

    evasion_prunable = InCheck && (depth != kDepthZero || move_count > 2) &&
                       best_value > kValueMatedInMaxPly &&
                       !move_is_capture(move);

    if ((!InCheck || evasion_prunable) && !pos.see_ge(move, kValueZero)) {
      continue;
    }

    prefetch(TT.FirstEntry(pos.key_after(move)));

    if (!pos.legal(move, ci.pinned)) {
      --move_count;
      continue;
    }

    ss->current_move = move;
    ss->continuation_history = &this_thread->continuation_history_[move_piece(
        move, pos.side_to_move())][move_to(move)];

    pos.do_move(move, st, gives_check);
    (ss + 1)->evaluated = false;

    value =
        gives_check
            ? -qsearch<NT, true>(pos, ss + 1, -beta, -alpha, depth - kOnePly)
            : -qsearch<NT, false>(pos, ss + 1, -beta, -alpha, depth - kOnePly);
    pos.undo_move(move);

    assert(value > -kValueInfinite && value < kValueInfinite);

    if (value > best_value) {
      best_value = value;

      if (value > alpha) {
        if (PvNode) update_pv(ss->pv, move, (ss + 1)->pv);

        if (PvNode && value < beta) {
          alpha = value;
          best_move = move;
        } else {
          tte->Save(position_key, value_to_tt(value, ss->ply), pv_hit,
                    kBoundLower, tt_depth, move, TT.generation());

          return value;
        }
      }
    }
  }

  if (InCheck && best_value == -kValueInfinite) return MatedIn(ss->ply - 1);

  tte->Save(position_key, value_to_tt(best_value, ss->ply), pv_hit,
            (PvNode && best_value > old_alpha ? kBoundExact : kBoundUpper),
            tt_depth, best_move, TT.generation());

  assert(best_value > -kValueInfinite && best_value < kValueInfinite);

  return best_value;
}

Value value_to_tt(Value v, int ply) {
  assert(v != kValueNone);

  return v >= kValueMateInMaxPly ? v + ply
                                 : (v <= kValueMatedInMaxPly ? v - ply : v);
}

Value value_from_tt(Value v, int ply) {
  return v == kValueNone ? kValueNone
                         : (v >= kValueMateInMaxPly
                                ? v - ply
                                : (v <= kValueMatedInMaxPly ? v + ply : v));
}

void update_pv(Move *pv, Move move, Move *child_pv) {
  for (*pv++ = move; child_pv && *child_pv != kMoveNone;) *pv++ = *child_pv++;
  *pv = kMoveNone;
}

void update_cm_stats(SearchStack *ss, Piece pc, Square to, int bonus) {
  for (int i : {1, 2, 4, 6})
    if (is_ok((ss - i)->current_move))
      (*(ss - i)->continuation_history)[pc][to] << bonus;
}

void update_stats(const Position &pos, SearchStack *ss, Move move, Move *quiets,
                  int quiets_count, int bonus) {
  if (ss->killers[0] != move) {
    ss->killers[1] = ss->killers[0];
    ss->killers[0] = move;
  }

  Color us = pos.side_to_move();
  Piece moved_piece = move_piece(move, pos.side_to_move());

  Thread *this_thread = pos.this_thread();

  this_thread->main_history_[us][FromTo(move)] << bonus;
  update_cm_stats(ss, moved_piece, move_to(move), bonus);
  if (is_ok((ss - 1)->current_move)) {
    Square prev_sq = move_to((ss - 1)->current_move);
    Piece prev_piece = move_piece((ss - 1)->current_move, ~us);
    this_thread->counter_moves_[prev_piece][prev_sq] = move;
  }

  for (int i = 0; i < quiets_count; ++i) {
    Move m = quiets[i];
    this_thread->main_history_[us][FromTo(quiets[i])] << -bonus;
    update_cm_stats(ss, move_piece(m, pos.side_to_move()), move_to(m), -bonus);
  }
}

void update_capture_stats(const Position &pos, Move move, Move *captures,
                          int capture_count, int bonus) {
  CapturePieceToHistory &capture_history = pos.this_thread()->capture_history_;
  Piece moved_piece = move_piece(move, pos.side_to_move());
  PieceType captured = move_capture(move);

  if (captured != kPieceNone)
    capture_history[moved_piece][move_to(move)][captured] << bonus;

  for (int i = 0; i < capture_count; ++i) {
    moved_piece = move_piece(captures[i], pos.side_to_move());
    captured = move_capture(captures[i]);
    capture_history[moved_piece][move_to(move)][captured] << bonus;
  }
}

void check_time() {
  static TimePoint last_info_time = now();

  int elapsed = Time.elapsed();
  TimePoint tick = Limits.start_time + elapsed;

  if (tick - last_info_time >= 1000) {
    last_info_time = tick;
  }

  if (Limits.ponder) return;

  if ((Limits.use_time_management() && elapsed > Time.maximum() - 10) ||
      (Limits.movetime && elapsed >= Limits.movetime) ||
      (Limits.nodes && Threads.nodes_searched() >= Limits.nodes))
    Signals.stop = true;
}

}  // namespace

string usi_pv(const Position &pos, Depth depth, Value alpha, Value beta) {
  std::stringstream ss;
  int elapsed = Time.elapsed() + 1;
  const Search::RootMoveVector &root_moves = pos.this_thread()->root_moves_;
  size_t pv_index = pos.this_thread()->pv_index_;
  size_t multi_pv = std::min((size_t)Options["MultiPV"], root_moves.size());
  uint64_t nodes_searched = Threads.nodes_searched();

  for (size_t i = 0; i < multi_pv; ++i) {
    bool updated = (i <= pv_index);

    if (depth == kOnePly && !updated) continue;

    Depth d = updated ? depth : depth - kOnePly;
    Value v = updated ? root_moves[i].score : root_moves[i].previous_score;

    if (ss.rdbuf()->in_avail()) ss << "\n";

    ss << "info"
       << " depth " << d / kOnePly << " seldepth "
       << pos.this_thread()->max_ply_ << " multipv " << i + 1 << " score "
       << USI::format_value(v);

    if (i == pv_index)
      ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

    ss << " nodes " << nodes_searched << " nps "
       << nodes_searched * 1000 / elapsed;

    if (elapsed > 1000)  // Earlier makes little sense
      ss << " hashfull " << TT.Hashfull();

    ss << " time " << elapsed << " pv";

    for (size_t j = 0; j < root_moves[i].pv.size(); ++j)
      ss << " " << USI::format_move(root_moves[i].pv[j]);
  }

  return ss.str();
}

bool RootMove::extract_ponder_from_tt(Position &pos) {
  StateInfo st;
  bool found;
  bool result = false;

  assert(pv.size() == 1);

  pos.do_move(pv[0], st);
  TTEntry *tte = TT.Probe(pos.key(), &found);
  if (found) {
    Move m = tte->move();
    if (MoveList<kLegal>(pos).contains(m)) {
      pv.push_back(m);
      result = true;
    }
  }
  pos.undo_move(pv[0]);

  return result;
}
