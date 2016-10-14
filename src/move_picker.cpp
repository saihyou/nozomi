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

#include <cassert>
#include "move_picker.h"
#include "thread.h"

namespace
{
enum Stages
{
  kMainSearch, kCapturesInit, kGoodCaptures, kKillers, kCounterMove, kQuietInit, kQuiet, kBadCaptures,
  kEvasion, kEvasionsInit, kAllEvasions,
  kProbCut, kProbCutInit, kProbCutCaptures,
  kQSearchWithChecks, kQCaptures1Init, kQCaptures1, kQChecks,
  kQSearchNoChecks, kQCaptures2Init, kQCaptures2,
  kQSearchRecaptures, kRecaptures
};

void
insertion_sort(ExtMove *begin, ExtMove *end)
{
  ExtMove tmp;
  ExtMove *p;
  ExtMove *q;

  for (p = begin + 1; p < end; ++p)
  {
    tmp = *p;
    for (q = p; q != begin && *(q - 1) < tmp; --q)
      *q = *(q - 1);
    *q = tmp;
  }
}

inline Move
pick_best(ExtMove *begin, ExtMove *end)
{
  std::swap(*begin, *std::max_element(begin, end));
  return *begin;
}
} // namespace

MovePicker::MovePicker
(
  const Position &p,
  Move            ttm,
  Depth           d,
  SearchStack    *s
)
:
pos_(p), ss_(s), depth_(d)
{
  assert(d > kDepthZero);

  Square prev_sq = move_to((ss_ - 1)->current_move);
  Piece prev_piece = move_piece((ss_ - 1)->current_move, ~pos_.side_to_move());
  countermove_ = pos_.this_thread()->counter_moves_[prev_piece][prev_sq];

  stage_ = pos_.in_check() ? kEvasion : kMainSearch;
  tt_move_ = (ttm && pos_.pseudo_legal(ttm) ? ttm : kMoveNone);
  stage_ += (tt_move_ == kMoveNone);
}

MovePicker::MovePicker
(
  const Position &p,
  Move            ttm,
  Depth           d,
  Square          s
)
:
pos_(p)
{
  assert(d <= kDepthZero);

  if (pos_.in_check())
  {
    stage_ = kEvasion;
  }
  else if (d > kDepthQsNoChecks)
  {
    stage_ = kQSearchWithChecks;
  }
  else if (d > kDepthQsRecaptues)
  {
    stage_ = kQSearchNoChecks;
  }
  else
  {
    stage_ = kQSearchRecaptures;
    recapture_square_ = s;
    return;
  }

  tt_move_ = (ttm && pos_.pseudo_legal(ttm) ? ttm : kMoveNone);
  stage_ += (tt_move_ == kMoveNone);
}

MovePicker::MovePicker
(
  const Position &p,
  Move            ttm,
  Value           th
)
:
pos_(p), threshold_(th)
{
  assert(!pos_.in_check());

  stage_ = kProbCut;

  tt_move_ =
    (
      ttm
      &&
      pos_.pseudo_legal(ttm)
      &&
      move_is_capture(ttm)
      &&
      pos_.see(ttm) > threshold_
    )
    ?
    ttm
    :
    kMoveNone;

  stage_ += (tt_move_ == kMoveNone);
}

template<>
void
MovePicker::score<kCaptures>()
{

  // Stockfishは自陣から離れているとペナルティを入れているが
  // 将棋としては意味ないのでは
  for (auto &m : *this)
  {
    m.value = static_cast<Value>(Eval::PieceValueTable[move_capture(m)]);
    if (move_is_promote(m))
      m.value += static_cast<Value>(Eval::PromotePieceValueTable[move_piece_type(m)]);
  }
}

template<>
void
MovePicker::score<kQuiets>()
{
  const HistoryStats &history = pos_.this_thread()->history_;
  const FromToStats &from_to = pos_.this_thread()->from_to_;
  
  const CounterMoveStats *cm = (ss_ - 1)->counter_moves;
  const CounterMoveStats *fm = (ss_ - 2)->counter_moves;
  for (auto &m : *this)
  {
    m.value =
      history[move_piece(m, pos_.side_to_move())][move_to(m)]
      +
      (cm ? (*cm)[move_piece(m, pos_.side_to_move())][move_to(m)] : kValueZero)
      +
      (fm ? (*fm)[move_piece(m, pos_.side_to_move())][move_to(m)] : kValueZero)
      +
      from_to.get(pos_.side_to_move(), m);
  }
}

