#pragma once

#include "chessmind/types.hpp"
#include <bit>

namespace chessmind {

[[nodiscard]] constexpr Bitboard square_bb(Square sq) noexcept {
    return 1ULL << sq;
}

[[nodiscard]] constexpr int pop_count(Bitboard bb) noexcept {
    return std::popcount(bb);
}

[[nodiscard]] constexpr Square lsb(Bitboard bb) noexcept {
    return static_cast<Square>(std::countr_zero(bb));
}

inline Square pop_lsb(Bitboard& bb) noexcept {
    Square sq = lsb(bb);
    bb &= bb - 1;
    return sq;
}

namespace bitboards {

void init();

[[nodiscard]] Bitboard pawn_attacks(Color c, Square sq) noexcept;
[[nodiscard]] Bitboard knight_attacks(Square sq) noexcept;
[[nodiscard]] Bitboard king_attacks(Square sq) noexcept;
[[nodiscard]] Bitboard bishop_attacks(Square sq, Bitboard occ) noexcept;
[[nodiscard]] Bitboard rook_attacks(Square sq, Bitboard occ) noexcept;
[[nodiscard]] Bitboard queen_attacks(Square sq, Bitboard occ) noexcept;

// File and Rank masks
constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_B = FILE_A << 1;
constexpr Bitboard FILE_C = FILE_A << 2;
constexpr Bitboard FILE_D = FILE_A << 3;
constexpr Bitboard FILE_E = FILE_A << 4;
constexpr Bitboard FILE_F = FILE_A << 5;
constexpr Bitboard FILE_G = FILE_A << 6;
constexpr Bitboard FILE_H = FILE_A << 7;

constexpr Bitboard RANK_1 = 0x00000000000000FFULL;
constexpr Bitboard RANK_2 = RANK_1 << 8;
constexpr Bitboard RANK_3 = RANK_1 << 16;
constexpr Bitboard RANK_4 = RANK_1 << 24;
constexpr Bitboard RANK_5 = RANK_1 << 32;
constexpr Bitboard RANK_6 = RANK_1 << 40;
constexpr Bitboard RANK_7 = RANK_1 << 48;
constexpr Bitboard RANK_8 = RANK_1 << 56;

} // namespace bitboards
} // namespace chessmind

