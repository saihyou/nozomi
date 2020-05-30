#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "evaluate_nn.h"
#include <cstdint>
#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "types.h"

const __m256i kZero = _mm256_setzero_si256();
const __m256i kOnes = _mm256_set1_epi16(1);
const __m256i kOffsets = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);

namespace eval {
NnFeature g_nnfeature;
Network g_network;

NnFeature::NnFeature() {
  if (!Allocate()) Free();
}

NnFeature::~NnFeature() { Free(); }

void NnFeature::MakeFeature(const Position& pos, Feature& feature) const {
  Square kings[2] = {pos.square_king(kBlack),
                     Eval::inverse(pos.square_king(kWhite))};
  const Eval::KPPIndex* list[2] = {pos.black_kpp_list(), pos.white_kpp_list()};
  for (int c = 0; c < 2; c++) {
    auto f = reinterpret_cast<__m256i*>(feature.feature[c]);
    std::memcpy(feature.feature[c], bias_,
                sizeof(std::int16_t) * kFeatureDemention);
    for (int i = 0; i < 38; i++) {
      int offset = (kings[c] * Eval::kFEEnd + list[c][i]) * kFeatureDemention;
      auto weight = reinterpret_cast<__m256i*>(kp_ + offset);
      for (int j = 0; j < 16; j++) {
        f[j] = _mm256_add_epi16(f[j], weight[j]);
      }
    }
  }
}

void NnFeature::UpdateFeature(const Position& pos, Feature& feature) const {
  Square kings[2] = {pos.square_king(kBlack),
                     Eval::inverse(pos.square_king(kWhite))};
  const Eval::KPPIndex* old_list[kNumberOfColor] = {
      pos.old_index_value(kBlack), pos.old_index_value(kWhite)};
  for (int c = 0; c < 2; c++) {
    auto f = reinterpret_cast<__m256i*>(feature.feature[c]);
    for (int i = 0; i < pos.chenged_index_num(); i++) {
      int offset =
          (kings[c] * Eval::kFEEnd + old_list[c][i]) * kFeatureDemention;
      auto weight = reinterpret_cast<__m256i*>(kp_ + offset);
      for (int j = 0; j < 16; j++) {
        f[j] = _mm256_sub_epi16(f[j], weight[j]);
      }
    }
  }

  const Eval::KPPIndex* new_list[kNumberOfColor] = {
      pos.new_index_value(kBlack), pos.new_index_value(kWhite)};

  for (int c = 0; c < 2; c++) {
    auto f = reinterpret_cast<__m256i*>(feature.feature[c]);
    for (int i = 0; i < pos.chenged_index_num(); i++) {
      int offset =
          (kings[c] * Eval::kFEEnd + new_list[c][i]) * kFeatureDemention;
      auto weight = reinterpret_cast<__m256i*>(kp_ + offset);
      for (int j = 0; j < 16; j++) {
        f[j] = _mm256_add_epi16(f[j], weight[j]);
      }
    }
  }
}

void NnFeature::UpdateFeature(int16_t* feature, Square king,
                              const Eval::KPPIndex* list) const {
  auto f = reinterpret_cast<__m256i*>(feature);
  std::memcpy(feature, bias_, sizeof(std::int16_t) * kFeatureDemention);
  for (int i = 0; i < 38; i++) {
    int offset = (king * Eval::kFEEnd + list[i]) * kFeatureDemention;
    auto weight = reinterpret_cast<__m256i*>(kp_ + offset);
    for (int j = 0; j < 16; j++) {
      f[j] = _mm256_add_epi16(f[j], weight[j]);
    }
  }
}

template <int kListNum>
void NnFeature::UpdateFeature(int16_t* feature, Square king,
                              const Eval::KPPIndex* old_list,
                              const Eval::KPPIndex* new_list) const {
  {
    auto f = reinterpret_cast<__m256i*>(feature);
    for (int i = 0; i < kListNum; i++) {
      int offset = (king * Eval::kFEEnd + old_list[i]) * kFeatureDemention;
      auto weight = reinterpret_cast<__m256i*>(kp_ + offset);
      for (int j = 0; j < 16; j++) {
        f[j] = _mm256_sub_epi16(f[j], weight[j]);
      }
    }
  }

  {
    auto f = reinterpret_cast<__m256i*>(feature);
    for (int i = 0; i < kListNum; i++) {
      int offset = (king * Eval::kFEEnd + new_list[i]) * kFeatureDemention;
      auto weight = reinterpret_cast<__m256i*>(kp_ + offset);
      for (int j = 0; j < 16; j++) {
        f[j] = _mm256_add_epi16(f[j], weight[j]);
      }
    }
  }
}

void NnFeature::ReadParameters(const std::string& path) {
  auto fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    return;
  }
  
