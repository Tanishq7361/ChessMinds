#pragma once

#include "chessmind/board.hpp"

namespace chessmind {

/// @brief Hand-crafted tapered evaluation function combining material, Piece-Square Tables (PST),
/// game phase calculation, pawn structure, and piece cooperation bonuses.
class Evaluator {
public:
    static constexpr int MATE_SCORE = 30000; // Value assigned to theoretical checkmate
    static constexpr int DRAW_SCORE = 0;     // Value assigned to draw

    /// @brief Evaluates the position from the perspective of the side to move (negamax format).
    /// @return Positive score in centipawns if side to move is winning, negative if losing.
    [[nodiscard]] static int evaluate(const Board& board) noexcept;
};

} // namespace chessmind
