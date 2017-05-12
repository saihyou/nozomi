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

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <sstream>

#include "evaluate.h"
#include "misc.h"
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"

using std::string;

USI::OptionsMap Options;

namespace USI 
{
void 
on_eval(const Option &) 
{ 
  Eval::init(); 
}

void 
on_threads(const Option &) 
{ 
  Threads.read_usi_options(); 
}

void 
on_hash_size(const Option &o) 
{ 
  TT.resize(o); 
}

void 
on_clear_hash(const Option &) 
{ 
  TT.clear(); 
}

bool 
ci_less(char c1, char c2) 
{ 
  return tolower(c1) < tolower(c2); 
}

bool 
CaseInsensitiveLess::operator()(const string & s1, const string & s2) const 
{
  return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), ci_less);
}

void 
init(OptionsMap& o) 
{
  o["BookFile"]                    = Option("book.bin");
  o["Contempt"]                    = Option(0, -50,  50);
  o["Threads"]                     = Option(1, 1, 128, on_threads);
  o["USI_Hash"]                    = Option(32, 1, 16384, on_hash_size);
  o["Clear_Hash"]                  = Option(on_clear_hash);
  o["USI_Ponder"]                  = Option(true);
  o["OwnBook"]                     = Option(true);
  o["MultiPV"]                     = Option(1, 1, 500);
  o["ByoyomiMargin"]               = Option(0, 0, 5000);
#ifdef APERY_BOOK
  o["Best_Book_Move"] = Option(false);
  o["Min_Book_Score"] = Option(-180, -kValueInfinite, kValueInfinite);
#endif
}

std::ostream& 
operator<<(std::ostream &os, const OptionsMap &om) 
{
  for (size_t index = 0; index < om.size(); ++index)
  {
    for (OptionsMap::const_iterator it = om.begin(); it != om.end(); ++it)
    {
      if (it->second.index_ == index)
      {
        const Option& o = it->second;
        os << "\noption name " << it->first << " type " << o.type_;

        if (o.type_ != "button")
          os << " default " << o.default_value_;

        if (o.type_ == "spin")
          os << " min " << o.min_ << " max " << o.max_;

        break;
      }
    }
  }
  return os;
}

Option::Option(const char* v, OnChange f) 
: 
type_("string"), min_(0), max_(0), index_(Options.size()), on_change_(f)
{ 
  default_value_ = current_value_ = v; 
}

Option::Option(bool v, OnChange f) 
: 
type_("check"), min_(0), max_(0), index_(Options.size()), on_change_(f)
{ 
  default_value_ = current_value_ = (v ? "true" : "false"); 
}

Option::Option(OnChange f) 
: type_("button"), min_(0), max_(0), index_(Options.size()), on_change_(f) 
{}

Option::Option(int v, int minv, int maxv, OnChange f) 
: 
type_("spin"), min_(minv), max_(maxv), index_(Options.size()), on_change_(f)
{ 
  std::ostringstream ss; 
  ss << v; 
  default_value_ = current_value_ = ss.str(); 
}

Option::operator int() const 
{
  assert(type_ == "check" || type_ == "spin");
  return (type_ == "spin" ? atoi(current_value_.c_str()) : current_value_ == "true");
}

Option::operator std::string() const 
{
  assert(type_ == "string");
  return current_value_;
}

Option & 
Option::operator=(const string &v) 
{
  assert(!type_.empty());

  if 
  (   
    (type_ != "button" && v.empty())
    || 
    (type_ == "check" && v != "true" && v != "false")
    || 
    (type_ == "spin" && (atoi(v.c_str()) < min_ || atoi(v.c_str()) > max_))
  )
    return *this;

  if (type_ != "button")
    current_value_ = v;

  if (on_change_)
    on_change_(*this);

  return *this;
}

} // namespace
