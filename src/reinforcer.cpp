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

#include <omp.h>
#include "reinforcer.h"
#include "transposition_table.h"
#include "thread.h"

constexpr Depth kSearchDepth = Depth(6);
constexpr int   kDiffMargin  = 128;
constexpr int   kBatchSize   = 100000;

void
Gradient::increment(const Position & pos, int step)
{
  const Square bk = pos.square_king(kBlack);
  const Square wk = pos.square_king(kWhite);
  const int *list_black = pos.black_kpp_list();
  const int *list_white = pos.white_kpp_list();

  for (int i = 0; i < Eval::kListNum; ++i)
  {
    const int k0 = list_black[i];
    const int k1 = list_white[i];
    for (int j = 0; j < i; ++j)
    {
      const int l0 = list_black[j];
      const int l1 = list_white[j];
      KppIndex bi(bk, k0, l0);
      kpp[bi.king][bi.i][bi.j] += step;
      KppIndex wi(Eval::inverse(wk), k1, l1);
      kpp[wi.king][wi.i][wi.j] -= step;
    }
    KingPosition bksq(bk);
    BoardPosition wksq(wk);
    int pi = k0;
    if (bksq.swap)
    {
      wksq.x = kFile9 - wksq.x;
      pi = inverse_file_kpp_index(pi);
    }
    else if (bksq.x == kFile5 && wksq.x > kFile5)
    {
      wksq.x = kFile9 - wksq.x;
      pi = lower_file_kpp_index(pi);
    }
    else if (bksq.x == kFile5 && wksq.x == kFile5)
    {
      pi = lower_file_kpp_index(pi);
    }
    kkp[bksq.square()][wksq.square()][pi] += step;
    kkpt[bksq.square()][wksq.square()][pi][pos.side_to_move()] += step;
  }
}

void
Gradient::clear()
{
  std::memset(this, 0, sizeof(*this));
}

Gradient &
operator+=(Gradient &lhs, Gradient &rhs)
{
  for
  (
    auto lit = &(***std::begin(lhs.kpp)), rit = &(***std::begin(rhs.kpp));
    lit != &(***std::end(lhs.kpp));
    ++lit, ++rit
  )
    *lit += *rit;

  for
  (
    auto lit = &(***std::begin(lhs.kkp)), rit = &(***std::begin(rhs.kkp));
    lit != &(***std::end(lhs.kkp));
    ++lit, ++rit
  )
    *lit += *rit;

  for
  (
    auto lit = &(****std::begin(lhs.kkpt)), rit = &(****std::begin(rhs.kkpt));
    lit != &(****std::end(lhs.kkpt));
    ++lit, ++rit
  )
    *lit += *rit;

  return lhs;
}

void
Reinforcer::reinforce(std::istringstream &is)
{
  std::string record_file_name;
  int num_threads;
  std::string type;

  is >> type;
  is >> record_file_name;
  is >> num_threads;

  TT.clear();
  Search::Limits.infinite            = 1;
  Search::Signals.stop_on_ponder_hit = false;
  Search::Signals.stop               = false;

#ifndef _OPENMP
  num_threads = 1;
#endif

  for (int i = 0; i < num_threads; ++i)
  {
    positions_.push_back(Position());
    Threads.push_back(new Thread);
  }

#ifdef _OPENMP
  omp_set_num_threads(num_threads);
#endif

  if (type == "make")
    make_value(record_file_name, "out.txt");
  else if (type == "update")
    update_param(record_file_name, num_threads);
}

void
Reinforcer::make_value(const std::string &record_file_name, const std::string &out_file_name)
{
  std::vector<PositionData> position_list;
  std::ifstream ifs(record_file_name.c_str());
  std::ofstream ofs(out_file_name.c_str());
  bool eof = false;
  uint64_t count = 0;

  while (!eof)
  {
    read_file(ifs, position_list, 10000, eof);
    Search::clear();
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
    for (int i = 0; i < static_cast<int>(position_list.size()); ++i)
    {
      PositionData &data = position_list[i];
      int thread_id = 0;
#ifdef _OPENMP
      thread_id = omp_get_thread_num();
#endif
      positions_[thread_id].set(data.sfen, Threads[thread_id + 1]);
      if (!positions_[thread_id].validate())
      {
        std::cout << "invalid pos" << std::endl;
        data.value = 0;
        continue;
      }
      data.value = static_cast<int>(search(positions_[thread_id], kSearchDepth));
    }

    ++count;
    if (count % 500 == 0)
      std::cout << count << std::endl;
    else if (count % 100 == 0)
      std::cout << "o" << std::flush;
    else if (count % 10 == 0)
      std::cout << "." << std::flush;

    for (auto &data : position_list)
    {
      if (data.value != 0 && data.value > -2000 && data.value < 2000)
        ofs << data.sfen << "," << data.value << std::endl;
    }
    position_list.clear();
  }
}

