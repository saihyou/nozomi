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

#include "book.h"

Book::Book()
{
  std::random_device rdev;
  engine_.seed(rdev());
}

Book::~Book()
{
  close();
}

void
Book::close()
{
  if (book_file_.is_open())
    book_file_.close();
  book_name_ = "";
}

bool
Book::open(const std::string &file_name)
{
  book_file_.open(file_name.c_str(), std::ifstream::in | std::ifstream::binary);
  if (!book_file_.is_open())
    return false;

  book_file_.seekg(0, std::ios::end);
  book_size_ = (uint32_t)(book_file_.tellg() / sizeof(BookEntry));
  
  if (!book_file_.good())
    return false;

  book_name_ = file_name;
  return true;
}

Move 
Book::get_move(const Position &pos)
{
  if (book_name_ == "")
    return kMoveNone;

  BookEntry entry;
  Move book_move = kMoveNone;
  unsigned int score;
  unsigned int scores_sum = 0;
  Key key = pos.key();
  for (unsigned i = find_entry(key); i < book_size_; i++)
  {
    entry = read_entry(i);
    if (entry.key != key)
      break;
    score = entry.score;
    scores_sum += score;
    if (engine_() % scores_sum < score)
      book_move = entry.move;
  }

  return book_move;
}

int 
Book::find_entry(Key key)
{
  int left;
  int right;
  int mid;

  left = 0;
  right = book_size_ - 1;

  while (left < right)
  {
    mid = (left + right) / 2;
    if (key <= read_entry(mid).key)
      right = mid;
    else
      left = mid + 1;
  }
  return read_entry(left).key == key ? left : book_size_;
}

BookEntry 
Book::read_entry(int index)
{
  BookEntry entry;
  book_file_.seekg(index * sizeof(BookEntry), std::ios_base::beg);
  book_file_.read((char *)&entry, sizeof(BookEntry));

  return entry;
}