  if (std::fread(bias_, sizeof(std::int16_t), kFeatureDemention, fp) !=
      kFeatureDemention) {
    std::cerr << "read error feature bias." << std::endl;
    std::fclose(fp);
    return;
  }

  if (std::fread(kp_, sizeof(std::int16_t), 81 * Eval::kFEEnd * kFeatureDemention,
                 fp) != 81 * Eval::kFEEnd * kFeatureDemention) {
    std::cerr << "read error feature kp." << std::endl;
    std::fclose(fp);
    return;
  }
  std::fclose(fp);
}

bool NnFeature::Allocate() {
  bias_ = static_cast<std::int16_t*>(
      _mm_malloc(sizeof(std::int16_t) * kFeatureDemention, 32));
  if (bias_ == nullptr) {
    return false;
  }

  kp_ = static_cast<std::int16_t*>(_mm_malloc(
      sizeof(std::int16_t) * 81 * Eval::kFEEnd * kFeatureDemention, 32));
  if (kp_ == nullptr) {
    _mm_free(bias_);
    return false;
  }

  return true;
}

void NnFeature::Free() {
  if (bias_ != nullptr) _mm_free(bias_);
  if (kp_ != nullptr) _mm_free(kp_);
}

void ActivateInputFeature(const Position& pos, const Feature& feature,
                          std::int8_t* output) {
  Color color[2] = {pos.side_to_move(), ~pos.side_to_move()};
  for (int i = 0; i < 2; i++) {
    auto out = reinterpret_cast<__m256i*>(&output[kFeatureDemention * i]);
    for (int j = 0; j < 8; j++) {
      auto feature0 =
          reinterpret_cast<const __m256i*>(feature.feature[color[i]])[j * 2];
      auto feature1 = reinterpret_cast<const __m256i*>(
          feature.feature[color[i]])[j * 2 + 1];
      _mm256_store_si256(
          &out[j],
          _mm256_permute4x64_epi64(
              _mm256_max_epi8(_mm256_packs_epi16(feature0, feature1), kZero),
              0b11011000));
    }
  }
}

Network::Network() {
  if (!Allocate()) Free();
}

Network::~Network() { Free(); }

template <int kInputDemention, int kOutputDemention>
void AffineTransform(const std::int8_t* input, std::int32_t* output,
                     const std::int32_t* bias, const std::int8_t* weight) {
  auto input_vector = reinterpret_cast<const __m256i*>(input);
  constexpr int loop_num = kInputDemention / 32;
  for (int i = 0; i < kOutputDemention; i++) {
    __m256i sum = _mm256_set_epi32(0, 0, 0, 0, 0, 0, 0, bias[i]);
    auto weights =
        reinterpret_cast<const __m256i*>(&weight[kInputDemention * i]);
    for (int j = 0; j < loop_num; j++) {
      __m256i product = _mm256_maddubs_epi16(
          _mm256_load_si256(&input_vector[j]), _mm256_load_si256(&weights[j]));
      product = _mm256_madd_epi16(product, kOnes);
      sum = _mm256_add_epi32(sum, product);
    }
    sum = _mm256_hadd_epi32(sum, sum);
    sum = _mm256_hadd_epi32(sum, sum);
    const __m128i lo = _mm256_extracti128_si256(sum, 0);
    const __m128i hi = _mm256_extracti128_si256(sum, 1);
    output[i] = _mm_cvtsi128_si32(lo) + _mm_cvtsi128_si32(hi);
  }
}

