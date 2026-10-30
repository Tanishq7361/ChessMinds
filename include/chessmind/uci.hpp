#pragma once

#include "chessmind/board.hpp"
#include "chessmind/tt.hpp"
#include "chessmind/search.hpp"
#include <string>

namespace chessmind {

class Uci {
public:
    Uci();
    void loop();

private:
    void handle_uci();
    void handle_isready();
    void handle_ucinewgame();
    void handle_position(const std::string& line);
    void handle_go(const std::string& line);

    Board board_;
    TranspositionTable tt_;
    Searcher searcher_;
};

} // namespace chessmind

