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

#include <cassert>
#include "move_generator.h"
#include "position.h"

namespace
{
inline bool
can_promote(Color color, uint32_t to)
{
  if (color == kBlack)
    return (to < k9D) ? true : false;
  else
    return (to > k1F) ? true : false;
}

inline bool
can_promote(Color color, uint32_t from, uint32_t to)
{
  if (color == kBlack)
    return (to < k9D || from < k9D) ? true : false;
  else
    return (to > k1F || from > k1F) ? true : false;
}

template<bool legal>
ExtMove *
generate_pawn(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kPawn, color);
  BitBoard dest = pawn_attack(color, piece);

  dest = dest & movable;
  while (dest.test())
  {
    Square to = dest.pop_bit();
    Square from = static_cast<Square>((color == kBlack) ? to + 9 : to - 9);
    bool promote = can_promote(color, to);
    PieceType capture = pos.piece_type(to);

    if (legal)
    {
      if ((color == kBlack && to > k1A) || (color == kWhite && to < k9I))
      {
        move->move = move_init(from, to, kPawn, capture, false);
        ++move;
      }
      if (promote)
      {
        move->move = move_init(from, to, kPawn, capture, true);
        ++move;
      }
    }
    else
    {
      // 歩は成る手しか生成しない
      move->move = move_init(from, to, kPawn, capture, promote);
      ++move;
    }
  }
  return move;
}

ExtMove *
generate_pawn_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  const BitBoard target = pawn_attack(color, pos.pieces(kPawn, color)) & movable;
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & pos.pieces(kPawn, color)).test()
    );

  BitBoard promotable = target & PromotableMaskTable[color];
  while (promotable.test())
  {
    const Square to = promotable.pop_bit();
    const Square from = static_cast<Square>((color == kBlack) ? to + 9 : to - 9);

    if ((ci.check_squares[kPromotedPawn] & MaskTable[to]).test())
    {
      move->move = move_init(from, to, kPawn, pos.piece_type(to), true);
      ++move;
    }
    else if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
      &&
      !aligned(from, to, pos.square_king(~color))
    )
    {
      move->move = move_init(from, to, kPawn, pos.piece_type(to), true);
      ++move;
    }
  }

  BitBoard not_promotable = target;
  not_promotable.not_and(PromotableMaskTable[color]);
  while (not_promotable.test())
  {
    const Square to = not_promotable.pop_bit();
    const Square from = static_cast<Square>((color == kBlack) ? to + 9 : to - 9);

    if ((ci.check_squares[kPawn] & MaskTable[to]).test())
    {
      move->move = move_init(from, to, kPawn, pos.piece_type(to), false);
      ++move;
    }
    else if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
      &&
      !aligned(from, to, pos.square_king(~color))
    )
    {
      move->move = move_init(from, to, kPawn, pos.piece_type(to), false);
      ++move;
    }
  }

  return move;
}

template<bool legal>
ExtMove *
generate_lance(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kLance, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & lance_attack(pos.occupied(), color, from);

    while (dest.test())
    {
      Square to = dest.pop_bit();
      bool promote = can_promote(color, to);
      PieceType capture = pos.piece_type(to);

      if (legal)
      {
        if ((color == kBlack && to > k1A) || (color == kWhite && to < k9I))
        {
          move->move = move_init(from, to, kLance, capture, false);
          ++move;
        }
      }
      else
      {
        // 2段目は必ずなる
        if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H))
        {
          move->move = move_init(from, to, kLance, capture, false);
          ++move;
        }
      }

      if (promote)
      {
        move->move = move_init(from, to, kLance, capture, true);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_lance_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kLance, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    const Square   from = piece.pop_bit();
    BitBoard dest = movable & lance_attack(pos.occupied(), color, from);
    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
      &&
      // lanceは前にしか進めないので、File方向の場合のみ空き王手とならない
      DirectionTable[from][pos.square_king(~color)] != kDirFile
    )
    {
      // この場合は、すべて王手となる
      while (dest.test())
      {
        const Square    to      = dest.pop_bit();
        const bool      promote = can_promote(color, to);
        const PieceType capture = pos.piece_type(to);

        if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H))
        {
          move->move = move_init(from, to, kLance, capture, false);
          ++move;
        }

        if (promote)
        {
          move->move = move_init(from, to, kLance, capture, true);
          ++move;
        }
      }
    }
    else
    {
      BitBoard not_promote_attack = dest & ci.check_squares[kLance];
      not_promote_attack.not_and(MustPromoteMaskTable[color]);
      while (not_promote_attack.test())
      {
        const Square    to      = not_promote_attack.pop_bit();
        const PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kLance, capture, false);
        ++move;
      }

      BitBoard promote_attack = dest & ci.check_squares[kPromotedLance] & PromotableMaskTable[color];
      while (promote_attack.test())
      {
        const Square    to      = promote_attack.pop_bit();
        const PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kLance, capture, true);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_knight(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kKnight, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & KnightAttacksTable[color][from];
    while (dest.test())
    {
      Square to = dest.pop_bit();
      bool promote = can_promote(color, to);
      PieceType capture = pos.piece_type(to);

      // 2段目は必ず成る
      if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H))
      {
        move->move = move_init(from, to, kKnight, capture, false);
        ++move;
      }
      if (promote)
      {
        move->move = move_init(from, to, kKnight, capture, true);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_knight_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kKnight, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & KnightAttacksTable[color][from];
    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
      // knightは上記の条件を満たせばすべて間接王手になる
    )
    {
      // この場合は、すべて王手となる
      while (dest.test())
      {
        Square to = dest.pop_bit();
        bool promote = can_promote(color, to);
        PieceType capture = pos.piece_type(to);

        // 2段目は必ず成る
        if ((color == kBlack && to > k1B) || (color == kWhite && to < k9H))
        {
          move->move = move_init(from, to, kKnight, capture, false);
          ++move;
        }
        if (promote)
        {
          move->move = move_init(from, to, kKnight, capture, true);
          ++move;
        }
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kKnight];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kKnight, capture, false);
        ++move;
      }

      attack = dest & ci.check_squares[kPromotedKnight] & PromotableMaskTable[color];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kKnight, capture, true);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_silver(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kSilver, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & SilverAttacksTable[color][from];
    while (dest.test())
    {
      Square to = dest.pop_bit();
      bool promote = can_promote(color, from, to);
      PieceType capture = pos.piece_type(to);

      move->move = move_init(from, to, kSilver, capture, false);
      ++move;
      if (promote)
      {
        move->move = move_init(from, to, kSilver, capture, true);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_silver_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kSilver, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & SilverAttacksTable[color][from];

    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
    )
    {
      while (dest.test())
      {
        Square to = dest.pop_bit();
        bool promote = can_promote(color, from, to);
        PieceType capture = pos.piece_type(to);

        if (DirectionTable[pos.square_king(~color)][from] != DirectionTable[pos.square_king(~color)][to])
        {
          move->move = move_init(from, to, kSilver, capture, false);
          ++move;
          if (promote)
          {
            move->move = move_init(from, to, kSilver, capture, true);
            ++move;
          }
        }
        else
        {
          if ((ci.check_squares[kSilver] & MaskTable[to]).test())
          {
            move->move = move_init(from, to, kSilver, capture, false);
            ++move;
          }

          if (promote && (ci.check_squares[kPromotedSilver] & MaskTable[to]).test())
          {
            move->move = move_init(from, to, kSilver, capture, true);
            ++move;
          }
        }
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kSilver];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kSilver, capture, false);
        ++move;
      }

      attack = dest & ci.check_squares[kPromotedSilver];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        if (can_promote(color, from, to))
        {
          move->move = move_init(from, to, kSilver, capture, true);
          ++move;
        }
      }
    }
  }
  return move;
}

