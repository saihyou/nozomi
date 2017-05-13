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

#include <fstream>
#include <iostream>
#include <istream>
#include <vector>

#include "misc.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "transposition_table.h"
#include "usi.h"

using namespace std;

static const char* Defaults[] = 
{
  "l6nl/5+P1gk/2np1S3/p1p4Pp/3P2Sp1/1PPb2P1P/P5GS1/R8/LN4bKL w GR5pnsg 1",
  "+R5g2/4g3k/5snp1/p2P1ppN1/4p2P1/P1g1nPP1K/1Pp2SN2/3ps1L2/L1G2b1+r1 w S5Pb2l 1",
  "lnsgk1snl/1r4gb1/p1pppp1pp/6p2/1p7/6R2/PPPPPPP1P/1B7/LNSGKGSNL b P 1",
  "ln4knl/1r3sgb1/3psg1pp/ppp1p1p2/5p3/2PPP4/PPSGSPPPP/2GB3R1/LNK4NL b - 1",

  "l5knl/5rgb1/3p3pp/p+P1s2g2/1np3p2/1S1Pp4/PP1S1P1PP/1K1G3R1/LNB1g2NL w S2P2p 1",
  "6kn1/4+B3l/p3pp1pp/r5p2/2s2Ps2/1P2P3P/P1PP2P+l1/2GKG4/LN3s+bNL b 2GN3Prsp 1",
  "lr5nl/3kg4/p1ns1pg1p/1p1pp4/2P4s1/3PPS+sp1/PPG2P2P/4G1RP1/L1BK3NL b B3Pn 1",
  "ln1g3nl/1r+b2kgs1/p2ppp1pp/1Sp3p2/1P1PB4/1pPS5/PsNG1PP1P/7R1/L1G1K2NL w 2P 1",

  "ln2k2g1/r1s6/pg1gpGssp/2Pp1pP2/4Nn3/2S5B/PPN2P1pP/2KLL4/L8 w B2Pr4p 1",
  "l6nl/1l7/3k3g1/p2spNp2/4bP3/PPPP1p2P/1S2P1N1+b/1KG2G1p1/LN1r5 w RG5P2sp 1",
  "+R3s3l/4g2k1/p1ppppPpp/9/n5+bP1/1PPP2p2/PGS5P/1KBG2L2/LN2r3L b SN3Pgsn 1",
  "ln1b5/1rs2ggk1/p2pp1sp1/1pP2pp1P/P6+s1/1LNPP4/1LSG1P3/1RKG5/LN7 b NPb4p 1",

  "l2g4l/1ks1g4/ppnsp1+Rpp/2ppr4/P6P1/1PP1PP3/1K1PB2+bP/2SNG4/LN1G3NL b 3Ps 1",
  "+R6nl/5s3/4n1+bpp/p2pp3k/1P5l1/1GP2PN2/P2PPN1sP/2gSKp2R/1s2G2LL b Gb5p 1",
  "ln1gk2nl/1r1s1sgb1/p1pp1p1pp/4p1p2/1p7/2PP5/PPBSPPPPP/2GR1K3/LN3GSNL w - 1",
  "lnS1k3l/2r3g2/p2ppgnpp/2psb1R2/5p3/2P6/PP1PPPS1P/1SG1K1G2/LN6L b B2Pn2p 1"
};

void
benchmark(const Position &current, istream &is) 
{
  string token;
  Search::LimitsType limits;
  vector<string> sfens;

  string tt_size    = (is >> token) ? token : "32";
  string threads    = (is >> token) ? token : "1";
  string limit      = (is >> token) ? token : "13";
  string sfen_file  = (is >> token) ? token : "default";
  string limitType  = (is >> token) ? token : "depth";

  Options["USI_Hash"]    = tt_size;
  Options["Threads"] = threads;
  Search::clear();

  if (limitType == "time")
    limits.movetime = 1000 * atoi(limit.c_str());

  else if (limitType == "nodes")
    limits.nodes = atoi(limit.c_str());

  else if (limitType == "mate")
    limits.mate = atoi(limit.c_str());

  else
    limits.depth = atoi(limit.c_str());

  if (sfen_file == "default")
  {
    sfens.assign(Defaults, Defaults + 16);
  }
  else
  {
      string sfen;
      ifstream file(sfen_file.c_str());

      if (!file.is_open())
      {
        cerr << "Unable to open file " << sfen_file << endl;
        return;
      }

      while (getline(file, sfen))
      {
        if (!sfen.empty())
          sfens.push_back(sfen);
      }

      file.close();
  }

  uint64_t nodes = 0;
  TimePoint elapsed = now();

  for (size_t i = 0; i < sfens.size(); ++i)
  {
    Position pos(sfens[i], Threads.main());

    cerr << "\nPosition: " << i + 1 << '/' << sfens.size() << endl;
    Search::StateStackPtr st;
    limits.start_time = now();
    Threads.start_thinking(pos, limits, st);
    Threads.main()->wait_for_search_finished();
    nodes += Threads.nodes_searched();
  }

  elapsed = now() - elapsed + 1;

  cerr << "\n==========================="
       << "\nTotal time (ms) : " << elapsed
       << "\nNodes searched  : " << nodes
       << "\nNodes/second    : " << 1000 * nodes / elapsed << endl;
}
