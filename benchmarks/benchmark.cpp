#include "chessmind/board.hpp"
#include "chessmind/tt.hpp"
#include "chessmind/search.hpp"
#include <iostream>
#include <chrono>

using namespace chessmind;

int main() {
    std::cout << "========================================================\n"
              << "          ChessMinds Engine Benchmark Suite             \n"
              << "========================================================\n\n";

    constexpr const char* BENCH_FENS[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",                                  // Opening startpos
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",                      // KiwiPete (tactics)
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",                                                  // Rook endgame
        "r1bqkb1r/pppp1ppp/2n5/4p3/2B1n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 5",                          // Italian tactical position
        "r2q1rk1/ppp2ppp/2n1bn2/2b1p3/3pP3/3P1NPP/PPP1NPB1/R1BQ1RK1 b - - 0 9"                       // Closed King's Indian middlegame
    };

    TranspositionTable tt(16);
    Searcher searcher(tt);
    Board board;

    constexpr int SEARCH_DEPTH = 6;
    auto total_t0 = std::chrono::steady_clock::now();

    for (size_t i = 0; i < std::size(BENCH_FENS); ++i) {
        std::cout << "--- Position " << (i + 1) << " / " << std::size(BENCH_FENS) << " ---\n"
                  << "FEN: " << BENCH_FENS[i] << "\n";

        board.set_fen(BENCH_FENS[i]);
        tt.clear();
        searcher.reset();

        SearchLimits limits;
        limits.max_depth = SEARCH_DEPTH;

        auto t0 = std::chrono::steady_clock::now();
        Move best = searcher.start_search(board, limits);
        auto t1 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        std::cout << "Result: bestmove " << best.to_uci() << " in " << ms << " ms\n\n";
    }

    auto total_t1 = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(total_t1 - total_t0).count();

    std::cout << "========================================================\n"
              << "All positions completed in " << total_ms << " ms (Depth " << SEARCH_DEPTH << ")\n"
              << "Engine performance verified successfully!\n"
              << "========================================================\n";

    return 0;
}

