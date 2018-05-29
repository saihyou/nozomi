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
#include "thread.h"
#include "search.h"
#include "misc.h"

#define USE_SIMD

namespace Eval
{
int16_t KPP[kBoardSquare][kFEEnd][kFEEnd];
int16_t KKPT[kBoardSquare][kBoardSquare][kFEEnd][kNumberOfColor];

constexpr int
kKingBrotherDiffSize = 7;

void
calc_full(const Position &pos, EvalParts &parts)
{
  KPPIndex *list_black = pos.black_kpp_list();
  KPPIndex *list_white = pos.white_kpp_list();

  Square sq_black_king = pos.square_king(kBlack);
  Square sq_white_king = pos.square_king(kWhite);
  Square inv_sq_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp = 0;
  int white_kpp = 0;
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
    kkpt += KKPT[sq_black_king][sq_white_king][k0][side_to_move];
  }

  parts.black_kpp = static_cast<Value>(black_kpp);
  parts.white_kpp = static_cast<Value>(white_kpp);
  parts.kkpt      = static_cast<Value>(kkpt);
}

Value
calc_part(Square king, const KPPIndex *current, const KPPIndex *before, const int *index, int num)
{
  int value = 0;
  for (int i = 0; i < num; ++i)
  {
    const auto kpp_current = KPP[king][current[index[i]]];
    const auto kpp_before  = KPP[king][before[index[i]]];
    for (int j = 0; j < kListNum; ++j)
    {
      value += kpp_current[current[j]];
      value -= kpp_before[before[j]];
    }
  }

  for (int i = 1; i < num; ++i)
  {
    const auto kpp_current = KPP[king][current[index[i]]];
    const auto kpp_before = KPP[king][before[index[i]]];
    for (int j = 0; j < i; ++j)
    {
      value -= kpp_current[current[index[j]]];
      value += kpp_before[before[index[j]]];
    }
  }
  return Value(value);
}

void
calc_no_capture_difference(const Position &pos, const EvalParts &last_parts, EvalParts &parts)
{
  Square    black_king        = pos.square_king(kBlack);
  Square    white_king        = pos.square_king(kWhite);
  Square    inv_white_king    = inverse(white_king);

  const KPPIndex *prev_list_black   = pos.prev_black_kpp_list();
  const KPPIndex *prev_list_white   = pos.prev_white_kpp_list();
  const KPPIndex *list_black        = pos.black_kpp_list();
  const KPPIndex *list_white        = pos.white_kpp_list();

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

  parts.black_kpp = last_parts.black_kpp + black_kpp_diff;
  parts.white_kpp = last_parts.white_kpp + white_kpp_diff;
  parts.kkpt      = static_cast<Value>(kkpt);
}

void
calc_difference_capture(const Position &pos, const EvalParts &last_parts, EvalParts &parts)
{
  Square    black_king        = pos.square_king(kBlack);
  Square    white_king        = pos.square_king(kWhite);
  Square    inv_white_king    = inverse(white_king);

  const KPPIndex *prev_list_black   = pos.prev_black_kpp_list();
  const KPPIndex *prev_list_white   = pos.prev_white_kpp_list();
  const KPPIndex *list_black        = pos.black_kpp_list();
  const KPPIndex *list_white        = pos.white_kpp_list();

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

  parts.black_kpp = last_parts.black_kpp + black_kpp_diff;
  parts.white_kpp = last_parts.white_kpp + white_kpp_diff;
  parts.kkpt      = static_cast<Value>(kkpt);
}

