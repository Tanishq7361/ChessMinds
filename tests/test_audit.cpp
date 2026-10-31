#include "chessmind/board.hpp"
#include "chessmind/movegen.hpp"
#include "chessmind/uci.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <algorithm>

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

// Helper: check if a UCI move string is in a list of legal moves
bool has_move(const std::vector<Move>& moves, std::string_view uci) {
    for (const auto& m : moves) {
        if (m.to_uci() == uci) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// 1. Side-to-move tracking and turn alternating
// -----------------------------------------------------------------------------
bool test_turn_management() {
    std::cout << "[TEST] 1. Side-to-move tracking and turn alternation..." << std::endl;
    Board board;
    board.reset();

    ASSERT_EQ(board.side_to_move(), WHITE);
    ASSERT_EQ(board.fullmove_number(), 1);

    // 1st move: White plays e2e4
    Move e2e4 = MoveGen::parse_uci_move(board, "e2e4");
    ASSERT_FALSE(e2e4.is_null());
    board.make_move(e2e4);

    ASSERT_EQ(board.side_to_move(), BLACK);
    ASSERT_EQ(board.fullmove_number(), 1);

    // 2nd move: Black plays e7e5
    Move e7e5 = MoveGen::parse_uci_move(board, "e7e5");
    ASSERT_FALSE(e7e5.is_null());
    board.make_move(e7e5);

    ASSERT_EQ(board.side_to_move(), WHITE);
    ASSERT_EQ(board.fullmove_number(), 2);

    // 3rd move: White plays g1f3
    Move g1f3 = MoveGen::parse_uci_move(board, "g1f3");
    ASSERT_FALSE(g1f3.is_null());
    board.make_move(g1f3);

    ASSERT_EQ(board.side_to_move(), BLACK);
    ASSERT_EQ(board.fullmove_number(), 2);

    // Undo 3rd move: returns to White to move
    board.undo_move();
    ASSERT_EQ(board.side_to_move(), WHITE);
    ASSERT_EQ(board.fullmove_number(), 2);

    // Undo 2nd move: returns to Black to move
    board.undo_move();
    ASSERT_EQ(board.side_to_move(), BLACK);
    ASSERT_EQ(board.fullmove_number(), 1);

    // Undo 1st move: returns to White to move at starting position
    board.undo_move();
    ASSERT_EQ(board.side_to_move(), WHITE);
    ASSERT_EQ(board.fullmove_number(), 1);

    // Test Null Move turn management
    board.make_null_move();
    ASSERT_EQ(board.side_to_move(), BLACK);
    board.undo_null_move();
    ASSERT_EQ(board.side_to_move(), WHITE);

    std::cout << "  --> PASS: Turns alternate strictly and undo correctly restores turn & ply.\n";
    return true;
}

// -----------------------------------------------------------------------------
// 2. Illegal moves rejection and board boundaries
// -----------------------------------------------------------------------------
bool test_illegal_move_rejection() {
    std::cout << "[TEST] 2. Illegal move rejection and boundary checks..." << std::endl;
    Board board;
    board.reset();

    // In startpos, White cannot move Black's pieces
    Move black_move = MoveGen::parse_uci_move(board, "e7e5");
    ASSERT_TRUE(black_move.is_null());

    // Pawns cannot move 3 squares
    Move pawn_3sq = MoveGen::parse_uci_move(board, "e2e5");
    ASSERT_TRUE(pawn_3sq.is_null());

    // Pawns cannot move backward
    Move pawn_back = MoveGen::parse_uci_move(board, "e2e1");
    ASSERT_TRUE(pawn_back.is_null());

    // Non-knights cannot jump over pieces (e.g. rook jumping pawn)
    Move rook_jump = MoveGen::parse_uci_move(board, "a1a3");
    ASSERT_TRUE(rook_jump.is_null());

    // King cannot jump or teleport
    Move king_teleport = MoveGen::parse_uci_move(board, "e1e3");
    ASSERT_TRUE(king_teleport.is_null());

    // Moving an empty square
    Move empty_sq = MoveGen::parse_uci_move(board, "e3e4");
    ASSERT_TRUE(empty_sq.is_null());

    // Knight boundary wrapping: Knight on H1 should not wrap around to A file
    // Precomputed knight attacks test on A1, H1:
    Bitboard a1_knights = bitboards::knight_attacks(SQ_A1);
    ASSERT_EQ(pop_count(a1_knights), 2); // only B3, C2
    Bitboard h1_knights = bitboards::knight_attacks(SQ_H1);
    ASSERT_EQ(pop_count(h1_knights), 2); // only G3, F2

    std::cout << "  --> PASS: Illegal moves, jumps, and off-board movements rejected.\n";
    return true;
}

// -----------------------------------------------------------------------------
// 3. King safety, pinned pieces, and check resolution
// -----------------------------------------------------------------------------
bool test_king_safety_and_pins() {
    std::cout << "[TEST] 3. King safety, pins, and check resolutions..." << std::endl;
    Board board;

    // Position: Absolute pin on White bishop by Black rook:
    // White King on e1, White Bishop on e4, Black Rook on e8.
    // Bishop CANNOT move off the e-file!
    board.set_fen("4r3/8/8/8/4B3/8/8/4K3 w - - 0 1");
    std::vector<Move> legal;
    MoveGen::generate_legal_moves(board, legal);

    // Bishop moves like e4d5, e4f5, e4c6, e4g6 must NOT be in legal moves!
    for (const auto& m : legal) {
        if (m.from == SQ_E4) {
            // If bishop moves, it must stay on the e-file (which bishops cannot do anyway!)
            ASSERT_TRUE(false); // Bishop has zero legal moves while pinned on orthogonal ray
        }
    }

    // Position: King in check by queen:
    // White King on e1, Black Queen on e8. White must escape check!
    board.set_fen("4q3/8/8/8/8/8/8/4K3 w - - 0 1");
    ASSERT_TRUE(board.in_check());
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);

    // All legal moves must result in king NOT being in check
    for (const auto& m : legal) {
        board.make_move(m);
        ASSERT_FALSE(board.in_check(WHITE));
        board.undo_move();
    }

    // Position: King cannot move into check:
    // White King on e1, Black Rook on d8 controlling d-file.
    // White King cannot move to d1 or d2.
    board.set_fen("3r4/8/8/8/8/8/8/4K3 w - - 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1d1"));
    ASSERT_FALSE(has_move(legal, "e1d2"));

    // King can NEVER be captured under any circumstances
    board.set_fen("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    for (const auto& m : legal) {
        ASSERT_FALSE(board.piece_at(m.to) == B_KING);
    }

    std::cout << "  --> PASS: King safety preserved, pins respected, moving into check forbidden.\n";
    return true;
}

