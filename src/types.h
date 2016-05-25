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

#ifndef _TYPES_H_
#define _TYPES_H_

#include <cassert>
#include <cctype>
#include <climits>
#include <cstdlib>
#include <stdint.h>

#if defined(_WIN64)
#  include <intrin.h>
#endif

#if defined(_MSC_VER)
#  include <nmmintrin.h>
#  include <xmmintrin.h>
#endif

#ifdef _MSC_VER
#  define FORCE_INLINE  __forceinline
#elif defined(__GNUC__)
#  define FORCE_INLINE  inline __attribute__((always_inline))
#else
#  define FORCE_INLINE  inline
#endif

typedef uint64_t Key;

constexpr int
kMaxMoves = 600;

constexpr int
kMaxPly = 128;

enum Color 
{
  kBlack,
  kWhite,
  kNoColor,
  kNumberOfColor = 2
};

enum Bound 
{
  kBoundNone,
  kBoundUpper,
  kBoundLower,
  kBoundExact = kBoundUpper | kBoundLower
};

enum Value : int32_t 
{
  kValueZero = 0,
  kValueDraw = 0,
  kValueKnownWin = 10000,
  kValueMaxEvaluate = 30000,
  kValueMate = 32000,
  kValueInfinite = 32001,
  kValueNone = 32002,

  kValueMateInMaxPly =  kValueMate - kMaxPly,
  kValueMatedInMaxPly = -kValueMate + kMaxPly,

  kValueSamePosition = kValueMateInMaxPly - 1,
};

constexpr int
kFlagPromoted = 8;

enum PieceType
{
  kOccupied = 0,
  kPieceNone = 0,
  kPawn,
  kLance,
  kKnight,
  kSilver,
  kBishop,
  kRook,
  kGold,
  kKing,
  kPromotedPawn,
  kPromotedLance,
  kPromotedKnight,
  kPromotedSilver,
  kHorse,
  kDragon,
  kPieceTypeMax
};

enum Piece
{
  kEmpty = 0,
  kBlackPawn,
  kBlackLance,
  kBlackKnight,
  kBlackSilver,
  kBlackBishop,
  kBlackRook,
  kBlackGold,
  kBlackKing,
  kBlackPromotedPawn,
  kBlackPromotedLance,
  kBlackPromotedKnight,
  kBlackPromotedSilver,
  kBlackHorse,
  kBlackDragon,
  kFlagWhite = 16,
  kWhitePawn,
  kWhiteLance,
  kWhiteKnight,
  kWhiteSilver,
  kWhiteBishop,
  kWhiteRook,
  kWhiteGold,
  kWhiteKing,
  kWhitePromotedPawn,
  kWhitePromotedLance,
  kWhitePromotedKnight,
  kWhitePromotedSilver,
  kWhiteHorse,
  kWhiteDragon,
  kPieceMax
};

enum Direction
{
  kDirMisc = 0,
  kDirFile = 0x02,      // 0b0010
  kDirRank = 0x03,      // 0b0011
  kDirRight45 = 0x04,   // 0b0100
  kDirLeft45 = 0x05,    // 0b0101
  kDirFlagCross = 0x02,
  kDirFlagDiag = 0x04
};

enum Depth 
{
  kOnePly = 1,

  kDepthZero = 0,
  kDepthQsChecks = 0,
  kDepthQsNoChecks = -1,
  kDepthQsRecaptues = -5,

  kDepthNone = -6,
  kDepthMax = kMaxPly
};

