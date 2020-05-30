/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

  nozomi is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  nozomi is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <iostream>

#include "bit_board.h"
#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"
#include "book.h"
#include "learn.h"
#include "learn_nn.h"
#include "kifu_maker.h"
#include "move_probability.h"

int
main(int argc, char* argv[]) 
{
  std::cout << engine_info() << std::endl;

  USI::init(Options);
  BitBoard::initialize();

  Position::initialize();
  Search::init();
  eval::Init();
  Threads.init();
  MoveScore::init();

  TT.Resize(Options["USI_Hash"]);

#ifdef APERY_BOOK
  AperyBook::init();
#else
  if (Options["OwnBook"])
    Search::BookManager.open(Options["BookFile"]);
#endif
#ifndef LEARN
  USI::loop(argc, argv);
#else
  if (argc < 2)
    return 1;

  std::string type(argv[1]);
  std::string cmd;
  for (int i = 2; i < argc; ++i)
    cmd += std::string(argv[i]) + " ";
  std::istringstream is(cmd);
#if 0
  if (type == "bonanza")
  {
    std::unique_ptr<Learner> learner(new Learner);
    learner->learn(is);
  }
#endif
  if (type == "learn")
  {
    std::unique_ptr<NnLearner> learner = std::make_unique<NnLearner>();
    learner->Learn(is);
  }
  else if (type == "kifu")
  {
    KifuMaker::make(is);
  }
  else if (type == "prob")
  {
    MoveScore::init();
    MoveScore::reinforce(is);
    MoveScore::read("test.txt");
  }
#endif
  Threads.exit();

  return 0;
}
