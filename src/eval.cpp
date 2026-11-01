#include "chessmind/eval.hpp"
#include <algorithm>

namespace chessmind {

namespace {

// Piece values in centipawns (100 cp = 1 pawn)
constexpr int MG_PIECE_VALUES[6] = { 100, 320, 330, 500, 900, 0 }; // Middlegame
constexpr int EG_PIECE_VALUES[6] = { 100, 290, 330, 520, 950, 0 }; // Endgame

// Weights used to determine the phase of the game (0 = pure endgame, 24 = opening/middlegame)
constexpr int GAME_PHASE_WEIGHTS[6] = { 0, 1, 1, 2, 4, 0 };
constexpr int TOTAL_PHASE = 24;

// -----------------------------------------------------------------
// Piece-Square Tables (PST) in centipawns (White perspective, A1=0, H8=63)
// -----------------------------------------------------------------

// Pawns: encourage central occupation in MG, advancing/queening in EG
constexpr int MG_PAWN_TABLE[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
     98, 134,  61,  95,  68, 126,  34, -11,
     -6,   7,  26,  31,  65,  56,  25, -20,
    -14,  13,   6,  21,  23,  12,  17, -23,
    -27,  -2,  -5,  12,  17,   6,  10, -25,
    -26,  -4,  -4, -10,   3,   3,  33, -12,
    -35,  -1, -20, -23, -15,  24,  38, -22,
      0,   0,   0,   0,   0,   0,   0,   0
};

constexpr int EG_PAWN_TABLE[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,
    178, 173, 158, 134, 147, 132, 165, 187,
     94, 100,  85,  67,  56,  53,  82,  84,
     32,  24,  13,   5,  -2,   4,  17,  17,
     13,   9,  -3,  -7,  -7,  -8,   3,  -1,
      4,   7,  -6,   1,   0,  -5,  -1,  -8,
     13,   8,   8,  10,  13,   0,   2,  -7,
      0,   0,   0,   0,   0,   0,   0,   0
};

// Knights: reward central placement, penalize rim squares
constexpr int MG_KNIGHT_TABLE[64] = {
    -167, -89, -34, -49,  61, -97, -15, -107,
     -73, -41,  72,  36,  23,  62,   7,  -17,
     -47,  60,  37,  65,  84, 129,  73,   44,
      -9,  17,  19,  53,  37,  69,  18,   22,
     -13,   4,  16,  13,  28,  19,  21,   -8,
     -23,  -9,  12,  10,  19,  17,  25,  -16,
     -29, -53, -12,  -3,  -1,  18, -14,  -19,
    -105, -21, -58, -33, -17, -28, -19,  -23
};

constexpr int EG_KNIGHT_TABLE[64] = {
    -58, -38, -13, -28, -31, -27, -63, -99,
    -25,  -8, -25,  -2,  -9, -25, -24, -52,
    -24, -20,  10,   9,  -1,  -9, -19, -41,
    -17,   3,  22,  22,  22,  11,   8, -18,
    -18,  -6,  16,  25,  16,  17,   4, -18,
    -23,  -3,  -1,  15,  10,  -3, -20, -22,
    -42, -20, -10,  -5,  -2, -20, -23, -44,
    -29, -51, -23, -15, -22, -18, -50, -64
};

// Bishops: reward long diagonals and center control
constexpr int MG_BISHOP_TABLE[64] = {
    -29,   4, -82, -37, -25, -42,   7,  -8,
    -26,  16, -18, -13,  30,  59,  18, -47,
    -16,  37,  43,  40,  35,  50,  37,  -2,
     -4,   5,  19,  50,  37,  37,   7,  -2,
     -6,  13,  13,  26,  34,  12,  10,   4,
      0,  15,  15,  15,  14,  27,  18,  10,
      4,  15,  16,   0,   7,  21,  33,   1,
    -33,  -3, -14, -21, -13, -12, -39, -21
};

constexpr int EG_BISHOP_TABLE[64] = {
    -14, -21, -11,  -8,  -7,  -9, -17, -24,
     -8,  -4,   7, -12,  -3, -13,  -4, -14,
      2,  -8,   0,  -1,  -2,   6,   0,   4,
     -3,   9,  12,   9,  14,  10,   3,   2,
     -6,   3,  13,  19,   7,  10,  -3,  -9,
    -12,  -3,   8,  10,  13,   3,  -7, -15,
    -14, -18,  -7,  -1,   4,  -9, -15, -27,
    -23,  -9, -23,  -5,  -9, -16,  -5, -17
};

// Rooks: reward 7th rank and open files
constexpr int MG_ROOK_TABLE[64] = {
     32,  42,  32,  51,  63,   9,  31,  43,
     27,  32,  58,  62,  80,  67,  26,  44,
     -5,  19,  26,  36,  17,  45,  61,  16,
    -24, -11,   7,  26,  24,  35,  -8, -20,
    -36, -26, -12,  -1,   9,  -7,   6, -23,
    -45, -25, -16, -17,   3,   0,  -5, -33,
    -44, -16, -20,  -9,  -1,  11,  -6, -71,
    -19, -13,   1,  17,  16,   7, -37, -26
};

constexpr int EG_ROOK_TABLE[64] = {
     13,  10,  18,  15,  12,  12,   8,   5,
     11,  13,  13,  11,  -3,   3,   8,   3,
      7,   7,   7,   5,   4,  -3,  -5,  -3,
      4,   3,  13,   1,   2,   1,  -1,   2,
      3,   5,   8,   4,  -5,  -6,  -8, -11,
     -4,   0,  -5,  -1,  -7, -12,  -8, -16,
     -6,  -6,   0,   2,  -9,  -9, -11,  -3,
     -9,   2,   3,  -1,  -5, -13,   4, -20
};

// Queens: centralized active piece in middlegame
constexpr int MG_QUEEN_TABLE[64] = {
    -28,   0,  29,  12,  59,  44,  43,  45,
    -24, -39,  -5,   1, -16,  57,  28,  54,
    -18, -15,  10,  14,  18,  35,  34,  32,
     -4,   0,  -5,   9,  21,  38,  15,  20,
    -10,  -8,  -2,  -4,   0,  -5,  -8, -10,
    -14, -12, -10,  -7,  -5,   2,  -8,  -4,
    -18,  -7,  -4,  -3,  -4,  -5, -10, -17,
     -3, -11,   5,   7,  -4,  12,  -6, -20
};

constexpr int EG_QUEEN_TABLE[64] = {
     -9,  22,  22,  27,  27,  19,  10,  20,
    -17,  20,  32,  41,  58,  25,  30,   0,
    -20,   6,   9,  49,  47,  35,  19,   9,
      3,  22,  24,  45,  57,  40,  57,  36,
    -18,  28,  19,  47,  31,  34,  39,  18,
    -16, -27,  15,   6,   9,  17,  10,   5,
    -22, -23, -30, -16, -16, -23, -36, -32,
    -33, -28, -22, -43,  -5, -32, -20, -41
};

// Kings: shelter and safety in MG (castled corners), central active participation in EG
constexpr int MG_KING_TABLE[64] = {
    -65,  23,  16, -15, -56, -34,   2,  13,
     29,  -1, -20,  -7,  -8,  -4, -38, -29,
     -9,  24,   2, -16, -20,   6,  22, -22,
    -17, -20, -12, -27, -30, -25, -14, -36,
    -49,  -1, -27, -39, -46, -44, -33, -51,
    -14, -14, -22, -46, -44, -30, -15, -27,
      1,   7,  -8, -64, -43, -16,   9,   8,
    -15,  36,  12, -54,   8, -28,  24,  14
};

constexpr int EG_KING_TABLE[64] = {
    -74, -35, -18, -18, -11,  15,   4, -17,
    -12,  17,  14,  17,  17,  38,  23,  11,
     10,  17,  23,  15,  20,  45,  44,  13,
     -8,  22,  24,  27,  26,  33,  26,   3,
    -18,  -4,  21,  24,  27,  23,   9, -11,
    -19,  -3,  11,  21,  23,  16,   7,  -9,
    -27, -11,   4,  13,  14,   4,  -5, -17,
    -53, -34, -21, -11, -28, -14, -24, -43
};

/// @brief Mirrors a square vertically for Black's perspective across the center (rank 1 <-> rank 8).
[[nodiscard]] constexpr int mirror_sq(Square sq) noexcept {
    return sq ^ 56;
}

} // namespace

/// @brief Evaluates static board quality in centipawns. Positive indicates advantage for side_to_move.
int Evaluator::evaluate(const Board& board) noexcept {
    // Immediate draws score 0
    if (board.is_fifty_move_rule() || board.is_draw_by_repetition(2) || board.is_insufficient_material()) {
        return DRAW_SCORE;
    }

    int mg_score = 0;
    int eg_score = 0;
    int game_phase = 0;

    const int* mg_tables[6] = { MG_PAWN_TABLE, MG_KNIGHT_TABLE, MG_BISHOP_TABLE, MG_ROOK_TABLE, MG_QUEEN_TABLE, MG_KING_TABLE };
    const int* eg_tables[6] = { EG_PAWN_TABLE, EG_KNIGHT_TABLE, EG_BISHOP_TABLE, EG_ROOK_TABLE, EG_QUEEN_TABLE, EG_KING_TABLE };

    // 1. Material and Piece-Square Table evaluation for both colors
    for (int pt = PAWN; pt <= KING; ++pt) {
        // White pieces
        Bitboard w_bb = board.piece_bb(WHITE, static_cast<PieceType>(pt));
        while (w_bb) {
            Square sq = pop_lsb(w_bb);
            mg_score += MG_PIECE_VALUES[pt] + mg_tables[pt][sq];
            eg_score += EG_PIECE_VALUES[pt] + eg_tables[pt][sq];
            game_phase += GAME_PHASE_WEIGHTS[pt];
        }

        // Black pieces (using vertically mirrored square for symmetrical piece placement evaluation)
        Bitboard b_bb = board.piece_bb(BLACK, static_cast<PieceType>(pt));
        while (b_bb) {
            Square sq = pop_lsb(b_bb);
            int m_sq = mirror_sq(sq);
            mg_score -= (MG_PIECE_VALUES[pt] + mg_tables[pt][m_sq]);
            eg_score -= (EG_PIECE_VALUES[pt] + eg_tables[pt][m_sq]);
            game_phase += GAME_PHASE_WEIGHTS[pt];
        }
    }

    // 2. Bishop Pair Bonus (+30 cp for having both light and dark squared bishops)
    if (pop_count(board.piece_bb(WHITE, BISHOP)) >= 2) {
        mg_score += 30;
        eg_score += 30;
    }
    if (pop_count(board.piece_bb(BLACK, BISHOP)) >= 2) {
        mg_score -= 30;
        eg_score -= 30;
    }

    // 3. Pawn Structure Evaluation
    Bitboard w_pawns = board.piece_bb(WHITE, PAWN);
    Bitboard b_pawns = board.piece_bb(BLACK, PAWN);

    for (int f = 0; f < 8; ++f) {
        Bitboard file_mask = bitboards::FILE_A << f;
        Bitboard adj_mask = 0;
        if (f > 0) adj_mask |= (bitboards::FILE_A << (f - 1));
        if (f < 7) adj_mask |= (bitboards::FILE_A << (f + 1));

        // Doubled pawns penalty (-15 cp per stacked pawn on same file)
        int w_in_file = pop_count(w_pawns & file_mask);
        if (w_in_file > 1) {
            mg_score -= 15 * (w_in_file - 1);
            eg_score -= 15 * (w_in_file - 1);
        }
        int b_in_file = pop_count(b_pawns & file_mask);
        if (b_in_file > 1) {
            mg_score += 15 * (b_in_file - 1);
            eg_score += 15 * (b_in_file - 1);
        }

        // Isolated pawns penalty (-15 cp if no friendly pawns on adjacent files)
        if (w_in_file > 0 && !(w_pawns & adj_mask)) {
            mg_score -= 15;
            eg_score -= 15;
        }
        if (b_in_file > 0 && !(b_pawns & adj_mask)) {
            mg_score += 15;
            eg_score += 15;
        }
    }

    // Passed pawns bonus (+15 to +85 cp scaling with advance toward promotion)
    Bitboard wp = w_pawns;
    while (wp) {
        Square sq = pop_lsb(wp);
        int f = square_file(sq);
        int r = square_rank(sq);
        Bitboard front_span = 0;
        for (int rank_ahead = r + 1; rank_ahead < 8; ++rank_ahead) {
            front_span |= (bitboards::RANK_1 << (rank_ahead * 8));
        }
        Bitboard files_ahead = bitboards::FILE_A << f;
        if (f > 0) files_ahead |= (bitboards::FILE_A << (f - 1));
        if (f < 7) files_ahead |= (bitboards::FILE_A << (f + 1));

        // If no opposing pawns in front or on adjacent files, pawn is passed
        if (!(b_pawns & (front_span & files_ahead))) {
            int bonus = 15 + r * 10;
            mg_score += bonus;
            eg_score += bonus * 2;
        }
    }

    Bitboard bp = b_pawns;
    while (bp) {
        Square sq = pop_lsb(bp);
        int f = square_file(sq);
        int r = square_rank(sq);
        Bitboard front_span = 0;
        for (int rank_ahead = 0; rank_ahead < r; ++rank_ahead) {
            front_span |= (bitboards::RANK_1 << (rank_ahead * 8));
        }
        Bitboard files_ahead = bitboards::FILE_A << f;
        if (f > 0) files_ahead |= (bitboards::FILE_A << (f - 1));
        if (f < 7) files_ahead |= (bitboards::FILE_A << (f + 1));

        if (!(w_pawns & (front_span & files_ahead))) {
            int bonus = 15 + (7 - r) * 10;
            mg_score -= bonus;
            eg_score -= bonus * 2;
        }
    }

    // 4. Rook Activity: Open and Semi-Open Files
    Bitboard w_rooks = board.piece_bb(WHITE, ROOK);
    while (w_rooks) {
        Square sq = pop_lsb(w_rooks);
        int f = square_file(sq);
        Bitboard file_mask = bitboards::FILE_A << f;
        if (!(w_pawns & file_mask)) {
            if (!(b_pawns & file_mask)) {
                // Fully open file (no pawns at all)
                mg_score += 25;
                eg_score += 15;
            } else {
                // Semi-open file (only opponent pawns)
                mg_score += 15;
                eg_score += 10;
            }
        }
    }

    Bitboard b_rooks = board.piece_bb(BLACK, ROOK);
    while (b_rooks) {
        Square sq = pop_lsb(b_rooks);
        int f = square_file(sq);
        Bitboard file_mask = bitboards::FILE_A << f;
        if (!(b_pawns & file_mask)) {
            if (!(w_pawns & file_mask)) {
                // Fully open file (no pawns at all)
                mg_score -= 25;
                eg_score -= 15;
            } else {
                // Semi-open file (only opponent pawns)
                mg_score -= 15;
                eg_score -= 10;
            }
        }
    }

    // 5. King Safety: Pawn Shield Defenses in Middlegame
    // White King Shield
    Square w_king_sq = board.king_square(WHITE);
    int w_king_f = square_file(w_king_sq);
    int w_king_r = square_rank(w_king_sq);
    if (w_king_r <= 1) {
        if (w_king_f >= 5) {
            // Kingside castled (f, g, h files)
            for (int f = 5; f <= 7; ++f) {
                Bitboard file_pawns = w_pawns & (bitboards::FILE_A << f);
                if (!file_pawns) {
                    mg_score -= 20; // Open file directly into the castled king
                } else {
                    int lowest_pawn_rank = square_rank(lsb(file_pawns));
                    if (lowest_pawn_rank > 2) {
                        mg_score -= 10 * (lowest_pawn_rank - 1); // Advanced shield weakness
                    }
                }
            }
        } else if (w_king_f <= 2) {
            // Queenside castled (a, b, c files)
            for (int f = 0; f <= 2; ++f) {
                Bitboard file_pawns = w_pawns & (bitboards::FILE_A << f);
                if (!file_pawns) {
                    mg_score -= 15;
                }
            }
        }
    }

    // Black King Shield
    Square b_king_sq = board.king_square(BLACK);
    int b_king_f = square_file(b_king_sq);
    int b_king_r = square_rank(b_king_sq);
    if (b_king_r >= 6) {
        if (b_king_f >= 5) {
            // Kingside castled (f, g, h files)
            for (int f = 5; f <= 7; ++f) {
                Bitboard file_pawns = b_pawns & (bitboards::FILE_A << f);
                if (!file_pawns) {
                    mg_score += 20; // Open file directly into Black's castled king
                } else {
                    int highest_pawn_rank = square_rank(msb(file_pawns));
                    if (highest_pawn_rank < 5) {
                        mg_score += 10 * (6 - highest_pawn_rank); // Advanced shield weakness
                    }
                }
            }
        } else if (b_king_f <= 2) {
            // Queenside castled (a, b, c files)
            for (int f = 0; f <= 2; ++f) {
                Bitboard file_pawns = b_pawns & (bitboards::FILE_A << f);
                if (!file_pawns) {
                    mg_score += 15;
                }
            }
        }
    }

    // 6. Piece Mobility (Knights, Bishops, Rooks, Queens)
    Bitboard all_occ = board.all_occupied();
    Bitboard w_pawn_attacks = 0;
    Bitboard b_pawn_attacks = 0;
    Bitboard temp_wp = w_pawns;
    while (temp_wp) {
        Square sq = pop_lsb(temp_wp);
        w_pawn_attacks |= bitboards::pawn_attacks(WHITE, sq);
    }
    Bitboard temp_bp = b_pawns;
    while (temp_bp) {
        Square sq = pop_lsb(temp_bp);
        b_pawn_attacks |= bitboards::pawn_attacks(BLACK, sq);
    }

    // Knights mobility
    Bitboard w_knights = board.piece_bb(WHITE, KNIGHT);
    while (w_knights) {
        Square sq = pop_lsb(w_knights);
        Bitboard mob = bitboards::knight_attacks(sq) & ~board.color_bb(WHITE) & ~b_pawn_attacks;
        int count = pop_count(mob);
        mg_score += count * 4;
        eg_score += count * 4;

        // Knight outpost bonus: ranks 4-6, defended by friendly pawn, cannot be attacked by enemy pawn
        int r = square_rank(sq);
        if (r >= 3 && r <= 5) {
            Bitboard friendly_defenders = bitboards::pawn_attacks(BLACK, sq) & w_pawns;
            if (friendly_defenders) {
                int f = square_file(sq);
                Bitboard enemy_pawn_threat = 0;
                for (int rank_ahead = r + 1; rank_ahead < 8; ++rank_ahead) {
                    if (f > 0) enemy_pawn_threat |= (bitboards::FILE_A << (f - 1)) & (bitboards::RANK_1 << (rank_ahead * 8));
                    if (f < 7) enemy_pawn_threat |= (bitboards::FILE_A << (f + 1)) & (bitboards::RANK_1 << (rank_ahead * 8));
                }
                if (!(b_pawns & enemy_pawn_threat)) {
                    mg_score += 25;
                    eg_score += 15;
                }
            }
        }
    }

    Bitboard b_knights = board.piece_bb(BLACK, KNIGHT);
    while (b_knights) {
        Square sq = pop_lsb(b_knights);
        Bitboard mob = bitboards::knight_attacks(sq) & ~board.color_bb(BLACK) & ~w_pawn_attacks;
        int count = pop_count(mob);
        mg_score -= count * 4;
        eg_score -= count * 4;

        // Black Knight outpost: ranks 3-5 (indices 4 down to 2)
        int r = square_rank(sq);
        if (r >= 2 && r <= 4) {
            Bitboard friendly_defenders = bitboards::pawn_attacks(WHITE, sq) & b_pawns;
            if (friendly_defenders) {
                int f = square_file(sq);
                Bitboard enemy_pawn_threat = 0;
                for (int rank_ahead = 0; rank_ahead < r; ++rank_ahead) {
                    if (f > 0) enemy_pawn_threat |= (bitboards::FILE_A << (f - 1)) & (bitboards::RANK_1 << (rank_ahead * 8));
                    if (f < 7) enemy_pawn_threat |= (bitboards::FILE_A << (f + 1)) & (bitboards::RANK_1 << (rank_ahead * 8));
                }
                if (!(w_pawns & enemy_pawn_threat)) {
                    mg_score -= 25;
                    eg_score -= 15;
                }
            }
        }
    }

    // Bishops mobility
    Bitboard w_bishops = board.piece_bb(WHITE, BISHOP);
    while (w_bishops) {
        Square sq = pop_lsb(w_bishops);
        Bitboard mob = bitboards::bishop_attacks(sq, all_occ) & ~board.color_bb(WHITE) & ~b_pawn_attacks;
        int count = pop_count(mob);
        mg_score += count * 4;
        eg_score += count * 4;
    }

    Bitboard b_bishops = board.piece_bb(BLACK, BISHOP);
    while (b_bishops) {
        Square sq = pop_lsb(b_bishops);
        Bitboard mob = bitboards::bishop_attacks(sq, all_occ) & ~board.color_bb(BLACK) & ~w_pawn_attacks;
        int count = pop_count(mob);
        mg_score -= count * 4;
        eg_score -= count * 4;
    }

    // Rooks on 7th rank and connected rooks
    Bitboard w_r = board.piece_bb(WHITE, ROOK);
    if (pop_count(w_r) >= 2) {
        Square r1 = pop_lsb(w_r);
        Square r2 = pop_lsb(w_r);
        if ((square_rank(r1) == square_rank(r2) && !(bitboards::rook_attacks(r1, all_occ) & square_bb(r2))) == 0 &&
            (bitboards::rook_attacks(r1, all_occ) & square_bb(r2))) {
            mg_score += 15;
            eg_score += 10;
        }
    }
    w_r = board.piece_bb(WHITE, ROOK);
    while (w_r) {
        Square sq = pop_lsb(w_r);
        int r = square_rank(sq);
        if (r == 6) { // 7th rank
            if (square_rank(board.king_square(BLACK)) == 7 || (b_pawns & bitboards::RANK_7)) {
                mg_score += 30;
                eg_score += 40;
            }
        }
        Bitboard mob = bitboards::rook_attacks(sq, all_occ) & ~board.color_bb(WHITE);
        int count = pop_count(mob);
        mg_score += count * 2;
        eg_score += count * 2;
    }

    Bitboard b_r = board.piece_bb(BLACK, ROOK);
    if (pop_count(b_r) >= 2) {
        Square r1 = pop_lsb(b_r);
        Square r2 = pop_lsb(b_r);
        if (bitboards::rook_attacks(r1, all_occ) & square_bb(r2)) {
            mg_score -= 15;
            eg_score -= 10;
        }
    }
    b_r = board.piece_bb(BLACK, ROOK);
    while (b_r) {
        Square sq = pop_lsb(b_r);
        int r = square_rank(sq);
        if (r == 1) { // 2nd rank for Black
            if (square_rank(board.king_square(WHITE)) == 0 || (w_pawns & bitboards::RANK_2)) {
                mg_score -= 30;
                eg_score -= 40;
            }
        }
        Bitboard mob = bitboards::rook_attacks(sq, all_occ) & ~board.color_bb(BLACK);
        int count = pop_count(mob);
        mg_score -= count * 2;
        eg_score -= count * 2;
    }

    // Queens mobility
    Bitboard w_queens = board.piece_bb(WHITE, QUEEN);
    while (w_queens) {
        Square sq = pop_lsb(w_queens);
        Bitboard mob = bitboards::queen_attacks(sq, all_occ) & ~board.color_bb(WHITE);
        int count = pop_count(mob);
        mg_score += count * 1;
        eg_score += count * 2;
    }
    Bitboard b_queens = board.piece_bb(BLACK, QUEEN);
    while (b_queens) {
        Square sq = pop_lsb(b_queens);
        Bitboard mob = bitboards::queen_attacks(sq, all_occ) & ~board.color_bb(BLACK);
        int count = pop_count(mob);
        mg_score -= count * 1;
        eg_score -= count * 2;
    }

    // 7. King Ring Attack & Danger Evaluation (Middlegame King Pressure)
    if (game_phase >= 8) { // Only evaluate in Opening and Middlegame
        // White attacking Black's King
        Square b_ksq = board.king_square(BLACK);
        if (b_ksq != SQ_NONE) {
            Bitboard b_ring = bitboards::king_attacks(b_ksq);
            int attack_units = 0;
            int attackers_count = 0;

            // Knights
            Bitboard knights = board.piece_bb(WHITE, KNIGHT);
            while (knights) {
                Square sq = pop_lsb(knights);
                if (bitboards::knight_attacks(sq) & b_ring) {
                    attack_units += 2;
                    attackers_count++;
                }
            }
            // Bishops
            Bitboard bishops = board.piece_bb(WHITE, BISHOP);
            while (bishops) {
                Square sq = pop_lsb(bishops);
                if (bitboards::bishop_attacks(sq, all_occ) & b_ring) {
                    attack_units += 2;
                    attackers_count++;
                }
            }
            // Rooks
            Bitboard rooks = board.piece_bb(WHITE, ROOK);
            while (rooks) {
                Square sq = pop_lsb(rooks);
                if (bitboards::rook_attacks(sq, all_occ) & b_ring) {
                    attack_units += 3;
                    attackers_count++;
                }
            }
            // Queen
            Bitboard queens = board.piece_bb(WHITE, QUEEN);
            while (queens) {
                Square sq = pop_lsb(queens);
                if (bitboards::queen_attacks(sq, all_occ) & b_ring) {
                    attack_units += 4;
                    attackers_count++;
                }
            }

            if (attackers_count >= 2) {
                mg_score += attack_units * attack_units * 3;
            }
        }

        // Black attacking White's King
        Square w_ksq = board.king_square(WHITE);
        if (w_ksq != SQ_NONE) {
            Bitboard w_ring = bitboards::king_attacks(w_ksq);
            int attack_units = 0;
            int attackers_count = 0;

            Bitboard knights = board.piece_bb(BLACK, KNIGHT);
            while (knights) {
                Square sq = pop_lsb(knights);
                if (bitboards::knight_attacks(sq) & w_ring) {
                    attack_units += 2;
                    attackers_count++;
                }
            }
            Bitboard bishops = board.piece_bb(BLACK, BISHOP);
            while (bishops) {
                Square sq = pop_lsb(bishops);
                if (bitboards::bishop_attacks(sq, all_occ) & w_ring) {
                    attack_units += 2;
                    attackers_count++;
                }
            }
            Bitboard rooks = board.piece_bb(BLACK, ROOK);
            while (rooks) {
                Square sq = pop_lsb(rooks);
                if (bitboards::rook_attacks(sq, all_occ) & w_ring) {
                    attack_units += 3;
                    attackers_count++;
                }
            }
            Bitboard queens = board.piece_bb(BLACK, QUEEN);
            while (queens) {
                Square sq = pop_lsb(queens);
                if (bitboards::queen_attacks(sq, all_occ) & w_ring) {
                    attack_units += 4;
                    attackers_count++;
                }
            }

            if (attackers_count >= 2) {
                mg_score -= attack_units * attack_units * 3;
            }
        }
    }

    // 8. Opening Piece Development Incentives
    if (game_phase >= 20) {
        if (board.piece_at(SQ_B1) == W_KNIGHT) mg_score -= 15;
        if (board.piece_at(SQ_G1) == W_KNIGHT) mg_score -= 15;
        if (board.piece_at(SQ_C1) == W_BISHOP) mg_score -= 15;
        if (board.piece_at(SQ_F1) == W_BISHOP) mg_score -= 15;

        if (board.piece_at(SQ_B8) == B_KNIGHT) mg_score += 15;
        if (board.piece_at(SQ_G8) == B_KNIGHT) mg_score += 15;
        if (board.piece_at(SQ_C8) == B_BISHOP) mg_score += 15;
        if (board.piece_at(SQ_F8) == B_BISHOP) mg_score += 15;
    }

    // 9. Tempo Bonus: slight initiative bonus for side to move
    if (board.side_to_move() == WHITE) {
        mg_score += 10;
        eg_score += 5;
    } else {
        mg_score -= 10;
        eg_score -= 5;
    }

    // 10. Tapered Evaluation: Blend Middlegame and Endgame scores based on game phase
    int phase = std::clamp(game_phase, 0, TOTAL_PHASE);
    int eval = (mg_score * phase + eg_score * (TOTAL_PHASE - phase)) / TOTAL_PHASE;

    // Negamax convention: return score relative to the current player
    return (board.side_to_move() == WHITE) ? eval : -eval;
}

} // namespace chessmind
