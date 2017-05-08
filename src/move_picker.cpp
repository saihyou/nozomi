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

constexpr int
RawPieceValueTable[kPieceTypeMax] =
{
  0,
  Eval::PieceValueTable[kPawn],
  Eval::PieceValueTable[kLance],
  Eval::PieceValueTable[kKnight],
  Eval::PieceValueTable[kSilver],
  Eval::PieceValueTable[kBishop],
  Eval::PieceValueTable[kRook],
  Eval::PieceValueTable[kGold],
  0,
  Eval::PieceValueTable[kPawn],
  Eval::PieceValueTable[kLance],
  Eval::PieceValueTable[kKnight],
  Eval::PieceValueTable[kSilver],
  Eval::PieceValueTable[kBishop],
  Eval::PieceValueTable[kRook]
};

void
partial_insertion_sort(ExtMove *begin, ExtMove *end, int limit)
{
  for (ExtMove *sorted_end = begin + 1, *p = begin + 1; p < end; ++p)
  {
    if (p->value >= limit)
    {
      ExtMove tmp = *p;
      ExtMove *q;
      for (q = sorted_end; q != begin && *(q - 1) < tmp; --q)
        *q = *(q - 1);
      *q = tmp;
      ++sorted_end;
    }
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
  killers_[0] = ss_->killers[0];
  killers_[1] = ss_->killers[1];

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
      pos_.see_ge(ttm, threshold_)
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
  for (auto &m : *this)
  {
    m.value = RawPieceValueTable[move_capture(m)];
  }
}

template<>
void
MovePicker::score<kQuiets>()
{
  const HistoryStats &history = pos_.this_thread()->history_;
  const FromToStats &from_to = pos_.this_thread()->from_to_;
  
  const CounterMoveStats &cmh = *(ss_ - 1)->counter_moves;
  const CounterMoveStats &fmh = *(ss_ - 2)->counter_moves;
  const CounterMoveStats &fm2 = *(ss_ - 4)->counter_moves;
  for (auto &m : *this)
  {
    m.value =
      history[move_piece(m, pos_.side_to_move())][move_to(m)]
      +
      cmh[move_piece(m, pos_.side_to_move())][move_to(m)]
      +
      fmh[move_piece(m, pos_.side_to_move())][move_to(m)]
      +
      fm2[move_piece(m, pos_.side_to_move())][move_to(m)]
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

  for (auto &m : *this)
  {
    if (move_is_capture(m))
    {
      m.value = RawPieceValueTable[move_capture(m)] + HistoryStats::kMax;
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
        if (pos_.see_ge(move, kValueZero))
          return move;

        *end_bad_captures_++ = move;
      }
    }

    ++stage_;
    move = killers_[0];
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
    move = killers_[1];
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
      move != killers_[0]
      &&
      move != killers_[1]
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
    partial_insertion_sort(cur_, end_moves_, -8000 * depth_ / kOnePly);
    ++stage_;

  case kQuiet:
    while (cur_ < end_moves_)
    {
      move = *cur_++;
      if
      (
        move != tt_move_
        &&
        move != killers_[0]
        &&
        move != killers_[1]
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
      if (move != tt_move_ && pos_.see_ge(move, threshold_))
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
