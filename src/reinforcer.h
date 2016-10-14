/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

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

#ifndef _LEINFORCER_H_
#define _LEINFORCER_H_

#include <iostream>
#include <atomic>
#include "position.h"
#include "stats.h"
#include "learn.h"

struct PositionData
{
  std::string sfen;
  int         value;
};

// 勾配でもなんでもなくなったけどこのままで
// learn.hのとほとんど一緒だけど、BonanzaMethodを今後どうするかで考える
struct Gradient
{
  int kpp[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd];
  int kkp[kBoardSquare][kBoardSquare][Eval::kFEEnd];
  int kkpt[kBoardSquare][kBoardSquare][Eval::kFEEnd][kNumberOfColor];

  void increment(const Position &pos, int step);
  void clear();
};

Gradient & 
operator+=(Gradient &lhs, Gradient &rhs);

class Reinforcer
{
public:
  void reinforce(std::istringstream &is);

private:
  void   make_value(const std::string &record_file_name, const std::string &out_file_name);
  void   update_param(const std::string &record_file_name, int num_threads);
  void   update_value(std::vector<PositionData> &position_list);
  size_t read_file(std::ifstream &ifs, std::vector<PositionData> &position_list, size_t num_positions, bool &eof);
  void   add_param(const Gradient &param);
  Value  search(Position &pos, Depth depth);

  std::vector<std::string>               game_list_;
  std::vector<Position>                  positions_;
  std::vector<std::unique_ptr<Gradient>> gradients_;
  uint64_t                               all_diff_;
};

#endif