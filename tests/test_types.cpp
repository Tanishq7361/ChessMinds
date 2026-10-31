#include "chessmind/types.hpp"
#include "chessmind/bitboard.hpp"
#include <iostream>
#include <cassert>
#include <string>

using namespace chessmind;

#define ASSERT_TRUE(expr) \
    do { \
        if (!(expr)) { \
            std::cerr << "Assertion FAILED: " << #expr << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "Assertion FAILED: " << #a << " == " << #b \
                      << " (" << (a) << " != " << (b) << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

bool test_color() {
    std::cout << "  Testing Color operations..." << std::endl;
    ASSERT_EQ(WHITE, 0);
    ASSERT_EQ(BLACK, 1);
    ASSERT_EQ(NO_COLOR, 2);

    ASSERT_EQ(~WHITE, BLACK);
    ASSERT_EQ(~BLACK, WHITE);
    ASSERT_EQ(~~WHITE, WHITE);
    return true;
}

bool test_pieces() {
    std::cout << "  Testing PieceType & Piece operations..." << std::endl;
    ASSERT_EQ(PAWN, 0);
    ASSERT_EQ(KNIGHT, 1);
    ASSERT_EQ(BISHOP, 2);
    ASSERT_EQ(ROOK, 3);
    ASSERT_EQ(QUEEN, 4);
    ASSERT_EQ(KING, 5);
    ASSERT_EQ(NO_PIECE_TYPE, 6);

    ASSERT_EQ(make_piece(WHITE, PAWN), W_PAWN);
    ASSERT_EQ(make_piece(WHITE, KING), W_KING);
    ASSERT_EQ(make_piece(BLACK, QUEEN), B_QUEEN);
    ASSERT_EQ(make_piece(BLACK, KNIGHT), B_KNIGHT);

    ASSERT_EQ(type_of(W_PAWN), PAWN);
    ASSERT_EQ(type_of(B_QUEEN), QUEEN);
    ASSERT_EQ(type_of(W_KING), KING);
    ASSERT_EQ(type_of(B_ROOK), ROOK);
    ASSERT_EQ(type_of(EMPTY), NO_PIECE_TYPE);

    ASSERT_EQ(color_of(W_PAWN), WHITE);
    ASSERT_EQ(color_of(W_QUEEN), WHITE);
    ASSERT_EQ(color_of(B_KING), BLACK);
    ASSERT_EQ(color_of(B_BISHOP), BLACK);
    ASSERT_EQ(color_of(EMPTY), NO_COLOR);

    // FEN character conversions
    ASSERT_EQ(piece_to_char(W_PAWN), 'P');
    ASSERT_EQ(piece_to_char(W_KNIGHT), 'N');
    ASSERT_EQ(piece_to_char(W_BISHOP), 'B');
    ASSERT_EQ(piece_to_char(W_ROOK), 'R');
    ASSERT_EQ(piece_to_char(W_QUEEN), 'Q');
    ASSERT_EQ(piece_to_char(W_KING), 'K');

    ASSERT_EQ(piece_to_char(B_PAWN), 'p');
    ASSERT_EQ(piece_to_char(B_KNIGHT), 'n');
    ASSERT_EQ(piece_to_char(B_BISHOP), 'b');
    ASSERT_EQ(piece_to_char(B_ROOK), 'r');
    ASSERT_EQ(piece_to_char(B_QUEEN), 'q');
    ASSERT_EQ(piece_to_char(B_KING), 'k');
    ASSERT_EQ(piece_to_char(EMPTY), ' ');

    ASSERT_EQ(char_to_piece('P'), W_PAWN);
    ASSERT_EQ(char_to_piece('k'), B_KING);
    ASSERT_EQ(char_to_piece('q'), B_QUEEN);
    ASSERT_EQ(char_to_piece('R'), W_ROOK);
    ASSERT_EQ(char_to_piece('?'), EMPTY);

    return true;
}

bool test_squares() {
    std::cout << "  Testing Square & coordinate conversions..." << std::endl;
    ASSERT_EQ(SQ_A1, 0);
    ASSERT_EQ(SQ_B1, 1);
    ASSERT_EQ(SQ_H1, 7);
    ASSERT_EQ(SQ_A2, 8);
    ASSERT_EQ(SQ_D4, 27);
    ASSERT_EQ(SQ_E4, 28);
    ASSERT_EQ(SQ_A8, 56);
    ASSERT_EQ(SQ_H8, 63);
    ASSERT_EQ(SQ_COUNT, 64);

    ASSERT_EQ(make_square(0, 0), SQ_A1);
    ASSERT_EQ(make_square(4, 3), SQ_E4);
    ASSERT_EQ(make_square(7, 7), SQ_H8);

    ASSERT_EQ(square_file(SQ_A1), 0);
    ASSERT_EQ(square_file(SQ_E4), 4);
    ASSERT_EQ(square_file(SQ_H8), 7);

    ASSERT_EQ(square_rank(SQ_A1), 0);
    ASSERT_EQ(square_rank(SQ_E4), 3);
    ASSERT_EQ(square_rank(SQ_H8), 7);

    // Directional offsets
    ASSERT_EQ(static_cast<Square>(SQ_E4 + NORTH), SQ_E5);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + SOUTH), SQ_E3);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + EAST), SQ_F4);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + WEST), SQ_D4);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + NORTH_EAST), SQ_F5);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + NORTH_WEST), SQ_D5);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + SOUTH_EAST), SQ_F3);
    ASSERT_EQ(static_cast<Square>(SQ_E4 + SOUTH_WEST), SQ_D3);

    // String coordinates
    ASSERT_EQ(square_to_string(SQ_A1), "a1");
    ASSERT_EQ(square_to_string(SQ_E4), "e4");
    ASSERT_EQ(square_to_string(SQ_H8), "h8");
    ASSERT_EQ(square_to_string(SQ_NONE), "-");

    ASSERT_EQ(string_to_square("a1"), SQ_A1);
    ASSERT_EQ(string_to_square("e4"), SQ_E4);
    ASSERT_EQ(string_to_square("h8"), SQ_H8);
    ASSERT_EQ(string_to_square("invalid"), SQ_NONE);
    ASSERT_EQ(string_to_square("z9"), SQ_NONE);

    // Roundtrip for all 64 squares
    for (int sq = 0; sq < 64; ++sq) {
        Square s = static_cast<Square>(sq);
        std::string str = square_to_string(s);
        ASSERT_EQ(string_to_square(str), s);
    }

    return true;
}

