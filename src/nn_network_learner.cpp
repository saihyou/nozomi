#include "nn_network_learner.h"

#include <omp.h>

#include <fstream>
#include <random>

namespace {

template <int kInputDemention, int kOutputDemention>
void AffineTransform(int batch_size, const std::vector<float>& input,
                     std::vector<float>& output, const float* bias,
                     const float* weight) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < batch_size; i++) {
    int input_offset = i * kInputDemention;
    int output_offset = i * kOutputDemention;
    for (int j = 0; j < kOutputDemention; j++) {
      float sum = bias[j];
      for (int k = 0; k < kInputDemention; k++) {
        int offset = j * kInputDemention + k;
        sum += weight[offset] * input[input_offset + k];
      }
      output[output_offset + j] = sum;
    }
  }
}

template <int kInputDemention>
void Activate(int batch_size, const std::vector<float>& input,
              std::vector<float>& output) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < batch_size; i++) {
    int input_offset = i * kInputDemention;
    for (int j = 0; j < kInputDemention; j++) {
      output[input_offset + j] =
          std::max(0.0f, std::min(1.0f, input[input_offset + j]));
    }
  }
}

template <int kInputDemention, int kOutputDemention>
void BackwardAffineTransform(int batch_size, const std::vector<float>& input,
                             std::vector<float>& output, const float* weight) {
#ifdef _OPENMP
#pragma omp parallel for
#endif
  for (int i = 0; i < batch_size; i++) {
    int input_offset = i * kInputDemention;
    int output_offset = i * kOutputDemention;
    for (int j = 0; j < kInputDemention; j++) {
      double sum = 0.0;
      for (int k = 0; k < kOutputDemention; k++) {
        int offset = kInputDemention * k + j;
        sum += weight[offset] * input[output_offset + k];
      }
      output[input_offset + j] = static_cast<float>(sum);
    }
  }
}

template <int kInputDemention, int kOutputDemention>
void UpdateParamesters(int batch_size, const std::vector<float>& gradients,
                       float learning_rate,
                       const std::vector<float>& batch_input, float* bias_v,
                       float* weights_v, float* bias, float* weights) {
  float bias_g[kOutputDemention] = {};
  float weights_g[kOutputDemention * kInputDemention] = {};

  for (int b = 0; b < batch_size; b++) {
    int input_offset = kInputDemention * b;
    int output_offset = kOutputDemention * b;
    for (int i = 0; i < kOutputDemention; i++) {
      bias_g[i] += gradients[output_offset + i];
    }
    for (int i = 0; i < kOutputDemention; i++) {
      for (int j = 0; j < kInputDemention; j++) {
        int offset = kInputDemention * i + j;
        weights_g[offset] +=
            gradients[output_offset + i] * batch_input[input_offset + j];
      }
    }
  }

  for (int i = 0; i < kOutputDemention; i++) {
    bias_g[i] = bias_g[i] / static_cast<float>(batch_size);
    bias_v[i] = 0.9f * bias_v[i] - learning_rate * bias_g[i];
    bias[i] += bias_v[i];
  }

  for (int i = 0; i < kOutputDemention * kInputDemention; i++) {
    weights_g[i] = weights_g[i] / static_cast<float>(batch_size);
    weights_v[i] = 0.9f * weights_v[i] - learning_rate * weights_g[i];
    weights[i] += weights_v[i];
  }
}

template <int kOutputDemention>
void BackwardActivate(int batch_size, const std::vector<float>& gradient,
                      const std::vector<float>& input,
                      std::vector<float>& output) {
  for (int i = 0; i < batch_size; i++) {
    int batch_offset = i * kOutputDemention;
    for (int j = 0; j < kOutputDemention; j++) {
      int index = batch_offset + j;
      output[index] =
          gradient[index] * (input[index] > 0.0f) * (input[index] < 1.0f);
    }
  }
}
}  // namespace

NnNetworkLearner::NnNetworkLearner()
    : quantized_bias0_(eval::g_network.bias0()),
      quantized_weights0_(eval::g_network.weights0()),
      quantized_bias1_(eval::g_network.bias1()),
      quantized_weights1_(eval::g_network.weights1()),
      quantized_bias2_(eval::g_network.bias2()),
      quantized_weights2_(eval::g_network.weights2()),
      bias0_{},
      weights0_{},
      bias1_{},
      weights1_{},
      bias2_{},
      weights2_{},
      bias0_v_{},
      weights0_v_{},
      bias1_v_{},
      weights1_v_{},
      bias2_v_{},
      weights2_v_{} {}

