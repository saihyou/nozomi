/*
  nozomi, a USI shogi playing engine derived from Stockfish (chess playing engin)
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

#ifndef _BOOK_H_
#define _BOOK_H_

#include <fstream>
#include <string>
#include <random>

#include "types.h"
#include "move.h"
#include "position.h"

struct BookEntry
{
  Key key;
  Move move;
  uint32_t score;
};

class Book
{
public:
  Book();

  ~Book();

  bool
  open(const std::string &file_name);

  void
  close();

  Move
  get_move(const Position &pos);

private:
  int 
  find_entry(Key key);

  BookEntry 
  read_entry(int index);

  uint32_t book_size_;
  std::ifstream book_file_;
  std::string book_name_;
  std::mt19937 engine_;
};

#endif
