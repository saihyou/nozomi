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

#ifndef _USI_H_
#define _USI_H_

#include <map>
#include <string>

#include "types.h"

class Position;

namespace USI 
{
class Option;

struct CaseInsensitiveLess 
{
  bool
  operator() (const std::string &, const std::string &) const;
};

typedef std::map<std::string, Option, CaseInsensitiveLess> OptionsMap;

class Option 
{
  typedef void (*OnChange)(const Option &);

public:
  Option(OnChange = nullptr);
  Option(bool v, OnChange = nullptr);
  Option(const char *v, OnChange = nullptr);
  Option(int v, int min, int max, OnChange = nullptr);

  Option &
  operator=(const std::string &v);

  operator int() const;

  operator std::string() const;

private:
  friend std::ostream& operator<<(std::ostream&, const OptionsMap&);

  std::string default_value_;
  std::string current_value_;
  std::string type_;
  int min_;
  int max_;
  size_t index_;
  OnChange on_change_;
};

void
init(OptionsMap &);

void
loop(int argc, char *argv[]);

std::string
format_value(Value v, Value alpha = -kValueInfinite, Value beta = kValueInfinite);

const std::string
format_move(Move m);

Move
to_move(const Position &pos, std::string &str);

std::string
to_sfen(const Position &pos);
} // namespace USI

extern USI::OptionsMap Options;

#endif