const std::vector<float>& NnNetworkLearner::Forward(
    int batch_size, const std::vector<float>& batch) {
  if (static_cast<int>(output0_.size()) <
      batch_size * eval::Network::kLayer1Input) {
    output0_.resize(batch_size * eval::Network::kLayer1Input);
    gradient0_.resize(batch_size * eval::Network::kLayer1Input);
  }

  AffineTransform<eval::Network::kLayer0Input, eval::Network::kLayer1Input>(
      batch_size, batch, output0_, bias0_, weights0_);

  if (static_cast<int>(buffer_.size()) <
      batch_size * eval::Network::kLayer1Input) {
    buffer_.resize(batch_size * eval::Network::kLayer1Input);
  }
  Activate<eval::Network::kLayer1Input>(batch_size, output0_, buffer_);

  if (static_cast<int>(output1_.size()) <
      batch_size * eval::Network::kLayer2Input) {
    output1_.resize(batch_size * eval::Network::kLayer1Input);
    gradient1_.resize(batch_size * eval::Network::kLayer1Input);
  }
  AffineTransform<eval::Network::kLayer1Input, eval::Network::kLayer2Input>(
      batch_size, buffer_, output1_, bias1_, weights1_);

  if (static_cast<int>(buffer_.size()) <
      batch_size * eval::Network::kLayer1Input) {
    buffer_.resize(batch_size * eval::Network::kLayer1Input);
  }
  Activate<eval::Network::kLayer2Input>(batch_size, output1_, buffer_);

  if (static_cast<int>(output2_.size()) < batch_size) {
    output2_.resize(batch_size);
  }
  AffineTransform<eval::Network::kLayer2Input, 1>(batch_size, buffer_, output2_,
                                                  &bias2_, weights2_);

  return output2_;
}

const std::vector<float>& NnNetworkLearner::Backward(
    int batch_size, float learning_rate, const std::vector<float>& batch,
    const std::vector<float>& input) {
  BackwardAffineTransform<eval::Network::kLayer2Input, 1>(
      batch_size, batch, gradient1_, weights2_);
  UpdateParamesters<eval::Network::kLayer2Input, 1>(
      batch_size, batch, learning_rate, output1_, &bias2_v_, weights2_v_,
      &bias2_, weights2_);
  BackwardActivate<eval::Network::kLayer2Input>(batch_size, gradient1_,
                                                output1_, buffer_);

  BackwardAffineTransform<eval::Network::kLayer1Input,
                          eval::Network::kLayer2Input>(batch_size, gradient1_,
                                                       gradient0_, weights1_);
  UpdateParamesters<eval::Network::kLayer1Input, eval::Network::kLayer2Input>(
      batch_size, gradient1_, learning_rate, output0_, bias1_v_, weights1_v_,
      bias1_, weights1_);
  BackwardActivate<eval::Network::kLayer1Input>(batch_size, gradient0_,
                                                output0_, buffer_);

  if (static_cast<int>(input_gradient_.size()) <
      eval::Network::kLayer0Input * batch_size) {
    input_gradient_.resize(eval::Network::kLayer0Input * batch_size);
  }
  BackwardAffineTransform<eval::Network::kLayer0Input,
                          eval::Network::kLayer1Input>(
      batch_size, gradient0_, input_gradient_, weights0_);
  UpdateParamesters<eval::Network::kLayer0Input, eval::Network::kLayer1Input>(
      batch_size, gradient0_, learning_rate, input, bias0_v_, weights0_v_,
      bias0_, weights0_);

  return input_gradient_;
}

void NnNetworkLearner::InitializeParameters() {
  std::random_device rnd;
  std::mt19937 mt(rnd());
  {
    auto distribution = std::normal_distribution<float>(
        0.0, 1.0f / static_cast<float>(std::sqrt(eval::Network::kLayer0Input)));
    for (int i = 0; i < eval::Network::kLayer1Input; i++) {
      float sum = 0.0f;
      for (int j = 0; j < eval::Network::kLayer0Input; j++) {
        const auto weight = distribution(mt);
        weights0_[eval::Network::kLayer0Input * i + j] = weight;
        sum += weight;
      }
      bias0_[i] = 0.5f - 0.5f * sum;
    }
  }

  {
    auto distribution = std::normal_distribution<float>(
        0.0, 1.0f / static_cast<float>(std::sqrt(eval::Network::kLayer1Input)));
    for (int i = 0; i < eval::Network::kLayer2Input; i++) {
      float sum = 0.0f;
      for (int j = 0; j < eval::Network::kLayer1Input; j++) {
        const auto weight = distribution(mt);
        weights1_[eval::Network::kLayer1Input * i + j] = weight;
        sum += weight;
      }
      bias1_[i] = 0.5f - 0.5f * sum;
    }
  }

  bias2_ = 0;
  std::fill(std::begin(weights2_), std::end(weights2_), 0.0f);

  QuantizeParameters();
}

