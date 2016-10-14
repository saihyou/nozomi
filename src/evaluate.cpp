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

#include <algorithm>
#include <cassert>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <cstring>

#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "misc.h"

namespace Eval
{
int16_t KPP[kBoardSquare][kFEEnd][kFEEnd];
int16_t KKP[kBoardSquare][kBoardSquare][kFEEnd];
int16_t KKPT[kBoardSquare][kBoardSquare][kFEEnd][kNumberOfColor];

Value
calc_full(const Position &pos, SearchStack *ss)
{
  int *list_black = pos.black_kpp_list();
  int *list_white = pos.white_kpp_list();

  int score = 0;

  Square sq_black_king = pos.square_king(kBlack);
  Square sq_white_king = pos.square_king(kWhite);
  Square inv_sq_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp = 0;
  int white_kpp = 0;
  int kkp  = KKP[sq_black_king][sq_white_king][list_black[0]];
  int kkpt = KKPT[sq_black_king][sq_white_king][list_black[0]][side_to_move];
  for (int i = 1; i < kListNum; ++i)
  {
    int k0 = list_black[i];
    int k1 = list_white[i];
    for (int j = 0; j < i; ++j)
    {
      int l0 = list_black[j];
      int l1 = list_white[j];
      black_kpp += KPP[sq_black_king][k0][l0];
      white_kpp -= KPP[inv_sq_white_king][k1][l1];
    }
    kkp  += KKP[sq_black_king][sq_white_king][k0];
    kkpt += KKPT[sq_black_king][sq_white_king][k0][side_to_move];
  }

  ss->black_kpp = static_cast<Value>(black_kpp);
  ss->white_kpp = static_cast<Value>(white_kpp);
  ss->kkp       = static_cast<Value>(kkp);
  ss->kkpt      = static_cast<Value>(kkpt);
  ss->material  = static_cast<Value>(pos.material() * kFvScale);
  score = ss->black_kpp + ss->white_kpp + ss->kkp + ss->material + ss->kkpt;

  return static_cast<Value>(score);
}

void
calc_no_capture_difference(const Position &pos, SearchStack *ss)
{
  Square    black_king        = pos.square_king(kBlack);
  Square    white_king        = pos.square_king(kWhite);
  Square    inv_white_king    = inverse(white_king);

  const int *prev_list_black   = pos.prev_black_kpp_list();
  const int *prev_list_white   = pos.prev_white_kpp_list();
  const int *list_black        = pos.black_kpp_list();
  const int *list_white        = pos.white_kpp_list();

  assert(pos.list_index_move() < 38);

  Color side_to_move = pos.side_to_move();
  int black_kpp_diff = 0;
  int white_kpp_diff = 0;
  int kkpt = 0;
  const auto *black_prev_kpp_table = KPP[black_king][prev_list_black[pos.list_index_move()]];
  const auto *black_kpp_table      = KPP[black_king][list_black[pos.list_index_move()]];
  const auto *white_prev_kpp_table = KPP[inv_white_king][prev_list_white[pos.list_index_move()]];
  const auto *white_kpp_table      = KPP[inv_white_king][list_white[pos.list_index_move()]];
  for (int i = 0; i < kListNum; ++i)
  {
    // 前回のを引く
    black_kpp_diff -= black_prev_kpp_table[prev_list_black[i]];
    // 今回のを足す
    black_kpp_diff += black_kpp_table[list_black[i]];

    // 前回のを引く
    white_kpp_diff += white_prev_kpp_table[prev_list_white[i]];
    // 今回のを足す
    white_kpp_diff -= white_kpp_table[list_white[i]];

    kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
  }
  // 前回のを引く
  int kkp_diff = -KKP[black_king][white_king][prev_list_black[pos.list_index_move()]];
  // 今回のを足す
  kkp_diff += KKP[black_king][white_king][list_black[pos.list_index_move()]];

  ss->black_kpp = (ss - 1)->black_kpp + black_kpp_diff;
  ss->white_kpp = (ss - 1)->white_kpp + white_kpp_diff;
  ss->kkp       = (ss - 1)->kkp + kkp_diff;
  ss->kkpt      = static_cast<Value>(kkpt);
  ss->material  = static_cast<Value>(pos.material() * kFvScale);
}

void
calc_difference_capture(const Position &pos, SearchStack *ss)
{
  Square    black_king        = pos.square_king(kBlack);
  Square    white_king        = pos.square_king(kWhite);
  Square    inv_white_king    = inverse(white_king);

  const int *prev_list_black   = pos.prev_black_kpp_list();
  const int *prev_list_white   = pos.prev_white_kpp_list();
  const int *list_black        = pos.black_kpp_list();
  const int *list_white        = pos.white_kpp_list();

  assert(pos.list_index_capture() < 38);
  assert(pos.list_index_move() < 38);

  Color side_to_move = pos.side_to_move();
  int black_kpp_diff = 0;
  int white_kpp_diff = 0;
  int kkpt = 0;
  const auto *black_prev_kpp_table     = KPP[black_king][prev_list_black[pos.list_index_move()]];
  const auto *black_prev_cap_kpp_table = KPP[black_king][prev_list_black[pos.list_index_capture()]];
  const auto *black_kpp_table          = KPP[black_king][list_black[pos.list_index_move()]];
  const auto *black_cap_kpp_table      = KPP[black_king][list_black[pos.list_index_capture()]];
  const auto *white_prev_kpp_table     = KPP[inv_white_king][prev_list_white[pos.list_index_move()]];
  const auto *white_prev_cap_kpp_table = KPP[inv_white_king][prev_list_white[pos.list_index_capture()]];
  const auto *white_kpp_table          = KPP[inv_white_king][list_white[pos.list_index_move()]];
  const auto *white_cap_kpp_table      = KPP[inv_white_king][list_white[pos.list_index_capture()]];

  for (int i = 0; i < kListNum; ++i)
  {
    // 前回のを引く
    black_kpp_diff -= black_prev_kpp_table[prev_list_black[i]];
    // とった分も引く
    black_kpp_diff -= black_prev_cap_kpp_table[prev_list_black[i]];
    // 今回のを足す
    black_kpp_diff += black_kpp_table[list_black[i]];
    black_kpp_diff += black_cap_kpp_table[list_black[i]];

    // 前回のを引く
    white_kpp_diff += white_prev_kpp_table[prev_list_white[i]];
    // とった分も引く
    white_kpp_diff += white_prev_cap_kpp_table[prev_list_white[i]];

    // 今回のを足す
    white_kpp_diff -= white_kpp_table[list_white[i]];
    white_kpp_diff -= white_cap_kpp_table[list_white[i]];

    kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
  }
  // 前回ので引きすぎたのを足す
  black_kpp_diff += black_prev_kpp_table[prev_list_black[pos.list_index_capture()]];
  // 今回ので足しすぎたのを引く
  black_kpp_diff -= black_kpp_table[list_black[pos.list_index_capture()]];

  // 前回ので引きすぎたのを足す
  white_kpp_diff -= white_prev_kpp_table[prev_list_white[pos.list_index_capture()]];
  // 今回ので足しすぎたのを引く
  white_kpp_diff += white_kpp_table[list_white[pos.list_index_capture()]];

  int kkp_diff = -KKP[black_king][white_king][prev_list_black[pos.list_index_move()]];
  kkp_diff -= KKP[black_king][white_king][prev_list_black[pos.list_index_capture()]];
  kkp_diff += KKP[black_king][white_king][list_black[pos.list_index_move()]];
  kkp_diff += KKP[black_king][white_king][list_black[pos.list_index_capture()]];

  ss->black_kpp = (ss - 1)->black_kpp + black_kpp_diff;
  ss->white_kpp = (ss - 1)->white_kpp + white_kpp_diff;
  ss->kkp       = (ss - 1)->kkp + kkp_diff;
  ss->kkpt      = static_cast<Value>(kkpt);
  ss->material  = static_cast<Value>(pos.material() * kFvScale);
}

template<Color kColor>
void
calc_difference_king_move_no_capture(const Position &pos, SearchStack *ss)
{
  const int *list_black = pos.black_kpp_list();
  const int *list_white = pos.white_kpp_list();
  const Square sq_black_king = pos.square_king(kBlack);
  const Square sq_white_king = pos.square_king(kWhite);
  Square inv_sq_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp = 0;
  int white_kpp = 0;
  const auto *kkp_table = KKP[sq_black_king][sq_white_king];
  int kkp  = kkp_table[list_black[0]];
  int kkpt = KKPT[sq_black_king][sq_white_king][list_black[0]][side_to_move];
  if (kColor == kBlack)
  {
    const auto *black_kpp_table = KPP[sq_black_king];
    for (int i = 1; i < kListNum; ++i)
    {
      const int k0 = list_black[i];
      const auto *black_pp_table = black_kpp_table[k0];
      for (int j = 0; j < i; ++j)
      {
        const int l0 = list_black[j];
        black_kpp += black_pp_table[l0];
      }
      kkp  += kkp_table[k0];
      kkpt += KKPT[sq_black_king][sq_white_king][list_black[i]][side_to_move];
    }
    ss->black_kpp = static_cast<Value>(black_kpp);
    ss->white_kpp = (ss - 1)->white_kpp;
  }
  else
  {
    const auto *white_kpp_table = KPP[inv_sq_white_king];
    for (int i = 1; i < kListNum; ++i)
    {
      const int k1 = list_white[i];
      const auto *white_pp_table = white_kpp_table[k1];
      for (int j = 0; j < i; ++j)
      {
        const int l1 = list_white[j];
        white_kpp -= white_pp_table[l1];
      }
      kkp  += kkp_table[list_black[i]];
      kkpt += KKPT[sq_black_king][sq_white_king][list_black[i]][side_to_move];
    }
    ss->black_kpp = (ss - 1)->black_kpp;
    ss->white_kpp = static_cast<Value>(white_kpp);
  }
  ss->kkp      = static_cast<Value>(kkp);
  ss->kkpt     = static_cast<Value>(kkpt);
  ss->material = static_cast<Value>(pos.material() * kFvScale);
}

void
calc_difference(const Position &pos, Move last_move, SearchStack *ss)
{
  const Square    from = move_from(last_move);
  const PieceType type = move_piece_type(last_move);

  if (type == kKing)
  {
    if (pos.side_to_move() == kBlack)
      calc_difference_king_move_no_capture<kWhite>(pos, ss);
    else
      calc_difference_king_move_no_capture<kBlack>(pos, ss);
  }
  else
  {
    if (from >= kBoardSquare)
    {
      calc_no_capture_difference(pos, ss);
    }
    else
    {
      const PieceType capture = move_capture(last_move);

      if (capture == kPieceNone)
        calc_no_capture_difference(pos, ss);
      else
        calc_difference_capture(pos, ss);
    }
  }
}

Value
calc_kkpt_value(const Position &pos)
{
  int score = 0;
  Square black_king = pos.square_king(kBlack);
  Square white_king = pos.square_king(kWhite);
  Color color = pos.side_to_move();
  int *list_black = pos.black_kpp_list();

  for (int i = 0; i < kListNum; ++i)
    score += KKPT[black_king][white_king][list_black[i]][color];
  return static_cast<Value>(score);
}

Value
evaluate(const Position &pos, SearchStack *ss)
{
  Value score;

  Move last_move = (ss - 1)->current_move;
  if ((ss - 1)->evaluated && (ss - 1)->current_move == kMoveNull)
  {
    
    ss->black_kpp = (ss - 1)->black_kpp;
    ss->white_kpp = (ss - 1)->white_kpp;
    ss->kkp       = (ss - 1)->kkp;
    ss->kkpt      = calc_kkpt_value(pos);
    ss->material  = (ss - 1)->material;
    score = ss->black_kpp + ss->white_kpp + ss->kkp + ss->material + ss->kkpt;
  }
  else if ((ss - 1)->evaluated && !(move_piece_type(last_move) == kKing && move_is_capture(last_move)))
  {
    calc_difference(pos, last_move, ss);
    score = ss->black_kpp + ss->white_kpp + ss->kkp + ss->material + ss->kkpt;
  }
  else
  {
    score = calc_full(pos, ss);
  }
  ss->evaluated = true;
  score = pos.side_to_move() == kWhite ? -score : score;
  score /= kFvScale;

  assert(score > -kValueInfinite && score < kValueInfinite);

  return score + kTempo;
}
  
bool
init() 
{
  std::ifstream ifs("kpp_kkp_kkpt.bin", std::ios::in | std::ios::binary);
  if (!ifs)
  {
    std::memset(KPP, 0, sizeof(KPP));
    std::memset(KKP, 0, sizeof(KKP));
    std::memset(KKPT, 0, sizeof(KKPT));
    return false;
  }
  ifs.read(reinterpret_cast<char *>(KPP), sizeof(KPP));
  ifs.read(reinterpret_cast<char *>(KKP), sizeof(KKP));
  ifs.read(reinterpret_cast<char *>(KKPT), sizeof(KKPT));
  ifs.close();

  return true;
}
} // namespace Eval
