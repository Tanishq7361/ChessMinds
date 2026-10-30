#pragma once

#include "chessmind/board.hpp"

namespace chessmind {

class Evaluator {
public:
    static constexpr int MATE_SCORE = 30000;
    static constexpr int DRAW_SCORE = 0;

    // Evaluates the board from the perspective of the side to move (negamax format)
    [[nodiscard]] static int evaluate(const Board& board) noexcept;
};

} // namespace chessmind