ExtMove *
generate_total_gold(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.total_gold(color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & GoldAttacksTable[color][from];
    while (dest.test())
    {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      PieceType place = pos.piece_type(from);

      move->move = move_init(from, to, place, capture, false);
      ++move;
    }
  }
  return move;
}

ExtMove *
generate_total_gold_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.total_gold(color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & GoldAttacksTable[color][from];
    PieceType place = pos.piece_type(from);

    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
    )
    {
      while (dest.test())
      {
        Square to = dest.pop_bit();
        PieceType capture = pos.piece_type(to);

        if
        (
          DirectionTable[pos.square_king(~color)][from] != DirectionTable[pos.square_king(~color)][to]
          ||
          (ci.check_squares[kGold] & MaskTable[to]).test()
        )
        {
          move->move = move_init(from, to, place, capture, false);
          ++move;
        }
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kGold];

      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, place, capture, false);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_king(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  Square from = pos.square_king(color);
  BitBoard dest = movable & KingAttacksTable[from];
  BitBoard occupied = pos.occupied();
  occupied.xor_bit(pos.square_king(color));

  while (dest.test())
  {
    Square to = dest.pop_bit();
    PieceType capture = pos.piece_type(to);
    move->move = move_init(from, to, kKing, capture, false);
    ++move;
  }

  return move;
}

ExtMove *
generate_king_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  Square from = pos.square_king(color);

  if
  (
    ci.discover_check_candidates.test()
    &&
    (ci.discover_check_candidates & MaskTable[from]).test()
  )
  {
    BitBoard dest = movable & KingAttacksTable[from];
    BitBoard occupied = pos.occupied();
    occupied.xor_bit(pos.square_king(color));

    while (dest.test())
    {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      if (DirectionTable[pos.square_king(~color)][from] != DirectionTable[pos.square_king(~color)][to])
      {
        move->move = move_init(from, to, kKing, capture, false);
        ++move;
      }
    }
  }

  return move;
}

template <bool legal>
ExtMove *
generate_bishop(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kBishop, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & bishop_attack(pos.occupied(), from);
    while (dest.test())
    {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      bool promote = can_promote(color, from, to);
      if (legal)
      {
        move->move = move_init(from, to, kBishop, capture, false);
        ++move;
        if (promote)
        {
          move->move = move_init(from, to, kBishop, capture, true);
          ++move;
        }
      }
      else
      {
        move->move = move_init(from, to, kBishop, capture, promote);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_bishop_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kBishop, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & bishop_attack(pos.occupied(), from);
    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
    )
    {
      // この場合全て王手
      // 角の後ろに飛車か香車がいるときにしか空き王手にならないので、
      // どの方向に動いても王手になるはず
      while (dest.test())
      {
        Square to = dest.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kBishop, capture, can_promote(color, from, to));
        ++move;
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kBishop];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kBishop, capture, can_promote(color, from, to));
        ++move;
      }

      attack = dest & ci.check_squares[kHorse];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        if
        (
          can_promote(color, from, to)
          &&
          // 角でも王手になる手は生成済み
          !(DirectionTable[pos.square_king(~color)][to] & kDirFlagDiag)
        )
        {
          move->move = move_init(from, to, kBishop, capture, true);
          ++move;
        }
      }
    }
  }
  return move;
}


template <bool legal>
ExtMove *
generate_rook(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kRook, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & rook_attack(pos.occupied(), from);
    while (dest.test())
    {
      Square to = dest.pop_bit();
      PieceType capture = pos.piece_type(to);
      bool promote = can_promote(color, from, to);
      if (legal)
      {
        move->move = move_init(from, to, kRook, capture, false);
        ++move;
        if (promote)
        {
          move->move = move_init(from, to, kRook, capture, true);
          ++move;
        }
      }
      else
      {
        move->move = move_init(from, to, kRook, capture, promote);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_rook_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kRook, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & rook_attack(pos.occupied(), from);
    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
    )
    {
      // この場合全て王手
      // 飛車のときは角が後ろにいるときしか空き王手にならないので、
      // どの方向に動いても王手になるはず
      while (dest.test())
      {
        Square to = dest.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kRook, capture, can_promote(color, from, to));
        ++move;
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kRook];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kRook, capture, can_promote(color, from, to));
        ++move;
      }

      attack = dest & ci.check_squares[kDragon];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        if
        (
          can_promote(color, from, to)
          &&
          // 成らずに王手になる手は生成済み
          !(DirectionTable[pos.square_king(~color)][to] & kDirFlagCross)
        )
        {
          move->move = move_init(from, to, kRook, capture, true);
          ++move;
        }
      }
    }
  }
  return move;
}

