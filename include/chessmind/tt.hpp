#pragma once

#include "chessmind/types.hpp"
#include <vector>
#include <cstdint>

namespace chessmind {

enum TTFlag : uint8_t {
    TT_NONE = 0,
    TT_EXACT = 1,
    TT_LOWERBOUND = 2,
    TT_UPPERBOUND = 3
};

struct TTEntry {
    uint64_t key = 0;
    Move best_move{};
    int16_t score = 0;
    int8_t depth = -1;
    uint8_t flag = TT_NONE;
};

class TranspositionTable {
public:
    explicit TranspositionTable(size_t size_mb = 16);

    void resize(size_t size_mb);
    void clear();

    [[nodiscard]] bool probe(uint64_t key, int depth, int alpha, int beta, int ply, int& score, Move& best_move) const noexcept;
    void store(uint64_t key, int depth, int score, uint8_t flag, int ply, Move best_move) noexcept;
    [[nodiscard]] Move probe_move(uint64_t key) const noexcept;

private:
    std::vector<TTEntry> table_;
    size_t count_ = 0;
};

} // namespace chessmind

