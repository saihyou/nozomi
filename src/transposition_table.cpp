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

#include <cstring>
#include <iostream>

#include "transposition_table.h"

TranspositionTable TT;

void 
TranspositionTable::resize(uint64_t mb_size)
{
  size_t new_cluster_count = size_t(1) << msb((mb_size * 1024 * 1024) / sizeof(Cluster));

  if (new_cluster_count == cluster_count_)
    return;

  cluster_count_ = new_cluster_count;

  free(mem_);
  mem_ = calloc(cluster_count_ * sizeof(Cluster) + kCacheLineSize - 1, 1);

  if (!mem_)
  {
    std::cerr << "Failed to allocate " << mb_size
              << "MB for transposition table." << std::endl;
    exit(EXIT_FAILURE);
  }

  table_ = (Cluster*)((uintptr_t(mem_) + kCacheLineSize - 1) & ~(kCacheLineSize - 1));
}

void 
TranspositionTable::clear()
{
  std::memset(table_, 0, cluster_count_ * sizeof(Cluster));
}

TTEntry * 
TranspositionTable::probe(const Key key, bool *found) const 
{
  TTEntry * const tte = first_entry(key);
  const uint32_t key32 = key >> 32;

  for (unsigned i = 0; i < kClusterSize; ++i)
  {
    if (tte[i].key32_ == 0 || tte[i].key32_ == key32)
    {
      if
      (
        (tte[i].generation_and_bound8_ & 0xFC) != generation_
        &&
        tte[i].key32_ != 0
      )
        tte[i].generation_and_bound8_ = static_cast<uint8_t>(generation_ | tte[i].bound()); // Refresh

#ifdef DISABLE_TT
      *found = false;
#else
      *found = (tte[i].key32_ != 0);
#endif
      return &tte[i];
    }
  }

  TTEntry *replace = tte;
  for (unsigned i = 1; i < kClusterSize; ++i)
  {
    if
    (
      replace->depth8_ - ((259 + generation_ - replace->generation_and_bound8_) & 0xFC) * 2 * kOnePly
      >
      tte[i].depth8_   - ((259 + generation_ - tte[i].generation_and_bound8_)   & 0xFC) * 2 * kOnePly
    )
      replace = &tte[i];
  }
  *found = false;
  return replace;
}

