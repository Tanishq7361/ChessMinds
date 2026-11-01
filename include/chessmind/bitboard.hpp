#pragma once

#include "chessmind/types.hpp"
#include <bit>

namespace chessmind {

/// @brief Generates a 64-bit mask with only the bit for `sq` set.
[[nodiscard]] constexpr Bitboard square_bb(Square sq) noexcept {
    return 1ULL << sq;
}

/// @brief Counts the number of set bits (population count) in a bitboard.
[[nodiscard]] constexpr int pop_count(Bitboard bb) noexcept {
    return std::popcount(bb);
}

/// @brief Returns the index of the least significant set bit (LSB).
[[nodiscard]] constexpr Square lsb(Bitboard bb) noexcept {
    return static_cast<Square>(std::countr_zero(bb));
}

/// @brief Returns the index of the most significant set bit (MSB).
[[nodiscard]] constexpr Square msb(Bitboard bb) noexcept {
    return static_cast<Square>(63 - std::countl_zero(bb));
}

/// @brief Extracts and clears the least significant set bit from `bb`, returning its Square index.
inline Square pop_lsb(Bitboard& bb) noexcept {
    Square sq = lsb(bb);
    bb &= bb - 1;
    return sq;
}

/// @brief Sets the bit at `sq` to 1.
inline void set_bit(Bitboard& bb, Square sq) noexcept {
    bb |= (1ULL << sq);
}

/// @brief Clears the bit at `sq` to 0.
inline void clear_bit(Bitboard& bb, Square sq) noexcept {
    bb &= ~(1ULL << sq);
}

/// @brief Checks whether the bit at `sq` is set.
[[nodiscard]] constexpr bool test_bit(Bitboard bb, Square sq) noexcept {
    return (bb & (1ULL << sq)) != 0;
}

/// @brief Checks if a bitboard contains more than one set bit.
[[nodiscard]] constexpr bool more_than_one(Bitboard bb) noexcept {
    return (bb & (bb - 1)) != 0;
}

namespace bitboards {

/// @brief Initializes precomputed attack lookup tables for leaping pieces (pawns, knights, kings).
void init();

/// @brief Returns precomputed pawn attack bitboard from square `sq` for color `c`.
[[nodiscard]] Bitboard pawn_attacks(Color c, Square sq) noexcept;

/// @brief Returns precomputed knight attack bitboard from square `sq`.
[[nodiscard]] Bitboard knight_attacks(Square sq) noexcept;

/// @brief Returns precomputed king attack bitboard from square `sq`.
[[nodiscard]] Bitboard king_attacks(Square sq) noexcept;

/// @brief Computes bishop attack bitboard from square `sq` blocked by pieces in `occ`.
[[nodiscard]] Bitboard bishop_attacks(Square sq, Bitboard occ) noexcept;

/// @brief Computes rook attack bitboard from square `sq` blocked by pieces in `occ`.
[[nodiscard]] Bitboard rook_attacks(Square sq, Bitboard occ) noexcept;

/// @brief Computes queen attack bitboard (union of bishop and rook rays) from square `sq` blocked by `occ`.
[[nodiscard]] Bitboard queen_attacks(Square sq, Bitboard occ) noexcept;

// File bit masks
constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_B = FILE_A << 1;
constexpr Bitboard FILE_C = FILE_A << 2;
constexpr Bitboard FILE_D = FILE_A << 3;
constexpr Bitboard FILE_E = FILE_A << 4;
constexpr Bitboard FILE_F = FILE_A << 5;
constexpr Bitboard FILE_G = FILE_A << 6;
constexpr Bitboard FILE_H = FILE_A << 7;

// Rank bit masks
constexpr Bitboard RANK_1 = 0x00000000000000FFULL;
constexpr Bitboard RANK_2 = RANK_1 << 8;
constexpr Bitboard RANK_3 = RANK_1 << 16;
constexpr Bitboard RANK_4 = RANK_1 << 24;
constexpr Bitboard RANK_5 = RANK_1 << 32;
constexpr Bitboard RANK_6 = RANK_1 << 40;
constexpr Bitboard RANK_7 = RANK_1 << 48;
constexpr Bitboard RANK_8 = RANK_1 << 56;

// Directional Bitboard Shifts
[[nodiscard]] constexpr Bitboard shift_north(Bitboard bb) noexcept {
    return bb << 8;
}

[[nodiscard]] constexpr Bitboard shift_south(Bitboard bb) noexcept {
    return bb >> 8;
}

[[nodiscard]] constexpr Bitboard shift_east(Bitboard bb) noexcept {
    return (bb & ~FILE_H) << 1;
}

[[nodiscard]] constexpr Bitboard shift_west(Bitboard bb) noexcept {
    return (bb & ~FILE_A) >> 1;
}

[[nodiscard]] constexpr Bitboard shift_north_east(Bitboard bb) noexcept {
    return (bb & ~FILE_H) << 9;
}

[[nodiscard]] constexpr Bitboard shift_north_west(Bitboard bb) noexcept {
    return (bb & ~FILE_A) << 7;
}

[[nodiscard]] constexpr Bitboard shift_south_east(Bitboard bb) noexcept {
    return (bb & ~FILE_H) >> 7;
}

[[nodiscard]] constexpr Bitboard shift_south_west(Bitboard bb) noexcept {
    return (bb & ~FILE_A) >> 9;
}

} // namespace bitboards
} // namespace chessmind
