#pragma once

#include "chessmind/types.hpp"
#include "chessmind/bitboard.hpp"
#include <array>
#include <vector>
#include <string>
#include <string_view>

namespace chessmind {

/// @brief Snapshot of reversible board state pushed during make_move and restored during undo_move.
struct StateRecord {
    Move move;                      // The move executed
    Piece moved = EMPTY;            // The piece that moved
    Piece captured = EMPTY;         // Any piece captured during this move
    uint8_t castling_rights = 0;    // Castling permissions before the move
    Square ep_square = SQ_NONE;     // En passant target square before the move
    int halfmove_clock = 0;         // Halfmove counter (50-move rule) before the move
    uint64_t zobrist_hash = 0;      // 64-bit Zobrist key of the position before the move
};

/// @brief Represents the state of the chessboard using bitboards and an auxiliary mailbox array.
class Board {
public:
    static constexpr std::string_view START_FEN = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

    Board();

    /// @brief Resets the chessboard to the standard starting position.
    void reset();

    /// @brief Loads a position from a standard Forsyth-Edwards Notation (FEN) string.
    /// @return True if parsing succeeded, false if invalid.
    bool set_fen(std::string_view fen);

    /// @brief Exports the current board position as a standard FEN string.
    [[nodiscard]] std::string to_fen() const;

    /// @brief Plays a move on the board, updating all bitboards, state, and Zobrist hash.
    void make_move(const Move& m);

    /// @brief Reverts the most recently executed move using the state history stack.
    void undo_move();

    /// @brief Passes turn to the opponent without moving any piece (used for Null-Move Pruning).
    void make_null_move();

    /// @brief Reverts the null move.
    void undo_null_move();

    /// @brief Checks whether a side possesses non-pawn pieces (to avoid zugzwang risks in null-move pruning).
    [[nodiscard]] bool has_non_pawn_material(Color c) const noexcept {
        return (pieces_[c][KNIGHT] | pieces_[c][BISHOP] | pieces_[c][ROOK] | pieces_[c][QUEEN]) != 0;
    }

    /// @brief Determines whether a square is under attack by any piece of `attacker_color`.
    [[nodiscard]] bool is_square_attacked(Square sq, Color attacker_color) const noexcept;

    /// @brief Determines whether the current side to move is in check.
    [[nodiscard]] bool in_check() const noexcept;

    /// @brief Determines whether the specified player color is in check.
    [[nodiscard]] bool in_check(Color c) const noexcept;

    /// @brief Returns the square index of the king for the given color.
    [[nodiscard]] Square king_square(Color c) const noexcept;

    /// @brief Detects if the current position has occurred `count` times in the game history.
    [[nodiscard]] bool is_draw_by_repetition(int count = 2) const noexcept;

    /// @brief Checks if the 50-move rule condition is met (100 half-moves without pawn move or capture).
    [[nodiscard]] bool is_fifty_move_rule() const noexcept;

    /// @brief Checks for insufficient mating material (K vs K, KB vs K, KN vs K, etc.).
    [[nodiscard]] bool is_insufficient_material() const noexcept;

    // Bitboard accessors
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

    /// @brief Calculates the full 64-bit Zobrist key from scratch for validation.
    [[nodiscard]] uint64_t compute_zobrist() const noexcept;

private:
    void clear();
    void put_piece(Square sq, Piece p);
    void remove_piece(Square sq);

    std::array<std::array<Bitboard, 6>, 2> pieces_{}; // Bitboards for [color][piece_type]
    std::array<Bitboard, 2> occ_{};                   // Combined occupancy per color
    Bitboard all_occ_ = 0;                            // Total board occupancy
    std::array<Piece, 64> board_{};                   // 64-square mailbox array for fast square lookups

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
