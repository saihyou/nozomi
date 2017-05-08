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

#include <vector>
#include <atomic>
#include <set>
#include <algorithm>
#include <mutex>
#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "move_generator.h"

struct MoveData
{
  Move move;
  bool use_learn;
  bool other_pv_exist;
  std::vector<Move> pv_data;
};

struct GameData
{
  std::string start_position;
  std::vector<MoveData> move_list;
};

struct RawEvaluater
{
  float kpp_raw[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd];
  float kkp_raw[kBoardSquare][kBoardSquare][Eval::kFEEnd];

  void
  increment(const Position &pos, double d)
  {
    const Square black_king = pos.square_king(kBlack);
    const Square white_king = pos.square_king(kWhite);
    const Eval::KPPIndex *list_black = pos.black_kpp_list();
    const Eval::KPPIndex *list_white = pos.white_kpp_list();
    const float param = static_cast<float>(d / Eval::kFvScale);
    for (int i = 0; i < Eval::kListNum; ++i)
    {
      const int k0 = list_black[i];
      const int k1 = list_white[i];
      for (int j = 0; j < i; ++j)
      {
        const int l0 = list_black[j];
        const int l1 = list_white[j];
        kpp_raw[black_king][k0][l0] += param;
        kpp_raw[Eval::inverse(white_king)][k1][l1] -= param;
      }
      kkp_raw[black_king][white_king][k0] += param;
    }
  }

  void
  clear()
  {
    std::memset(this, 0, sizeof(*this));
  }
};

inline RawEvaluater&
operator += (RawEvaluater &lhs, RawEvaluater &rhs)
{
  for 
  (
    auto lit = &(***std::begin(lhs.kpp_raw)), rit = &(***std::begin(rhs.kpp_raw));
    lit != &(***std::end(lhs.kpp_raw));
    ++lit, ++rit
  )
    *lit += *rit;
  for
  (
    auto lit = &(***std::begin(lhs.kkp_raw)), rit = &(***std::begin(rhs.kkp_raw));
    lit != &(***std::end(lhs.kkp_raw));
    ++lit, ++rit
  )
    *lit += *rit;

  return lhs;
}

struct BoardPosition
{
  BoardPosition() : x(9), y(9) {}
  BoardPosition(Square sq)
  {
    x = sq % 9;
    y = sq / 9;
  }

  Square
  square() const
  {
    return Square(y * 9 + x);
  }

  Square
  inverse_square() const
  {
    int inverse_x = kFile9 - x;
    return Square(y * 9 + inverse_x);
  }

  Square
  lower_square() const
  {
    int lower_x = x;
    if (x > kFile5)
      lower_x = kFile9 - x;
    return Square(y * 9 + lower_x);
  }

  Square
  inverse_black_white() const
  {
    return Square(kBoardSquare - 1 - square());
  }

  int x;
  int y;
};

struct KingPosition : BoardPosition
{
  KingPosition(Square sq)
  {
    x = sq % 9;
    if (x > kFile5)
    {
      x = kFile9 - x;
      swap = true;
    }
    y = sq / 9;
  }
  bool swap = false;
};

const int
KPPIndexTable[] =
{
  Eval::kFHandPawn,
  Eval::kEHandPawn,
  Eval::kFHandLance,
  Eval::kEHandLance,
  Eval::kFHandKnight,
  Eval::kEHandKnight,
  Eval::kFHandSilver,
  Eval::kEHandSilver,
  Eval::kFHandGold,
  Eval::kEHandGold,
  Eval::kFHandBishop,
  Eval::kEHandBishop,
  Eval::kFHandRook,
  Eval::kEHandRook,
  Eval::kFPawn,
  Eval::kEPawn,
  Eval::kFLance,
  Eval::kELance,
  Eval::kFKnight,
  Eval::kEKnight,
  Eval::kFSilver,
  Eval::kESilver,
  Eval::kFGold,
  Eval::kEGold,
  Eval::kFBishop,
  Eval::kEBishop,
  Eval::kFHorse,
  Eval::kEHorse,
  Eval::kFRook,
  Eval::kERook,
  Eval::kFDragon,
  Eval::kEDragon,
  Eval::kFEEnd
};

