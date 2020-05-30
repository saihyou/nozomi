#include <iostream>
#include <iterator>
#include <map>
#include <algorithm>
#include <cmath>

#include "bit_board.h"
#include "position.h"
#include "book.h"
#include "usi.h"

double 
win_rate(int v)
{
  return 1.0 / (1.0 + std::exp(-v / 600));
}

bool operator<(const BookEntry &left, const BookEntry &right)
{
  return left.key < right.key;
}

const int kBookDepth = 50;
int
main(int argc, char* argv[]) 
{
  BitBoard::initialize();
  Position::initialize();
  std::ifstream in_file(argv[1]);
  std::vector<BookEntry> entry;
  bool eof = false;
  std::string str;
  while (!in_file.eof())
  {
    std::getline(in_file, str);
    if (str.empty())
      break;

    std::string sfen;
    std::string move;
    std::string cp;
    std::istringstream stream(str);
    std::getline(stream, sfen, ',');
    std::getline(stream, move, ',');
    std::getline(stream, cp, ',');
    if (!move.empty())
    {
      BookEntry e;
      Position p;
      p.set(sfen, nullptr);
      e.key = p.key();
      e.move = USI::to_move(p, move);
      e.score = static_cast<uint32_t>(std::stoi(cp) + 1000);
      entry.push_back(e);
    }
  }

  std::sort(entry.begin(), entry.end());

  std::ofstream out_file("new_book.bin", std::ofstream::out | std::ofstream::binary);
  out_file.write(reinterpret_cast<char *>(entry.data()), entry.size() * sizeof(BookEntry));
  
  return 0;
}
