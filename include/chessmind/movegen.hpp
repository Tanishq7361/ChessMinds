#pragma once

#include "chessmind/board.hpp"
#include <vector>

namespace chessmind {

class MoveGen {
public:
    static void generate_legal_moves(Board& board, std::vector<Move>& legal_moves);
    static void generate_legal_captures(Board& board, std::vector<Move>& legal_captures);
    static void generate_pseudo_legal_moves(const Board& board, std::vector<Move>& moves, bool captures_only = false);
};

} // namespace chessmind

