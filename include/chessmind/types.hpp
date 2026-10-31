#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chessmind {

/// @brief 64-bit integer where each bit represents a square on the 8x8 chessboard.
/// Bit 0 corresponds to A1, and bit 63 corresponds to H8.
using Bitboard = uint64_t;

/// @brief Represents player color.
enum Color : uint8_t {
    WHITE = 0,
    BLACK = 1,
    NO_COLOR = 2
};

/// @brief Flips player color (White -> Black, Black -> White).
[[nodiscard]] constexpr Color operator~(Color c) noexcept {
    return static_cast<Color>(c ^ 1);
}

/// @brief Piece types without color association.
enum PieceType : uint8_t {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    NONE = 6,
    NO_PIECE_TYPE = 6
};

/// @brief Specific piece instances with both color and piece type encoded.
enum Piece : uint8_t {
    W_PAWN = 0,
    W_KNIGHT = 1,
    W_BISHOP = 2,
    W_ROOK = 3,
    W_QUEEN = 4,
    W_KING = 5,
    B_PAWN = 6,
    B_KNIGHT = 7,
    B_BISHOP = 8,
    B_ROOK = 9,
    B_QUEEN = 10,
    B_KING = 11,
    EMPTY = 12
};

/// @brief File indices (0 = File A, 7 = File H).
enum File : uint8_t {
    FILE_A_IDX = 0,
    FILE_B_IDX,
    FILE_C_IDX,
    FILE_D_IDX,
    FILE_E_IDX,
    FILE_F_IDX,
    FILE_G_IDX,
    FILE_H_IDX
};

/// @brief Rank indices (0 = Rank 1, 7 = Rank 8).
enum Rank : uint8_t {
    RANK_1_IDX = 0,
    RANK_2_IDX,
    RANK_3_IDX,
    RANK_4_IDX,
    RANK_5_IDX,
    RANK_6_IDX,
    RANK_7_IDX,
    RANK_8_IDX
};

/// @brief Directional board step offsets.
enum Direction : int8_t {
    NORTH = 8,
    SOUTH = -8,
    EAST = 1,
    WEST = -1,
    NORTH_EAST = 9,
    NORTH_WEST = 7,
    SOUTH_EAST = -7,
    SOUTH_WEST = -9
};

/// @brief Constructs a colored Piece from a Color and PieceType.
[[nodiscard]] constexpr Piece make_piece(Color c, PieceType pt) noexcept {
    return static_cast<Piece>((c == WHITE ? 0 : 6) + pt);
}

/// @brief Extracts the uncolored PieceType from a colored Piece.
[[nodiscard]] constexpr PieceType type_of(Piece p) noexcept {
    return (p == EMPTY) ? PieceType::NONE : static_cast<PieceType>(p % 6);
}

/// @brief Extracts the player Color of a colored Piece.
[[nodiscard]] constexpr Color color_of(Piece p) noexcept {
    return (p == EMPTY) ? Color::NO_COLOR : static_cast<Color>(p / 6);
}

/// @brief Converts a Piece to its standard FEN character representation.
[[nodiscard]] constexpr char piece_to_char(Piece p) noexcept {
    constexpr const char symbols[] = "PNBRQKpnbrqk ";
    return (p <= EMPTY) ? symbols[p] : ' ';
}

/// @brief Converts a standard FEN character into a Piece.
[[nodiscard]] constexpr Piece char_to_piece(char ch) noexcept {
    switch (ch) {
        case 'P': return W_PAWN;
        case 'N': return W_KNIGHT;
        case 'B': return W_BISHOP;
        case 'R': return W_ROOK;
        case 'Q': return W_QUEEN;
        case 'K': return W_KING;
        case 'p': return B_PAWN;
        case 'n': return B_KNIGHT;
        case 'b': return B_BISHOP;
        case 'r': return B_ROOK;
        case 'q': return B_QUEEN;
        case 'k': return B_KING;
        default:  return EMPTY;
    }
}

/// @brief Squares on the chessboard indexed 0 to 63 (Little-Endian Rank-File mapping: A1=0, H8=63).
enum Square : uint8_t {
    SQ_A1 = 0,  SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
    SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
    SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
    SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
    SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
    SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
    SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
    SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
    SQ_NONE = 64
};

/// @brief Total number of board squares.
constexpr int SQ_COUNT = 64;

/// @brief Step a square in a given direction.
[[nodiscard]] constexpr Square operator+(Square sq, Direction dir) noexcept {
    return static_cast<Square>(static_cast<int>(sq) + static_cast<int>(dir));
}

/// @brief Step a square in the opposite of a given direction.
[[nodiscard]] constexpr Square operator-(Square sq, Direction dir) noexcept {
    return static_cast<Square>(static_cast<int>(sq) - static_cast<int>(dir));
}

/// @brief Constructs a Square given file [0..7] and rank [0..7].
[[nodiscard]] constexpr Square make_square(int file, int rank) noexcept {
    return static_cast<Square>(rank * 8 + file);
}

/// @brief Returns the 0-indexed file [0..7] (A-H) of a square.
[[nodiscard]] constexpr int square_file(Square sq) noexcept {
    return sq % 8;
}

/// @brief Returns the 0-indexed rank [0..7] (1-8) of a square.
[[nodiscard]] constexpr int square_rank(Square sq) noexcept {
    return sq / 8;
}

/// @brief Converts a Square into coordinate string notation (e.g. SQ_E4 -> "e4").
[[nodiscard]] inline std::string square_to_string(Square sq) {
    if (sq >= SQ_NONE) return "-";
    std::string s;
    s += static_cast<char>('a' + square_file(sq));
    s += static_cast<char>('1' + square_rank(sq));
    return s;
}

