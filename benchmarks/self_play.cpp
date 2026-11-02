#include "chessmind/board.hpp"
#include "chessmind/movegen.hpp"
#include "chessmind/search.hpp"
#include "chessmind/eval.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <cmath>
#include <chrono>
#include <string>

using namespace chessmind;

/// @brief Result of an individual game between two engines.
enum class GameResult {
    ENGINE_A_WIN,
    ENGINE_B_WIN,
    DRAW
};

/// @brief Standard starting FENs for tournament variety (diverse opening setups)
constexpr const char* TOURNAMENT_OPENINGS[] = {
    "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",                               // Standard Start
    "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",                           // 1. e4 e5
    "rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2",                           // 1. d4 d5
    "rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",                           // Sicilian
    "rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2",                           // French
    "rnbqkbnr/pppppp1p/8/6p1/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2"                            // Modern / Flank
};

/// @brief Plays one game between Engine A and Engine B.
GameResult play_single_game(const std::string& opening_fen, bool engine_a_is_white, int depth_a, int depth_b) {
    Board board;
    board.set_fen(opening_fen);

    TranspositionTable tt_a(16);
    TranspositionTable tt_b(16);
    Searcher searcher_a(tt_a);
    Searcher searcher_b(tt_b);

    Color color_a = engine_a_is_white ? WHITE : BLACK;
    int plies = 0;
    constexpr int MAX_PLIES = 150;

    while (plies < MAX_PLIES) {
        Color stm = board.side_to_move();
        std::vector<Move> legal;
        MoveGen::generate_legal_moves(board, legal);

        if (legal.empty()) {
            if (board.in_check(stm)) {
                // Side to move is checkmated
                return (stm == color_a) ? GameResult::ENGINE_B_WIN : GameResult::ENGINE_A_WIN;
            }
            return GameResult::DRAW; // Stalemate
        }

        if (board.is_fifty_move_rule() || board.is_draw_by_repetition(3) || board.is_insufficient_material()) {
            return GameResult::DRAW;
        }

        Move best_move = NULL_MOVE;
        if (stm == color_a) {
            SearchLimits limits;
            limits.max_depth = depth_a;
            best_move = searcher_a.start_search(board, limits);
        } else {
            SearchLimits limits;
            limits.max_depth = depth_b;
            best_move = searcher_b.start_search(board, limits);
        }

        if (best_move.is_null()) {
            best_move = legal[0];
        }

        board.make_move(best_move);
        ++plies;
    }

    return GameResult::DRAW;
}

int main(int argc, char* argv[]) {
    int depth_a = 5;
    int depth_b = 3;
    int num_openings = static_cast<int>(std::size(TOURNAMENT_OPENINGS));

    if (argc >= 3) {
        depth_a = std::max(1, std::atoi(argv[1]));
        depth_b = std::max(1, std::atoi(argv[2]));
    } else if (argc == 2) {
        depth_a = std::max(1, std::atoi(argv[1]));
        depth_b = depth_a;
    }

    std::cout << "=================================================================\n";
    std::cout << "  ChessMinds Controlled Engine Self-Play & Match Framework        \n";
    std::cout << "=================================================================\n";
    std::cout << " Engine A Depth:        " << depth_a << " plies\n";
    std::cout << " Engine B Depth:        " << depth_b << " plies\n";
    std::cout << " Paired Openings:       " << num_openings << " (Color-alternated rounds)\n";
    std::cout << " Total Games:           " << (num_openings * 2) << "\n";
    std::cout << "-----------------------------------------------------------------\n";

    int wins_a = 0;
    int wins_b = 0;
    int draws = 0;

    auto t_start = std::chrono::steady_clock::now();

    for (int i = 0; i < num_openings; ++i) {
        std::cout << "[Pair " << (i + 1) << "/" << num_openings << "] Running opening: " 
                  << TOURNAMENT_OPENINGS[i] << "\n";

        // Round 1: Engine A is White
        GameResult res1 = play_single_game(TOURNAMENT_OPENINGS[i], true, depth_a, depth_b);
        if (res1 == GameResult::ENGINE_A_WIN) {
            ++wins_a;
            std::cout << "  -> Round 1 (Engine A White): Engine A won [1-0]\n";
        } else if (res1 == GameResult::ENGINE_B_WIN) {
            ++wins_b;
            std::cout << "  -> Round 1 (Engine A White): Engine B won [0-1]\n";
        } else {
            ++draws;
            std::cout << "  -> Round 1 (Engine A White): Draw [1/2-1/2]\n";
        }

        // Round 2: Engine A is Black (Color Alternated)
        GameResult res2 = play_single_game(TOURNAMENT_OPENINGS[i], false, depth_a, depth_b);
        if (res2 == GameResult::ENGINE_A_WIN) {
            ++wins_a;
            std::cout << "  -> Round 2 (Engine A Black): Engine A won [0-1]\n";
        } else if (res2 == GameResult::ENGINE_B_WIN) {
            ++wins_b;
            std::cout << "  -> Round 2 (Engine A Black): Engine B won [1-0]\n";
        } else {
            ++draws;
            std::cout << "  -> Round 2 (Engine A Black): Draw [1/2-1/2]\n";
        }
    }

    auto t_end = std::chrono::steady_clock::now();
    double total_sec = std::chrono::duration<double>(t_end - t_start).count();

    int total_games = num_openings * 2;
    double score_a = wins_a + 0.5 * draws;
    double win_rate = score_a / total_games;

    // Standard Elo difference calculation
    double elo_diff = 0.0;
    if (win_rate > 0.001 && win_rate < 0.999) {
        elo_diff = -400.0 * std::log10(1.0 / win_rate - 1.0);
    } else if (win_rate >= 0.999) {
        elo_diff = 800.0;
    } else {
        elo_diff = -800.0;
    }

    // 95% Confidence Interval via standard error
    double se = std::sqrt(win_rate * (1.0 - win_rate) / total_games);
    double elo_error = 1.96 * se * (400.0 / (std::log(10.0) * std::max(0.01, win_rate * (1.0 - win_rate))));

    std::cout << "\n=================================================================\n";
    std::cout << "  Controlled Match Summary & Statistical Evaluation              \n";
    std::cout << "=================================================================\n";
    std::cout << " Total Games Played: " << total_games << "\n";
    std::cout << " Engine A Score:     " << score_a << " / " << total_games 
              << " (" << std::fixed << std::setprecision(1) << (win_rate * 100.0) << "%)\n";
    std::cout << " Record (+/=/-):     " << wins_a << " wins, " << draws << " draws, " << wins_b << " losses\n";
    std::cout << " Time Elapsed:       " << std::setprecision(2) << total_sec << " seconds\n";
    std::cout << " Calculated Delta Elo: " << (elo_diff >= 0 ? "+" : "") << std::setprecision(1) << elo_diff
              << " +/- " << elo_error << " (95% CI)\n";
    std::cout << "=================================================================\n";

    return 0;
}
