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

#include <iomanip>
#include <iostream>
#include <sstream>

#include "misc.h"
#include "thread.h"

using namespace std;

static const string kVersion = "20160529";

const string
engine_info(bool to_usi) 
{
  const string months("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");
  string month;
  string day;
  string year;
  stringstream s;
  stringstream date(__DATE__);

  s << "nozomi " << kVersion << setfill('0');

  if (kVersion.empty())
  {
    date >> month >> day >> year;
    s << setw(2) << day << setw(2) << (1 + months.find(month) / 4) << year.substr(2);
  }

  s << (to_usi ? "\nid author ": " by ")
    << "Yuhei Ohmori";

  return s.str();
}

std::ostream &
operator<<(std::ostream& os, SyncCout sc) 
{

  static std::mutex m;

  if (sc == kIoLock)
      m.lock();

  if (sc == kIoUnlock)
      m.unlock();

  return os;
}

void
prefetch(void *addr)
{
#if defined(_MSC_VER)
  _mm_prefetch((char *)addr, _MM_HINT_T0);
#else
  __builtin_prefetch(addr);
#endif
}

