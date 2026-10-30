#include "chessmind/tt.hpp"
#include "chessmind/eval.hpp"
#include <algorithm>

namespace chessmind {

TranspositionTable::TranspositionTable(size_t size_mb) {
    resize(size_mb);
}

void TranspositionTable::resize(size_t size_mb) {
    size_t bytes = size_mb * 1024 * 1024;
    count_ = bytes / sizeof(TTEntry);
    if (count_ == 0) count_ = 1024;
    table_.assign(count_, TTEntry{});
}

void TranspositionTable::clear() {
    std::fill(table_.begin(), table_.end(), TTEntry{});
}

namespace {

int score_to_tt(int score, int ply) noexcept {
    if (score > Evaluator::MATE_SCORE - 1000) return score + ply;
    if (score < -Evaluator::MATE_SCORE + 1000) return score - ply;
    return score;
}

int score_from_tt(int score, int ply) noexcept {
    if (score > Evaluator::MATE_SCORE - 1000) return score - ply;
    if (score < -Evaluator::MATE_SCORE + 1000) return score + ply;
    return score;
}

} // namespace

bool TranspositionTable::probe(uint64_t key, int depth, int alpha, int beta, int ply, int& score, Move& best_move) const noexcept {
    if (count_ == 0) return false;
    const TTEntry& entry = table_[key % count_];

    if (entry.key != key) return false;

    best_move = entry.best_move;

    if (entry.depth >= depth) {
        int tt_score = score_from_tt(entry.score, ply);
        if (entry.flag == TT_EXACT) {
            score = tt_score;
            return true;
        }
        if (entry.flag == TT_LOWERBOUND && tt_score >= beta) {
            score = tt_score;
            return true;
        }
        if (entry.flag == TT_UPPERBOUND && tt_score <= alpha) {
            score = tt_score;
            return true;
        }
    }
    return false;
}

Move TranspositionTable::probe_move(uint64_t key) const noexcept {
    if (count_ == 0) return NULL_MOVE;
    const TTEntry& entry = table_[key % count_];
    return (entry.key == key) ? entry.best_move : NULL_MOVE;
}

void TranspositionTable::store(uint64_t key, int depth, int score, uint8_t flag, int ply, Move best_move) noexcept {
    if (count_ == 0) return;
    TTEntry& entry = table_[key % count_];

    // Replace if empty, different key, or searched deeper or same depth
    if (entry.key != key || depth >= entry.depth) {
        entry.key = key;
        entry.depth = static_cast<int8_t>(depth);
        entry.score = static_cast<int16_t>(score_to_tt(score, ply));
        entry.flag = flag;
        if (!best_move.is_null()) {
            entry.best_move = best_move;
        }
    }
}

} // namespace chessmind

