#pragma once

#include "chessmind/board.hpp"
#include "chessmind/tt.hpp"
#include <chrono>
#include <atomic>

namespace chessmind {

/// @brief Search constraints passed from the UCI `go` command.
struct SearchLimits {
    int wtime = -1;             // White time remaining (ms)
    int btime = -1;             // Black time remaining (ms)
    int winc = 0;               // White time increment per move (ms)
    int binc = 0;               // Black time increment per move (ms)
    int movetime = -1;          // Exact time allocated for this move (ms)
    int max_depth = 64;         // Maximum ply depth to explore
    uint64_t max_nodes = 0;     // Maximum nodes limit
    bool infinite = false;      // Search until explicitly stopped
};

/// @brief Search engine implementing Alpha-Beta pruning, Quiescence Search, Iterative Deepening, and Move Ordering.
class Searcher {
public:
    explicit Searcher(TranspositionTable& tt);

    /// @brief Resets killer tables, history tables, and search statistics.
    void reset();

    /// @brief Initiates iterative deepening search on `board` within given `limits`.
    /// @return Best legal move found.
    Move start_search(Board& board, const SearchLimits& limits);

    /// @brief Signals the running search to gracefully abort at the next node check.
    void stop();

    [[nodiscard]] bool is_searching() const noexcept { return searching_; }

private:
    /// @brief Main alpha-beta negamax search routine.
    int negamax(Board& board, int depth, int ply, int alpha, int beta, bool is_null_allowed);

    /// @brief Quiescence search evaluating captures only to stabilize positions at depth 0.
    int quiescence(Board& board, int alpha, int beta, int ply);

    /// @brief Sorts moves using TT move, MVV-LVA captures, killer moves, and history heuristics.
    void order_moves(const Board& board, std::vector<Move>& moves, Move tt_move, int ply);

    /// @brief Computes sorting priority score for a move.
    [[nodiscard]] int score_move(const Board& board, const Move& m, Move tt_move, int ply) const noexcept;

    /// @brief Checks if search has exceeded allocated thinking time.
    void check_time_limit();

    TranspositionTable& tt_;
    std::atomic<bool> stop_flag_{false};
    bool searching_ = false;

    std::chrono::steady_clock::time_point start_time_;
    std::chrono::steady_clock::time_point stop_time_;
    uint64_t nodes_ = 0;

    // Killer moves table: [ply][slot] (stores two quiet moves causing beta cutoffs per ply)
    std::array<std::array<Move, 2>, 64> killer_moves_{};

    // History heuristic table: [color][from_sq][to_sq] (accumulates successful quiet moves)
    std::array<std::array<std::array<int, 64>, 64>, 2> history_{};
};

} // namespace chessmind
