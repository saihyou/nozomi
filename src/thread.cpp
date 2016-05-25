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

#include <algorithm>
#include <cassert>

#include "move_generator.h"
#include "search.h"
#include "thread.h"
#include "usi.h"

using namespace Search;

ThreadPool Threads;

Thread::Thread()
{
  reset_calls_ = false;
  exit_        = false;
  history_.clear();
  counter_moves_.clear();
  index_  = Threads.size();
  std::unique_lock<std::mutex> lk(mutex_);
  searching_ = true;
  native_thread_ = std::thread(&Thread::idle_loop, this);
  sleep_condition_.wait(lk, [&]{ return !searching_; });
}

Thread::~Thread()
{
  mutex_.lock();
  exit_ = true;
  sleep_condition_.notify_one();
  mutex_.unlock();
  native_thread_.join();
}

void
Thread::wait_for_search_finished()
{
  std::unique_lock<std::mutex> lk(mutex_);
  sleep_condition_.wait(lk, [&]{ return !searching_; });
}

void
Thread::wait(std::atomic_bool &condition)
{

  std::unique_lock<std::mutex> lk(mutex_);
  sleep_condition_.wait(lk, [&] { return bool(condition); });
}

void
Thread::start_searching(bool resume)
{
  std::unique_lock<std::mutex> lk(mutex_);
  
  if (!resume)
    searching_ = true;

  sleep_condition_.notify_one();
}

void
Thread::idle_loop()
{
  while (!exit_)
  {
    std::unique_lock<std::mutex> lk(mutex_);
    searching_ = false;

    while (!searching_ && !exit_)
    {
      sleep_condition_.notify_one();
      sleep_condition_.wait(lk);
    }
    lk.unlock();

    if (!exit_)
      search();
  }
}

void
ThreadPool::init()
{
  push_back(new MainThread);
  read_usi_options();
}

void
ThreadPool::exit()
{
  while (size())
  {
    delete back();
    pop_back();
  }
}

void
ThreadPool::read_usi_options()
{
  size_t requested = Options["Threads"];

  assert(requested > 0);

  while (size() < requested)
    push_back(new Thread);

  while (size() > requested)
  {
    delete back();
    pop_back();
  }
}

int64_t
ThreadPool::nodes_searched()
{
  int64_t nodes = 0;
  for (Thread *th : *this)
    nodes += th->root_pos_.nodes_searched();
  return nodes;
}

void
ThreadPool::start_thinking
(
  const Position   &pos,
  const LimitsType &limits,
  StateStackPtr    &states
)
{
  main()->wait_for_search_finished();
  
  Signals.stop_on_ponder_hit = false;
  Signals.stop               = false;

  main()->root_moves_.clear();
  main()->root_pos_ = pos;
  Limits = limits;
  if (states.get())
  {
    SetupStates = std::move(states);
    assert(!states.get());
  }

  for (const auto &m : MoveList<kLegalForSearch>(pos))
  {
    if
    (
      limits.searchmoves.empty()
      ||
      std::count(limits.searchmoves.begin(), limits.searchmoves.end(), m.move)
    )
      main()->root_moves_.push_back(RootMove(m.move));
  }

  main()->start_searching();
}
