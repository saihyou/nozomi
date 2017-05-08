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
#ifdef LEARN

#include <string>
#include <fstream>
#include <mutex>
#include <random>
#include <thread>
#include <cstring>
#include <atomic>
#include <algorithm>
#include "types.h"
#include "evaluate.h"
#include "learn.h"
#include "search.h"
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"

#if 0
#define PRINT_PV(x) x
#else
#define PRINT_PV(x)
#endif

const char *kStartSFEN = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";

enum AttackDirection
{
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

template<typename Type> struct PartEvaluater
{
  Type abs_kpp[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd];
  Type abs_kpd[kBoardSquare][Eval::kFEEnd][kNumberOfColor][kBoardSquare][kAttackNum];
  Type abs_pp[Eval::kFEEnd][Eval::kFEEnd];
  Type abs_pd[Eval::kFEEnd][kNumberOfColor][kBoardSquare][kAttackNum];
  Type rel_kpp_board[kPieceMax][17][17][kPieceMax][17][17];
  Type rel_kpp_hand[Eval::kFEHandEnd][kPieceMax][17][17];
  Type rel_kpd_board[kPieceMax][17][17][kNumberOfColor][17][17][kAttackNum];
  Type rel_kpd_hand[Eval::kFEHandEnd][kNumberOfColor][17][17][kAttackNum];
  Type rel_pp_board[kPieceMax][kPieceMax][17][17];
  Type rel_pp_hand[Eval::kFEHandEnd][kPieceMax];
  Type rel_pd_board[kPieceMax][kNumberOfColor][17][17][kAttackNum];
  Type rel_pd_hand[Eval::kFEHandEnd][kNumberOfColor][kAttackNum];
  Type abs_kkp[kBoardSquare][kBoardSquare][Eval::kFEEnd];
  Type abs_kkd[kBoardSquare][kBoardSquare][kBoardSquare][kAttackNum];
  Type abs_kp[kBoardSquare][Eval::kFEEnd];
  Type abs_kd[kBoardSquare][kNumberOfColor][kBoardSquare][kAttackNum];
  Type rel_kkp_board[17][17][kPieceMax][17][17];
  Type rel_kkp_hand[17][17][Eval::kFEHandEnd];
  Type rel_kkd_board[17][17][kNumberOfColor][17][17][kAttackNum];
  Type rel_kp_board[kPieceMax][17][17];
  Type rel_kd_board[kNumberOfColor][17][17][kAttackNum];
  Type abs_kk[kBoardSquare][kBoardSquare];

  void
  clear()
  {
    std::memset(this, 0, sizeof(*this));
  }

  size_t
  number_of() const
  {
    return sizeof(*this) / sizeof(Type);
  }

  Type *
  get_adress(size_t i)
  {
    return (reinterpret_cast<Type *>(this->abs_kpp) + i);
  }
};


struct PieceParam
{
  PieceParam(int i)
  {
    const int begin_index = kpp_index_begin(i);
    switch (begin_index)
    {
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

struct RelativePosition
{
  RelativePosition(Square base, Square sq)
  {
    BoardPosition bp = BoardPosition(base);
    BoardPosition sqp = BoardPosition(sq);

    x = sqp.x - bp.x + 8;
    y = sqp.y - bp.y + 8;
  }

  int x;
  int y;
};

constexpr int kFvWindow = 256;
static PartEvaluater<std::atomic<float> > *g_part_param;
static PartEvaluater<int16_t>             *g_part_value;

int
inverse_black_white_kpp_index(int i)
{
  const int begin_index = kpp_index_begin(i);
  int hand_piece_num;
  int result = 0;
  int square;
  switch (begin_index)
  {
  case Eval::kFHandPawn:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandPawn + hand_piece_num;
    break;
  case Eval::kEHandPawn:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandPawn + hand_piece_num;
    break;
  case Eval::kFHandLance:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandLance + hand_piece_num;
    break;
  case Eval::kEHandLance:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandLance + hand_piece_num;
    break;
  case Eval::kFHandKnight:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandKnight + hand_piece_num;
    break;
  case Eval::kEHandKnight:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandKnight + hand_piece_num;
    break;
  case Eval::kFHandSilver:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandSilver + hand_piece_num;
    break;
  case Eval::kEHandSilver:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandSilver + hand_piece_num;
    break;
  case Eval::kFHandGold:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandGold + hand_piece_num;
    break;
  case Eval::kEHandGold:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandGold + hand_piece_num;
    break;
  case Eval::kFHandBishop:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandBishop + hand_piece_num;
    break;
  case Eval::kEHandBishop:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandBishop + hand_piece_num;
    break;
  case Eval::kFHandRook:
    hand_piece_num = i - begin_index;
    result = Eval::kEHandRook + hand_piece_num;
    break;
  case Eval::kEHandRook:
    hand_piece_num = i - begin_index;
    result = Eval::kFHandRook + hand_piece_num;
    break;
  case Eval::kFPawn:
    square = i - begin_index;
    result = Eval::kEPawn + kBoardSquare - 1 - square;
    break;
  case Eval::kEPawn:
    square = i - begin_index;
    result = Eval::kFPawn + kBoardSquare - 1 - square;
    break;
  case Eval::kFLance:
    square = i - begin_index;
    result = Eval::kELance + kBoardSquare - 1 - square;
    break;
  case Eval::kELance:
    square = i - begin_index;
    result = Eval::kFLance + kBoardSquare - 1 - square;
    break;
  case Eval::kFKnight:
    square = i - begin_index;
    result = Eval::kEKnight + kBoardSquare - 1 - square;
    break;
  case Eval::kEKnight:
    square = i - begin_index;
    result = Eval::kFKnight + kBoardSquare - 1 - square;
    break;
  case Eval::kFSilver:
    square = i - begin_index;
    result = Eval::kESilver + kBoardSquare - 1 - square;
    break;
  case Eval::kESilver:
    square = i - begin_index;
    result = Eval::kFSilver + kBoardSquare - 1 - square;
    break;
  case Eval::kFGold:
    square = i - begin_index;
    result = Eval::kEGold + kBoardSquare - 1 - square;
    break;
  case Eval::kEGold:
    square = i - begin_index;
    result = Eval::kFGold + kBoardSquare - 1 - square;
    break;
  case Eval::kFBishop:
    square = i - begin_index;
    result = Eval::kEBishop + kBoardSquare - 1 - square;
    break;
  case Eval::kEBishop:
    square = i - begin_index;
    result = Eval::kFBishop + kBoardSquare - 1 - square;
    break;
  case Eval::kFRook:
    square = i - begin_index;
    result = Eval::kERook + kBoardSquare - 1 - square;
    break;
  case Eval::kERook:
    square = i - begin_index;
    result = Eval::kFRook + kBoardSquare - 1 - square;
    break;
  case Eval::kFHorse:
    square = i - begin_index;
    result = Eval::kEHorse + kBoardSquare - 1 - square;
    break;
  case Eval::kEHorse:
    square = i - begin_index;
    result = Eval::kFHorse + kBoardSquare - 1 - square;
    break;
  case Eval::kFDragon:
    square = i - begin_index;
    result = Eval::kEDragon + kBoardSquare - 1 - square;
    break;
  case Eval::kEDragon:
    square = i - begin_index;
    result = Eval::kFDragon + kBoardSquare - 1 - square;
    break;
  default:
    assert(false);
    break;
  }
  return result;
}


void
add_atmic_float(std::atomic<float> &target, float param)
{
  float old = target.load(std::memory_order_consume);
  float desired = old + param;
  while 
  (
    !target.compare_exchange_weak
    (
      old,
      desired,
      std::memory_order_release,
      std::memory_order_consume
    )
  )
    desired = old + param;
}

double
sigmoid(double x)
{
  const double a = 7.0 / static_cast<double>(kFvWindow);
  const double clipx = std::max(static_cast<double>(-kFvWindow), std::min(static_cast<double>(kFvWindow), x));
  return 1.0 / (1.0 + exp(-a * clipx));
}

double
dsigmoid(double x)
{
  if (x <= -kFvWindow || x >= kFvWindow)
    return 0.0;
  const double a = 7.0 / static_cast<double>(kFvWindow);
  return a * sigmoid(x) * (1 - sigmoid(x));
}

constexpr double
fv_penalty()
{
  return (0.2 / static_cast<double>(Eval::kFvScale));
}

template <bool UsePenalty>
void
Learner::update_fv(int16_t &v, std::atomic<float> &dv_ref)
{
  float dv = dv_ref.load();
  const int step = static_cast<int>(_mm_popcnt_u64(mt64_() & update_mask_));
  if (UsePenalty)
  {
    if (v > 0)
      dv -= static_cast<float>(fv_penalty());
    else if (v < 0)
      dv += static_cast<float>(fv_penalty());
  }
  else
  {
    if (dv == 0)
      return;
  }

  if (dv >= 0.0 && v <= std::numeric_limits<int16_t>::max() - step)
    v += step;
  else if (dv <= 0.0 && v >= std::numeric_limits<int16_t>::min() + step)
    v -= step;
}

template <bool UsePenalty>
void
Learner::update_eval()
{
  for (size_t i = 0; i < g_part_value->number_of(); ++i)
    update_fv<UsePenalty>(*(g_part_value->get_adress(i)), *(g_part_param->get_adress(i)));

  std::ofstream ofs("part_value.bin", std::ios::binary);
  ofs.write(reinterpret_cast<char *>(g_part_value), sizeof(PartEvaluater<int16_t>));
  ofs.close();

  add_part_param<false>();
  std::ofstream fs("new_fv.bin", std::ios::binary);
  fs.write(reinterpret_cast<char *>(Eval::KPP), sizeof(Eval::KPP));
  fs.write(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
  fs.close();
}

void
Learner::learn_phase2_body(int thread_id)
{
  SearchStack ss[2];
  RawEvaluater &eval_data = (thread_id == 0) ? base_eval_ : eval_list_[thread_id - 1];
  eval_data.clear();
  for 
  (
    size_t i = increment_game_count<false>();
    i < game_num_for_iteration_;
    i = increment_game_count<false>()
  )
  {
    GameData &game_data = game_list_[i];
    Search::StateStackPtr setup_states = Search::StateStackPtr(new std::stack<StateInfo>());
    Position &pos = position_list_[thread_id];
    pos.set(game_data.start_position, Threads[thread_id + 1]);
    for (auto &move_data : game_data.move_list)
    {
      PRINT_PV(pos.print());
      if (!move_data.use_learn || !move_data.other_pv_exist)
      {
        setup_states->push(StateInfo());
        pos.do_move(move_data.move, setup_states->top());
        continue;
      }

      int pv_index = 0;
      PRINT_PV(std::cout << "recordpv: ");
      const Color root_color = pos.side_to_move();
      for (; move_data.pv_data[pv_index] != kMoveNone; ++pv_index)
      {
        PRINT_PV(std::cout << USI::format_move(move_data.pv_data[pv_index]));
        setup_states->push(StateInfo());
        pos.do_move(move_data.pv_data[pv_index], setup_states->top());
      }
      ss[0].evaluated = false;
      const Value record_value = 
        (root_color == pos.side_to_move())
        ?
        Eval::evaluate(pos, ss + 1)
        :
        -Eval::evaluate(pos, ss + 1);
      PRINT_PV(std::cout << ", value: " << record_value << std::endl);
      for (int j = pv_index - 1; j >= 0; --j)
        pos.undo_move(move_data.pv_data[j]);

      double sum_dt = 0.0;
      int pv_data_num = static_cast<int>(move_data.pv_data.size());
      for (int other = pv_index + 1; other < pv_data_num; ++other)
      {
        PRINT_PV(std::cout << "otherpv : ");
        for (; move_data.pv_data[other] != kMoveNone; ++other)
        {
          PRINT_PV(std::cout << USI::format_move(move_data.pv_data[other]));
          setup_states->push(StateInfo());
          pos.do_move(move_data.pv_data[other], setup_states->top());
        }
        ss[0].evaluated = false;
        const Value value =
          (root_color == pos.side_to_move())
          ?
          Eval::evaluate(pos, ss + 1)
          :
          -Eval::evaluate(pos, ss + 1);
        const double diff = value - record_value;
        const double dt = (root_color == kBlack) ? dsigmoid(diff) : -dsigmoid(diff);
        PRINT_PV(std::cout << ", value: " << value << ", dT: " << dt << std::endl);
        sum_dt += dt;
        eval_data.increment(pos, -dt);
        for (int j = other - 1; move_data.pv_data[j] != kMoveNone; --j)
          pos.undo_move(move_data.pv_data[j]);
      }

      for (int j = 0; j < pv_index; ++j)
      {
        setup_states->push(StateInfo());
        pos.do_move(move_data.pv_data[j], setup_states->top());
      }
      eval_data.increment(pos, sum_dt);
      for (int j = pv_index - 1; j >= 0; --j)
        pos.undo_move(move_data.pv_data[j]);
      setup_states->push(StateInfo());
      pos.do_move(move_data.move, setup_states->top());
    }
  }
}

void
Learner::learn_phase2()
{
  for (int step = 0; step < step_num_; ++step)
  {
    std::cout << "step" << step + 1 << "/" << step_num_ << " " << std::flush;
    game_count_ = 0;
    std::vector<std::thread> threads(thread_num_ - 1);
    for (int i = 1; i < thread_num_; ++i)
      threads[i - 1] = std::thread([this, i] { learn_phase2_body(i); });
    learn_phase2_body(0);
    for (auto &thread : threads)
      thread.join();
    for (auto &data : eval_list_)
      base_eval_ += data;
    g_part_param->clear();
    add_part_param<true>();
    set_update_mask(step);
    std::cout << "update eval ... " << std::flush;
    if (use_penalty_)
      update_eval<true>();
    else
      update_eval<false>();
    std::cout << "done" << std::endl;
  }
}

void
Learner::learn_phase1_body(int thread_id)
{
  std::random_device seed_gen;
  std::default_random_engine engine(seed_gen());
  std::uniform_int_distribution<> dist(min_depth_, max_depth_);

  for 
  (
    size_t i = increment_game_count<true>();
    i < game_num_for_iteration_;
    i = increment_game_count<true>()
  )
  {
    GameData &game_data = game_list_[i];
    Search::StateStackPtr setup_states = Search::StateStackPtr(new std::stack<StateInfo>());
    Position &pos = position_list_[thread_id];
    pos.set(game_data.start_position, Threads[thread_id + 1]);

    for (auto &move_data : game_data.move_list)
    {
      MoveList<kLegal> legal_moves(pos);

      bool is_legal = false;
      for (auto &m : legal_moves)
      {
        if (m.move == move_data.move)
        {
          is_legal = true;
          break;
        }
      }
      if (!is_legal)
      {
        move_data.use_learn = false;
        std::cout << "ilegal move" << std::endl;
        break;
      }

      if (move_data.use_learn)
      {
        Depth depth = static_cast<Depth>(dist(engine));
        const Value recode_value =
          search_pv
          (
            pos,
            move_data.move,
            depth
          );

        ++move_count_;
        int record_is_nth = 0;

        move_data.pv_data.clear();
        move_data.other_pv_exist = false;
        if (-kValueMaxEvaluate < recode_value && recode_value < kValueMaxEvaluate)
        {
          Thread *thread = pos.this_thread();
          move_data.pv_data.insert
          (
            std::end(move_data.pv_data),
            std::begin(thread->root_moves_[0].pv),
            std::end(thread->root_moves_[0].pv)
          );
          move_data.pv_data.push_back(kMoveNone);

          search_other_pv
          (
            pos,
            recode_value,
            depth,
            legal_moves,
            move_data.move
          );

          for (auto &rm : thread->root_moves_)
          {
            if (recode_value - kFvWindow < rm.score && rm.score < recode_value + kFvWindow)
            {
              move_data.pv_data.insert
              (
                std::end(move_data.pv_data),
                std::begin(rm.pv),
                std::end(rm.pv)
              );
              move_data.pv_data.push_back(kMoveNone);
              move_data.other_pv_exist = true;
            }
            if (recode_value <= rm.score)
              ++record_is_nth;
          }

          for (int i = record_is_nth; i < kPredictionsSize; i++)
            ++predictions_[i];
        }
      }
      setup_states->push(StateInfo());
      pos.do_move(move_data.move, setup_states->top());
    }
  }
}

void
Learner::learn_phase1()
{
  std::random_device seed_gen;
  std::default_random_engine engine(seed_gen());
  std::shuffle(std::begin(game_list_), std::end(game_list_), engine);
  for (auto &pred : predictions_)
    pred.store(0);
  move_count_.store(0);
  game_count_ = 0;
  TT.clear();
  Search::Limits.infinite = 1;
  Search::Signals.stop_on_ponder_hit = false;
  Search::Signals.stop = false;

  std::vector<std::thread> threads(thread_num_ - 1);
  for (int i = 1; i < thread_num_; ++i)
    threads[i - 1] = std::thread([this, i] { learn_phase1_body(i); });
  learn_phase1_body(0);
  for (auto &thread : threads)
    thread.join();

  std::cout << "\nGames       = " << game_list_.size()
            << "\nTotal Moves = " << move_count_
            << "\nPrediction  = ";
  for (auto &pred : predictions_)
    std::cout << static_cast<double>(pred.load() * 100) / move_count_.load() << ", ";
  std::cout << std::endl;
}

void
Learner::learn(std::istringstream &is)
{
  std::string record_file_name;
  int64_t game_num;
  int update_max;
  int update_min;

  is >> record_file_name;
  is >> game_num;
  is >> thread_num_;
  is >> min_depth_;
  is >> max_depth_;
  is >> step_num_;
  is >> game_num_for_iteration_;
  is >> update_max;
  is >> update_min;
  is >> use_penalty_;

  if (update_max < 0 || update_max > 64)
    update_max = 64;
  if (update_min < 0 || update_min > update_max)
    update_min = update_max;

  std::cout << "record_file      : " << record_file_name << std::endl;
  std::cout << "read_games       : " << (game_num == 0 ? "all" : std::to_string(game_num)) << std::endl;
  std::cout << "thread_num       : " << thread_num_ << std::endl;
  std::cout << "search_depth_min : " << min_depth_ << std::endl;
  std::cout << "search_depth_max : " << max_depth_ << std::endl;
  std::cout << "step_num         : " << step_num_ << std::endl;
  std::cout << "game_num_for_iteration : " << game_num_for_iteration_ << std::endl;
  std::cout << "update_max       : " << update_max << std::endl;
  std::cout << "update_min       : " << update_min << std::endl;
  std::cout << "use_penalty      : " << use_penalty_ << std::endl;

  g_part_param =
    (PartEvaluater<std::atomic<float> > *)malloc(sizeof(PartEvaluater<std::atomic<float> >));
  g_part_value =
    (PartEvaluater<int16_t> *)malloc(sizeof(PartEvaluater<int16_t>));

  std::ifstream ifs("part_value.bin", std::ios::in | std::ios::binary);
  if (ifs)
  {
    ifs.read(reinterpret_cast<char *>(g_part_value), sizeof(PartEvaluater<int16_t>));
    add_part_param<false>();
    ifs.close();
  }
  else
  {
    std::memset(g_part_value, 0, sizeof(PartEvaluater<int16_t>));
  }

  mt64_ = std::mt19937_64(std::chrono::system_clock::now().time_since_epoch().count());
  update_max_mask_ = (uint64_t(1) << update_max) - 1;
  update_min_mask_ = (uint64_t(1) << update_min) - 1;
  set_update_mask(step_num_);
  for (int i = 0; i < thread_num_ - 1; ++i)
    eval_list_.push_back(base_eval_);

  for (int i = 0; i < thread_num_; ++i)
  {
    position_list_.push_back(Position());
    Threads.push_back(new Thread);
  }

  read_file(record_file_name);
  if (game_num_for_iteration_ == 0)
    game_num_for_iteration_ = game_list_.size();
  for (int i = 0; ; ++i)
  {
    std::cout << "iteration " << i << std::endl;
    std::cout << "parse1 start" << std::endl;
    learn_phase1();
    std::cout << "parse2 start" << std::endl;
    learn_phase2();
  }

  free(g_part_param);
  free(g_part_value);
}

void
Learner::set_update_mask(int step)
{
  const int step_max = step_num_;
  const int max = static_cast<int>(_mm_popcnt_u64(update_max_mask_));
  const int min = static_cast<int>(_mm_popcnt_u64(update_min_mask_));
  update_mask_ = max - (((max - min) * step + (step_max >> 1)) / step_max);
}

bool
Learner::read_file(std::string &file_name)
{
  std::ifstream ifs(file_name.c_str(), std::ios::binary);
  if (!ifs)
  {
    std::cout << "" << file_name << std::endl;
    return false;
  }

  std::set<std::pair<Key, Move> > dictionary;
  std::string sfen;
  while (true)
  {
    std::getline(ifs, sfen);
    if (!ifs)
      break;
    set_move(sfen, dictionary);
  }
  return true;
}

void
Learner::set_move(std::string &game, std::set<std::pair<Key, Move> > &dictionary)
{
  std::string token, sfen;
  std::istringstream is(game);

  is >> token;

  if (token == "startpos")
  {
    sfen = kStartSFEN;
    is >> token;
  }
  else if (token == "sfen")
  {
    while (is >> token && token != "moves")
      sfen += token + " ";
  }
  else
  {
    return;
  }

  GameData game_data;
  game_data.start_position = sfen;
  Position pos(sfen, Threads.main());
  Move m;
  Search::StateStackPtr setup_states = Search::StateStackPtr(new std::stack<StateInfo>());
  while (is >> token && (m = USI::to_move(pos, token)) != kMoveNone)
  {
    MoveData data;
    data.move = m;
    if (dictionary.find(std::make_pair(pos.key(), m)) == std::end(dictionary))
    {
      dictionary.insert(std::make_pair(pos.key(), m));
      data.use_learn = true;
    }
    else
    {
      data.use_learn = false;
    }
    game_data.move_list.push_back(data);
    setup_states->push(StateInfo());
    pos.do_move(m, setup_states->top());
  }
  game_list_.push_back(game_data);
}

template<bool print>
size_t
Learner::increment_game_count()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (print)
  {
    if ((game_count_ + 1) % 500 == 0)
      std::cout << (game_count_ + 1) << std::endl;
    else if ((game_count_ + 1) % 100 == 0)
      std::cout << "o" << std::flush;
    else if ((game_count_ + 1) % 10 == 0)
      std::cout << "." << std::flush;
  }
  return game_count_++;
}

template<bool Divide>
void
Learner::add_part_param()
{
#if defined _OPENMP
#pragma omp parallel
#endif
  {
#ifdef _OPENMP
#pragma omp for
#endif
    for (int k = k9A; k < kBoardSquare; ++k)
    {
      Square king = static_cast<Square>(k);
      for (int i = 0; i < Eval::kFEEnd; ++i)
      {
        for (int j = 0; j < Eval::kFEEnd; ++j)
        {
          int32_t kpp_tmp = 0;
          int     part_num = 0;
          
          if (i == j)
          {
            if (!Divide)
              Eval::KPP[king][i][j] = 0;
            continue;
          }

          KppIndex kpp_index(king, i, j);
          if (Divide)
          {
            add_atmic_float
            (
              g_part_param->abs_kpp[kpp_index.king][kpp_index.i][kpp_index.j],
              base_eval_.kpp_raw[king][i][j]
            );
          }
          else
          {
            kpp_tmp +=
              g_part_value->abs_kpp[kpp_index.king][kpp_index.i][kpp_index.j];
            ++part_num;
          }

          if (kpp_index.j < Eval::kFEHandEnd)
          {
            // 両方持ち駒
            if (Divide)
            {
              add_atmic_float
              (
                g_part_param->abs_pp[kpp_index.i][kpp_index.j],
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->abs_kp[kpp_index.king][kpp_index.i],
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->abs_kp[kpp_index.king][kpp_index.j],
                base_eval_.kpp_raw[king][i][j]
              );
            }
            else
            {
              kpp_tmp += g_part_value->abs_pp[kpp_index.i][kpp_index.j];
              ++part_num;
              kpp_tmp += g_part_value->abs_kp[kpp_index.king][kpp_index.i];
              ++part_num;
              kpp_tmp += g_part_value->abs_kp[kpp_index.king][kpp_index.j];
              ++part_num;
            }
          }
          else if (kpp_index.i < Eval::kFEHandEnd)
          {
            // iは持ち駒、jは盤上
            int lower_j = lower_file_kpp_index(kpp_index.j);
            PieceParam pj(kpp_index.j);
            PieceParam lpj(lower_j);
            RelativePosition rlpj(kpp_index.king, lpj.square);
            if (rlpj.x > 8)
              rlpj.x = 16 - rlpj.x;

            if (king == pj.square)
              continue;

            if (Divide)
            {
              add_atmic_float
              (
                g_part_param->abs_kp[kpp_index.king][kpp_index.i],
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->abs_kp[kpp_index.king][lower_j],
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->abs_pp[kpp_index.i][lower_j],
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->rel_kpp_hand[kpp_index.i][lpj.piece][rlpj.y][rlpj.x], 
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->rel_kp_board[lpj.piece][rlpj.y][rlpj.x],
                base_eval_.kpp_raw[king][i][j]
              );
              add_atmic_float
              (
                g_part_param->rel_pp_hand[kpp_index.i][pj.piece],
                base_eval_.kpp_raw[king][i][j]
              );
            }
            else
            {
              kpp_tmp += g_part_value->abs_kp[kpp_index.king][kpp_index.i];
              ++part_num;
              kpp_tmp += g_part_value->abs_kp[kpp_index.king][lower_j];
              ++part_num;
              kpp_tmp += g_part_value->abs_pp[kpp_index.i][lower_j];
              ++part_num;
              kpp_tmp += g_part_value->rel_kpp_hand[kpp_index.i][lpj.piece][rlpj.y][rlpj.x];
              ++part_num;
              kpp_tmp += g_part_value->rel_kp_board[lpj.piece][rlpj.y][rlpj.x];
              ++part_num;
              kpp_tmp += g_part_value->rel_pp_hand[kpp_index.i][pj.piece];
              ++part_num;
            }

            int32_t tmp = 0;
            for (int d = 0; d < pj.direction_num; ++d)
            {
              if (Divide)
              {
                add_atmic_float
                (
                  g_part_param->abs_kpd[kpp_index.king][kpp_index.i][pj.color][pj.square][pj.directions[d]], 
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->abs_pd[kpp_index.i][pj.color][lpj.square][pj.directions[d]], 
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->abs_kd[kpp_index.king][pj.color][pj.square][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_kpd_hand[kpp_index.i][pj.color][rlpj.y][rlpj.x][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_pd_hand[kpp_index.i][pj.color][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_kd_board[pj.color][rlpj.y][rlpj.x][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
              }
              else
              {
                tmp += g_part_value->abs_kpd[kpp_index.king][kpp_index.i][pj.color][pj.square][pj.directions[d]];
                tmp += g_part_value->abs_pd[kpp_index.i][pj.color][lpj.square][pj.directions[d]];
                tmp += g_part_value->abs_kd[kpp_index.king][pj.color][pj.square][pj.directions[d]];
                tmp += g_part_value->rel_kpd_hand[kpp_index.i][pj.color][rlpj.y][rlpj.x][pj.directions[d]];
                tmp += g_part_value->rel_pd_hand[kpp_index.i][pj.color][pj.directions[d]];
                tmp += g_part_value->rel_kd_board[pj.color][rlpj.y][rlpj.x][pj.directions[d]];
              }
            }

            if (!Divide)
            {
              kpp_tmp += static_cast<int32_t>((std::round(static_cast<double>(tmp) / static_cast<double>(pj.direction_num))));
              ++part_num += 6;
            }
          }
          else
          {
            // 両方盤上

            PieceParam pi(kpp_index.i);
            PieceParam pj(kpp_index.j);
            RelativePosition ri(kpp_index.king, pi.square);
            RelativePosition rj(kpp_index.king, pj.square);
            RelativePosition rij(pi.square, pj.square);
            int lower_j = lower_file_kpp_index(kpp_index.j);

            if (king == pi.square || king == pj.square)
              continue;

            if (Divide)
            {
              add_atmic_float(g_part_param->abs_kp[kpp_index.king][kpp_index.i], base_eval_.kpp_raw[king][i][j]);
              add_atmic_float(g_part_param->abs_kp[kpp_index.king][lower_j], base_eval_.kpp_raw[king][i][j]);
            }
            else
            {
              kpp_tmp += g_part_value->abs_kp[kpp_index.king][kpp_index.i];
              ++part_num;
              kpp_tmp += g_part_value->abs_kp[kpp_index.king][lower_j];
              ++part_num;
            }

            RelativePosition ri_kp = ri;
            RelativePosition rj_kp = rj;
            PieceParam pj_kp(lower_j);
            if (ri_kp.x > 8)
              ri_kp.x = 16 - ri_kp.x;
            if (rj_kp.x > 8)
              rj_kp.x = 16 - rj_kp.x;

            if (Divide)
            {
              add_atmic_float(g_part_param->rel_kp_board[pi.piece][ri_kp.y][ri_kp.x], base_eval_.kpp_raw[king][i][j]);
              add_atmic_float(g_part_param->rel_kp_board[pj.piece][rj_kp.y][rj_kp.x], base_eval_.kpp_raw[king][i][j]);
            }
            else
            {
              kpp_tmp += g_part_value->rel_kp_board[pi.piece][ri_kp.y][ri_kp.x];
              ++part_num;
              kpp_tmp += g_part_value->rel_kp_board[pj.piece][rj_kp.y][rj_kp.x];
              ++part_num;
            }

            // iの相対位置を左側にする
            if (ri.x > 8)
            {
              ri.x = 16 - ri.x;
              rj.x = 16 - rj.x;
            }
            if (pi.piece == pj.piece)
            {
              if (rij.x > 8)
                rij.x = 16 - rij.x;
              if (rij.y > 8)
                rij.y = 16 - rij.y;
              if (ri.y > rj.y || (ri.y == rj.y && ri.x > rj.x))
              {
                if (Divide)
                {
                  add_atmic_float
                  (
                    g_part_param->rel_kpp_board[pj.piece][rj.y][rj.x][pi.piece][ri.y][ri.x],
                    base_eval_.kpp_raw[king][i][j]
                  );
                  add_atmic_float
                  (
                    g_part_param->rel_pp_board[pi.piece][pj.piece][rij.y][rij.x],
                    base_eval_.kpp_raw[king][i][j]
                  );
                }
                else
                {
                  kpp_tmp += g_part_value->rel_kpp_board[pj.piece][rj.y][rj.x][pi.piece][ri.y][ri.x];
                  ++part_num;
                  kpp_tmp += g_part_value->rel_pp_board[pi.piece][pj.piece][rij.y][rij.x];
                  ++part_num;
                }
              }
              else
              {
                if (Divide)
                {
                  add_atmic_float
                  (
                    g_part_param->rel_kpp_board[pi.piece][ri.y][ri.x][pj.piece][rj.y][rj.x],
                    base_eval_.kpp_raw[king][i][j]
                  );
                  add_atmic_float
                  (
                    g_part_param->rel_pp_board[pi.piece][pj.piece][rij.y][rij.x],
                    base_eval_.kpp_raw[king][i][j]
                  );
                }
                else
                {
                  kpp_tmp += g_part_value->rel_kpp_board[pi.piece][ri.y][ri.x][pj.piece][rj.y][rj.x];
                  ++part_num;
                  kpp_tmp += g_part_value->rel_pp_board[pi.piece][pj.piece][rij.y][rij.x];
                  ++part_num;
                }
              }
            }
            else
            {
              if (Divide)
              {
                add_atmic_float
                (
                  g_part_param->rel_kpp_board[pi.piece][ri.y][ri.x][pj.piece][rj.y][rj.x],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_pp_board[pi.piece][pj.piece][rij.y][rij.x],
                  base_eval_.kpp_raw[king][i][j]
                );
              }
              else
              {
                kpp_tmp += g_part_value->rel_kpp_board[pi.piece][ri.y][ri.x][pj.piece][rj.y][rj.x];
                ++part_num;
                kpp_tmp += g_part_value->rel_pp_board[pi.piece][pj.piece][rij.y][rij.x];
                ++part_num;
              }
            }

            int32_t tmp = 0;
            for (int d = 0; d < pi.direction_num; ++d)
            {
              if (Divide)
              {
                add_atmic_float
                (
                  g_part_param->abs_kpd[kpp_index.king][kpp_index.j][pi.color][pi.square][pi.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->abs_kd[kpp_index.king][pi.color][pi.square][pi.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_kpd_board[pj.piece][rj.y][rj.x][pi.color][ri.y][ri.x][pi.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_pd_board[pj.piece][pi.color][rij.y][rij.x][pi.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
              }
              else
              {
                tmp += g_part_value->abs_kpd[kpp_index.king][kpp_index.j][pi.color][pi.square][pi.directions[d]];
                tmp += g_part_value->abs_kd[kpp_index.king][pi.color][pi.square][pi.directions[d]];
                tmp += g_part_value->rel_kpd_board[pj.piece][rj.y][rj.x][pi.color][ri.y][ri.x][pi.directions[d]];
                tmp += g_part_value->rel_pd_board[pj.piece][pi.color][rij.y][rij.x][pi.directions[d]];
              }
            }
            if (!Divide)
            {
              kpp_tmp += static_cast<int32_t>(std::round(static_cast<double>(tmp) / static_cast<double>(pi.direction_num)));
              ++part_num;
            }

            tmp = 0;
            for (int d = 0; d < pj.direction_num; ++d)
            {
              if (Divide)
              {
                add_atmic_float
                (
                  g_part_param->abs_kpd[kpp_index.king][kpp_index.i][pj.color][pj.square][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->abs_kd[kpp_index.king][pj_kp.color][pj_kp.square][pj_kp.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_kpd_board[pi.piece][ri.y][ri.x][pj.color][rj.y][rj.x][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
                add_atmic_float
                (
                  g_part_param->rel_pd_board[pi.piece][pj.color][rij.y][rij.x][pj.directions[d]],
                  base_eval_.kpp_raw[king][i][j]
                );
              }
              else
              {
                tmp += g_part_value->abs_kpd[kpp_index.king][kpp_index.i][pj.color][pj.square][pj.directions[d]];
                tmp += g_part_value->abs_kd[kpp_index.king][pj_kp.color][pj_kp.square][pj_kp.directions[d]];
                tmp += g_part_value->rel_kpd_board[pi.piece][ri.y][ri.x][pj.color][rj.y][rj.x][pj.directions[d]];
                tmp += g_part_value->rel_pd_board[pi.piece][pj.color][rij.y][rij.x][pj.directions[d]];
              }
            }

            if (!Divide)
            {
              kpp_tmp += static_cast<int32_t>(std::round(static_cast<double>(tmp) / static_cast<double>(pj.direction_num)));
              ++part_num;
            }


            int p_j = kpp_index.j;
            int p_i = kpp_index.i;
            // PPの計算時、iが5筋にいると、jを左右反転さてても一緒
            if (pi.square % 9 == kFile5)
            {
              p_j = lower_file_kpp_index(kpp_index.j);
              if (p_j < p_i)
                std::swap(p_i, p_j);
            }
            else if (pi.square % 9 > kFile5)
            {
              p_i = inverse_file_kpp_index(kpp_index.i);
              p_j = inverse_file_kpp_index(kpp_index.j);
              if (p_j < p_i)
                std::swap(p_i, p_j);
            }

            if (Divide)
            {
              add_atmic_float(g_part_param->abs_pp[p_i][p_j], base_eval_.kpp_raw[king][i][j]);
            }
            else
            {
              kpp_tmp += g_part_value->abs_pp[p_i][p_j];
              ++part_num;
            }

            PieceParam ppi(p_i);
            PieceParam ppj(p_j);
            tmp = 0;
            for (int d = 0; d < ppi.direction_num; ++d)
            {
              if (Divide)
              {
                add_atmic_float(g_part_param->abs_pd[p_j][ppi.color][ppi.square][ppi.directions[d]], base_eval_.kpp_raw[king][i][j]);
              }
              else
              {
                tmp += g_part_value->abs_pd[p_j][ppi.color][ppi.square][ppi.directions[d]];
              }
            }
            if (!Divide)
            {
              kpp_tmp += static_cast<int32_t>(std::round(static_cast<double>(tmp) / static_cast<double>(ppi.direction_num)));
              ++part_num;
            }

            tmp = 0;
            for (int d = 0; d < ppj.direction_num; ++d)
            {
              if (Divide)
              {
                add_atmic_float(g_part_param->abs_pd[p_i][ppj.color][ppj.square][ppj.directions[d]], base_eval_.kpp_raw[king][i][j]);
              }
              else
              {
                kpp_tmp += static_cast<int32_t>(std::round(static_cast<double>(tmp) / static_cast<double>(ppj.direction_num)));
                ++part_num;
              }
            }
            if (!Divide)
            {
              kpp_tmp += static_cast<int32_t>(std::round(static_cast<double>(tmp) / static_cast<double>(ppj.direction_num)));
              ++part_num;
            }
          }
          if (!Divide)
            Eval::KPP[king][i][j] = static_cast<int16_t>((std::round(static_cast<double>(kpp_tmp) / static_cast<double>(part_num))));
        }
      }
    }

#ifdef _OPENMP
#pragma omp for
#endif
    for (int k1 = k9A; k1 < kBoardSquare; ++k1)
    {
      Square king1 = static_cast<Square>(k1);
      for (int k2 = k9A; k2 < kBoardSquare; ++k2)
      {
        Square king2 = static_cast<Square>(k2);
        if (king1 == king2)
          continue;

        RelativePosition kk(king1, king2);
        for (int i = 1; i < Eval::kFEEnd; ++i)
        {
          int32_t kkp_tmp = 0;
          int kkp_num = 0;
          int32_t tmp = 0;
          int num = 0;

          add_part_kp_kkp_param<Divide>(king1, i, 1, king1, king2, i, tmp, num);
          Square inv_king2 = static_cast<Square>(kBoardSquare - 1 - king2);
          int inv_i = inverse_black_white_kpp_index(i);
          add_part_kp_kkp_param<Divide>(inv_king2, inv_i, -1, king1, king2, i, tmp, num);

          PieceParam pp(i);
          int pi = i;
          KingPosition ksq1(king1);
          BoardPosition ksq2(king2);
          if (ksq1.swap)
          {
            ksq2.x = kFile9 - ksq2.x;
            pi = inverse_file_kpp_index(pi);
          }
          else if (ksq1.x == kFile5 && ksq2.x > kFile5)
          {
            ksq2.x = kFile9 - ksq2.x;
            pi = lower_file_kpp_index(pi);
          }
          else if (ksq1.x == kFile5 && ksq2.x == kFile5)
          {
            pi = lower_file_kpp_index(pi);
          }

          if (Divide)
          {
            add_atmic_float(g_part_param->abs_kkp[ksq1.square()][ksq2.square()][pi], base_eval_.kkp_raw[k1][k2][i]);
            add_atmic_float(g_part_param->abs_kk[ksq1.square()][ksq2.square()], base_eval_.kkp_raw[k1][k2][i]);
          }
          else
          {
            kkp_tmp += g_part_value->abs_kkp[ksq1.square()][ksq2.square()][pi];
            ++kkp_num;
            kkp_tmp += g_part_value->abs_kk[ksq1.square()][ksq2.square()];
            ++kkp_num;
          }

          RelativePosition rk2(ksq1.square(), ksq2.square());
          pp = PieceParam(pi);
          if (pi < Eval::kFEHandEnd)
          {
            if (rk2.x > 8)
              rk2.x = 16 - rk2.x;

            if (Divide)
            {
              add_atmic_float(g_part_param->rel_kkp_hand[rk2.y][rk2.x][pi], base_eval_.kkp_raw[k1][k2][i]);
            }
            else
            {
              kkp_tmp += g_part_value->rel_kkp_hand[rk2.y][rk2.x][pi];
              ++kkp_num;
            }
          }
          else
          {
            RelativePosition rpi(ksq1.square(), pp.square);

            if (rk2.x > 8)
            {
              rk2.x = 16 - rk2.x;
              rpi.x = 16 - rpi.x;
            }
            else if (rk2.x == 8 && rpi.x > 8)
            {
              rpi.x = 16 - rpi.x;
            }

            if (Divide)
            {
              add_atmic_float(g_part_param->rel_kkp_board[rk2.y][rk2.x][pp.piece][rpi.y][rpi.x], base_eval_.kkp_raw[k1][k2][i]);
            }
            else
            {
              kkp_tmp += g_part_value->rel_kkp_board[rk2.y][rk2.x][pp.piece][rpi.y][rpi.x];
              ++kkp_num;
            }

            tmp = 0;
            for (int d = 0; d < pp.direction_num; ++d)
            {
              if (Divide)
              {
                add_atmic_float(g_part_param->abs_kkd[ksq1.square()][ksq2.square()][pp.square][pp.directions[d]], base_eval_.kkp_raw[k1][k2][i]);
                add_atmic_float(g_part_param->rel_kkd_board[rk2.y][rk2.x][pp.color][rpi.y][rpi.x][pp.directions[d]], base_eval_.kkp_raw[k1][k2][i]);
              }
              else
              {
                tmp += g_part_value->abs_kkd[ksq1.square()][ksq2.square()][pp.square][pp.directions[d]];
                tmp += g_part_value->rel_kkd_board[rk2.y][rk2.x][pp.color][rpi.y][rpi.x][pp.directions[d]];
              }
            }
            kkp_tmp += static_cast<int32_t>(std::round(static_cast<double>(tmp) / static_cast<double>(pp.direction_num)));
            ++kkp_num;
          }
          
          if (!Divide)
          {
            Eval::KKPT[k1][k2][i][0] = static_cast<int16_t>((std::round(static_cast<double>(kkp_tmp) / static_cast<double>(kkp_num))));
            Eval::KKPT[k1][k2][i][1] = static_cast<int16_t>((std::round(static_cast<double>(kkp_tmp) / static_cast<double>(kkp_num))));
          }
        }
      }
    }
  }
}            

template<bool Divide>
void
Learner::add_part_kp_kkp_param(Square king, int kp_i, int sign, Square king1, Square king2, int i, int32_t &ret_value, int &ret_num)
{
  KingPosition kp(king);
  if (kp.swap)
    kp_i = inverse_file_kpp_index(kp_i);
  else if (kp.x == kFile5)
    kp_i = lower_file_kpp_index(kp_i);

  int32_t value = 0;
  int num = 0;
  
  if (Divide)
  {
    add_atmic_float(g_part_param->abs_kp[kp.lower_square()][kp_i], sign * base_eval_.kkp_raw[king1][king2][i]);
  }
  else
  {
    value += sign * g_part_value->abs_kp[kp.lower_square()][kp_i];
    ++num;
  }

  if (i >= Eval::kFEHandEnd)
  {
    PieceParam pi(kp_i);
    RelativePosition ri(kp.square(), pi.square);

    if (Divide)
    {
      add_atmic_float(g_part_param->rel_kp_board[pi.piece][ri.y][ri.x], sign * base_eval_.kkp_raw[king1][king2][i]);
    }
    else
    {
      value += sign * g_part_value->rel_kp_board[pi.piece][ri.y][ri.x];
      ++num;
    }

    int32_t tmp = 0;
    for (int d = 0; d < pi.direction_num; ++d)
    {
      if (Divide)
      {
        add_atmic_float(g_part_param->abs_kd[pi.color][ri.y][ri.x][pi.directions[d]], sign * base_eval_.kkp_raw[king1][king2][i]);
        add_atmic_float(g_part_param->rel_kd_board[pi.color][ri.y][ri.x][pi.directions[d]], sign * base_eval_.kkp_raw[king1][king2][i]);
      }
      else
      {
        tmp += sign * g_part_value->abs_kd[pi.color][ri.y][ri.x][pi.directions[d]];
        tmp += sign * g_part_value->rel_kd_board[pi.color][ri.y][ri.x][pi.directions[d]];
      }
    }
    value += static_cast<int32_t>((std::round(static_cast<double>(tmp) / static_cast<double>(pi.direction_num))));
    ++num;
  }
  ret_value = value;
  ret_num = num;
}

Value
Learner::search_pv(Position &pos, Move record_move, Depth depth)
{
  SearchStack stack[kMaxPly + 4];
  SearchStack *ss = stack + 2;
  Value best_value;
  Move pv[kMaxPly + 1];

  std::memset(ss - 2, 0, 5 * sizeof(SearchStack));

  StateInfo new_info;
  Thread *thread = pos.this_thread();
  thread->root_moves_.clear();
  thread->root_moves_.push_back(Search::RootMove(record_move));

  thread->history_.clear();
  thread->counter_moves_.clear();
  thread->counter_move_history_.clear();
  thread->from_to_.clear();
  thread->pv_index_ = 0;
  thread->calls_count_ = 0;
  thread->max_ply_ = 0;
  thread->root_depth_ = kDepthZero;

  Search::RootMove &root_move = thread->root_moves_[0];

  ss->pv = pv;
  ss->pv[0] = kMoveNone;
  (ss - 1)->ply = 1;
  
  pos.do_move(record_move, new_info);
  best_value = -Search::search(pos, ss, -kValueMaxEvaluate, kValueMaxEvaluate, depth);
  pos.undo_move(record_move);

  root_move.score = best_value;
  root_move.pv.resize(1);
  for (Move *m = ss->pv; *m != kMoveNone; ++m)
    root_move.pv.push_back(*m);

  return best_value;
}

void
Learner::search_other_pv
(
  Position &pos,
  Value record_value,
  Depth depth,
  MoveList<kLegal> &legal_moves,
  Move record_move
)
{
  SearchStack stack[kMaxPly + 4];
  SearchStack *ss = stack + 2;

  std::memset(ss - 2, 0, 5 * sizeof(SearchStack));

  Thread *thread = pos.this_thread();

  thread->history_.clear();
  thread->counter_moves_.clear();
  thread->counter_move_history_.clear();
  thread->from_to_.clear();
  thread->pv_index_ = 0;
  thread->calls_count_ = 0;
  thread->max_ply_ = 0;
  thread->root_depth_ = kDepthZero;

  thread->root_moves_.clear();
  for (auto &m : legal_moves)
  {
    if (m.move != record_move)
      thread->root_moves_.push_back(Search::RootMove(m.move));
  }

  for (size_t i = 0; i < 10 && i < thread->root_moves_.size(); i++)
  {
    thread->pv_index_ = i;
    Search::search
    (
      pos,
      ss,
      record_value - kFvWindow,
      record_value + kFvWindow,
      depth + 1
    );
    std::stable_sort(thread->root_moves_.begin() + i, thread->root_moves_.end());
  }
  std::stable_sort(thread->root_moves_.begin(), thread->root_moves_.end());
}

#endif
