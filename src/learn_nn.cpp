#include "learn_nn.h"
#include <omp.h>
#include <cmath>
#include <random>
#include <vector>
#include "position.h"
#include "search.h"
#include "thread.h"
#include "transposition_table.h"
#include "kifu_maker.h"
#include "usi.h"

namespace {

bool Exist(const std::string& path) {
  std::ifstream ifs(path);
  return ifs.is_open();
}

float Tanh(Value v) {
  return static_cast<float>(
      std::tanh(static_cast<float>(v) / NnNetworkLearner::kNormalizeConstant));
}

float Dtanh(float v) {
  return static_cast<float>(1.0 - std::tanh(static_cast<double>(v)) *
                                      std::tanh(static_cast<double>(v)));
}

}  // namespace


Example MakeExample(Position& pos, Color root_color, Value kifu_value,
                    Color win) {
  Example example;
  if (pos.side_to_move() == kBlack) {
    std::memcpy(example.list[0], pos.black_kpp_list(),
                sizeof(Eval::KPPIndex) * eval::kKpListLength);
    std::memcpy(example.list[1], pos.white_kpp_list(),
                sizeof(Eval::KPPIndex) * eval::kKpListLength);
    example.king[0] = pos.square_king(kBlack);
    example.king[1] = Eval::inverse(pos.square_king(kWhite));
    example.material = Tanh(static_cast<Value>(pos.material()));
  } else {
    std::memcpy(example.list[0], pos.white_kpp_list(),
                sizeof(Eval::KPPIndex) * eval::kKpListLength);
    std::memcpy(example.list[1], pos.black_kpp_list(),
                sizeof(Eval::KPPIndex) * eval::kKpListLength);
    example.king[0] = Eval::inverse(pos.square_king(kWhite));
    example.king[1] = pos.square_king(kBlack);
    example.material = Tanh(static_cast<Value>(-pos.material()));
  }

  if (pos.side_to_move() == root_color) {
    example.value = Tanh(kifu_value);
  } else {
    example.value = Tanh(-kifu_value);
  }

  example.win = 0.0f;
  if (pos.side_to_move() == win) {
    example.win = 1.0f;
  } else if (pos.side_to_move() == ~win) {
    example.win = -1.0f;
  }
  return example;
}

void NnLearner::MakeBatch(std::vector<PositionData>& batch) {
  examples_.resize(batch.size());

  TT.Clear();
  for (int i = 0; i < num_threads_; i++) {
    Threads[i + 1]->Clear();
  }

#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < static_cast<int>(batch.size()); i++) {
    PositionData& data = batch[i];
    int thread_id = 0;
#ifdef _OPENMP
    thread_id = omp_get_thread_num();
#endif

    if (data.win == kNoColor) continue;

    Position& pos = positions_[thread_id];
    pos.set(data.sfen, Threads[thread_id + 1]);

    SearchStack stack[50];
    SearchStack* ss = stack + 7;
    std::memset(ss - 7, 0, 10 * sizeof(SearchStack));
    for (int i = 7; i > 0; --i)
      (ss - i)->continuation_history =
          &Threads[thread_id + 1]
               ->continuation_history_[0][0][kPieceNone][0];  // Use as sentinel

    Move pv[kMaxPly + 1];
    ss->pv = pv;

    StateInfo next_state;
    Color root_color = pos.side_to_move();
    Search::qsearch(pos, ss, -kValueInfinite, kValueInfinite);

    StateInfo state[20];
    int j = 0;
    while (pv[j] != kMoveNone) {
      pos.do_move(pv[j], state[j]);
      ++j;
    }
    examples_[i] = MakeExample(pos, root_color, data.value, data.win);
  }
}

void NnLearner::CalcurateGradient(const std::vector<Example>& batch) {
  feature_.Forward(batch);
  auto network_output =
      network_.Forward(static_cast<int>(batch.size()), feature_.output());
  float win_diff = 0.0f;
  float value_diff = 0.0f;
  for (int i = 0; i < static_cast<int>(batch.size()); i++) {
    float output = std::min(1.0f, std::max(-1.0f, std::tanh(network_output[i]) + batch[i].material));
    float delta_value = batch[i].value - output;
    delta_value = Dtanh(output) * delta_value;
    float delta_win = batch[i].win - output;
    delta_win = Dtanh(output) * delta_win;
    gradients_[i] = -(delta_value + delta_win);
    win_diff += std::pow((batch[i].win - output), 2.0f);
    value_diff += std::pow((batch[i].value - output), 2.0f);
  }
  std::cout << "win diff : " << win_diff / batch_size_ << std::endl;
  std::cout << "value diff : " << value_diff / batch_size_ << std::endl;
}