enum Square
{
  k9A = 0, k8A, k7A, k6A, k5A, k4A, k3A, k2A, k1A,
  k9B, k8B, k7B, k6B, k5B, k4B, k3B, k2B, k1B,
  k9C, k8C, k7C, k6C, k5C, k4C, k3C, k2C, k1C,
  // x < k9Dの範囲が後手陣。先手にとって駒がなれるのはこの範囲。
  k9D, k8D, k7D, k6D, k5D, k4D, k3D, k2D, k1D,
  k9E, k8E, k7E, k6E, k5E, k4E, k3E, k2E, k1E,
  k9F, k8F, k7F, k6F, k5F, k4F, k3F, k2F, k1F,
  // x > k1Fの範囲が先手陣。後手にとって駒がなれるのはこの範囲。
  k9G, k8G, k7G, k6G, k5G, k4G, k3G, k2G, k1G,
  k9H, k8H, k7H, k6H, k5H, k4H, k3H, k2H, k1H,
  k9I, k8I, k7I, k6I, k5I, k4I, k3I, k2I, k1I,
  kBoardSquare,
  kBlackHandPawn   = kBoardSquare     - 1, // 1から使用するため、k1Iと同じ値でもいい
  kBlackHandLance  = kBlackHandPawn   + 18,
  kBlackHandKnight = kBlackHandLance  + 4,
  kBlackHandSilver = kBlackHandKnight + 4,
  kBlackHandGold   = kBlackHandSilver + 4,
  kBlackHandBishop = kBlackHandGold   + 4,
  kBlackHandRook   = kBlackHandBishop + 2,
  kWhiteHandPawn   = kBlackHandRook   + 2,
  kWhiteHandLance  = kWhiteHandPawn   + 18,
  kWhiteHandKnight = kWhiteHandLance  + 4,
  kWhiteHandSilver = kWhiteHandKnight + 4,
  kWhiteHandGold   = kWhiteHandSilver + 4,
  kWhiteHandBishop = kWhiteHandGold   + 4,
  kWhiteHandRook   = kWhiteHandBishop + 2,
  kSquareHand      = kWhiteHandRook   + 3
};

enum File
{
  kFile1,
  kFile2,
  kFile3,
  kFile4,
  kFile5,
  kFile6,
  kFile7,
  kFile8,
  kFile9,
  kNumberOfFile
};

enum Rank
{
  kRank1,
  kRank2,
  kRank3,
  kRank4,
  kRank5,
  kRank6,
  kRank7,
  kRank8,
  kRank9,
  kNumberOfRank
};

#define ENABLE_SAFE_OPERATORS_ON(T)                                         \
inline T operator+(const T d1, const T d2) { return T(int(d1) + int(d2)); } \
inline T operator+(T d1, int i) { return T(int(d1) + i); }                  \
inline T operator-(const T d1, const T d2) { return T(int(d1) - int(d2)); } \
inline T operator*(int i, const T d) { return T(i * int(d)); }              \
inline T operator*(const T d, int i) { return T(int(d) * i); }              \
inline T operator*(const T d1, const T d2) { return T(int(d1) * int(d2)); } \
inline T operator-(const T d) { return T(-int(d)); }                        \
inline T& operator+=(T& d1, const T d2) { return d1 = d1 + d2; }            \
inline T& operator-=(T& d1, const T d2) { return d1 = d1 - d2; }            \
inline T& operator*=(T& d, int i) { return d = T(int(d) * i); }

#define ENABLE_OPERATORS_ON(T) ENABLE_SAFE_OPERATORS_ON(T)                  \
inline T& operator++(T& d) { return d = T(int(d) + 1); }                    \
inline T& operator--(T& d) { return d = T(int(d) - 1); }                    \
inline T operator/(const T d, int i) { return T(int(d) / i); }              \
inline T& operator/=(T& d, int i) { return d = T(int(d) / i); }

ENABLE_OPERATORS_ON(Value)
ENABLE_OPERATORS_ON(PieceType)
ENABLE_OPERATORS_ON(Piece)
ENABLE_OPERATORS_ON(Color)
ENABLE_OPERATORS_ON(Depth)
ENABLE_OPERATORS_ON(Square)
ENABLE_OPERATORS_ON(File)
ENABLE_OPERATORS_ON(Rank)

inline Value operator-(Value v, int i) { return Value(int(v) - i); }
inline Value& operator+=(Value& v, int i) { return v = v + i; }

#undef ENABLE_OPERATORS_ON
#undef ENABLE_SAFE_OPERATORS_ON

inline Color
operator~(Color c) 
{
  return Color(c ^ kWhite);
}

inline Value
mate_in(int ply) 
{
  return kValueMate - ply;
}

inline Value
mated_in(int ply) 
{
  return -kValueMate + ply;
}

inline Piece
make_piece(PieceType pt, Color c) 
{
  return Piece((c << 4) | pt);
}

inline PieceType
type_of(Piece p)  
{
  return PieceType(p & 0xF);
}

inline Color
color_of(Piece p) 
{
  assert(p != kEmpty);
  return Color(p >> 4);
}

inline PieceType
to_drop_piece_type(Square from)
{
  return PieceType(from - kBoardSquare + 1);
}

inline PieceType
unpromote_piece_type(PieceType p)
{
  return PieceType(p & 0x7);
}

inline bool
is_ok(Square s) 
{
  return s >= k9A && s <= k1I;
}

#endif
