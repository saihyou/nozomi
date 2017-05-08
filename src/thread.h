/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2016 Yuhei Ohmori

  This code is based on Stockfish (Chess playing engin).
  Copyright (C) 2004-2008 Tord Romstad (Glaurung author)
  Copyright (C) 2008-2015 Marco Costalba, Joona Kiiski, Tord Romstad
  Copyright (C) 2015-2016 Marco Costalba, Joona Kiiski, Gary Linscott, Tord Romstad

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

#ifndef _THREAD_H_
#define _THREAD_H_

#include <atomic>
#include <bitset>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

#include "move_picker.h"
#include "position.h"
#include "search.h"
#include "evaluate.h"

class Thread
{
  std::thread             native_thread_;
  std::mutex              mutex_;
  std::condition_variable sleep_condition_;
  bool                    exit_;
  bool                    searching_;

public:
  Thread();

  virtual
  ~Thread();

  virtual void
  search();

  void
  idle_loop();

  void
  start_searching(bool resume = false);

  void
  wait_for_search_finished();

  void
  wait(std::atomic_bool &b);

  size_t index_;
  size_t pv_index_;
  int    max_ply_;
  int    calls_count_;

  Eval::HashTable         eval_hash_;
  Position                root_pos_;
  Search::RootMoveVector  root_moves_;
  Depth                   root_depth_;
  FromToStats             from_to_;
  Depth                   completed_depth_;
  std::atomic_bool        reset_calls_;
  HistoryStats            history_;
  MovesStats              counter_moves_;
  CounterMoveHistoryStats counter_move_history_;
};

struct MainThread : public Thread
{
  virtual void
  search();

  bool   easy_move_played;
  bool   failed_low;
  double best_move_changes;
  Value  previous_score;
};

struct ThreadPool : public std::vector<Thread *>
{
  void
  init();

  void
  exit();

  MainThread *
  main()
  {
    return static_cast<MainThread *>(at(0));
  }

  void
  start_thinking(const Position &, const Search::LimitsType &, Search::StateStackPtr &);

  void
  read_usi_options();

  int64_t
  nodes_searched();
};

extern ThreadPool Threads;

#endif
