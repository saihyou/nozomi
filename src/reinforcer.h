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

using LearnFloatingPoint = float;


struct PositionData
{
  std::string sfen;
  Value       value;
  Color       win;
  std::string next_move;
  int total_ply;
};

struct Gradient
{
  std::atomic<LearnFloatingPoint> kppt[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd][kNumberOfColor];
  std::atomic<LearnFloatingPoint> kkpt[kBoardSquare][kBoardSquare][Eval::kFEEnd][kNumberOfColor];
  std::atomic<int> kppt_count[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd][kNumberOfColor];
  std::atomic<int> kkpt_count[kBoardSquare][kBoardSquare][Eval::kFEEnd][kNumberOfColor];

  void increment(const Position &pos, LearnFloatingPoint delta, bool kpp);
  void clear();
};


class Reinforcer
{
public:
  void   reinforce(std::istringstream &is);

private:
  void   update_param(const std::string &record_file_name, int num_threads);
  void   compute_gradient(std::vector<PositionData> &position_list);
  size_t read_file(std::ifstream &ifs, std::vector<PositionData> &position_list, size_t num_positions, bool &eof);
  void   add_param(const Gradient &param1);
  void   load_param();
  void   save_param();
  bool kpp_;
  int                                    batch_size_;
  std::vector<std::string>               game_list_;
  std::vector<Position>                  positions_;
  std::unique_ptr<Gradient>              gradient_;
  LearnFloatingPoint                     win_diff_;
  LearnFloatingPoint                     value_diff_;
};

#endif