template<Color kColor>
void
calc_difference_king_move_capture(const Position &pos, const EvalParts &last_parts, EvalParts &parts)
{
  const KPPIndex *list_black = pos.black_kpp_list();
  const KPPIndex *list_white = pos.white_kpp_list();
  const Square black_king = pos.square_king(kBlack);
  const Square white_king = pos.square_king(kWhite);
  Square inv_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp = 0;
  int white_kpp = 0;
  int kkpt = KKPT[black_king][white_king][list_black[0]][side_to_move];
  if (kColor == kBlack)
  {
    const auto *black_kpp_table = KPP[black_king];
    const KPPIndex *prev_list_white = pos.prev_white_kpp_list();
    const KPPIndex *list_white = pos.white_kpp_list();

    const auto *white_prev_cap_kpp_table = KPP[inv_white_king][prev_list_white[pos.list_index_capture()]];
    const auto *white_cap_kpp_table = KPP[inv_white_king][list_white[pos.list_index_capture()]];
    int white_kpp_diff = 0;

    // とった分も引く
    white_kpp_diff += white_prev_cap_kpp_table[prev_list_white[0]];
    // 今回のを足す
    white_kpp_diff -= white_cap_kpp_table[list_white[0]];
    for (int i = 1; i < kListNum; ++i)
    {
      const int k0 = list_black[i];
      const auto *black_pp_table = black_kpp_table[k0];
      for (int j = 0; j < i; ++j)
      {
        const int l0 = list_black[j];
        black_kpp += black_pp_table[l0];
      }
      // とった分も引く
      white_kpp_diff += white_prev_cap_kpp_table[prev_list_white[i]];
      // 今回のを足す
      white_kpp_diff -= white_cap_kpp_table[list_white[i]];

      kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
    }

    parts.black_kpp = static_cast<Value>(black_kpp);
    parts.white_kpp = last_parts.white_kpp + white_kpp_diff;
  }
  else
  {
    const KPPIndex *prev_list_black = pos.prev_black_kpp_list();
    const KPPIndex *list_black = pos.black_kpp_list();

    const auto *black_prev_cap_kpp_table = KPP[black_king][prev_list_black[pos.list_index_capture()]];
    const auto *black_cap_kpp_table = KPP[black_king][list_black[pos.list_index_capture()]];
    const auto *white_kpp_table = KPP[inv_white_king];
    int black_kpp_diff = 0;
    // とった分も引く
    black_kpp_diff -= black_prev_cap_kpp_table[prev_list_black[0]];
    // 今回のを足す
    black_kpp_diff += black_cap_kpp_table[list_black[0]];
    for (int i = 1; i < kListNum; ++i)
    {
      const int k1 = list_white[i];
      const auto *white_pp_table = white_kpp_table[k1];
      for (int j = 0; j < i; ++j)
      {
        const int l1 = list_white[j];
        white_kpp -= white_pp_table[l1];
      }
      // とった分も引く
      black_kpp_diff -= black_prev_cap_kpp_table[prev_list_black[i]];
      // 今回のを足す
      black_kpp_diff += black_cap_kpp_table[list_black[i]];

      kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
    }

    parts.black_kpp = last_parts.black_kpp + black_kpp_diff;
    parts.white_kpp = static_cast<Value>(white_kpp);
  }
  parts.kkpt     = static_cast<Value>(kkpt);
}

template<Color kColor>
void
calc_difference_king_move_no_capture(const Position &pos, const EvalParts &last_parts, EvalParts &parts)
{
  const KPPIndex *list_black = pos.black_kpp_list();
  const KPPIndex *list_white = pos.white_kpp_list();
  const Square sq_black_king = pos.square_king(kBlack);
  const Square sq_white_king = pos.square_king(kWhite);
  Square inv_sq_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp = 0;
  int white_kpp = 0;
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
      kkpt += KKPT[sq_black_king][sq_white_king][list_black[i]][side_to_move];
    }
    parts.black_kpp = static_cast<Value>(black_kpp);
    parts.white_kpp = last_parts.white_kpp;
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
      kkpt += KKPT[sq_black_king][sq_white_king][list_black[i]][side_to_move];
    }
    parts.black_kpp = last_parts.black_kpp;
    parts.white_kpp = static_cast<Value>(white_kpp);
  }
  parts.kkpt = static_cast<Value>(kkpt);
}

#if defined (_MSC_VER)
uint32_t
first_one(uint64_t b)
{
  unsigned long index = 0;

  _BitScanForward64(&index, b);
  return index;
}
#else
uint32_t
first_one(uint64_t b)
{
  return __builtin_ctzll(b);
}
#endif


