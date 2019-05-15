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

#include "move_generator.h"
#include <cassert>
#include "position.h"

namespace {

class DropableList {
 public:
  DropableList(const BitBoard &bb) : list_num_(0) {
    BitBoard target = bb;
    while (target) {
      list_[list_num_] = target.pop_bit();
      list_num_++;
    }
  }

  constexpr const Square *begin() const { return &list_[0]; }
  constexpr const Square *end() const { return &list_[list_num_]; }

 private:
  std::array<Square, kBoardSquare> list_;
  int list_num_;
};

template <bool legal>
ExtMove *GeneratePawn(const Position &pos, const BitBoard &movable,
                      ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kPawn, color);
  BitBoard dest = pawn_attack(color, piece);

  dest = dest & movable;
  while (dest) {
    Square to = dest.pop_bit();
    Square from = static_cast<Square>((color == kBlack) ? to + 9 : to - 9);
    bool promote = CanPromote(color, to);
    PieceType capture = pos.piece_type(to);

    if constexpr (legal) {
      if ((color == kBlack && to > k1A) || (color == kWhite && to < k9I)) {
        move->move = move_init(from, to, kPawn, capture, false);
        move++;
      }
      if (promote) {
        move->move = move_init(from, to, kPawn, capture, true);
        move++;
      }
    } else {
      // 歩は成る手しか生成しない
      move->move = move_init(from, to, kPawn, capture, promote);
      move++;
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GeneratePawnCheck(const Position &pos, const BitBoard &movable,
                           const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  const BitBoard target =
      pawn_attack(color, pos.pieces(kPawn, color)) & movable;

  BitBoard promotable = target & PromotableMaskTable[color];
  while (promotable) {
    const Square to = promotable.pop_bit();
    const Square from =
        static_cast<Square>((color == kBlack) ? to + 9 : to - 9);

    if constexpr (discover_check) {
      if ((ci.discover_check_candidates & MaskTable[from]) &&
          !aligned(from, to, pos.square_king(~color))) {
        move->move = move_init(from, to, kPawn, pos.piece_type(to), true);
        move++;
        continue;
      }
    }

    if ((ci.check_squares[kPromotedPawn] & MaskTable[to])) {
      move->move = move_init(from, to, kPawn, pos.piece_type(to), true);
      move++;
    }
  }

  BitBoard not_promotable = target & NotPromotableMaskTable[color];
  while (not_promotable) {
    const Square to = not_promotable.pop_bit();
    const Square from =
        static_cast<Square>((color == kBlack) ? to + 9 : to - 9);

    if constexpr (discover_check) {
      if ((ci.discover_check_candidates & MaskTable[from]) &&
          !aligned(from, to, pos.square_king(~color))) {
        move->move = move_init(from, to, kPawn, pos.piece_type(to), false);
        move++;
        continue;
      }
    }

    if ((ci.check_squares[kPawn] & MaskTable[to])) {
      move->move = move_init(from, to, kPawn, pos.piece_type(to), false);
      move++;
    }
  }

  return move;
}

template <bool legal>
ExtMove *GenerateLance(const Position &pos, const BitBoard &movable,
                       ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kLance, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & lance_attack(pos.occupied(), color, from);

    while (dest) {
      Square to = dest.pop_bit();
      bool promote = CanPromote(color, to);
      PieceType capture = pos.piece_type(to);

      if constexpr (legal) {
        if ((color == kBlack && to > k1A) || (color == kWhite && to < k9I)) {
          move->move = move_init(from, to, kLance, capture, false);
          move++;
        }
      } else {
        // 2段目は必ずなる
        if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H)) {
          move->move = move_init(from, to, kLance, capture, false);
          move++;
        }
      }

      if (promote) {
        move->move = move_init(from, to, kLance, capture, true);
        move++;
      }
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateLanceCheck(const Position &pos, const BitBoard &movable,
                            const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kLance, color);

  while (piece) {
    const Square from = piece.pop_bit();
    BitBoard dest = movable & lance_attack(pos.occupied(), color, from);

    if constexpr (discover_check) {
      if ((ci.discover_check_candidates & MaskTable[from]) &&
          // lanceは前にしか進めないので、File方向の場合のみ空き王手とならない
          DirectionTable[from][pos.square_king(~color)] != kDirFile) {
        // この場合は、すべて王手となる
        while (dest) {
          const Square to = dest.pop_bit();
          const bool promote = CanPromote(color, to);
          const PieceType capture = pos.piece_type(to);

          if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H)) {
            move->move = move_init(from, to, kLance, capture, false);
            move++;
          }

          if (promote) {
            move->move = move_init(from, to, kLance, capture, true);
            move++;
          }
        }
        // 通常の王手は調べなくても問題ない
        continue;
      }
    }

    BitBoard not_promote_attack = dest & ci.check_squares[kLance];
    not_promote_attack.not_and(MustPromoteMaskTable[color]);
    while (not_promote_attack) {
      const Square to = not_promote_attack.pop_bit();
      const PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kLance, capture, false);
      move++;
    }

    BitBoard promote_attack =
        dest & ci.check_squares[kPromotedLance] & PromotableMaskTable[color];
    while (promote_attack) {
      const Square to = promote_attack.pop_bit();
      const PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kLance, capture, true);
      move++;
    }
  }
  return move;
}

ExtMove *GenerateKnight(const Position &pos, const BitBoard &movable,
                        ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kKnight, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & KnightAttacksTable[color][from];
    while (dest) {
      Square to = dest.pop_bit();
      bool promote = CanPromote(color, to);
      PieceType capture = pos.piece_type(to);

      // 2段目は必ず成る
      if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H)) {
        move->move = move_init(from, to, kKnight, capture, false);
        move++;
      }
      if (promote) {
        move->move = move_init(from, to, kKnight, capture, true);
        move++;
      }
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateKnightCheck(const Position &pos, const BitBoard &movable,
                             const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kKnight, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & KnightAttacksTable[color][from];
    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        // knightは上記の条件を満たせばすべて間接王手になる
        while (dest) {
          Square to = dest.pop_bit();
          bool promote = CanPromote(color, to);
          PieceType capture = pos.piece_type(to);

          // 2段目は必ず成る
          if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H)) {
            move->move = move_init(from, to, kKnight, capture, false);
            move++;
          }
          if (promote) {
            move->move = move_init(from, to, kKnight, capture, true);
            move++;
          }
        }
        continue;
      }
    }

    BitBoard attack = dest & ci.check_squares[kKnight];
    while (attack) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kKnight, capture, false);
      move++;
    }

    attack =
        dest & ci.check_squares[kPromotedKnight] & PromotableMaskTable[color];
    while (attack) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kKnight, capture, true);
      move++;
    }
  }
  return move;
}

