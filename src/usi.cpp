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

#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"
#include "timeman.h"

using namespace std;

extern void benchmark(const Position& pos, istream& is);

namespace 
{
const char* kStartSFEN = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";

Search::StateStackPtr SetupStates;

const string SquareToStringTable[] =
{
  "9a", "8a", "7a", "6a", "5a", "4a", "3a", "2a", "1a",
  "9b", "8b", "7b", "6b", "5b", "4b", "3b", "2b", "1b",
  "9c", "8c", "7c", "6c", "5c", "4c", "3c", "2c", "1c",
  "9d", "8d", "7d", "6d", "5d", "4d", "3d", "2d", "1d",
  "9e", "8e", "7e", "6e", "5e", "4e", "3e", "2e", "1e",
  "9f", "8f", "7f", "6f", "5f", "4f", "3f", "2f", "1f",
  "9g", "8g", "7g", "6g", "5g", "4g", "3g", "2g", "1g",
  "9h", "8h", "7h", "6h", "5h", "4h", "3h", "2h", "1h",
  "9i", "8i", "7i", "6i", "5i", "4i", "3i", "2i", "1i"
};

const string PieceStringTable[] =
{
  "",
  "P", "L", "N", "S", "B", "R", "G", "K", "+P", "+L", "+N", "+S", "+B", "+R",
  "", "",
  "p", "l", "n", "s", "b", "r", "g", "k", "+p", "+l", "+n", "+s", "+b", "+r"
  ""
};

const char *kPieceToChar = " PLNSBRGK";

void 
position(Position& pos, istringstream& is) 
{
  Move m;
  string token, sfen;

  is >> token;

  if (token == "startpos")
  {
    sfen = kStartSFEN;
    is >> token;
  }
  else if (token == "sfen")
  {
    while (is >> token && token != "moves")
      sfen += token + " ";
  }
  else
  {
    return;
  }

  pos.set(sfen, Threads.main());
  SetupStates = Search::StateStackPtr(new std::stack<StateInfo>());

  while (is >> token && (m = USI::to_move(pos, token)) != kMoveNone)
  {
    SetupStates->push(StateInfo());
    pos.do_move(m, SetupStates->top());
  }
}

void 
setoption(istringstream& is) 
{
  string token, name, value;

  is >> token;

  while (is >> token && token != "value")
    name += string(" ", !name.empty()) + token;

  while (is >> token)
    value += string(" ", !value.empty()) + token;

  if (Options.count(name))
    Options[name] = value;
  else
    sync_cout << "No such option: " << name << sync_endl;
}

void 
go(const Position &pos, istringstream &is) 
{
  Search::LimitsType limits;
  string token;

  limits.start_time = now();
  
  while (is >> token)
  {
    if (token == "searchmoves")
    {
      while (is >> token)
        limits.searchmoves.push_back(USI::to_move(pos, token));
    } 
    else if (token == "wtime")
    {
      is >> limits.time[kWhite];
    }
    else if (token == "btime")
    {
      is >> limits.time[kBlack];
    }
    else if (token == "winc")
    {
      is >> limits.inc[kWhite];
    }
    else if (token == "binc")
    {
      is >> limits.inc[kBlack];
    }
    else if (token == "movestogo")
    {
      is >> limits.movestogo;
    }
    else if (token == "byoyomi")
    {
      is >> limits.byoyomi;
    }
    else if (token == "depth")
    {     
      is >> limits.depth;
    }
    else if (token == "nodes")
    {
      is >> limits.nodes;
    }
    else if (token == "movetime")
    {
      is >> limits.movetime;
    }
    else if (token == "mate")
    {
      is >> limits.mate;
    }
    else if (token == "infinite")
    {
      limits.infinite = true;
    }
    else if (token == "ponder")
    {
      limits.ponder = true;
    }
  }

  Threads.start_thinking(pos, limits, SetupStates);
}

} // namespace

