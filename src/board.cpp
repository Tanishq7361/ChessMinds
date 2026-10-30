#include "chessmind/board.hpp"
#include "chessmind/zobrist.hpp"
#include <sstream>
#include <cctype>

namespace chessmind {

namespace {

constexpr std::array<uint8_t, 64> CASTLE_MASK = []{
    std::array<uint8_t, 64> m{};
    m.fill(0xF);
    m[SQ_A1] &= ~WHITE_OOO;
    m[SQ_H1] &= ~WHITE_OO;
    m[SQ_E1] &= ~(WHITE_OO | WHITE_OOO);
    m[SQ_A8] &= ~BLACK_OOO;
    m[SQ_H8] &= ~BLACK_OO;
    m[SQ_E8] &= ~(BLACK_OO | BLACK_OOO);
    return m;
}();

} // namespace

Board::Board() {
    bitboards::init();
    zobrist::init();
    reset();
}

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

void Board::put_piece(Square sq, Piece p) {
    if (sq >= 64 || p == EMPTY) return;
    Color c = color_of(p);
    PieceType pt = type_of(p);
    Bitboard mask = square_bb(sq);

    pieces_[c][pt] |= mask;
    occ_[c] |= mask;
    all_occ_ |= mask;
    board_[sq] = p;
}

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

bool Board::set_fen(std::string_view fen) {
    clear();
    std::istringstream iss{std::string(fen)};
    std::string piece_placement, active_color, castling, ep_str;
    int halfmove = 0, fullmove = 1;

    if (!(iss >> piece_placement >> active_color >> castling >> ep_str)) {
        return false;
    }
    iss >> halfmove >> fullmove;

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

    side_to_move_ = (active_color == "b") ? BLACK : WHITE;

    castling_rights_ = NO_CASTLING;
    for (char ch : castling) {
        switch (ch) {
            case 'K': castling_rights_ |= WHITE_OO; break;
            case 'Q': castling_rights_ |= WHITE_OOO; break;
            case 'k': castling_rights_ |= BLACK_OO; break;
            case 'q': castling_rights_ |= BLACK_OOO; break;
            default: break;
        }
    }

    if (ep_str != "-" && ep_str.length() == 2) {
        int ep_file = ep_str[0] - 'a';
        int ep_rank = ep_str[1] - '1';
        if (ep_file >= 0 && ep_file < 8 && ep_rank >= 0 && ep_rank < 8) {
            ep_square_ = make_square(ep_file, ep_rank);
        }
    }

    halfmove_clock_ = halfmove;
    fullmove_number_ = (fullmove > 0) ? fullmove : 1;
    zobrist_hash_ = compute_zobrist();
    repetition_history_.push_back(zobrist_hash_);

    return true;
}

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

bool Board::is_square_attacked(Square sq, Color attacker_color) const noexcept {
    // Pawns
    Bitboard pawn_att = bitboards::pawn_attacks(~attacker_color, sq);
    if (pawn_att & pieces_[attacker_color][PAWN]) return true;

    // Knights
    if (bitboards::knight_attacks(sq) & pieces_[attacker_color][KNIGHT]) return true;

    // Kings
    if (bitboards::king_attacks(sq) & pieces_[attacker_color][KING]) return true;

    // Bishops & Queens
    Bitboard bq = pieces_[attacker_color][BISHOP] | pieces_[attacker_color][QUEEN];
    if (bq && (bitboards::bishop_attacks(sq, all_occ_) & bq)) return true;

    // Rooks & Queens
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

    // Save captured piece
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

    // Castling rook move
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

    // Placement on destination (handling promotion)
    if (m.is_promotion()) {
        put_piece(m.to, make_piece(us, m.promo));
    } else {
        put_piece(m.to, moving_piece);
    }

    // Update halfmove clock (reset on pawn moves or captures)
    if (pt == PAWN || state.captured != EMPTY) {
        halfmove_clock_ = 0;
    } else {
        ++halfmove_clock_;
    }

    // Update castling rights
    castling_rights_ &= CASTLE_MASK[m.from];
    castling_rights_ &= CASTLE_MASK[m.to];

    // Update en passant square
    if ((m.flags & FLAG_DOUBLE_PUSH) != 0) {
        ep_square_ = (us == WHITE) ? static_cast<Square>(m.from + 8) : static_cast<Square>(m.from - 8);
    } else {
        ep_square_ = SQ_NONE;
    }

    // Advance turn
    if (us == BLACK) {
        ++fullmove_number_;
    }
    side_to_move_ = them;

    zobrist_hash_ = compute_zobrist();
    history_.push_back(state);
    repetition_history_.push_back(zobrist_hash_);
}

void Board::undo_move() {
    if (history_.empty()) return;

    StateRecord state = history_.back();
    history_.pop_back();
    repetition_history_.pop_back();

    Move m = state.move;
    Color them = side_to_move_; // enemy
    Color us = ~them;           // side that played move m

    // Revert piece on destination square
    remove_piece(m.to);

    // Put moving piece back on origin square
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

    // Revert castling rook move
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

    // Restore board state variables
    castling_rights_ = state.castling_rights;
    ep_square_ = state.ep_square;
    halfmove_clock_ = state.halfmove_clock;
    zobrist_hash_ = state.zobrist_hash;

    if (us == BLACK) {
        --fullmove_number_;
    }
    side_to_move_ = us;
}

bool Board::is_draw_by_repetition(int count) const noexcept {
    int occurrences = 0;
    int limit = static_cast<int>(repetition_history_.size()) - 1;
    int max_steps = std::min(halfmove_clock_, limit);

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

    if (w_minors == 0 && b_minors == 0) return true; // K vs K
    if (w_minors == 1 && b_minors == 0) return true; // KB vs K or KN vs K
    if (w_minors == 0 && b_minors == 1) return true; // K vs KB or K vs KN

    // KB vs KB with same-colored bishops
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
