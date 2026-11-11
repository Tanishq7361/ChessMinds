#include "chessmind/board.hpp"
#include "chessmind/movegen.hpp"
#include "chessmind/search.hpp"
#include "chessmind/eval.hpp"
#include "chessmind/uci.hpp"
#include <iostream>
#include <cassert>
#include <random>
#include <sstream>

using namespace chessmind;

void test_tactical_regression() {
    std::cout << "[TEST] 1. Tactical Regression Suite (Tactical Motifs)...\n";
    TranspositionTable tt(16);
    Searcher searcher(tt);

    struct TacticalTest {
        const char* fen;
        const char* expected_move;
        const char* description;
    };

    TacticalTest tests[] = {
        {"r1bqkb1r/pppp1ppp/2n5/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 0 4", "h5f7", "Scholar's Mate in 1"},
        {"6k1/5ppp/8/8/8/8/4R3/4K3 w - - 0 1", "e2e8", "Back-Rank Mate in 1"},
        {"rnbqkbnr/ppppp2p/8/5pp1/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 3", "d1h5", "Fool's Mate in 1"},
        {"4k3/8/8/3b4/8/8/3R4/4K3 w - - 0 1", "d2d5", "Hanging Bishop Capture"},
        {"r2qkb1r/pp2nppp/3p4/2pNN1B1/2BnP3/3P4/PPP2PPP/R2bK2R w KQkq - 0 1", "d5f6", "Smothered Knight Strike"}
    };

    for (const auto& t : tests) {
        Board board;
        board.set_fen(t.fen);
        tt.clear();
        searcher.reset();

        SearchLimits limits;
        limits.max_depth = 5;
        Move best = searcher.start_search(board, limits);

        if (best.to_uci() == t.expected_move) {
            std::cout << "  --> PASS: " << t.description << " found move " << best.to_uci() << "\n";
        } else {
            std::cerr << "  --> FAIL: " << t.description << " expected " << t.expected_move 
                      << " but got " << best.to_uci() << "\n";
            assert(false);
        }
    }
}

void test_random_walk_legality() {
    std::cout << "[TEST] 2. Random-Position Legality & Zobrist Consistency Walk (100 plies)...\n";
    Board board;
    board.reset();

    std::mt19937 rng(1337);

    for (int ply = 0; ply < 100; ++ply) {
        std::vector<Move> legal;
        MoveGen::generate_legal_moves(board, legal);

        if (legal.empty()) {
            // Reached terminal node, reset
            board.reset();
            continue;
        }

        // Verify Zobrist integrity
        assert(board.zobrist_hash() == board.compute_zobrist());

        // Select random legal move
        std::uniform_int_distribution<size_t> dist(0, legal.size() - 1);
        Move m = legal[dist(rng)];

        board.make_move(m);

        // Verify side that just moved did not leave their king in check
        Color just_moved = ~board.side_to_move();
        assert(!board.in_check(just_moved));

        // Verify SEE evaluation is finite and doesn't crash
        int see_val = board.see(m);
        (void)see_val;
    }
    std::cout << "  --> PASS: All 100 random moves strictly legal, king safe, Zobrist 100% verified.\n";
}

void test_search_node_limit() {
    std::cout << "[TEST] 3. UCI node-limit enforcement and legal fallback...\n";
    Board board;
    TranspositionTable tt(4);
    Searcher searcher(tt);
    SearchLimits limits;
    limits.max_depth = 64;
    limits.max_nodes = 1;

    std::stringstream output;
    std::streambuf* previous_output = std::cout.rdbuf(output.rdbuf());
    const Move best = searcher.start_search(board, limits);
    std::cout.rdbuf(previous_output);

    assert(searcher.nodes_searched() == limits.max_nodes);
    assert(!best.is_null());
    assert(MoveGen::is_legal_move(board, best));
    std::cout << "  --> PASS: Search stopped at the exact node budget and returned a legal move.\n";
}

void test_en_passant_see_xray() {
    std::cout << "[TEST] 4. En-passant SEE reveals slider recaptures...\n";
    Board board;
    assert(board.set_fen("4k3/8/8/3pP3/3r4/8/8/4K3 w - d6 0 1"));
    const Move en_passant = MoveGen::parse_uci_move(board, "e5d6");
    assert(!en_passant.is_null());
    assert(board.see(en_passant) == 0);
    assert(!board.see_ge(en_passant, 1));
    std::cout << "  --> PASS: SEE accounts for the rook recapture opened by en passant.\n";
}

void test_uci_integration() {
    std::cout << "[TEST] 5. Complete UCI Protocol Integration Pipeline...\n";
    Uci uci;

    // Redirect cout to stringstream to capture output
    std::stringstream buffer;
    std::streambuf* prev_cout = std::cout.rdbuf(buffer.rdbuf());

    // 1. handle_uci
    uci.handle_uci();
    std::string uci_out = buffer.str();
    assert(uci_out.find("id name ChessMinds") != std::string::npos);
    assert(uci_out.find("uciok") != std::string::npos);
    buffer.str("");

    // 2. handle_isready
    uci.handle_isready();
    std::string ready_out = buffer.str();
    assert(ready_out.find("readyok") != std::string::npos);
    buffer.str("");

    // 3. handle_ucinewgame & handle_position
    uci.handle_ucinewgame();
    uci.handle_position("position startpos moves e2e4 e7e5");

    // 4. handle_legalmoves
    uci.handle_legalmoves();
    std::string legal_out = buffer.str();
    assert(legal_out.find("legalmoves") != std::string::npos);
    buffer.str("");

    // 5. handle_go depth
    uci.handle_go("go depth 2");
    std::string go_out = buffer.str();
    assert(go_out.find("bestmove") != std::string::npos);
    buffer.str("");

    // Restore cout
    std::cout.rdbuf(prev_cout);
    std::cout << "  --> PASS: UCI identification, readiness, move execution, and go commands verified.\n";
}

int main() {
    std::cout << "========================================================\n";
    std::cout << " ChessMinds Regression, Stress & UCI Integration Suite  \n";
    std::cout << "========================================================\n";

    test_tactical_regression();
    test_random_walk_legality();
    test_search_node_limit();
    test_en_passant_see_xray();
    test_uci_integration();

    std::cout << "========================================================\n";
    std::cout << " ALL REGRESSION & INTEGRATION TESTS PASSED! [100%]\n";
    std::cout << "========================================================\n";
    return 0;
}