template<>
void
MovePicker::score<kEvasions>()
{
  const HistoryStats &history = pos_.this_thread()->history_;
  const FromToStats &from_to = pos_.this_thread()->from_to_;
  Color c = pos_.side_to_move();
  Value see;

  for (auto &m : *this)
  {
    see = pos_.see_sign(m);
    if (see < kValueZero)
    {
      m.value = see - HistoryStats::kMax;
    }
    else if (move_is_capture(m))
    {
      PieceType pt = move_piece_type(m);
      if (move_is_promote(m))
        pt = pt + kFlagPromoted;
      m.value =
        static_cast<Value>(Eval::ExchangePieceValueTable[move_capture(m)] - Eval::ExchangePieceValueTable[pt] + HistoryStats::kMax);
    }
    else
    {
      m.value = history[move_piece(m, pos_.side_to_move())][move_to(m)] + from_to.get(c, m);
    }
  }
}

Move
MovePicker::next_move()
{
  Move move;

  switch (stage_)
  {
  case kMainSearch:
  case kEvasion:
  case kQSearchWithChecks:
  case kQSearchNoChecks:
  case kProbCut:
    ++stage_;
    return tt_move_;

  case kCapturesInit:
    end_bad_captures_ = cur_ = moves_;
    end_moves_ = generate<kCaptures>(pos_, cur_);
    score<kCaptures>();
    ++stage_;

  case kGoodCaptures:
    while (cur_ < end_moves_)
    {
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_)
      {
        if (pos_.see_sign(move) >= kValueZero)
          return move;

        *end_bad_captures_++ = move;
      }
    }

    ++stage_;
    move = ss_->killers[0];
    if
    (
      move != kMoveNone
      &&
      move != tt_move_
      &&
      pos_.pseudo_legal(move)
      &&
      !move_is_capture(move)
    )
      return move;

  case kKillers:
    ++stage_;
    move = ss_->killers[1];
    if
    (
      move != kMoveNone
      &&
      move != tt_move_
      &&
      pos_.pseudo_legal(move)
      &&
      !move_is_capture(move)
    )
      return move;

  case kCounterMove:
    ++stage_;
    move = countermove_;
    if
    (
      move != kMoveNone
      &&
      move != tt_move_
      &&
      move != ss_->killers[0]
      &&
      move != ss_->killers[1]
      &&
      pos_.pseudo_legal(move)
      &&
      !move_is_capture(move)
    )
      return move;

  case kQuietInit:
    cur_ = end_bad_captures_;
    end_moves_ = generate<kQuiets>(pos_, cur_);
    score<kQuiets>();
    if (depth_ < 3 * kOnePly)
    {
      ExtMove *good_quiet =
        std::partition(cur_, end_moves_, [](const ExtMove &m){ return m.value > kValueZero; });
      insertion_sort(cur_, good_quiet);
    }
    else
    {
      insertion_sort(cur_, end_moves_);
    }
    ++stage_;

  case kQuiet:
    while (cur_ < end_moves_)
    {
      move = *cur_++;
      if
      (
        move != tt_move_
        &&
        move != ss_->killers[0]
        &&
        move != ss_->killers[1]
        &&
        move != countermove_
      )
        return move;
    }
    ++stage_;
    cur_ = moves_;

  case kBadCaptures:
    if (cur_ < end_bad_captures_)
      return *cur_++;
    break;

  case kEvasionsInit:
    cur_ = moves_;
    end_moves_ = generate<kEvasions>(pos_, cur_);
    if (end_moves_ - cur_ - (tt_move_ != kMoveNone) > 1)
      score<kEvasions>();
    ++stage_;

  case kAllEvasions:
    while (cur_ < end_moves_)
    {
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_)
        return move;
    }
    break;

  case kProbCutInit:
    cur_ = moves_;
    end_moves_ = generate<kCaptures>(pos_, cur_);
    score<kCaptures>();
    ++stage_;

  case kProbCutCaptures:
    while (cur_ < end_moves_)
    {
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_ && pos_.see(move) > threshold_)
        return move;
    }
    break;

  case kQCaptures1Init:
  case kQCaptures2Init:
    cur_ = moves_;
    end_moves_ = generate<kCaptures>(pos_, cur_);
    score<kCaptures>();
    ++stage_;

  case kQCaptures1:
  case kQCaptures2:
    while (cur_ < end_moves_)
    {
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_)
        return move;
    }
    if (stage_ == kQCaptures2)
      break;
    cur_ = moves_;
    end_moves_ = generate<kQuietChecks>(pos_, cur_);
    ++stage_;

  case kQChecks:
    while (cur_ < end_moves_)
    {
      move = cur_++->move;
      if (move != tt_move_)
        return move;
    }
    break;

  case kQSearchRecaptures:
    cur_ = moves_;
    end_moves_ = generate<kCaptures>(pos_, cur_);
    score<kCaptures>();
    ++stage_;
    
  case kRecaptures:
    while (cur_ < end_moves_)
    {
      move = pick_best(cur_++, end_moves_);
      if (move_to(move) == recapture_square_)
        return move;
    }
    break;
    
  default:
    assert(false);
  }

  return kMoveNone;
}