ExtMove *GenerateSilver(const Position &pos, const BitBoard &movable,
                        ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kSilver, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & SilverAttacksTable[color][from];
    while (dest) {
      Square to = dest.pop_bit();
      bool promote = CanPromote(color, from, to);
      PieceType capture = pos.piece_type(to);

      move->move = move_init(from, to, kSilver, capture, false);
      move++;
      if (promote) {
        move->move = move_init(from, to, kSilver, capture, true);
        move++;
      }
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateSilverCheck(const Position &pos, const BitBoard &movable,
                             const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kSilver, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & SilverAttacksTable[color][from];

    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        while (dest) {
          Square to = dest.pop_bit();
          bool promote = CanPromote(color, from, to);
          PieceType capture = pos.piece_type(to);

          if (DirectionTable[pos.square_king(~color)][from] !=
              DirectionTable[pos.square_king(~color)][to]) {
            move->move = move_init(from, to, kSilver, capture, false);
            move++;
            if (promote) {
              move->move = move_init(from, to, kSilver, capture, true);
              move++;
            }
          } else {
            // 動く方向と玉の位置が一緒ならば通常の王手になるかチェックする
            if (ci.check_squares[kSilver] & MaskTable[to]) {
              move->move = move_init(from, to, kSilver, capture, false);
              move++;
            }

            if (promote &&
                (ci.check_squares[kPromotedSilver] & MaskTable[to])) {
              move->move = move_init(from, to, kSilver, capture, true);
              move++;
            }
          }
        }
        continue;
      }
    }

    BitBoard attack = dest & ci.check_squares[kSilver];
    while (attack) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kSilver, capture, false);
      move++;
    }

    attack = dest & ci.check_squares[kPromotedSilver];
    while (attack) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      if (CanPromote(color, from, to)) {
        move->move = move_init(from, to, kSilver, capture, true);
        move++;
      }
    }
  }
  return move;
}

ExtMove *GenerateTotalGold(const Position &pos, const BitBoard &movable,
                           ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.total_gold(color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & GoldAttacksTable[color][from];
    while (dest) {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      PieceType place = pos.piece_type(from);

      move->move = move_init(from, to, place, capture, false);
      move++;
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateTotalGoldCheck(const Position &pos, const BitBoard &movable,
                                const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.total_gold(color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & GoldAttacksTable[color][from];
    PieceType place = pos.piece_type(from);

    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        while (dest) {
          Square to = dest.pop_bit();
          PieceType capture = pos.piece_type(to);

          if (DirectionTable[pos.square_king(~color)][from] !=
                  DirectionTable[pos.square_king(~color)][to] ||
              (ci.check_squares[kGold] & MaskTable[to])) {
            move->move = move_init(from, to, place, capture, false);
            move++;
          }
        }
        continue;
      }
    }

    BitBoard attack = dest & ci.check_squares[kGold];

    while (attack) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, place, capture, false);
      move++;
    }
  }
  return move;
}

ExtMove *GenerateKing(const Position &pos, const BitBoard &movable,
                      ExtMove *move) {
  Color color = pos.side_to_move();
  Square from = pos.square_king(color);
  BitBoard dest = movable & KingAttacksTable[from];
  BitBoard occupied = pos.occupied();
  occupied.xor_bit(pos.square_king(color));

  while (dest) {
    Square to = dest.pop_bit();
    PieceType capture = pos.piece_type(to);
    move->move = move_init(from, to, kKing, capture, false);
    move++;
  }

  return move;
}

ExtMove *GenerateKingCheck(const Position &pos, const BitBoard &movable,
                           const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  Square from = pos.square_king(color);

  if (ci.discover_check_candidates &&
      (ci.discover_check_candidates & MaskTable[from])) {
    BitBoard dest = movable & KingAttacksTable[from];
    BitBoard occupied = pos.occupied();
    occupied.xor_bit(pos.square_king(color));

    while (dest) {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      if (DirectionTable[pos.square_king(~color)][from] !=
          DirectionTable[pos.square_king(~color)][to]) {
        move->move = move_init(from, to, kKing, capture, false);
        move++;
      }
    }
  }

  return move;
}

template <bool legal>
ExtMove *GenerateBishop(const Position &pos, const BitBoard &movable,
                        ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kBishop, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & bishop_attack(pos.occupied(), from);
    while (dest) {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      bool promote = CanPromote(color, from, to);
      if constexpr (legal) {
        move->move = move_init(from, to, kBishop, capture, false);
        move++;
        if (promote) {
          move->move = move_init(from, to, kBishop, capture, true);
          move++;
        }
      } else {
        move->move = move_init(from, to, kBishop, capture, promote);
        move++;
      }
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateBishopCheck(const Position &pos, const BitBoard &movable,
                             const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kBishop, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & bishop_attack(pos.occupied(), from);
    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        // この場合全て王手
        // 角の後ろに飛車か香車がいるときにしか空き王手にならないので、
        // どの方向に動いても王手になるはず
        while (dest.test()) {
          Square to = dest.pop_bit();
          PieceType capture = pos.piece_type(to);
          move->move = move_init(from, to, kBishop, capture,
                                 CanPromote(color, from, to));
          move++;
        }
        continue;
      }
    }

    BitBoard attack = dest & ci.check_squares[kBishop];
    while (attack.test()) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move =
          move_init(from, to, kBishop, capture, CanPromote(color, from, to));
      ++move;
    }

    attack = dest & ci.check_squares[kHorse];
    while (attack.test()) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      if (CanPromote(color, from, to) &&
          // 角でも王手になる手は生成済み
          !(DirectionTable[pos.square_king(~color)][to] & kDirFlagDiag)) {
        move->move = move_init(from, to, kBishop, capture, true);
        ++move;
      }
    }
  }
  return move;
}

template <bool legal>
ExtMove *GenerateRook(const Position &pos, const BitBoard &movable,
                      ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kRook, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & rook_attack(pos.occupied(), from);
    while (dest) {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      bool promote = CanPromote(color, from, to);
      if constexpr (legal) {
        move->move = move_init(from, to, kRook, capture, false);
        move++;
        if (promote) {
          move->move = move_init(from, to, kRook, capture, true);
          move++;
        }
      } else {
        move->move = move_init(from, to, kRook, capture, promote);
        move++;
      }
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateRookCheck(const Position &pos, const BitBoard &movable,
                           const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kRook, color);

  while (piece.test()) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & rook_attack(pos.occupied(), from);

    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        // この場合全て王手
        // 飛車のときは角が後ろにいるときしか空き王手にならないので、
        // どの方向に動いても王手になるはず
        while (dest) {
          Square to = dest.pop_bit();
          PieceType capture = pos.piece_type(to);
          move->move =
              move_init(from, to, kRook, capture, CanPromote(color, from, to));
          move++;
        }
        continue;
      }
    }

    BitBoard attack = dest & ci.check_squares[kRook];
    while (attack.test()) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move =
          move_init(from, to, kRook, capture, CanPromote(color, from, to));
      move++;
    }

    attack = dest & ci.check_squares[kDragon];
    while (attack.test()) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      if (CanPromote(color, from, to) &&
          // 成らずに王手になる手は生成済み
          !(DirectionTable[pos.square_king(~color)][to] & kDirFlagCross)) {
        move->move = move_init(from, to, kRook, capture, true);
        move++;
      }
    }
  }
  return move;
}