void 
USI::loop(int argc, char* argv[]) 
{
  Position pos(kStartSFEN, Threads.main());
  string token, cmd;

  for (int i = 1; i < argc; ++i)
    cmd += std::string(argv[i]) + " ";

  do 
  {
    if (argc == 1 && !getline(cin, cmd))
    {
      sync_cout << "quit" << sync_endl;
      cmd = "quit";
    }

    istringstream is(cmd);
    
    token.clear();
    is >> skipws >> token;
    
    if (token == "quit" || token == "stop" || token == "ponderhit" || token == "gameover")
    {
      if (token != "ponderhit" || Search::Signals.stop_on_ponder_hit)
      {
        Search::Signals.stop = true;
        Threads.main()->start_searching(true);
      }
      else
      {
        Search::Limits.ponder = false;
      }

      if (token == "gameover")
        is >> token;
    }
    else if (token == "key")
    {
      sync_cout << hex << uppercase << setfill('0')
                << "position key: "   << setw(16) << pos.key()
                << dec << nouppercase << setfill(' ') << sync_endl;
    }
    else if (token == "usi")
    {
      sync_cout << "id name " << engine_info(true)
                << "\n"       << Options
                << "\nusiok"  << sync_endl;
    }
    else if (token == "usinewgame")
    {
      Search::clear();
    }
    else if (token == "go")
    {
      go(pos, is);
    }
    else if (token == "position")
    {
      position(pos, is);
    }
    else if (token == "setoption")
    {
      setoption(is);
    }
    else if (token == "bench")
    {
      benchmark(pos, is);
    }
    else if (token == "isready")
    {
      sync_cout << "readyok" << sync_endl;
    }
    else if (token == "valid")
    {
      if (pos.validate())
        sync_cout << "true"  << sync_endl;
      else
        sync_cout << "false" << sync_endl;
    }
    else if (token == "ismate")
    {
      Move move = search_mate1ply(pos);
      sync_cout << USI::format_move(move) << sync_endl;
    }
    else
    {
      sync_cout << "Unknown command: " << cmd << sync_endl;
    }
  } while (token != "quit" && argc == 1);
  
  Threads.main()->wait_for_search_finished();
}

string 
USI::format_value(Value v, Value alpha, Value beta) 
{
  stringstream ss;

  if (abs(v) < kValueMateInMaxPly)
    ss << "cp " << v * 100 / Eval::kPawnValue;
  else if (v == kValueSamePosition || v == -kValueSamePosition)
    ss << "cp " << v;
  else
    ss << "mate " << (v > 0 ? kValueMate - v : -kValueMate - v);

  ss << (v >= beta ? " lowerbound" : v <= alpha ? " upperbound" : "");

  return ss.str();
}

const string 
USI::format_move(Move m) 
{
  Square from = move_from(m);
  Square to = move_to(m);

  if (m == kMoveNone)
    return "(none)";

  if (from >= kBoardSquare)
  {
    PieceType piece = to_drop_piece_type(from);
    string drop(1, kPieceToChar[piece]);
    drop += "*" + SquareToStringTable[to];
    return drop;
  }
    
  string move = SquareToStringTable[from] + SquareToStringTable[to];
  if (move_is_promote(m))
    move += "+";

  return move;
}

Move 
USI::to_move(const Position &pos, string &str) 
{
  for (MoveList<kLegal> it(pos); *it; ++it)
  {
    if (str == format_move(*it))
      return *it;
  }

  return kMoveNone;
}

std::string
USI::to_sfen(const Position &pos)
{
  string s;
  int empty = 0;
  Square sq = k9A;
  for (File y = kFile1; y < kNumberOfFile; ++y)
  {
    for (Rank x = kRank1; x < kNumberOfRank; ++x)
    {
      Piece p = pos.square(sq);
      if (p != kEmpty)
      {
        if (empty > 0)
        {
          s += std::to_string(empty);
          empty = 0;
        }
        s += PieceStringTable[p];
      }
      else
      {
        empty += 1;
      }
      ++sq;
    }

    if (empty > 0)
    {
      s += std::to_string(empty);
      empty = 0;
    }

    if (y != kNumberOfFile - 1)
      s += "/";
  }

  if (pos.side_to_move() == kBlack)
    s += " b ";
  else
    s += " w ";

  string h;
  for (Color c = kBlack; c < kNumberOfColor; ++c)
  {
    Hand hand = pos.hand(c);
    for (PieceType pt = kPawn; pt < kKing; ++pt)
    {
      int num = number_of(hand, pt);
      if (num > 0)
      {
        if (num > 1)
          h += std::to_string(num);
        Piece p = make_piece(pt, c);
        h += PieceStringTable[p];
      }
    }
  }

  if (h.empty())
    s += "-";
  else
    s += h;

  s += " ";
  s += std::to_string(pos.game_ply());

  return s;
}
