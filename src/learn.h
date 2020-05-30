/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

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

#ifndef _LEARN_H_
#define _LEARN_H_

#include <algorithm>
#include <atomic>
#include <mutex>
#include <set>
#include <vector>

#include "evaluate.h"
#include "move_generator.h"
#include "position.h"
#include "search.h"

enum AttackDirection {
  kNoneAttack,
  kDiagTopAttack,
  kTopAttack,
  kRankAttack,
  kDiagDownAttack,
  kDownAttack,
  kLanceAttack,
  kBishopAttack,
  kRookAttack,
  kKnightAttack,
  kAttackNum
};

struct MoveData {
  Move move;
  bool use_learn;
  bool other_pv_exist;
  std::vector<Move> pv_data;
};

struct GameData {
  std::string start_position;
  std::vector<MoveData> move_list;
};

struct RawEvaluater {
  float kpp_raw[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd];
  float kkp_raw[kBoardSquare][kBoardSquare][Eval::kFEEnd];

  void increment(const Position &pos, double d) {
    const Square black_king = pos.square_king(kBlack);
    const Square white_king = pos.square_king(kWhite);
    const Eval::KPPIndex *list_black = pos.black_kpp_list();
    const Eval::KPPIndex *list_white = pos.white_kpp_list();
    const float param = static_cast<float>(d / Eval::kFvScale);
    for (int i = 0; i < Eval::kListNum; ++i) {
      const int k0 = list_black[i];
      const int k1 = list_white[i];
      for (int j = 0; j < i; ++j) {
        const int l0 = list_black[j];
        const int l1 = list_white[j];
        kpp_raw[black_king][k0][l0] += param;
        kpp_raw[Eval::inverse(white_king)][k1][l1] -= param;
      }
      kkp_raw[black_king][white_king][k0] += param;
    }
  }

  void clear() { std::memset(this, 0, sizeof(*this)); }
};

inline RawEvaluater &operator+=(RawEvaluater &lhs, RawEvaluater &rhs) {
  for (auto lit = &(***std::begin(lhs.kpp_raw)),
            rit = &(***std::begin(rhs.kpp_raw));
       lit != &(***std::end(lhs.kpp_raw)); ++lit, ++rit)
    *lit += *rit;
  for (auto lit = &(***std::begin(lhs.kkp_raw)),
            rit = &(***std::begin(rhs.kkp_raw));
       lit != &(***std::end(lhs.kkp_raw)); ++lit, ++rit)
    *lit += *rit;

  return lhs;
}

struct BoardPosition {
  BoardPosition() : x(9), y(9) {}
  BoardPosition(Square sq) {
    x = sq % 9;
    y = sq / 9;
  }

  Square square() const { return Square(y * 9 + x); }

  Square inverse_square() const {
    int inverse_x = kFile9 - x;
    return Square(y * 9 + inverse_x);
  }

  Square lower_square() const {
    int lower_x = x;
    if (x > kFile5) lower_x = kFile9 - x;
    return Square(y * 9 + lower_x);
  }

  Square inverse_black_white() const {
    return Square(kBoardSquare - 1 - square());
  }

  int x;
  int y;
};

struct KingPosition : BoardPosition {
  KingPosition(Square sq) {
    x = sq % 9;
    if (x > kFile5) {
      x = kFile9 - x;
      swap = true;
    }
    y = sq / 9;
  }
  bool swap = false;
};

const Eval::KPPIndex KPPIndexTable[] = {
    Eval::kFHandPawn,   Eval::kEHandPawn,   Eval::kFHandLance,
    Eval::kEHandLance,  Eval::kFHandKnight, Eval::kEHandKnight,
    Eval::kFHandSilver, Eval::kEHandSilver, Eval::kFHandGold,
    Eval::kEHandGold,   Eval::kFHandBishop, Eval::kEHandBishop,
    Eval::kFHandRook,   Eval::kEHandRook,   Eval::kFPawn,
    Eval::kEPawn,       Eval::kFLance,      Eval::kELance,
    Eval::kFKnight,     Eval::kEKnight,     Eval::kFSilver,
    Eval::kESilver,     Eval::kFGold,       Eval::kEGold,
    Eval::kFBishop,     Eval::kEBishop,     Eval::kFHorse,
    Eval::kEHorse,      Eval::kFRook,       Eval::kERook,
    Eval::kFDragon,     Eval::kEDragon,     Eval::kFEEnd};