ExtMove *GenerateHorse(const Position &pos, const BitBoard &movable,
                       ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kHorse, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & horse_attack(pos.occupied(), from);
    while (dest) {
      Square to = dest.pop_bit();
      move->move = move_init(from, to, kHorse, pos.piece_type(to), false);
      move++;
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateHorseCheck(const Position &pos, const BitBoard &movable,
                            const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kHorse, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & horse_attack(pos.occupied(), from);
    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        while (dest) {
          Square to = dest.pop_bit();
          PieceType capture = pos.piece_type(to);

          if (DirectionTable[pos.square_king(~color)][from] !=
                  DirectionTable[pos.square_king(~color)][to] ||
              (ci.check_squares[kHorse] & MaskTable[to])) {
            move->move = move_init(from, to, kHorse, capture, false);
            move++;
          }
        }
        continue;
      }
    }

    BitBoard attack = dest & ci.check_squares[kHorse];
    while (attack.test()) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kHorse, capture, false);
      move++;
    }
  }
  return move;
}

ExtMove *GenerateDragon(const Position &pos, const BitBoard &movable,
                        ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kDragon, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & dragon_attack(pos.occupied(), from);
    while (dest) {
      Square to = dest.pop_bit();
      move->move = move_init(from, to, kDragon, pos.piece_type(to), false);
      move++;
    }
  }
  return move;
}

template <bool discover_check>
ExtMove *GenerateDragonCheck(const Position &pos, const BitBoard &movable,
                             const CheckInfo &ci, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kDragon, color);

  while (piece) {
    Square from = piece.pop_bit();
    BitBoard dest = movable & dragon_attack(pos.occupied(), from);
    if constexpr (discover_check) {
      if (ci.discover_check_candidates & MaskTable[from]) {
        while (dest) {
          Square to = dest.pop_bit();
          PieceType capture = pos.piece_type(to);

          if (DirectionTable[pos.square_king(~color)][from] !=
                  DirectionTable[pos.square_king(~color)][to] ||
              (ci.check_squares[kDragon] & MaskTable[to])) {
            move->move = move_init(from, to, kDragon, capture, false);
            move++;
          }
        }
      }
      continue;
    }

    BitBoard attack = dest & ci.check_squares[kDragon];
    while (attack) {
      Square to = attack.pop_bit();
      PieceType capture = pos.piece_type(to);
      move->move = move_init(from, to, kDragon, capture, false);
      move++;
    }
  }
  return move;
}

ExtMove *GeneratePawnDrop(const Position &pos, const BitBoard &bb,
                          ExtMove *move) {
  Color color = pos.side_to_move();
  const BitBoard pawn = pos.pieces(kPawn, color);
  const uint64_t p = pawn.ToUint64();
  const uint64_t b = 0x1FF;
  uint64_t pawn_exist = (p & b) | ((p >> 9) & b) | ((p >> 18) & b) |
                        ((p >> 27) & b) | ((p >> 36) & b) | ((p >> 45) & b) |
                        ((p >> 54) & b);
  BitBoard target = bb & PawnDropableTable[pawn_exist][color];
  Square to;
  while (target.test()) {
    to = target.pop_bit();
    if (!pos.gives_mate_by_drop_pawn(to)) {
      move->move = move_init(to, kPawn);
      move++;
    }
  }

  return move;
}

// kPawnは非対応
template <PieceType type>
ExtMove *GenerateDrop(const Position &pos, const DropableList &list,
                      ExtMove *move) {
  if constexpr (type == kLance) {
    Color color = pos.side_to_move();
    for (auto &sq : list) {
      if ((color == kBlack && sq > k1A) || (color == kWhite && sq < k9I)) {
        move->move = move_init(sq, kLance);
        move++;
      }
    }
  } else if constexpr (type == kKnight) {
    Color color = pos.side_to_move();
    for (auto &sq : list) {
      if ((color == kBlack && sq > k1B) || (color == kWhite && sq < k9H)) {
        move->move = move_init(sq, kKnight);
        move++;
      }
    }
  } else {
    for (auto &sq : list) {
      move->move = move_init(sq, type);
      move++;
    }
  }
  return move;
}

ExtMove *GenerateDropMany(const Position &pos, const BitBoard &bb,
                          HandType piece_type, ExtMove *move) {
  DropableList list(bb);

  if (piece_type & kHandLanceExist)
    move = GenerateDrop<kLance>(pos, list, move);
  if (piece_type & kHandKnightExist)
    move = GenerateDrop<kKnight>(pos, list, move);
  if (piece_type & kHandSilverExist)
    move = GenerateDrop<kSilver>(pos, list, move);
  if (piece_type & kHandGoldExist) move = GenerateDrop<kGold>(pos, list, move);
  if (piece_type & kHandBishopExist)
    move = GenerateDrop<kBishop>(pos, list, move);
  if (piece_type & kHandRookExist) move = GenerateDrop<kRook>(pos, list, move);
  return move;
}

template <PieceType type>
ExtMove *GenerateDropOne(const Position &pos, const BitBoard &bb,
                         ExtMove *move) {
  Color color = pos.side_to_move();
  if constexpr (type == kLance) {
    BitBoard target = bb & LanceDropableMaskTable[color];
    while (target) {
      Square sq = target.pop_bit();
      move->move = move_init(sq, kLance);
      move++;
    }
  } else if constexpr (type == kKnight) {
    BitBoard target = bb & KnightDropableMaskTable[color];
    while (target) {
      Square sq = target.pop_bit();
      move->move = move_init(sq, kKnight);
      move++;
    }
  } else {
    BitBoard target = bb;
    while (target) {
      Square sq = target.pop_bit();
      move->move = move_init(sq, type);
      move++;
    }
  }
  return move;
}

ExtMove *GenerateDrop(const Position &pos, const BitBoard &bb, ExtMove *move) {
  Color color = pos.side_to_move();
  const Hand hand = pos.hand(color);
  if (has_hand(hand, kPawn)) move = GeneratePawnDrop(pos, bb, move);

  if (!has_hand_except_pawn(hand)) return move;

  HandType piece_type = extract_piece_without_pawn(hand);

  switch (piece_type) {
    case kHandLanceExist:
      move = GenerateDropOne<kLance>(pos, bb, move);
      break;
    case kHandKnightExist:
      move = GenerateDropOne<kKnight>(pos, bb, move);
      break;
    case kHandSilverExist:
      move = GenerateDropOne<kSilver>(pos, bb, move);
      break;
    case kHandGoldExist:
      move = GenerateDropOne<kGold>(pos, bb, move);
      break;
    case kHandBishopExist:
      move = GenerateDropOne<kBishop>(pos, bb, move);
      break;
    case kHandRookExist:
      move = GenerateDropOne<kRook>(pos, bb, move);
      break;
    default:
      move = GenerateDropMany(pos, bb, piece_type, move);
      break;
  }

  return move;
}

