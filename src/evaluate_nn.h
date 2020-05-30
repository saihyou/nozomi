#ifndef NOZOMI_EVALUATE_NN_H_
#define NOZOMI_EVALUATE_NN_H_

#include <array>
#include <cstdint>
#include "move.h"
#include "types.h"
#include "evaluate.h"

class Position;
struct SearchStack;

namespace eval {
constexpr int kKpListLength = 38;
constexpr int kFeatureDemention = 256;
constexpr int kWeightScaleBits = 6;
constexpr int kOutputScale = 16;
constexpr int kTableSize = 131072;

struct alignas(32) Feature {
  std::int16_t feature[kNumberOfColor][kFeatureDemention];
  Value value;
};

struct Entry {
  Feature feature;
  Key key;
};

struct HashTable {
  Entry* operator[](Key key) {
    return &table[(uint32_t)key & (kTableSize - 1)];
  }
  void Clear() { table.fill({}); }

 private:
  std::array<Entry, kTableSize> table;
};

class NnFeature {
 public:
  NnFeature();
  ~NnFeature();
  void MakeFeature(const Position& pos, Feature& feature) const;
  void UpdateFeature(const Position& pos, Feature& feature) const;
  void UpdateFeature(int16_t* feature, Square king,
                     const Eval::KPPIndex* list) const;
  template <int kListNum>
  void UpdateFeature(int16_t* feature, Square king,
                     const Eval::KPPIndex* old_list,
                     const Eval::KPPIndex* new_list) const;
  void ReadParameters(const std::string& path);
#ifdef LEARN
  std::int16_t* bias() { return bias_; }
  std::int16_t* kp() { return kp_; }
#endif

 private:
  bool Allocate();
  void Free();

  std::int16_t* bias_ = nullptr;
  std::int16_t* kp_ = nullptr;
};

class Network {
 public:
  Network();
  ~Network();
  Value Compute(const std::int8_t* input);
  void ReadParameters(const std::string& path);
#ifdef LEARN
  std::int32_t* bias0() { return bias0_; }
  std::int8_t* weights0() { return weights0_; }
  std::int32_t* bias1() { return bias1_; }
  std::int8_t* weights1() { return weights1_; }
  std::int32_t* bias2() { return bias2_; }
  std::int8_t* weights2() { return weights2_; }
#endif

  static constexpr int kLayer0Input = 512;
  static constexpr int kLayer1Input = 32;
  static constexpr int kLayer2Input = 32;

 private:
  bool Allocate();
  void Free();

  std::int32_t* bias0_ = nullptr;
  std::int8_t* weights0_ = nullptr;
  std::int32_t* bias1_ = nullptr;
  std::int8_t* weights1_ = nullptr;
  std::int32_t* bias2_ = nullptr;
  std::int8_t* weights2_ = nullptr;
};

bool Init();
Value Evaluate(const Position& pos, SearchStack* ss);
#ifdef LEARN
extern NnFeature g_nnfeature;
extern Network g_network;
#endif
}  // namespace eval
#endif