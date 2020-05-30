#include "nn_feature_learner.h"

#include <omp.h>

#include <fstream>
#include <iostream>
#include <random>

#include "learn.h"

NnFeatureLearner::NnFeatureLearner()
    : quantized_bias_(eval::g_nnfeature.bias()),
      quantized_kp_(eval::g_nnfeature.kp()),
      bias_{},
      bias_v_{},
      kp_{},
      kp_v_{},
      kp_diff_{},
      kp_num_{},
      k_diff_{},
      k_num_{},
      p_diff_{},
      p_num_{},
      rel_kp_diff_{},
      rel_kp_num_{} {}

void NnFeatureLearner::Forward(const std::vector<Example>& batch) {
  if (output_.size() < batch.size() * eval::Network::kLayer0Input) {
    output_.resize(batch.size() * eval::Network::kLayer0Input);
    gradient_.resize(batch.size() * eval::Network::kLayer0Input);
  }
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < static_cast<int>(batch.size()); i++) {
    int batch_offset = i * eval::Network::kLayer0Input;
    for (int j = 0; j < 2; j++) {
      int output_offset = batch_offset + j * eval::kFeatureDemention;
      for (int k = 0; k < eval::kFeatureDemention; k++) {
        output_[output_offset + k] = bias_[k];
      }

      for (auto& b : batch[i].list[j]) {
        int offset =
            (batch[i].king[j] * Eval::kFEEnd + b) * eval::kFeatureDemention;
        for (int k = 0; k < eval::kFeatureDemention; k++) {
          output_[output_offset + k] += kp_[offset + k];
        }
      }
    }
  }

#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < static_cast<int>(batch.size()); i++) {
    int batch_offset = i * eval::Network::kLayer0Input;
    for (int j = 0; j < eval::Network::kLayer0Input; j++) {
      int offset = batch_offset + j;
      output_[offset] = std::max(0.0f, std::min(1.0f, output_[offset]));
    }
  }
}

void NnFeatureLearner::Backward(const std::vector<Example> batch,
                                float learning_rate,
                                const std::vector<float>& gradient) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int b = 0; b < static_cast<int>(batch.size()); b++) {
    int batch_offset = eval::Network::kLayer0Input * b;
    for (int i = 0; i < eval::Network::kLayer0Input; i++) {
      int index = batch_offset + i;
      gradient_[index] =
          gradient[index] * (output_[index] > 0.0f) * (output_[index] < 1.0f);
    }
  }

  float g[eval::kFeatureDemention] = {};
  for (int b = 0; b < static_cast<int>(batch.size()); b++) {
    int batch_offset = eval::Network::kLayer0Input * b;
    for (int c = 0; c < 2; c++) {
      int output_offset = batch_offset + eval::kFeatureDemention * c;
      for (int i = 0; i < eval::kFeatureDemention; i++) {
        g[i] += gradient_[output_offset + i] / static_cast<float>(batch.size());
      }
    }
  }

  for (int i = 0; i < eval::kFeatureDemention; i++) {
    bias_v_[i] = 0.9 * bias_v_[i] - learning_rate * g[i];
    bias_[i] += bias_v_[i];
  }

  for (int b = 0; b < static_cast<int>(batch.size()); b++) {
    int batch_offset = eval::Network::kLayer0Input * b;
    for (int c = 0; c < 2; c++) {
      int output_offset = batch_offset + eval::kFeatureDemention * c;
      for (const auto& feature : batch[b].list[c]) {
        int offset = (batch[b].king[c] * Eval::kFEEnd + feature) *
                     eval::kFeatureDemention;
        PieceParam param(feature);
        RelativePosition relative(batch[b].king[c], param.square);
        for (int i = 0; i < eval::kFeatureDemention; i++) {
          kp_diff_[offset + i] += gradient_[output_offset + i];
          kp_num_[offset + i] += 1;
          p_diff_[feature * eval::kFeatureDemention + i] +=
              gradient_[output_offset + i];
          p_num_[feature * eval::kFeatureDemention + i] += 1;
          if (feature < Eval::kFEHandEnd) {
            rel_kp_diff_[param.piece][0][0][i] += gradient_[output_offset + i];
            rel_kp_num_[param.piece][0][0][i] += 1;
          } else {
            rel_kp_diff_[param.piece][relative.x][relative.y][i] +=
                gradient_[output_offset + i];
            rel_kp_num_[param.piece][relative.x][relative.y][i] += 1;
          }
        }
      }
      for (int i = 0; i < eval::kFeatureDemention; i++) {
        k_diff_[batch[b].king[c] * eval::kFeatureDemention + i] +=
            gradient_[output_offset + i];
        k_num_[batch[b].king[c] * eval::kFeatureDemention + i] += 1;
      }
    }
  }

