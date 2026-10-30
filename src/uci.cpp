#include "chessmind/uci.hpp"
#include "chessmind/movegen.hpp"
#include <iostream>
#include <sstream>
#include <vector>

namespace chessmind {

Uci::Uci() : tt_(16), searcher_(tt_) {
    board_.reset();
}

void Uci::handle_uci() {
    std::cout << "id name ChessMinds\n"
              << "id author Tanishq Shah\n"
              << "uciok\n" << std::flush;
}

void Uci::handle_isready() {
    std::cout << "readyok\n" << std::flush;
}

void Uci::handle_ucinewgame() {
    tt_.clear();
    searcher_.reset();
    board_.reset();
}

void Uci::handle_position(const std::string& line) {
    std::istringstream iss(line);
    std::string token;
    iss >> token; // consume "position"

    if (!(iss >> token)) return;

    if (token == "startpos") {
        board_.reset();
        iss >> token; // might be "moves" or EOF
    } else if (token == "fen") {
        std::string fen;
        // FEN has 6 fields: pieces, color, castling, ep, halfmove, fullmove
        for (int i = 0; i < 6 && (iss >> token); ++i) {
            if (token == "moves") break;
            if (!fen.empty()) fen += " ";
            fen += token;
        }
        board_.set_fen(fen);
        if (token != "moves") {
            iss >> token; // might be "moves"
        }
    }

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
                // If move failed to match, break out cleanly
                break;
            }
        }
    }
}

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
        } else if (command == "stop") {
            searcher_.stop();
        } else if (command == "quit") {
            break;
        }
    }
}

} // namespace chessmind

