#include "chessmind/search.hpp"
#include "chessmind/movegen.hpp"
#include "chessmind/eval.hpp"
#include <iostream>
#include <algorithm>

namespace chessmind {

namespace {

constexpr int MVV_LVA_VALUES[6] = { 100, 320, 330, 500, 900, 10000 };

} // namespace

Searcher::Searcher(TranspositionTable& tt) : tt_(tt) {
    reset();
}

void Searcher::reset() {
    stop_flag_ = false;
    searching_ = false;
    nodes_ = 0;
    for (auto& row : killer_moves_) {
        row.fill(NULL_MOVE);
    }
    for (auto& c : history_) {
        for (auto& f : c) {
            f.fill(0);
        }
    }
}

void Searcher::stop() {
    stop_flag_ = true;
}

void Searcher::check_time_limit() {
    if (stop_flag_) return;
    if (std::chrono::steady_clock::now() >= stop_time_) {
        stop_flag_ = true;
    }
}

int Searcher::score_move(const Board& board, const Move& m, Move tt_move, int ply) const noexcept {
    if (m == tt_move) return 1'000'000;

    if (m.is_capture()) {
        Piece victim = board.piece_at(m.to);
        PieceType victim_pt = (victim != EMPTY) ? type_of(victim) : PAWN;
        PieceType attacker_pt = type_of(board.piece_at(m.from));
        int score = 100'000 + MVV_LVA_VALUES[victim_pt] * 10 - MVV_LVA_VALUES[attacker_pt];
        if (m.is_promotion()) score += MVV_LVA_VALUES[m.promo];
        return score;
    }

    if (ply < 64) {
        if (m == killer_moves_[ply][0]) return 50'000;
        if (m == killer_moves_[ply][1]) return 40'000;
    }

    Color c = board.side_to_move();
    return history_[c][m.from][m.to];
}

void Searcher::order_moves(const Board& board, std::vector<Move>& moves, Move tt_move, int ply) {
    std::vector<std::pair<int, Move>> scored;
    scored.reserve(moves.size());

    for (const auto& m : moves) {
        scored.emplace_back(score_move(board, m, tt_move, ply), m);
    }

    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    for (size_t i = 0; i < moves.size(); ++i) {
        moves[i] = scored[i].second;
    }
}

int Searcher::quiescence(Board& board, int alpha, int beta, int ply) {
    ++nodes_;
    if ((nodes_ & 2047) == 0) {
        check_time_limit();
    }
    if (stop_flag_) return 0;

    if (ply >= 63) {
        return Evaluator::evaluate(board);
    }

    int stand_pat = Evaluator::evaluate(board);
    if (stand_pat >= beta) {
        return beta;
    }
    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    std::vector<Move> captures;
    MoveGen::generate_legal_captures(board, captures);
    order_moves(board, captures, NULL_MOVE, ply);

    for (const auto& m : captures) {
        // Delta pruning
        if (stand_pat + 900 + 100 < alpha && !m.is_promotion()) {
            continue;
        }

        board.make_move(m);
        int score = -quiescence(board, -beta, -alpha, ply + 1);
        board.undo_move();

        if (stop_flag_) return 0;

        if (score >= beta) {
            return beta;
        }
        if (score > alpha) {
            alpha = score;
        }
    }

    return alpha;
}

int Searcher::negamax(Board& board, int depth, int ply, int alpha, int beta, [[maybe_unused]] bool is_null_allowed) {
    ++nodes_;
    if ((nodes_ & 2047) == 0) {
        check_time_limit();
    }
    if (stop_flag_) return 0;

    if (ply > 0) {
        if (board.is_fifty_move_rule() || board.is_draw_by_repetition(2)) {
            return Evaluator::DRAW_SCORE;
        }
    }

    int orig_alpha = alpha;
    Move tt_move = NULL_MOVE;
    int tt_score = 0;

    if (ply > 0 && tt_.probe(board.zobrist_hash(), depth, alpha, beta, ply, tt_score, tt_move)) {
        return tt_score;
    }
    if (tt_move.is_null()) {
        tt_move = tt_.probe_move(board.zobrist_hash());
    }

    if (depth <= 0) {
        return quiescence(board, alpha, beta, ply);
    }

    std::vector<Move> legal_moves;
    MoveGen::generate_legal_moves(board, legal_moves);

    if (legal_moves.empty()) {
        if (board.in_check()) {
            return -Evaluator::MATE_SCORE + ply; // Checkmate
        }
        return Evaluator::DRAW_SCORE; // Stalemate
    }

    order_moves(board, legal_moves, tt_move, ply);

    int best_score = -Evaluator::MATE_SCORE;
    Move best_move = NULL_MOVE;

    for (const auto& m : legal_moves) {
        board.make_move(m);
        int score = -negamax(board, depth - 1, ply + 1, -beta, -alpha, true);
        board.undo_move();

        if (stop_flag_) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = m;
        }

        if (score > alpha) {
            alpha = score;
            if (alpha >= beta) {
                // Beta cutoff
                if (!m.is_capture() && ply < 64) {
                    killer_moves_[ply][1] = killer_moves_[ply][0];
                    killer_moves_[ply][0] = m;
                    history_[board.side_to_move()][m.from][m.to] += depth * depth;
                }
                tt_.store(board.zobrist_hash(), depth, beta, TT_LOWERBOUND, ply, m);
                return beta;
            }
        }
    }