void
Reinforcer::update_param(const std::string &record_file_name, int num_threads)
{
  std::ifstream ifs2("new_fv2.bin", std::ios::in | std::ios::binary);
  if (ifs2)
  {
    ifs2.read(reinterpret_cast<char *>(Eval::KPP), sizeof(Eval::KPP));
    ifs2.read(reinterpret_cast<char *>(Eval::KKP), sizeof(Eval::KKP));
    ifs2.read(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
    ifs2.close();
  }

  all_diff_ = 0;
  for (int i = 0; i < num_threads; ++i)
    gradients_.push_back(std::unique_ptr<Gradient>(new Gradient));

  std::vector<PositionData> position_list;
  std::ifstream ifs(record_file_name.c_str());
  bool eof = false;
  int count = 0;

  while (!eof)
  {
    read_file(ifs, position_list, kBatchSize, eof);
    if (position_list.empty())
      break;

    update_value(position_list);
    for (int i = 1; i < num_threads; ++i)
      *gradients_[0] += *gradients_[i];
    position_list.clear();
    add_param(*gradients_[0]);
    std::cout << "count : " << ++count << std::endl;
    std::cout << std::sqrt(static_cast<double>(all_diff_) / kBatchSize) << std::endl;
    all_diff_ = 0;
    for (auto &g : gradients_)
      g->clear();
    if (count % 100 == 0)
    {
      std::ofstream fs("new_fv2.bin", std::ios::binary);
      fs.write(reinterpret_cast<char *>(Eval::KPP), sizeof(Eval::KPP));
      fs.write(reinterpret_cast<char *>(Eval::KKP), sizeof(Eval::KKP));
      fs.write(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
      fs.close();
    }
  }
  std::ofstream fs("new_fv2.bin", std::ios::binary);
  fs.write(reinterpret_cast<char *>(Eval::KPP), sizeof(Eval::KPP));
  fs.write(reinterpret_cast<char *>(Eval::KKP), sizeof(Eval::KKP));
  fs.write(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
  fs.close();
}

void
Reinforcer::update_value(std::vector<PositionData> &position_list)
{
// 4スレッドくらいまでしか有効に使えないけど、ひとまずこのまま
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
  for (int i = 0; i < static_cast<int>(position_list.size()); ++i)
  {
    PositionData &data = position_list[i];
    int thread_id = 0;
#ifdef _OPENMP
    thread_id = omp_get_thread_num();
#endif
    int diff = 0;

    if (data.value < -2000 || data.value > 2000 || data.value == 0)
      continue;

    positions_[thread_id].set(data.sfen, Threads[thread_id + 1]);
    
    SearchStack stack[20];
    Move pv[kMaxPly + 1];
    SearchStack *ss = stack + 2;
    std::memset(ss - 2, 0, 5 * sizeof(SearchStack));
    ss->pv = pv;
    StateInfo state[20];
    Value quiet_value;
    quiet_value = Search::qsearch(positions_[thread_id], ss, -kValueInfinite, kValueInfinite);
    diff = data.value - quiet_value;

    Color root_color = positions_[thread_id].side_to_move();
    int step = 0;
    if (diff > kDiffMargin)
      step = (root_color == kBlack) ? 1 : -1;
    else if (diff < -kDiffMargin)
      step = (root_color == kBlack) ? -1 : 1;

    if (step != 0)
    {
      int j = 0;
      while (pv[j] != kMoveNone)
      {
        positions_[thread_id].do_move(pv[j], state[j]);
        ++j;
      }
      gradients_[thread_id]->increment(positions_[thread_id], step);
    }

#ifdef _OPENMP
#pragma omp critical
#endif
    all_diff_ += diff * diff;
  }
}

size_t
Reinforcer::read_file(std::ifstream &ifs, std::vector<PositionData> &position_list, size_t num_positions, bool &eof)
{
  std::string str;
  size_t num = 0;
  eof = false;
  while (num < num_positions)
  {
    std::getline(ifs, str);

    if (ifs.eof())
    {
      eof = true;
      break;
    }

    if (str.empty())
      continue;

    PositionData data;
    std::istringstream stream(str);
    std::getline(stream, data.sfen, ',');
    std::string value;
    std::getline(stream, value, ',');
    if (value.empty())
      data.value = 0;
    else
      data.value = std::stoi(value);
    position_list.push_back(data);
    ++num;
  }
  return num;
}

void Reinforcer::add_param(const Gradient &param)
{
#ifdef _OPENMP
#pragma omp parallel
#endif
  {
#ifdef _OPENMP
#pragma omp for
#endif
    for (int k = 0; k < kBoardSquare; ++k)
    {
      Square king = static_cast<Square>(k);
      for (int i = 0; i < Eval::kFEEnd; ++i)
      {
        for (int j = 0; j < Eval::kFEEnd; ++j)
        {
          if (i == j)
            continue;

          KppIndex kpp_index(king, i, j);
          if (param.kpp[kpp_index.king][kpp_index.i][kpp_index.j] > 0)
          {
            if (Eval::KPP[king][i][j] < INT16_MAX)
              Eval::KPP[king][i][j] += 1;
          }
          else if (param.kpp[kpp_index.king][kpp_index.i][kpp_index.j] < 0)
          {
            if (Eval::KPP[king][i][j] > INT16_MIN)
              Eval::KPP[king][i][j] -= 1;
          }
        }
      }
    }

#ifdef _OPENMP
#pragma omp for
#endif
    for (int k0 = 0; k0 < kBoardSquare; ++k0)
    {
      Square king0 = static_cast<Square>(k0);
      for (int k1 = 0; k1 < kBoardSquare; ++k1)
      {
        Square king1 = static_cast<Square>(k1);

        for (int i = 1; i < Eval::kFEEnd; ++i)
        {
          KingPosition ksq0(king0);
          BoardPosition ksq1(king1);
          int pi = i;
          if (ksq0.swap)
          {
            ksq1.x = kFile9 - ksq1.x;
            pi = inverse_file_kpp_index(pi);
          }
          else if (ksq0.x == kFile5 && ksq1.x > kFile5)
          {
            ksq1.x = kFile9 - ksq1.x;
            pi = lower_file_kpp_index(pi);
          }
          else if (ksq0.x == kFile5 && ksq1.x == kFile5)
          {
            pi = lower_file_kpp_index(pi);
          }

          if (param.kkp[ksq0.square()][ksq1.square()][pi] > 0)
          {
            if (Eval::KKP[k0][k1][i] < INT16_MAX)
              Eval::KKP[k0][k1][i] += 1;
          }
          else if (param.kkp[ksq0.square()][ksq1.square()][pi] < 0)
          {
            if (Eval::KKP[k0][k1][i] > INT16_MIN)
              Eval::KKP[k0][k1][i] -= 1;
          }

          if (param.kkpt[ksq0.square()][ksq1.square()][pi][0] > 0)
          {
            if (Eval::KKPT[k0][k1][i][0] < INT16_MAX)
              Eval::KKPT[k0][k1][i][0] += 1;
          }
          else if (param.kkpt[ksq0.square()][ksq1.square()][pi][0] < 0)
          {
            if (Eval::KKPT[k0][k1][i][0] > INT16_MIN)
              Eval::KKPT[k0][k1][i][0] -= 1;
          }

          if (param.kkpt[ksq0.square()][ksq1.square()][pi][1] > 0)
          {
            if (Eval::KKPT[k0][k1][i][1] < INT16_MAX)
              Eval::KKPT[k0][k1][i][1] += 1;
          }
          else if (param.kkpt[ksq0.square()][ksq1.square()][pi][1] < 0)
          {
            if (Eval::KKPT[k0][k1][i][1] > INT16_MIN)
              Eval::KKPT[k0][k1][i][1] -= 1;
          }
        }
      }
    }
  }
}

Value
Reinforcer::search(Position &pos, Depth depth)
{
  SearchStack stack[kMaxPly + 4];
  SearchStack *ss = stack + 2;
  std::memset(ss - 2, 0, 5 * sizeof(SearchStack));

  StateInfo new_info;
  Thread *thread = pos.this_thread();

  thread->history_.clear();
  thread->counter_moves_.clear();
  thread->counter_move_history_.clear();
  thread->from_to_.clear();
  thread->pv_index_ = 0;
  thread->calls_count_ = 0;
  thread->max_ply_ = 0;
  thread->root_depth_ = kDepthZero;

  thread->root_moves_.clear();
  for (auto &m : MoveList<kLegalForSearch>(pos))
    thread->root_moves_.push_back(Search::RootMove(m.move));

  return Search::search(pos, ss, -kValueInfinite, kValueInfinite, depth);
}

