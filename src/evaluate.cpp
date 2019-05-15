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

#include "evaluate.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"

#define USE_SIMD
#define USE_SIMD_2

namespace Eval {
enum Turn { kUs, kThem };

int16_t KPPT[kBoardSquare][kFEEnd][kFEEnd][kNumberOfColor];
int16_t KKPT[kBoardSquare][kBoardSquare][kFEEnd][kNumberOfColor];

constexpr int kKingBrotherDiffSize = 7;

void CalcFull(const Position &pos, EvalParts &parts) {
  KPPIndex *list_black = pos.black_kpp_list();
  KPPIndex *list_white = pos.white_kpp_list();

  Square sq_black_king = pos.square_king(kBlack);
  Square sq_white_king = pos.square_king(kWhite);
  Square inv_sq_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp[kNumberOfColor] = {};
  int white_kpp[kNumberOfColor] = {};
  int kkpt = KKPT[sq_black_king][sq_white_king][list_black[0]][side_to_move];
#ifdef USE_SIMD_2
  __m128i total = _mm_setzero_si128();
#endif
  for (int i = 1; i < kListNum; ++i) {
    int k0 = list_black[i];
    int k1 = list_white[i];
    for (int j = 0; j < i; ++j) {
      int l0 = list_black[j];
      int l1 = list_white[j];
#ifdef USE_SIMD_2
      auto black = KPPT[sq_black_king][k0][l0];
      auto white = KPPT[inv_sq_white_king][k1][l1];
      __m128i tmp = _mm_set_epi32(0, 0, *reinterpret_cast<int32_t *>(black),
                                  *reinterpret_cast<int32_t *>(white));
      tmp = _mm_cvtepi16_epi32(tmp);
      total = _mm_add_epi32(total, tmp);
#else
      black_kpp[kUs] += KPPT[sq_black_king][k0][l0][kUs];
      black_kpp[kThem] += KPPT[sq_black_king][k0][l0][kThem];
      white_kpp[kUs] -= KPPT[inv_sq_white_king][k1][l1][kUs];
      white_kpp[kThem] -= KPPT[inv_sq_white_king][k1][l1][kThem];
#endif
    }
    kkpt += KKPT[sq_black_king][sq_white_king][k0][side_to_move];
  }
#ifdef USE_SIMD_2
  black_kpp[kUs] = _mm_extract_epi32(total, 2);
  black_kpp[kThem] = _mm_extract_epi32(total, 3);
  white_kpp[kUs] = -_mm_extract_epi32(total, 0);
  white_kpp[kThem] = -_mm_extract_epi32(total, 1);
#endif
  parts.kppt[kBlack].us = static_cast<Value>(black_kpp[kUs]);
  parts.kppt[kBlack].them = static_cast<Value>(black_kpp[kThem]);
  parts.kppt[kWhite].us = static_cast<Value>(white_kpp[kUs]);
  parts.kppt[kWhite].them = static_cast<Value>(white_kpp[kThem]);
  parts.kkpt = static_cast<Value>(kkpt);
}

ValueUnit CalcPart(Square king, const KPPIndex *current, const KPPIndex *before,
                   const int *index, int num) {
  int black_value = 0;
  int white_value = 0;
#ifdef USE_SIMD_2
  __m128i total = _mm_setzero_si128();
  __m128i extra = _mm_setzero_si128();
#endif
  for (int i = 0; i < num; ++i) {
    const auto kpp_current = KPPT[king][current[index[i]]];
    const auto kpp_before = KPPT[king][before[index[i]]];
    for (int j = 0; j < kListNum; ++j) {
#ifdef USE_SIMD_2
      __m128i tmp = _mm_set_epi32(
          0, 0, *reinterpret_cast<int32_t *>(kpp_current[current[j]]),
          *reinterpret_cast<int32_t *>(kpp_before[before[j]]));
      tmp = _mm_cvtepi16_epi32(tmp);
      total = _mm_add_epi32(total, tmp);
#else
      black_value += kpp_current[current[j]][kUs];
      white_value += kpp_current[current[j]][kThem];
      black_value -= kpp_before[before[j]][kUs];
      white_value -= kpp_before[before[j]][kThem];
#endif
    }
  }

  for (int i = 1; i < num; ++i) {
    const auto kpp_current = KPPT[king][current[index[i]]];
    const auto kpp_before = KPPT[king][before[index[i]]];
    for (int j = 0; j < i; ++j) {
#ifdef USE_SIMD_2
      __m128i tmp = _mm_set_epi32(
          0, 0, *reinterpret_cast<int32_t *>(kpp_current[current[index[j]]]),
          *reinterpret_cast<int32_t *>(kpp_before[before[index[j]]]));
      tmp = _mm_cvtepi16_epi32(tmp);
      extra = _mm_add_epi32(extra, tmp);
#else
      black_value -= kpp_current[current[index[j]]][kUs];
      white_value -= kpp_current[current[index[j]]][kThem];
      black_value += kpp_before[before[index[j]]][kUs];
      white_value += kpp_before[before[index[j]]][kThem];
#endif
    }
  }
#ifdef USE_SIMD_2
  black_value = _mm_extract_epi32(total, 2) - _mm_extract_epi32(total, 0) -
                _mm_extract_epi32(extra, 2) + _mm_extract_epi32(extra, 0);
  white_value = _mm_extract_epi32(total, 3) - _mm_extract_epi32(total, 1) -
                _mm_extract_epi32(extra, 3) + _mm_extract_epi32(extra, 1);
#endif
  ValueUnit value = {Value(black_value), Value(white_value)};
  return value;
}

void CalcDifferenceWithoutCapture(const Position &pos,
                                  const EvalParts &last_parts,
                                  EvalParts &parts) {
  Square black_king = pos.square_king(kBlack);
  Square white_king = pos.square_king(kWhite);
  Square inv_white_king = inverse(white_king);

  const KPPIndex *prev_list_black = pos.prev_black_kpp_list();
  const KPPIndex *prev_list_white = pos.prev_white_kpp_list();
  const KPPIndex *list_black = pos.black_kpp_list();
  const KPPIndex *list_white = pos.white_kpp_list();

  assert(pos.list_index_move() < 38);

  Color side_to_move = pos.side_to_move();
  int black_kpp_diff[kNumberOfColor] = {};
  int white_kpp_diff[kNumberOfColor] = {};
  int kkpt = 0;
  const auto *black_prev_kpp_table =
      KPPT[black_king][prev_list_black[pos.list_index_move()]];
  const auto *black_kpp_table =
      KPPT[black_king][list_black[pos.list_index_move()]];
  const auto *white_prev_kpp_table =
      KPPT[inv_white_king][prev_list_white[pos.list_index_move()]];
  const auto *white_kpp_table =
      KPPT[inv_white_king][list_white[pos.list_index_move()]];
#ifdef USE_SIMD_2
  __m256i total = _mm256_setzero_si256();
#endif
  for (int i = 0; i < kListNum; ++i) {
#ifdef USE_SIMD_2
    __m128i tmp = _mm_set_epi32(
        *reinterpret_cast<const int32_t *>(
            black_prev_kpp_table[prev_list_black[i]]),
        *reinterpret_cast<const int32_t *>(black_kpp_table[list_black[i]]),
        *reinterpret_cast<const int32_t *>(
            white_prev_kpp_table[prev_list_white[i]]),
        *reinterpret_cast<const int32_t *>(white_kpp_table[list_white[i]]));
    __m256i tmp2 = _mm256_cvtepi16_epi32(tmp);
    total = _mm256_add_epi32(total, tmp2);
#else
    // 前回のを引く
    black_kpp_diff[kUs] -= black_prev_kpp_table[prev_list_black[i]][kUs];
    black_kpp_diff[kThem] -= black_prev_kpp_table[prev_list_black[i]][kThem];
    // 今回のを足す
    black_kpp_diff[kUs] += black_kpp_table[list_black[i]][kUs];
    black_kpp_diff[kThem] += black_kpp_table[list_black[i]][kThem];

    // 前回のを引く
    white_kpp_diff[kUs] += white_prev_kpp_table[prev_list_white[i]][kUs];
    white_kpp_diff[kThem] += white_prev_kpp_table[prev_list_white[i]][kThem];

    // 今回のを足す
    white_kpp_diff[kUs] -= white_kpp_table[list_white[i]][kUs];
    white_kpp_diff[kThem] -= white_kpp_table[list_white[i]][kThem];
#endif
    kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
  }
#ifdef USE_SIMD_2
  black_kpp_diff[kUs] =
      -_mm256_extract_epi32(total, 6) + _mm256_extract_epi32(total, 4);
  black_kpp_diff[kThem] =
      -_mm256_extract_epi32(total, 7) + _mm256_extract_epi32(total, 5);
  white_kpp_diff[kUs] =
      _mm256_extract_epi32(total, 2) - _mm256_extract_epi32(total, 0);
  white_kpp_diff[kThem] =
      _mm256_extract_epi32(total, 3) - _mm256_extract_epi32(total, 1);
#endif
  parts.kppt[kBlack].us = last_parts.kppt[kBlack].us + black_kpp_diff[kUs];
  parts.kppt[kBlack].them =
      last_parts.kppt[kBlack].them + black_kpp_diff[kThem];
  parts.kppt[kWhite].us = last_parts.kppt[kWhite].us + white_kpp_diff[kUs];
  parts.kppt[kWhite].them =
      last_parts.kppt[kWhite].them + white_kpp_diff[kThem];

  parts.kkpt = static_cast<Value>(kkpt);
}

void CalcDifferenceWithCapture(const Position &pos, const EvalParts &last_parts,
                               EvalParts &parts) {
  Square black_king = pos.square_king(kBlack);
  Square white_king = pos.square_king(kWhite);
  Square inv_white_king = inverse(white_king);

  const KPPIndex *prev_list_black = pos.prev_black_kpp_list();
  const KPPIndex *prev_list_white = pos.prev_white_kpp_list();
  const KPPIndex *list_black = pos.black_kpp_list();
  const KPPIndex *list_white = pos.white_kpp_list();

  assert(pos.list_index_capture() < 38);
  assert(pos.list_index_move() < 38);

  Color side_to_move = pos.side_to_move();
  int black_kpp_diff[kNumberOfColor] = {};
  int white_kpp_diff[kNumberOfColor] = {};
  int kkpt = 0;
  const auto *black_prev_kpp_table =
      KPPT[black_king][prev_list_black[pos.list_index_move()]];
  const auto *black_prev_cap_kpp_table =
      KPPT[black_king][prev_list_black[pos.list_index_capture()]];
  const auto *black_kpp_table =
      KPPT[black_king][list_black[pos.list_index_move()]];
  const auto *black_cap_kpp_table =
      KPPT[black_king][list_black[pos.list_index_capture()]];
  const auto *white_prev_kpp_table =
      KPPT[inv_white_king][prev_list_white[pos.list_index_move()]];
  const auto *white_prev_cap_kpp_table =
      KPPT[inv_white_king][prev_list_white[pos.list_index_capture()]];
  const auto *white_kpp_table =
      KPPT[inv_white_king][list_white[pos.list_index_move()]];
  const auto *white_cap_kpp_table =
      KPPT[inv_white_king][list_white[pos.list_index_capture()]];

#ifdef USE_SIMD_2
  __m256i total_black = _mm256_setzero_si256();
  __m256i total_white = _mm256_setzero_si256();
  __m256i extra = _mm256_setzero_si256();
#endif
  for (int i = 0; i < kListNum; ++i) {
#ifdef USE_SIMD_2
    __m128i tmp = _mm_set_epi32(
        *reinterpret_cast<const int32_t *>(
            black_prev_kpp_table[prev_list_black[i]]),
        *reinterpret_cast<const int32_t *>(
            black_prev_cap_kpp_table[prev_list_black[i]]),
        *reinterpret_cast<const int32_t *>(black_kpp_table[list_black[i]]),
        *reinterpret_cast<const int32_t *>(black_cap_kpp_table[list_black[i]]));
    __m256i tmp2 = _mm256_cvtepi16_epi32(tmp);
    total_black = _mm256_add_epi32(total_black, tmp2);

    tmp = _mm_set_epi32(
        *reinterpret_cast<const int32_t *>(
            white_prev_kpp_table[prev_list_white[i]]),
        *reinterpret_cast<const int32_t *>(
            white_prev_cap_kpp_table[prev_list_white[i]]),
        *reinterpret_cast<const int32_t *>(white_kpp_table[list_white[i]]),
        *reinterpret_cast<const int32_t *>(white_cap_kpp_table[list_white[i]]));
    tmp2 = _mm256_cvtepi16_epi32(tmp);
    total_white = _mm256_add_epi32(total_white, tmp2);
#else
    // 前回のを引く
    black_kpp_diff[kUs] -= black_prev_kpp_table[prev_list_black[i]][kUs];
    black_kpp_diff[kThem] -= black_prev_kpp_table[prev_list_black[i]][kThem];
    // とった分も引く
    black_kpp_diff[kUs] -= black_prev_cap_kpp_table[prev_list_black[i]][kUs];
    black_kpp_diff[kThem] -=
        black_prev_cap_kpp_table[prev_list_black[i]][kThem];

    // 今回のを足す
    black_kpp_diff[kUs] += black_kpp_table[list_black[i]][kUs];
    black_kpp_diff[kThem] += black_kpp_table[list_black[i]][kThem];
    black_kpp_diff[kUs] += black_cap_kpp_table[list_black[i]][kUs];
    black_kpp_diff[kThem] += black_cap_kpp_table[list_black[i]][kThem];

    // 前回のを引く
    white_kpp_diff[kUs] += white_prev_kpp_table[prev_list_white[i]][kUs];
    white_kpp_diff[kThem] += white_prev_kpp_table[prev_list_white[i]][kThem];

    // とった分も引く
    white_kpp_diff[kUs] += white_prev_cap_kpp_table[prev_list_white[i]][kUs];
    white_kpp_diff[kThem] +=
        white_prev_cap_kpp_table[prev_list_white[i]][kThem];

    // 今回のを足す
    white_kpp_diff[kUs] -= white_kpp_table[list_white[i]][kUs];
    white_kpp_diff[kThem] -= white_kpp_table[list_white[i]][kThem];

    white_kpp_diff[kUs] -= white_cap_kpp_table[list_white[i]][kUs];
    white_kpp_diff[kThem] -= white_cap_kpp_table[list_white[i]][kThem];
#endif
    kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
  }
#ifdef USE_SIMD_2
  __m128i tmp = _mm_set_epi32(
      *reinterpret_cast<const int32_t *>(
          black_prev_kpp_table[prev_list_black[pos.list_index_capture()]]),
      *reinterpret_cast<const int32_t *>(
          black_kpp_table[list_black[pos.list_index_capture()]]),
      *reinterpret_cast<const int32_t *>(
          white_prev_kpp_table[prev_list_white[pos.list_index_capture()]]),
      *reinterpret_cast<const int32_t *>(
          white_kpp_table[list_white[pos.list_index_capture()]]));
  __m256i tmp2 = _mm256_cvtepi16_epi32(tmp);
  extra = _mm256_add_epi32(extra, tmp2);
#else
  // 前回ので引きすぎたのを足す
  black_kpp_diff[kUs] +=
      black_prev_kpp_table[prev_list_black[pos.list_index_capture()]][kUs];
  black_kpp_diff[kThem] +=
      black_prev_kpp_table[prev_list_black[pos.list_index_capture()]][kThem];

  // 今回ので足しすぎたのを引く
  black_kpp_diff[kUs] -=
      black_kpp_table[list_black[pos.list_index_capture()]][kUs];
  black_kpp_diff[kThem] -=
      black_kpp_table[list_black[pos.list_index_capture()]][kThem];

  // 前回ので引きすぎたのを足す
  white_kpp_diff[kUs] -=
      white_prev_kpp_table[prev_list_white[pos.list_index_capture()]][kUs];
  white_kpp_diff[kThem] -=
      white_prev_kpp_table[prev_list_white[pos.list_index_capture()]][kThem];

  // 今回ので足しすぎたのを引く
  white_kpp_diff[kUs] +=
      white_kpp_table[list_white[pos.list_index_capture()]][kUs];
  white_kpp_diff[kThem] +=
      white_kpp_table[list_white[pos.list_index_capture()]][kThem];
#endif

#ifdef USE_SIMD_2
  total_black =
      _mm256_add_epi32(total_black, _mm256_srli_si256(total_black, 8));
  __m128i sum_black = _mm_sub_epi32(_mm256_extracti128_si256(total_black, 0),
                                    _mm256_extracti128_si256(total_black, 1));
  extra = _mm256_sub_epi32(_mm256_srli_si256(extra, 8), extra);
  sum_black = _mm_add_epi32(sum_black, _mm256_extracti128_si256(extra, 1));
  black_kpp_diff[kUs] = _mm_extract_epi32(sum_black, 0);
  black_kpp_diff[kThem] = _mm_extract_epi32(sum_black, 1);

  total_white =
      _mm256_add_epi32(total_white, _mm256_srli_si256(total_white, 8));
  __m128i sum_white = _mm_sub_epi32(_mm256_extracti128_si256(total_white, 0),
                                    _mm256_extracti128_si256(total_white, 1));
  sum_white = _mm_add_epi32(sum_white, _mm256_extracti128_si256(extra, 0));
  white_kpp_diff[kUs] = -_mm_extract_epi32(sum_white, 0);
  white_kpp_diff[kThem] = -_mm_extract_epi32(sum_white, 1);
#endif

  parts.kppt[kBlack].us = last_parts.kppt[kBlack].us + black_kpp_diff[kUs];
  parts.kppt[kBlack].them =
      last_parts.kppt[kBlack].them + black_kpp_diff[kThem];
  parts.kppt[kWhite].us = last_parts.kppt[kWhite].us + white_kpp_diff[kUs];
  parts.kppt[kWhite].them =
      last_parts.kppt[kWhite].them + white_kpp_diff[kThem];

  parts.kkpt = static_cast<Value>(kkpt);
}

template <Color C>
void CalcDifferenceKingMoveWithCapture(const Position &pos,
                                       const EvalParts &last_parts,
                                       EvalParts &parts) {
  const KPPIndex *list_black = pos.black_kpp_list();
  const KPPIndex *list_white = pos.white_kpp_list();
  const Square black_king = pos.square_king(kBlack);
  const Square white_king = pos.square_king(kWhite);
  Square inv_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp[kNumberOfColor] = {0};
  int white_kpp[kNumberOfColor] = {0};
  int kkpt = KKPT[black_king][white_king][list_black[0]][side_to_move];

  if (C == kBlack) {
#ifdef USE_SIMD_2
    __m128i total_black = _mm_setzero_si128();
    __m128i total_white = _mm_setzero_si128();
#endif
    const auto *black_kpp_table = KPPT[black_king];
    const KPPIndex *prev_list_white = pos.prev_white_kpp_list();

    const auto *white_prev_cap_kpp_table =
        KPPT[inv_white_king][prev_list_white[pos.list_index_capture()]];
    const auto *white_cap_kpp_table =
        KPPT[inv_white_king][list_white[pos.list_index_capture()]];
    int white_kpp_diff[kNumberOfColor] = {0};
#ifdef USE_SIMD_2
    __m128i tmp = _mm_set_epi32(
        0, 0,
        *reinterpret_cast<const int32_t *>(
            white_prev_cap_kpp_table[prev_list_white[0]]),
        *reinterpret_cast<const int32_t *>(white_cap_kpp_table[list_white[0]]));
    tmp = _mm_cvtepi16_epi32(tmp);
    total_white = _mm_add_epi32(total_white, tmp);
#else
    // とった分も引く
    white_kpp_diff[kUs] += white_prev_cap_kpp_table[prev_list_white[0]][kUs];
    white_kpp_diff[kThem] +=
        white_prev_cap_kpp_table[prev_list_white[0]][kThem];

    // 今回のを足す
    white_kpp_diff[kUs] -= white_cap_kpp_table[list_white[0]][kUs];
    white_kpp_diff[kThem] -= white_cap_kpp_table[list_white[0]][kThem];
#endif
    for (int i = 1; i < kListNum; ++i) {
      const int k0 = list_black[i];
      const auto *black_pp_table = black_kpp_table[k0];
      for (int j = 0; j < i; ++j) {
        const int l0 = list_black[j];
#ifdef USE_SIMD_2
        tmp = _mm_set_epi32(
            0, 0, 0, *reinterpret_cast<const int32_t *>(black_pp_table[l0]));
        tmp = _mm_cvtepi16_epi32(tmp);
        total_black = _mm_add_epi32(total_black, tmp);
#else
        black_kpp[kUs] += black_pp_table[l0][kUs];
        black_kpp[kThem] += black_pp_table[l0][kThem];
#endif
      }
#ifdef USE_SIMD_2
      tmp = _mm_set_epi32(0, 0,
                          *reinterpret_cast<const int32_t *>(
                              white_prev_cap_kpp_table[prev_list_white[i]]),
                          *reinterpret_cast<const int32_t *>(
                              white_cap_kpp_table[list_white[i]]));
      tmp = _mm_cvtepi16_epi32(tmp);
      total_white = _mm_add_epi32(total_white, tmp);
#else
      // とった分も引く
      white_kpp_diff[kUs] += white_prev_cap_kpp_table[prev_list_white[i]][kUs];
      white_kpp_diff[kThem] +=
          white_prev_cap_kpp_table[prev_list_white[i]][kThem];

      // 今回のを足す
      white_kpp_diff[kUs] -= white_cap_kpp_table[list_white[i]][kUs];
      white_kpp_diff[kThem] -= white_cap_kpp_table[list_white[i]][kThem];
#endif
      kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
    }
#ifdef USE_SIMD_2
    total_white = _mm_sub_epi32(total_white, _mm_srli_si128(total_white, 8));
    black_kpp[kUs] = _mm_extract_epi32(total_black, 0);
    black_kpp[kThem] = _mm_extract_epi32(total_black, 1);
    white_kpp_diff[kUs] = -_mm_extract_epi32(total_white, 0);
    white_kpp_diff[kThem] = -_mm_extract_epi32(total_white, 1);
#endif
    parts.kppt[kBlack].us = static_cast<Value>(black_kpp[kUs]);
    parts.kppt[kBlack].them = static_cast<Value>(black_kpp[kThem]);
    parts.kppt[kWhite].us = last_parts.kppt[kWhite].us + white_kpp_diff[kUs];
    parts.kppt[kWhite].them =
        last_parts.kppt[kWhite].them + white_kpp_diff[kThem];
  } else {
#ifdef USE_SIMD_2
    __m128i total_black = _mm_setzero_si128();
    __m128i total_white = _mm_setzero_si128();
#endif
    const KPPIndex *prev_list_black = pos.prev_black_kpp_list();

    const auto *black_prev_cap_kpp_table =
        KPPT[black_king][prev_list_black[pos.list_index_capture()]];
    const auto *black_cap_kpp_table =
        KPPT[black_king][list_black[pos.list_index_capture()]];
    const auto *white_kpp_table = KPPT[inv_white_king];
    int black_kpp_diff[kNumberOfColor] = {0};
#ifdef USE_SIMD_2
    __m128i tmp = _mm_set_epi32(
        0, 0,
        *reinterpret_cast<const int32_t *>(
            black_prev_cap_kpp_table[prev_list_black[0]]),
        *reinterpret_cast<const int32_t *>(black_cap_kpp_table[list_black[0]]));
    tmp = _mm_cvtepi16_epi32(tmp);
    total_black = _mm_add_epi32(total_black, tmp);
#else
    // とった分も引く
    black_kpp_diff[kUs] -= black_prev_cap_kpp_table[prev_list_black[0]][kUs];
    black_kpp_diff[kThem] -=
        black_prev_cap_kpp_table[prev_list_black[0]][kThem];

    // 今回のを足す
    black_kpp_diff[kUs] += black_cap_kpp_table[list_black[0]][kUs];
    black_kpp_diff[kThem] += black_cap_kpp_table[list_black[0]][kThem];
#endif
    for (int i = 1; i < kListNum; ++i) {
      const int k1 = list_white[i];
      const auto *white_pp_table = white_kpp_table[k1];
      for (int j = 0; j < i; ++j) {
        const int l1 = list_white[j];
#ifdef USE_SIMD_2
        tmp = _mm_set_epi32(
            0, 0, 0, *reinterpret_cast<const int32_t *>(white_pp_table[l1]));
        tmp = _mm_cvtepi16_epi32(tmp);
        total_white = _mm_add_epi32(total_white, tmp);
#else
        white_kpp[kUs] -= white_pp_table[l1][kUs];
        white_kpp[kThem] -= white_pp_table[l1][kThem];
#endif
      }
#ifdef USE_SIMD_2
      tmp = _mm_set_epi32(0, 0,
                          *reinterpret_cast<const int32_t *>(
                              black_prev_cap_kpp_table[prev_list_black[i]]),
                          *reinterpret_cast<const int32_t *>(
                              black_cap_kpp_table[list_black[i]]));
      tmp = _mm_cvtepi16_epi32(tmp);
      total_black = _mm_add_epi32(total_black, tmp);
#else
      // とった分も引く
      black_kpp_diff[kUs] -= black_prev_cap_kpp_table[prev_list_black[i]][kUs];
      black_kpp_diff[kThem] -=
          black_prev_cap_kpp_table[prev_list_black[i]][kThem];

      // 今回のを足す
      black_kpp_diff[kUs] += black_cap_kpp_table[list_black[i]][kUs];
      black_kpp_diff[kThem] += black_cap_kpp_table[list_black[i]][kThem];
#endif
      kkpt += KKPT[black_king][white_king][list_black[i]][side_to_move];
    }
#ifdef USE_SIMD_2
    total_black = _mm_sub_epi32(total_black, _mm_srli_si128(total_black, 8));
    black_kpp_diff[kUs] = _mm_extract_epi32(total_black, 0);
    black_kpp_diff[kThem] = _mm_extract_epi32(total_black, 1);
    white_kpp[kUs] = -_mm_extract_epi32(total_white, 0);
    white_kpp[kThem] = -_mm_extract_epi32(total_white, 1);
#endif
    parts.kppt[kBlack].us = last_parts.kppt[kBlack].us + black_kpp_diff[kUs];
    parts.kppt[kBlack].them =
        last_parts.kppt[kBlack].them + black_kpp_diff[kThem];
    parts.kppt[kWhite].us = static_cast<Value>(white_kpp[kUs]);
    parts.kppt[kWhite].them = static_cast<Value>(white_kpp[kThem]);
  }
  parts.kkpt = static_cast<Value>(kkpt);
}

template <Color C>
void CalcDifferenceKingMoveWithoutCapture(const Position &pos,
                                          const EvalParts &last_parts,
                                          EvalParts &parts) {
  const KPPIndex *list_black = pos.black_kpp_list();
  const KPPIndex *list_white = pos.white_kpp_list();
  const Square sq_black_king = pos.square_king(kBlack);
  const Square sq_white_king = pos.square_king(kWhite);
  Square inv_sq_white_king = inverse(pos.square_king(kWhite));

  Color side_to_move = pos.side_to_move();
  int black_kpp[kNumberOfColor] = {0};
  int white_kpp[kNumberOfColor] = {0};
  int kkpt = KKPT[sq_black_king][sq_white_king][list_black[0]][side_to_move];
  if (C == kBlack) {
    const auto *black_kpp_table = KPPT[sq_black_king];
    for (int i = 1; i < kListNum; ++i) {
      const int k0 = list_black[i];
      const auto *black_pp_table = black_kpp_table[k0];
      for (int j = 0; j < i; ++j) {
        const int l0 = list_black[j];
        black_kpp[kUs] += black_pp_table[l0][kUs];
        black_kpp[kThem] += black_pp_table[l0][kThem];
      }
      kkpt += KKPT[sq_black_king][sq_white_king][list_black[i]][side_to_move];
    }
    parts.kppt[kBlack].us = static_cast<Value>(black_kpp[kUs]);
    parts.kppt[kBlack].them = static_cast<Value>(black_kpp[kThem]);
    parts.kppt[kWhite] = last_parts.kppt[kWhite];
  } else {
    const auto *white_kpp_table = KPPT[inv_sq_white_king];
    for (int i = 1; i < kListNum; ++i) {
      const int k1 = list_white[i];
      const auto *white_pp_table = white_kpp_table[k1];
      for (int j = 0; j < i; ++j) {
        const int l1 = list_white[j];
        white_kpp[kUs] -= white_pp_table[l1][kUs];
        white_kpp[kThem] -= white_pp_table[l1][kThem];
      }
      kkpt += KKPT[sq_black_king][sq_white_king][list_black[i]][side_to_move];
    }
    parts.kppt[kBlack] = last_parts.kppt[kBlack];
    parts.kppt[kWhite].us = static_cast<Value>(white_kpp[kUs]);
    parts.kppt[kWhite].them = static_cast<Value>(white_kpp[kThem]);
  }
  parts.kkpt = static_cast<Value>(kkpt);
}

#if defined(_MSC_VER)
uint32_t FirstOne(uint64_t b) {
  unsigned long index = 0;

  _BitScanForward64(&index, b);
  return index;
}
#else
uint32_t FirstOne(uint64_t b) { return __builtin_ctzll(b); }
#endif

void CalcDifference(const Position &pos, Move last_move,
                    const EvalParts &last_parts, EvalParts &parts) {
  const Square from = move_from(last_move);
  const PieceType type = move_piece_type(last_move);

  if (type == kKing) {
    Color enemy = ~pos.side_to_move();
    Square king = pos.square_king(enemy);
    auto list = (pos.side_to_move() == kBlack) ? pos.white_kpp_list()
                                               : pos.black_kpp_list();
    auto entry = pos.this_thread()->kpp_list_.GetList(enemy, king);
    auto cache_value = pos.this_thread()->kpp_list_.GetValue(enemy, king);
    int count = 0;
    int index[kKingBrotherDiffSize];
#ifdef USE_SIMD
    if (cache_value.us != kValueZero) {
      // listの一致しない箇所のbitを立てる
      uint64_t bit = 0;
      for (int i = 0; i < 2; ++i) {
        __m256i current =
            _mm256_loadu_si256(reinterpret_cast<const __m256i *>(list + i * 16));
        __m256i old = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(entry + i * 16));
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
        __m128i current = _mm_set_epi16(0, 0, list[37], list[36], list[35],
                                        list[34], list[33], list[32]);
        __m128i old = _mm_set_epi16(0, 0, entry[37], entry[36], entry[35],
                                    entry[34], entry[33], entry[32]);
        __m128i cmp = _mm_cmpeq_epi16(current, old);
        __m128i pack = _mm_packs_epi16(cmp, _mm_set1_epi8(-1));
        pack = _mm_andnot_si128(pack, _mm_set1_epi8(-1));
        bit |= (static_cast<uint64_t>(_mm_movemask_epi8(pack)) << 32);
      }
      if (_mm_popcnt_u64(bit) < kKingBrotherDiffSize) {
        while (bit != 0) {
          uint32_t p = FirstOne(bit);
          index[count++] = p;
          bit ^= (1LLU << p);
        }
      } else {
        cache_value.us = kValueZero;
        cache_value.them = kValueZero;
      }
    }
#else
    if (cache_value != kValueZero) {
      for (int i = 0; i < kListNum; ++i) {
        if (entry[i] != list[i]) {
          if (count == kKingBrotherDiffSize) {
            cache_value = kValueZero;
            break;
          }
          index[count++] = i;
        }
      }
    }
#endif
    if (move_capture(last_move) == kPieceNone) {
      if (cache_value.us == kValueZero) {
        if (pos.side_to_move() == kBlack)
          CalcDifferenceKingMoveWithoutCapture<kWhite>(pos, last_parts, parts);
        else
          CalcDifferenceKingMoveWithoutCapture<kBlack>(pos, last_parts, parts);
      } else {
        if (enemy == kBlack) {
          parts.kppt[kBlack] = cache_value;
          if (count != 0)
            parts.kppt[kBlack] +=
                CalcPart(king, pos.black_kpp_list(), entry, index, count);
          parts.kppt[kWhite] = last_parts.kppt[kWhite];
          parts.kkpt = CalcKkptValue(pos);
        } else {
          parts.kppt[kBlack] = last_parts.kppt[kBlack];
          parts.kppt[kWhite] = cache_value;
          if (count != 0)
            parts.kppt[kWhite] -= CalcPart(inverse(king), pos.white_kpp_list(),
                                           entry, index, count);
          parts.kkpt = CalcKkptValue(pos);
        }
      }
    } else {
      if (cache_value.us == kValueZero) {
        if (pos.side_to_move() == kBlack)
          CalcDifferenceKingMoveWithCapture<kWhite>(pos, last_parts, parts);
        else
          CalcDifferenceKingMoveWithCapture<kBlack>(pos, last_parts, parts);
      } else {
        if (enemy == kBlack) {
          parts.kppt[kBlack] = cache_value;
          if (count != 0)
            parts.kppt[kBlack] +=
                CalcPart(king, pos.black_kpp_list(), entry, index, count);
          int cap = pos.list_index_capture();
          parts.kppt[kWhite] =
              last_parts.kppt[kWhite] -
              CalcPart(inverse(pos.square_king(kWhite)), pos.white_kpp_list(),
                       pos.prev_white_kpp_list(), &cap, 1);
          parts.kkpt = CalcKkptValue(pos);
        } else {
          int cap = pos.list_index_capture();
          parts.kppt[kBlack] =
              last_parts.kppt[kBlack] +
              CalcPart(pos.square_king(kBlack), pos.black_kpp_list(),
                       pos.prev_black_kpp_list(), &cap, 1);
          parts.kppt[kWhite] = cache_value;
          if (count != 0)
            parts.kppt[kWhite] -= CalcPart(inverse(king), pos.white_kpp_list(),
                                           entry, index, count);
          parts.kkpt = CalcKkptValue(pos);
        }
      }
    }

    // 差分がない場合はコピーしないようにする
    // ただほとんどないので意味がないかも
    if (cache_value.us == kValueZero) {
      pos.this_thread()->kpp_list_.SetList(enemy, king, list);
      auto v = (pos.side_to_move() == kBlack) ? parts.kppt[kWhite]
                                              : parts.kppt[kBlack];
      pos.this_thread()->kpp_list_.SetValue(enemy, king, v);
    }
  } else {
    if (from >= kBoardSquare) {
      CalcDifferenceWithoutCapture(pos, last_parts, parts);
    } else {
      const PieceType capture = move_capture(last_move);

      if (capture == kPieceNone)
        CalcDifferenceWithoutCapture(pos, last_parts, parts);
      else
        CalcDifferenceWithCapture(pos, last_parts, parts);
    }
  }
}

