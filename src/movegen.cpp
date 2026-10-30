#include "chessmind/movegen.hpp"

namespace chessmind {

void MoveGen::generate_pseudo_legal_moves(const Board& board, std::vector<Move>& moves, bool captures_only) {
    Color us = board.side_to_move();
    Color them = ~us;
    Bitboard friendly = board.color_bb(us);
    Bitboard enemy = board.color_bb(them);
    Bitboard all_occ = board.all_occupied();

    // 1. Pawns
    Bitboard pawns = board.piece_bb(us, PAWN);
    while (pawns) {
        Square from = pop_lsb(pawns);
        int r = square_rank(from);

        if (us == WHITE) {
            // Push
            Square to1 = static_cast<Square>(from + 8);
            if (board.piece_at(to1) == EMPTY) {
                if (r == 6) { // Promotion
                    moves.emplace_back(from, to1, FLAG_PROMOTION, QUEEN);
                    moves.emplace_back(from, to1, FLAG_PROMOTION, ROOK);
                    moves.emplace_back(from, to1, FLAG_PROMOTION, BISHOP);
                    moves.emplace_back(from, to1, FLAG_PROMOTION, KNIGHT);
                } else if (!captures_only) {
                    moves.emplace_back(from, to1, FLAG_QUIET);
                    if (r == 1) { // Double push
                        Square to2 = static_cast<Square>(from + 16);
                        if (board.piece_at(to2) == EMPTY) {
                            moves.emplace_back(from, to2, FLAG_DOUBLE_PUSH);
                        }
                    }
                }
            }

            // Captures
            Bitboard att = bitboards::pawn_attacks(WHITE, from) & enemy;
            while (att) {
                Square to = pop_lsb(att);
                if (r == 6) {
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, QUEEN);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, ROOK);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, BISHOP);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, KNIGHT);
                } else {
                    moves.emplace_back(from, to, FLAG_CAPTURE);
                }
            }

            // En passant
            if (board.ep_square() != SQ_NONE) {
                if (bitboards::pawn_attacks(WHITE, from) & square_bb(board.ep_square())) {
                    moves.emplace_back(from, board.ep_square(), FLAG_EN_PASSANT | FLAG_CAPTURE);
                }
            }
        } else {
            // Black pawns
            Square to1 = static_cast<Square>(from - 8);
            if (board.piece_at(to1) == EMPTY) {
                if (r == 1) { // Promotion
                    moves.emplace_back(from, to1, FLAG_PROMOTION, QUEEN);
                    moves.emplace_back(from, to1, FLAG_PROMOTION, ROOK);
                    moves.emplace_back(from, to1, FLAG_PROMOTION, BISHOP);
                    moves.emplace_back(from, to1, FLAG_PROMOTION, KNIGHT);
                } else if (!captures_only) {
                    moves.emplace_back(from, to1, FLAG_QUIET);
                    if (r == 6) { // Double push
                        Square to2 = static_cast<Square>(from - 16);
                        if (board.piece_at(to2) == EMPTY) {
                            moves.emplace_back(from, to2, FLAG_DOUBLE_PUSH);
                        }
                    }
                }
            }

            // Captures
            Bitboard att = bitboards::pawn_attacks(BLACK, from) & enemy;
            while (att) {
                Square to = pop_lsb(att);
                if (r == 1) {
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, QUEEN);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, ROOK);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, BISHOP);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, KNIGHT);
                } else {
                    moves.emplace_back(from, to, FLAG_CAPTURE);
                }
            }

            // En passant
            if (board.ep_square() != SQ_NONE) {
                if (bitboards::pawn_attacks(BLACK, from) & square_bb(board.ep_square())) {
                    moves.emplace_back(from, board.ep_square(), FLAG_EN_PASSANT | FLAG_CAPTURE);
                }
            }
        }
    }

    // 2. Knights
    Bitboard knights = board.piece_bb(us, KNIGHT);
    while (knights) {
        Square from = pop_lsb(knights);
        Bitboard targets = bitboards::knight_attacks(from) & ~friendly;
        if (captures_only) targets &= enemy;
        while (targets) {
            Square to = pop_lsb(targets);
            if (square_bb(to) & enemy) {
                moves.emplace_back(from, to, FLAG_CAPTURE);
            } else {
                moves.emplace_back(from, to, FLAG_QUIET);
            }
        }
    }

    // 3. Bishops
    Bitboard bishops = board.piece_bb(us, BISHOP);
    while (bishops) {
        Square from = pop_lsb(bishops);
        Bitboard targets = bitboards::bishop_attacks(from, all_occ) & ~friendly;
        if (captures_only) targets &= enemy;
        while (targets) {
            Square to = pop_lsb(targets);
            if (square_bb(to) & enemy) {
                moves.emplace_back(from, to, FLAG_CAPTURE);
            } else {
                moves.emplace_back(from, to, FLAG_QUIET);
            }
        }
    }

    // 4. Rooks
    Bitboard rooks = board.piece_bb(us, ROOK);
    while (rooks) {
        Square from = pop_lsb(rooks);
        Bitboard targets = bitboards::rook_attacks(from, all_occ) & ~friendly;
        if (captures_only) targets &= enemy;
        while (targets) {
            Square to = pop_lsb(targets);
            if (square_bb(to) & enemy) {
                moves.emplace_back(from, to, FLAG_CAPTURE);
            } else {
                moves.emplace_back(from, to, FLAG_QUIET);
            }
        }
    }

    // 5. Queens
    Bitboard queens = board.piece_bb(us, QUEEN);
    while (queens) {
        Square from = pop_lsb(queens);
        Bitboard targets = bitboards::queen_attacks(from, all_occ) & ~friendly;
        if (captures_only) targets &= enemy;
        while (targets) {
            Square to = pop_lsb(targets);
            if (square_bb(to) & enemy) {
                moves.emplace_back(from, to, FLAG_CAPTURE);
            } else {
                moves.emplace_back(from, to, FLAG_QUIET);
            }
        }
    }

    // 6. King
    Square ksq = board.king_square(us);
    if (ksq != SQ_NONE) {
        Bitboard targets = bitboards::king_attacks(ksq) & ~friendly;
        if (captures_only) targets &= enemy;
        while (targets) {
            Square to = pop_lsb(targets);
            if (square_bb(to) & enemy) {
                moves.emplace_back(ksq, to, FLAG_CAPTURE);
            } else {
                moves.emplace_back(ksq, to, FLAG_QUIET);
            }
        }

        // Castling (only during full search, not in quiescence, and only when king is not in check)
        if (!captures_only && !board.is_square_attacked(ksq, them)) {
            uint8_t cr = board.castling_rights();
            if (us == WHITE) {
                if (cr & WHITE_OO) {
                    if (!(all_occ & (square_bb(SQ_F1) | square_bb(SQ_G1))) &&
                        !board.is_square_attacked(SQ_F1, BLACK) &&
                        !board.is_square_attacked(SQ_G1, BLACK)) {
                        moves.emplace_back(SQ_E1, SQ_G1, FLAG_CASTLE);
                    }
                }
                if (cr & WHITE_OOO) {
                    if (!(all_occ & (square_bb(SQ_B1) | square_bb(SQ_C1) | square_bb(SQ_D1))) &&
                        !board.is_square_attacked(SQ_D1, BLACK) &&
                        !board.is_square_attacked(SQ_C1, BLACK)) {
                        moves.emplace_back(SQ_E1, SQ_C1, FLAG_CASTLE);
                    }
                }
            } else {
                if (cr & BLACK_OO) {
                    if (!(all_occ & (square_bb(SQ_F8) | square_bb(SQ_G8))) &&
                        !board.is_square_attacked(SQ_F8, WHITE) &&
                        !board.is_square_attacked(SQ_G8, WHITE)) {
                        moves.emplace_back(SQ_E8, SQ_G8, FLAG_CASTLE);
                    }
                }
                if (cr & BLACK_OOO) {
                    if (!(all_occ & (square_bb(SQ_B8) | square_bb(SQ_C8) | square_bb(SQ_D8))) &&
                        !board.is_square_attacked(SQ_D8, WHITE) &&
                        !board.is_square_attacked(SQ_C8, WHITE)) {
                        moves.emplace_back(SQ_E8, SQ_C8, FLAG_CASTLE);
                    }
                }
            }
        }
    }
}

void MoveGen::generate_legal_moves(Board& board, std::vector<Move>& legal_moves) {
    std::vector<Move> pseudo;
    pseudo.reserve(48);
    generate_pseudo_legal_moves(board, pseudo, false);

    legal_moves.clear();
    legal_moves.reserve(pseudo.size());

    Color us = board.side_to_move();
    for (const auto& m : pseudo) {
        board.make_move(m);
        if (!board.in_check(us)) {
            legal_moves.push_back(m);
        }
        board.undo_move();
    }
}

void MoveGen::generate_legal_captures(Board& board, std::vector<Move>& legal_captures) {
    std::vector<Move> pseudo;
    pseudo.reserve(24);
    generate_pseudo_legal_moves(board, pseudo, true);

    legal_captures.clear();
    legal_captures.reserve(pseudo.size());

    Color us = board.side_to_move();
    for (const auto& m : pseudo) {
        board.make_move(m);
        if (!board.in_check(us)) {
            legal_captures.push_back(m);
        }
        board.undo_move();
    }
}

} // namespace chessmind