bool test_moves() {
    std::cout << "  Testing Move representation & UCI formatting..." << std::endl;
    Move m1(SQ_E2, SQ_E4, Move::DOUBLE_PUSH);
    ASSERT_EQ(m1.from, SQ_E2);
    ASSERT_EQ(m1.to, SQ_E4);
    ASSERT_EQ(m1.from_sq(), SQ_E2);
    ASSERT_EQ(m1.to_sq(), SQ_E4);
    ASSERT_EQ(m1.flags, Move::DOUBLE_PUSH);
    ASSERT_EQ(m1.is_promotion(), false);
    ASSERT_EQ(m1.is_capture(), false);
    ASSERT_EQ(m1.is_castling(), false);
    ASSERT_EQ(m1.to_uci(), "e2e4");

    Move m2(SQ_E7, SQ_E8, Move::PROMOTION, QUEEN);
    ASSERT_EQ(m2.from, SQ_E7);
    ASSERT_EQ(m2.to, SQ_E8);
    ASSERT_TRUE(m2.is_promotion());
    ASSERT_EQ(m2.promotion_piece(), QUEEN);
    ASSERT_EQ(m2.to_uci(), "e7e8q");

    Move m_knight_promo(SQ_B7, SQ_B8, Move::PROMOTION, KNIGHT);
    ASSERT_TRUE(m_knight_promo.is_promotion());
    ASSERT_EQ(m_knight_promo.promotion_piece(), KNIGHT);
    ASSERT_EQ(m_knight_promo.to_uci(), "b7b8n");

    Move m_capture(SQ_E4, SQ_D5, Move::CAPTURE);
    ASSERT_TRUE(m_capture.is_capture());
    ASSERT_EQ(m_capture.to_uci(), "e4d5");

    Move m_castle(SQ_E1, SQ_G1, Move::CASTLE);
    ASSERT_TRUE(m_castle.is_castling());
    ASSERT_EQ(m_castle.to_uci(), "e1g1");

    Move null_m = Move::null_move();
    ASSERT_TRUE(null_m.is_null());
    ASSERT_EQ(null_m.to_uci(), "0000");

    ASSERT_TRUE(m1 == Move(SQ_E2, SQ_E4, Move::DOUBLE_PUSH));
    ASSERT_TRUE(m1 != m2);

    return true;
}

