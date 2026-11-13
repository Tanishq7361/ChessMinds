#include "chessmind/tt.hpp"
#include "chessmind/eval.hpp"
#include <algorithm>
#include <bit>

namespace chessmind {

TranspositionTable::TranspositionTable(size_t size_mb) {
    resize(size_mb);
}

/// @brief Allocates clusters to fit the specified RAM size, rounding down to power of 2.
void TranspositionTable::resize(size_t size_mb) {
    size_t bytes = size_mb * 1024 * 1024;
    size_t clusters = bytes / sizeof(TTCluster);
    if (clusters == 0) clusters = 1024;
    
    // Round down to power of 2 for ultra-fast bitmask indexing
    clusters_count_ = 1ULL << (63 - std::countl_zero(clusters));
    mask_ = clusters_count_ - 1;
    table_.assign(clusters_count_, TTCluster{});
}

void TranspositionTable::clear() {
    std::fill(table_.begin(), table_.end(), TTCluster{});
}

namespace {

/// @brief Converts mate scores from root-relative to node-relative for persistent storage.
int score_to_tt(int score, int ply) noexcept {
    if (score > Evaluator::MATE_SCORE - 1000) return score + ply;
    if (score < -Evaluator::MATE_SCORE + 1000) return score - ply;
    return score;
}

/// @brief Re-adjusts stored mate score relative to current search ply.
int score_from_tt(int score, int ply) noexcept {
    if (score > Evaluator::MATE_SCORE - 1000) return score - ply;
    if (score < -Evaluator::MATE_SCORE + 1000) return score + ply;
    return score;
}

} // namespace

/// @brief Checks whether a matching entry exists in the 2-way cluster and satisfies search depth/bounds.
bool TranspositionTable::probe(uint64_t key, int depth, int alpha, int beta, int ply, int& score, Move& best_move) const noexcept {
    if (clusters_count_ == 0) return false;
    const TTCluster& cluster = table_[key & mask_];

    for (int i = 0; i < 2; ++i) {
        const TTEntry& entry = cluster.entries[i];
        if (entry.key == key) {
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
    }
    return false;
}

/// @brief Returns the best move stored for this key across cluster slots (for move ordering).
Move TranspositionTable::probe_move(uint64_t key) const noexcept {
    if (clusters_count_ == 0) return NULL_MOVE;
    const TTCluster& cluster = table_[key & mask_];
    if (cluster.entries[0].key == key && !cluster.entries[0].best_move.is_null()) {
        return cluster.entries[0].best_move;
    }
    if (cluster.entries[1].key == key && !cluster.entries[1].best_move.is_null()) {
        return cluster.entries[1].best_move;
    }
    return NULL_MOVE;
}

/// @brief Stores or updates entry in 2-way cluster: slot 0 is depth/age-preferred, slot 1 is always-replace.
void TranspositionTable::store(uint64_t key, int depth, int score, uint8_t flag, int ply, Move best_move) noexcept {
    if (clusters_count_ == 0) return;
    TTCluster& cluster = table_[key & mask_];

    // 1. If key already exists in the cluster, update that slot
    for (int i = 0; i < 2; ++i) {
        if (cluster.entries[i].key == key) {
            if (depth >= cluster.entries[i].depth || flag == TT_EXACT || cluster.entries[i].age != generation_) {
                cluster.entries[i].depth = static_cast<int8_t>(depth);
                cluster.entries[i].score = static_cast<int16_t>(score_to_tt(score, ply));
                cluster.entries[i].flag = flag;
                cluster.entries[i].age = generation_;
            }
            if (!best_move.is_null()) {
                cluster.entries[i].best_move = best_move;
            }
            return;
        }
    }

    // 2. Key is new: slot 0 is depth/generation-preferred, slot 1 is always-replace
    int target_idx = 1;
    if (cluster.entries[0].key == 0 || 
        cluster.entries[0].age != generation_ || 
        depth >= cluster.entries[0].depth) {
        target_idx = 0;
    }

    TTEntry& entry = cluster.entries[target_idx];
    entry.key = key;
    entry.depth = static_cast<int8_t>(depth);
    entry.score = static_cast<int16_t>(score_to_tt(score, ply));
    entry.flag = flag;
    entry.age = generation_;
    if (!best_move.is_null()) {
        entry.best_move = best_move;
    }
}

} // namespace chessmind
