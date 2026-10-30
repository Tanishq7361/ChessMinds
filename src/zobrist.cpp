#include "chessmind/zobrist.hpp"

namespace chessmind::zobrist {

std::array<std::array<std::array<uint64_t, 64>, 6>, 2> PIECE_KEYS{};
std::array<uint64_t, 16> CASTLE_KEYS{};
std::array<uint64_t, 65> EP_KEYS{};
uint64_t SIDE_KEY = 0;

namespace {

// Deterministic PRNG seed for reproducible hash keys across runs and platforms
uint64_t prng_state = 1070372ULL;

/// @brief Generates high-entropy 64-bit random numbers using 64-bit xorshift*.
uint64_t random_u64() noexcept {
    prng_state ^= prng_state >> 12;
    prng_state ^= prng_state << 25;
    prng_state ^= prng_state >> 27;
    return prng_state * 0x2545F4914F6CDD1DULL;
}

bool initialized = false;

} // namespace

/// @brief Fills the Zobrist key arrays with deterministic pseudo-random 64-bit bitmasks.
void init() {
    if (initialized) return;
    prng_state = 1070372ULL;

    for (int c = 0; c < 2; ++c) {
        for (int pt = 0; pt < 6; ++pt) {
            for (int sq = 0; sq < 64; ++sq) {
                PIECE_KEYS[c][pt][sq] = random_u64();
            }
        }
    }

    for (int i = 0; i < 16; ++i) {
        CASTLE_KEYS[i] = random_u64();
    }

    for (int sq = 0; sq < 65; ++sq) {
        EP_KEYS[sq] = (sq == 64) ? 0ULL : random_u64();
    }

    SIDE_KEY = random_u64();
    initialized = true;
}

} // namespace chessmind::zobrist
