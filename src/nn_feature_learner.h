#include <vector>
#include <cstdint>
#include "evaluate.h"
#include "evaluate_nn.h"
#include "types.h"

struct Example {
  Eval::KPPIndex list[kNumberOfColor][eval::kKpListLength];
  Square king[kNumberOfColor];
  float value;
  float win;
  float material;
};

class NnFeatureLearner {
 public:
  NnFeatureLearner();
  void Forward(const std::vector<Example>& batch);
  void Backward(const std::vector<Example> batch, float learning_rate,
                const std::vector<float>& gradient);
  const std::vector<float>& output() const { return output_; }
  void InitializeParameters();
  void LoadParameters();
  void QuantizeParameters();
  void OutputParamesters(const std::string& file_name) const;

 private:
  std::int16_t* quantized_bias_;
  std::int16_t* quantized_kp_;
  float bias_[eval::kFeatureDemention];
  float bias_v_[eval::kFeatureDemention];
  float kp_[eval::kFeatureDemention * kBoardSquare * Eval::kFEEnd];
  float kp_v_[eval::kFeatureDemention * kBoardSquare * Eval::kFEEnd];
  float kp_diff_[eval::kFeatureDemention * kBoardSquare * Eval::kFEEnd];
  int kp_num_[eval::kFeatureDemention * kBoardSquare * Eval::kFEEnd];
  float k_diff_[eval::kFeatureDemention * kBoardSquare];
  int k_num_[eval::kFeatureDemention * kBoardSquare];
  float p_diff_[eval::kFeatureDemention * Eval::kFEEnd];
  int p_num_[eval::kFeatureDemention * Eval::kFEEnd];
  float rel_kp_diff_[kPieceMax][17][17][eval::kFeatureDemention];
  int rel_kp_num_[kPieceMax][17][17][eval::kFeatureDemention];
  std::vector<float> output_;
  std::vector<float> gradient_;

  static constexpr float kQuantizeScale = 127.0f;
};