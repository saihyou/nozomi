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

#include "move_picker.h"
#include <cassert>
#include "move_probability.h"
#include "stats.h"

namespace {

enum Stages {
  kMainTt,
  kCaptureInit,
  kGoodCapture,
  kRefutation,
  kQuietInit,
  kQuiet,
  kBadCapture,
  kEvasionTt,
  kEvasionInit,
  kEvasion,
  kProbcutTt,
  kProbcutInit,
  kProbCut,
  kQsearchTt,
  kQcaptureInit,
  kQcapture,
  kQcheckInit,
  kQcheck,
  kProbSearchTt,
  kProbSearchInit,
  kProbSearch
};

constexpr int kRawPieceValueTable[kPieceTypeMax] = {
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
    Eval::PieceValueTable[kRook]};

// Helper filter used with select()
const auto Any = []() { return true; };

// partial_insertion_sort() sorts moves in descending order up to and including
// a given limit. The order of moves smaller than the limit is left unspecified.
void PartialInsertionSort(ExtMove *begin, ExtMove *end, int limit) {
  for (ExtMove *sorted_end = begin, *p = begin + 1; p < end; ++p)
    if (p->value >= limit) {
      ExtMove tmp = *p, *q;
      *p = *++sorted_end;
      for (q = sorted_end; q != begin && *(q - 1) < tmp; --q) *q = *(q - 1);
      *q = tmp;
    }
}

}  // namespace

/// Constructors of the MovePicker class. As arguments we pass information
/// to help it to return the (presumably) good moves first, to decide which
/// moves to return (in the quiescence search, for instance, we only want to
/// search captures, promotions, and some checks) and how important good move
/// ordering is at the current node.

/// MovePicker constructor for the main search
MovePicker::MovePicker(const Position &p, const CheckInfo *ci, Move ttm,
                       Depth d, const ButterflyHistory *mh,
                       const CapturePieceToHistory *cph,
                       const PieceToHistory **ch, Move cm, Move *killers)
    : pos_(p),
      ci_(ci),
      main_history_(mh),
      capture_history_(cph),
      continuation_history_(ch),
      refutations_{{killers[0], 0}, {killers[1], 0}, {cm, 0}},
      depth_(d) {
  assert(d > kDepthZero);

  if (pos_.in_check()) {
    stage_ = kEvasionTt;
  } else {
    stage_ = (depth_ >= 16 * kOnePly) ? kProbSearchTt : kMainTt;
  }
  tt_move_ = ttm && pos_.pseudo_legal(ttm) ? ttm : kMoveNone;
  stage_ += (tt_move_ == kMoveNone);
}

/// MovePicker constructor for quiescence search
MovePicker::MovePicker(const Position &p, Move ttm, Depth d,
                       const ButterflyHistory *mh,
                       const CapturePieceToHistory *cph,
                       const PieceToHistory **ch, Square rs)
    : pos_(p),
      main_history_(mh),
      capture_history_(cph),
      continuation_history_(ch),
      recapture_square_(rs),
      depth_(d) {
  assert(d <= kDepthZero);

  stage_ = pos_.in_check() ? kEvasionTt : kQsearchTt;
  tt_move_ =
      ttm && pos_.pseudo_legal(ttm) &&
              (depth_ > kDepthQsRecaptues || move_to(ttm) == recapture_square_)
          ? ttm
          : kMoveNone;
  stage_ += (tt_move_ == kMoveNone);
}

/// MovePicker constructor for ProbCut: we generate captures with SEE greater
/// than or equal to the given threshold.
MovePicker::MovePicker(const Position &p, Move ttm, Value th,
                       const CapturePieceToHistory *cph)
    : pos_(p), capture_history_(cph), threshold_(th) {
  assert(!pos_.in_check());

  stage_ = kProbcutTt;
  tt_move_ = ttm && pos_.pseudo_legal(ttm) && move_capture(ttm) &&
                     pos_.see_ge(ttm, threshold_)
                 ? ttm
                 : kMoveNone;
  stage_ += (tt_move_ == kMoveNone);
}

/// MovePicker::score() assigns a numerical value to each move in a list, used
/// for sorting. Captures are ordered by Most Valuable Victim (MVV), preferring
/// captures with a good history. Quiets moves are ordered using the histories.
template <GenType Type>
void MovePicker::Score() {
  static_assert(Type == kCaptures || Type == kQuiets || Type == kEvasions,
                "Wrong type");
  Color us = pos_.side_to_move();
  for (auto &m : *this) {
    if (Type == kCaptures) {
      m.value =
          kRawPieceValueTable[move_capture(m)] +
          (*capture_history_)[move_piece(m, us)][move_to(m)][move_capture(m)] /
              8;
    } else if (Type == kQuiets) {
      m.value = (*main_history_)[us][FromTo(m)] +
                (*continuation_history_[0])[move_piece(m, us)][move_to(m)] +
                (*continuation_history_[1])[move_piece(m, us)][move_to(m)] +
                (*continuation_history_[3])[move_piece(m, us)][move_to(m)] +
        (*continuation_history_[5])[move_piece(m, us)][move_to(m)] / 2;
    } else {
      if (move_capture(m)) {
        m.value =
            kRawPieceValueTable[move_capture(m)] - Value(move_piece_type(m));
      } else {
        m.value = (*main_history_)[us][FromTo(m)] +
                  (*continuation_history_[0])[move_piece(m, us)][move_to(m)] -
                  (1 << 28);
      }
    }
  }
}

