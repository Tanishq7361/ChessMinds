#include "chessmind/movegen.hpp"

namespace chessmind {

/// @brief Generates pseudo-legal moves for all piece types according to chess movement rules.
void MoveGen::generate_pseudo_legal_moves(const Board& board, std::vector<Move>& moves, bool captures_only) {
    Color us = board.side_to_move();
    Color them = ~us;
    Bitboard friendly = board.color_bb(us);
    Bitboard enemy = board.color_bb(them);
    Bitboard all_occ = board.all_occupied();

    // -------------------------------------------------------------
    // 1. Pawn Moves (Single Push, Double Push, Captures, En Passant, Promotions)
    // -------------------------------------------------------------
    Bitboard pawns = board.piece_bb(us, PAWN);
    while (pawns) {
        Square from = pop_lsb(pawns);
        int r = square_rank(from);

        if (us == WHITE) {
            // White single push forward (r + 1)
            if (r < 7) {
                Square to1 = static_cast<Square>(from + 8);
                if (board.piece_at(to1) == EMPTY) {
                    if (r == 6) { // Promotion on 8th rank
                        moves.emplace_back(from, to1, FLAG_PROMOTION, QUEEN);
                        moves.emplace_back(from, to1, FLAG_PROMOTION, ROOK);
                        moves.emplace_back(from, to1, FLAG_PROMOTION, BISHOP);
                        moves.emplace_back(from, to1, FLAG_PROMOTION, KNIGHT);
                    } else if (!captures_only) {
                        moves.emplace_back(from, to1, FLAG_QUIET);
                        // White double push from rank 2
                        if (r == 1) {
                            Square to2 = static_cast<Square>(from + 16);
                            if (board.piece_at(to2) == EMPTY) {
                                moves.emplace_back(from, to2, FLAG_DOUBLE_PUSH);
                            }
                        }
                    }
                }
            }

            // White diagonal captures
            Bitboard att = bitboards::pawn_attacks(WHITE, from) & enemy;
            while (att) {
                Square to = pop_lsb(att);
                if (r == 6) { // Promotion with capture
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, QUEEN);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, ROOK);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, BISHOP);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, KNIGHT);
                } else {
                    moves.emplace_back(from, to, FLAG_CAPTURE);
                }
            }

            // White en passant capture
            if (board.ep_square() != SQ_NONE) {
                if (bitboards::pawn_attacks(WHITE, from) & square_bb(board.ep_square())) {
                    Square cap_sq = make_square(square_file(board.ep_square()), square_rank(from));
                    if (board.piece_at(cap_sq) == B_PAWN && board.piece_at(board.ep_square()) == EMPTY) {
                        moves.emplace_back(from, board.ep_square(), FLAG_EN_PASSANT | FLAG_CAPTURE);
                    }
                }
            }
        } else {
            // Black single push forward (r - 1)
            if (r > 0) {
                Square to1 = static_cast<Square>(from - 8);
                if (board.piece_at(to1) == EMPTY) {
                    if (r == 1) { // Promotion on 1st rank
                        moves.emplace_back(from, to1, FLAG_PROMOTION, QUEEN);
                        moves.emplace_back(from, to1, FLAG_PROMOTION, ROOK);
                        moves.emplace_back(from, to1, FLAG_PROMOTION, BISHOP);
                        moves.emplace_back(from, to1, FLAG_PROMOTION, KNIGHT);
                    } else if (!captures_only) {
                        moves.emplace_back(from, to1, FLAG_QUIET);
                        // Black double push from rank 7
                        if (r == 6) {
                            Square to2 = static_cast<Square>(from - 16);
                            if (board.piece_at(to2) == EMPTY) {
                                moves.emplace_back(from, to2, FLAG_DOUBLE_PUSH);
                            }
                        }
                    }
                }
            }

            // Black diagonal captures
            Bitboard att = bitboards::pawn_attacks(BLACK, from) & enemy;
            while (att) {
                Square to = pop_lsb(att);
                if (r == 1) { // Promotion with capture
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, QUEEN);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, ROOK);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, BISHOP);
                    moves.emplace_back(from, to, FLAG_PROMOTION | FLAG_CAPTURE, KNIGHT);
                } else {
                    moves.emplace_back(from, to, FLAG_CAPTURE);
                }
            }

            // Black en passant capture
            if (board.ep_square() != SQ_NONE) {
                if (bitboards::pawn_attacks(BLACK, from) & square_bb(board.ep_square())) {
                    Square cap_sq = make_square(square_file(board.ep_square()), square_rank(from));
                    if (board.piece_at(cap_sq) == W_PAWN && board.piece_at(board.ep_square()) == EMPTY) {
                        moves.emplace_back(from, board.ep_square(), FLAG_EN_PASSANT | FLAG_CAPTURE);
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------
    // 2. Knight Moves
    // -------------------------------------------------------------
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

    // -------------------------------------------------------------
    // 3. Bishop Moves (Diagonal Slider Rays)
    // -------------------------------------------------------------
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

    // -------------------------------------------------------------
    // 4. Rook Moves (Orthogonal Slider Rays)
    // -------------------------------------------------------------
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

    // -------------------------------------------------------------
    // 5. Queen Moves (Both Diagonal and Orthogonal Rays)
    // -------------------------------------------------------------
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

    // -------------------------------------------------------------
    // 6. King Moves & Castling
    // -------------------------------------------------------------
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

        // Castling moves (only generated for full search, not in quiescence, and only when king is on its home square and not in check)
        if (!captures_only && !board.is_square_attacked(ksq, them)) {
            uint8_t cr = board.castling_rights();
            if (us == WHITE && ksq == SQ_E1) {
                // White kingside (e1 -> g1)
                if ((cr & WHITE_OO) && board.piece_at(SQ_H1) == W_ROOK) {
                    if (!(all_occ & (square_bb(SQ_F1) | square_bb(SQ_G1))) &&
                        !board.is_square_attacked(SQ_F1, BLACK) &&
                        !board.is_square_attacked(SQ_G1, BLACK)) {
                        moves.emplace_back(SQ_E1, SQ_G1, FLAG_CASTLE);
                    }
                }
                // White queenside (e1 -> c1)
                if ((cr & WHITE_OOO) && board.piece_at(SQ_A1) == W_ROOK) {
                    if (!(all_occ & (square_bb(SQ_B1) | square_bb(SQ_C1) | square_bb(SQ_D1))) &&
                        !board.is_square_attacked(SQ_D1, BLACK) &&
                        !board.is_square_attacked(SQ_C1, BLACK)) {
                        moves.emplace_back(SQ_E1, SQ_C1, FLAG_CASTLE);
                    }
                }
            } else if (us == BLACK && ksq == SQ_E8) {
                // Black kingside (e8 -> g8)
                if ((cr & BLACK_OO) && board.piece_at(SQ_H8) == B_ROOK) {
                    if (!(all_occ & (square_bb(SQ_F8) | square_bb(SQ_G8))) &&
                        !board.is_square_attacked(SQ_F8, WHITE) &&
                        !board.is_square_attacked(SQ_G8, WHITE)) {
                        moves.emplace_back(SQ_E8, SQ_G8, FLAG_CASTLE);
                    }
                }
                // Black queenside (e8 -> c8)
                if ((cr & BLACK_OOO) && board.piece_at(SQ_A8) == B_ROOK) {
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

/// @brief Generates all pseudo-legal moves and filters out any move that leaves the moving king in check.
void MoveGen::generate_legal_moves(Board& board, std::vector<Move>& legal_moves) {
    std::vector<Move> pseudo;
    pseudo.reserve(48);
    generate_pseudo_legal_moves(board, pseudo, false);

    legal_moves.clear();
    legal_moves.reserve(pseudo.size());

    Color us = board.side_to_move();
    for (const auto& m : pseudo) {
        // Never allow capturing the enemy king
        if (board.piece_at(m.to) == make_piece(~us, KING)) {
            continue;
        }
        board.make_move(m);
        // After make_move, the moving side is ~side_to_move(). Test if that king is attacked.
        if (!board.in_check(us)) {
            legal_moves.push_back(m);
        }
        board.undo_move();
    }
}

/// @brief Generates only pseudo-legal captures and filters out moves leaving king in check.
void MoveGen::generate_legal_captures(Board& board, std::vector<Move>& legal_captures) {
    std::vector<Move> pseudo;
    pseudo.reserve(24);
    generate_pseudo_legal_moves(board, pseudo, true);

    legal_captures.clear();
    legal_captures.reserve(pseudo.size());

    Color us = board.side_to_move();
    for (const auto& m : pseudo) {
        if (board.piece_at(m.to) == make_piece(~us, KING)) {
            continue;
        }
        board.make_move(m);
        if (!board.in_check(us)) {
            legal_captures.push_back(m);
        }
        board.undo_move();
    }
}

/// @brief Checks whether a specific move is strictly legal in the given board position.
bool MoveGen::is_legal_move(Board& board, const Move& m) {
    std::vector<Move> legal;
    generate_legal_moves(board, legal);
    for (const auto& lm : legal) {
        if (lm == m) return true;
    }
    return false;
}

/// @brief Parses a UCI move string (e.g. "e2e4", "e7e8q") and verifies it against legal moves.
Move MoveGen::parse_uci_move(Board& board, std::string_view uci) {
    while (!uci.empty() && std::isspace(static_cast<unsigned char>(uci.front()))) {
        uci.remove_prefix(1);
    }
    while (!uci.empty() && (std::isspace(static_cast<unsigned char>(uci.back())) || uci.back() == '\r' || uci.back() == '\n')) {
        uci.remove_suffix(1);
    }
    if (uci.length() < 4 || uci.length() > 5) {
        return NULL_MOVE;
    }

    std::string clean;
    clean.reserve(uci.length());
    for (char c : uci) {
        clean += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    Square from = string_to_square(clean.substr(0, 2));
    Square to = string_to_square(clean.substr(2, 2));
    if (from == SQ_NONE || to == SQ_NONE) {
        return NULL_MOVE;
    }

    PieceType promo = PieceType::NONE;
    if (clean.length() == 5) {
        switch (clean[4]) {
            case 'q': promo = QUEEN; break;
            case 'r': promo = ROOK; break;
            case 'b': promo = BISHOP; break;
            case 'n': promo = KNIGHT; break;
            default: return NULL_MOVE;
        }
    }

    std::vector<Move> legal;
    generate_legal_moves(board, legal);

    for (const auto& m : legal) {
        if (m.from == from && m.to == to) {
            if (m.is_promotion()) {
                if (promo == PieceType::NONE) {
                    if (m.promo == QUEEN) return m; // Default to queen promotion
                } else if (m.promo == promo) {
                    return m;
                }
            } else {
                return m;
            }
        }
    }

    return NULL_MOVE;
}

} // namespace chessmind