ExtMove *GenerateDropCheck(const Position &pos, const BitBoard &bb,
                           ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard dest = bb & PawnAttacksTable[~color][pos.square_king(~color)];
  const Hand hand = pos.hand(color);
  if (has_hand(hand, kPawn)) move = GeneratePawnDrop(pos, dest, move);

  if (!has_hand_except_pawn(hand)) return move;

  Square sq;
  if (has_hand(hand, kLance)) {
    dest = bb & lance_attack(pos.occupied(), ~color, pos.square_king(~color)) &
           LanceDropableMaskTable[color];
    while (dest) {
      sq = dest.pop_bit();
      move->move = move_init(sq, kLance);
      move++;
    }
  }

  if (has_hand(hand, kKnight)) {
    dest = bb & KnightAttacksTable[~color][pos.square_king(~color)] &
           KnightDropableMaskTable[color];
    while (dest) {
      sq = dest.pop_bit();
      move->move = move_init(sq, kKnight);
      move++;
    }
  }

  if (has_hand(hand, kSilver)) {
    dest = bb & SilverAttacksTable[~color][pos.square_king(~color)];
    while (dest) {
      sq = dest.pop_bit();
      move->move = move_init(sq, kSilver);
      move++;
    }
  }

  if (has_hand(hand, kGold)) {
    dest = bb & GoldAttacksTable[~color][pos.square_king(~color)];
    while (dest) {
      sq = dest.pop_bit();
      move->move = move_init(sq, kGold);
      move++;
    }
  }

  if (has_hand(hand, kBishop)) {
    dest = bb & bishop_attack(pos.occupied(), pos.square_king(~color));
    while (dest) {
      sq = dest.pop_bit();
      move->move = move_init(sq, kBishop);
      move++;
    }
  }

  if (has_hand(hand, kRook)) {
    dest = bb & rook_attack(pos.occupied(), pos.square_king(~color));
    while (dest) {
      sq = dest.pop_bit();
      move->move = move_init(sq, kRook);
      move++;
    }
  }

  return move;
}
}  // namespace

template <GenType type>
ExtMove *generate(const Position &pos, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard target;

  if constexpr (type == kCaptures) {
    target = pos.pieces(kOccupied, ~color);
  } else if constexpr (type == kQuiets) {
    target = pos.occupied();
    target = ~target;
  } else if constexpr (type == kNonEvasions) {
    target = pos.pieces(kOccupied, color);
    target = ~target;
  }
  move = GeneratePawn<false>(pos, target, move);
  move = GenerateLance<false>(pos, target, move);
  move = GenerateKnight(pos, target, move);
  move = GenerateSilver(pos, target, move);
  move = GenerateTotalGold(pos, target, move);
  move = GenerateBishop<false>(pos, target, move);
  move = GenerateRook<false>(pos, target, move);
  move = GenerateHorse(pos, target, move);
  move = GenerateDragon(pos, target, move);
  move = GenerateKing(pos, target, move);
  if constexpr (type == kQuiets) {
    if (pos.hand(color) != kHandZero) move = GenerateDrop(pos, target, move);
  } else if constexpr (type == kNonEvasions) {
    if (pos.hand(color) != kHandZero) {
      target = pos.occupied();
      target = ~target;
      move = GenerateDrop(pos, target, move);
    }
  }
  return move;
}

template ExtMove *generate<kCaptures>(const Position &, ExtMove *);
template ExtMove *generate<kQuiets>(const Position &, ExtMove *);
template ExtMove *generate<kNonEvasions>(const Position &, ExtMove *);

template <>
ExtMove *generate<kEvasions>(const Position &pos, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard oc = ~pos.pieces(kOccupied, color) | pos.pieces(kOccupied, ~color);

  move = GenerateKing(pos, oc, move);

  BitBoard checker = pos.checkers_bitboard();

  if (checker.popcount() > 1) {
    // 両王手
    return move;
  }

  int check_sq = checker.first_one();
  BitBoard inter = BetweenTable[pos.square_king(color)][check_sq];
  BitBoard target = inter | checker;

  move = GeneratePawn<false>(pos, target, move);
  move = GenerateLance<false>(pos, target, move);
  move = GenerateKnight(pos, target, move);
  move = GenerateSilver(pos, target, move);
  move = GenerateTotalGold(pos, target, move);
  move = GenerateBishop<false>(pos, target, move);
  move = GenerateRook<false>(pos, target, move);
  move = GenerateHorse(pos, target, move);
  move = GenerateDragon(pos, target, move);
  if (pos.hand(color) != kHandZero && inter)
    move = GenerateDrop(pos, inter, move);

  return move;
}

ExtMove *generate_legal_evasion(const Position &pos, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard oc = ~pos.pieces(kOccupied, color) | pos.pieces(kOccupied, ~color);
  move = GenerateKing(pos, oc, move);

  BitBoard checker = pos.checkers_bitboard();

  if (checker.popcount() > 1) {
    // 両王手
    return move;
  }

  int check_sq = checker.first_one();
  BitBoard inter = BetweenTable[pos.square_king(color)][check_sq];
  BitBoard target = inter | checker;

  move = GeneratePawn<true>(pos, target, move);
  move = GenerateLance<true>(pos, target, move);
  move = GenerateKnight(pos, target, move);
  move = GenerateSilver(pos, target, move);
  move = GenerateTotalGold(pos, target, move);
  move = GenerateBishop<true>(pos, target, move);
  move = GenerateRook<true>(pos, target, move);
  move = GenerateHorse(pos, target, move);
  move = GenerateDragon(pos, target, move);
  if (pos.hand(color) != kHandZero) move = GenerateDrop(pos, inter, move);

  return move;
}

ExtMove *generate_legal_nonevasion(const Position &pos, ExtMove *move) {
  BitBoard target = ~pos.pieces(kOccupied, pos.side_to_move());

  move = GeneratePawn<true>(pos, target, move);
  move = GenerateLance<true>(pos, target, move);
  move = GenerateKnight(pos, target, move);
  move = GenerateSilver(pos, target, move);
  move = GenerateTotalGold(pos, target, move);
  move = GenerateBishop<true>(pos, target, move);
  move = GenerateRook<true>(pos, target, move);
  move = GenerateHorse(pos, target, move);
  move = GenerateDragon(pos, target, move);
  move = GenerateKing(pos, target, move);

  move = GenerateDrop(pos, ~pos.occupied(), move);

  return move;
}

