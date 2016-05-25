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
  kMainSearch, kCapturesS1, kKillersS1, kQuiets1S1, kQuiets2S1, kBadCapturesS1,
  kEvasion, kEvasionsS2,
  kQSearch0, kCapturesS3, kQuietChecksS3,
  kQSearch1, kCapturesS4,
  kProbCut, kCapturesS5,
  kRecapture, kCapturesS6,
  kStop
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
  const Position                &p,
  Move                           ttm,
  Depth                          d,
  const HistoryStats            &h,
  const CounterMoveStats        &cmh,
  const CounterMoveStats        &fmh,
  Move                           cm,
  SearchStack                   *s
)
:
pos_(p), history_(h), counter_move_history_(&cmh), followup_move_history_(&fmh), ss_(s), countermove_(cm), depth_(d)
{
  assert(d > kDepthZero);

  if (p.in_check())
    stage_ = kEvasion;
  else
    stage_ = kMainSearch;

  tt_move_ = (ttm && pos_.pseudo_legal(ttm) ? ttm : kMoveNone);
  end_moves_ += (tt_move_ != kMoveNone);
}

MovePicker::MovePicker
(
  const Position                &p,
  Move                           ttm,
  Depth                          d,
  const HistoryStats            &h,
  Square                         sq
)
:
pos_(p), history_(h)
{
  assert(d <= kDepthZero);

  if (p.in_check())
  {
    stage_ = kEvasion;
  }
  else if (d > kDepthQsNoChecks)
  {
    stage_ = kQSearch0;
  }
  else if (d > kDepthQsRecaptues)
  {
    stage_ = kQSearch1;
  }
  else
  {
    stage_ = kRecapture;
    recapture_square_ = sq;
    ttm = kMoveNone;
  }

  tt_move_ = (ttm && pos_.pseudo_legal(ttm) ? ttm : kMoveNone);
  end_moves_ += (tt_move_ != kMoveNone);
}

MovePicker::MovePicker
(
  const Position                &p,
  Move                           ttm,
  const HistoryStats            &h,
  Value                          th
)
:
pos_(p), history_(h), capture_threshold_(th)
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
      pos_.see(ttm) > capture_threshold_
    )
    ?
    ttm
    :
    kMoveNone;

  end_moves_ += (tt_move_ != kMoveNone);
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
  for (auto &m : *this)
  {
    m.value =
      history_[move_piece(m, pos_.side_to_move())][move_to(m)]
      +
      (*counter_move_history_)[move_piece(m, pos_.side_to_move())][move_to(m)]
      +
      (*followup_move_history_)[move_piece(m, pos_.side_to_move())][move_to(m)];
  }
}

template<>
void
MovePicker::score<kEvasions>()
{
  Value see_score;

  for (auto &m : *this)
  {
    if ((see_score = pos_.see_sign(m)) < kValueZero)
    {
      m.value = see_score - HistoryStats::kMax;
    }
    else if (move_capture(m) != 0)
    {
      PieceType pt = move_piece_type(m);
      if (move_is_promote(m))
        pt = pt + kFlagPromoted;
      m.value =
        static_cast<Value>(Eval::ExchangePieceValueTable[move_capture(m)] - Eval::ExchangePieceValueTable[pt] + HistoryStats::kMax);
    }
    else
    {
      m.value = history_[move_piece(m, pos_.side_to_move())][move_to(m)];
    }
  }
}

void
MovePicker::generate_next_stage()
{
  cur_ = moves_;

  switch (++stage_)
  {
  case kCapturesS1:
  case kCapturesS3:
  case kCapturesS4:
  case kCapturesS5:
  case kCapturesS6:
    end_moves_ = generate<kCaptures>(pos_, moves_);
    score<kCaptures>();
    break;

  case kKillersS1:
    killers_[0] = ss_->killers[0];
    killers_[1] = ss_->killers[1];
    killers_[2] = countermove_;
    cur_        = killers_;
    end_moves_  = cur_ + 2 + (countermove_ != killers_[0] && countermove_ != killers_[1]);
    break;

  case kQuiets1S1:
    end_quiets_ = end_moves_ = generate<kQuiets>(pos_, moves_);
    score<kQuiets>();
    end_moves_ = std::partition(cur_, end_moves_, [](const ExtMove &m) { return m.value > kValueZero; });
    insertion_sort(cur_, end_moves_);
    break;

  case kQuiets2S1:
    cur_       = end_moves_;
    end_moves_ = end_quiets_;
    if (depth_ >= 3 * kOnePly)
      insertion_sort(cur_, end_moves_);
    break;

  case kBadCapturesS1:
    cur_       = moves_ + kMaxMoves - 1;
    end_moves_ = end_bad_captures_;
    break;

  case kEvasionsS2:
    end_moves_ = generate<kEvasions>(pos_, moves_);
    if (end_moves_ - moves_ > 1)
      score<kEvasions>();
    break;

  case kQuietChecksS3:
    end_moves_ = generate<kQuietChecks>(pos_, moves_);
    break;

  case kEvasion:
  case kQSearch0:
  case kQSearch1:
  case kProbCut:
  case kRecapture:
  case kStop:
    stage_ = kStop;
    break;

  default:
    assert(false);
  }
}

Move
MovePicker::next_move()
{
  Move move;

  while (true)
  {
    while (cur_ == end_moves_ && stage_ != kStop)
      generate_next_stage();

    switch (stage_)
    {
    case kMainSearch:
    case kEvasion:
    case kQSearch0:
    case kQSearch1:
    case kProbCut:
      ++cur_;
      return tt_move_;

    case kCapturesS1:
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_)
      {
        if (pos_.see_sign(move) >= kValueZero)
          return move;

        *end_bad_captures_-- = move;
      }
      break;

    case kKillersS1:
      move = *cur_++;
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
      break;

    case kQuiets1S1:
    case kQuiets2S1:
      move = *cur_++;
      if
      (
        move != tt_move_
        &&
        move != killers_[0]
        &&
        move != killers_[1]
        &&
        move != killers_[2]
      )
        return move;
      break;

    case kBadCapturesS1:
      return *cur_--;

    case kEvasionsS2:
    case kCapturesS3:
    case kCapturesS4:
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_)
        return move;
      break;

    case kCapturesS5:
      move = pick_best(cur_++, end_moves_);
      if (move != tt_move_ && pos_.see(move) > capture_threshold_)
        return move;
      break;

    case kCapturesS6:
      move = pick_best(cur_++, end_moves_);
      if (move_to(move) == recapture_square_)
        return move;
      break;

    case kQuietChecksS3:
      move = *cur_++;
      if (move != tt_move_)
        return move;
      break;

    case kStop:
      return kMoveNone;

    default:
      assert(false);
    }
  }
}