void Activate(const std::int32_t* input, std::int8_t* output) {
  const auto in = reinterpret_cast<const __m256i*>(input);
  auto out = reinterpret_cast<__m256i*>(output);
  const __m256i words0 = _mm256_srai_epi16(
      _mm256_packs_epi32(_mm256_load_si256(&in[0]), _mm256_load_si256(&in[1])),
      kWeightScaleBits);
  const __m256i words1 = _mm256_srai_epi16(
      _mm256_packs_epi32(_mm256_load_si256(&in[2]), _mm256_load_si256(&in[3])),
      kWeightScaleBits);
  _mm256_store_si256(
      out, _mm256_permutevar8x32_epi32(
               _mm256_max_epi8(_mm256_packs_epi16(words0, words1), kZero),
               kOffsets));
}

Value Network::Compute(const std::int8_t* input) {
  alignas(32) std::int32_t features0[Network::kLayer1Input];
  AffineTransform<Network::kLayer0Input, Network::kLayer1Input>(
      input, features0, bias0_, weights0_);

  alignas(32) std::int8_t out0[Network::kLayer1Input];
  Activate(features0, out0);

  alignas(32) std::int32_t features1[Network::kLayer2Input];
  AffineTransform<Network::kLayer1Input, Network::kLayer2Input>(
      out0, features1, bias1_, weights1_);

  alignas(32) std::int8_t out1[Network::kLayer2Input];
  Activate(features1, out1);

  alignas(32) std::int32_t features2;
  AffineTransform<Network::kLayer2Input, 1>(out1, &features2, bias2_,
                                            weights2_);

  return static_cast<Value>(features2 / kOutputScale);
}

bool Network::Allocate() {
  bias0_ = static_cast<std::int32_t*>(
      _mm_malloc(sizeof(std::int32_t) * Network::kLayer1Input, 32));
  if (bias0_ == nullptr) return false;
  weights0_ = static_cast<std::int8_t*>(_mm_malloc(
      sizeof(std::int8_t) * Network::kLayer0Input * Network::kLayer1Input, 32));
  if (weights0_ == nullptr) return false;
  bias1_ = static_cast<std::int32_t*>(
      _mm_malloc(sizeof(std::int32_t) * Network::kLayer2Input, 32));
  if (bias1_ == nullptr) return false;
  weights1_ = static_cast<std::int8_t*>(_mm_malloc(
      sizeof(std::int8_t) * Network::kLayer1Input * Network::kLayer2Input, 32));
  if (weights1_ == nullptr) return false;
  bias2_ = static_cast<std::int32_t*>(_mm_malloc(sizeof(std::int32_t) * 1, 32));
  if (bias2_ == nullptr) return false;
  weights2_ = static_cast<std::int8_t*>(
      _mm_malloc(sizeof(std::int8_t) * Network::kLayer2Input * 1, 32));
  if (weights2_ == nullptr) return false;

  return true;
}

void Network::Free() {
  if (bias0_ != nullptr) {
    _mm_free(bias0_);
    bias0_ = nullptr;
  }

  if (weights0_ != nullptr) {
    _mm_free(weights0_);
    weights0_ = nullptr;
  }

  if (bias1_ != nullptr) {
    _mm_free(bias1_);
    bias1_ = nullptr;
  }

  if (weights1_ != nullptr) {
    _mm_free(weights1_);
    weights1_ = nullptr;
  }

  if (bias2_ != nullptr) {
    _mm_free(bias2_);
    bias2_ = nullptr;
  }

  if (weights2_ != nullptr) {
    _mm_free(weights2_);
    weights2_ = nullptr;
  }
}