template <>
ExtMove *generate<kLegal>(const Position &pos, ExtMove *mlist) {
  ExtMove *end;
  ExtMove *cur = mlist;
  Color side_to_move = pos.side_to_move();

  end = pos.in_check() ? generate_legal_evasion(pos, mlist)
                       : generate_legal_nonevasion(pos, mlist);

  BitBoard pinned = pos.pinned_pieces(side_to_move);
  while (cur != end) {
    if (!pos.legal(cur->move, pinned))
      cur->move = (--end)->move;
    else
      ++cur;
  }

  return end;
}

template <>
ExtMove *generate<kChecks>(const Position &pos, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard target;
  CheckInfo ci(pos);

  target = ~pos.pieces(kOccupied, color);

  if ((ci.discover_check_candidates & pos.pieces(kPawn, color)).test())
    move = GeneratePawnCheck<true>(pos, target, ci, move);
  else
    move = GeneratePawnCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kLance, color)).test())
    move = GenerateLanceCheck<true>(pos, target, ci, move);
  else
    move = GenerateLanceCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kKnight, color)).test())
    move = GenerateKnightCheck<true>(pos, target, ci, move);
  else
    move = GenerateKnightCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kSilver, color)).test())
    move = GenerateSilverCheck<true>(pos, target, ci, move);
  else
    move = GenerateSilverCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.total_gold(color)).test())
    move = GenerateTotalGoldCheck<true>(pos, target, ci, move);
  else
    move = GenerateTotalGoldCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kBishop, color)).test())
    move = GenerateBishopCheck<true>(pos, target, ci, move);
  else
    move = GenerateBishopCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kRook, color)).test())
    move = GenerateRookCheck<true>(pos, target, ci, move);
  else
    move = GenerateRookCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kHorse, color)).test())
    move = GenerateHorseCheck<true>(pos, target, ci, move);
  else
    move = GenerateHorseCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kDragon, color)).test())
    move = GenerateDragonCheck<true>(pos, target, ci, move);
  else
    move = GenerateDragonCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kKing, color)).test())
    move = GenerateKingCheck(pos, target, ci, move);

  if (pos.hand(color) != kHandZero)
    move = GenerateDropCheck(pos, ~pos.occupied(), move);
  return move;
}

template <>
ExtMove *generate<kQuietChecks>(const Position &pos, ExtMove *move) {
  Color color = pos.side_to_move();
  BitBoard target = ~pos.occupied();
  CheckInfo ci(pos);

  if ((ci.discover_check_candidates & pos.pieces(kPawn, color)).test())
    move = GeneratePawnCheck<true>(pos, target, ci, move);
  else
    move = GeneratePawnCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kLance, color)).test())
    move = GenerateLanceCheck<true>(pos, target, ci, move);
  else
    move = GenerateLanceCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kKnight, color)).test())
    move = GenerateKnightCheck<true>(pos, target, ci, move);
  else
    move = GenerateKnightCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kSilver, color)).test())
    move = GenerateSilverCheck<true>(pos, target, ci, move);
  else
    move = GenerateSilverCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.total_gold(color)).test())
    move = GenerateTotalGoldCheck<true>(pos, target, ci, move);
  else
    move = GenerateTotalGoldCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kBishop, color)).test())
    move = GenerateBishopCheck<true>(pos, target, ci, move);
  else
    move = GenerateBishopCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kRook, color)).test())
    move = GenerateRookCheck<true>(pos, target, ci, move);
  else
    move = GenerateRookCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kHorse, color)).test())
    move = GenerateHorseCheck<true>(pos, target, ci, move);
  else
    move = GenerateHorseCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kDragon, color)).test())
    move = GenerateDragonCheck<true>(pos, target, ci, move);
  else
    move = GenerateDragonCheck<false>(pos, target, ci, move);

  if ((ci.discover_check_candidates & pos.pieces(kKing, color)).test())
    move = GenerateKingCheck(pos, target, ci, move);

  if (pos.hand(pos.side_to_move()) != kHandZero)
    move = GenerateDropCheck(pos, target, move);

  return move;
}

template <>
ExtMove *generate<kLegalForSearch>(const Position &pos, ExtMove *mlist) {
  ExtMove *end;
  ExtMove *cur = mlist;
  Color side_to_move = pos.side_to_move();

  end = pos.in_check() ? generate<kEvasions>(pos, mlist)
                       : generate<kNonEvasions>(pos, mlist);

  BitBoard pinned = pos.pinned_pieces(side_to_move);
  while (cur != end) {
    if (!pos.legal(cur->move, pinned))
      cur->move = (--end)->move;
    else
      ++cur;
  }

  return end;
}

ExtMove *generate_recapture(const Position &pos, Square s, ExtMove *move) {
  BitBoard target = MaskTable[s];
  move = GeneratePawn<false>(pos, target, move);
  move = GenerateLance<false>(pos, target, move);
  move = GenerateKnight(pos, target, move);
  move = GenerateSilver(pos, target, move);
  move = GenerateTotalGold(pos, target, move);
  move = GenerateBishop<false>(pos, target, move);
  move = GenerateRook<false>(pos, target, move);
  move = GenerateHorse(pos, target, move);
  move = GenerateDragon(pos, target, move);
  move = GenerateKing(pos, target, move);
  return move;
}

bool can_king_escape(const Position &pos, Square sq,
                     const BitBoard &check_attack, Color color) {
  BitBoard king_movable =
      ~pos.pieces(kOccupied, color) & KingAttacksTable[pos.square_king(color)];
  king_movable.not_and(check_attack);
  king_movable = king_movable ^ MaskTable[sq];
  const BitBoard occupied = pos.occupied();

  while (king_movable.test()) {
    const Square to = king_movable.pop_bit();
    if (!pos.is_attacked(to, color, occupied)) return true;
  }

  return false;
}

/// 玉が動くことで王手を回避できるか
///
/// @param pos          Positionクラス
/// @param sq           王手をかけた駒の位置
/// @param check_attack 王手をかけた駒の利き
/// @param color        王手をかけられている側
/// @param occupied     occupied bitboard
bool can_king_escape(const Position &pos, Square sq,
                     const BitBoard &check_attack, Color color,
                     const BitBoard &occupied) {
  BitBoard king_movable =
      ~pos.pieces(kOccupied, color) & KingAttacksTable[pos.square_king(color)];
  // 王手をかけた駒の利きがある場所にはいけない
  king_movable.not_and(check_attack);
  // 王手をかけた駒の場所にも行けない
  king_movable = king_movable ^ MaskTable[sq];
  while (king_movable.test()) {
    const Square to = king_movable.pop_bit();
    if (!pos.is_attacked(to, color, occupied)) return true;
  }

  return false;
}

bool can_king_escape(const Position &pos, Color color,
                     const BitBoard &occupied) {
  BitBoard king_movable =
      ~pos.pieces(kOccupied, color) & KingAttacksTable[pos.square_king(color)];

  while (king_movable.test()) {
    const Square to = king_movable.pop_bit();
    if (!pos.is_attacked(to, color, occupied)) return true;
  }

  return false;
}

