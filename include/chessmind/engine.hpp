#pragma once

#include "chessmind/uci.hpp"
#include <string_view>

namespace chessmind {

class Engine {
public:
    Engine() = default;
    ~Engine() = default;

    [[nodiscard]] std::string_view name() const noexcept;
    [[nodiscard]] std::string_view version() const noexcept;

    void run_uci();
};

} // namespace chessmind
