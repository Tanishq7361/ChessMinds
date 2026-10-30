#include "chessmind/board.hpp"
#include "chessmind/movegen.hpp"
#include <iostream>
#include <chrono>

using namespace chessmind;

/// @brief Recursively counts leaf nodes to depth `depth` to verify move generator correctness.
uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1ULL;
    std::vector<Move> moves;
    MoveGen::generate_legal_moves(board, moves);
    if (depth == 1) return moves.size();

    uint64_t nodes = 0;
    for (const auto& m : moves) {
        board.make_move(m);
        nodes += perft(board, depth - 1);
        board.undo_move();
    }
    return nodes;
}

/// @brief Runs standard Perft benchmark suites on starting position and KiwiPete position.
int main() {
    Board board;

    // 1. Starting position benchmark
    std::cout << "[TEST] Running Perft tests on starting position..." << std::endl;
    board.reset();

    constexpr uint64_t expected_startpos[] = {
        1ULL,
        20ULL,
        400ULL,
        8902ULL,
        197281ULL
    };

    for (int d = 1; d <= 4; ++d) {
        auto t0 = std::chrono::steady_clock::now();
        uint64_t nodes = perft(board, d);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "Depth " << d << ": " << nodes << " nodes (" << ms << " ms) - ";
        if (nodes == expected_startpos[d]) {
            std::cout << "PASS\n";
        } else {
            std::cout << "FAIL (Expected: " << expected_startpos[d] << ")\n";
            return 1;
        }
    }

    // 2. KiwiPete position benchmark (tests tricky en passant, castling, promotion, and check states)
    std::cout << "[TEST] Running Perft tests on KiwiPete position..." << std::endl;
    bool ok = board.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    if (!ok) {
        std::cerr << "Failed to set KiwiPete FEN\n";
        return 1;
    }

    constexpr uint64_t expected_kiwipete[] = {
        1ULL,
        48ULL,
        2039ULL,
        97862ULL
    };

    for (int d = 1; d <= 3; ++d) {
        auto t0 = std::chrono::steady_clock::now();
        uint64_t nodes = perft(board, d);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "KiwiPete Depth " << d << ": " << nodes << " nodes (" << ms << " ms) - ";
        if (nodes == expected_kiwipete[d]) {
            std::cout << "PASS\n";
        } else {
            std::cout << "FAIL (Expected: " << expected_kiwipete[d] << ")\n";
            return 1;
        }
    }

    std::cout << "All Perft tests PASSED successfully!\n";
    return 0;
}