bool can_piece_capture(const Position &pos, Square sq, const BitBoard &pinned,
                       Color color, const BitBoard &occupied) {
  BitBoard attack = pos.pieces(kPawn, color) & PawnAttacksTable[~color][sq];
  attack.and_or(pos.pieces(kKnight, color), KnightAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kSilver, color), SilverAttacksTable[~color][sq]);
  attack.and_or(pos.total_gold(color), GoldAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kHorse, color) | pos.pieces(kDragon, color),
                KingAttacksTable[sq]);
  attack.and_or(pos.bishop_horse(color), bishop_attack(occupied, sq));
  attack.and_or((pos.rook_dragon(color) | (pos.pieces(kLance, color) &
                                           LanceAttacksTable[~color][sq][0])),
                rook_attack(occupied, sq));

  while (attack.test()) {
    const Square from = attack.pop_bit();
    if (!pos.is_king_discover(from, sq, color, pinned)) return true;
  }

  return false;
}

bool can_piece_capture(const Position &pos, Square sq, Color color,
                       const BitBoard &occupied) {
  BitBoard attack = pos.pieces(kPawn, color) & PawnAttacksTable[~color][sq];
  attack.and_or(pos.pieces(kKnight, color), KnightAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kSilver, color), SilverAttacksTable[~color][sq]);
  attack.and_or(pos.total_gold(color), GoldAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kHorse, color) | pos.pieces(kDragon, color),
                KingAttacksTable[sq]);
  attack.and_or(pos.bishop_horse(color), bishop_attack(occupied, sq));
  attack.and_or((pos.rook_dragon(color) | (pos.pieces(kLance, color) &
                                           LanceAttacksTable[~color][sq][0])),
                rook_attack(occupied, sq));

  const BitBoard pinned = pos.pinned_pieces(color, occupied);
  while (attack.test()) {
    const Square from = attack.pop_bit();
    if (!pos.is_king_discover(from, sq, color, pinned)) return true;
  }

  return false;
}

// この辺はAperyを参考にしている
Move search_drop_mate(Position &pos, const BitBoard &bb) {
  Color color = pos.side_to_move();
  BitBoard dest;
  Square sq;
  bool result;
  const Square enemy = pos.square_king(~color);
  BitBoard occupied = pos.occupied();
  const Hand hand = pos.hand(color);
  const BitBoard pinned = pos.pinned_pieces(~color);

  if (has_hand(hand, kRook)) {
    // 隣接王手だけ考える
    dest = bb & RookStepAttacksTable[enemy];
    while (dest.test()) {
      sq = dest.pop_bit();
      // sqの場所に自駒の利きがなければ敵の王にとられる
      if (pos.is_attacked(sq, ~color, occupied)) {
        BitBoard new_occupied = occupied ^ MaskTable[sq];
        result = (can_king_escape(pos, sq, RookAttacksTable[sq][0], ~color,
                                  new_occupied) ||
                  can_piece_capture(pos, sq, pinned, ~color, new_occupied));
        if (!result) return move_init(sq, kRook);
      }
    }
  } else if (has_hand(hand, kLance)) {
    // else ifなのは飛車で詰まない場合は香車でも詰まないから
    dest = bb & PawnAttacksTable[~color][enemy] & LanceDropableMaskTable[color];
    if (dest.test()) {
      sq = (color == kBlack) ? Square(enemy + 9) : Square(enemy - 9);
      if (pos.is_attacked(sq, ~color, occupied)) {
        BitBoard new_occupied = occupied ^ MaskTable[sq];
        result = (can_king_escape(pos, sq, LanceAttacksTable[color][sq][0],
                                  ~color, new_occupied) ||
                  can_piece_capture(pos, sq, pinned, ~color, new_occupied));
        if (!result) return move_init(sq, kLance);
      }
    }
  }

  if (has_hand(hand, kBishop)) {
    dest = bb & BishopStepAttacksTable[enemy];
    while (dest.test()) {
      sq = dest.pop_bit();
      if (pos.is_attacked(sq, ~color, occupied)) {
        BitBoard new_occupied = occupied ^ MaskTable[sq];
        result = (can_king_escape(pos, sq, BishopAttacksTable[sq][0], ~color,
                                  new_occupied) ||
                  can_piece_capture(pos, sq, pinned, ~color, new_occupied));
        if (!result) return move_init(sq, kBishop);
      }
    }
  }

  if (has_hand(hand, kGold)) {
    // 飛車打ちを調べたので後ろに動くのは調べなくてもよい
    // 前は斜めにも動けるので、調べないといけない
    if (has_hand(hand, kRook))
      dest = bb &
             (GoldAttacksTable[~color][enemy] ^ PawnAttacksTable[color][enemy]);
    else
      dest = bb & GoldAttacksTable[~color][enemy];
    while (dest.test()) {
      sq = dest.pop_bit();
      if (pos.is_attacked(sq, ~color, occupied)) {
        BitBoard new_occupied = occupied ^ MaskTable[sq];
        result = (can_king_escape(pos, sq, GoldAttacksTable[color][sq], ~color,
                                  new_occupied) ||
                  can_piece_capture(pos, sq, pinned, ~color, new_occupied));
        if (!result) return move_init(sq, kGold);
      }
    }
  }

  if (has_hand(hand, kSilver)) {
    if (has_hand(hand, kGold)) {
      // 金打ちを先に調べているので、斜め後ろだけしらべればよい
      if (has_hand(hand, kBishop)) {
        // 角打ちを調べているので、銀では詰まない
        goto silver_end;
      }
      dest = bb & (SilverAttacksTable[~color][enemy] &
                   GoldAttacksTable[color][enemy]);
    } else {
      if (has_hand(hand, kBishop)) {
        // 角打ちを調べているので前だけしらべればよい
        dest = bb & (SilverAttacksTable[~color][enemy] &
                     GoldAttacksTable[~color][enemy]);
      } else {
        dest = bb & SilverAttacksTable[~color][enemy];
      }
    }
    while (dest.test()) {
      sq = dest.pop_bit();
      if (pos.is_attacked(sq, ~color, occupied)) {
        BitBoard new_occupied = occupied ^ MaskTable[sq];
        result = (can_king_escape(pos, sq, SilverAttacksTable[color][sq],
                                  ~color, new_occupied) ||
                  can_piece_capture(pos, sq, pinned, ~color, new_occupied));
        if (!result) return move_init(sq, kSilver);
      }
    }
  }

silver_end:
  if (has_hand(hand, kKnight)) {
    dest =
        bb & KnightAttacksTable[~color][enemy] & KnightDropableMaskTable[color];
    while (dest.test()) {
      sq = dest.pop_bit();
      // 桂馬はsqの場所に利きがなくても王が取れない
      BitBoard new_occupied = occupied ^ MaskTable[sq];
      result = (can_king_escape(pos, ~color, new_occupied) ||
                can_piece_capture(pos, sq, pinned, ~color, new_occupied));
      if (!result) return move_init(sq, kKnight);
    }
  }
  // 打ち歩詰めになるので歩は調べなくてもよい
  return kMoveNone;
}