void NnNetworkLearner::LoadParameters() {
  for (int i = 0; i < eval::Network::kLayer1Input; i++) {
    bias0_[i] = static_cast<float>(quantized_bias0_[i]) / kNetworkBiasScale;
  }

  for (int i = 0; i < eval::Network::kLayer0Input * eval::Network::kLayer1Input;
       i++) {
    weights0_[i] =
        static_cast<float>(quantized_weights0_[i]) / kNetworkWeightScale;
  }

  for (int i = 0; i < eval::Network::kLayer2Input; i++) {
    bias1_[i] = static_cast<float>(quantized_bias1_[i]) / kNetworkBiasScale;
  }

  for (int i = 0; i < eval::Network::kLayer1Input * eval::Network::kLayer2Input;
       i++) {
    weights1_[i] =
        static_cast<float>(quantized_weights1_[i]) / kNetworkWeightScale;
  }

  bias2_ = static_cast<float>(*quantized_bias2_) / kOutputBiasScale;

  for (int i = 0; i < eval::Network::kLayer2Input; i++) {
    weights2_[i] =
        static_cast<float>(quantized_weights2_[i]) / kOutputWeightScale;
  }
}

void NnNetworkLearner::QuantizeParameters() {
  for (int i = 0; i < eval::Network::kLayer1Input; i++) {
    quantized_bias0_[i] = static_cast<std::int32_t>(
        std::floor(bias0_[i] * kNetworkBiasScale + 0.5));
  }

  for (int i = 0; i < eval::Network::kLayer0Input * eval::Network::kLayer1Input;
       i++) {
    weights0_[i] =
        std::max(-kNetworkWeightMax, std::min(kNetworkWeightMax, weights0_[i]));
    quantized_weights0_[i] = static_cast<std::int8_t>(
        std::floor(weights0_[i] * kNetworkWeightScale + 0.5));
  }

  for (int i = 0; i < eval::Network::kLayer2Input; i++) {
    quantized_bias1_[i] = static_cast<std::int32_t>(
        std::floor(bias1_[i] * kNetworkBiasScale + 0.5));
  }

  for (int i = 0; i < eval::Network::kLayer1Input * eval::Network::kLayer2Input;
       i++) {
    weights1_[i] =
        std::max(-kNetworkWeightMax, std::min(kNetworkWeightMax, weights1_[i]));
    quantized_weights1_[i] = static_cast<std::int8_t>(
        std::floor(weights1_[i] * kNetworkWeightScale + 0.5));
  }

  *quantized_bias2_ =
      static_cast<std::int32_t>(std::floor(bias2_ * kOutputBiasScale + 0.5));

  for (int i = 0; i < eval::Network::kLayer2Input; i++) {
    weights2_[i] =
        std::max(-kOutputWeightMax, std::min(kOutputWeightMax, weights2_[i]));
    quantized_weights2_[i] = static_cast<std::int8_t>(
        std::floor(weights2_[i] * kOutputWeightScale + 0.5));
  }
}

void NnNetworkLearner::OutputParamesters(const std::string& file_name) const {
  std::ofstream out(file_name, std::ios::binary);
  out.write(reinterpret_cast<char*>(quantized_bias0_),
            sizeof(std::int32_t) * eval::Network::kLayer1Input);
  out.write(reinterpret_cast<char*>(quantized_weights0_),
            sizeof(std::int8_t) * eval::Network::kLayer0Input *
                eval::Network::kLayer1Input);
  out.write(reinterpret_cast<char*>(quantized_bias1_),
            sizeof(std::int32_t) * eval::Network::kLayer2Input);
  out.write(reinterpret_cast<char*>(quantized_weights1_),
            sizeof(std::int8_t) * eval::Network::kLayer1Input *
                eval::Network::kLayer2Input);
  out.write(reinterpret_cast<char*>(quantized_bias2_), sizeof(std::int32_t));
  out.write(reinterpret_cast<char*>(quantized_weights2_),
            sizeof(std::int8_t) * eval::Network::kLayer2Input);
}
