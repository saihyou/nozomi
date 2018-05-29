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
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <omp.h>
#include <random>
#include <iterator>
#include "reinforcer.h"
#include "transposition_table.h"
#include "thread.h"
#include "usi.h"

struct LearnEval
{
  LearnFloatingPoint kpp[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd];
  LearnFloatingPoint kkpt[kBoardSquare][kBoardSquare][Eval::kFEEnd][kNumberOfColor];

  bool load(const char *file_name);
  void save(const char *file_name);
  void clear();
};

void
LearnEval::save(const char *file_name)
{
  std::FILE *fp = std::fopen(file_name, "wb");
  std::fwrite(this, 1, sizeof(LearnEval), fp);
  std::fclose(fp);
}

bool
LearnEval::load(const char *file_name)
{
  std::FILE *fp = std::fopen(file_name, "rb");
  if (fp == nullptr)
    return false;
  std::fread(this, 1, sizeof(LearnEval), fp);
  std::fclose(fp);
  return true;
}

void
LearnEval::clear()
{
  std::memset(this, 0, sizeof(LearnEval));
}

LearnEval *g_floating_eval;
LearnEval *g_before_update;
double g_learning_rate;

void
store_to_normal_value(const LearnEval &eval)
{
  for (int k = 0; k < kBoardSquare; ++k)
  {
    for (int p0 = 0; p0 < Eval::kFEEnd; ++p0)
    {
      for (int p1 = 0; p1 < Eval::kFEEnd; ++p1)
      {
        Eval::KPP[k][p0][p1] = static_cast<int16_t>(eval.kpp[k][p0][p1]);
      }
    }
  }

  for (int k0 = 0; k0 < kBoardSquare; ++k0)
  {
    for (int k1 = 0; k1 < kBoardSquare; ++k1)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        Eval::KKPT[k0][k1][p][0] = static_cast<int16_t>(eval.kkpt[k0][k1][p][0]);
        Eval::KKPT[k0][k1][p][1] = static_cast<int16_t>(eval.kkpt[k0][k1][p][1]);
      }
    }
  }
}

void
load_to_floating_value(LearnEval &eval)
{
  for (int k = 0; k < kBoardSquare; ++k)
  {
    for (int p0 = 0; p0 < Eval::kFEEnd; ++p0)
    {
      for (int p1 = 0; p1 < Eval::kFEEnd; ++p1)
      {
        eval.kpp[k][p0][p1] = static_cast<LearnFloatingPoint>(Eval::KPP[k][p0][p1]);
      }
    }
  }

  for (int k0 = 0; k0 < kBoardSquare; ++k0)
  {
    for (int k1 = 0; k1 < kBoardSquare; ++k1)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        eval.kkpt[k0][k1][p][0] = static_cast<LearnFloatingPoint>(Eval::KKPT[k0][k1][p][0]);
        eval.kkpt[k0][k1][p][1] = static_cast<LearnFloatingPoint>(Eval::KKPT[k0][k1][p][1]);
      }
    }
  }
}

namespace
{
LearnFloatingPoint
tanh(int32_t v)
{
  return static_cast<LearnFloatingPoint>(std::tanh(static_cast<double>(v) / 706.0));
}

LearnFloatingPoint
dtanh(int32_t v)
{
  return static_cast<LearnFloatingPoint>(1.0 - tanh(v) * tanh(v));
}

void
add_atmic_float(std::atomic<LearnFloatingPoint> &target, LearnFloatingPoint param)
{
  LearnFloatingPoint old = target.load(std::memory_order_consume);
  LearnFloatingPoint desired = old + param;
  while
  (
    !target.compare_exchange_weak
    (
      old,
      desired,
      std::memory_order_release,
      std::memory_order_consume
    )
  )
    desired = old + param;
}
};

