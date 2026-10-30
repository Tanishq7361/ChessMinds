#include "chessmind/engine.hpp"

namespace chessmind {

std::string_view Engine::name() const noexcept {
    return "ChessMinds Engine";
}

std::string_view Engine::version() const noexcept {
    return "1.0.0";
}

void Engine::run_uci() {
    Uci uci;
    uci.loop();
}

} // namespace chessmind
