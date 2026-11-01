#include "chessmind/search.hpp"
#include "chessmind/movegen.hpp"
#include "chessmind/eval.hpp"
#include <iostream>
#include <algorithm>

namespace chessmind {

namespace {

// MVV-LVA (Most Valuable Victim - Least Valuable Attacker) piece value array
// P=100, N=320, B=330, R=500, Q=900, K=10000
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
    for (auto& row : pv_table_) {
        row.fill(NULL_MOVE);
    }
    pv_length_.fill(0);
}

void Searcher::stop() {
    stop_flag_ = true;
}

/// @brief Aborts search if wall clock time exceeds the allocated budget.
void Searcher::check_time_limit() {
    if (stop_flag_) return;
    if (std::chrono::steady_clock::now() >= stop_time_) {
        stop_flag_ = true;
    }
}

/// @brief Assigns a heuristic score to each move for ordering:
/// 1. Hash move from Transposition Table (highest priority: 1,000,000)
/// 2. Good captures using MVV-LVA (100,000 + victim*10 - attacker)
/// 3. Killer moves (quiet moves that recently produced beta cutoffs: 50,000 / 40,000)
/// 4. History heuristic (quiet moves with historical cutoff success)
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

/// @brief Sorts moves in descending order of their heuristic score to maximize alpha-beta pruning cutoffs.
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

/// @brief Quiescence search continues searching captures at leaf nodes to eliminate the horizon effect.
int Searcher::quiescence(Board& board, int alpha, int beta, int ply) {
    ++nodes_;
    // Check time every 2048 nodes
    if ((nodes_ & 2047) == 0) {
        check_time_limit();
    }
    if (stop_flag_) return 0;

    if (ply >= 63) {
        return Evaluator::evaluate(board);
    }

    // Stand-pat score: static evaluation before making any capture
    int stand_pat = Evaluator::evaluate(board);
    if (stand_pat >= beta) {
        return beta; // Fail-high cutoff
    }
    if (stand_pat > alpha) {
        alpha = stand_pat;
    }

    // Search only legal captures and promotions
    std::vector<Move> captures;
    MoveGen::generate_legal_captures(board, captures);
    order_moves(board, captures, NULL_MOVE, ply);

    for (const auto& m : captures) {
        // Delta pruning: skip capture if even capturing a queen cannot bring score up to alpha
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

/// @brief Recursive Negamax algorithm with Alpha-Beta pruning, Null-Move Pruning, and LMR.
int Searcher::negamax(Board& board, int depth, int ply, int alpha, int beta, bool is_null_allowed) {
    if (ply < 64) {
        pv_length_[ply] = ply;
    }

    ++nodes_;
    if ((nodes_ & 2047) == 0) {
        check_time_limit();
    }
    if (stop_flag_) return 0;

    // Repetition and 50-move rule draw detection (at non-root plies)
    if (ply > 0) {
        if (board.is_fifty_move_rule() || board.is_draw_by_repetition(2)) {
            return Evaluator::DRAW_SCORE;
        }
    }

    // Check extension: if in check, search 1 ply deeper to resolve tactical check threats
    bool in_check = board.in_check();
    if (in_check && ply > 0) {
        ++depth;
    }

    int orig_alpha = alpha;
    Move tt_move = NULL_MOVE;
    int tt_score = 0;

    // Transposition Table probe
    if (ply > 0 && tt_.probe(board.zobrist_hash(), depth, alpha, beta, ply, tt_score, tt_move)) {
        return tt_score;
    }
    if (tt_move.is_null()) {
        tt_move = tt_.probe_move(board.zobrist_hash());
    }

    // Transition to quiescence search at leaf nodes
    if (depth <= 0) {
        return quiescence(board, alpha, beta, ply);
    }

    // Static evaluation used for pruning heuristics
    int static_eval = Evaluator::evaluate(board);

    // Reverse Futility Pruning (RFP / Static Null Move Pruning):
    // At shallow depths, if static evaluation is well above beta, prune early without searching moves.
    if (!in_check && ply > 0 && depth <= 3 && std::abs(beta) < Evaluator::MATE_SCORE) {
        int rfp_margin = 75 * depth;
        if (static_eval - rfp_margin >= beta) {
            return beta;
        }
    }

    // Null-Move Pruning (NMP):
    // If not in check, null move allowed, depth >= 3, and side to move has pieces other than pawns
    if (is_null_allowed && !in_check && ply > 0 && depth >= 3 && board.has_non_pawn_material(board.side_to_move())) {
        int R = 2 + (depth > 6 ? 1 : 0);
        board.make_null_move();
        int null_score = -negamax(board, depth - 1 - R, ply + 1, -beta, -beta + 1, false);
        board.undo_null_move();

        if (stop_flag_) return 0;
        if (null_score >= beta) {
            return beta; // Fail-high cutoff without exploring full subtree
        }
    }

    // Generate legal moves
    std::vector<Move> legal_moves;
    MoveGen::generate_legal_moves(board, legal_moves);

    // Terminal position: checkmate or stalemate
    if (legal_moves.empty()) {
        if (in_check) {
            return -Evaluator::MATE_SCORE + ply; // Faster mates scored higher
        }
        return Evaluator::DRAW_SCORE;            // Stalemate
    }

    // Order moves for optimal alpha-beta branch pruning
    order_moves(board, legal_moves, tt_move, ply);

    int best_score = -Evaluator::MATE_SCORE;
    Move best_move = NULL_MOVE;
    int moves_searched = 0;

    for (const auto& m : legal_moves) {
        // Futility Pruning (FP): Skip quiet moves with no prospect of exceeding alpha near leaf nodes
        if (moves_searched > 0 && depth <= 2 && !in_check && !m.is_capture() && !m.is_promotion()) {
            if (static_eval + 120 * depth <= alpha) {
                continue;
            }
        }

        board.make_move(m);
        ++moves_searched;

        int score;
        // Principal Variation Search (PVS) with Late Move Reductions (LMR)
        if (moves_searched == 1) {
            // First move: search with full [alpha, beta] window
            score = -negamax(board, depth - 1, ply + 1, -beta, -alpha, true);
        } else {
            // Subsequent moves: null-window scout search with possible LMR
            int reduction = 0;
            if (moves_searched >= 4 && depth >= 3 && !m.is_capture() && !m.is_promotion() && !in_check) {
                reduction = 1;
                if (moves_searched >= 8 && depth >= 5) {
                    reduction = 2;
                }
            }

            score = -negamax(board, depth - 1 - reduction, ply + 1, -alpha - 1, -alpha, true);

            // Re-search at full depth with null-window if reduced search fails high
            if (reduction > 0 && score > alpha) {
                score = -negamax(board, depth - 1, ply + 1, -alpha - 1, -alpha, true);
            }

            // If scout search fails high (score between alpha and beta), re-search with full window
            if (score > alpha && score < beta) {
                score = -negamax(board, depth - 1, ply + 1, -beta, -alpha, true);
            }
        }

        board.undo_move();

        if (stop_flag_) return 0;

        if (score > best_score) {
            best_score = score;
            best_move = m;
        }

        if (score > alpha) {
            alpha = score;

            // Update Triangular PV Table
            if (ply < 64) {
                pv_table_[ply][ply] = m;
                for (int next_ply = ply + 1; next_ply < pv_length_[ply + 1] && next_ply < 64; ++next_ply) {
                    pv_table_[ply][next_ply] = pv_table_[ply + 1][next_ply];
                }
                pv_length_[ply] = pv_length_[ply + 1];
            }

            if (alpha >= beta) {
                // Beta cutoff (fail-high): record killer move and history heuristic
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

    // Store evaluation in Transposition Table
    uint8_t flag = (best_score <= orig_alpha) ? TT_UPPERBOUND : TT_EXACT;
    tt_.store(board.zobrist_hash(), depth, best_score, flag, ply, best_move);

    return best_score;
}

/// @brief Orchestrates iterative deepening search, allocates time, and outputs UCI info lines.
Move Searcher::start_search(Board& board, const SearchLimits& limits) {
    reset();
    searching_ = true;
    start_time_ = std::chrono::steady_clock::now();

    // 1. Time Management: allocate thinking budget based on clock and increment
    if (limits.movetime > 0) {
        int alloc = std::max(5, limits.movetime - 25);
        stop_time_ = start_time_ + std::chrono::milliseconds(alloc);
    } else if (limits.wtime >= 0 || limits.btime >= 0) {
        int time_left = (board.side_to_move() == WHITE) ? limits.wtime : limits.btime;
        int inc = (board.side_to_move() == WHITE) ? limits.winc : limits.binc;
        int alloc = 0;
        if (time_left > 0) {
            // Allocate 1/25th of remaining time plus 75% of increment
            alloc = time_left / 25 + (inc * 3) / 4;
            if (alloc > time_left - 30) {
                alloc = std::max(5, time_left - 30);
            }
        } else {
            alloc = 50;
        }
        stop_time_ = start_time_ + std::chrono::milliseconds(alloc);
    } else {
        // Unconstrained time: allow up to 1 hour
        stop_time_ = start_time_ + std::chrono::hours(1);
    }

    std::vector<Move> root_moves;
    MoveGen::generate_legal_moves(board, root_moves);
    if (root_moves.empty()) {
        searching_ = false;
        return NULL_MOVE;
    }

    Move best_overall_move = root_moves[0];
    std::string best_overall_pv = best_overall_move.to_uci();
    int best_overall_score = 0;
    int max_depth = std::min(limits.max_depth, 64);

    // 2. Iterative Deepening loop (depth 1, 2, 3...)
    for (int current_depth = 1; current_depth <= max_depth; ++current_depth) {
        Move iteration_best_move = NULL_MOVE;
        order_moves(board, root_moves, best_overall_move, 0);

        int best_score = -Evaluator::MATE_SCORE;

        // Aspiration Windows: Search with a narrow window around previous score for depth >= 4
        int alpha = -Evaluator::MATE_SCORE;
        int beta = Evaluator::MATE_SCORE;
        int delta = 35;

        if (current_depth >= 4 && std::abs(best_overall_score) < Evaluator::MATE_SCORE - 100) {
            alpha = std::max(-Evaluator::MATE_SCORE, best_overall_score - delta);
            beta = std::min(Evaluator::MATE_SCORE, best_overall_score + delta);
        }

        while (true) {
            best_score = -Evaluator::MATE_SCORE;
            int orig_alpha = alpha;

            for (size_t i = 0; i < root_moves.size(); ++i) {
                const auto& m = root_moves[i];
                board.make_move(m);

                int score;
                // Root PVS
                if (i == 0) {
                    score = -negamax(board, current_depth - 1, 1, -beta, -alpha, true);
                } else {
                    score = -negamax(board, current_depth - 1, 1, -alpha - 1, -alpha, true);
                    if (score > alpha && score < beta) {
                        score = -negamax(board, current_depth - 1, 1, -beta, -alpha, true);
                    }
                }

                board.undo_move();

                if (stop_flag_) break;

                if (score > best_score) {
                    best_score = score;
                    iteration_best_move = m;

                    // Update root PV
                    pv_table_[0][0] = m;
                    for (int next_ply = 1; next_ply < pv_length_[1] && next_ply < 64; ++next_ply) {
                        pv_table_[0][next_ply] = pv_table_[1][next_ply];
                    }
                    pv_length_[0] = pv_length_[1];
                }
                if (score > alpha) {
                    alpha = score;
                }
            }

            if (stop_flag_) break;

            // Check if score fell outside aspiration window
            if (best_score <= orig_alpha && orig_alpha > -Evaluator::MATE_SCORE) {
                alpha = -Evaluator::MATE_SCORE;
            } else if (best_score >= beta && beta < Evaluator::MATE_SCORE) {
                beta = Evaluator::MATE_SCORE;
            } else {
                break; // Score is within window or window is already fully open
            }
        }

        if (stop_flag_) {
            break;
        }

        if (!iteration_best_move.is_null()) {
            best_overall_move = iteration_best_move;
            best_overall_score = best_score;
        }

        // Construct full Principal Variation string
        std::string current_pv;
        for (int i = 0; i < pv_length_[0] && i < 64; ++i) {
            if (pv_table_[0][i].is_null()) break;
            if (!current_pv.empty()) current_pv += " ";
            current_pv += pv_table_[0][i].to_uci();
        }
        if (!current_pv.empty()) {
            best_overall_pv = current_pv;
        }

        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
        uint64_t nps = (elapsed_ms > 0) ? (nodes_ * 1000 / elapsed_ms) : 0;

        // Output UCI search status line with full PV
        std::cout << "info depth " << current_depth
                  << " score cp " << best_score
                  << " time " << elapsed_ms
                  << " nodes " << nodes_
                  << " nps " << nps
                  << " pv " << best_overall_pv
                  << std::endl;

        // Early break if guaranteed checkmate found
        if (std::abs(best_score) >= Evaluator::MATE_SCORE - 100) {
            break;
        }

        // Break if allocated time has expired
        if (now >= stop_time_) {
            break;
        }
    }

    searching_ = false;
    return best_overall_move;
}

} // namespace chessmind
