#include "chessmind/bitboard.hpp"
#include <array>

namespace chessmind::bitboards {

namespace {

// Lookup tables for non-sliding piece attacks
std::array<std::array<Bitboard, 64>, 2> PAWN_ATTACK_TABLE{};
std::array<Bitboard, 64> KNIGHT_ATTACK_TABLE{};
std::array<Bitboard, 64> KING_ATTACK_TABLE{};
bool initialized = false;

/// @brief Precomputes pawn capture attack rays for both White and Black on all 64 squares.
void init_pawn_attacks() {
    for (int sq = 0; sq < 64; ++sq) {
        int f = square_file(static_cast<Square>(sq));
        int r = square_rank(static_cast<Square>(sq));

        // White pawns attack diagonally upward (r + 1)
        Bitboard w_att = 0;
        if (r < 7) {
            if (f > 0) w_att |= square_bb(make_square(f - 1, r + 1));
            if (f < 7) w_att |= square_bb(make_square(f + 1, r + 1));
        }
        PAWN_ATTACK_TABLE[WHITE][sq] = w_att;

        // Black pawns attack diagonally downward (r - 1)
        Bitboard b_att = 0;
        if (r > 0) {
            if (f > 0) b_att |= square_bb(make_square(f - 1, r - 1));
            if (f < 7) b_att |= square_bb(make_square(f + 1, r - 1));
        }
        PAWN_ATTACK_TABLE[BLACK][sq] = b_att;
    }
}

/// @brief Precomputes knight leaping attacks for all 8 possible L-shaped jumps.
void init_knight_attacks() {
    constexpr int offsets[8][2] = {
        {1, 2}, {2, 1}, {2, -1}, {1, -2},
        {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}
    };

    for (int sq = 0; sq < 64; ++sq) {
        int f = square_file(static_cast<Square>(sq));
        int r = square_rank(static_cast<Square>(sq));
        Bitboard att = 0;

        for (auto [df, dr] : offsets) {
            int nf = f + df;
            int nr = r + dr;
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                att |= square_bb(make_square(nf, nr));
            }
        }
        KNIGHT_ATTACK_TABLE[sq] = att;
    }
}

/// @brief Precomputes king attacks for all 8 adjacent squares.
void init_king_attacks() {
    constexpr int offsets[8][2] = {
        {0, 1}, {1, 1}, {1, 0}, {1, -1},
        {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}
    };

    for (int sq = 0; sq < 64; ++sq) {
        int f = square_file(static_cast<Square>(sq));
        int r = square_rank(static_cast<Square>(sq));
        Bitboard att = 0;

        for (auto [df, dr] : offsets) {
            int nf = f + df;
            int nr = r + dr;
            if (nf >= 0 && nf < 8 && nr >= 0 && nr < 8) {
                att |= square_bb(make_square(nf, nr));
            }
        }
        KING_ATTACK_TABLE[sq] = att;
    }
}

} // namespace

/// @brief Global initializer ensuring lookup tables are populated once.
void init() {
    if (initialized) return;
    init_pawn_attacks();
    init_knight_attacks();
    init_king_attacks();
    initialized = true;
}

Bitboard pawn_attacks(Color c, Square sq) noexcept {
    return PAWN_ATTACK_TABLE[c][sq];
}

Bitboard knight_attacks(Square sq) noexcept {
    return KNIGHT_ATTACK_TABLE[sq];
}

Bitboard king_attacks(Square sq) noexcept {
    return KING_ATTACK_TABLE[sq];
}

/// @brief Ray-casts along 4 diagonal directions until the board edge or an occupied square is encountered.
Bitboard bishop_attacks(Square sq, Bitboard occ) noexcept {
    int f = square_file(sq);
    int r = square_rank(sq);
    Bitboard att = 0;

    constexpr int dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    for (auto [df, dr] : dirs) {
        int cf = f + df;
        int cr = r + dr;
        while (cf >= 0 && cf < 8 && cr >= 0 && cr < 8) {
            Square target = make_square(cf, cr);
            Bitboard bit = square_bb(target);
            att |= bit;
            if (occ & bit) break; // Ray is blocked by an intervening piece
            cf += df;
            cr += dr;
        }
    }
    return att;
}

/// @brief Ray-casts along 4 orthogonal directions (horizontal and vertical) until blocked.
Bitboard rook_attacks(Square sq, Bitboard occ) noexcept {
    int f = square_file(sq);
    int r = square_rank(sq);
    Bitboard att = 0;

    constexpr int dirs[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
    for (auto [df, dr] : dirs) {
        int cf = f + df;
        int cr = r + dr;
        while (cf >= 0 && cf < 8 && cr >= 0 && cr < 8) {
            Square target = make_square(cf, cr);
            Bitboard bit = square_bb(target);
            att |= bit;
            if (occ & bit) break; // Ray is blocked by an intervening piece
            cf += df;
            cr += dr;
        }
    }
    return att;
}

/// @brief Queen attacks are the mathematical union of diagonal and orthogonal rays.
Bitboard queen_attacks(Square sq, Bitboard occ) noexcept {
    return bishop_attacks(sq, occ) | rook_attacks(sq, occ);
}

} // namespace chessmind::bitboards
