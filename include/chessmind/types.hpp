#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace chessmind {

using Bitboard = uint64_t;

enum Color : uint8_t {
    WHITE = 0,
    BLACK = 1,
    NO_COLOR = 2
};

[[nodiscard]] constexpr Color operator~(Color c) noexcept {
    return static_cast<Color>(c ^ 1);
}

enum PieceType : uint8_t {
    PAWN = 0,
    KNIGHT = 1,
    BISHOP = 2,
    ROOK = 3,
    QUEEN = 4,
    KING = 5,
    NONE = 6
};

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

[[nodiscard]] constexpr Piece make_piece(Color c, PieceType pt) noexcept {
    return static_cast<Piece>((c == WHITE ? 0 : 6) + pt);
}

[[nodiscard]] constexpr PieceType type_of(Piece p) noexcept {
    return (p == EMPTY) ? PieceType::NONE : static_cast<PieceType>(p % 6);
}

[[nodiscard]] constexpr Color color_of(Piece p) noexcept {
    return (p == EMPTY) ? Color::NO_COLOR : static_cast<Color>(p / 6);
}

// 0 = a1, 7 = h1, ..., 56 = a8, 63 = h8, 64 = NO_SQ
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

[[nodiscard]] constexpr Square make_square(int file, int rank) noexcept {
    return static_cast<Square>(rank * 8 + file);
}

[[nodiscard]] constexpr int square_file(Square sq) noexcept {
    return sq % 8;
}

[[nodiscard]] constexpr int square_rank(Square sq) noexcept {
    return sq / 8;
}

enum CastlingRights : uint8_t {
    NO_CASTLING = 0,
    WHITE_OO = 1,
    WHITE_OOO = 2,
    BLACK_OO = 4,
    BLACK_OOO = 8,
    WHITE_CASTLING = WHITE_OO | WHITE_OOO,
    BLACK_CASTLING = BLACK_OO | BLACK_OOO,
    ALL_CASTLING = WHITE_CASTLING | BLACK_CASTLING
};

enum MoveFlags : uint8_t {
    FLAG_QUIET = 0,
    FLAG_DOUBLE_PUSH = 1,
    FLAG_CASTLE = 2,
    FLAG_CAPTURE = 4,
    FLAG_EN_PASSANT = 8,
    FLAG_PROMOTION = 16
};

struct Move {
    Square from = SQ_NONE;
    Square to = SQ_NONE;
    PieceType promo = PieceType::NONE;
    uint8_t flags = FLAG_QUIET;

    constexpr Move() noexcept = default;
    constexpr Move(Square f, Square t, uint8_t fl = FLAG_QUIET, PieceType pr = PieceType::NONE) noexcept
        : from(f), to(t), promo(pr), flags(fl) {}

    [[nodiscard]] constexpr bool is_null() const noexcept {
        return from == SQ_NONE;
    }

    [[nodiscard]] constexpr bool is_capture() const noexcept {
        return (flags & FLAG_CAPTURE) != 0;
    }

    [[nodiscard]] constexpr bool is_en_passant() const noexcept {
        return (flags & FLAG_EN_PASSANT) != 0;
    }

    [[nodiscard]] constexpr bool is_promotion() const noexcept {
        return (flags & FLAG_PROMOTION) != 0;
    }

    [[nodiscard]] constexpr bool is_castling() const noexcept {
        return (flags & FLAG_CASTLE) != 0;
    }

    [[nodiscard]] constexpr bool operator==(const Move& other) const noexcept {
        return from == other.from && to == other.to && promo == other.promo && flags == other.flags;
    }

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

inline const Move NULL_MOVE{};

} // namespace chessmind

