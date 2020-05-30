#include <vector>
#include "evaluate.h"
#include "evaluate_nn.h"
#include "types.h"
#include "nn_feature_learner.h"
#include "nn_network_learner.h"


struct PositionData {
  std::string sfen;
  Value value;
  Color win;
  std::string next_move;
};



class NnLearner {
 public:
  void Learn(std::istringstream& is);

 private:
  void Initialize();
  void LearnMain();
  std::vector<PositionData> ReadSfenFile(std::ifstream& ifs, bool& eof);
  void MakeBatch(std::vector<PositionData>& batch);
  void CalcurateGradient(const std::vector<Example>& batch);
  void UpdateParameters(const std::vector<Example>& batch);
  double Validate() const;

  std::vector<Example> examples_;
  std::vector<Position> positions_;
  NnFeatureLearner feature_;
  NnNetworkLearner network_;
  std::vector<float> gradients_;
  int num_threads_;
  int batch_size_;
  float learning_rate_;
  std::string train_file_name_;
  std::string valid_file_name_;
  int count_;
};