void
Gradient::increment(const Position &pos, LearnFloatingPoint delta)
{
  const Square bk = pos.square_king(kBlack);
  const Square wk = pos.square_king(kWhite);
  const Eval::KPPIndex *list_black = pos.black_kpp_list();
  const Eval::KPPIndex *list_white = pos.white_kpp_list();

  for (int i = 0; i < Eval::kListNum; ++i)
  {
    const int k0 = list_black[i];
    const int k1 = list_white[i];
    for (int j = 0; j < i; ++j)
    {
      const int l0 = list_black[j];
      const int l1 = list_white[j];
      KppIndex bi(bk, k0, l0);
      add_atmic_float(kpp[bi.king][bi.i][bi.j], delta);
      kpp_count[bi.king][bi.i][bi.j].fetch_add(1);
      KppIndex wi(Eval::inverse(wk), k1, l1);
      add_atmic_float(kpp[wi.king][wi.i][wi.j], -delta);
      kpp_count[wi.king][wi.i][wi.j].fetch_add(1);
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
    add_atmic_float(kkpt[bksq.square()][wksq.square()][pi][pos.side_to_move()], delta);
    kkpt_count[bksq.square()][wksq.square()][pi][pos.side_to_move()].fetch_add(1);

    Square inv_bk = Eval::inverse(wk);
    Square inv_wk = Eval::inverse(bk);
    int inv_k0 = inverse_black_white_kpp_index(k0);
    KingPosition inv_bksq(inv_bk);
    BoardPosition inv_wksq(inv_wk);
    int inv_pi = inv_k0;
    if (inv_bksq.swap)
    {
      inv_wksq.x = kFile9 - inv_wksq.x;
      inv_pi = inverse_file_kpp_index(inv_pi);
    }
    else if (inv_bksq.x == kFile5 && inv_wksq.x > kFile5)
    {
      inv_wksq.x = kFile9 - inv_wksq.x;
      inv_pi = lower_file_kpp_index(inv_pi);
    }
    else if (inv_bksq.x == kFile5 && inv_wksq.x == kFile5)
    {
      inv_pi = lower_file_kpp_index(inv_pi);
    }

    add_atmic_float(kkpt[inv_bksq.square()][inv_wksq.square()][inv_pi][~pos.side_to_move()], -delta);
    kkpt_count[inv_bksq.square()][inv_wksq.square()][inv_pi][~pos.side_to_move()].fetch_add(1);
  }
}

void
Gradient::clear()
{
  std::memset(this, 0, sizeof(*this));
}

void
Reinforcer::reinforce(std::istringstream &is)
{
  std::string record_file_name;
  int num_threads = 1;
  std::string type;

  is >> type;
  is >> record_file_name;
  is >> num_threads;
  is >> g_learning_rate;
  if (g_learning_rate < 0.000001 || g_learning_rate > 1)
    g_learning_rate = 0.001;
  batch_size_ = 100000;

  g_floating_eval = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_floating_eval == nullptr)
    return;
  g_before_update = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_before_update == nullptr)
  {
    std::free(g_floating_eval);
    return;
  }

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

  update_param(record_file_name, num_threads);

  std::free(g_floating_eval);
  std::free(g_before_update);
}

// 局面ファイルから勾配を計算し、評価関数パラメーターを更新する
void
Reinforcer::update_param(const std::string &record_file_name, int num_threads)
{
  load_param();
  win_diff_ = 0;
  value_diff_ = 0;
  gradient_ = std::unique_ptr<Gradient>(new Gradient);
  gradient_->clear();

  std::vector<PositionData> position_list;
  std::ifstream ifs(record_file_name.c_str());
  bool eof = false;
  int count = 0;

  while (!eof)
  {
    auto num = read_file(ifs, position_list, batch_size_, eof);
    if (position_list.empty() || num < batch_size_)
      break;

    TT.clear();
    compute_gradient(position_list);

    position_list.clear();
    add_param(*gradient_);
    std::cout << "count : " << ++count << std::endl;
    std::cout << std::sqrt(static_cast<double>(win_diff_) / batch_size_) << std::endl;
    std::cout << std::sqrt(static_cast<double>(value_diff_) / batch_size_) << std::endl;
    win_diff_ = 0;
    value_diff_ = 0;
    gradient_->clear();
    store_to_normal_value(*g_floating_eval);
    if (count % 100 == 0)
      save_param();
  }
  save_param();
}



