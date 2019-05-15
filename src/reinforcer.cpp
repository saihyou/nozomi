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

#include "reinforcer.h"
#include <omp.h>
#include <iterator>
#include <random>
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"

#define USE_ADAM
#define USE_QSEARCH

struct LearnEval {
  LearnFloatingPoint kppt[kBoardSquare][Eval::kFEEnd][Eval::kFEEnd]
                         [kNumberOfColor];
  LearnFloatingPoint kkpt[kBoardSquare][kBoardSquare][Eval::kFEEnd]
                         [kNumberOfColor];

  bool load(const char *file_name);
  void save(const char *file_name);
  void clear();
};

void LearnEval::save(const char *file_name) {
  std::FILE *fp = std::fopen(file_name, "wb");
  std::fwrite(this, 1, sizeof(LearnEval), fp);
  std::fclose(fp);
}

bool LearnEval::load(const char *file_name) {
  std::FILE *fp = std::fopen(file_name, "rb");
  if (fp == nullptr) return false;
  std::fread(this, 1, sizeof(LearnEval), fp);
  std::fclose(fp);
  return true;
}

void LearnEval::clear() { std::memset(this, 0, sizeof(LearnEval)); }

LearnEval *g_floating_eval;
#ifdef USE_ADAM
LearnEval *g_learning_m;
LearnEval *g_learning_v;
#else
LearnEval *g_learning_param;
#endif
double g_learning_rate;

constexpr double kFloatDivider = 1024.0;
constexpr int kBatchSize = 200000;

void store_to_normal_value(const LearnEval &eval) {
  for (int k = 0; k < kBoardSquare; ++k) {
    for (int p0 = 0; p0 < Eval::kFEEnd; ++p0) {
      for (int p1 = 0; p1 < Eval::kFEEnd; ++p1) {
        Eval::KPPT[k][p0][p1][0] =
            static_cast<int16_t>(eval.kppt[k][p0][p1][0] * kFloatDivider);
        Eval::KPPT[k][p0][p1][1] =
            static_cast<int16_t>(eval.kppt[k][p0][p1][1] * kFloatDivider);
      }
    }
  }

  for (int k0 = 0; k0 < kBoardSquare; ++k0) {
    for (int k1 = 0; k1 < kBoardSquare; ++k1) {
      for (int p = 0; p < Eval::kFEEnd; ++p) {
        Eval::KKPT[k0][k1][p][0] =
            static_cast<int16_t>(eval.kkpt[k0][k1][p][0] * kFloatDivider);
        Eval::KKPT[k0][k1][p][1] =
            static_cast<int16_t>(eval.kkpt[k0][k1][p][1] * kFloatDivider);
      }
    }
  }
}

void load_to_floating_value(LearnEval &eval) {
  for (int k = 0; k < kBoardSquare; ++k) {
    for (int p0 = 0; p0 < Eval::kFEEnd; ++p0) {
      for (int p1 = 0; p1 < Eval::kFEEnd; ++p1) {
        eval.kppt[k][p0][p1][0] =
            static_cast<LearnFloatingPoint>(Eval::KPPT[k][p0][p1][0] / kFloatDivider);
        eval.kppt[k][p0][p1][1] =
            static_cast<LearnFloatingPoint>(Eval::KPPT[k][p0][p1][1] / kFloatDivider);
      }
    }
  }

  for (int k0 = 0; k0 < kBoardSquare; ++k0) {
    for (int k1 = 0; k1 < kBoardSquare; ++k1) {
      for (int p = 0; p < Eval::kFEEnd; ++p) {
        eval.kkpt[k0][k1][p][0] =
            static_cast<LearnFloatingPoint>(Eval::KKPT[k0][k1][p][0] / kFloatDivider);
        eval.kkpt[k0][k1][p][1] =
            static_cast<LearnFloatingPoint>(Eval::KKPT[k0][k1][p][1] / kFloatDivider);
      }
    }
  }
}