Move search_pawn_mate(Position &pos, const BitBoard &movable,
                      const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kPawn, color);
  BitBoard dest = movable & pawn_attack(color, piece);
  bool result = true;

  BitBoard enemy_field = dest & PromotableMaskTable[color];
  while (enemy_field.test()) {
    const Square to = enemy_field.pop_bit();
    const Square from =
        static_cast<Square>((color == kBlack) ? to + 9 : to - 9);
    if ((GoldAttacksTable[color][to] & MaskTable[pos.square_king(~color)])
            .test() &&
        !pos.is_king_discover(from, to, color, ci.pinned)) {
      pos.move_with_promotion_temporary(from, to, kPawn, pos.piece_type(to));
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        const BitBoard pinned = pos.pinned_pieces(~color);
        result =
            (can_king_escape(pos, to, GoldAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, pinned, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kPawn, pos.piece_type(to));
      if (!result) return move_init(from, to, kPawn, pos.piece_type(to), true);
    }
  }

  dest = dest & NotPromotableMaskTable[color];
  while (dest.test()) {
    const Square to = dest.pop_bit();
    const Square from =
        static_cast<Square>((color == kBlack) ? to + 9 : to - 9);
    if ((PawnAttacksTable[color][to] & MaskTable[pos.square_king(~color)])
            .test() &&
        !pos.is_king_discover(from, to, color, ci.pinned)) {
      pos.move_temporary(from, to, kPawn, pos.piece_type(to));
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        const BitBoard pinned = pos.pinned_pieces(~color);
        result =
            (can_king_escape(pos, to, PawnAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, pinned, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kPawn, pos.piece_type(to));
      if (!result) return move_init(from, to, kPawn, pos.piece_type(to), false);
    }
  }
  return kMoveNone;
}

Move search_lance_mate(Position &pos, const BitBoard &movable,
                       const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kLance, color);
  bool result = true;

  while (piece.test()) {
    const Square from = piece.pop_bit();
    BitBoard dest = movable & lance_attack(pos.occupied(), color, from);
    BitBoard attack = (dest & PromotableMaskTable[color]) &
                      GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kLance, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result =
            (can_king_escape(pos, to, GoldAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kLance, capture);
      if (!result) return move_init(from, to, kLance, capture, true);
    }

    const BitBoard rank3mask = (color == kBlack)
                                   ? BitBoard(0x7FC0000ULL, 0)
                                   : BitBoard(0x7FC0000000000000ULL, 0);
    attack =
        (dest & rank3mask) & PawnAttacksTable[~color][pos.square_king(~color)];
    if (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kLance, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, LanceAttacksTable[color][to][0],
                                  ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kLance, capture);
      if (!result) return move_init(from, to, kLance, capture, false);
    }

    dest = dest & NotPromotableMaskTable[color];
    attack = dest & PawnAttacksTable[~color][pos.square_king(~color)];
    if (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kLance, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, LanceAttacksTable[color][to][0],
                                  ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kLance, capture);
      if (!result) return move_init(from, to, kLance, capture, false);
    }
  }
  return kMoveNone;
}

Move search_knight_mate(Position &pos, const BitBoard &movable,
                        const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kKnight, color);
  bool result = true;

  while (piece.test()) {
    const Square from = piece.pop_bit();
    const BitBoard dest = movable & KnightAttacksTable[color][from];
    BitBoard attack =
        dest & KnightAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kKnight, capture);
      result = (can_king_escape(pos, ~color, pos.occupied()) ||
                can_piece_capture(pos, to, ~color, pos.occupied()));
      pos.move_temporary(from, to, kKnight, capture);
      if (!result) return move_init(from, to, kKnight, capture, false);
    }

    attack = (dest & PromotableMaskTable[color]) &
             GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kKnight, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result =
            (can_king_escape(pos, to, GoldAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kKnight, capture);
      if (!result) return move_init(from, to, kKnight, capture, true);
    }
  }
  return kMoveNone;
}

Move search_silver_mate(Position &pos, const BitBoard &movable,
                        const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kSilver, color);
  bool result = true;

  // fromが敵陣で成ることができる場合
  BitBoard enemy_field = piece & PromotableMaskTable[color];
  while (enemy_field.test()) {
    const Square from = enemy_field.pop_bit();
    BitBoard dest = movable & SilverAttacksTable[color][from];
    BitBoard attack =
        dest & SilverAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kSilver, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = true;
        if (!can_piece_capture(pos, to, ~color, pos.occupied())) {
          if (!can_king_escape(pos, to, SilverAttacksTable[color][to], ~color))
            result = false;
        } else {
          dest = dest ^ MaskTable[to];
        }
      }
      pos.move_temporary(from, to, kSilver, capture);
      if (!result) return move_init(from, to, kSilver, capture, false);
    }

    attack = dest & GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kSilver, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result =
            (can_king_escape(pos, to, GoldAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kSilver, capture);
      if (!result) return move_init(from, to, kSilver, capture, true);
    }
  }

  piece = piece & NotPromotableMaskTable[color];
  while (piece.test()) {
    // toが敵陣で成ることができる
    const Square from = piece.pop_bit();
    BitBoard promotable = movable & PromotableMaskTable[color];
    BitBoard dest = promotable & SilverAttacksTable[color][from];
    BitBoard attack =
        dest & SilverAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kSilver, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = true;
        if (!can_piece_capture(pos, to, ~color, pos.occupied())) {
          if (!can_king_escape(pos, to, SilverAttacksTable[color][to], ~color))
            result = false;
        } else {
          dest = dest ^ MaskTable[to];
        }
      }
      pos.move_temporary(from, to, kSilver, capture);
      if (!result) return move_init(from, to, kSilver, capture, false);
    }

    attack = dest & GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kSilver, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result =
            (can_king_escape(pos, to, GoldAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kSilver, capture);
      if (!result) return move_init(from, to, kSilver, capture, true);
    }

    // fromもtoも敵陣になく成ることができない
    BitBoard not_promotable = movable & NotPromotableMaskTable[color];
    BitBoard not_promote_dest =
        not_promotable & SilverAttacksTable[color][from];
    BitBoard not_promote_attack =
        not_promote_dest & SilverAttacksTable[~color][pos.square_king(~color)];
    while (not_promote_attack.test()) {
      const Square to = not_promote_attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kSilver, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result =
            (can_king_escape(pos, to, SilverAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kSilver, capture);
      if (!result) return move_init(from, to, kSilver, capture, false);
    }
  }

  return kMoveNone;
}