ExtMove *
generate_horse(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kHorse, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & horse_attack(pos.occupied(), from);
    while (dest.test())
    {
      Square to = dest.pop_bit();
      move->move = move_init(from, to, kHorse, pos.piece_type(to), false);
      ++move;
    }
  }
  return move;
}

ExtMove *
generate_horse_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kHorse, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & horse_attack(pos.occupied(), from);
    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
    )
    {
      while (dest.test())
      {
        Square to = dest.pop_bit();
        PieceType capture = pos.piece_type(to);

        if
        (
          DirectionTable[pos.square_king(~color)][from] != DirectionTable[pos.square_king(~color)][to]
          ||
          (ci.check_squares[kHorse] & MaskTable[to]).test()
        )
        {
          move->move = move_init(from, to, kHorse, capture, false);
          ++move;
        }
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kHorse];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kHorse, capture, false);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_dragon(const Position &pos, const BitBoard &movable, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kDragon, color);

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & dragon_attack(pos.occupied(), from);
    while (dest.test())
    {
      Square to = dest.pop_bit();
      move->move = move_init(from, to, kDragon, pos.piece_type(to), false);
      ++move;
    }
  }
  return move;
}

ExtMove *
generate_dragon_check(const Position &pos, const BitBoard &movable, const CheckInfo &ci, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kDragon, color);
  const bool discover_check =
    (
      ci.discover_check_candidates.test()
      &&
      (ci.discover_check_candidates & piece).test()
    );

  while (piece.test())
  {
    Square from = piece.pop_bit();
    BitBoard dest = movable & dragon_attack(pos.occupied(), from);
    if
    (
      discover_check
      &&
      (ci.discover_check_candidates & MaskTable[from]).test()
    )
    {
      while (dest.test())
      {
        Square to = dest.pop_bit();
        PieceType capture = pos.piece_type(to);

        if
        (
          DirectionTable[pos.square_king(~color)][from] != DirectionTable[pos.square_king(~color)][to]
          ||
          (ci.check_squares[kDragon] & MaskTable[to]).test()
        )
        {
          move->move = move_init(from, to, kDragon, capture, false);
          ++move;
        }
      }
    }
    else
    {
      BitBoard attack = dest & ci.check_squares[kDragon];
      while (attack.test())
      {
        Square to = attack.pop_bit();
        PieceType capture = pos.piece_type(to);
        move->move = move_init(from, to, kDragon, capture, false);
        ++move;
      }
    }
  }
  return move;
}

ExtMove *
generate_drop_pawn(const Position &pos, const BitBoard &bb, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard target = bb;
  const BitBoard pawn = pos.pieces(kPawn, color);
  const uint64_t mask_file1 = (1ULL << 54 | 1ULL << 45 | 1ULL << 36 | 1ULL << 27 |1ULL << 18 | 1ULL << 9 | 1U);
  const uint64_t pawn_exist = pawn.to_uint64();
  for (int i = 0; i < 9; i++)
  {
    if (pawn_exist & (mask_file1 << i))
      target.not_and(FileMaskTable[i]);
  }
  if (color == kBlack)
    target.not_and(RankMaskTable[kRank1]);
  else
    target.not_and(RankMaskTable[kRank9]);

  Square to;
  while (target.test())
  {
    to = target.pop_bit();
    if (!pos.gives_mate_by_drop_pawn(to))
    {
      move->move = move_init(to, kPawn);
      ++move;
    }
  }

  return move;
}

void
make_dropable_list(const BitBoard &bb, Square list[kBoardSquare], int *total_num)
{
  BitBoard target = bb;
  *total_num = 0;
  while (target.test())
  {
    list[*total_num] = target.pop_bit();
    ++(*total_num);
  }
}

ExtMove *
generate_drop_lance(const Position &pos, const Square list[kBoardSquare], int total_num, ExtMove *move)
{
  Color color = pos.side_to_move();
  for (int i = 0; i < total_num; i++)
  {
    if
    (
      (color == kBlack && list[i] > k1A)
      ||
      (color == kWhite && list[i] < k9I)
    )
    {
      move->move = move_init(list[i], kLance);
      ++move;
    }
  }
  return move;
}

ExtMove *
generate_drop_knight(const Position &pos, const Square list[kBoardSquare], int total_num, ExtMove *move)
{
  Color color = pos.side_to_move();
  for (int i = 0; i < total_num; i++)
  {
    if
    (
      (color == kBlack && list[i] > k1B)
      ||
      (color == kWhite && list[i] < k9H)
    )
    {
      move->move = move_init(list[i], kKnight);
      ++move;
    }
  }
  return move;
}

ExtMove *
generate_drop_silver(const Position &pos, const Square list[kBoardSquare], int total_num, ExtMove *move)
{
  for (int i = 0; i < total_num; i++)
  {
    move->move = move_init(list[i], kSilver);
    ++move;
  }
  return move;
}

ExtMove *
generate_drop_gold(const Position &pos, const Square list[kBoardSquare], int total_num, ExtMove *move)
{
  for (int i = 0; i < total_num; i++)
  {
    move->move = move_init(list[i], kGold);
    ++move;
  }
  return move;
}

ExtMove *
generate_drop_bishop(const Position &pos, const Square list[kBoardSquare], int total_num, ExtMove *move)
{
  for (int i = 0; i < total_num; i++)
  {
    move->move = move_init(list[i], kBishop);
    ++move;
  }
  return move;
}

ExtMove *
generate_drop_rook(const Position &pos, const Square list[kBoardSquare], int total_num, ExtMove *move)
{
  for (int i = 0; i < total_num; i++)
  {
    move->move = move_init(list[i], kRook);
    ++move;
  }
  return move;
}

