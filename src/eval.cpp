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
// Independently designed positional heuristics following classical chess principles:
// - Central control & pawn advancement
// - Minor piece centralization and active diagonal/outpost placement
// - Rooks on central files and 7th rank
// - King shelter in middlegame vs. king activation in endgame
// -----------------------------------------------------------------

// Pawns: rank 2 starting structure, central advancement, promotion threat
constexpr int MG_PAWN_TABLE[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,  // Rank 1
      0,   0,   0, -10, -10,   0,   0,   0,  // Rank 2
      5,  10,  15,  20,  20,  15,  10,   5,  // Rank 3
     10,  15,  25,  35,  35,  25,  15,  10,  // Rank 4
     15,  20,  30,  40,  40,  30,  20,  15,  // Rank 5
     25,  30,  40,  50,  50,  40,  30,  25,  // Rank 6
     60,  70,  80,  90,  90,  80,  70,  60,  // Rank 7
      0,   0,   0,   0,   0,   0,   0,   0   // Rank 8
};

constexpr int EG_PAWN_TABLE[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,  // Rank 1
      0,   0,   0,   0,   0,   0,   0,   0,  // Rank 2
     10,  10,  15,  15,  15,  15,  10,  10,  // Rank 3
     25,  25,  30,  35,  35,  30,  25,  25,  // Rank 4
     50,  50,  55,  60,  60,  55,  50,  50,  // Rank 5
     80,  85,  90,  95,  95,  90,  85,  80,  // Rank 6
    120, 130, 140, 145, 145, 140, 130, 120,  // Rank 7
      0,   0,   0,   0,   0,   0,   0,   0   // Rank 8
};

// Knights: central placement, penalize rim and awkward back rank
constexpr int MG_KNIGHT_TABLE[64] = {
    -40, -20, -10, -10, -10, -10, -20, -40,  // Rank 1
    -20, -10,   5,   5,   5,   5, -10, -20,  // Rank 2
    -10,   5,  15,  20,  20,  15,   5, -10,  // Rank 3
    -10,  10,  25,  30,  30,  25,  10, -10,  // Rank 4
    -10,  10,  25,  30,  30,  25,  10, -10,  // Rank 5
    -10,   5,  15,  20,  20,  15,   5, -10,  // Rank 6
    -20, -10,   0,   5,   5,   0, -10, -20,  // Rank 7
    -40, -20, -10, -10, -10, -10, -20, -40   // Rank 8
};

constexpr int EG_KNIGHT_TABLE[64] = {
    -30, -15, -10, -10, -10, -10, -15, -30,  // Rank 1
    -15,  -5,   5,   5,   5,   5,  -5, -15,  // Rank 2
    -10,   5,  15,  15,  15,  15,   5, -10,  // Rank 3
    -10,  10,  20,  25,  25,  20,  10, -10,  // Rank 4
    -10,  10,  20,  25,  25,  20,  10, -10,  // Rank 5
    -10,   5,  15,  15,  15,  15,   5, -10,  // Rank 6
    -15,  -5,   5,   5,   5,   5,  -5, -15,  // Rank 7
    -30, -15, -10, -10, -10, -10, -15, -30   // Rank 8
};

// Bishops: long diagonals, fianchetto, center control
constexpr int MG_BISHOP_TABLE[64] = {
    -15, -10, -10, -10, -10, -10, -10, -15,  // Rank 1
    -10,  10,   5,   5,   5,   5,  10, -10,  // Rank 2
     -5,   5,  10,  15,  15,  10,   5,  -5,  // Rank 3
     -5,  10,  15,  20,  20,  15,  10,  -5,  // Rank 4
     -5,  10,  15,  20,  20,  15,  10,  -5,  // Rank 5
     -5,   5,  10,  15,  15,  10,   5,  -5,  // Rank 6
    -10,   0,   5,   5,   5,   5,   0, -10,  // Rank 7
    -15, -10, -10, -10, -10, -10, -10, -15   // Rank 8
};