/// MovePicker::select() returns the next move satisfying a predicate function.
/// It never returns the TT move.
template <MovePicker::PickType T, typename Pred>
Move MovePicker::Select(Pred filter) {
  while (cur_ < end_moves_) {
    if (T == kBest) std::swap(*cur_, *std::max_element(cur_, end_moves_));

    score_ = cur_->value;
    move_ = *cur_++;

    if (move_ != tt_move_ && filter()) return move_;
  }
  return move_ = kMoveNone;
}

/// MovePicker::next_move() is the most important method of the MovePicker
/// class. It returns a new pseudo legal move every time it is called until
/// there are no more moves left, picking the move with the highest score from a
/// list of generated moves.
Move MovePicker::NextMove(int *score) {
top:
  switch (stage_) {
    case kMainTt:
    case kEvasionTt:
    case kQsearchTt:
    case kProbcutTt:
    case kProbSearchTt:
      ++stage_;
      return tt_move_;

    case kCaptureInit:
    case kProbcutInit:
      cur_ = end_bad_captures_ = moves_;
      end_moves_ = generate<kCaptures>(pos_, cur_);

      Score<kCaptures>();
      ++stage_;
      goto top;

    case kQcaptureInit:
      cur_ = end_bad_captures_ = moves_;
      if (depth_ > kDepthQsRecaptues)
        end_moves_ = generate<kCaptures>(pos_, cur_);
      else
        end_moves_ = generate_recapture(pos_, recapture_square_, cur_);

      Score<kCaptures>();
      ++stage_;
      goto top;

    case kGoodCapture:
      if (Select<kBest>([&]() {
            return pos_.see_ge(move_, Value(-55 * (cur_ - 1)->value / 1024))
                       ?
                       // Move losing capture to endBadCaptures to be tried
                       // later
                       true
                       : (*end_bad_captures_++ = move_, false);
          }))
        return move_;

      // Prepare the pointers to loop over the refutations array
      cur_ = std::begin(refutations_);
      end_moves_ = std::end(refutations_);

      // If the countermove is the same as a killer, skip it
      if (refutations_[0].move == refutations_[2].move ||
          refutations_[1].move == refutations_[2].move)
        --end_moves_;

      ++stage_;
      /* fallthrough */

    case kRefutation:
      if (Select<kNext>([&]() {
            return move_ != kMoveNone && !move_is_capture(move_) &&
                   pos_.pseudo_legal(move_);
          }))
        return move_;
      ++stage_;
      /* fallthrough */

    case kQuietInit:
      cur_ = end_bad_captures_;
      end_moves_ = generate<kQuiets>(pos_, cur_);

      Score<kQuiets>();
      PartialInsertionSort(cur_, end_moves_, -4000 * depth_ / kOnePly);
      ++stage_;
      /* fallthrough */

    case kQuiet:
      if (Select<kNext>([&]() {
            return move_ != refutations_[0] && move_ != refutations_[1] &&
                   move_ != refutations_[2];
          }))
        return move_;

      // Prepare the pointers to loop over the bad captures
      cur_ = moves_;
      end_moves_ = end_bad_captures_;

      ++stage_;
      /* fallthrough */

    case kBadCapture:
      return Select<kNext>(Any);

    case kEvasionInit:
      cur_ = moves_;
      end_moves_ = generate<kEvasions>(pos_, cur_);

      Score<kEvasions>();
      ++stage_;
      /* fallthrough */

    case kEvasion:
      return Select<kBest>(Any);

    case kProbCut:
      return Select<kBest>([&]() { return pos_.see_ge(move_, threshold_); });

    case kQcapture:
      return Select<kBest>(Any);

      // If we did not find any move and we do not try checks, we have finished
      if (depth_ != kDepthQsChecks) return kMoveNone;

      ++stage_;
      /* fallthrough */

    case kQcheckInit:
      cur_ = moves_;
      end_moves_ = generate<kQuietChecks>(pos_, cur_);

      ++stage_;
      /* fallthrough */

    case kQcheck:
      return Select<kNext>(Any);

    case kProbSearchInit:
      cur_ = moves_;
      end_moves_ = generate<kLegalForSearch>(pos_, cur_);
      for (auto &m : *this) m.value = MoveScore::evaluate(pos_, *ci_, m.move);
      std::sort(cur_, end_moves_, [](const ExtMove &a, const ExtMove &b) {
        return a.value > b.value;
      });
      max_score_ = cur_->value;
      ++stage_;

    case kProbSearch:
      return Select<kNext>([&]() {
        *score = score_ - max_score_;
        return true;
      });
  }

  assert(false);
  return kMoveNone;  // Silence warning
}