Value CalcKkptValue(const Position &pos) {
  int score = 0;
  Square black_king = pos.square_king(kBlack);
  Square white_king = pos.square_king(kWhite);
  Color color = pos.side_to_move();
  KPPIndex *list_black = pos.black_kpp_list();

  for (int i = 0; i < kListNum; ++i)
    score += KKPT[black_king][white_king][list_black[i]][color];
  return static_cast<Value>(score);
}

Value evaluate(const Position &pos, SearchStack *ss) {
  Value score;
  Entry *e = pos.this_thread()->eval_hash_[pos.key()];
  if (e->key == pos.key()) {
    ss->eval_parts = e->parts;
  } else {
    Move last_move = (ss - 1)->current_move;
    if ((ss - 1)->evaluated) {
      if ((ss - 1)->current_move == kMoveNull) {
        ss->eval_parts.kppt[kBlack] = (ss - 1)->eval_parts.kppt[kBlack];
        ss->eval_parts.kppt[kWhite] = (ss - 1)->eval_parts.kppt[kWhite];
        ss->eval_parts.kkpt = CalcKkptValue(pos);
        ss->material = (ss - 1)->material;
      } else {
        CalcDifference(pos, last_move, (ss - 1)->eval_parts, ss->eval_parts);
      }
    } else {
      CalcFull(pos, ss->eval_parts);
    }
    e->key = pos.key();
    e->parts = ss->eval_parts;
  }

  ss->evaluated = true;
  ss->material = static_cast<Value>(pos.material() * kFvScale);

  if (pos.side_to_move() == kBlack) {
    score = ss->eval_parts.kppt[kBlack].us + ss->eval_parts.kppt[kWhite].them +
            ss->material + ss->eval_parts.kkpt;
  } else {
    score = ss->eval_parts.kppt[kBlack].them + ss->eval_parts.kppt[kWhite].us +
            ss->material + ss->eval_parts.kkpt;
    score = -score;
  }
  score /= kFvScale;

  assert(score > -kValueInfinite && score < kValueInfinite);

  return score;
}

bool init() {
  std::ifstream ifs("kppt_kkpt.bin", std::ios::in | std::ios::binary);
  if (!ifs) {
    std::memset(KPPT, 0, sizeof(KPPT));
    std::memset(KKPT, 0, sizeof(KKPT));
    return false;
  }
  ifs.read(reinterpret_cast<char *>(KPPT), sizeof(KPPT));
  ifs.read(reinterpret_cast<char *>(KKPT), sizeof(KKPT));
  ifs.close();

  return true;
}
}  // namespace Eval
