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

#ifndef _TRANSPOSITION_TABLE_H_
#define _TRANSPOSITION_TABLE_H_

#include "misc.h"
#include "types.h"
#include "move.h"
#include "position.h"

// key        32 bit Stockfishは16bitだが、衝突が多いので32bitにする
// move       32 bit
// value      16 bit
// generation  6 bit
// bound type  2 bit
// depth       8 bit
class TTEntry 
{
public:
  Move 
  move() const
  {
    return static_cast<Move>(move32_);
  }

  Value 
  value() const
  {
    return static_cast<Value>(value16_); 
  }

  Depth 
  depth() const 
  { 
    return static_cast<Depth>(depth8_); 
  }

  Bound 
  bound() const      
  { 
    return static_cast<Bound>(generation_and_bound8_ & 0x3); 
  }

  void 
  save(Key k, Value v, Bound b, Depth d, Move m, uint8_t g) 
  {

    if (m || (k >> 32) != key32_)
      move32_ = m;

    if
    (
      (k >> 32) != key32_
      ||
      d  / kOnePly > depth8_ - 4
      ||
      b == kBoundExact
    )
    {
      key32_                 = (uint32_t)(k >> 32);
      value16_               = (int16_t)v;
      generation_and_bound8_ = (uint8_t)(g | b);
      depth8_                = (int8_t)(d / kOnePly);
    }
  }

private:
  friend class TranspositionTable;

  uint8_t
  generation() const
  {
    return generation_and_bound8_ & 0xFC;
  }

  uint32_t key32_;
  uint32_t move32_;
  int16_t  value16_;
  uint8_t  generation_and_bound8_;
  int8_t   depth8_;
};

class TranspositionTable 
{
  static const int
  kCacheLineSize = 64;
  
  static const int
  kClusterSize = 5;

  struct Cluster 
  {
    TTEntry entry[kClusterSize];
  };

public:
  ~TranspositionTable() 
  { 
    free(mem_); 
  }

  void 
  new_search() 
  { 
    generation_ += 4; 
  }

  TTEntry * 
  probe(const Key key, bool *found) const;

  TTEntry * 
  first_entry(const Key key) const
  {
    return &table_[(size_t)key & (cluster_count_ - 1)].entry[0];
  }

  void 
  resize(uint64_t mb_size);

  void 
  clear();

  uint8_t
  generation() const
  {
    return generation_;
  }

private:
  size_t     cluster_count_;
  Cluster   *table_;
  void      *mem_;
  uint8_t    generation_;
};

extern TranspositionTable TT;

#endif
