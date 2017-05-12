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
// https://github.com/HiraokaTakuya/apery/blob/master/src/book.hpp

#ifndef _APERY_BOOK_H_
#define _APERY_BOOK_H_

#include <random>
#include <fstream>
#include <chrono>
#include <tuple>
#include "types.h"
#include "position.h"

struct AperyBookEntry
{
  Key      key;
  uint16_t from_to_pro;
  uint16_t count;
  Value    score;
};

class AperyBook : private std::ifstream
{
public:
  AperyBook() : random_(std::chrono::system_clock::now().time_since_epoch().count()) {}
  
  std::tuple<Move, Value>
  probe(const Position &pos, const std::string &fname, bool pick_best);
  
  static void
  init();

  static Key
  book_key(const Position &pos);

private:
  bool
  open(const char *fname);

  void
  binary_search(Key key);

  std::mt19937_64 random_;
  std::string file_name_;
  size_t size_;

  static std::mt19937_64 mt64bit_;
  static Key zob_piece_[kPieceMax][kBoardSquare];
  static Key zob_hand_[7][19];
  static Key zob_turn_;
};

#endif