inline Eval::KPPIndex KppIndexBegin(Eval::KPPIndex i) {
  return *(
      std::upper_bound(std::begin(KPPIndexTable), std::end(KPPIndexTable), i) -
      1);
}

inline Eval::KPPIndex InverseFileKppIndex(Eval::KPPIndex i) {
  if (i < Eval::kFEHandEnd) return i;

  const int begin = KppIndexBegin(i);
  const Square sq = static_cast<Square>(i - begin);
  const BoardPosition pos = BoardPosition(sq);
  return static_cast<Eval::KPPIndex>(begin + pos.inverse_square());
}

inline Eval::KPPIndex LowerFileKppIndex(Eval::KPPIndex i) {
  if (i < Eval::kFEHandEnd) return i;

  const int begin = KppIndexBegin(i);
  const Square sq = static_cast<Square>(i - begin);
  const BoardPosition pos = BoardPosition(sq);
  return static_cast<Eval::KPPIndex>(begin + pos.lower_square());
}

struct KppIndex {
  KppIndex(Square k, Eval::KPPIndex in_i, Eval::KPPIndex in_j) {
    if (in_i == in_j) {
      i = 0;
      j = 0;
      king = kBoardSquare;
      return;
    }

    if (in_j < in_i) std::swap(in_i, in_j);
    KingPosition kp(k);
    if (kp.swap) {
      in_i = InverseFileKppIndex(in_i);
      in_j = InverseFileKppIndex(in_j);
      if (in_j < in_i) std::swap(in_i, in_j);
    } else if (kp.x == kFile5) {
      if (in_i >= Eval::kFPawn) {
        const int begin = KppIndexBegin(in_i);
        const BoardPosition i_pos(static_cast<Square>(in_i - begin));
        if (i_pos.x > kFile5) {
          in_i = static_cast<Eval::KPPIndex>(begin + i_pos.inverse_square());
          in_j = InverseFileKppIndex(in_j);
        } else if (i_pos.x == kFile5) {
          in_j = InverseFileKppIndex(in_j);
        }
        if (in_j < in_i) std::swap(in_i, in_j);
      }
    }
    i = in_i;
    j = in_j;
    king = kp.square();
  }

  Square king;
  int i;
  int j;
};

struct RelativePosition {
  RelativePosition(Square base, Square sq) {
    BoardPosition bp = BoardPosition(base);
    BoardPosition sqp = BoardPosition(sq);

    x = sqp.x - bp.x + 8;
    y = sqp.y - bp.y + 8;
  }

  int x;
  int y;
};

