import unittest
import os
import sys
from pathlib import Path

# Add project root to sys.path
repo_root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(repo_root))

from tools.tournament import (
    EngineConfig,
    UCISubprocess,
    SPRT,
    calculate_elo_difference,
    load_openings,
    DEFAULT_OPENINGS,
    Tournament,
)
import chess


class TestTournamentFramework(unittest.TestCase):
    def setUp(self):
        self.engine_path = str(repo_root / "build" / "chessmind.exe")
        self.assertTrue(os.path.isfile(self.engine_path), f"Engine binary not found at {self.engine_path}")

    def test_uci_communication(self):
        """Tests UCI engine startup, handshake, options, and ready check."""
        cfg = EngineConfig(name="ChessMindsTest", path=self.engine_path, hash_mb=32)
        proc = UCISubprocess(cfg, timeout_sec=10.0)
        try:
            proc.start()
            self.assertTrue(proc.actual_name.startswith("ChessMinds"))
            self.assertTrue(proc.wait_ready())
            
            # Request move from start position
            b = chess.Board()
            move_uci, info = proc.get_move(b)
            self.assertIsNotNone(move_uci)
            m = chess.Move.from_uci(move_uci)
            self.assertIn(m, b.legal_moves)
        finally:
            proc.terminate()

    def test_elo_calculation_and_ci(self):
        """Tests standard Elo difference and confidence interval calculations."""
        # Equal score -> 0 Elo difference
        elo, ci = calculate_elo_difference(10, 20, 10)
        self.assertAlmostEqual(elo, 0.0, delta=1.0)
        self.assertGreater(ci, 0.0)

        # 75% score -> +190.8 Elo
        elo_win, ci_win = calculate_elo_difference(30, 0, 10)
        self.assertGreater(elo_win, 150.0)
        self.assertLess(elo_win, 250.0)

        # 25% score -> -190.8 Elo
        elo_loss, ci_loss = calculate_elo_difference(10, 0, 30)
        self.assertLess(elo_loss, -150.0)
        self.assertGreater(elo_loss, -250.0)

    def test_sprt_hypothesis_bounds(self):
        """Tests Sequential Probability Ratio Test early stop triggers."""
        sprt = SPRT(elo0=0.0, elo1=35.0, alpha=0.05, beta=0.05)
        
        # Consistent dominant wins should trigger H1
        for _ in range(30):
            llr, decision = sprt.update(wins=25, draws=5, losses=0)
            if decision == "H1_ACCEPTED":
                break
        self.assertEqual(decision, "H1_ACCEPTED")

        # Heavy losses should trigger H0
        sprt2 = SPRT(elo0=0.0, elo1=35.0, alpha=0.05, beta=0.05)
        for _ in range(30):
            llr, decision = sprt2.update(wins=0, draws=5, losses=25)
            if decision == "H0_ACCEPTED":
                break
        self.assertEqual(decision, "H0_ACCEPTED")

    def test_paired_color_switching_and_game_termination(self):
        """Runs a mini 2-game paired match and validates PGN/CSV/JSON artifacts."""
        cfg1 = EngineConfig(name="CM_Depth3", path=self.engine_path, depth=3)
        cfg2 = EngineConfig(name="CM_Depth2", path=self.engine_path, depth=2)

        openings = [DEFAULT_OPENINGS[0]]  # standard startpos
        pgn_test = str(repo_root / "build" / "test_match.pgn")
        csv_test = str(repo_root / "build" / "test_match.csv")
        json_test = str(repo_root / "build" / "test_match.json")

        tourney = Tournament(
            config1=cfg1,
            config2=cfg2,
            openings=openings,
            rounds=1, # 1 pair = 2 games
            pgn_file=pgn_test,
            csv_file=csv_test,
            json_file=json_test,
            seed=1234,
        )

        summary = tourney.run()
        self.assertEqual(summary["total_games"], 2)
        self.assertEqual(len(tourney.records), 2)
        
        # Verify color switching: Game 1 CM_Depth3 White, Game 2 CM_Depth3 Black
        self.assertEqual(tourney.records[0].white_name, "CM_Depth3")
        self.assertEqual(tourney.records[0].black_name, "CM_Depth2")
        self.assertEqual(tourney.records[1].white_name, "CM_Depth2")
        self.assertEqual(tourney.records[1].black_name, "CM_Depth3")

        # Verify artifacts exist
        self.assertTrue(os.path.isfile(pgn_test))
        self.assertTrue(os.path.isfile(csv_test))
        self.assertTrue(os.path.isfile(json_test))

        # Check PGN readability with python-chess
        with open(pgn_test, "r", encoding="utf-8") as f:
            game1 = chess.pgn.read_game(f)
            self.assertIsNotNone(game1)
            self.assertEqual(game1.headers["White"], "CM_Depth3")
            game2 = chess.pgn.read_game(f)
            self.assertIsNotNone(game2)
            self.assertEqual(game2.headers["White"], "CM_Depth2")


if __name__ == "__main__":
    unittest.main()