ExtMove *
generate_drop(const Position &pos, const BitBoard &bb, ExtMove *move)
{
  Color color = pos.side_to_move();
  const Hand hand = pos.hand(color);
  if (has_hand(hand, kPawn))
    move = generate_drop_pawn(pos, bb, move);

  if (!has_hand_except_pawn(hand))
    return move;

  Square list[kBoardSquare];
  int total_num = 0;

  make_dropable_list(bb, list, &total_num);

  if (has_hand(hand, kLance))
    move = generate_drop_lance(pos, list, total_num, move);

  if (has_hand(hand, kKnight))
    move = generate_drop_knight(pos, list, total_num, move);

  if (has_hand(hand, kSilver))
    move = generate_drop_silver(pos, list, total_num, move);

  if (has_hand(hand, kGold))
    move = generate_drop_gold(pos, list, total_num, move);

  if (has_hand(hand, kBishop))
    move = generate_drop_bishop(pos, list, total_num, move);

  if (has_hand(hand, kRook))
    move = generate_drop_rook(pos, list, total_num, move);

  return move;
}

ExtMove *
generate_drop_check(const Position &pos, const BitBoard &bb, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard dest = bb & PawnAttacksTable[~color][pos.square_king(~color)];
  const Hand hand = pos.hand(color);
  if (has_hand(hand, kPawn))
    move = generate_drop_pawn(pos, dest, move);

  if (!has_hand_except_pawn(hand))
    return move;

  Square sq;
  if (has_hand(hand, kLance))
  {
    dest = bb & lance_attack(pos.occupied(), ~color, pos.square_king(~color));
    while (dest.test())
    {
      sq = dest.pop_bit();
      if
      (
        (color == kBlack && sq > k1A)
        ||
        (color == kWhite && sq < k9I)
      )
      {
        move->move = move_init(sq, kLance);
        ++move;
      }
    }
  }

  if (has_hand(hand, kKnight))
  {
    dest = bb & KnightAttacksTable[~color][pos.square_king(~color)];
    while (dest.test())
    {
      sq = dest.pop_bit();
      if
      (
        (color == kBlack && sq > k1B)
        ||
        (color == kWhite && sq < k9H)
      )
      {
        move->move = move_init(sq, kKnight);
        ++move;
      }
    }
  }

  if (has_hand(hand, kSilver))
  {
    dest = bb & SilverAttacksTable[~color][pos.square_king(~color)];
    while (dest.test())
    {
      sq = dest.pop_bit();
      move->move = move_init(sq, kSilver);
      ++move;
    }
  }

  if (has_hand(hand, kGold))
  {
    dest = bb & GoldAttacksTable[~color][pos.square_king(~color)];
    while (dest.test())
    {
      sq = dest.pop_bit();
      move->move = move_init(sq, kGold);
      ++move;
    }
  }

  if (has_hand(hand, kBishop))
  {
    dest = bb & bishop_attack(pos.occupied(), pos.square_king(~color));
    while (dest.test())
    {
      sq = dest.pop_bit();
      move->move = move_init(sq, kBishop);
      ++move;
    }
  }

  if (has_hand(hand, kRook))
  {
    dest = bb & rook_attack(pos.occupied(), pos.square_king(~color));
    while (dest.test())
    {
      sq = dest.pop_bit();
      move->move = move_init(sq, kRook);
      ++move;
    }
  }

  return move;
}
} // namespace


template<GenType type>
ExtMove *
generate(const Position &pos, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard target;

  if (type == kCaptures)
  {
    target = pos.pieces(kOccupied, ~color);
  }
  else if (type == kQuiets)
  {
    target = pos.occupied();
    target = ~target;
  }
  else if (type == kNonEvasions)
  {
    target = pos.pieces(kOccupied, color);
    target = ~target;
  }
  move = generate_pawn<false>(pos, target, move);
  move = generate_lance<false>(pos, target, move);
  move = generate_knight(pos, target, move);
  move = generate_silver(pos, target, move);
  move = generate_total_gold(pos, target, move);
  move = generate_bishop<false>(pos, target, move);
  move = generate_rook<false>(pos, target, move);
  move = generate_horse(pos, target, move);
  move = generate_dragon(pos, target, move);
  move = generate_king(pos, target, move);
  if (type == kQuiets)
  {
    if (pos.hand(color) != kHandZero)
      move = generate_drop(pos, target, move);
  }
  else if (type == kNonEvasions)
  {
    if (pos.hand(color) != kHandZero)
    {
      target = pos.occupied();
      target = ~target;
      move = generate_drop(pos, target, move);
    }
  }
  return move;
}

template ExtMove* generate<kCaptures>(const Position&, ExtMove*);
template ExtMove* generate<kQuiets>(const Position&, ExtMove*);
template ExtMove* generate<kNonEvasions>(const Position&, ExtMove*);

template <>
ExtMove *
generate<kEvasions>(const Position &pos, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard oc = ~pos.pieces(kOccupied, color) | pos.pieces(kOccupied, ~color);

  move = generate_king(pos, oc, move);

  BitBoard checker = pos.checkers_bitboard();

  if (checker.popcount() > 1)
  {
    // 両王手
    return move;
  }

  int check_sq = checker.first_one();
  BitBoard inter = BetweenTable[pos.square_king(color)][check_sq];
  BitBoard target = inter | checker;

  move = generate_pawn<false>(pos, target, move);
  move = generate_lance<false>(pos, target, move);
  move = generate_knight(pos, target, move);
  move = generate_silver(pos, target, move);
  move = generate_total_gold(pos, target, move);
  move = generate_bishop<false>(pos, target, move);
  move = generate_rook<false>(pos, target, move);
  move = generate_horse(pos, target, move);
  move = generate_dragon(pos, target, move);
  if (pos.hand(color) != kHandZero && inter.test())
    move = generate_drop(pos, inter, move);

  return move;
}