// -----------------------------------------------------------------------------
// 4. Castling legality and checks
// -----------------------------------------------------------------------------
bool test_castling_legality() {
    std::cout << "[TEST] 4. Castling legality and invalid state handling..." << std::endl;
    Board board;

    // 1. Standard castling available: White can castle O-O (e1g1) and O-O-O (e1c1)
    board.set_fen("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
    std::vector<Move> legal;
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_TRUE(has_move(legal, "e1g1"));
    ASSERT_TRUE(has_move(legal, "e1c1"));

    // 2. Cannot castle while in check!
    // Black rook on e8 checking White king on e1
    board.set_fen("r3k2r/8/8/8/4r3/8/8/R3K2R w KQkq - 0 1");
    ASSERT_TRUE(board.in_check());
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1g1"));
    ASSERT_FALSE(has_move(legal, "e1c1"));

    // 3. Cannot castle through check!
    // Black rook on f8 attacking f1 (king passes through f1 to reach g1)
    board.set_fen("r3k2r/8/8/8/5r2/8/8/R3K2R w KQkq - 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1g1")); // f1 is attacked
    ASSERT_TRUE(has_move(legal, "e1c1"));  // queenside still legal

    // 4. Cannot castle into check!
    // Black rook on g8 attacking g1
    board.set_fen("r3k2r/8/8/8/6r1/8/8/R3K2R w KQkq - 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1g1")); // g1 is attacked

    // 5. Pieces intervening: bishop on f1 blocks kingside castling
    board.set_fen("r3k2r/8/8/8/8/8/8/R3KB1R w KQkq - 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1g1"));

    // 6. King not on home square E1 cannot castle even if FEN claimed 'K'
    board.set_fen("r3k2r/8/8/8/8/8/4K3/R6R w KQkq - 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1g1"));
    ASSERT_FALSE(has_move(legal, "e2g1"));

    // 7. Missing rook in corner blocks castling even if FEN claimed 'K'
    board.set_fen("r3k2r/8/8/8/8/8/8/R3K3 w KQkq - 0 1"); // No rook on H1
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "e1g1"));

    std::cout << "  --> PASS: All castling conditions and edge cases verified strictly.\n";
    return true;
}