/// @brief Parses a coordinate string (e.g. "e4") into a Square.
[[nodiscard]] inline Square string_to_square(std::string_view str) noexcept {
    if (str.length() < 2) return SQ_NONE;
    int f = str[0] - 'a';
    int r = str[1] - '1';
    if (f >= 0 && f < 8 && r >= 0 && r < 8) {
        return make_square(f, r);
    }
    return SQ_NONE;
}

/// @brief Bit flags representing castling availability.
enum CastlingRights : uint8_t {
    NO_CASTLING = 0,
    WHITE_OO = 1,        // White kingside (O-O)
    WHITE_OOO = 2,       // White queenside (O-O-O)
    BLACK_OO = 4,        // Black kingside (O-O)
    BLACK_OOO = 8,       // Black queenside (O-O-O)
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ALL_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};

/// @brief Move category flags used to distinguish special chess moves.
enum MoveFlags : uint8_t {
    FLAG_QUIET = 0,           // Non-capture, non-special move
    FLAG_DOUBLE_PUSH = 1,     // Pawn 2-square initial push
    FLAG_CASTLE = 2,          // Castling move
    FLAG_CAPTURE = 4,         // Piece capture
    FLAG_EN_PASSANT = 8,      // En passant pawn capture
    FLAG_PROMOTION = 16       // Pawn promotion
};

/// @brief Compact representation of a chess move.
struct Move {
    static constexpr uint8_t QUIET = FLAG_QUIET;
    static constexpr uint8_t DOUBLE_PUSH = FLAG_DOUBLE_PUSH;
    static constexpr uint8_t CASTLE = FLAG_CASTLE;
    static constexpr uint8_t CAPTURE = FLAG_CAPTURE;
    static constexpr uint8_t EN_PASSANT = FLAG_EN_PASSANT;
    static constexpr uint8_t PROMOTION = FLAG_PROMOTION;

    Square from = SQ_NONE;            // Origin square [0..63]
    Square to = SQ_NONE;              // Destination square [0..63]
    PieceType promo = PieceType::NONE;// Promotion piece type (Q, R, B, N)
    uint8_t flags = FLAG_QUIET;       // MoveFlags combination

    constexpr Move() noexcept = default;
    constexpr Move(Square f, Square t, uint8_t fl = FLAG_QUIET, PieceType pr = PieceType::NONE) noexcept
        : from(f), to(t), promo(pr), flags(fl) {}

    /// @brief Creates an empty / null move.
    [[nodiscard]] static constexpr Move null_move() noexcept {
        return Move{};
    }

    /// @brief Checks whether the move is uninitialized or null.
    [[nodiscard]] constexpr bool is_null() const noexcept {
        return from == SQ_NONE;
    }

    /// @brief Origin square.
    [[nodiscard]] constexpr Square from_sq() const noexcept {
        return from;
    }

    /// @brief Destination square.
    [[nodiscard]] constexpr Square to_sq() const noexcept {
        return to;
    }

    /// @brief Promotion piece type (or NONE).
    [[nodiscard]] constexpr PieceType promotion_piece() const noexcept {
        return promo;
    }

    /// @brief Checks whether the move is a capture.
    [[nodiscard]] constexpr bool is_capture() const noexcept {
        return (flags & FLAG_CAPTURE) != 0;
    }

    /// @brief Checks whether the move is an en passant capture.
    [[nodiscard]] constexpr bool is_en_passant() const noexcept {
        return (flags & FLAG_EN_PASSANT) != 0;
    }

    /// @brief Checks whether the move is a pawn promotion.
    [[nodiscard]] constexpr bool is_promotion() const noexcept {
        return (flags & FLAG_PROMOTION) != 0;
    }

    /// @brief Checks whether the move is castling.
    [[nodiscard]] constexpr bool is_castling() const noexcept {
        return (flags & FLAG_CASTLE) != 0;
    }

    /// @brief Encoded integer value of the move for fast hashing or comparison.
    [[nodiscard]] constexpr uint32_t raw() const noexcept {
        return static_cast<uint32_t>(from) |
              (static_cast<uint32_t>(to) << 8) |
              (static_cast<uint32_t>(promo) << 16) |
              (static_cast<uint32_t>(flags) << 24);
    }

    /// @brief Move equality operator.
    [[nodiscard]] constexpr bool operator==(const Move& other) const noexcept {
        return from == other.from && to == other.to && promo == other.promo && flags == other.flags;
    }

    /// @brief Move inequality operator.
    [[nodiscard]] constexpr bool operator!=(const Move& other) const noexcept {
        return !(*this == other);
    }

    /// @brief Converts the move to standard UCI string format (e.g. "e2e4", "e7e8q").
    [[nodiscard]] std::string to_uci() const {
        if (is_null()) return "0000";
        std::string s;
        s += static_cast<char>('a' + square_file(from));
        s += static_cast<char>('1' + square_rank(from));
        s += static_cast<char>('a' + square_file(to));
        s += static_cast<char>('1' + square_rank(to));
        if (is_promotion()) {
            switch (promo) {
                case KNIGHT: s += 'n'; break;
                case BISHOP: s += 'b'; break;
                case ROOK:   s += 'r'; break;
                case QUEEN:  s += 'q'; break;
                default: break;
            }
        }
        return s;
    }
};

/// @brief Represents a null move (used for TT probes and null-move concepts).
inline const Move NULL_MOVE{};

} // namespace chessmind