ExtMove *
generate_legal_evasion(const Position &pos, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard oc = ~pos.pieces(kOccupied, color) | pos.pieces(kOccupied, ~color);
  move = generate_king(pos, oc, move);

  BitBoard checker = pos.checkers_bitboard();

  if (checker.popcount() > 1)
  {
    // 両王手
    return move;
  }

  int check_sq = checker.first_one();
  BitBoard inter = BetweenTable[pos.square_king(color)][check_sq];
  BitBoard target = inter | checker;

  move = generate_pawn<true>(pos, target, move);
  move = generate_lance<true>(pos, target, move);
  move = generate_knight(pos, target, move);
  move = generate_silver(pos, target, move);
  move = generate_total_gold(pos, target, move);
  move = generate_bishop<true>(pos, target, move);
  move = generate_rook<true>(pos, target, move);
  move = generate_horse(pos, target, move);
  move = generate_dragon(pos, target, move);
  if (pos.hand(color) != kHandZero)
    move = generate_drop(pos, inter, move);

  return move;
}

ExtMove *
generate_legal_nonevasion(const Position &pos, ExtMove *move)
{
  BitBoard target = ~pos.pieces(kOccupied, pos.side_to_move());

  move = generate_pawn<true>(pos, target, move);
  move = generate_lance<true>(pos, target, move);
  move = generate_knight(pos, target, move);
  move = generate_silver(pos, target, move);
  move = generate_total_gold(pos, target, move);
  move = generate_bishop<true>(pos, target, move);
  move = generate_rook<true>(pos, target, move);
  move = generate_horse(pos, target, move);
  move = generate_dragon(pos, target, move);
  move = generate_king(pos, target, move);

  move = generate_drop(pos, ~pos.occupied(), move);

  return move;
}

template<>
ExtMove *
generate<kLegal>(const Position &pos, ExtMove *mlist)
{
  ExtMove *end;
  ExtMove *cur = mlist;
  Color side_to_move = pos.side_to_move();

  end =
    pos.in_check()
    ?
    generate_legal_evasion(pos, mlist)
    :
    generate_legal_nonevasion(pos, mlist);

  BitBoard pinned = pos.pinned_pieces(side_to_move);
  while (cur != end)
  {
    if (!pos.legal(cur->move, pinned))
      cur->move = (--end)->move;
    else
      ++cur;
  }

  return end;
}

template <>
ExtMove *
generate<kChecks>(const Position &pos, ExtMove *move)
{
  Color color = pos.side_to_move();
  BitBoard target;
  CheckInfo ci(pos);

  target = ~pos.pieces(kOccupied, color);

  move = generate_pawn_check(pos, target, ci, move);
  move = generate_lance_check(pos, target, ci, move);
  move = generate_knight_check(pos, target, ci, move);
  move = generate_silver_check(pos, target, ci, move);
  move = generate_total_gold_check(pos, target, ci, move);
  move = generate_bishop_check(pos, target, ci, move);
  move = generate_rook_check(pos, target, ci, move);
  move = generate_horse_check(pos, target, ci, move);
  move = generate_dragon_check(pos, target, ci, move);
  move = generate_king_check(pos, target, ci, move);
  if (pos.hand(color) != kHandZero)
    move = generate_drop_check(pos, ~pos.occupied(), move);
  return move;
}

template <>
ExtMove *
generate<kQuietChecks>(const Position &pos, ExtMove *move)
{
  BitBoard target = ~pos.occupied();
  CheckInfo ci(pos);

  move = generate_pawn_check(pos, target, ci, move);
  move = generate_lance_check(pos, target, ci, move);
  move = generate_knight_check(pos, target, ci, move);
  move = generate_silver_check(pos, target, ci, move);
  move = generate_total_gold_check(pos, target, ci, move);
  move = generate_bishop_check(pos, target, ci, move);
  move = generate_rook_check(pos, target, ci, move);
  move = generate_horse_check(pos, target, ci, move);
  move = generate_dragon_check(pos, target, ci, move);
  move = generate_king_check(pos, target, ci, move);
  if (pos.hand(pos.side_to_move()) != kHandZero)
    move = generate_drop_check(pos, target, move);

  return move;
}


template<>
ExtMove *
generate<kLegalForSearch>(const Position &pos, ExtMove *mlist)
{
  ExtMove *end;
  ExtMove *cur = mlist;
  Color side_to_move = pos.side_to_move();

  end = pos.in_check() ?
    generate<kEvasions>(pos, mlist)
    :
    generate<kNonEvasions>(pos, mlist);

  BitBoard pinned = pos.pinned_pieces(side_to_move);
  while (cur != end)
  {
    if (!pos.legal(cur->move, pinned))
      cur->move = (--end)->move;
    else
      ++cur;
  }

  return end;
}

bool
can_king_escape(const Position &pos, Square sq, const BitBoard &check_attack, Color color)
{
  BitBoard king_movable = ~pos.pieces(kOccupied, color) & KingAttacksTable[pos.square_king(color)];
  king_movable.not_and(check_attack);
  king_movable = king_movable ^ MaskTable[sq];
  const BitBoard occupied = pos.occupied();

  while (king_movable.test())
  {
    const Square to = king_movable.pop_bit();
    if (!pos.is_attacked(to, color, occupied))
      return true;
  }

  return false;
}

bool
can_king_escape(const Position &pos, Color color)
{
  BitBoard king_movable = ~pos.pieces(kOccupied, color) & KingAttacksTable[pos.square_king(color)];
  const BitBoard occupied = pos.occupied();

  while (king_movable.test())
  {
    const Square to = king_movable.pop_bit();
    if (!pos.is_attacked(to, color, occupied))
      return true;
  }

  return false;
}

bool
can_piece_capture(const Position &pos, Square sq, const BitBoard &pinned, Color color)
{
  const BitBoard occupied = pos.occupied();
  BitBoard attack = pos.pieces(kPawn, color) & PawnAttacksTable[~color][sq];
  attack.and_or(pos.pieces(kKnight, color), KnightAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kSilver, color), SilverAttacksTable[~color][sq]);
  attack.and_or(pos.total_gold(color), GoldAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kHorse, color) | pos.pieces(kDragon, color), KingAttacksTable[sq]);
  attack.and_or(pos.bishop_horse(color), bishop_attack(occupied, sq));
  attack.and_or(pos.rook_dragon(color), rook_attack(occupied, sq));
  attack.and_or(pos.pieces(kLance, color), lance_attack(occupied, ~color, sq));

  while (attack.test())
  {
    const Square from = attack.pop_bit();
    if (!pos.is_king_discover(from, sq, color, pinned))
      return true;
  }

  return false;
}

