#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <omp.h>
#include <fstream>
#include <vector>
#include <valarray>
#include "move_probability.h"
#include "evaluate.h"
#include "move_generator.h"
#include "search.h"
#include "thread.h"
#include "usi.h"
#include "transposition_table.h"

namespace MoveScore
{

int16_t
g_score_from_to[kNumberOfBoardHand][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
int16_t
g_score_piece_to[kPieceTypeMax][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
int16_t
g_score_capture[kPieceTypeMax][kPieceTypeMax][kNumberOfColor][Eval::kScoreEnd];
int16_t
g_score_check[2][kNumberOfColor][Eval::kScoreEnd];

Value
evaluate(const Position &pos, const CheckInfo &ci, Move move)
{
  Eval::KPPIndex *list_black = pos.black_kpp_list();
  Eval::KPPIndex black_king_index = Eval::kFKing + pos.square_king(kBlack);
  Eval::KPPIndex white_king_index = Eval::kEKing + pos.square_king(kWhite);
  Square from = move_from(move);
  Square to = move_to(move);
  PieceType capture = kPieceNone;
  PieceType move_type = kPieceNone;
  PieceType after_type = kPieceNone;
  if (from >= kBoardSquare)
  {
    after_type = move_type = to_drop_piece_type(from);
  }
  else
  {
    move_type = move_piece_type(move);
    after_type = move_type;
    if (move_is_promote(move))
      after_type = after_type + kFlagPromoted;
    capture = move_capture(move);
  }

  bool gives_check = pos.gives_check(move, ci);
  Color color = pos.side_to_move();
  auto from_to = g_score_from_to[from][to][color];
  auto piece_to = g_score_piece_to[after_type][to][color];
  auto score_capture = g_score_capture[move_type][capture][color];
  auto score_check = g_score_check[gives_check][color];
  __m128i total = _mm_setzero_si128();
  for (int i = 0; i < Eval::kListNum; ++i)
  {
    __m128i tmp = _mm_set_epi16(0, 0, 0, 0, from_to[list_black[i]], piece_to[list_black[i]], score_capture[list_black[i]], score_check[list_black[i]]);
    tmp = _mm_cvtepi16_epi32(tmp);
    total = _mm_add_epi32(total, tmp);
  }
  {
    __m128i tmp = _mm_set_epi16(0, 0, 0, 0, from_to[black_king_index], piece_to[black_king_index], score_capture[black_king_index], score_check[black_king_index]);
    tmp = _mm_cvtepi16_epi32(tmp);
    total = _mm_add_epi32(total, tmp);
    tmp = _mm_set_epi16(0, 0, 0, 0, from_to[white_king_index], piece_to[white_king_index], score_capture[white_king_index], score_check[white_king_index]);
    tmp = _mm_cvtepi16_epi32(tmp);
    total = _mm_add_epi32(total, tmp);
  }

  total = _mm_add_epi32(total, _mm_srli_si128(total, 4));
  total = _mm_add_epi32(total, _mm_srli_si128(total, 8));
  int score = _mm_cvtsi128_si32(total);

  return Value(score);
}

bool
init()
{
  std::ifstream ifs("move_score.bin", std::ios::in | std::ios::binary);
  if (!ifs)
  {
    std::memset(g_score_from_to, 0, sizeof(g_score_from_to));
    std::memset(g_score_piece_to, 0, sizeof(g_score_piece_to));
    std::memset(g_score_capture, 0, sizeof(g_score_capture));
    std::memset(g_score_check, 0, sizeof(g_score_check));

    return false;
  }

  ifs.read(reinterpret_cast<char *>(g_score_from_to), sizeof(g_score_from_to));
  ifs.read(reinterpret_cast<char *>(g_score_piece_to), sizeof(g_score_piece_to));
  ifs.read(reinterpret_cast<char *>(g_score_capture), sizeof(g_score_capture));
  ifs.read(reinterpret_cast<char *>(g_score_check), sizeof(g_score_check));
  ifs.close();

  return true;
}

#ifdef LEARN

int
get_move_ranking(const Position &pos, Move move)
{
  CheckInfo ci(pos);
  std::vector<ExtMove> legal_moves;
  for (auto m : MoveList<kLegalForSearch>(pos))
  {
    m.value = evaluate(pos, ci, m.move);
    legal_moves.push_back(m);
  }
  std::sort(legal_moves.begin(), legal_moves.end(), [](const ExtMove &a, const ExtMove &b){ return a.value > b.value; });
  int rank = 0;
  for (auto &m : legal_moves)
  {
    if (m.move == move)
      return rank;
    ++rank;
  }
  return -1;
}

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

void
read(const std::string &book_file)
{
  std::vector<std::vector<std::string>> book;
  load_book(book_file, book);
  int counter[11] = {0};
  int total = 0;
  uint64_t sum = 0;
  int worst = 0;
  
  for (auto &g : book)
  {
    Search::StateStackPtr state = Search::StateStackPtr(new std::stack<StateInfo>());
    Position pos;
    pos.set("lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1", Threads[0]);
    for (auto &m : g)
    {
      Move move = USI::to_move(pos, m);
      if (move == kMoveNone)
        break;
      state->push(StateInfo());
      int rank = get_move_ranking(pos, move);
      if (rank >= 10 || rank == -1)
        ++counter[10];
      else
        ++counter[rank];
      ++total;
      sum += rank;
      if (rank > worst)
        worst = rank;
      pos.do_move(move, state->top());
    }
  }
  std::cout << "total : " << total << std::endl;
  for (int i = 0; i < 11; ++i)
    std::cout << "     " << i << " : " << counter[i] << " (" << (double)counter[i] / (double)total * 100.0 << " %)" << std::endl;
  std::cout << "average rank : " << (double)sum / (double)total << std::endl;
  std::cout << "worst rank   : " << worst << std::endl;
}

struct LearnEval
{
  float score_from_to[kNumberOfBoardHand][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
  float score_piece_to[kPieceTypeMax][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
  float score_capture[kPieceTypeMax][kPieceTypeMax][kNumberOfColor][Eval::kScoreEnd];
  float score_check[2][kNumberOfColor][Eval::kScoreEnd];

  bool load(const char *file_name);
  void save(const char *file_name);
  void clear();
};

void
LearnEval::save(const char *file_name)
{
  std::FILE *fp = std::fopen(file_name, "wb");
  std::fwrite(this, 1, sizeof(LearnEval), fp);
  std::fclose(fp);
}

bool
LearnEval::load(const char *file_name)
{
  std::FILE *fp = std::fopen(file_name, "rb");
  if (fp == nullptr)
    return false;
  std::fread(this, 1, sizeof(LearnEval), fp);
  std::fclose(fp);
  return true;
}

void
LearnEval::clear()
{
  std::memset(this, 0, sizeof(LearnEval));
}

LearnEval *g_floating_eval;
LearnEval *g_before_update;

void
to_normal_value(const LearnEval &eval)
{
  for (int f = 0; f < kNumberOfBoardHand; ++f)
  {
    for (int t = 0; t < kBoardSquare; ++t)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        g_score_from_to[f][t][0][p] = static_cast<int16_t>(eval.score_from_to[f][t][0][p] * 1024.0);
        g_score_from_to[f][t][1][p] = static_cast<int16_t>(eval.score_from_to[f][t][1][p] * 1024.0);
      }
    }
  }

  for (int a = 0; a < kPieceTypeMax; ++a)
  {
    for (int t = 0; t < kBoardSquare; ++t)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        g_score_piece_to[a][t][0][p] = static_cast<int16_t>(eval.score_piece_to[a][t][0][p] * 1024.0);
        g_score_piece_to[a][t][1][p] = static_cast<int16_t>(eval.score_piece_to[a][t][1][p] * 1024.0);
      }
    }
  }

  for (int m = 0; m < kPieceTypeMax; ++m)
  {
    for (int t = 0; t < kPieceTypeMax; ++t)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        g_score_capture[m][t][0][p] = static_cast<int16_t>(eval.score_capture[m][t][0][p] * 1024.0);
        g_score_capture[m][t][1][p] = static_cast<int16_t>(eval.score_capture[m][t][1][p] * 1024.0);
      }
    }
  }

  for (int p = 0; p < Eval::kFEEnd; ++p)
  {
    g_score_check[0][0][p] = static_cast<int16_t>(eval.score_check[0][0][p] * 1024.0);
    g_score_check[0][1][p] = static_cast<int16_t>(eval.score_check[0][1][p] * 1024.0);
    g_score_check[1][0][p] = static_cast<int16_t>(eval.score_check[1][0][p] * 1024.0);
    g_score_check[1][1][p] = static_cast<int16_t>(eval.score_check[1][1][p] * 1024.0);
  }
}

