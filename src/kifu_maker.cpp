/*
  nozomi, a USI shogi playing engine
  Copyright (C) 2017 Yuhei Ohmori

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

#include <string>
#include <vector>
#include "kifu_maker.h"
#include "reinforcer.h"
#include "transposition_table.h"
#include "thread.h"
#include "usi.h"

namespace KifuMaker
{
// 探索深さ
constexpr Depth
kSearchDepth = Depth(6);

// 探索打ち切りの点数
constexpr Value
kWinValue = Value(2000);

// book局面からkMinRandamMove以上kMaxRandamMove以下の数、ランダムムーブで動かし
// 探索の初期局面を生成する
constexpr int
kMinRandamMove = 0;

constexpr int
kMaxRandamMove = 25;

// kMinBookMove以上kMaxBookMove以下のbookの手を使いbook局面を生成する
constexpr int
kMinBookMove = 5;

constexpr int
kMaxBookMove = 30;

// 局面数がこの数を超えたら、局面をシャッフルしファイルに書き出す
constexpr int
kKifuStoreNum = 100000;

// ファイルに書き出した回数がこの数になったら終了
constexpr int
kEndCount = 50;

// 合法手の中からランダムで手を返す
Move
pick_randam_move(Position &pos, std::mt19937 &mt)
{
  MoveList<kLegal> ml(pos);
  if (ml.size() == 0)
    return kMoveNone;
  std::uniform_int_distribution<> r(0, static_cast<int>(ml.size()) - 1);
  return ml[r(mt)];
}

// bookfileを読み込む
// bookfileは以下のsfen形式
//   startpos moves 2g2f 3c3d 2f2e 2b3c 7g7f ...
//   startpos moves 7g7f 3c3d 6g6f 3d3e 7i6h ...
void
load_book(const std::string &file_name, std::vector<std::vector<std::string>> &book)
{
  std::ifstream ifs(file_name.c_str());
  while (true)
  {
    std::string sfen;
    std::getline(ifs, sfen);
    if (!ifs)
      break;
    std::istringstream is(sfen);
    std::vector<std::string> moves;
    std::string token;
    is >> token;
    is >> token;
    while (is >> token)
      moves.push_back(token);
    book.push_back(moves);
  }
}

// 自己対局を行い局面を生成する
// 生成した局面はposition_listに追加する
void
play_game(std::vector<PositionData> &position_list, std::vector<std::vector<std::string>> &book)
{
  std::random_device rnd;
  std::mt19937 mt(rnd());
  Search::StateStackPtr state = Search::StateStackPtr(new std::stack<StateInfo>());
  Position pos;
  pos.set("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1", Threads[0]);
  Thread *thread = pos.this_thread();

  if (!book.empty())
  {
    std::uniform_int_distribution<> book_index(0, book.size());
    std::vector<std::string> &moves = book[book_index(mt)];
    std::uniform_int_distribution<> move_index(kMinBookMove, kMaxBookMove);
    int book_end = std::min(move_index(mt), static_cast<int>(moves.size()));
    for (int i = 0; i < book_end; ++i)
    {
      Move m = USI::to_move(pos, moves[i]);
      if (m == kMoveNone)
        return;
      state->push(StateInfo());
      pos.do_move(m, state->top());
    }
  }

  Search::clear();

  Search::Limits.infinite = 1;
  Search::Signals.stop_on_ponder_hit = false;
  Search::Signals.stop = false;

  thread->pv_index_ = 0;
  thread->calls_count_ = 0;
  thread->max_ply_ = 0;
  thread->root_depth_ = kDepthZero;
  std::uniform_int_distribution<> g(kMinRandamMove, kMaxRandamMove);
  int randam_end = g(mt);
  for (int i = 0; i < randam_end; ++i)
  {
    Move m = pick_randam_move(pos, mt);
    if (m == kMoveNone)
      return;

    state->push(StateInfo());
    pos.do_move(m, state->top());
  }

  Color win;
  std::vector<PositionData> game;
  while (true)
  {
    SearchStack stack[kMaxPly + 7];
    SearchStack *ss = stack + 4;
    std::memset(ss - 4, 0, 8 * sizeof(SearchStack));
    for (int i = 4; i > 0; --i)
      (ss - i)->counter_moves = &thread->counter_move_history_[kEmpty][0];

    thread->root_moves_.clear();
    for (auto &m : MoveList<kLegalForSearch>(pos))
      thread->root_moves_.push_back(Search::RootMove(m.move));
    if (thread->root_moves_.empty())
      return;

    Value v = Search::search(pos, ss, -kValueInfinite, kValueInfinite, kSearchDepth);
    if (v < -kWinValue || v > kWinValue || v == kValueDraw)
    {
      if (v < -kWinValue)
        win = ~pos.side_to_move();
      else if (v > kWinValue)
        win = pos.side_to_move();
      else
        win = kNoColor;
      break;
    }
    PositionData data;
    data.value = v;
    data.sfen = USI::to_sfen(pos);
    game.push_back(data);
    std::stable_sort(thread->root_moves_.begin(), thread->root_moves_.end());
    state->push(StateInfo());
    pos.do_move(thread->root_moves_[0].pv[0], state->top());
  }
  for (auto &g : game)
  {
    g.win = win;
    position_list.push_back(g);
  }
}

// 自己対局の局面を生成し、ファイルに書き出す
void
make(std::istringstream &is)
{
  std::string record_file_name;
  is >> record_file_name;

  std::string book_file_name;
  std::vector<std::vector<std::string>> book;
  is >> book_file_name;
  if (!book_file_name.empty())
    load_book(book_file_name, book);

  std::vector<PositionData> position_list;
  int count = 0;
  int write_count = 0;
  while (true)
  {
    play_game(position_list, book);
    ++count;
    if (count % 500 == 0)
      std::cout << count << std::endl;
    else if (count % 100 == 0)
      std::cout << "o" << std::flush;
    else if (count % 10 == 0)
      std::cout << "." << std::flush;
    if (position_list.size() > kKifuStoreNum)
    {
      std::shuffle(position_list.begin(), position_list.end(), std::mt19937());
      std::ofstream out_file(record_file_name, std::ios::out | std::ios::app);
      for (auto &m : position_list)
      {
        out_file << m.sfen << "," << m.value;
        if (m.win == kBlack)
          out_file << ",b" << std::endl;
        else if (m.win == kWhite)
          out_file << ",w" << std::endl;
        else
          out_file << ",d" << std::endl;
      }
      out_file.close();
      position_list.clear();
      ++write_count;
      if (write_count == kEndCount)
        break;
    }
  }
}
}
