#pragma once

#include "chessmind/types.hpp"
#include <vector>
#include <cstdint>

namespace chessmind {

/// @brief Bound types stored in Transposition Table entries.
enum TTFlag : uint8_t {
    TT_NONE = 0,
    TT_EXACT = 1,        // Exact evaluation from a PV-node (alpha < score < beta)
    TT_LOWERBOUND = 2,   // Beta-cutoff fail-high node (score >= beta)
    TT_UPPERBOUND = 3    // All-node fail-low node (score <= alpha)
};

/// @brief Individual entry in the Transposition Table (16 bytes).
struct TTEntry {
    uint64_t key = 0;           // 64-bit Zobrist key of the position
    Move best_move{};           // Best move identified during search (4 bytes)
    int16_t score = 0;          // Search score (2 bytes)
    int8_t depth = -1;          // Depth to which this subtree was searched (1 byte)
    uint8_t flag = TT_NONE;     // Node type (EXACT, LOWERBOUND, UPPERBOUND) (1 byte)
    uint8_t age = 0;            // Age / generation of this entry for eviction (1 byte)
};

/// @brief Two-way associative cluster: 1 depth-preferred entry + 1 always-replace entry (32 bytes).
struct TTCluster {
    TTEntry entries[2];
};

/// @brief Two-way associative hash table with power-of-two mask caching evaluated positions.
class TranspositionTable {
public:
    explicit TranspositionTable(size_t size_mb = 16);

    /// @brief Resizes table to allocate roughly `size_mb` megabytes (rounded down to power of 2 clusters).
    void resize(size_t size_mb);

    /// @brief Clears all entries in the table.
    void clear();

    /// @brief Increments generation counter for table aging / entry eviction.
    void advance_generation() noexcept { ++generation_; }

    /// @brief Current TT generation.
    [[nodiscard]] uint8_t generation() const noexcept { return generation_; }

    /// @brief Probes table for matching entry.
    /// @return True if usable TT cutoff occurs at given alpha-beta bounds and depth.
    [[nodiscard]] bool probe(uint64_t key, int depth, int alpha, int beta, int ply, int& score, Move& best_move) const noexcept;

    /// @brief Stores or updates an entry in the transposition table using two-way associative replacement.
    void store(uint64_t key, int depth, int score, uint8_t flag, int ply, Move best_move) noexcept;

    /// @brief Retrieves the cached best move for move ordering without requiring depth bounds.
    [[nodiscard]] Move probe_move(uint64_t key) const noexcept;

private:
    std::vector<TTCluster> table_;
    size_t clusters_count_ = 0;
    size_t mask_ = 0;
    uint8_t generation_ = 0;
};

} // namespace chessmind