bool test_bitboards() {
    std::cout << "  Testing Bitboard manipulation & shifts..." << std::endl;
    // Square bitboards
    ASSERT_EQ(square_bb(SQ_A1), 1ULL);
    ASSERT_EQ(square_bb(SQ_B1), 2ULL);
    ASSERT_EQ(square_bb(SQ_H8), 1ULL << 63);

    // Popcount
    ASSERT_EQ(pop_count(0ULL), 0);
    ASSERT_EQ(pop_count(1ULL), 1);
    ASSERT_EQ(pop_count(0xFFULL), 8);
    ASSERT_EQ(pop_count(0x5555555555555555ULL), 32);
    ASSERT_EQ(pop_count(~0ULL), 64);

    // LSB & pop_lsb
    Bitboard b1 = (1ULL << SQ_E4) | (1ULL << SQ_H8);
    ASSERT_EQ(lsb(b1), SQ_E4);
    Square first = pop_lsb(b1);
    ASSERT_EQ(first, SQ_E4);
    ASSERT_EQ(b1, 1ULL << SQ_H8);
    ASSERT_EQ(lsb(b1), SQ_H8);
    Square second = pop_lsb(b1);
    ASSERT_EQ(second, SQ_H8);
    ASSERT_EQ(b1, 0ULL);

    // set_bit, clear_bit, test_bit
    Bitboard b2 = 0;
    ASSERT_FALSE(test_bit(b2, SQ_D4));
    set_bit(b2, SQ_D4);
    ASSERT_TRUE(test_bit(b2, SQ_D4));
    ASSERT_EQ(b2, square_bb(SQ_D4));
    clear_bit(b2, SQ_D4);
    ASSERT_FALSE(test_bit(b2, SQ_D4));
    ASSERT_EQ(b2, 0ULL);

    // more_than_one
    ASSERT_FALSE(more_than_one(0ULL));
    ASSERT_FALSE(more_than_one(1ULL << SQ_C3));
    ASSERT_TRUE(more_than_one((1ULL << SQ_A1) | (1ULL << SQ_A2)));

    // Directional bitboard shifts
    Bitboard e4_bb = square_bb(SQ_E4);
    ASSERT_EQ(bitboards::shift_north(e4_bb), square_bb(SQ_E5));
    ASSERT_EQ(bitboards::shift_south(e4_bb), square_bb(SQ_E3));
    ASSERT_EQ(bitboards::shift_east(e4_bb), square_bb(SQ_F4));
    ASSERT_EQ(bitboards::shift_west(e4_bb), square_bb(SQ_D4));
    ASSERT_EQ(bitboards::shift_north_east(e4_bb), square_bb(SQ_F5));
    ASSERT_EQ(bitboards::shift_north_west(e4_bb), square_bb(SQ_D5));
    ASSERT_EQ(bitboards::shift_south_east(e4_bb), square_bb(SQ_F3));
    ASSERT_EQ(bitboards::shift_south_west(e4_bb), square_bb(SQ_D3));

    // Boundary edge tests (no wrapping)
    Bitboard h4_bb = square_bb(SQ_H4);
    ASSERT_EQ(bitboards::shift_east(h4_bb), 0ULL);
    ASSERT_EQ(bitboards::shift_north_east(h4_bb), 0ULL);
    ASSERT_EQ(bitboards::shift_south_east(h4_bb), 0ULL);

    Bitboard a4_bb = square_bb(SQ_A4);
    ASSERT_EQ(bitboards::shift_west(a4_bb), 0ULL);
    ASSERT_EQ(bitboards::shift_north_west(a4_bb), 0ULL);
    ASSERT_EQ(bitboards::shift_south_west(a4_bb), 0ULL);

    return true;
}