void NnLearner::UpdateParameters(const std::vector<Example>& batch) {
  float lr = learning_rate_;
  auto grad = network_.Backward(batch_size_, lr, gradients_,
                                feature_.output());
  feature_.Backward(batch, lr, grad);
}

void NnLearner::Learn(std::istringstream& is) {
  num_threads_ = 1;

  is >> train_file_name_;
  is >> num_threads_;
  is >> learning_rate_;
  is >> batch_size_;
#if 0
  is >> valid_file_name_;
#endif
  TT.Clear();
  Search::Limits.infinite = 1;
  Search::Signals.stop_on_ponder_hit = false;
  Search::Signals.stop = false;

#ifndef _OPENMP
  num_threads_ = 1;
#endif

  for (int i = 0; i < num_threads_; ++i) {
    positions_.push_back(Position());
    Threads.push_back(new Thread);
  }

#ifdef _OPENMP
  omp_set_num_threads(num_threads_);
#endif
  Initialize();
  LearnMain();
}

void NnLearner::Initialize() {
  if (Exist("nn_feature2.bin")) {
    eval::g_nnfeature.ReadParameters("nn_feature2.bin");
    feature_.LoadParameters();
  } else if (Exist("nn_feature.bin")) {
    // ‚±‚Ìê‡‚Í‚·‚Å‚É“Ç‚Ýž‚ÝÏ‚Ý
    feature_.LoadParameters();
  } else {
    feature_.InitializeParameters();
  }

  if (Exist("nn_network2.bin")) {
    eval::g_network.ReadParameters("nn_network2.bin");
    network_.LoadParameters();
  } else if (Exist("nn_network.bin")) {
    network_.LoadParameters();
  } else {
    network_.InitializeParameters();
  }
  gradients_.resize(batch_size_);
}

void NnLearner::LearnMain() {
  std::ifstream ifs(train_file_name_.c_str());
  bool eof = false;
  count_ = 0;
  while (!eof) {
    std::cout << "Count : " << count_ << std::endl;
    auto batch_data = ReadSfenFile(ifs, eof);
    if (static_cast<int>(batch_data.size()) == batch_size_) {
      MakeBatch(batch_data);
      CalcurateGradient(examples_);
      UpdateParameters(examples_);
      feature_.QuantizeParameters();
      network_.QuantizeParameters();
      count_++;
    }
  }
#if 0
  double score = Validate();
  std::cout << "Score : " << score << std::endl;
#endif
  feature_.OutputParamesters("nn_feature2.bin");
  network_.OutputParamesters("nn_network2.bin");
}

std::vector<PositionData> NnLearner::ReadSfenFile(std::ifstream& ifs,
                                                  bool& eof) {
  std::vector<PositionData> list;
  std::string str;
  int num = 0;
  eof = false;
  while (num < batch_size_) {
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

    list.push_back(data);
    ++num;
  }

  return list;
}

double NnLearner::Validate() const {
  std::ifstream ifs(valid_file_name_);
  int hit = 0;
  int total = 0;
  while (!ifs.eof()) {
    std::string sfen;
    std::getline(ifs, sfen);
    if (sfen.empty()) continue;

    std::istringstream is(sfen);
    std::string token;
    is >> token;
    is >> token;

    Threads[0]->Clear();
    TT.Clear();
    Search::StateStackPtr state =
        Search::StateStackPtr(new std::stack<StateInfo>());
    Position pos;
    pos.set("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1",
            Threads[0]);

    while (is >> token) {
      Move kifu_move = USI::to_move(pos, token);
      if (kifu_move == kMoveNone) break;
      state->push(StateInfo());
      if (!KifuMaker::search(pos, 1, Depth(3))) {
        break;
      }
      Thread* thread = pos.this_thread();
      auto move = thread->root_moves_[0].pv[0];
      if (move == kifu_move) {
        hit++;
      }
      total++;
      state->push(StateInfo());
      pos.do_move(kifu_move, state->top());
    }
  }
  ifs.close();
  return static_cast<double>(hit) / static_cast<double>(total);
}