void
calc_difference(const Position &pos, Move last_move, const EvalParts &last_parts, EvalParts &parts)
{
  const Square    from = move_from(last_move);
  const PieceType type = move_piece_type(last_move);

  if (type == kKing)
  {
    Color enemy = ~pos.side_to_move();
    Square king = pos.square_king(enemy);
    auto list = (pos.side_to_move() == kBlack) ? pos.white_kpp_list() : pos.black_kpp_list();
    auto entry = pos.this_thread()->king_cache_.get_list(enemy, king);
    auto cache_value = pos.this_thread()->king_cache_.get_value(enemy, king);
    int count = 0;
    int index[kKingBrotherDiffSize];
#ifdef USE_SIMD
    if (cache_value != kValueZero)
    {
      // listの一致しない箇所のbitを立てる
      uint64_t bit = 0;
      for (int i = 0; i < 2; ++i)
      {
        __m256i current = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(list + i * 16));
        __m256i old = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(entry + i * 16));
        // 現在と保存してあるlistを比較する
        // 一致する箇所は全て1となり一致しない箇所は全て0で埋まる
        __m256i cmp = _mm256_cmpeq_epi16(current, old);
        // __m256iを__m128iに分ける
        __m128i ext0 = _mm256_extracti128_si256(cmp, 0);
        __m128i ext1 = _mm256_extracti128_si256(cmp, 1);
        // 16bit -> 8bitに変換する
        __m128i pack = _mm_packs_epi16(ext0, ext1);
        // 一致しない箇所を1にするためnotをとる
        pack = _mm_andnot_si128(pack, _mm_set1_epi8(-1));
        // 1bitだけあればよいので最上位bitを抽出する
        bit |= (static_cast<uint64_t>(_mm_movemask_epi8(pack)) << i * 16);
      }
      {
        __m128i current = _mm_set_epi16(0, 0, list[37],list[36], list[35] ,list[34] ,list[33] ,list[32]);
        __m128i old = _mm_set_epi16(0, 0, entry[37], entry[36], entry[35], entry[34], entry[33], entry[32]);
        __m128i cmp = _mm_cmpeq_epi16(current, old);
        __m128i pack = _mm_packs_epi16(cmp, _mm_set1_epi8(-1));
        pack = _mm_andnot_si128(pack, _mm_set1_epi8(-1));
        bit |= (static_cast<uint64_t>(_mm_movemask_epi8(pack)) << 32);
      }
      if (_mm_popcnt_u64(bit) < kKingBrotherDiffSize)
      {
        while (bit != 0)
        {
          uint32_t p = first_one(bit);
          index[count++] = p;
          bit ^= (1LLU << p);
        }
      }
      else
      {
        cache_value = kValueZero;
      }
    }
#else
    if (cache_value != kValueZero)
    {
      for (int i = 0; i < kListNum; ++i)
      {
        if (entry[i] != list[i])
        {
          if (count == kKingBrotherDiffSize)
          {
            cache_value = kValueZero;
            break;
          }
          index[count++] = i;
        }
      }
    }
