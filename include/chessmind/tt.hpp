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

/// @brief Individual entry in the Transposition Table.
struct TTEntry {
    uint64_t key = 0;           // 64-bit Zobrist key of the position
    Move best_move{};           // Best move identified during search
    int16_t score = 0;          // Search score (mate scores adjusted for ply distance)
    int8_t depth = -1;          // Depth to which this subtree was searched
    uint8_t flag = TT_NONE;     // Node type (EXACT, LOWERBOUND, UPPERBOUND)
};

/// @brief Hash table caching evaluated positions to avoid redundant sub-tree searches across move transpositions.
class TranspositionTable {
public:
    explicit TranspositionTable(size_t size_mb = 16);

    /// @brief Resizes table to allocate roughly `size_mb` megabytes.
    void resize(size_t size_mb);

    /// @brief Clears all entries in the table.
    void clear();

    /// @brief Probes table for matching entry.
    /// @return True if usable TT cutoff occurs at given alpha-beta bounds and depth.
    [[nodiscard]] bool probe(uint64_t key, int depth, int alpha, int beta, int ply, int& score, Move& best_move) const noexcept;

    /// @brief Stores or updates an entry in the transposition table using replace-if-deeper heuristic.
    void store(uint64_t key, int depth, int score, uint8_t flag, int ply, Move best_move) noexcept;

    /// @brief Retrieves the cached best move for move ordering without requiring depth bounds.
    [[nodiscard]] Move probe_move(uint64_t key) const noexcept;

private:
    std::vector<TTEntry> table_;
    size_t count_ = 0;
};

} // namespace chessmind
