#pragma once

#include "chessmind/types.hpp"
#include "chessmind/bitboard.hpp"
#include <array>
#include <vector>
#include <string>
#include <string_view>

namespace chessmind {

struct StateRecord {
    Move move;
    Piece moved = EMPTY;
    Piece captured = EMPTY;
    uint8_t castling_rights = 0;
    Square ep_square = SQ_NONE;
    int halfmove_clock = 0;
    uint64_t zobrist_hash = 0;
};

class Board {
public:
    static constexpr std::string_view START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Board();

    void reset();
    bool set_fen(std::string_view fen);
    [[nodiscard]] std::string to_fen() const;

    void make_move(const Move& m);
    void undo_move();

    [[nodiscard]] bool is_square_attacked(Square sq, Color attacker_color) const noexcept;
    [[nodiscard]] bool in_check() const noexcept;
    [[nodiscard]] bool in_check(Color c) const noexcept;
    [[nodiscard]] Square king_square(Color c) const noexcept;

    [[nodiscard]] bool is_draw_by_repetition(int count = 2) const noexcept;
    [[nodiscard]] bool is_fifty_move_rule() const noexcept;
    [[nodiscard]] bool is_insufficient_material() const noexcept;

    [[nodiscard]] Bitboard piece_bb(Color c, PieceType pt) const noexcept { return pieces_[c][pt]; }
    [[nodiscard]] Bitboard color_bb(Color c) const noexcept { return occ_[c]; }
    [[nodiscard]] Bitboard all_occupied() const noexcept { return all_occ_; }
    [[nodiscard]] Piece piece_at(Square sq) const noexcept { return board_[sq]; }
    [[nodiscard]] Color side_to_move() const noexcept { return side_to_move_; }
    [[nodiscard]] uint8_t castling_rights() const noexcept { return castling_rights_; }
    [[nodiscard]] Square ep_square() const noexcept { return ep_square_; }
    [[nodiscard]] int halfmove_clock() const noexcept { return halfmove_clock_; }
    [[nodiscard]] int fullmove_number() const noexcept { return fullmove_number_; }
    [[nodiscard]] uint64_t zobrist_hash() const noexcept { return zobrist_hash_; }

    [[nodiscard]] uint64_t compute_zobrist() const noexcept;

private:
    void clear();
    void put_piece(Square sq, Piece p);
    void remove_piece(Square sq);

    std::array<std::array<Bitboard, 6>, 2> pieces_{};
    std::array<Bitboard, 2> occ_{};
    Bitboard all_occ_ = 0;
    std::array<Piece, 64> board_{};

    Color side_to_move_ = WHITE;
    uint8_t castling_rights_ = ALL_CASTLING;
    Square ep_square_ = SQ_NONE;
    int halfmove_clock_ = 0;
    int fullmove_number_ = 1;
    uint64_t zobrist_hash_ = 0;

    std::vector<StateRecord> history_;
    std::vector<uint64_t> repetition_history_;
};

} // namespace chessmind