void Network::ReadParameters(const std::string& path) {
  auto fp = std::fopen(path.c_str(), "rb");
  if (fp == nullptr) {
    return;
  }

  if (std::fread(bias0_, sizeof(std::int32_t), 32, fp) != 32) {
    std::cerr << "read error bias0." << std::endl;
    std::fclose(fp);
    return;
  }

  if (std::fread(weights0_, sizeof(std::int8_t), 512 * 32, fp) != 512 * 32) {
    std::cerr << "read error weights0." << std::endl;
    std::fclose(fp);
    return;
  }

  if (std::fread(bias1_, sizeof(std::int32_t), 32, fp) != 32) {
    std::cerr << "read error bias1." << std::endl;
    std::fclose(fp);
    return;
  }

  if (std::fread(weights1_, sizeof(std::int8_t), 32 * 32, fp) != 32 * 32) {
    std::cerr << "read error weights1." << std::endl;
    std::fclose(fp);
    return;
  }

  if (std::fread(bias2_, sizeof(std::int32_t), 1, fp) != 1) {
    std::cerr << "read error bias2." << std::endl;
    std::fclose(fp);
    return;
  }

  if (std::fread(weights2_, sizeof(std::int8_t), 32, fp) != 32) {
    std::cerr << "read error weights2." << std::endl;
    std::fclose(fp);
    return;
  }
  std::fclose(fp);
}

bool Init() {
  g_nnfeature.ReadParameters("nn_feature.bin");
  g_network.ReadParameters("nn_network.bin");
  return true;
}

Value Evaluate(const Position& pos, SearchStack* ss) {
  // “¯‚¶Position‚ð2“x’Tõ‚µ‚½Žž‚ÉA•]‰¿Ï‚Ý‚Æ‚È‚éƒP[ƒX‚ª‚ ‚é
  if (ss->evaluated) {
    return std::min(kValueMaxEvaluate,
                    std::max(-kValueMaxEvaluate, ss->feature.value));
  }

  Key key = pos.key();
  Entry* e = pos.this_thread()->eval_hash_[key];
  if (e->key == key) {
    std::memcpy(ss->feature.feature, e->feature.feature,
                sizeof(e->feature.feature));
    ss->feature.value = e->feature.value;
  } else {
    if ((ss - 1)->current_move == kMoveNull) {
      // Null move‚Ìê‡‚Í•]‰¿Ï‚Ý‚Ì‚Í‚¸
      ss->feature = (ss - 1)->feature;
    } else if ((ss - 1)->evaluated) {
      ss->feature = (ss - 1)->feature;
      if (move_piece_type((ss - 1)->current_move) != kKing) {
        g_nnfeature.UpdateFeature(pos, ss->feature);
      } else {
        if (pos.side_to_move() == kBlack) {
          Square king = Eval::inverse(pos.square_king(kWhite));
          const Eval::KPPIndex* list = pos.white_kpp_list();
          g_nnfeature.UpdateFeature(ss->feature.feature[kWhite], king, list);
        } else {
          Square king = pos.square_king(kBlack);
          const Eval::KPPIndex* list = pos.black_kpp_list();
          g_nnfeature.UpdateFeature(ss->feature.feature[kBlack], king, list);
        }
        if (move_capture((ss - 1)->current_move) != kPieceNone) {
          if (pos.side_to_move() == kBlack) {
            Square king = pos.square_king(kBlack);
            const Eval::KPPIndex* old_list = pos.old_index_value(kBlack);
            const Eval::KPPIndex* new_list = pos.new_index_value(kBlack);
            g_nnfeature.UpdateFeature<1>(ss->feature.feature[kBlack], king,
                                         &old_list[1], &new_list[1]);
          } else {
            Square king = Eval::inverse(pos.square_king(kWhite));
            const Eval::KPPIndex* old_list = pos.old_index_value(kWhite);
            const Eval::KPPIndex* new_list = pos.new_index_value(kWhite);
            g_nnfeature.UpdateFeature<1>(ss->feature.feature[kWhite], king,
                                         &old_list[1], &new_list[1]);
          }
        }
      }
    } else {
      g_nnfeature.MakeFeature(pos, ss->feature);
    }

    alignas(32) int8_t activated_feature[512];
    ActivateInputFeature(pos, ss->feature, activated_feature);
    ss->feature.value = g_network.Compute(activated_feature);
    e->feature = ss->feature;
    e->key = key;
  }

  ss->evaluated = true;
  return std::min(kValueMaxEvaluate,
                  std::max(-kValueMaxEvaluate, ss->feature.value));
}
}  // namespace eval
