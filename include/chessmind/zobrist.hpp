#pragma once

#include "chessmind/types.hpp"
#include <array>

namespace chessmind::zobrist {

/// @brief Initializes pseudorandom 64-bit numbers for Zobrist board hashing.
void init();

/// @brief Random keys for each [color][piece_type][square].
extern std::array<std::array<std::array<uint64_t, 64>, 6>, 2> PIECE_KEYS;

/// @brief Random keys for each combination of castling rights [0..15].
extern std::array<uint64_t, 16> CASTLE_KEYS;

/// @brief Random keys for the en passant target square [0..64] (64 = no ep).
extern std::array<uint64_t, 65> EP_KEYS;

/// @brief Random key XORed when Black is the side to move.
extern uint64_t SIDE_KEY;

} // namespace chessmind::zobrist