namespace {
LearnFloatingPoint tanh(int32_t v) {
  return static_cast<LearnFloatingPoint>(
      std::tanh(static_cast<double>(v) / 800.0));
}

LearnFloatingPoint dtanh(int32_t v) {
  return static_cast<LearnFloatingPoint>(1.0 - tanh(v) * tanh(v));
}

void add_atmic_float(std::atomic<LearnFloatingPoint> &target,
                     LearnFloatingPoint param) {
  LearnFloatingPoint old = target.load(std::memory_order_consume);
  LearnFloatingPoint desired = old + param;
  while (!target.compare_exchange_weak(old, desired, std::memory_order_release,
                                       std::memory_order_consume))
    desired = old + param;
}
};  // namespace

void Gradient::increment(const Position &pos, LearnFloatingPoint delta,
                         bool kpp) {
  const Square bk = pos.square_king(kBlack);
  const Square wk = pos.square_king(kWhite);
  const Eval::KPPIndex *list_black = pos.black_kpp_list();
  const Eval::KPPIndex *list_white = pos.white_kpp_list();
  const Color side_to_move = pos.side_to_move();

  for (int i = 0; i < Eval::kListNum; ++i) {
    const int k0 = list_black[i];
    const int k1 = list_white[i];
    for (int j = 0; j < i; ++j) {
      const int l0 = list_black[j];
      const int l1 = list_white[j];
      KppIndex bi(bk, k0, l0);
      add_atmic_float(kppt[bi.king][bi.i][bi.j][side_to_move], delta);
      kppt_count[bi.king][bi.i][bi.j][side_to_move].fetch_add(1);
      if (kpp) {
        add_atmic_float(kppt[bi.king][bi.i][bi.j][~side_to_move], delta);
        kppt_count[bi.king][bi.i][bi.j][~side_to_move].fetch_add(1);
      }
      KppIndex wi(Eval::inverse(wk), k1, l1);
      add_atmic_float(kppt[wi.king][wi.i][wi.j][~side_to_move], -delta);
      kppt_count[wi.king][wi.i][wi.j][~side_to_move].fetch_add(1);
      if (kpp) {
        add_atmic_float(kppt[wi.king][wi.i][wi.j][side_to_move], -delta);
        kppt_count[wi.king][wi.i][wi.j][side_to_move].fetch_add(1);
      }
    }
    KingPosition bksq(bk);
    BoardPosition wksq(wk);
    int pi = k0;
    if (bksq.swap) {
      wksq.x = kFile9 - wksq.x;
      pi = inverse_file_kpp_index(pi);
    } else if (bksq.x == kFile5 && wksq.x > kFile5) {
      wksq.x = kFile9 - wksq.x;
      pi = lower_file_kpp_index(pi);
    } else if (bksq.x == kFile5 && wksq.x == kFile5) {
      pi = lower_file_kpp_index(pi);
    }
    add_atmic_float(kkpt[bksq.square()][wksq.square()][pi][pos.side_to_move()],
                    delta);
    kkpt_count[bksq.square()][wksq.square()][pi][pos.side_to_move()].fetch_add(
        1);

    Square inv_bk = Eval::inverse(wk);
    Square inv_wk = Eval::inverse(bk);
    KingPosition inv_bksq(inv_bk);
    BoardPosition inv_wksq(inv_wk);
    int inv_pi = k1;
    if (inv_bksq.swap) {
      inv_wksq.x = kFile9 - inv_wksq.x;
      inv_pi = inverse_file_kpp_index(inv_pi);
    } else if (inv_bksq.x == kFile5 && inv_wksq.x > kFile5) {
      inv_wksq.x = kFile9 - inv_wksq.x;
      inv_pi = lower_file_kpp_index(inv_pi);
    } else if (inv_bksq.x == kFile5 && inv_wksq.x == kFile5) {
      inv_pi = lower_file_kpp_index(inv_pi);
    }

    add_atmic_float(
        kkpt[inv_bksq.square()][inv_wksq.square()][inv_pi][~pos.side_to_move()],
        -delta);
    kkpt_count[inv_bksq.square()][inv_wksq.square()][inv_pi]
              [~pos.side_to_move()]
                  .fetch_add(1);
  }
}