// -----------------------------------------------------------------------------
// 5. En passant legality and en passant pin
// -----------------------------------------------------------------------------
bool test_en_passant_legality() {
    std::cout << "[TEST] 5. En passant legality and pin detection..." << std::endl;
    Board board;

    // 1. Standard valid en passant:
    // White pawn on e5, Black pawn pushed d7-d5, ep square is d6.
    board.set_fen("rnbqkbnr/ppp1pppp/8/3pP3/8/8/PPPP1PPP/RNBQKBNR w KQkq d6 0 2");
    std::vector<Move> legal;
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_TRUE(has_move(legal, "e5d6"));

    // Execute en passant move: verify captured pawn on d5 is removed
    Move ep = MoveGen::parse_uci_move(board, "e5d6");
    ASSERT_FALSE(ep.is_null());
    ASSERT_TRUE(ep.is_en_passant());
    board.make_move(ep);
    ASSERT_EQ(board.piece_at(SQ_D5), EMPTY);
    ASSERT_EQ(board.piece_at(SQ_D6), W_PAWN);

    // Undo en passant move: verify captured pawn restored on d5
    board.undo_move();
    ASSERT_EQ(board.piece_at(SQ_D5), B_PAWN);
    ASSERT_EQ(board.piece_at(SQ_E5), W_PAWN);
    ASSERT_EQ(board.piece_at(SQ_D6), EMPTY);

    // 2. En passant pin (Rank pin):
    // Black rook on a5, Black pawn on c5, White pawn on d5, White king on e5.
    // If White plays d5xc6 e.p., BOTH c5 and d5 vacate the 5th rank,
    // exposing White King on e5 to Black Rook on a5! ILLEGAL!
    board.set_fen("8/4k3/8/r1pPK3/8/8/8/8 w - c6 0 1");
    legal.clear();
    MoveGen::generate_legal_moves(board, legal);
    ASSERT_FALSE(has_move(legal, "d5c6")); // Must be rejected by pin!

    std::cout << "  --> PASS: En passant captures, undos, and pin safety verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// 6. UCI Move parsing and position command handling
// -----------------------------------------------------------------------------
bool test_uci_handling() {
    std::cout << "[TEST] 6. UCI position and move parsing..." << std::endl;
    Board board;
    board.reset();

    // Uppercase parsing
    Move m_upper = MoveGen::parse_uci_move(board, "E2E4");
    ASSERT_FALSE(m_upper.is_null());
    ASSERT_EQ(m_upper.to_uci(), "e2e4");

    // Whitespace and trailing newline
    Move m_spaces = MoveGen::parse_uci_move(board, "  e2e4\r\n");
    ASSERT_FALSE(m_spaces.is_null());
    ASSERT_EQ(m_spaces.to_uci(), "e2e4");

    // Promotion parsing
    board.set_fen("8/4P3/8/8/8/8/8/4K2k w - - 0 1");
    Move promo_q = MoveGen::parse_uci_move(board, "e7e8q");
    ASSERT_FALSE(promo_q.is_null());
    ASSERT_TRUE(promo_q.is_promotion());
    ASSERT_EQ(promo_q.promo, QUEEN);

    // Default promotion to queen if promo suffix is omitted
    Move promo_default = MoveGen::parse_uci_move(board, "e7e8");
    ASSERT_FALSE(promo_default.is_null());
    ASSERT_EQ(promo_default.promo, QUEEN);

    // Illegal UCI move rejection
    Move illegal_move = MoveGen::parse_uci_move(board, "e7e5"); // Pawn jumping 2 squares to empty square from rank 7
    ASSERT_TRUE(illegal_move.is_null());

    std::cout << "  --> PASS: UCI parsing robustness and casing handling verified.\n";
    return true;
}

// -----------------------------------------------------------------------------
// 7. Standard Perft Positions (Zero Regression Guarantee)
// -----------------------------------------------------------------------------
uint64_t perft(Board& board, int depth) {
    if (depth == 0) return 1ULL;
    std::vector<Move> moves;
    MoveGen::generate_legal_moves(board, moves);
    if (depth == 1) return moves.size();

    uint64_t nodes = 0;
    for (const auto& m : moves) {
        board.make_move(m);
        nodes += perft(board, depth - 1);
        board.undo_move();
    }
    return nodes;
}

bool test_perft_suite() {
    std::cout << "[TEST] 7. Standard Perft Suites..." << std::endl;
    Board board;
    board.reset();

    // Startpos: D1=20, D2=400, D3=8902, D4=197281
    ASSERT_EQ(perft(board, 1), 20ULL);
    ASSERT_EQ(perft(board, 2), 400ULL);
    ASSERT_EQ(perft(board, 3), 8902ULL);
    ASSERT_EQ(perft(board, 4), 197281ULL);

    // KiwiPete: D1=48, D2=2039, D3=97862
    board.set_fen("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
    ASSERT_EQ(perft(board, 1), 48ULL);
    ASSERT_EQ(perft(board, 2), 2039ULL);
    ASSERT_EQ(perft(board, 3), 97862ULL);

    std::cout << "  --> PASS: Standard Perft leaf node counts match 100% exactly.\n";
    return true;
}

int main() {
    std::cout << "========================================================\n"
              << "  ChessMinds Move Generation & Turn Management Audit    \n"
              << "========================================================\n";

    bool passed = true;
    passed &= test_turn_management();
    passed &= test_illegal_move_rejection();
    passed &= test_king_safety_and_pins();
    passed &= test_castling_legality();
    passed &= test_en_passant_legality();
    passed &= test_uci_handling();
    passed &= test_perft_suite();

    std::cout << "========================================================\n";
    if (passed) {
        std::cout << "AUDIT SUITE COMPLETE: ALL TESTS PASSED [100%]\n";
        return 0;
    } else {
        std::cerr << "AUDIT SUITE FAILED: ISSUES DETECTED!\n";
        return 1;
    }
}