constexpr int EG_BISHOP_TABLE[64] = {
    -10, -10, -10, -10, -10, -10, -10, -10,  // Rank 1
    -10,   0,   0,   0,   0,   0,   0, -10,  // Rank 2
    -10,   5,  10,  10,  10,  10,   5, -10,  // Rank 3
    -10,   5,  15,  20,  20,  15,   5, -10,  // Rank 4
    -10,   5,  15,  20,  20,  15,   5, -10,  // Rank 5
    -10,   5,  10,  10,  10,  10,   5, -10,  // Rank 6
    -10,   0,   0,   0,   0,   0,   0, -10,  // Rank 7
    -10, -10, -10, -10, -10, -10, -10, -10   // Rank 8
};

// Rooks: open files (d, e files) and 7th rank pressure
constexpr int MG_ROOK_TABLE[64] = {
      0,   0,   5,  15,  15,   5,   0,   0,  // Rank 1
     -5,   0,   0,   5,   5,   0,   0,  -5,  // Rank 2
     -5,   0,   0,   5,   5,   0,   0,  -5,  // Rank 3
     -5,   0,   5,  10,  10,   5,   0,  -5,  // Rank 4
     -5,   0,   5,  10,  10,   5,   0,  -5,  // Rank 5
      0,   5,   5,  10,  10,   5,   5,   0,  // Rank 6
     25,  30,  30,  35,  35,  30,  30,  25,  // Rank 7
      5,  10,  10,  15,  15,  10,  10,   5   // Rank 8
};

constexpr int EG_ROOK_TABLE[64] = {
      0,   0,   5,  10,  10,   5,   0,   0,  // Rank 1
      0,   5,   5,  10,  10,   5,   5,   0,  // Rank 2
      0,   5,  10,  15,  15,  10,   5,   0,  // Rank 3
      5,  10,  15,  20,  20,  15,  10,   5,  // Rank 4
      5,  10,  15,  20,  20,  15,  10,   5,  // Rank 5
     10,  15,  20,  25,  25,  20,  15,  10,  // Rank 6
     30,  35,  35,  40,  40,  35,  35,  30,  // Rank 7
     10,  15,  15,  20,  20,  15,  15,  10   // Rank 8
};

// Queens: centralized active piece, cautious in early opening
constexpr int MG_QUEEN_TABLE[64] = {
    -10,  -5,   0,   5,   5,   0,  -5, -10,  // Rank 1
     -5,   0,   5,  10,  10,   5,   0,  -5,  // Rank 2
     -5,   5,  10,  15,  15,  10,   5,  -5,  // Rank 3
     -5,   5,  10,  15,  15,  10,   5,  -5,  // Rank 4
     -5,   5,  10,  15,  15,  10,   5,  -5,  // Rank 5
     -5,   0,   5,  10,  10,   5,   0,  -5,  // Rank 6
    -10,  -5,   0,   5,   5,   0,  -5, -10,  // Rank 7
    -15, -10,  -5,   0,   0,  -5, -10, -15   // Rank 8
};

constexpr int EG_QUEEN_TABLE[64] = {
    -15, -10, -10, -10, -10, -10, -10, -15,  // Rank 1
    -10,   0,   5,   5,   5,   5,   0, -10,  // Rank 2
    -10,   5,  15,  20,  20,  15,   5, -10,  // Rank 3
    -10,   5,  20,  30,  30,  20,   5, -10,  // Rank 4
    -10,   5,  20,  30,  30,  20,   5, -10,  // Rank 5
    -10,   5,  15,  20,  20,  15,   5, -10,  // Rank 6
    -10,   0,   5,   5,   5,   5,   0, -10,  // Rank 7
    -15, -10, -10, -10, -10, -10, -10, -15   // Rank 8
};

// Kings: shelter in MG (castled corners g1/h1, c1/b1), active center march in EG
constexpr int MG_KING_TABLE[64] = {
     15,  30,  25,  -5, -10,   0,  35,  25,  // Rank 1
      5,  10,   5, -15, -20, -10,  10,   5,  // Rank 2
    -10, -15, -20, -30, -30, -20, -15, -10,  // Rank 3
    -30, -35, -40, -50, -50, -40, -35, -30,  // Rank 4
    -40, -45, -50, -60, -60, -50, -45, -40,  // Rank 5
    -50, -55, -60, -70, -70, -60, -55, -50,  // Rank 6
    -60, -65, -70, -80, -80, -70, -65, -60,  // Rank 7
    -70, -75, -80, -90, -90, -80, -75, -70   // Rank 8
};

