#ifndef NOZOMI_PIECE_INDEX_H_
#define NOZOMI_PIECE_INDEX_H_

#include "evaluate.h"

class IndexList {
 private:
  Eval::KPPIndex list_[kNumberOfColor][Eval::kListNum];
};

#endif
