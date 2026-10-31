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
        while (iss >> token && token != "moves") {
            if (!fen.empty()) fen += " ";
            fen += token;
        }
        board_.set_fen(fen);
    }

    // Apply subsequent moves with full legality verification
    if (token == "moves") {
        std::string move_str;
        while (iss >> move_str) {
            Move m = MoveGen::parse_uci_move(board_, move_str);
            if (m.is_null()) {
                std::cerr << "info string Warning: rejected illegal move '" << move_str
                          << "' in position (side to move: "
                          << (board_.side_to_move() == WHITE ? "White" : "Black") << ")\n";
                break;
            }
            board_.make_move(m);
        }
    }
}

/// @brief Handles the 'go' command: parses search bounds (time, increment, depth) and outputs 'bestmove'.
void Uci::handle_go(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    iss >> token; // consume "go"

    // Check for 'go perft <depth>' first
    if (iss >> token) {
        if (token == "perft") {
            int depth = 1;
            iss >> depth;
            if (depth <= 0) depth = 1;
            handle_perft(depth);
            return;
        }
    }

    // Reset the stream and re-parse for normal go command
    std::istringstream iss2(line);
    iss2 >> token; // consume "go"

    SearchLimits limits;

    while (iss2 >> token) {
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

static uint64_t run_perft_divide(Board& b, int depth) {
    if (depth == 0) return 1ULL;
    std::vector<Move> moves;
    MoveGen::generate_legal_moves(b, moves);
    if (depth == 1) return moves.size();
    uint64_t nodes = 0;
    for (const auto& m : moves) {
        b.make_move(m);
        nodes += run_perft_divide(b, depth - 1);
        b.undo_move();
    }
    return nodes;
}

/// @brief Runs a perft divide on the current board position and prints per-move leaf counts.
void Uci::handle_perft(int depth) {
    std::vector<Move> moves;
    MoveGen::generate_legal_moves(board_, moves);
    uint64_t total_nodes = 0;
    auto t0 = std::chrono::steady_clock::now();

    for (const auto& m : moves) {
        board_.make_move(m);
        uint64_t nodes = (depth > 1) ? run_perft_divide(board_, depth - 1) : 1ULL;
        board_.undo_move();
        total_nodes += nodes;
        std::cout << m.to_uci() << ": " << nodes << "\n";
    }

    auto t1 = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    std::cout << "\nNodes searched: " << total_nodes << "\n"
              << "Time taken:     " << elapsed_ms << " ms\n" << std::flush;
}

/// @brief Outputs all currently legal moves for UI hint dots and frontend validation.
void Uci::handle_legalmoves() {
    std::vector<Move> moves;
    MoveGen::generate_legal_moves(board_, moves);
    std::cout << "legalmoves";
    for (const auto& m : moves) {
        std::cout << " " << m.to_uci();
    }
    std::cout << "\n" << std::flush;
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
        } else if (command == "perft") {
            int depth = 1;
            iss >> depth;
            if (depth <= 0) depth = 1;
            handle_perft(depth);
        } else if (command == "legalmoves") {
            handle_legalmoves();
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
