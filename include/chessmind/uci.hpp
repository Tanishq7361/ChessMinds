#pragma once

#include "chessmind/board.hpp"
#include "chessmind/tt.hpp"
#include "chessmind/search.hpp"
#include <string>

namespace chessmind {

/// @brief Universal Chess Interface (UCI) protocol handler for communication with GUIs and bot bridges.
class Uci {
public:
    Uci();

    /// @brief Main input reading loop parsing standard UCI commands from stdin until "quit".
    void loop();

private:
    void handle_uci();
    void handle_isready();
    void handle_ucinewgame();
    void handle_position(const std::string& line);
    void handle_go(const std::string& line);
    void handle_bench(int depth = 6);

    Board board_;
    TranspositionTable tt_;
    Searcher searcher_;
};

} // namespace chessmind
