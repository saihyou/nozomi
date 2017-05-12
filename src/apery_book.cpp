/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2017 Yuhei Ohmori

  This code is based on Aoery.
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad
  Copyright (C) 2011-2017 Hiraoka Takuya

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

// Aperyフォーマットのbookファイルをnozomiで使用できるようにする
// 以下のコードをベースに作成しています。
// https://github.com/HiraokaTakuya/apery/blob/master/src/book.cpp

#include "apery_book.h"
#include "usi.h"

namespace
{

// AperyのSquareをnozomiのSquareに変換する
Square
to_square(int apery_sq)
{
  int x = 8 - apery_sq / 9;
  int y = apery_sq % 9;
  return Square(9 * y + x);
}
}

std::mt19937_64 AperyBook::mt64bit_;
Key AperyBook::zob_piece_[kPieceMax][kBoardSquare];
Key AperyBook::zob_hand_[7][19];
Key AperyBook::zob_turn_;

void
AperyBook::init()
{
  for (int p = 0; p < kPieceMax; ++p)
  {
    for (int sq = 0; sq < kBoardSquare; ++sq)
      zob_piece_[p][to_square(sq)] = mt64bit_();
  }

  for (int hp = 0; hp < 7; ++hp)
  {
    for (int num = 0; num < 19; ++num)
      zob_hand_[hp][num] = mt64bit_();
  }
  zob_turn_ = mt64bit_();
}

bool
AperyBook::open(const char * fname)
{
  file_name_ = "";

  if (is_open())
    close();

  std::ifstream::open(fname, std::ifstream::in | std::ifstream::binary | std::ios::ate);
  if (!is_open())
    return false;

  size_ = tellg() / sizeof(AperyBookEntry);

  if (!good())
  {
    std::cerr << "Failed to open book file " << fname << std::endl;
    return false;
  }

  file_name_ = fname;
  return true;
}

void
AperyBook::binary_search(Key key)
{
  size_t low = 0;
  size_t high = size_ - 1;
  size_t mid;
  AperyBookEntry entry;

  while (low < high && good()) 
  {
    mid = (low + high) / 2;
                                                                                                     
    seekg(mid * sizeof(AperyBookEntry), std::ios_base::beg);
    read(reinterpret_cast<char*>(&entry), sizeof(entry));

    if (key <= entry.key)
      high = mid;
    else
      low = mid + 1;
  }

  seekg(low * sizeof(AperyBookEntry), std::ios_base::beg);
}

Key
AperyBook::book_key(const Position &pos) 
{
  Key key = 0;
  BitBoard bb = pos.occupied();

  while (bb.test()) 
  {
    Square sq = bb.pop_bit();
    key ^= zob_piece_[pos.square(sq)][sq];
  }

  Hand hand = pos.hand(pos.side_to_move());
  key ^= zob_hand_[0][number_of(hand, kPawn)];
  key ^= zob_hand_[1][number_of(hand, kLance)];
  key ^= zob_hand_[2][number_of(hand, kKnight)];
  key ^= zob_hand_[3][number_of(hand, kSilver)];
  key ^= zob_hand_[4][number_of(hand, kGold)];
  key ^= zob_hand_[5][number_of(hand, kBishop)];
  key ^= zob_hand_[6][number_of(hand, kRook)];

  if (pos.side_to_move() == kWhite)
    key ^= zob_turn_;
  return key;
}

std::tuple<Move, Value> 
AperyBook::probe(const Position &pos, const std::string &fname, bool pick_best) 
{
  AperyBookEntry entry;
  uint16_t best = 0;
  uint32_t sum = 0;
  Move move = kMoveNone;
  Key key = book_key(pos);
  Value min_book_score = static_cast<Value>(static_cast<int>(Options["Min_Book_Score"]));
  Value score = kValueZero;

  if (file_name_ != fname && !open(fname.c_str()))
    return std::make_tuple(kMoveNone, kValueZero);

  binary_search(key);
                                                                                                                                              
  while (read(reinterpret_cast<char*>(&entry), sizeof(entry)), entry.key == key && good()) 
  {
    best = std::max(best, entry.count);
    sum += entry.count;
                                                                                                                                              
    if 
    (
      min_book_score <= entry.score
      &&
      (
        (random_() % sum < entry.count)
        ||
        (pick_best && entry.count == best)
      )
    )
    {
      Square to = to_square(entry.from_to_pro & 0x007fU);
      int from_raw = (entry.from_to_pro >> 7) & 0x007fU;
      if (from_raw >= kBoardSquare) 
      {
        move = move_init(to, to_drop_piece_type(static_cast<Square>(from_raw)));
      }
      else
      {
        Square from = to_square(from_raw);
        PieceType pt_from = type_of(pos.square(from));
        if (entry.from_to_pro & kPromoted)
          move = move_init(from, to, pt_from, type_of(pos.square(to)), true);
        else
          move = move_init(from, to, pt_from, type_of(pos.square(to)), false);
      }
      score = entry.score;
    }
  }

  return std::make_tuple(move, score);
}
