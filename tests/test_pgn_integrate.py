#!/usr/bin/env python3
"""
Unit and Integration Tests for ChessMinds PGN Integration & Resume Pipeline
=============================================================================
Tests:
  1. Canonical FEN deduplication (transposition handling, clock independence).
  2. Append-only behavior (never deletes or truncates existing data).
  3. Safe backup creation before dataset modification.
  4. Idempotency (re-running on same PGN appends 0 positions).
  5. Parameter export, serialization, and resume loading.
  6. Rating and result filtering.
"""

import io
import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path

import chess
import chess.pgn

from tools.pgn_integrate import (
    fen_key,
    is_quiet,
    load_existing_keys,
    backup_dataset,
    append_to_dataset,
    extract_positions_from_pgn,
)
from tools.texel_fast import Params


class PgnIntegrateTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_fen_key_canonical_deduplication(self):
        # Two FENs with identical position/side/castling/ep, but different halfmove/fullmove clocks
        fen1 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1"
        fen2 = "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 5 12"
        self.assertEqual(fen_key(fen1), fen_key(fen2))

        # Different position must have different key
        fen3 = "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2"
        self.assertNotEqual(fen_key(fen1), fen_key(fen3))

    def test_backup_creates_exact_copy(self):
        dataset = self.root / "quiet_positions.txt"
        sample_content = (
            "# ChessMinds Dataset\n"
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1\t0.5\n"
        )
        dataset.write_text(sample_content, encoding="utf-8")

        bak = backup_dataset(dataset)
        self.assertTrue(bak.exists())
        self.assertEqual(bak.read_text(encoding="utf-8"), sample_content)

    def test_append_preserves_existing_data(self):
        dataset = self.root / "quiet_positions.txt"
        initial_lines = [
            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1\t0.5\n",
            "rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2\t1.0\n",
        ]
        dataset.write_text("".join(initial_lines), encoding="utf-8")

        new_positions = [
            ("r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3", 0.5),
            ("r1bqk1nr/pppp1ppp/2n5/4p3/1b2P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 4 4", 1.0),
        ]
        append_to_dataset(dataset, new_positions, "test_source.pgn")

        content = dataset.read_text(encoding="utf-8")
        # Existing lines must still be present
        for line in initial_lines:
            self.assertIn(line.strip(), content)

        # Appended positions must be present
        for fen, res in new_positions:
            self.assertIn(f"{fen}\t{res}", content)

    def test_idempotency_and_deduplication(self):
        pgn_text = (
            '[Event "Test Game 1"]\n'
            '[WhiteElo "2200"]\n'
            '[BlackElo "2300"]\n'
            '[Result "1-0"]\n\n'
            "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 d6 8. c3 O-O 9. h3 Nb8 10. d4 Nbd7 1-0\n"
        )
        pgn_path = self.root / "test.pgn"
        pgn_path.write_text(pgn_text, encoding="utf-8")

        existing_keys = set()
        # First extraction: extracts new positions
        new_pos1, stats1 = extract_positions_from_pgn(
            pgn_files=[pgn_path],
            existing_keys=existing_keys,
            min_elo=2000,
            skip_opening=0,
            max_new_positions=100,
        )
        self.assertGreater(len(new_pos1), 0)
        self.assertEqual(stats1["positions_new"], len(new_pos1))

        # Second extraction with the same keys populated: must find ZERO new positions (100% duplicates)
        new_pos2, stats2 = extract_positions_from_pgn(
            pgn_files=[pgn_path],
            existing_keys=existing_keys,
            min_elo=2000,
            skip_opening=0,
            max_new_positions=100,
        )
        self.assertEqual(len(new_pos2), 0)
        self.assertEqual(stats2["positions_new"], 0)
        self.assertGreater(stats2["positions_duplicate"], 0)

    def test_rating_and_result_filtering(self):
        pgn_text = (
            '[Event "Low rating"]\n[WhiteElo "1800"]\n[BlackElo "1900"]\n[Result "1-0"]\n\n1. e4 e5 1-0\n\n'
            '[Event "Unrated"]\n[Result "1-0"]\n\n1. e4 e5 1-0\n\n'
            '[Event "Incomplete"]\n[WhiteElo "2400"]\n[BlackElo "2400"]\n[Result "*"]\n\n1. e4 e5 *\n\n'
            '[Event "Valid"]\n[WhiteElo "2200"]\n[BlackElo "2200"]\n[Result "1/2-1/2"]\n\n1. e4 e5 2. Nf3 Nc6 1/2-1/2\n'
        )
        pgn_path = self.root / "filters.pgn"
        pgn_path.write_text(pgn_text, encoding="utf-8")

        existing_keys = set()
        new_pos, stats = extract_positions_from_pgn(
            pgn_files=[pgn_path],
            existing_keys=existing_keys,
            min_elo=2000,
            skip_opening=0,
            max_new_positions=100,
        )
        self.assertEqual(stats["games_read"], 4)
        self.assertEqual(stats["games_skip_elo"], 2)       # Low rating + Unrated
        self.assertEqual(stats["games_skip_result"], 1)    # Incomplete
        self.assertEqual(stats["games_accepted"], 1)       # Valid


class TexelParamsSerializationTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_params_to_dict_and_from_dict(self):
        p = Params()
        d = p.to_dict()
        self.assertIn("mg_pv", d)
        self.assertIn("eg_pv", d)
        self.assertIn("mg_pawn", d)
        self.assertIn("eg_pawn", d)
        self.assertIn("mg_king", d)
        self.assertIn("eg_king", d)
        self.assertEqual(len(d["mg_pv"]), 6)
        self.assertEqual(len(d["eg_pv"]), 6)
        self.assertEqual(len(d["mg_pawn"]), 64)

        # Mutate a value and restore
        p.mg_pv[1] = 999
        self.assertEqual(p.mg_pv[1], 999)
        p.from_dict(d)
        self.assertNotEqual(p.mg_pv[1], 999)

    def test_json_save_and_load(self):
        json_path = self.root / "checkpoint.json"
        p1 = Params()
        p1.mg_pv[0] = 111
        p1.bishop_pair_mg = 45
        p1.save_json(json_path)

        self.assertTrue(json_path.exists())

        p2 = Params()
        p2.load_json(json_path)
        self.assertEqual(p2.mg_pv[0], 111)
        self.assertEqual(p2.bishop_pair_mg, 45)


if __name__ == "__main__":
    unittest.main()