void
to_floating_value(LearnEval &eval)
{
  for (int f = 0; f < kNumberOfBoardHand; ++f)
  {
    for (int t = 0; t < kBoardSquare; ++t)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        eval.score_from_to[f][t][0][p] = static_cast<float>(g_score_from_to[f][t][0][p]) / 1024.0f;
        eval.score_from_to[f][t][1][p] = static_cast<float>(g_score_from_to[f][t][1][p]) / 1024.0f;
      }
    }
  }

  for (int a = 0; a < kPieceTypeMax; ++a)
  {
    for (int t = 0; t < kBoardSquare; ++t)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        eval.score_piece_to[a][t][0][p] = static_cast<float>(g_score_piece_to[a][t][0][p]) / 1024.0f;
        eval.score_piece_to[a][t][1][p] = static_cast<float>(g_score_piece_to[a][t][1][p]) / 1024.0f;
      }
    }
  }

  for (int m = 0; m < kPieceTypeMax; ++m)
  {
    for (int t = 0; t < kPieceTypeMax; ++t)
    {
      for (int p = 0; p < Eval::kFEEnd; ++p)
      {
        eval.score_capture[m][t][0][p] = static_cast<float>(g_score_capture[m][t][0][p]) / 1024.0f;
        eval.score_capture[m][t][1][p] = static_cast<float>(g_score_capture[m][t][1][p]) / 1024.0f;
      }
    }
  }

  for (int p = 0; p < Eval::kFEEnd; ++p)
  {
    eval.score_check[0][0][p] = static_cast<float>(g_score_check[0][0][p]) / 1024.0f;
    eval.score_check[0][1][p] = static_cast<float>(g_score_check[0][1][p]) / 1024.0f;
    eval.score_check[1][0][p] = static_cast<float>(g_score_check[1][0][p]) / 1024.0f;
    eval.score_check[1][1][p] = static_cast<float>(g_score_check[1][1][p]) / 1024.0f;
  }
}