void Gradient::clear() { std::memset(this, 0, sizeof(*this)); }

void Reinforcer::reinforce(std::istringstream &is) {
  std::string record_file_name;
  int num_threads = 1;
  std::string type;

  is >> type;
  is >> record_file_name;
  is >> num_threads;
  is >> g_learning_rate;
  if (g_learning_rate < 0.000001 || g_learning_rate > 1)
    g_learning_rate = 0.001;
  batch_size_ = kBatchSize;

  kpp_ = false;
  if (type == "kpp") kpp_ = true;

  g_floating_eval = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_floating_eval == nullptr) return;
#ifdef USE_ADAM
  g_learning_m = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_learning_m == nullptr) {
    std::free(g_floating_eval);
    return;
  }
  g_learning_v = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_learning_v == nullptr) {
    std::free(g_learning_m);
    std::free(g_floating_eval);
    return;
  }
#else
  g_learning_param =
      static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_learning_param == nullptr) {
    std::free(g_floating_eval);
    return;
  }
#endif

  TT.Clear();
  Search::Limits.infinite = 1;
  Search::Signals.stop_on_ponder_hit = false;
  Search::Signals.stop = false;

#ifndef _OPENMP
  num_threads = 1;
#endif

  for (int i = 0; i < num_threads; ++i) {
    positions_.push_back(Position());
    Threads.push_back(new Thread);
  }

#ifdef _OPENMP
  omp_set_num_threads(num_threads);
#endif

  update_param(record_file_name, num_threads);

  std::free(g_floating_eval);
#ifdef USE_ADAM
  std::free(g_learning_m);
  std::free(g_learning_v);
#else
  std::free(g_learning_param);
#endif
}

// 局面ファイルから勾配を計算し、評価関数パラメーターを更新する
void Reinforcer::update_param(const std::string &record_file_name,
                              int num_threads) {
  load_param();
  win_diff_ = 0;
  value_diff_ = 0;
  gradient_ = std::unique_ptr<Gradient>(new Gradient);
  gradient_->clear();

  std::vector<PositionData> position_list;
  std::ifstream ifs(record_file_name.c_str());
  bool eof = false;
  int count = 0;

  while (!eof) {
    auto num = read_file(ifs, position_list, batch_size_, eof);
    if (position_list.empty() || num < batch_size_) break;

    TT.Clear();
    for (int i = 1; i < num_threads + 1; ++i) Threads[i]->Clear();
    compute_gradient(position_list);

    position_list.clear();
    add_param(*gradient_);
    std::cout << "count : " << ++count << std::endl;
    std::cout << std::sqrt(static_cast<double>(win_diff_) / batch_size_)
              << std::endl;
    std::cout << std::sqrt(static_cast<double>(value_diff_) / batch_size_)
              << std::endl;
    win_diff_ = 0;
    value_diff_ = 0;
    gradient_->clear();
    store_to_normal_value(*g_floating_eval);
  }
  save_param();
}