struct PieceParam {
  PieceParam(Eval::KPPIndex i) {
    const Eval::KPPIndex begin_index = KppIndexBegin(i);
    switch (begin_index) {
      case Eval::kFHandPawn:
        color = kBlack;
        piece = kBlackPawn;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        direction_num = 2;
        break;
      case Eval::kEHandPawn:
        color = kWhite;
        piece = kWhitePawn;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        direction_num = 2;
        break;
      case Eval::kFHandLance:
        color = kBlack;
        piece = kBlackLance;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kLanceAttack;
        direction_num = 3;
        break;
      case Eval::kEHandLance:
        color = kWhite;
        piece = kWhiteLance;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kLanceAttack;
        direction_num = 3;
        break;
      case Eval::kFHandKnight:
        color = kBlack;
        piece = kBlackKnight;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kKnightAttack;
        direction_num = 2;
        break;
      case Eval::kEHandKnight:
        color = kWhite;
        piece = kBlackKnight;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kKnightAttack;
        direction_num = 2;
        break;
      case Eval::kFHandSilver:
        color = kBlack;
        piece = kBlackSilver;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kDiagDownAttack;
        direction_num = 4;
        break;
      case Eval::kEHandSilver:
        color = kWhite;
        piece = kWhiteSilver;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kDiagDownAttack;
        direction_num = 4;
        break;
      case Eval::kFHandGold:
        color = kBlack;
        piece = kBlackGold;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kRankAttack;
        directions[4] = kDownAttack;
        direction_num = 5;
        break;
      case Eval::kEHandGold:
        color = kWhite;
        piece = kWhiteGold;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kRankAttack;
        directions[4] = kDownAttack;
        direction_num = 5;
        break;
      case Eval::kFHandBishop:
        color = kBlack;
        piece = kBlackBishop;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kDiagTopAttack;
        directions[2] = kDiagDownAttack;
        directions[3] = kBishopAttack;
        direction_num = 4;
        break;
      case Eval::kEHandBishop:
        color = kWhite;
        piece = kWhiteBishop;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kDiagTopAttack;
        directions[2] = kDiagDownAttack;
        directions[3] = kBishopAttack;
        direction_num = 4;
        break;
      case Eval::kFHandRook:
        color = kBlack;
        piece = kBlackRook;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDownAttack;
        directions[3] = kRankAttack;
        directions[4] = kRookAttack;
        directions[5] = kLanceAttack;
        direction_num = 6;
        break;
      case Eval::kEHandRook:
        color = kWhite;
        piece = kWhiteRook;
        square = kBoardSquare;
        hand_piece_num = i - begin_index;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDownAttack;
        directions[3] = kRankAttack;
        directions[4] = kRookAttack;
        directions[5] = kLanceAttack;
        direction_num = 6;
        break;
      case Eval::kFPawn:
        color = kBlack;
        piece = kBlackPawn;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        direction_num = 2;
        break;
      case Eval::kEPawn:
        color = kWhite;
        piece = kWhitePawn;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        direction_num = 2;
        break;
      case Eval::kFLance:
        color = kBlack;
        piece = kBlackLance;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kLanceAttack;
        direction_num = 3;
        break;
      case Eval::kELance:
        color = kWhite;
        piece = kWhiteLance;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kLanceAttack;
        direction_num = 3;
        break;
      case Eval::kFKnight:
        color = kBlack;
        piece = kBlackKnight;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kKnightAttack;
        direction_num = 2;
        break;
      case Eval::kEKnight:
        color = kWhite;
        piece = kWhiteKnight;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kKnightAttack;
        direction_num = 2;
        break;
      case Eval::kFSilver:
        color = kBlack;
        piece = kBlackSilver;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kDiagDownAttack;
        direction_num = 4;
        break;
      case Eval::kESilver:
        color = kWhite;
        piece = kWhiteSilver;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kDiagDownAttack;
        direction_num = 4;
        break;
      case Eval::kFGold:
        color = kBlack;
        piece = kBlackGold;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kRankAttack;
        directions[4] = kDownAttack;
        direction_num = 5;
        break;
      case Eval::kEGold:
        color = kWhite;
        piece = kWhiteGold;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDiagTopAttack;
        directions[3] = kRankAttack;
        directions[4] = kDownAttack;
        direction_num = 5;
        break;
      case Eval::kFBishop:
        color = kBlack;
        piece = kBlackBishop;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kDiagTopAttack;
        directions[2] = kDiagDownAttack;
        directions[3] = kBishopAttack;
        direction_num = 4;
        break;
      case Eval::kEBishop:
        color = kWhite;
        piece = kWhiteBishop;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kDiagTopAttack;
        directions[2] = kDiagDownAttack;
        directions[3] = kBishopAttack;
        direction_num = 4;
        break;
      case Eval::kFRook:
        color = kBlack;
        piece = kBlackRook;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDownAttack;
        directions[3] = kRankAttack;
        directions[4] = kRookAttack;
        directions[5] = kLanceAttack;
        direction_num = 6;
        break;
      case Eval::kERook:
        color = kWhite;
        piece = kWhiteRook;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDownAttack;
        directions[3] = kRankAttack;
        directions[4] = kRookAttack;
        directions[5] = kLanceAttack;
        direction_num = 6;
        break;
      case Eval::kFHorse:
        color = kBlack;
        piece = kBlackHorse;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kDiagTopAttack;
        directions[2] = kDiagDownAttack;
        directions[3] = kBishopAttack;
        directions[4] = kTopAttack;
        directions[5] = kRankAttack;
        directions[6] = kDownAttack;
        direction_num = 7;
        break;
      case Eval::kEHorse:
        color = kWhite;
        piece = kWhiteHorse;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kDiagTopAttack;
        directions[2] = kDiagDownAttack;
        directions[3] = kBishopAttack;
        directions[4] = kTopAttack;
        directions[5] = kRankAttack;
        directions[6] = kDownAttack;
        direction_num = 7;
        break;
      case Eval::kFDragon:
        color = kBlack;
        piece = kBlackDragon;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDownAttack;
        directions[3] = kRankAttack;
        directions[4] = kRookAttack;
        directions[5] = kLanceAttack;
        directions[6] = kDiagTopAttack;
        directions[7] = kDiagDownAttack;
        direction_num = 8;
        break;
      case Eval::kEDragon:
        color = kWhite;
        piece = kWhiteDragon;
        square = static_cast<Square>(i - begin_index);
        hand_piece_num = 0;
        directions[0] = kNoneAttack;
        directions[1] = kTopAttack;
        directions[2] = kDownAttack;
        directions[3] = kRankAttack;
        directions[4] = kRookAttack;
        directions[5] = kLanceAttack;
        directions[6] = kDiagTopAttack;
        directions[7] = kDiagDownAttack;
        direction_num = 8;
        break;
      default:
        assert(false);
        color = kBlack;
        piece = kEmpty;
        square = kBoardSquare;
        hand_piece_num = 0;
        direction_num = 0;
        break;
    }
  }

