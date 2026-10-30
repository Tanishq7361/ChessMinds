#pragma once

#include "chessmind/board.hpp"
#include <vector>

namespace chessmind {

/// @brief Generates pseudo-legal and strictly legal chess moves.
class MoveGen {
public:
    /// @brief Generates all strictly legal moves for the current side to move.
    /// Filters out moves that leave the king in check.
    static void generate_legal_moves(Board& board, std::vector<Move>& legal_moves);

    /// @brief Generates strictly legal capture moves (and promotions) used in quiescence search.
    static void generate_legal_captures(Board& board, std::vector<Move>& legal_captures);

    /// @brief Generates pseudo-legal moves (moves matching piece movement rules, before check validation).
    /// @param captures_only If true, omits quiet non-capturing moves.
    static void generate_pseudo_legal_moves(const Board& board, std::vector<Move>& moves, bool captures_only = false);
};

} // namespace chessmind
