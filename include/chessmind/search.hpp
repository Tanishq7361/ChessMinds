#pragma once

#include "chessmind/board.hpp"
#include "chessmind/tt.hpp"
#include <chrono>
#include <atomic>

namespace chessmind {

struct SearchLimits {
    int wtime = -1;
    int btime = -1;
    int winc = 0;
    int binc = 0;
    int movetime = -1;
    int max_depth = 64;
    uint64_t max_nodes = 0;
    bool infinite = false;
};

class Searcher {
public:
    explicit Searcher(TranspositionTable& tt);

    void reset();
    Move start_search(Board& board, const SearchLimits& limits);
    void stop();

    [[nodiscard]] bool is_searching() const noexcept { return searching_; }

private:
    int negamax(Board& board, int depth, int ply, int alpha, int beta, bool is_null_allowed);
    int quiescence(Board& board, int alpha, int beta, int ply);

    void order_moves(const Board& board, std::vector<Move>& moves, Move tt_move, int ply);
    [[nodiscard]] int score_move(const Board& board, const Move& m, Move tt_move, int ply) const noexcept;

    void check_time_limit();

    TranspositionTable& tt_;
    std::atomic<bool> stop_flag_{false};
    bool searching_ = false;

    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point stop_time_;
    uint64_t nodes_ = 0;

    // Killer moves [ply][slot]
    std::array<std::array<Move, 2>, 64> killer_moves_{};
    // History heuristic [color][from][to]
    std::array<std::array<std::array<int, 64>, 64>, 2> history_{};
};

} // namespace chessmind