// 勾配を計算する
void Reinforcer::compute_gradient(std::vector<PositionData> &position_list) {
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
  for (int i = 0; i < static_cast<int>(position_list.size()); ++i) {
    PositionData &data = position_list[i];
    int thread_id = 0;
#ifdef _OPENMP
    thread_id = omp_get_thread_num();
#endif

    if (data.win == kNoColor) continue;

    positions_[thread_id].set(data.sfen, Threads[thread_id + 1]);

    SearchStack stack[30];
    SearchStack *ss = stack + 7;
    std::memset(ss - 7, 0, 10 * sizeof(SearchStack));
    for (int i = 7; i > 0; --i)
      (ss - i)->continuation_history =
          &Threads[thread_id + 1]
               ->continuation_history_[kPieceNone][0];  // Use as sentinel
#ifdef USE_QSEARCH
    Move pv[kMaxPly + 1];
    ss->pv = pv;
#endif
    StateInfo next_state;
    Color root_color = positions_[thread_id].side_to_move();
#ifdef USE_QSEARCH
    Value quiet_value = Search::qsearch(positions_[thread_id], ss,
                                        -kValueInfinite, kValueInfinite);
#else
    Value quiet_value = Eval::evaluate(positions_[thread_id], ss);
#endif
    // 局面データに記録した評価値から換算した勝率と局面 の静止探索の評価値から換算した勝率の差をとる
    int32_t data_value = (root_color == kWhite) ? -data.value : data.value;
    int32_t int_value = (root_color == kWhite) ? -quiet_value : quiet_value;
    LearnFloatingPoint delta_value = tanh(data_value) - tanh(int_value);
    delta_value = dtanh(int_value) * delta_value;

    // 最終的な勝敗と局面の静止探索の評価値から換算した勝率の差をとる
    LearnFloatingPoint delta_win = 0.0;
    if (data.win == kBlack)
      delta_win = 1.0f - tanh(int_value);
    else if (data.win == kWhite)
      delta_win = -1.0f - tanh(int_value);
    delta_win = dtanh(int_value) * delta_win;
#ifdef USE_QSEARCH
    StateInfo state[20];
    int j = 0;
    while (pv[j] != kMoveNone) {
      positions_[thread_id].do_move(pv[j], state[j]);
      ++j;
    }
#endif
    LearnFloatingPoint delta = delta_win + delta_value;
    gradient_->increment(positions_[thread_id], delta, kpp_);

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
size_t Reinforcer::read_file(std::ifstream &ifs,
                             std::vector<PositionData> &position_list,
                             size_t num_positions, bool &eof) {
  std::string str;
  size_t num = 0;
  eof = false;
  while (num < num_positions) {
    std::getline(ifs, str);

    if (ifs.eof()) {
      eof = true;
      break;
    }

    if (str.empty()) continue;

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

#ifdef USE_ADAM
void update(LearnFloatingPoint grad, LearnFloatingPoint *param,
            LearnFloatingPoint *m, LearnFloatingPoint *v, int batch_size) {
  const double beta_1 = 0.9;
  const double beta_2 = 0.999;
  const double e = 0.00000001;
  double g = 0.0;
  if (batch_size > 0 && (grad > 0.0 || grad < 0.0))
    g = (grad / static_cast<double>(batch_size));
  else
    return;

  double m_t = beta_1 * *m + (1 - beta_1) * g;
  double v_t = beta_2 * *v + (1 - beta_2) * g * g;
  double m_hat = m_t / (1 - beta_1);
  double v_hat = v_t / (1 - beta_2);
  double w = g_learning_rate * m_hat / (std::sqrt(v_hat) + e);
  *param += static_cast<LearnFloatingPoint>(w);
  *m = static_cast<LearnFloatingPoint>(m_t);
  *v = static_cast<LearnFloatingPoint>(v_t);
#else
void update(LearnFloatingPoint grad, LearnFloatingPoint *param,
            LearnFloatingPoint *w, int batch_size) {
  double p = 0.0;
  if (batch_size > 0 && (grad > 0.0 || grad < 0.0))
    p = (grad / static_cast<LearnFloatingPoint>(batch_size));

  if (p > 0 || p < 0) {
    LearnFloatingPoint v = static_cast<LearnFloatingPoint>(
        p * g_learning_rate + 0.9 * *w);
    *param += v;
    *w = v;
  }
#endif
}

// 勾配からパラメーターを更新する
void Reinforcer::add_param(const Gradient &param) {
#ifdef _OPENMP
#pragma omp parallel
#endif
  {
#ifdef _OPENMP
#pragma omp for
#endif
    for (int k = 0; k < kBoardSquare; ++k) {
      Square king = static_cast<Square>(k);
      for (int i = 0; i < Eval::kFEEnd; ++i) {
        for (int j = 0; j < Eval::kFEEnd; ++j) {
          if (i == j) continue;

          KppIndex kpp_index(king, i, j);
          LearnFloatingPoint grad =
              param.kppt[kpp_index.king][kpp_index.i][kpp_index.j][0].load();
          int count =
              param.kppt_count[kpp_index.king][kpp_index.i][kpp_index.j][0]
                  .load();
#ifdef USE_ADAM
          update(grad, &g_floating_eval->kppt[king][i][j][0],
                 &g_learning_m->kppt[king][i][j][0],
                 &g_learning_v->kppt[king][i][j][0], count);

#else
          update(grad, &g_floating_eval->kppt[king][i][j][0],
                 &g_learning_param->kppt[king][i][j][0], count);
#endif
          grad = param.kppt[kpp_index.king][kpp_index.i][kpp_index.j][1].load();
          count = param.kppt_count[kpp_index.king][kpp_index.i][kpp_index.j][1]
                      .load();
#ifdef USE_ADAM
          update(grad, &g_floating_eval->kppt[king][i][j][1],
                 &g_learning_m->kppt[king][i][j][1],
                 &g_learning_v->kppt[king][i][j][1], count);

#else
          update(grad, &g_floating_eval->kppt[king][i][j][1],
                 &g_learning_param->kppt[king][i][j][1], count);
#endif
        }
      }
    }

#ifdef _OPENMP
#pragma omp for
#endif
    for (int k0 = 0; k0 < kBoardSquare; ++k0) {
      Square king0 = static_cast<Square>(k0);
      for (int k1 = 0; k1 < kBoardSquare; ++k1) {
        Square king1 = static_cast<Square>(k1);

        for (int i = 1; i < Eval::kFEEnd; ++i) {
          KingPosition ksq0(king0);
          BoardPosition ksq1(king1);
          int pi = i;
          if (ksq0.swap) {
            ksq1.x = kFile9 - ksq1.x;
            pi = inverse_file_kpp_index(pi);
          } else if (ksq0.x == kFile5 && ksq1.x > kFile5) {
            ksq1.x = kFile9 - ksq1.x;
            pi = lower_file_kpp_index(pi);
          } else if (ksq0.x == kFile5 && ksq1.x == kFile5) {
            pi = lower_file_kpp_index(pi);
          }
          LearnFloatingPoint grad =
              param.kkpt[ksq0.square()][ksq1.square()][pi][0].load();
          int count =
              param.kkpt_count[ksq0.square()][ksq1.square()][pi][0].load();
#ifdef USE_ADAM
          update(grad, &g_floating_eval->kkpt[k0][k1][i][0],
                 &g_learning_m->kkpt[k0][k1][i][0],
                 &g_learning_v->kkpt[k0][k1][i][0], count);
#else
          update(grad, &g_floating_eval->kkpt[k0][k1][i][0],
                 &g_learning_param->kkpt[k0][k1][i][0], count);
#endif
          grad = param.kkpt[ksq0.square()][ksq1.square()][pi][1].load();
          count = param.kkpt_count[ksq0.square()][ksq1.square()][pi][1].load();
#ifdef USE_ADAM
          update(grad, &g_floating_eval->kkpt[k0][k1][i][1],
                 &g_learning_m->kkpt[k0][k1][i][1],
                 &g_learning_v->kkpt[k0][k1][i][1], count);
#else
          update(grad, &g_floating_eval->kkpt[k0][k1][i][1],
                 &g_learning_param->kkpt[k0][k1][i][1], count);
#endif
        }
      }
    }
  }
}

void Reinforcer::load_param() {
  if (g_floating_eval->load("value_float.bin")) {
    store_to_normal_value(*g_floating_eval);
    return;
  }

  std::ifstream ifs2("new_fv2.bin", std::ios::in | std::ios::binary);
  if (ifs2) {
    ifs2.read(reinterpret_cast<char *>(Eval::KPPT), sizeof(Eval::KPPT));
    ifs2.read(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
    ifs2.close();
  }
  load_to_floating_value(*g_floating_eval);
#ifdef USE_ADAM
  g_learning_m->clear();
  g_learning_v->clear();
#else

#endif
}

void Reinforcer::save_param() {
  std::ofstream fs("new_fv2.bin", std::ios::binary);
  fs.write(reinterpret_cast<char *>(Eval::KPPT), sizeof(Eval::KPPT));
  fs.write(reinterpret_cast<char *>(Eval::KKPT), sizeof(Eval::KKPT));
  fs.close();
  g_floating_eval->save("value_float.bin");
}