// 勾配を計算する
void
Reinforcer::compute_gradient(std::vector<PositionData> &position_list)
{
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

    if (data.win == kNoColor)
      continue;

    positions_[thread_id].set(data.sfen, Threads[thread_id + 1]);
    
    SearchStack stack[20];
    Move pv[kMaxPly + 1];
    SearchStack *ss = stack + 2;
    std::memset(ss - 2, 0, 5 * sizeof(SearchStack));
    ss->pv = pv;
    StateInfo state[20];
    StateInfo next_state;
    Color root_color = positions_[thread_id].side_to_move();
#if 0
    Move next_move = USI::to_move(positions_[thread_id], data.next_move);
    if (next_move != kMoveNone)
      positions_[thread_id].do_move(next_move, next_state);
#endif
    Value quiet_value =
      Search::qsearch(positions_[thread_id], ss, -kValueInfinite, kValueInfinite);
#if 0
    if (next_move != kMoveNone)
      quiet_value = -quiet_value;
#endif
    // 局面データに記録した評価値から換算した勝率と局面の静止探索の評価値から換算した勝率の差をとる
    int32_t data_value = (root_color == kWhite) ? -data.value : data.value;
    int32_t int_value = (root_color == kWhite) ? -quiet_value : quiet_value;
    LearnFloatingPoint delta_value = tanh(data_value) - tanh(int_value);
    delta_value = dtanh(int_value) * delta_value;

    // 最終的な勝敗と局面の静止探索の評価値から換算した勝率の差をとる
    LearnFloatingPoint delta_win = 0.0;
    if (data.win == kBlack)
      delta_win = 1.0 - tanh(int_value);
    else if (data.win == kWhite)
      delta_win = -1.0 - tanh(int_value);
    delta_win = dtanh(int_value) * delta_win;

    int j = 0;
    while (pv[j] != kMoveNone)
    {
      positions_[thread_id].do_move(pv[j], state[j]);
      ++j;
    }

    LearnFloatingPoint delta = delta_win + delta_value;
    gradient_->increment(positions_[thread_id], delta);

#ifdef _OPENMP
#pragma omp critical
#endif
    {
      win_diff_ += delta_win * delta_win;
      value_diff_ += delta_value * delta_value;
    }
  }
}

// 局面データから読み込む
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
      data.value = kValueZero;
    else
      data.value = static_cast<Value>(std::stoi(value));

    std::string win;
    std::getline(stream, win, ',');
    if (win == "b")
      data.win = kBlack;
    else if (win == "w")
      data.win = kWhite;
    else
      data.win = kNumberOfColor;

    std::getline(stream, data.next_move, ',');

    position_list.push_back(data);
    ++num;
  }
  return num;
}

void
update(LearnFloatingPoint grad, LearnFloatingPoint *param, LearnFloatingPoint *before, int batch_size)
{
#if 0
  if (grad > 0.0)
    *param += 1.0;
  else if (grad < 0.0)
    *param -= 1.0;
#else
  const double l = g_learning_rate * 0.00001;
  double p = 0.0;
  if (batch_size > 0 && (grad > 0.0 || grad < 0.0))
    p = (grad / static_cast<LearnFloatingPoint>(batch_size));

  if (p > 0 || p < 0)
  {
    LearnFloatingPoint v = static_cast<LearnFloatingPoint>(p * g_learning_rate + 0.9 * *before);
    *param += v;
    *before = v;
    *param = *param / (1 + l);
  }
#endif
}

// 勾配からパラメーターを更新する
void
Reinforcer::add_param(const Gradient &param)
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
          LearnFloatingPoint grad = param.kpp[kpp_index.king][kpp_index.i][kpp_index.j].load();
          int count = param.kpp_count[kpp_index.king][kpp_index.i][kpp_index.j].load();
          update(grad, &g_floating_eval->kpp[king][i][j], &g_before_update->kpp[king][i][j], count);
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
          LearnFloatingPoint grad = param.kkpt[ksq0.square()][ksq1.square()][pi][0].load();
          int count = param.kkpt_count[ksq0.square()][ksq1.square()][pi][0].load();
          update(grad, &g_floating_eval->kkpt[k0][k1][i][0], &g_before_update->kkpt[k0][k1][i][0], count);
          
          grad = param.kkpt[ksq0.square()][ksq1.square()][pi][1].load();
          count = param.kkpt_count[ksq0.square()][ksq1.square()][pi][1].load();
          update(grad, &g_floating_eval->kkpt[k0][k1][i][1], &g_before_update->kkpt[k0][k1][i][1], count);
        }
      }
    }
  }
}

void
Reinforcer::load_param()
{
  if (g_floating_eval->load("value_float.bin"))
  {
    store_to_normal_value(*g_floating_eval);
    if (!g_before_update->load("before_update.bin"))
      g_before_update->clear();
    return;
  }

  std::ifstream ifs2("new_fv2.bin", std::ios::in | std::ios::binary);
  if (ifs2)
  {
    ifs2.read(reinterpret_cast<char *>(Eval::KPP), sizeof(Eval::KPP));
    ifs2.read(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
    ifs2.close();
  }
  load_to_floating_value(*g_floating_eval);
  g_before_update->clear();
}

void
Reinforcer::save_param()
{
  std::ofstream fs("new_fv2.bin", std::ios::binary);
  fs.write(reinterpret_cast<char *>(Eval::KPP), sizeof(Eval::KPP));
  fs.write(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
  fs.close();
  g_floating_eval->save("value_float.bin");
  g_before_update->save("before_update.bin");
}