#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int k = 0; k < kBoardSquare; k++) {
    for (int p = 0; p < Eval::kFEEnd; p++) {
      for (int f = 0; f < eval::kFeatureDemention; f++) {
        int i = (k * Eval::kFEEnd + p) * eval::kFeatureDemention;
        PieceParam param(static_cast<Eval::KPPIndex>(p));
        RelativePosition relative(static_cast<Square>(k), param.square);
        int total = kp_num_[i + f] + p_num_[p * eval::kFeatureDemention + f] +
                    k_num_[k * eval::kFeatureDemention + f];
        float g = (kp_num_[i + f] > 0)
                      ? kp_diff_[i + f] / static_cast<float>(kp_num_[i + f])
                      : 0.0f;
        float g_p = (p_num_[p * eval::kFeatureDemention + f] > 0)
                        ? p_diff_[p * eval::kFeatureDemention + f] /
                              static_cast<float>(
                                  p_num_[p * eval::kFeatureDemention + f])
                        : 0.0f;
        float g_k = (k_num_[k * eval::kFeatureDemention + f] > 0)
                        ? k_diff_[k * eval::kFeatureDemention + f] /
                              static_cast<float>(
                                  k_num_[k * eval::kFeatureDemention + f])
                        : 0.0f;
        float g_rel_kp = 0.0f;
        if (p < Eval::kFEHandEnd) {
          total += rel_kp_num_[param.piece][0][0][f];
          if (rel_kp_num_[param.piece][0][0][f] > 0)
            g_rel_kp = rel_kp_diff_[param.piece][0][0][f] /
                       static_cast<float>(rel_kp_num_[param.piece][0][0][f]);
        } else {
          total += rel_kp_num_[param.piece][relative.x][relative.y][f];
          if (rel_kp_num_[param.piece][relative.x][relative.y][f] > 0)
            g_rel_kp = rel_kp_diff_[param.piece][relative.x][relative.y][f] /
                       static_cast<float>(
                           rel_kp_num_[param.piece][relative.x][relative.y][f]);
        }
        if (total == 0) continue;
        g = (g + g_k + g_p + g_rel_kp) / 4.0f - 0.02 * kp_[i + f];
        kp_v_[i + f] = 0.9 * kp_v_[i + f] - learning_rate * g;
        kp_[i + f] += kp_v_[i + f];
      }
    }
  }

  std::memset(kp_diff_, 0, sizeof(kp_diff_));
  std::memset(kp_num_, 0, sizeof(kp_num_));
  std::memset(k_diff_, 0, sizeof(k_diff_));
  std::memset(k_num_, 0, sizeof(k_num_));
  std::memset(p_diff_, 0, sizeof(p_diff_));
  std::memset(p_num_, 0, sizeof(p_num_));
  std::memset(rel_kp_diff_, 0, sizeof(rel_kp_diff_));
  std::memset(rel_kp_num_, 0, sizeof(rel_kp_num_));
}

void NnFeatureLearner::InitializeParameters() {
  std::random_device rnd;
  std::mt19937 mt(rnd());
  // Ç‚ÇÀÇ§ÇÁâ§Ç∆ìØÇ∂èâä˙ílÇégÇ¡ÇƒÇ›ÇÈ
  for (int i = 0; i < 256; i++) {
    bias_[i] = 0.5f;
  }

  auto distribution =
      std::normal_distribution<float>(0.0f, 0.1f / std::sqrt(38.0f));
  for (int i = 0; i < 256 * kBoardSquare * Eval::kFEEnd; i++) {
    kp_[i] = distribution(mt);
  }
  QuantizeParameters();
}

void NnFeatureLearner::LoadParameters() {
  for (int i = 0; i < eval::kFeatureDemention; i++) {
    bias_[i] = static_cast<float>(quantized_bias_[i] / kQuantizeScale);
  }

  for (int i = 0; i < eval::kFeatureDemention * kBoardSquare * Eval::kFEEnd;
       i++) {
    kp_[i] = static_cast<float>(quantized_kp_[i] / kQuantizeScale);
  }
}

void NnFeatureLearner::QuantizeParameters() {
  for (int i = 0; i < 256; i++) {
    quantized_bias_[i] =
        static_cast<std::int16_t>(std::floor(bias_[i] * kQuantizeScale + 0.5));
  }

  for (int i = 0; i < 256 * kBoardSquare * Eval::kFEEnd; i++) {
    quantized_kp_[i] =
        static_cast<std::int16_t>(std::floor(kp_[i] * kQuantizeScale + 0.5));
  }
}

void NnFeatureLearner::OutputParamesters(const std::string& file_name) const {
  std::ofstream out(file_name, std::ios::binary);
  out.write(reinterpret_cast<char*>(quantized_bias_),
            eval::kFeatureDemention * sizeof(std::int16_t));
  out.write(reinterpret_cast<char*>(quantized_kp_),
            81 * Eval::kFEEnd * eval::kFeatureDemention * sizeof(std::int16_t));
}