    uint8_t flag = (best_score <= orig_alpha) ? TT_UPPERBOUND : TT_EXACT;
    tt_.store(board.zobrist_hash(), depth, best_score, flag, ply, best_move);

    return best_score;
}

Move Searcher::start_search(Board& board, const SearchLimits& limits) {
    reset();
    searching_ = true;
    start_time_ = std::chrono::steady_clock::now();

    // Time management calculation
    if (limits.movetime > 0) {
        int alloc = std::max(5, limits.movetime - 25);
        stop_time_ = start_time_ + std::chrono::milliseconds(alloc);
    } else if (limits.wtime >= 0 || limits.btime >= 0) {
        int time_left = (board.side_to_move() == WHITE) ? limits.wtime : limits.btime;
        int inc = (board.side_to_move() == WHITE) ? limits.winc : limits.binc;
        int alloc = 0;
        if (time_left > 0) {
            alloc = time_left / 25 + (inc * 3) / 4;
            if (alloc > time_left - 30) {
                alloc = std::max(5, time_left - 30);
            }
        } else {
            alloc = 50;
        }
        stop_time_ = start_time_ + std::chrono::milliseconds(alloc);
    } else {
        // No time limit specified: give 100 seconds unless bounded by depth
        stop_time_ = start_time_ + std::chrono::hours(1);
    }

    std::vector<Move> root_moves;
    MoveGen::generate_legal_moves(board, root_moves);
    if (root_moves.empty()) {
        searching_ = false;
        return NULL_MOVE;
    }

    Move best_overall_move = root_moves[0];
    int max_depth = std::min(limits.max_depth, 64);

    for (int current_depth = 1; current_depth <= max_depth; ++current_depth) {
        Move iteration_best_move = NULL_MOVE;
        int alpha = -Evaluator::MATE_SCORE;
        int beta = Evaluator::MATE_SCORE;

        order_moves(board, root_moves, best_overall_move, 0);

        int best_score = -Evaluator::MATE_SCORE;

        for (const auto& m : root_moves) {
            board.make_move(m);
            int score = -negamax(board, current_depth - 1, 1, -beta, -alpha, true);
            board.undo_move();

            if (stop_flag_) break;

            if (score > best_score) {
                best_score = score;
                iteration_best_move = m;
            }
            if (score > alpha) {
                alpha = score;
            }
        }

        if (stop_flag_) {
            break;
        }

        if (!iteration_best_move.is_null()) {
            best_overall_move = iteration_best_move;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
        uint64_t nps = (elapsed_ms > 0) ? (nodes_ * 1000 / elapsed_ms) : 0;

        // Print UCI info line
        std::cout << "info depth " << current_depth
                  << " score cp " << best_score
                  << " time " << elapsed_ms
                  << " nodes " << nodes_
                  << " nps " << nps
                  << " pv " << best_overall_move.to_uci()
                  << std::endl;

        // If checkmate is found, stop early
        if (std::abs(best_score) >= Evaluator::MATE_SCORE - 100) {
            break;
        }

        // Check if allocated time expired
        if (now >= stop_time_) {
            break;
        }
    }

    searching_ = false;
    return best_overall_move;
}

} // namespace chessmind