  Piece piece;
  AttackDirection directions[kAttackNum];
  int direction_num;
  Square square;
  Color color;
  int hand_piece_num;
};


constexpr int kPredictionsSize = 7;

class Learner {
 public:
  void learn(std::istringstream &is);

 private:
  bool read_file(std::string &file_name);

  void set_move(std::string &game, std::set<std::pair<Key, Move> > &dictionary);

  void learn_phase1_body(int thread_id);

  void learn_phase1();

  void learn_phase2_body(int thread_id);

  void learn_phase2();

  template <bool print>
  size_t increment_game_count();

  void set_update_mask(int step);

  template <bool Divide>
  void add_part_param();

  template <bool Divide>
  void add_part_kp_kkp_param(Square king, int kp_i, int sign, Square king1,
                             Square king2, int i, int32_t &ret_value,
                             int &ret_num);

  template <bool UsePenalty>
  void update_fv(int16_t &v, std::atomic<float> &dv_ref);

  template <bool UsePenalty>
  void update_eval();

  Value search_pv(Position &pos, Move record_move, Depth depth);

  void search_other_pv(Position &pos, Value record_value, Depth depth,
                       MoveList<kLegal> &legal_moves, Move record_move);

  std::vector<GameData> game_list_;
  std::vector<RawEvaluater> eval_list_;
  std::vector<Position> position_list_;
  RawEvaluater base_eval_;
  std::atomic<int64_t> predictions_[kPredictionsSize];
  std::atomic<int64_t> move_count_;
  std::mutex mutex_;
  size_t game_count_ = 0;
  size_t game_num_for_iteration_;
  int step_num_;
  int thread_num_;
  int min_depth_;
  int max_depth_;
  bool use_penalty_;
  int64_t update_max_mask_;
  int64_t update_min_mask_;
  int64_t update_mask_;
  std::mt19937_64 mt64_;
};

int inverse_black_white_kpp_index(int i);

#endif