bool
can_piece_capture(const Position &pos, Square sq, Color color)
{
  const BitBoard occupied = pos.occupied();
  BitBoard attack = pos.pieces(kPawn, color) & PawnAttacksTable[~color][sq];
  attack.and_or(pos.pieces(kKnight, color), KnightAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kSilver, color), SilverAttacksTable[~color][sq]);
  attack.and_or(pos.total_gold(color), GoldAttacksTable[~color][sq]);
  attack.and_or(pos.pieces(kHorse, color) | pos.pieces(kDragon, color), KingAttacksTable[sq]);
  attack.and_or(pos.bishop_horse(color), bishop_attack(occupied, sq));
  attack.and_or(pos.rook_dragon(color), rook_attack(occupied, sq));
  attack.and_or(pos.pieces(kLance, color), lance_attack(occupied, ~color, sq));

  const BitBoard pinned = pos.pinned_pieces(color);
  while (attack.test())
  {
    const Square from = attack.pop_bit();
    if (!pos.is_king_discover(from, sq, color, pinned))
      return true;
  }

  return false;
}

// この辺はAperyを参考にしている
Move
search_drop_mate(Position &pos, const BitBoard &bb)
{
  Color color = pos.side_to_move();
  BitBoard dest;
  Square sq;
  bool result;
  Move move;
  const Square enemy = pos.square_king(~color);
  BitBoard occupied = pos.occupied();
  const Hand hand = pos.hand(color);
  const BitBoard pinned = pos.pinned_pieces(~color);

  if (has_hand(hand, kRook))
  {
    // 隣接王手だけ考える
    // 金の利きでandをとれば、十字の利きだけ取り出せる
    dest = bb & (GoldAttacksTable[kBlack][enemy] & GoldAttacksTable[kWhite][enemy]);
    while (dest.test())
    {
      sq = dest.pop_bit();
      // sqの場所に自駒の利きがなければ敵の王にとられる
      if (pos.is_attacked(sq, ~color, occupied))
      {
        move = move_init(sq, kRook);
        pos.do_temporary_move(move);
        result =
          (
            can_king_escape(pos, sq, RookAttacksTable[sq][0], ~color)
            ||
            can_piece_capture(pos, sq, pinned, ~color)
          );
        pos.undo_temporary_move(move);
        if (!result)
          return move;
      }
    }
  }
  else if (has_hand(hand, kLance))
  {
    // else ifなのは飛車で詰まない場合は香車でも詰まないから
    dest = bb & PawnAttacksTable[~color][enemy];
    if (dest.test())
    {
      sq = (color == kBlack) ? Square(enemy + 9) : Square(enemy - 9);
      if
      (
        (
          (color == kBlack && sq > k1A)
          ||
          (color == kWhite && sq < k9I)
        )
        &&
        pos.is_attacked(sq, ~color, occupied)
      )
      {
        move = move_init(sq, kLance);
        pos.do_temporary_move(move);
        result =
          (
            can_king_escape(pos, sq, LanceAttacksTable[color][sq][0], ~color)
            ||
            can_piece_capture(pos, sq, pinned, ~color)
          );
        pos.undo_temporary_move(move);
        if (!result)
          return move;
      }
    }
  }

  if (has_hand(hand, kBishop))
  {
    dest = bb & (SilverAttacksTable[kBlack][enemy] & SilverAttacksTable[kWhite][enemy]);
    while (dest.test())
    {
      sq = dest.pop_bit();
      if (pos.is_attacked(sq, ~color, occupied))
      {
        move = move_init(sq, kBishop);
        pos.do_temporary_move(move);
        result =
          (
            can_king_escape(pos, sq, BishopAttacksTable[sq][0], ~color)
            ||
            can_piece_capture(pos, sq, pinned, ~color)
          );
        pos.undo_temporary_move(move);
        if (!result)
          return move;
      }
    }
  }

  if (has_hand(hand, kGold))
  {
    // 飛車打ちを調べたので後ろに動くのは調べなくてもよい
    // 前は斜めにも動けるので、調べないといけない
    if (has_hand(hand, kRook))
      dest = bb & (GoldAttacksTable[~color][enemy] ^ PawnAttacksTable[color][enemy]);
    else
      dest = bb & GoldAttacksTable[~color][enemy];
    while (dest.test())
    {
      sq = dest.pop_bit();
      if (pos.is_attacked(sq, ~color, occupied))
      {
        move = move_init(sq, kGold);
        pos.do_temporary_move(move);
        result =
          (
            can_king_escape(pos, sq, GoldAttacksTable[color][sq], ~color)
            ||
            can_piece_capture(pos, sq, pinned, ~color)
          );
        pos.undo_temporary_move(move);
        if (!result)
          return move;
      }
    }
  }

  if (has_hand(hand, kSilver))
  {
    if (has_hand(hand, kGold))
    {
      // 金打ちを先に調べているので、斜め後ろだけしらべればよい
      if (has_hand(hand, kBishop))
      {
        // 角打ちを調べているので、銀では詰まない
        goto silver_end;
      }
      dest = bb & (SilverAttacksTable[~color][enemy] & GoldAttacksTable[color][enemy]);
    }
    else
    {
      if (has_hand(hand, kBishop))
      {
        // 角打ちを調べているので前だけしらべればよい
        dest = bb & (SilverAttacksTable[~color][enemy] & GoldAttacksTable[~color][enemy]);
      }
      else
      {
        dest = bb & SilverAttacksTable[~color][enemy];
      }
    }
    while (dest.test())
    {
      sq = dest.pop_bit();
      if (pos.is_attacked(sq, ~color, occupied))
      {
        move = move_init(sq, kSilver);
        pos.do_temporary_move(move);
        result =
          (
            can_king_escape(pos, sq, SilverAttacksTable[color][sq], ~color)
            ||
            can_piece_capture(pos, sq, pinned, ~color)
          );
        pos.undo_temporary_move(move);
        if (!result)
          return move;
      }
    }
  }

silver_end:
  if (has_hand(hand, kKnight))
  {
    dest = bb & KnightAttacksTable[~color][enemy];
    while (dest.test())
    {
      sq = dest.pop_bit();
      // 桂馬はsqの場所に利きがなくても王が取れない
      if
      (
        (color == kBlack && sq > k1B)
        ||
        (color == kWhite && sq < k9H)
      )
      {
        move = move_init(sq, kKnight);
        pos.do_temporary_move(move);
        result = (can_king_escape(pos, ~color) || can_piece_capture(pos, sq, pinned, ~color));
        pos.undo_temporary_move(move);
        if (!result)
          return move;
      }
    }
  }
  // 打ち歩詰めになるので歩は調べなくてもよい
  return kMoveNone;
}