Move search_total_gold_mate(Position &pos, const BitBoard &movable,
                            const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.total_gold(color);
  bool result = true;

  while (piece.test()) {
    const Square from = piece.pop_bit();
    BitBoard dest = movable & GoldAttacksTable[color][from];
    PieceType place = pos.piece_type(from);
    BitBoard attack = dest & GoldAttacksTable[~color][pos.square_king(~color)];

    while (attack.test()) {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, place, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result =
            (can_king_escape(pos, to, GoldAttacksTable[color][to], ~color) ||
             can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, place, capture);
      if (!result) return move_init(from, to, place, capture, false);
    }
  }
  return kMoveNone;
}

Move search_bishop_mate(Position &pos, const BitBoard &movable,
                        const BitBoard &occupied, const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kBishop, color);
  bool result = true;

  // fromが敵陣で成ることができる場合
  BitBoard enemy_field = piece & PromotableMaskTable[color];
  while (enemy_field.test()) {
    const Square from = enemy_field.pop_bit();
    BitBoard dest = movable & bishop_attack(occupied, from);
    while (dest.test()) {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kBishop, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(
                      pos, to, BishopAttacksTable[to][0] | KingAttacksTable[to],
                      ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kBishop, capture);
      if (!result) return move_init(from, to, kBishop, capture, true);
    }
  }

  const Square enemy = pos.square_king(~color);
  piece = piece & NotPromotableMaskTable[color];
  while (piece.test()) {
    // toが敵陣で成ることができる
    const Square from = piece.pop_bit();
    BitBoard dest = movable & bishop_attack(occupied, from);
    BitBoard promotable = dest & PromotableMaskTable[color];
    while (promotable.test()) {
      const Square to = promotable.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kBishop, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(
                      pos, to, BishopAttacksTable[to][0] | KingAttacksTable[to],
                      ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kBishop, capture);
      if (!result) return move_init(from, to, kBishop, capture, true);
    }

    // fromもtoも敵陣になく成ることができない
    BitBoard not_promotable = dest & NotPromotableMaskTable[color];
    BitBoard not_promotable_dest =
        (not_promotable & (SilverAttacksTable[kBlack][enemy] &
                           SilverAttacksTable[kWhite][enemy]));
    while (not_promotable_dest.test()) {
      const Square to = not_promotable_dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kBishop, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, BishopAttacksTable[to][0], ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kBishop, capture);
      if (!result) return move_init(from, to, kBishop, capture, false);
    }
  }

  return kMoveNone;
}

Move search_rook_mate(Position &pos, const BitBoard &movable,
                      const BitBoard &occupied, const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kRook, color);
  bool result = true;

  // fromが敵陣で成ることができる場合
  BitBoard enemy_field = piece & PromotableMaskTable[color];
  while (enemy_field.test()) {
    const Square from = enemy_field.pop_bit();
    BitBoard dest = movable & rook_attack(occupied, from);
    while (dest.test()) {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kRook, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, dragon_attack(pos.occupied(), to),
                                  ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kRook, capture);
      if (!result) return move_init(from, to, kRook, capture, true);
    }
  }

  const Square enemy = pos.square_king(~color);
  piece = piece & NotPromotableMaskTable[color];
  while (piece.test()) {
    // toが敵陣で成ることができる
    const Square from = piece.pop_bit();
    BitBoard dest = movable & rook_attack(occupied, from);
    BitBoard promotable = dest & PromotableMaskTable[color];
    while (promotable.test()) {
      const Square to = promotable.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_with_promotion_temporary(from, to, kRook, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, dragon_attack(pos.occupied(), to),
                                  ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_with_promotion_temporary(from, to, kRook, capture);
      if (!result) return move_init(from, to, kRook, capture, true);
    }

    // fromもtoも敵陣になく成ることができない
    BitBoard not_promotable = dest & NotPromotableMaskTable[color];
    BitBoard not_promotable_dest =
        (not_promotable &
         (GoldAttacksTable[kBlack][enemy] & GoldAttacksTable[kWhite][enemy]));
    while (not_promotable_dest.test()) {
      const Square to = not_promotable_dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kRook, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, RookAttacksTable[to][0], ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kRook, capture);
      if (!result) return move_init(from, to, kRook, capture, false);
    }
  }

  return kMoveNone;
}

Move search_horse_mate(Position &pos, const BitBoard &movable,
                       const BitBoard &occupied, const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kHorse, color);
  bool result = true;

  while (piece.test()) {
    const Square from = piece.pop_bit();
    BitBoard dest = movable & horse_attack(occupied, from);
    while (dest.test()) {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kHorse, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(
                      pos, to, BishopAttacksTable[to][0] | KingAttacksTable[to],
                      ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kHorse, capture);
      if (!result) return move_init(from, to, kHorse, capture, false);
    }
  }
  return kMoveNone;
}

Move search_dragon_mate(Position &pos, const BitBoard &movable,
                        const BitBoard &occupied, const CheckInfo &ci) {
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kDragon, color);
  bool result = true;

  while (piece.test()) {
    const Square from = piece.pop_bit();
    BitBoard dest = movable & dragon_attack(occupied, from);
    while (dest.test()) {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned)) continue;

      pos.move_temporary(from, to, kDragon, capture);
      if (pos.is_attacked(to, ~color, pos.occupied())) {
        result = (can_king_escape(pos, to, dragon_attack(pos.occupied(), to),
                                  ~color) ||
                  can_piece_capture(pos, to, ~color, pos.occupied()));
      }
      pos.move_temporary(from, to, kDragon, capture);
      if (!result) return move_init(from, to, kDragon, capture, false);
    }
  }
  return kMoveNone;
}
Move search_mate1ply(Position &pos) {
  Color color = pos.side_to_move();
  BitBoard target;
  Move move;

  const BitBoard occupied = pos.occupied();
  if (pos.hand(color) != 0) {
    target = ~pos.occupied();
    move = search_drop_mate(pos, target);
    if (move != kMoveNone) return move;
  }

  CheckInfo ci(pos);
  target = ~pos.pieces(kOccupied, color);
  BitBoard movable = target & KingAttacksTable[pos.square_king(~color)];

  move = search_dragon_mate(pos, movable, occupied, ci);
  if (move != kMoveNone) return move;

  move = search_horse_mate(pos, movable, occupied, ci);
  if (move != kMoveNone) return move;

  move = search_rook_mate(pos, movable, occupied, ci);
  if (move != kMoveNone) return move;

  move = search_bishop_mate(pos, movable, occupied, ci);
  if (move != kMoveNone) return move;

  move = search_total_gold_mate(pos, movable, ci);
  if (move != kMoveNone) return move;

  move = search_silver_mate(pos, movable, ci);
  if (move != kMoveNone) return move;

  // 桂だけ隣接王手にならない
  move = search_knight_mate(pos, target, ci);
  if (move != kMoveNone) return move;

  move = search_lance_mate(pos, movable, ci);
  if (move != kMoveNone) return move;

  move = search_pawn_mate(pos, movable, ci);
  if (move != kMoveNone) return move;

  return kMoveNone;
}