#endif
    if (move_capture(last_move) == kPieceNone)
    {
      if (cache_value == kValueZero)
      {
        if (pos.side_to_move() == kBlack)
          calc_difference_king_move_no_capture<kWhite>(pos, last_parts, parts);
        else
          calc_difference_king_move_no_capture<kBlack>(pos, last_parts, parts);
    }
    else
    {
        if (enemy == kBlack)
        {
          parts.black_kpp = cache_value;
          if (count != 0)
            parts.black_kpp += calc_part(king, pos.black_kpp_list(), entry, index, count);
          parts.white_kpp = last_parts.white_kpp;
          parts.kkpt      = calc_kkpt_value(pos);
        }
        else
        {
          parts.black_kpp = last_parts.black_kpp;
          parts.white_kpp = cache_value;
          if (count != 0)
            parts.white_kpp -= calc_part(inverse(king), pos.white_kpp_list(), entry, index, count);
          parts.kkpt = calc_kkpt_value(pos);
        }
      }
    }
    else
    {
      if (cache_value == kValueZero)
      {
        if (pos.side_to_move() == kBlack)
          calc_difference_king_move_capture<kWhite>(pos, last_parts, parts);
        else
          calc_difference_king_move_capture<kBlack>(pos, last_parts, parts);
      }
      else
      {
        if (enemy == kBlack)
        {
          parts.black_kpp = cache_value;
          if (count != 0)
            parts.black_kpp += calc_part(king, pos.black_kpp_list(), entry, index, count);
          int cap = pos.list_index_capture();
          parts.white_kpp = last_parts.white_kpp - calc_part(inverse(pos.square_king(kWhite)), pos.white_kpp_list(), pos.prev_white_kpp_list(), &cap, 1);
          parts.kkpt = calc_kkpt_value(pos);
        }
        else
        {
          int cap = pos.list_index_capture();
          parts.black_kpp = last_parts.black_kpp + calc_part(pos.square_king(kBlack), pos.black_kpp_list(), pos.prev_black_kpp_list(), &cap, 1);
          parts.white_kpp = cache_value;
          if (count != 0)
            parts.white_kpp -= calc_part(inverse(king), pos.white_kpp_list(), entry, index, count);
          parts.kkpt = calc_kkpt_value(pos);
        }
      }
    }

    // 差分がない場合はコピーしないようにする
    // ただほとんどないので意味がないかも
    if (cache_value == kValueZero)
    {
      pos.this_thread()->king_cache_.set_list(enemy, king, list);
      auto v = (pos.side_to_move() == kBlack) ? parts.white_kpp : parts.black_kpp;
      pos.this_thread()->king_cache_.set_value(enemy, king, v);
    }
  }
  else
  {
    if (from >= kBoardSquare)
    {
      calc_no_capture_difference(pos, last_parts, parts);
    }
    else
    {
      const PieceType capture = move_capture(last_move);

      if (capture == kPieceNone)
        calc_no_capture_difference(pos, last_parts, parts);
      else
        calc_difference_capture(pos, last_parts, parts);
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
  KPPIndex *list_black = pos.black_kpp_list();

  for (int i = 0; i < kListNum; ++i)
    score += KKPT[black_king][white_king][list_black[i]][color];
  return static_cast<Value>(score);
}

Value
evaluate(const Position &pos, SearchStack *ss)
{
  Value score;
  Entry *e = pos.this_thread()->eval_hash_[pos.key()];
  if (e->key == pos.key())
  {
    ss->eval_parts = e->parts;
  }
  else
  {

    Move last_move = (ss - 1)->current_move;
    if ((ss - 1)->evaluated)
    {
      if ((ss - 1)->current_move == kMoveNull)
      {
        ss->eval_parts.black_kpp = (ss - 1)->eval_parts.black_kpp;
        ss->eval_parts.white_kpp = (ss - 1)->eval_parts.white_kpp;
        ss->eval_parts.kkpt = calc_kkpt_value(pos);
        ss->material = (ss - 1)->material;
      }
      else
      {
        calc_difference(pos, last_move, (ss - 1)->eval_parts, ss->eval_parts);
      }
    }
    else
    {
      calc_full(pos, ss->eval_parts);
    }
    e->key = pos.key();
    e->parts = ss->eval_parts;
  }

  ss->evaluated = true;
  ss->material  = static_cast<Value>(pos.material() * kFvScale);

  score = ss->eval_parts.black_kpp + ss->eval_parts.white_kpp + ss->material + ss->eval_parts.kkpt;
  score = pos.side_to_move() == kWhite ? -score : score;
  score /= kFvScale;

  assert(score > -kValueInfinite && score < kValueInfinite);
#ifdef LEARN
  return score;
#else
  return score + 2 * ss->ply;
#endif
}
  
bool
init() 
{
  std::ifstream ifs("kpp_kkpt.bin", std::ios::in | std::ios::binary);
  if (!ifs)
  {
    std::memset(KPP, 0, sizeof(KPP));
    std::memset(KKPT, 0, sizeof(KKPT));
    return false;
  }
  ifs.read(reinterpret_cast<char *>(KPP), sizeof(KPP));
  ifs.read(reinterpret_cast<char *>(KKPT), sizeof(KKPT));
  ifs.close();

  return true;
}
} // namespace Eval
