#pragma once

#include "chessmind/types.hpp"
#include <array>

namespace chessmind::zobrist {

void init();

extern std::array<std::array<std::array<uint64_t, 64>, 6>, 2> PIECE_KEYS;
extern std::array<uint64_t, 16> CASTLE_KEYS;
extern std::array<uint64_t, 65> EP_KEYS;
extern uint64_t SIDE_KEY;

} // namespace chessmind::zobrist

