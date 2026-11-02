#include "chessmind/board.hpp"
#include "chessmind/movegen.hpp"
#include "chessmind/search.hpp"
#include "chessmind/eval.hpp"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>
#include <random>

using namespace chessmind;

/// @brief Baseline agent: picks the best capture using MVV-LVA or a random legal move.
Move baseline_move(Board& board, std::mt19937& rng) {
    std::vector<Move> legal;
    MoveGen::generate_legal_moves(board, legal);
    if (legal.empty()) return NULL_MOVE;

    Move best_capture = NULL_MOVE;
    int best_score = -1;

    for (const auto& m : legal) {
        if (m.is_capture()) {
            Piece victim = board.piece_at(m.to);
            PieceType v_pt = (victim != EMPTY) ? type_of(victim) : PAWN;
            PieceType a_pt = type_of(board.piece_at(m.from));
            int score = static_cast<int>(v_pt) * 10 - static_cast<int>(a_pt);
            if (score > best_score) {
                best_score = score;
                best_capture = m;
            }
        }
    }

    if (!best_capture.is_null()) return best_capture;

    std::uniform_int_distribution<size_t> dist(0, legal.size() - 1);
    return legal[dist(rng)];
}

/// @brief Simulates a game between ChessMinds and Baseline agent under time controls.
/// @param engine_color Color played by ChessMinds.
/// @param total_time_ms Initial clock time in ms.
/// @param inc_ms Increment in ms.
bool run_game(Color engine_color, int total_time_ms, int inc_ms) {
    Board board;
    board.reset();

    TranspositionTable tt(16);
    Searcher engine(tt);

    int wtime = total_time_ms;
    int btime = total_time_ms;

    std::mt19937 rng(42);
    int ply = 0;
    constexpr int MAX_PLIES = 200;

    std::cout << "\n=======================================================\n";
    std::cout << "Starting Simulation Game: ChessMinds (" 
              << (engine_color == WHITE ? "White" : "Black") << ") vs Baseline\n";
    std::cout << "Initial Clock: " << total_time_ms << " ms | Increment: " << inc_ms << " ms\n";
    std::cout << "=======================================================\n";

    while (ply < MAX_PLIES) {
        Color stm = board.side_to_move();
        std::vector<Move> legal_moves;
        MoveGen::generate_legal_moves(board, legal_moves);

        if (legal_moves.empty()) {
            if (board.in_check(stm)) {
                std::cout << "--> Game ended by CHECKMATE on ply " << ply << "! Winner: "
                          << (stm == WHITE ? "Black" : "White") << "\n";
            } else {
                std::cout << "--> Game ended by STALEMATE on ply " << ply << "!\n";
            }
            return true;
        }

        if (board.is_fifty_move_rule()) {
            std::cout << "--> Game ended by FIFTY-MOVE RULE on ply " << ply << "!\n";
            return true;
        }

        if (board.is_draw_by_repetition(3)) {
            std::cout << "--> Game ended by THREEFOLD REPETITION on ply " << ply << "!\n";
            return true;
        }

        if (board.is_insufficient_material()) {
            std::cout << "--> Game ended by INSUFFICIENT MATERIAL on ply " << ply << "!\n";
            return true;
        }

        Move chosen_move = NULL_MOVE;

        if (stm == engine_color) {
            SearchLimits limits;
            limits.wtime = wtime;
            limits.btime = btime;
            limits.winc = inc_ms;
            limits.binc = inc_ms;

            auto t0 = std::chrono::steady_clock::now();
            chosen_move = engine.start_search(board, limits);
            auto t1 = std::chrono::steady_clock::now();
            int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

            // Validate that move is legal
            bool is_legal = false;
            for (const auto& lm : legal_moves) {
                if (lm == chosen_move) {
                    is_legal = true;
                    break;
                }
            }

            if (!is_legal) {
                std::cerr << "CRITICAL ERROR: ChessMinds produced illegal move: " 
                          << chosen_move.to_uci() << " on ply " << ply << "\n";
                return false;
            }

            if (stm == WHITE) {
                wtime -= elapsed;
                wtime += inc_ms;
                if (wtime <= 0) {
                    std::cerr << "CRITICAL ERROR: ChessMinds TIMED OUT as White on ply " << ply << "!\n";
                    return false;
                }
            } else {
                btime -= elapsed;
                btime += inc_ms;
                if (btime <= 0) {
                    std::cerr << "CRITICAL ERROR: ChessMinds TIMED OUT as Black on ply " << ply << "!\n";
                    return false;
                }
            }

            std::cout << "Ply " << (ply + 1) << " [" << (stm == WHITE ? "W" : "B") << "] ChessMinds: "
                      << chosen_move.to_uci() << " (spent " << elapsed << " ms, clock left: "
                      << (stm == WHITE ? wtime : btime) << " ms)\n";
        } else {
            chosen_move = baseline_move(board, rng);
            if (stm == WHITE) {
                wtime += inc_ms;
            } else {
                btime += inc_ms;
            }
            std::cout << "Ply " << (ply + 1) << " [" << (stm == WHITE ? "W" : "B") << "] Baseline:   "
                      << chosen_move.to_uci() << "\n";
        }

        board.make_move(chosen_move);
        ++ply;
    }

    std::cout << "--> Game reached maximum ply limit (" << MAX_PLIES << "). Adjudicated Draw.\n";
    return true;
}

int main() {
    std::cout << "========================================================\n";
    std::cout << " ChessMinds Automated Baseline Game Simulation Suite   \n";
    std::cout << "========================================================\n";

    // Test Game 1: ChessMinds as White (scaled blitz: 10s + 200ms)
    bool game1_ok = run_game(WHITE, 10000, 200);
    if (!game1_ok) {
        std::cerr << "Game 1 failed!\n";
        return 1;
    }

    // Test Game 2: ChessMinds as Black (scaled blitz: 10s + 200ms)
    bool game2_ok = run_game(BLACK, 10000, 200);
    if (!game2_ok) {
        std::cerr << "Game 2 failed!\n";
        return 1;
    }

    std::cout << "\n========================================================\n";
    std::cout << " ALL GAME SIMULATIONS PASSED! [100%]\n";
    std::cout << " Zero illegal moves, zero timeouts, zero crashes.\n";
    std::cout << "========================================================\n";
    return 0;
}
