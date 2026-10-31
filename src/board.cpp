#include "chessmind/board.hpp"
#include "chessmind/zobrist.hpp"
#include <sstream>
#include <cctype>

namespace chessmind {

namespace {

/// @brief Precomputed mask to quickly revoke castling rights when a king or rook moves or is captured.
constexpr std::array<uint8_t, 64> CASTLE_MASK = []{
    std::array<uint8_t, 64> m{};
    m.fill(0xF);
    m[SQ_A1] &= ~WHITE_OOO;              // White queen rook moved/captured
    m[SQ_H1] &= ~WHITE_OO;               // White king rook moved/captured
    m[SQ_E1] &= ~(WHITE_OO | WHITE_OOO); // White king moved
    m[SQ_A8] &= ~BLACK_OOO;              // Black queen rook moved/captured
    m[SQ_H8] &= ~BLACK_OO;               // Black king rook moved/captured
    m[SQ_E8] &= ~(BLACK_OO | BLACK_OOO); // Black king moved
    return m;
}();

} // namespace

Board::Board() {
    bitboards::init();
    zobrist::init();
    reset();
}

/// @brief Clears all piece placements, occupancies, and game metadata.
void Board::clear() {
    for (int c = 0; c < 2; ++c) {
        for (int pt = 0; pt < 6; ++pt) {
            pieces_[c][pt] = 0;
        }
        occ_[c] = 0;
    }
    all_occ_ = 0;
    board_.fill(EMPTY);

    side_to_move_ = WHITE;
    castling_rights_ = NO_CASTLING;
    ep_square_ = SQ_NONE;
    halfmove_clock_ = 0;
    fullmove_number_ = 1;
    zobrist_hash_ = 0;

    history_.clear();
    repetition_history_.clear();
}

/// @brief Places a piece on a square and updates the bitboard structures.
void Board::put_piece(Square sq, Piece p) {
    if (sq >= 64 || p == EMPTY) return;
    if (board_[sq] != EMPTY) {
        remove_piece(sq);
    }
    Color c = color_of(p);
    PieceType pt = type_of(p);
    Bitboard mask = square_bb(sq);

    pieces_[c][pt] |= mask;
    occ_[c] |= mask;
    all_occ_ |= mask;
    board_[sq] = p;
}

/// @brief Removes a piece from a square and clears its corresponding bitboard bits.
void Board::remove_piece(Square sq) {
    if (sq >= 64) return;
    Piece p = board_[sq];
    if (p == EMPTY) return;
    Color c = color_of(p);
    PieceType pt = type_of(p);
    Bitboard mask = ~square_bb(sq);

    pieces_[c][pt] &= mask;
    occ_[c] &= mask;
    all_occ_ &= mask;
    board_[sq] = EMPTY;
}

void Board::reset() {
    set_fen(START_FEN);
}

/// @brief Parses a standard 6-field FEN string into the board state.
bool Board::set_fen(std::string_view fen) {
    clear();
    std::istringstream iss{std::string(fen)};
    std::string piece_placement, active_color, castling, ep_str;
    int halfmove = 0, fullmove = 1;

    if (!(iss >> piece_placement >> active_color >> castling >> ep_str)) {
        return false;
    }
    iss >> halfmove >> fullmove;

    // 1. Piece placement
    int r = 7;
    int f = 0;
    for (char ch : piece_placement) {
        if (ch == '/') {
            --r;
            f = 0;
        } else if (std::isdigit(static_cast<unsigned char>(ch))) {
            f += ch - '0';
        } else {
            Piece p = EMPTY;
            switch (ch) {
                case 'P': p = W_PAWN; break;
                case 'N': p = W_KNIGHT; break;
                case 'B': p = W_BISHOP; break;
                case 'R': p = W_ROOK; break;
                case 'Q': p = W_QUEEN; break;
                case 'K': p = W_KING; break;
                case 'p': p = B_PAWN; break;
                case 'n': p = B_KNIGHT; break;
                case 'b': p = B_BISHOP; break;
                case 'r': p = B_ROOK; break;
                case 'q': p = B_QUEEN; break;
                case 'k': p = B_KING; break;
                default: return false;
            }
            if (f < 8 && r >= 0) {
                put_piece(make_square(f, r), p);
                ++f;
            }
        }
    }

    // 2. Active color
    side_to_move_ = (active_color == "b") ? BLACK : WHITE;

    // 3. Castling availability - validate that king and rooks are actually present
    castling_rights_ = NO_CASTLING;
    for (char ch : castling) {
        switch (ch) {
            case 'K':
                if (board_[SQ_E1] == W_KING && board_[SQ_H1] == W_ROOK) {
                    castling_rights_ |= WHITE_OO;
                }
                break;
            case 'Q':
                if (board_[SQ_E1] == W_KING && board_[SQ_A1] == W_ROOK) {
                    castling_rights_ |= WHITE_OOO;
                }
                break;
            case 'k':
                if (board_[SQ_E8] == B_KING && board_[SQ_H8] == B_ROOK) {
                    castling_rights_ |= BLACK_OO;
                }
                break;
            case 'q':
                if (board_[SQ_E8] == B_KING && board_[SQ_A8] == B_ROOK) {
                    castling_rights_ |= BLACK_OOO;
                }
                break;
            default: break;
        }
    }

    // 4. En passant target square - validate rank and presence of enemy pawn
    ep_square_ = SQ_NONE;
    if (ep_str != "-" && ep_str.length() == 2) {
        int ep_file = ep_str[0] - 'a';
        int ep_rank = ep_str[1] - '1';
        if (ep_file >= 0 && ep_file < 8) {
            if (side_to_move_ == WHITE && ep_rank == 5) {
                Square target_sq = make_square(ep_file, 5);
                Square pawn_sq = make_square(ep_file, 4);
                if (board_[target_sq] == EMPTY && board_[pawn_sq] == B_PAWN) {
                    ep_square_ = target_sq;
                }
            } else if (side_to_move_ == BLACK && ep_rank == 2) {
                Square target_sq = make_square(ep_file, 2);
                Square pawn_sq = make_square(ep_file, 3);
                if (board_[target_sq] == EMPTY && board_[pawn_sq] == W_PAWN) {
                    ep_square_ = target_sq;
                }
            }
        }
    }

    // 5. Halfmove clock & fullmove number
    halfmove_clock_ = halfmove;
    fullmove_number_ = (fullmove > 0) ? fullmove : 1;
    zobrist_hash_ = compute_zobrist();
    repetition_history_.push_back(zobrist_hash_);

    return true;
}

/// @brief Generates a standard FEN representation of the current board state.
std::string Board::to_fen() const {
    std::string fen;
    for (int r = 7; r >= 0; --r) {
        int empty_count = 0;
        for (int f = 0; f < 8; ++f) {
            Square sq = make_square(f, r);
            Piece p = board_[sq];
            if (p == EMPTY) {
                ++empty_count;
            } else {
                if (empty_count > 0) {
                    fen += std::to_string(empty_count);
                    empty_count = 0;
                }
                constexpr const char symbols[] = "PNBRQKpnbrqk";
                fen += symbols[p];
            }
        }
        if (empty_count > 0) {
            fen += std::to_string(empty_count);
        }
        if (r > 0) fen += '/';
    }

    fen += (side_to_move_ == WHITE ? " w " : " b ");

    std::string castling;
    if (castling_rights_ & WHITE_OO) castling += 'K';
    if (castling_rights_ & WHITE_OOO) castling += 'Q';
    if (castling_rights_ & BLACK_OO) castling += 'k';
    if (castling_rights_ & BLACK_OOO) castling += 'q';
    if (castling.empty()) castling = "-";
    fen += castling + " ";

    if (ep_square_ == SQ_NONE) {
        fen += "- ";
    } else {
        fen += static_cast<char>('a' + square_file(ep_square_));
        fen += static_cast<char>('1' + square_rank(ep_square_));
        fen += " ";
    }

    fen += std::to_string(halfmove_clock_) + " " + std::to_string(fullmove_number_);
    return fen;
}

/// @brief Calculates the full 64-bit Zobrist key by combining piece, castling, ep, and side keys.
uint64_t Board::compute_zobrist() const noexcept {
    uint64_t hash = 0;
    for (int sq = 0; sq < 64; ++sq) {
        Piece p = board_[sq];
        if (p != EMPTY) {
            hash ^= zobrist::PIECE_KEYS[color_of(p)][type_of(p)][sq];
        }
    }
    hash ^= zobrist::CASTLE_KEYS[castling_rights_ & 0xF];
    if (ep_square_ != SQ_NONE) {
        hash ^= zobrist::EP_KEYS[ep_square_];
    }
    if (side_to_move_ == BLACK) {
        hash ^= zobrist::SIDE_KEY;
    }
    return hash;
}

Square Board::king_square(Color c) const noexcept {
    Bitboard k = pieces_[c][KING];
    return k ? lsb(k) : SQ_NONE;
}

/// @brief Checks whether `sq` is attacked by any enemy piece by casting inverse attack rays.
bool Board::is_square_attacked(Square sq, Color attacker_color) const noexcept {
    // 1. Pawn attacks
    Bitboard pawn_att = bitboards::pawn_attacks(~attacker_color, sq);
    if (pawn_att & pieces_[attacker_color][PAWN]) return true;

    // 2. Knight attacks
    if (bitboards::knight_attacks(sq) & pieces_[attacker_color][KNIGHT]) return true;

    // 3. King attacks
    if (bitboards::king_attacks(sq) & pieces_[attacker_color][KING]) return true;

    // 4. Diagonal sliders (Bishops and Queens)
    Bitboard bq = pieces_[attacker_color][BISHOP] | pieces_[attacker_color][QUEEN];
    if (bq && (bitboards::bishop_attacks(sq, all_occ_) & bq)) return true;

    // 5. Orthogonal sliders (Rooks and Queens)
    Bitboard rq = pieces_[attacker_color][ROOK] | pieces_[attacker_color][QUEEN];
    if (rq && (bitboards::rook_attacks(sq, all_occ_) & rq)) return true;

    return false;
}

bool Board::in_check(Color c) const noexcept {
    Square ksq = king_square(c);
    return (ksq != SQ_NONE) && is_square_attacked(ksq, ~c);
}

bool Board::in_check() const noexcept {
    return in_check(side_to_move_);
}

/// @brief Applies a move to the board and saves the previous state for undo_move.
void Board::make_move(const Move& m) {
    StateRecord state;
    state.move = m;
    state.moved = board_[m.from];
    state.castling_rights = castling_rights_;
    state.ep_square = ep_square_;
    state.halfmove_clock = halfmove_clock_;
    state.zobrist_hash = zobrist_hash_;

    Piece moving_piece = state.moved;
    Color us = side_to_move_;
    Color them = ~us;
    PieceType pt = type_of(moving_piece);

    // Handle piece captures (including en passant)
    if (m.is_en_passant()) {
        Square cap_sq = make_square(square_file(m.to), square_rank(m.from));
        state.captured = board_[cap_sq];
        remove_piece(cap_sq);
    } else {
        state.captured = board_[m.to];
        if (state.captured != EMPTY) {
            remove_piece(m.to);
        }
    }

    // Move piece from origin
    remove_piece(m.from);

    // Relocate rook if castling
    if (m.is_castling()) {
        if (m.to == SQ_G1) {
            remove_piece(SQ_H1);
            put_piece(SQ_F1, W_ROOK);
        } else if (m.to == SQ_C1) {
            remove_piece(SQ_A1);
            put_piece(SQ_D1, W_ROOK);
        } else if (m.to == SQ_G8) {
            remove_piece(SQ_H8);
            put_piece(SQ_F8, B_ROOK);
        } else if (m.to == SQ_C8) {
            remove_piece(SQ_A8);
            put_piece(SQ_D8, B_ROOK);
        }
    }

    // Place piece at destination (promote if applicable)
    if (m.is_promotion()) {
        PieceType pr = m.promo;
        if (pr != QUEEN && pr != ROOK && pr != BISHOP && pr != KNIGHT) {
            pr = QUEEN;
        }
        put_piece(m.to, make_piece(us, pr));
    } else {
        put_piece(m.to, moving_piece);
    }

    // Reset halfmove clock on pawn moves or captures (50-move rule counter)
    if (pt == PAWN || state.captured != EMPTY) {
        halfmove_clock_ = 0;
    } else {
        ++halfmove_clock_;
    }

    // Update castling rights if king or rook moved or was captured
    castling_rights_ &= CASTLE_MASK[m.from];
    castling_rights_ &= CASTLE_MASK[m.to];

    // Set new en passant target square if a pawn pushed two squares
    if ((m.flags & FLAG_DOUBLE_PUSH) != 0) {
        ep_square_ = (us == WHITE) ? static_cast<Square>(m.from + 8) : static_cast<Square>(m.from - 8);
    } else {
        ep_square_ = SQ_NONE;
    }

    // Advance turn and fullmove counter
    if (us == BLACK) {
        ++fullmove_number_;
    }
    side_to_move_ = them;

    zobrist_hash_ = compute_zobrist();
    history_.push_back(state);
    repetition_history_.push_back(zobrist_hash_);
}

/// @brief Reverts the most recent move and restores the saved state.
void Board::undo_move() {
    if (history_.empty()) return;

    StateRecord state = history_.back();
    history_.pop_back();
    repetition_history_.pop_back();

    Move m = state.move;
    Color them = side_to_move_; // Current turn
    Color us = ~them;           // Player whose move is being undone

    // Remove piece from destination square
    remove_piece(m.to);

    // Restore moved piece back to origin square
    put_piece(m.from, state.moved);

    // Restore captured piece if any
    if (state.captured != EMPTY) {
        if (m.is_en_passant()) {
            Square cap_sq = make_square(square_file(m.to), square_rank(m.from));
            put_piece(cap_sq, state.captured);
        } else {
            put_piece(m.to, state.captured);
        }
    }

    // Restore castling rook to initial corner
    if (m.is_castling()) {
        if (m.to == SQ_G1) {
            remove_piece(SQ_F1);
            put_piece(SQ_H1, W_ROOK);
        } else if (m.to == SQ_C1) {
            remove_piece(SQ_D1);
            put_piece(SQ_A1, W_ROOK);
        } else if (m.to == SQ_G8) {
            remove_piece(SQ_F8);
            put_piece(SQ_H8, B_ROOK);
        } else if (m.to == SQ_C8) {
            remove_piece(SQ_D8);
            put_piece(SQ_A8, B_ROOK);
        }
    }

    // Revert state variables
    castling_rights_ = state.castling_rights;
    ep_square_ = state.ep_square;
    halfmove_clock_ = state.halfmove_clock;
    zobrist_hash_ = state.zobrist_hash;

    if (us == BLACK) {
        --fullmove_number_;
    }
    side_to_move_ = us;
}

/// @brief Passes turn without making a physical piece move (Null-Move Pruning).
void Board::make_null_move() {
    StateRecord state;
    state.move = NULL_MOVE;
    state.moved = EMPTY;
    state.captured = EMPTY;
    state.castling_rights = castling_rights_;
    state.ep_square = ep_square_;
    state.halfmove_clock = halfmove_clock_;
    state.zobrist_hash = zobrist_hash_;

    // Clear en passant and advance turn
    ep_square_ = SQ_NONE;
    ++halfmove_clock_;
    if (side_to_move_ == BLACK) {
        ++fullmove_number_;
    }
    side_to_move_ = ~side_to_move_;

    zobrist_hash_ = compute_zobrist();
    history_.push_back(state);
    repetition_history_.push_back(zobrist_hash_);
}

/// @brief Undoes a null move, restoring turn and previous en passant state.
void Board::undo_null_move() {
    if (history_.empty()) return;

    StateRecord state = history_.back();
    history_.pop_back();
    repetition_history_.pop_back();

    Color us = ~side_to_move_;
    if (us == BLACK) {
        --fullmove_number_;
    }
    side_to_move_ = us;

    castling_rights_ = state.castling_rights;
    ep_square_ = state.ep_square;
    halfmove_clock_ = state.halfmove_clock;
    zobrist_hash_ = state.zobrist_hash;
}

/// @brief Checks if the current Zobrist key has appeared `count` times within the reversible halfmove window.
bool Board::is_draw_by_repetition(int count) const noexcept {
    int occurrences = 0;
    int limit = static_cast<int>(repetition_history_.size()) - 1;
    int max_steps = std::min(halfmove_clock_, limit);

    // Positions can only match on the same player's turn (step of 2)
    for (int i = 0; i <= max_steps; i += 2) {
        if (repetition_history_[limit - i] == zobrist_hash_) {
            ++occurrences;
            if (occurrences >= count) return true;
        }
    }
    return false;
}

bool Board::is_fifty_move_rule() const noexcept {
    return halfmove_clock_ >= 100;
}

/// @brief Evaluates whether insufficient material remains on board to force checkmate.
bool Board::is_insufficient_material() const noexcept {
    if (pieces_[WHITE][PAWN] || pieces_[BLACK][PAWN] ||
        pieces_[WHITE][ROOK] || pieces_[BLACK][ROOK] ||
        pieces_[WHITE][QUEEN] || pieces_[BLACK][QUEEN]) {
        return false;
    }

    int w_knights = pop_count(pieces_[WHITE][KNIGHT]);
    int b_knights = pop_count(pieces_[BLACK][KNIGHT]);
    int w_bishops = pop_count(pieces_[WHITE][BISHOP]);
    int b_bishops = pop_count(pieces_[BLACK][BISHOP]);

    int w_minors = w_knights + w_bishops;
    int b_minors = b_knights + b_bishops;

    if (w_minors == 0 && b_minors == 0) return true; // King vs King
    if (w_minors == 1 && b_minors == 0) return true; // King + Minor vs King
    if (w_minors == 0 && b_minors == 1) return true; // King vs King + Minor

    // King + Bishop vs King + Bishop with both bishops on the same color squares
    if (w_minors == 1 && b_minors == 1 && w_bishops == 1 && b_bishops == 1) {
        Square w_bsq = lsb(pieces_[WHITE][BISHOP]);
        Square b_bsq = lsb(pieces_[BLACK][BISHOP]);
        bool w_light = ((square_file(w_bsq) + square_rank(w_bsq)) % 2) != 0;
        bool b_light = ((square_file(b_bsq) + square_rank(b_bsq)) % 2) != 0;
        if (w_light == b_light) return true;
    }

    return false;
}

} // namespace chessmind