bool test_attack_tables() {
    std::cout << "  Testing leaping attack tables (knight, king, pawn)..." << std::endl;
    bitboards::init();

    // Knight attacks on D4: 8 squares
    Bitboard d4_knights = bitboards::knight_attacks(SQ_D4);
    ASSERT_EQ(pop_count(d4_knights), 8);
    ASSERT_TRUE(test_bit(d4_knights, SQ_C2));
    ASSERT_TRUE(test_bit(d4_knights, SQ_E2));
    ASSERT_TRUE(test_bit(d4_knights, SQ_B3));
    ASSERT_TRUE(test_bit(d4_knights, SQ_F3));
    ASSERT_TRUE(test_bit(d4_knights, SQ_B5));
    ASSERT_TRUE(test_bit(d4_knights, SQ_F5));
    ASSERT_TRUE(test_bit(d4_knights, SQ_C6));
    ASSERT_TRUE(test_bit(d4_knights, SQ_E6));

    // Knight on corner A1: only 2 squares (B3, C2)
    Bitboard a1_knights = bitboards::knight_attacks(SQ_A1);
    ASSERT_EQ(pop_count(a1_knights), 2);
    ASSERT_TRUE(test_bit(a1_knights, SQ_B3));
    ASSERT_TRUE(test_bit(a1_knights, SQ_C2));

    // King attacks on D4: 8 squares
    Bitboard d4_king = bitboards::king_attacks(SQ_D4);
    ASSERT_EQ(pop_count(d4_king), 8);

    // King on corner A1: 3 squares (A2, B1, B2)
    Bitboard a1_king = bitboards::king_attacks(SQ_A1);
    ASSERT_EQ(pop_count(a1_king), 3);
    ASSERT_TRUE(test_bit(a1_king, SQ_A2));
    ASSERT_TRUE(test_bit(a1_king, SQ_B1));
    ASSERT_TRUE(test_bit(a1_king, SQ_B2));

    // Pawn attacks
    Bitboard w_pawn_d4 = bitboards::pawn_attacks(WHITE, SQ_D4);
    ASSERT_EQ(pop_count(w_pawn_d4), 2);
    ASSERT_TRUE(test_bit(w_pawn_d4, SQ_C5));
    ASSERT_TRUE(test_bit(w_pawn_d4, SQ_E5));

    Bitboard b_pawn_d4 = bitboards::pawn_attacks(BLACK, SQ_D4);
    ASSERT_EQ(pop_count(b_pawn_d4), 2);
    ASSERT_TRUE(test_bit(b_pawn_d4, SQ_C3));
    ASSERT_TRUE(test_bit(b_pawn_d4, SQ_E3));

    return true;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ChessMinds C++20 Foundational Types   " << std::endl;
    std::cout << "========================================" << std::endl;

    bool passed = true;
    passed &= test_color();
    passed &= test_pieces();
    passed &= test_squares();
    passed &= test_moves();
    passed &= test_bitboards();
    passed &= test_attack_tables();

    std::cout << "----------------------------------------" << std::endl;
    if (passed) {
        std::cout << "ALL FOUNDATIONAL TESTS PASSED! [100%]" << std::endl;
        return 0;
    } else {
        std::cerr << "SOME TESTS FAILED!" << std::endl;
        return 1;
    }
}
