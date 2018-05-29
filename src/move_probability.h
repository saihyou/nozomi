#ifndef _MOVE_SCORE_H_
#define _MOVE_SCORE_H_

#include <string>
#include "position.h"

namespace MoveScore
{
bool
init();

Value
evaluate(const Position &pos, const CheckInfo &ci, Move move);

void
read(const std::string &book_file);

void
reinforce(std::istringstream &is);
}

#endif