constexpr int EG_KING_TABLE[64] = {
    -40, -25, -20, -20, -20, -20, -25, -40,  // Rank 1
    -25, -10,   0,   0,   0,   0, -10, -25,  // Rank 2
    -20,   0,  15,  20,  20,  15,   0, -20,  // Rank 3
    -20,   0,  25,  35,  35,  25,   0, -20,  // Rank 4
    -20,   0,  25,  35,  35,  25,   0, -20,  // Rank 5
    -20,   0,  15,  20,  20,  15,   0, -20,  // Rank 6
    -25, -10,   0,   0,   0,   0, -10, -25,  // Rank 7
    -40, -25, -20, -20, -20, -20, -25, -40   // Rank 8
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
    Bitboard w_passed_bb = 0;
    Bitboard b_passed_bb = 0;

    Square w_king_sq_eg = board.king_square(WHITE);
    int w_kf = square_file(w_king_sq_eg);
    int w_kr = square_rank(w_king_sq_eg);

    Square b_king_sq_eg = board.king_square(BLACK);
    int b_kf = square_file(b_king_sq_eg);
    int b_kr = square_rank(b_king_sq_eg);

    Bitboard w_rooks_all = board.piece_bb(WHITE, ROOK);
    Bitboard b_rooks_all = board.piece_bb(BLACK, ROOK);

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
            w_passed_bb |= square_bb(sq);
            int bonus = 15 + r * 10;
            mg_score += bonus;
            eg_score += bonus * 2;

            // Protected passed pawn bonus (defended by friendly pawn)
            if (bitboards::pawn_attacks(BLACK, sq) & w_pawns) {
                mg_score += 15;
                eg_score += 25;
            }

            // Tarrasch rule: Rooks behind passed pawns
            Bitboard behind_file = (bitboards::FILE_A << f) & ((1ULL << sq) - 1);
            if (w_rooks_all & behind_file) {
                mg_score += 15;
                eg_score += 25;
            }
            if (b_rooks_all & behind_file) {
                mg_score -= 15;
                eg_score -= 20;
            }

            // Endgame King support / interception proximity
            if (game_phase < 16) {
                int w_dist_friendly = std::max(std::abs(w_kf - f), std::abs(w_kr - r));
                int b_dist_enemy = std::max(std::abs(b_kf - f), std::abs(b_kr - r));
                eg_score += (b_dist_enemy - w_dist_friendly) * 4;
            }
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
            b_passed_bb |= square_bb(sq);
            int bonus = 15 + (7 - r) * 10;
            mg_score -= bonus;
            eg_score -= bonus * 2;

            // Protected passed pawn bonus
            if (bitboards::pawn_attacks(WHITE, sq) & b_pawns) {
                mg_score -= 15;
                eg_score -= 25;
            }

            // Tarrasch rule: Rooks behind passed pawns
            Bitboard behind_file = (bitboards::FILE_A << f) & ~((1ULL << (sq + 1)) - 1);
            if (b_rooks_all & behind_file) {
                mg_score -= 15;
                eg_score -= 25;
            }
            if (w_rooks_all & behind_file) {
                mg_score += 15;
                eg_score += 20;
            }

            // Endgame King support / interception proximity
            if (game_phase < 16) {
                int b_dist_friendly = std::max(std::abs(b_kf - f), std::abs(b_kr - r));
                int w_dist_enemy = std::max(std::abs(w_kf - f), std::abs(w_kr - r));
                eg_score -= (w_dist_enemy - b_dist_friendly) * 4;
            }
        }
    }

    // Connected passed pawns bonus
    Bitboard temp_wp_passed = w_passed_bb;
    while (temp_wp_passed) {
        Square sq = pop_lsb(temp_wp_passed);
        int f = square_file(sq);
        Bitboard adj = 0;
        if (f > 0) adj |= (bitboards::FILE_A << (f - 1));
        if (f < 7) adj |= (bitboards::FILE_A << (f + 1));
        if (w_passed_bb & adj) {
            mg_score += 15;
            eg_score += 25;
        }
    }

    Bitboard temp_bp_passed = b_passed_bb;
    while (temp_bp_passed) {
        Square sq = pop_lsb(temp_bp_passed);
        int f = square_file(sq);
        Bitboard adj = 0;
        if (f > 0) adj |= (bitboards::FILE_A << (f - 1));
        if (f < 7) adj |= (bitboards::FILE_A << (f + 1));
        if (b_passed_bb & adj) {
            mg_score -= 15;
            eg_score -= 25;
        }
    }

    // Central Pawn Phalanxes (pawns side-by-side controlling the center)
    Bitboard w_phalanx = w_pawns & ((w_pawns & ~bitboards::FILE_H) << 1) & (bitboards::RANK_4 | bitboards::RANK_5 | bitboards::RANK_6);
    if (w_phalanx) {
        mg_score += pop_count(w_phalanx) * 12;
    }
    Bitboard b_phalanx = b_pawns & ((b_pawns & ~bitboards::FILE_H) << 1) & (bitboards::RANK_5 | bitboards::RANK_4 | bitboards::RANK_3);
    if (b_phalanx) {
        mg_score -= pop_count(b_phalanx) * 12;
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

    // Bishops mobility & Bad Bishop evaluation
    constexpr Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;

    Bitboard w_bishops = board.piece_bb(WHITE, BISHOP);
    while (w_bishops) {
        Square sq = pop_lsb(w_bishops);
        Bitboard mob = bitboards::bishop_attacks(sq, all_occ) & ~board.color_bb(WHITE) & ~b_pawn_attacks;
        int count = pop_count(mob);
        mg_score += count * 4;
        eg_score += count * 4;

        // Bad Bishop penalty: penalized if blocked by 4 or more friendly pawns on the same color squares
        bool is_light = test_bit(LIGHT_SQUARES, sq);
        int pawns_on_color = pop_count(w_pawns & (is_light ? LIGHT_SQUARES : ~LIGHT_SQUARES));
        if (pawns_on_color >= 4) {
            mg_score -= (pawns_on_color - 3) * 6;
            eg_score -= (pawns_on_color - 3) * 8;
        }
    }

    Bitboard b_bishops = board.piece_bb(BLACK, BISHOP);
    while (b_bishops) {
        Square sq = pop_lsb(b_bishops);
        Bitboard mob = bitboards::bishop_attacks(sq, all_occ) & ~board.color_bb(BLACK) & ~w_pawn_attacks;
        int count = pop_count(mob);
        mg_score -= count * 4;
        eg_score -= count * 4;

        bool is_light = test_bit(LIGHT_SQUARES, sq);
        int pawns_on_color = pop_count(b_pawns & (is_light ? LIGHT_SQUARES : ~LIGHT_SQUARES));
        if (pawns_on_color >= 4) {
            mg_score += (pawns_on_color - 3) * 6;
            eg_score += (pawns_on_color - 3) * 8;
        }
    }

    // Exchange Safety: Non-pawn pieces attacked by enemy pawns (tactical vulnerability penalty)
    Bitboard w_minors_majors = board.piece_bb(WHITE, KNIGHT) | board.piece_bb(WHITE, BISHOP) | board.piece_bb(WHITE, ROOK) | board.piece_bb(WHITE, QUEEN);
    Bitboard w_threatened = w_minors_majors & b_pawn_attacks;
    if (w_threatened) {
        mg_score -= pop_count(w_threatened) * 20;
        eg_score -= pop_count(w_threatened) * 15;
    }

    Bitboard b_minors_majors = board.piece_bb(BLACK, KNIGHT) | board.piece_bb(BLACK, BISHOP) | board.piece_bb(BLACK, ROOK) | board.piece_bb(BLACK, QUEEN);
    Bitboard b_threatened = b_minors_majors & w_pawn_attacks;
    if (b_threatened) {
        mg_score += pop_count(b_threatened) * 20;
        eg_score += pop_count(b_threatened) * 15;
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