Move
search_pawn_mate(Position &pos, const BitBoard &movable, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kPawn, color);
  BitBoard dest = movable & pawn_attack(color, piece);
  bool result = true;
  Move move;

  BitBoard enemy_field = dest & PromotableMaskTable[color];
  while (enemy_field.test())
  {
    const Square to   = enemy_field.pop_bit();
    const Square from = static_cast<Square>((color == kBlack) ? to + 9 : to - 9);
    if
    (
      (GoldAttacksTable[color][to] & MaskTable[pos.square_king(~color)]).test()
      &&
      !pos.is_king_discover(from, to, color, ci.pinned)
    )
    {
      move = move_init(from, to, kPawn, pos.piece_type(to), true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        const BitBoard pinned = pos.pinned_pieces(~color);
        result =
          (
            can_king_escape(pos, to, GoldAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, pinned, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  dest.not_and(PromotableMaskTable[color]);
  while (dest.test())
  {
    const Square to   = dest.pop_bit();
    const Square from = static_cast<Square>((color == kBlack) ? to + 9 : to - 9);
    if
    (
      (PawnAttacksTable[color][to] & MaskTable[pos.square_king(~color)]).test()
      &&
      !pos.is_king_discover(from, to, color, ci.pinned)
    )
    {
      move = move_init(from, to, kPawn, pos.piece_type(to), false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        const BitBoard pinned = pos.pinned_pieces(~color);
        result =
          (
            can_king_escape(pos, to, PawnAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, pinned, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }
  return kMoveNone;
}

Move
search_lance_mate(Position &pos, const BitBoard &movable, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kLance, color);
  Move move;
  bool result = true;

  while (piece.test())
  {
    const Square   from = piece.pop_bit();
    BitBoard dest = movable & lance_attack(pos.occupied(), color, from);
    BitBoard attack = (dest & PromotableMaskTable[color]) & GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kLance, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, GoldAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    const BitBoard rank3mask = (color == kBlack) ? BitBoard(0x7FC0000ULL, 0) : BitBoard(0x7FC0000000000000ULL, 0);
    attack = (dest & rank3mask) & PawnAttacksTable[~color][pos.square_king(~color)];
    if (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kLance, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, LanceAttacksTable[color][to][0], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    dest.not_and(PromotableMaskTable[color]);
    attack = dest & PawnAttacksTable[~color][pos.square_king(~color)];
    if (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kLance, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, LanceAttacksTable[color][to][0], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }

      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }
  return kMoveNone;
}

Move
search_knight_mate(Position &pos, const BitBoard &movable, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kKnight, color);
  Move move;
  bool result = true;

  while (piece.test())
  {
    const Square   from = piece.pop_bit();
    const BitBoard dest = movable & KnightAttacksTable[color][from];
    BitBoard attack = dest & KnightAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kKnight, capture, false);
      pos.do_temporary_move(move);
      result = (can_king_escape(pos, ~color) || can_piece_capture(pos, to, ~color));
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    attack = (dest & PromotableMaskTable[color]) & GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kKnight, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, GoldAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }
  return kMoveNone;
}

Move
search_silver_mate(Position &pos, const BitBoard &movable, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kSilver, color);
  Move move;
  bool result = true;

  // fromが敵陣で成ることができる場合
  BitBoard enemy_field = piece & PromotableMaskTable[color];
  while (enemy_field.test())
  {
    const Square from = enemy_field.pop_bit();
    BitBoard dest = movable & SilverAttacksTable[color][from];
    BitBoard attack = dest & SilverAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to     = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kSilver, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, SilverAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    attack = dest & GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kSilver, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, GoldAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  piece.not_and(PromotableMaskTable[color]);
  while (piece.test())
  {
    // toが敵陣で成ることができる
    const Square from = piece.pop_bit();
    BitBoard     promotable = movable & PromotableMaskTable[color];
    BitBoard     dest = promotable & SilverAttacksTable[color][from];
    BitBoard     attack = dest & SilverAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to     = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kSilver, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, SilverAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    attack = dest & GoldAttacksTable[~color][pos.square_king(~color)];
    while (attack.test())
    {
      const Square    to      = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kSilver, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, GoldAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    // fromもtoも敵陣になく成ることができない
    BitBoard not_promotable = movable;
    not_promotable.not_and(PromotableMaskTable[color]);
    BitBoard not_promote_dest = not_promotable & SilverAttacksTable[color][from];
    BitBoard not_promote_attack = not_promote_dest & SilverAttacksTable[~color][pos.square_king(~color)];
    while (not_promote_attack.test())
    {
      const Square    to      = not_promote_attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kSilver, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, SilverAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  return kMoveNone;
}

Move
search_total_gold_mate(Position &pos, const BitBoard &movable, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.total_gold(color);
  Move move;
  bool result = true;

  while (piece.test())
  {
    const Square from = piece.pop_bit();
    BitBoard  dest  = movable & GoldAttacksTable[color][from];
    PieceType place = pos.piece_type(from);
    BitBoard attack = dest & GoldAttacksTable[~color][pos.square_king(~color)];

    while (attack.test())
    {
      const Square to = attack.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, place, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, GoldAttacksTable[color][to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }
  return kMoveNone;
}

Move
search_bishop_mate(Position &pos, const BitBoard &movable, const BitBoard &occupied, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kBishop, color);
  Move move;
  bool result = true;

  // fromが敵陣で成ることができる場合
  BitBoard enemy_field = piece & PromotableMaskTable[color];
  while (enemy_field.test())
  {
    const Square from = enemy_field.pop_bit();
    BitBoard     dest = movable & bishop_attack(occupied, from);
    while (dest.test())
    {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kBishop, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, BishopAttacksTable[to][0] | KingAttacksTable[to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  const Square enemy = pos.square_king(~color);
  piece.not_and(PromotableMaskTable[color]);
  while (piece.test())
  {
    // toが敵陣で成ることができる
    const Square from = piece.pop_bit();
    BitBoard     dest = movable & bishop_attack(occupied, from);
    BitBoard     promotable = dest & PromotableMaskTable[color];
    while (promotable.test())
    {
      const Square to = promotable.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kBishop, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, BishopAttacksTable[to][0] | KingAttacksTable[to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    // fromもtoも敵陣になく成ることができない
    BitBoard not_promotable = dest;
    not_promotable.not_and(PromotableMaskTable[color]);
    BitBoard not_promotable_dest = (not_promotable & (SilverAttacksTable[kBlack][enemy] & SilverAttacksTable[kWhite][enemy]));
    while (not_promotable_dest.test())
    {
      const Square to = not_promotable_dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kBishop, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, BishopAttacksTable[to][0], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  return kMoveNone;
}

Move
search_rook_mate(Position &pos, const BitBoard &movable, const BitBoard &occupied, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kRook, color);
  Move move;
  bool result = true;

  // fromが敵陣で成ることができる場合
  BitBoard enemy_field = piece & PromotableMaskTable[color];
  while (enemy_field.test())
  {
    const Square from = enemy_field.pop_bit();
    BitBoard     dest = movable & rook_attack(occupied, from);
    while (dest.test())
    {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kRook, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, dragon_attack(pos.occupied(), to), ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  const Square enemy = pos.square_king(~color);
  piece.not_and(PromotableMaskTable[color]);
  while (piece.test())
  {
    // toが敵陣で成ることができる
    const Square from = piece.pop_bit();
    BitBoard dest = movable & rook_attack(occupied, from);
    BitBoard promotable = dest & PromotableMaskTable[color];
    while (promotable.test())
    {
      const Square to = promotable.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kRook, capture, true);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, dragon_attack(pos.occupied(), to), ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }

    // fromもtoも敵陣になく成ることができない
    BitBoard not_promotable = dest;
    not_promotable.not_and(PromotableMaskTable[color]);
    BitBoard not_promotable_dest = (not_promotable & (GoldAttacksTable[kBlack][enemy] & GoldAttacksTable[kWhite][enemy]));
    while (not_promotable_dest.test())
    {
      const Square to = not_promotable_dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kRook, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, RookAttacksTable[to][0], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }

  return kMoveNone;
}

Move
search_horse_mate(Position &pos, const BitBoard &movable, const BitBoard &occupied, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kHorse, color);
  Move move;
  bool result = true;

  while (piece.test())
  {
    const Square from = piece.pop_bit();
    BitBoard     dest = movable & horse_attack(occupied, from);
    while (dest.test())
    {
      const Square to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kHorse, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, BishopAttacksTable[to][0] | KingAttacksTable[to], ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }
  return kMoveNone;
}

Move
search_dragon_mate(Position &pos, const BitBoard &movable, const BitBoard &occupied, const CheckInfo &ci)
{
  Color color = pos.side_to_move();
  BitBoard piece = pos.pieces(kDragon, color);
  Move move;
  bool result = true;

  while (piece.test())
  {
    const Square from = piece.pop_bit();
    BitBoard dest = movable & dragon_attack(occupied, from);
    while (dest.test())
    {
      const Square    to = dest.pop_bit();
      const PieceType capture = pos.piece_type(to);

      if (pos.is_king_discover(from, to, color, ci.pinned))
        continue;

      move = move_init(from, to, kDragon, capture, false);
      pos.do_temporary_move(move);
      if (pos.is_attacked(to, ~color, pos.occupied()))
      {
        result =
          (
            can_king_escape(pos, to, dragon_attack(pos.occupied(), to), ~color)
            ||
            can_piece_capture(pos, to, ~color)
          );
      }
      pos.undo_temporary_move(move);
      if (!result)
        return move;
    }
  }
  return kMoveNone;
}

Move
search_mate1ply(Position &pos)
{
  Color color = pos.side_to_move();
  BitBoard target;
  Move move;
  CheckInfo ci(pos);

  const BitBoard occupied = pos.occupied();
  if (pos.hand(color) != 0)
  {
    target = ~pos.occupied();
    move = search_drop_mate(pos, target);
    if (move != kMoveNone)
      return move;
  }

  target = ~pos.pieces(kOccupied, color);
  BitBoard movable = target & KingAttacksTable[pos.square_king(~color)];

  move = search_dragon_mate(pos, movable, occupied, ci);
  if (move != kMoveNone)
    return move;

  move = search_horse_mate(pos, movable, occupied, ci);
  if (move != kMoveNone)
    return move;

  move = search_rook_mate(pos, movable, occupied, ci);
  if (move != kMoveNone)
    return move;

  move = search_bishop_mate(pos, movable, occupied, ci);
  if (move != kMoveNone)
    return move;

  move = search_total_gold_mate(pos, movable, ci);
  if (move != kMoveNone)
    return move;

  move = search_silver_mate(pos, movable, ci);
  if (move != kMoveNone)
    return move;

  // 桂だけ隣接王手にならない
  move = search_knight_mate(pos, target, ci);
  if (move != kMoveNone)
    return move;

  move = search_lance_mate(pos, movable, ci);
  if (move != kMoveNone)
    return move;

  move = search_pawn_mate(pos, movable, ci);
  if (move != kMoveNone)
    return move;

  return kMoveNone;
}
