#include "chessmind/uci.hpp"
#include "chessmind/movegen.hpp"
#include <iostream>
#include <sstream>
#include <vector>

namespace chessmind {

Uci::Uci() : tt_(16), searcher_(tt_) {
    board_.reset();
}

/// @brief Handles the 'uci' command: identifies engine and responds with 'uciok'.
void Uci::handle_uci() {
    std::cout << "id name ChessMinds\n"
              << "id author Tanishq Shah\n"
              << "uciok\n" << std::flush;
}

/// @brief Handles the 'isready' command: responds with 'readyok' to confirm initialization.
void Uci::handle_isready() {
    std::cout << "readyok\n" << std::flush;
}

/// @brief Handles 'ucinewgame': resets board, history, and clears the transposition table.
void Uci::handle_ucinewgame() {
    tt_.clear();
    searcher_.reset();
    board_.reset();
}

/// @brief Handles the 'position' command: parses 'startpos' or 'fen', followed by optional move list.
void Uci::handle_position(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    iss >> token; // consume "position"

    if (!(iss >> token)) return;

    if (token == "startpos") {
        board_.reset();
        iss >> token; // checks for "moves" or EOF
    } else if (token == "fen") {
        std::string fen;
        // Read 6 standard FEN fields
        for (int i = 0; i < 6 && (iss >> token); ++i) {
            if (token == "moves") break;
            if (!fen.empty()) fen += " ";
            fen += token;
        }
        board_.set_fen(fen);
        if (token != "moves") {
            iss >> token; // checks for "moves"
        }
    }

    // Apply subsequent moves
    if (token == "moves") {
        std::string move_str;
        while (iss >> move_str) {
            std::vector<Move> legal;
            MoveGen::generate_legal_moves(board_, legal);
            bool played = false;
            for (const auto& m : legal) {
                if (m.to_uci() == move_str) {
                    board_.make_move(m);
                    played = true;
                    break;
                }
            }
            if (!played) {
                break;
            }
        }
    }
}

/// @brief Handles the 'go' command: parses search bounds (time, increment, depth) and outputs 'bestmove'.
void Uci::handle_go(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    iss >> token; // consume "go"

    SearchLimits limits;

    while (iss >> token) {
        if (token == "wtime") {
            iss >> limits.wtime;
        } else if (token == "btime") {
            iss >> limits.btime;
        } else if (token == "winc") {
            iss >> limits.winc;
        } else if (token == "binc") {
            iss >> limits.binc;
        } else if (token == "movetime") {
            iss >> limits.movetime;
        } else if (token == "depth") {
            iss >> limits.max_depth;
        } else if (token == "infinite") {
            limits.infinite = true;
        }
    }

    Move best_move = searcher_.start_search(board_, limits);
    std::cout << "bestmove " << best_move.to_uci() << "\n" << std::flush;
}

/// @brief Runs standard benchmark positions at fixed depth and reports overall performance.
void Uci::handle_bench(int depth) {
    constexpr const char* BENCH_FENS[] = {
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
        "r1bqkb1r/pppp1ppp/2n5/4p3/2B1n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 5",
        "r2q1rk1/ppp2ppp/2n1bn2/2b1p3/3pP3/3P1NPP/PPP1NPB1/R1BQ1RK1 b - - 0 9"
    };

    std::cout << "[BENCH] Running ChessMinds benchmark across 5 positions (depth " << depth << ")..." << std::endl;
    auto t0 = std::chrono::steady_clock::now();

    for (size_t i = 0; i < std::size(BENCH_FENS); ++i) {
        board_.set_fen(BENCH_FENS[i]);
        tt_.clear();
        searcher_.reset();

        SearchLimits limits;
        limits.max_depth = depth;
        Move m = searcher_.start_search(board_, limits);
        std::cout << "Position " << (i + 1) << " -> bestmove " << m.to_uci() << std::endl;
    }

    auto t1 = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "\n============================================\n"
              << "ChessMinds Benchmark Complete\n"
              << "Total Time: " << elapsed_ms << " ms\n"
              << "Depth:      " << depth << "\n"
              << "============================================\n" << std::flush;
}

/// @brief Continuously reads commands from stdin and dispatches to appropriate handlers.
void Uci::loop() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        std::istringstream iss(line);
        std::string command;
        iss >> command;

        if (command == "uci") {
            handle_uci();
        } else if (command == "isready") {
            handle_isready();
        } else if (command == "ucinewgame") {
            handle_ucinewgame();
        } else if (command == "position") {
            handle_position(line);
        } else if (command == "go") {
            handle_go(line);
        } else if (command == "bench") {
            int depth = 6;
            iss >> depth;
            if (depth <= 0) depth = 6;
            handle_bench(depth);
        } else if (command == "stop") {
            searcher_.stop();
        } else if (command == "quit") {
            break;
        }
    }
}

} // namespace chessmind
