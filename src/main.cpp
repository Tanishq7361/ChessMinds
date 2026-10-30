#include "chessmind/engine.hpp"

/// @brief ChessMinds executable entry point.
/// Initializes the engine facade and starts standard UCI communication.
int main() {
    chessmind::Engine engine;
    engine.run_uci();
    return 0;
}