inline int
kpp_index_begin(int i)
{
  return *(std::upper_bound(std::begin(KPPIndexTable), std::end(KPPIndexTable), i) - 1);
}

inline int
inverse_file_kpp_index(int i)
{
  if (i < Eval::kFEHandEnd)
    return i;

  const int begin = kpp_index_begin(i);
  const Square sq = static_cast<Square>(i - begin);
  const BoardPosition pos = BoardPosition(sq);
  return static_cast<int>(begin + pos.inverse_square());
}

inline int
lower_file_kpp_index(int i)
{
  if (i < Eval::kFEHandEnd)
    return i;

  const int begin = kpp_index_begin(i);
  const Square sq = static_cast<Square>(i - begin);
  const BoardPosition pos = BoardPosition(sq);
  return static_cast<int>(begin + pos.lower_square());
}


struct KppIndex
{
  KppIndex(Square k, int in_i, int in_j)
  {
    if (in_i == in_j)
    {
      i = 0;
      j = 0;
      king = kBoardSquare;
      return;
    }

    if (in_j < in_i)
      std::swap(in_i, in_j);
    KingPosition kp(k);
    if (kp.swap)
    {
      in_i = inverse_file_kpp_index(in_i);
      in_j = inverse_file_kpp_index(in_j);
      if (in_j < in_i)
        std::swap(in_i, in_j);
    }
    else if (kp.x == kFile5)
    {
      if (in_i >= Eval::kFPawn)
      {
        const int begin = kpp_index_begin(in_i);
        const BoardPosition i_pos(static_cast<Square>(in_i - begin));
        if (i_pos.x > kFile5)
        {
          in_i = begin + i_pos.inverse_square();
          in_j = inverse_file_kpp_index(in_j);
        }
        else if (i_pos.x == kFile5)
        {
          in_j = lower_file_kpp_index(in_j);
        }
        if (in_j < in_i)
          std::swap(in_i, in_j);
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

constexpr int
kPredictionsSize = 7;

class Learner
{
public:
  void
  learn(std::istringstream &is);

private:
  bool
  read_file(std::string &file_name);

  void
  set_move(std::string &game, std::set<std::pair<Key, Move> > &dictionary);

  void
  learn_phase1_body(int thread_id);

  void
  learn_phase1();

  void
  learn_phase2_body(int thread_id);

  void
  learn_phase2();

  template <bool print>
  size_t
  increment_game_count();

  void
  set_update_mask(int step);

  template<bool Divide>
  void
  add_part_param();

  template<bool Divide>
  void
  add_part_kp_kkp_param(Square king, int kp_i, int sign, Square king1, Square king2, int i, int32_t &ret_value, int &ret_num);

  template <bool UsePenalty>
  void
  update_fv(int16_t &v, std::atomic<float> &dv_ref);

  template <bool UsePenalty>
  void
  update_eval();

  Value
  search_pv(Position &pos, Move record_move, Depth depth);

  void
  search_other_pv
  (
    Position &pos,
    Value record_value,
    Depth depth,
    MoveList<kLegal> &legal_moves,
    Move record_move
  );


  std::vector<GameData> game_list_;
  std::vector<RawEvaluater> eval_list_;
  std::vector<Position> position_list_;
  RawEvaluater base_eval_;
  std::atomic<int64_t> predictions_[kPredictionsSize];
  std::atomic<int64_t> move_count_;
  std::mutex mutex_;
  size_t   game_count_ = 0;
  size_t   game_num_for_iteration_;
  int      step_num_;
  int      thread_num_;
  int      min_depth_;
  int      max_depth_;
  bool     use_penalty_;
  int64_t  update_max_mask_;
  int64_t  update_min_mask_;
  int64_t  update_mask_;
  std::mt19937_64 mt64_;
};

int
inverse_black_white_kpp_index(int i);

#endif
