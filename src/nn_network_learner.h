#include <vector>
#include <cstdint>
#include "evaluate_nn.h"

class NnNetworkLearner {
 public:
  NnNetworkLearner();
  const std::vector<float>& Forward(int batch_size,
                                    const std::vector<float>& batch);
  const std::vector<float>& Backward(int batch_size, float learning_rate,
                                     const std::vector<float>& batch,
                                     const std::vector<float>& input);
  void InitializeParameters();
  void LoadParameters();
  void QuantizeParameters();
  void OutputParamesters(const std::string& file_name) const;

  static constexpr float kNormalizeConstant = 2000.0f;

 private:
  std::int32_t* quantized_bias0_;
  std::int8_t* quantized_weights0_;
  std::int32_t* quantized_bias1_;
  std::int8_t* quantized_weights1_;
  std::int32_t* quantized_bias2_;
  std::int8_t* quantized_weights2_;
  float bias0_[eval::Network::kLayer1Input];
  float weights0_[eval::Network::kLayer0Input * eval::Network::kLayer1Input];
  float bias1_[eval::Network::kLayer2Input];
  float weights1_[eval::Network::kLayer1Input * eval::Network::kLayer2Input];
  float bias2_;
  float weights2_[eval::Network::kLayer2Input];
  float bias0_v_[eval::Network::kLayer1Input];
  float weights0_v_[eval::Network::kLayer0Input * eval::Network::kLayer1Input];
  float bias1_v_[eval::Network::kLayer2Input];
  float weights1_v_[eval::Network::kLayer1Input * eval::Network::kLayer2Input];
  float bias2_v_;
  float weights2_v_[eval::Network::kLayer2Input];
  std::vector<float> output0_;
  std::vector<float> output1_;
  std::vector<float> output2_;
  std::vector<float> input_gradient_;
  std::vector<float> gradient0_;
  std::vector<float> gradient1_;
  std::vector<float> buffer_;

  static constexpr float kQuantizeScale = 127.0f;
  static constexpr float kOutputBiasScale =
      float(kNormalizeConstant * eval::kOutputScale);
  static constexpr float kOutputWeightScale = kOutputBiasScale / kQuantizeScale;
  static constexpr float kOutputWeightMax =
      std::numeric_limits<std::int8_t>::max() / kOutputWeightScale;
  static constexpr float kNetworkBiasScale =
      ((1 << eval::kWeightScaleBits) * kQuantizeScale);
  static constexpr float kNetworkWeightScale = kNetworkBiasScale / kQuantizeScale;
  static constexpr float kNetworkWeightMax =
      std::numeric_limits<std::int8_t>::max() / kNetworkWeightScale;
};