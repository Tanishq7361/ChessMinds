#pragma once

#include "chessmind/uci.hpp"
#include <string_view>

namespace chessmind {

/// @brief Top-level facade coordinating the ChessMinds engine lifecycle.
class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    /// @brief Returns the official engine name.
    [[nodiscard]] std::string_view name() const noexcept;

    /// @brief Returns the semantic version string of the engine.
    [[nodiscard]] std::string_view version() const noexcept;

    /// @brief Starts the UCI standard command-line interaction loop.
    void run_uci();
};

} // namespace chessmind
