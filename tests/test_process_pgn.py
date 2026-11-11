import csv
import gzip
import io
import json
import tempfile
import unittest
from pathlib import Path

import chess.pgn

from tools.process_pgn import collect_quiet_positions, process_files


def game_from_text(text):
    game = chess.pgn.read_game(io.StringIO(text))
    if game is None:
        raise AssertionError("test PGN did not contain a game")
    return game


class ProcessPgnTests(unittest.TestCase):
    def test_extracts_only_legal_quiet_human_moves_with_actual_result(self):
        game = game_from_text(
            '[Event "quiet"]\n[WhiteElo "2100"]\n[BlackElo "2200"]\n[Result "1-0"]\n\n'
            "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 1-0\n"
        )

        reason, rows = collect_quiet_positions(game, "quiet.pgn", 1)

        self.assertIsNone(reason)
        self.assertEqual([row["move_uci"] for row in rows], ["e2e4", "e7e5", "g1f3", "b8c6", "f1b5", "a7a6"])
        self.assertTrue(all(row["result"] == "1-0" for row in rows))
        self.assertTrue(all(row["white_elo"] == 2100 and row["black_elo"] == 2200 for row in rows))
        for row in rows:
            board = chess.Board(row["fen"])
            move = chess.Move.from_uci(row["move_uci"])
            self.assertTrue(board.is_legal(move))
            self.assertFalse(board.is_check() or board.is_capture(move) or board.gives_check(move))

    def test_rejects_bad_ratings_results_and_malformed_moves(self):
        cases = (
            ('[WhiteElo "1999"]\n[BlackElo "2200"]\n[Result "1-0"]\n\n1. e4 1-0\n', "below_rating_threshold"),
            ('[WhiteElo "2100"]\n[BlackElo "2200"]\n[Result "*"]\n\n1. e4 *\n', "invalid_result"),
            ('[WhiteElo "2100"]\n[BlackElo "2200"]\n[Result "1-0"]\n\n1. e4 e5 2. Ke7 1-0\n', "malformed"),
            ('[WhiteElo "x"]\n[BlackElo "2200"]\n[Result "1-0"]\n\n1. e4 1-0\n', "invalid_ratings"),
        )
        for text, expected_reason in cases:
            with self.subTest(expected_reason=expected_reason):
                reason, rows = collect_quiet_positions(game_from_text(text), "test.pgn", 1)
                self.assertEqual(reason, expected_reason)
                self.assertEqual(rows, [])

    def test_streams_pgn_and_writes_compressed_csv_and_report(self):
        pgn = (
            '[WhiteElo "2100"]\n[BlackElo "2200"]\n[Result "1-0"]\n\n1. e4 e5 1-0\n\n'
            '[WhiteElo "1800"]\n[BlackElo "2200"]\n[Result "0-1"]\n\n1. d4 d5 0-1\n'
        )
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "games.pgn"
            source.write_text(pgn, encoding="utf-8")
            output = root / "positions.csv.gz"

            report = process_files([source], output)

            with gzip.open(output, "rt", encoding="utf-8", newline="") as stream:
                rows = list(csv.DictReader(stream))
            saved_report = json.loads((root / "positions.csv.gz.report.json").read_text(encoding="utf-8"))
            self.assertEqual(len(rows), 2)
            self.assertEqual(report["games_read"], 2)
            self.assertEqual(report["games_accepted"], 1)
            self.assertEqual(report["games_rejected"], 1)
            self.assertEqual(report["rejections_by_reason"]["below_rating_threshold"], 1)
            self.assertEqual(saved_report, report)


if __name__ == "__main__":
    unittest.main()