void
add_atomic_float(std::atomic<float> &target, float param)
{
  float old = target.load(std::memory_order_consume);
  float desired = old + param;
  while
  (
    !target.compare_exchange_weak
    (
      old,
      desired,
      std::memory_order_release,
      std::memory_order_consume
    )
  )
    desired = old + param;
}

struct Gradient
{
  std::atomic<float> score_from_to[kNumberOfBoardHand][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<float> score_piece_to[kPieceTypeMax][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<float> score_capture[kPieceTypeMax][kPieceTypeMax][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<float> score_check[2][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<int>   count_from_to[kNumberOfBoardHand][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<int>   count_piece_to[kPieceTypeMax][kBoardSquare][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<int>   count_capture[kPieceTypeMax][kPieceTypeMax][kNumberOfColor][Eval::kScoreEnd];
  std::atomic<int>   count_check[2][kNumberOfColor][Eval::kScoreEnd];

  void increment(const Position &pos, const CheckInfo &ci, Move m, float delta);
  void clear();
};

void
Gradient::increment(const Position &pos, const CheckInfo &ci, Move m, float delta)
{
  Eval::KPPIndex *list_black = pos.black_kpp_list();
  Eval::KPPIndex black_king_index = Eval::kFKing + pos.square_king(kBlack);
  Eval::KPPIndex white_king_index = Eval::kEKing + pos.square_king(kWhite);
  Square from = move_from(m);
  Square to = move_to(m);
  PieceType capture = kPieceNone;
  PieceType move_type = kPieceNone;

  PieceType after_type = kPieceNone;
  if (from >= kBoardSquare)
  {
    move_type = to_drop_piece_type(from);
  }
  else
  {
    after_type = move_type = move_piece_type(m);
    if (move_is_promote(m))
      after_type = after_type + kFlagPromoted;
    capture = move_capture(m);
  }
  bool gives_check = pos.gives_check(m, ci);
  Color color = pos.side_to_move();
  for (int i = 0; i < Eval::kListNum; ++i)
  {
    add_atomic_float(score_from_to[from][to][color][list_black[i]], delta);
    add_atomic_float(score_piece_to[after_type][to][color][list_black[i]], delta);
    add_atomic_float(score_capture[move_type][capture][color][list_black[i]], delta);
    add_atomic_float(score_check[gives_check][color][list_black[i]], delta);
    count_from_to[from][to][color][list_black[i]].fetch_add(1);
    count_piece_to[after_type][to][color][list_black[i]].fetch_add(1);
    count_capture[move_type][capture][color][list_black[i]].fetch_add(1);
    count_check[gives_check][color][list_black[i]].fetch_add(1);
  }
  add_atomic_float(score_from_to[from][to][color][black_king_index], delta);
  add_atomic_float(score_piece_to[after_type][to][color][black_king_index], delta);
  add_atomic_float(score_capture[move_type][capture][color][black_king_index], delta);
  add_atomic_float(score_check[gives_check][color][black_king_index], delta);
  count_from_to[from][to][color][black_king_index].fetch_add(1);
  count_piece_to[after_type][to][color][black_king_index].fetch_add(1);
  count_capture[move_type][capture][color][black_king_index].fetch_add(1);
  count_check[gives_check][color][black_king_index].fetch_add(1);

  add_atomic_float(score_from_to[from][to][color][white_king_index], delta);
  add_atomic_float(score_piece_to[after_type][to][color][white_king_index], delta);
  add_atomic_float(score_capture[move_type][capture][color][white_king_index], delta);
  add_atomic_float(score_check[gives_check][color][white_king_index], delta);
  count_from_to[from][to][color][white_king_index].fetch_add(1);
  count_piece_to[after_type][to][color][white_king_index].fetch_add(1);
  count_capture[move_type][capture][color][white_king_index].fetch_add(1);
  count_check[gives_check][color][white_king_index].fetch_add(1);
}

void
Gradient::clear()
{
  std::memset(this, 0, sizeof(*this));
}

void
load_param()
{
  if (g_floating_eval->load("score_float.bin"))
  {
    to_normal_value(*g_floating_eval);
    if (!g_before_update->load("score_before.bin"))
      g_before_update->clear();
    return;
  }
  std::ifstream ifs("new_move_score.bin", std::ios::in | std::ios::binary);
  if (ifs)
  {
    ifs.read(reinterpret_cast<char *>(g_score_from_to), sizeof(g_score_from_to));
    ifs.read(reinterpret_cast<char *>(g_score_piece_to), sizeof(g_score_piece_to));
    ifs.read(reinterpret_cast<char *>(g_score_capture), sizeof(g_score_capture));
    ifs.read(reinterpret_cast<char *>(g_score_check), sizeof(g_score_check));
    ifs.close();
  }
  to_floating_value(*g_floating_eval);
  g_before_update->clear();
}

void
save_param()
{
  std::ofstream fs("new_move_score.bin", std::ios::binary);

  fs.write(reinterpret_cast<char *>(g_score_from_to), sizeof(g_score_from_to));
  fs.write(reinterpret_cast<char *>(g_score_piece_to), sizeof(g_score_piece_to));
  fs.write(reinterpret_cast<char *>(g_score_capture), sizeof(g_score_capture));
  fs.write(reinterpret_cast<char *>(g_score_check), sizeof(g_score_check));

  fs.close();
  g_floating_eval->save("score_float.bin");
  g_before_update->save("score_before.bin");
}

struct KifuData
{
  std::string sfen;
  Value       value;
  Color       win;
  std::string next_move;
};

size_t
read_file(std::ifstream &ifs, std::vector<KifuData> &position_list, size_t num_positions, bool &eof)
{
  std::string str;
  size_t num = 0;
  eof = false;
  while (num < num_positions)
  {
    std::getline(ifs, str);

    if (ifs.eof())
    {
      eof = true;
      break;
    }

    if (str.empty())
      continue;

    KifuData data;
    std::istringstream stream(str);
    std::getline(stream, data.sfen, ',');
    std::string value;
    std::getline(stream, value, ',');
    if (value.empty())
      data.value = kValueZero;
    else
      data.value = static_cast<Value>(std::stoi(value));

    std::string win;
    std::getline(stream, win, ',');
    if (win == "b")
      data.win = kBlack;
    else if (win == "w")
      data.win = kWhite;
    else
      data.win = kNumberOfColor;

    std::getline(stream, data.next_move, ',');

    position_list.push_back(data);
    ++num;
  }
  return num;
}

double
compute_gradient(std::vector<KifuData> &position_list, std::vector<Position> &positions, Gradient *gradient)
{
  double diff = 0;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic)
#endif
  for (int i = 0; i < static_cast<int>(position_list.size()); ++i)
  {
    KifuData &data = position_list[i];
    int thread_id = 0;
#ifdef _OPENMP
    thread_id = omp_get_thread_num();
#endif

    if (data.win == kNoColor)
      continue;

    positions[thread_id].set(data.sfen, Threads[thread_id + 1]);

    SearchStack stack[20];
    Move pv[kMaxPly + 1];
    SearchStack *ss = stack + 2;
    std::memset(ss - 2, 0, 5 * sizeof(SearchStack));
    ss->pv = pv;
    StateInfo next_state;
    Move next_move = USI::to_move(positions[thread_id], data.next_move);

    MoveList<kLegalForSearch> list(positions[thread_id]);
    double delta = 0.0;

    CheckInfo ci(positions[thread_id]);
    std::valarray<double> values(list.size());
    for (size_t j = 0; j < list.size(); ++j)
      values[j] = (double)evaluate(positions[thread_id], ci, list[j]) / 1024.0;
    values = std::exp(values - values.max());
    values = values / values.sum();
    for (size_t j = 0; j < list.size(); ++j)
    {
      double d = 0.0;
      if (list[j] == next_move)
        d = 1.0 - values[j];
      else
        d = -values[j];
      gradient->increment(positions[thread_id], ci, list[j], d);
      delta += d * d;
    }

#ifdef _OPENMP
#pragma omp critical
#endif
    {
      diff += delta * delta;
    }
  }
  return diff;
}

void
update(float grad, float *param, float *before, int count, double learning_rate)
{
  const double l = 0.01 * learning_rate;
  double p = 0.0;
  if (count > 0 && (grad > 0.0 || grad < 0.0))
    p = (grad / static_cast<double>(count));

  if (p > 0 || p < 0)
  {
    float v = static_cast<float>(p * learning_rate + 0.9 * *before);
    *param += v;
    *before = v;
    *param = *param / (1 + l);
  }
}

void
add_param(Gradient *param, double learning_rate)
{
#ifdef _OPENMP
#pragma omp parallel
#endif
  {
#ifdef _OPENMP
#pragma omp for
#endif
    for (int f = 0; f < kNumberOfBoardHand; ++f)
    {
      for (int t = 0; t < kBoardSquare; ++t)
      {
        for (int p = 0; p < Eval::kFEEnd; ++p)
        {
          float grad = param->score_from_to[f][t][0][p].load();
          int count = param->count_from_to[f][t][0][p].load();
          update(grad, &g_floating_eval->score_from_to[f][t][0][p], &g_before_update->score_from_to[f][t][0][p], count, learning_rate);
          grad = param->score_from_to[f][t][1][p].load();
          count = param->count_from_to[f][t][1][p].load();
          update(grad, &g_floating_eval->score_from_to[f][t][1][p], &g_before_update->score_from_to[f][t][1][p], count, learning_rate);
        }
      }
    }

#ifdef _OPENMP
#pragma omp for
#endif
    for (int a = 0; a < kPieceTypeMax; ++a)
    {
      for (int t = 0; t < kBoardSquare; ++t)
      {
        for (int p = 0; p < Eval::kFEEnd; ++p)
        {
          float grad = param->score_piece_to[a][t][0][p].load();
          int count = param->count_piece_to[a][t][0][p].load();
          update(grad, &g_floating_eval->score_piece_to[a][t][0][p], &g_before_update->score_piece_to[a][t][0][p], count, learning_rate);
          grad = param->score_piece_to[a][t][1][p].load();
          count = param->count_piece_to[a][t][1][p].load();
          update(grad, &g_floating_eval->score_piece_to[a][t][1][p], &g_before_update->score_piece_to[a][t][1][p], count, learning_rate);
        }
      }
    }

#ifdef _OPENMP
#pragma omp for
#endif
    for (int m = 0; m < kPieceTypeMax; ++m)
    {
      for (int t = 0; t < kPieceTypeMax; ++t)
      {
        for (int p = 0; p < Eval::kFEEnd; ++p)
        {
          float grad = param->score_capture[m][t][0][p].load();
          int count = param->count_capture[m][t][0][p].load();
          update(grad, &g_floating_eval->score_capture[m][t][0][p], &g_before_update->score_capture[m][t][0][p], count, learning_rate);
          grad = param->score_capture[m][t][1][p].load();
          count = param->count_capture[m][t][1][p].load();
          update(grad, &g_floating_eval->score_capture[m][t][1][p], &g_before_update->score_capture[m][t][1][p], count, learning_rate);
        }
      }
    }

#ifdef _OPENMP
#pragma omp for
#endif
    for (int p = 0; p < Eval::kFEEnd; ++p)
    {
      float grad = param->score_check[0][0][p].load();
      int count = param->count_check[0][0][p].load();
      update(grad, &g_floating_eval->score_check[0][0][p], &g_before_update->score_check[0][0][p], count, learning_rate);
      grad = param->score_check[0][1][p].load();
      count = param->count_check[0][1][p].load();
      update(grad, &g_floating_eval->score_check[0][1][p], &g_before_update->score_check[0][1][p], count, learning_rate);
      grad = param->score_check[1][0][p].load();
      count = param->count_check[1][0][p].load();
      update(grad, &g_floating_eval->score_check[1][0][p], &g_before_update->score_check[1][0][p], count, learning_rate);
      grad = param->score_check[1][1][p].load();
      count = param->count_check[1][1][p].load();
      update(grad, &g_floating_eval->score_check[1][1][p], &g_before_update->score_check[1][1][p], count, learning_rate);
    }
  }
}

void
update_param(const std::string &record_file_name, std::vector<Position> &positions, int num_threads, double learning_rate)
{
  load_param();
  double win_diff = 0;
  int batch_size = 100000;
  std::unique_ptr<Gradient> gradient = std::unique_ptr<Gradient>(new Gradient);
  gradient->clear();

  std::vector<KifuData> position_list;
  std::ifstream ifs(record_file_name.c_str());
  bool eof = false;
  int count = 0;

  while (!eof)
  {
    read_file(ifs, position_list, batch_size, eof);
    if (position_list.empty())
      break;

    TT.clear();
    win_diff += compute_gradient(position_list, positions, gradient.get());

    position_list.clear();
    add_param(gradient.get(), learning_rate);
    std::cout << "count : " << ++count << std::endl;
    std::cout << std::sqrt(win_diff / batch_size) << std::endl;
    win_diff = 0;
    gradient->clear();
    to_normal_value(*g_floating_eval);
    if (count % 100 == 0)
      save_param();
  }
  save_param();
}

void
reinforce(std::istringstream &is)
{
  std::string record_file_name;
  int num_threads = 1;
  double learning_rate;

  is >> record_file_name;
  is >> num_threads;
  is >> learning_rate;

  g_floating_eval = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_floating_eval == nullptr)
    return;
  g_before_update = static_cast<LearnEval *>(std::malloc(sizeof(LearnEval)));
  if (g_before_update == nullptr)
  {
    std::free(g_floating_eval);
    return;
  }
  TT.clear();
  Search::Limits.infinite = 1;
  Search::Signals.stop_on_ponder_hit = false;
  Search::Signals.stop = false;

#ifndef _OPENMP
  num_threads = 1;
#endif

  std::vector<Position> positions;
  for (int i = 0; i < num_threads; ++i)
  {
    positions.push_back(Position());
    Threads.push_back(new Thread);
  }

#ifdef _OPENMP
  omp_set_num_threads(num_threads);
#endif

  update_param(record_file_name, positions, num_threads, learning_rate);

  std::free(g_floating_eval);
  std::free(g_before_update);
}
#endif
}
