#!/usr/bin/env python3
"""
ChessMinds PGN Integrator — Append-Only, Deduplication-Safe
============================================================
Extracts quiet positions from new PGN file(s) and APPENDS them to the
existing quiet_positions.txt dataset without deleting any prior positions.

Key guarantees:
  - Existing positions are NEVER deleted or overwritten.
  - Only positions with FENs not already in the dataset are appended.
  - A .bak backup is created before any write.
  - Running twice on the same input appends zero new positions (idempotent).
  - Reports: games processed, positions extracted, duplicates skipped, appended.

Format:
  quiet_positions.txt: "FEN<TAB>result" (1.0=white wins, 0.5=draw, 0.0=black wins)
  Same format consumed by texel_fast.py and texel_tuner.py.

Usage:
  py tools/pgn_integrate.py --input data/raw/my_new.pgn
  py tools/pgn_integrate.py --input data/raw/ --min-elo 2000
  py tools/pgn_integrate.py --input data/raw/my.pgn --stats-only
  py tools/pgn_integrate.py --input data/raw/my.pgn --max-positions 500000
"""

from __future__ import annotations

import argparse
import gzip
import io
import shutil
import sys
import time
from pathlib import Path

import chess
import chess.pgn

# ─── Paths ────────────────────────────────────────────────────────────────────
ROOT         = Path(__file__).resolve().parent.parent
POSITIONS_DIR = ROOT / "data" / "positions"
DEFAULT_DATASET = POSITIONS_DIR / "quiet_positions.txt"
RAW_DIR      = ROOT / "data" / "raw"

# ─── Helpers ──────────────────────────────────────────────────────────────────

def result_to_float(result_str: str) -> float | None:
    """Convert PGN result string to white-perspective float."""
    if result_str == "1-0":      return 1.0
    if result_str == "0-1":      return 0.0
    if result_str == "1/2-1/2": return 0.5
    return None   # '*' or unknown → discard


def parse_elo(value: str | None) -> int | None:
    """Parse ELO tag value to int, return None if invalid."""
    if value is None:
        return None
    try:
        elo = int(value)
        return elo if elo > 0 else None
    except ValueError:
        return None


def is_quiet(board: chess.Board, move: chess.Move) -> bool:
    """
    Returns True if the resulting position is quiet (suitable for Texel tuning).
    Mirrors filter_pgn.py logic:
      - Not a capture
      - Not a promotion
      - Does not give check
    """
    if board.is_capture(move):
        return False
    if move.promotion is not None:
        return False
    board.push(move)
    in_check = board.is_check()
    board.pop()
    return not in_check


def fen_key(fen: str) -> str:
    """
    Canonical FEN key for deduplication: use only the first 4 fields
    (position, side-to-move, castling, en passant) — ignore halfmove/fullmove
    clocks so transpositions with different clocks are still de-duped.
    """
    return " ".join(fen.split()[:4])


def open_pgn_file(path: Path):
    """Open a .pgn or .pgn.gz file for text reading."""
    if path.suffix.lower() == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("rt", encoding="utf-8", errors="replace")


# ─── Phase 1: Load existing dataset keys ──────────────────────────────────────

def load_existing_keys(dataset_path: Path, verbose: bool = True) -> set[str]:
    """
    Read the existing quiet_positions.txt and return a set of canonical FEN keys.
    Comments and blank lines are skipped. Very memory-efficient (only key stored).
    """
    keys: set[str] = set()
    if not dataset_path.exists():
        if verbose:
            print(f"  [INFO] Dataset not found at {dataset_path} — will create fresh.")
        return keys

    t0 = time.time()
    if verbose:
        print(f"  Loading existing dataset keys from: {dataset_path.name}")

    line_count = 0
    with open(dataset_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 1:
                continue
            keys.add(fen_key(parts[0]))
            line_count += 1
            if verbose and line_count % 1_000_000 == 0:
                print(f"    ... loaded {line_count:,} keys", end="\r", flush=True)

    if verbose:
        elapsed = time.time() - t0
        print(f"  Loaded {len(keys):,} existing position keys in {elapsed:.1f}s")

    return keys


# ─── Phase 2: Collect input PGN files ─────────────────────────────────────────

def collect_pgn_files(inputs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for item in inputs:
        if item.is_dir():
            files.extend(
                p for p in sorted(item.rglob("*"))
                if p.is_file() and p.suffix.lower() in (".pgn",) or
                   (p.is_file() and p.name.lower().endswith(".pgn.gz"))
            )
        elif item.is_file():
            files.append(item)
        else:
            raise FileNotFoundError(f"Input does not exist: {item}")
    result = sorted(set(p.resolve() for p in files))
    if not result:
        raise FileNotFoundError("No .pgn or .pgn.gz files found in the supplied input(s).")
    return result


# ─── Phase 3: Extract new positions from PGN ──────────────────────────────────

def extract_positions_from_pgn(
    pgn_files: list[Path],
    existing_keys: set[str],
    min_elo: int,
    skip_opening: int,
    max_new_positions: int,
) -> tuple[list[tuple[str, float]], dict]:
    """
    Replay PGN games and extract quiet positions not in existing_keys.

    Returns:
      new_positions: list of (fen, result_float)
      stats: dict with processing statistics
    """
    stats = {
        "files_processed": 0,
        "games_read": 0,
        "games_accepted": 0,
        "games_skip_result": 0,
        "games_skip_elo": 0,
        "games_skip_malformed": 0,
        "positions_candidate": 0,
        "positions_duplicate": 0,
        "positions_new": 0,
    }

    new_positions: list[tuple[str, float]] = []

    for pgn_path in pgn_files:
        if len(new_positions) >= max_new_positions:
            print(f"\n  [LIMIT] Max new positions ({max_new_positions:,}) reached — stopping.")
            break

        file_size = pgn_path.stat().st_size
        print(f"\n  ── Processing: {pgn_path.name}  ({file_size / 1e6:.1f} MB)")
        stats["files_processed"] += 1

        file_games = 0
        t0 = time.time()

        with open_pgn_file(pgn_path) as pgn_stream:
            while True:
                if len(new_positions) >= max_new_positions:
                    break
                try:
                    game = chess.pgn.read_game(pgn_stream)
                except Exception:
                    stats["games_skip_malformed"] += 1
                    continue

                if game is None:
                    break

                stats["games_read"] += 1
                file_games += 1

                if file_games % 10_000 == 0:
                    elapsed = time.time() - t0
                    print(
                        f"    [{pgn_path.name}] {file_games:,} games | "
                        f"+{stats['positions_new']:,} new | {elapsed:.0f}s",
                        end="\r", flush=True
                    )

                # ── Filter: result ──────────────────────────────────────────
                result_str = game.headers.get("Result", "*")
                result = result_to_float(result_str)
                if result is None:
                    stats["games_skip_result"] += 1
                    continue

                # ── Filter: Elo ─────────────────────────────────────────────
                if min_elo > 0:
                    w_elo = parse_elo(game.headers.get("WhiteElo"))
                    b_elo = parse_elo(game.headers.get("BlackElo"))
                    if w_elo is None or b_elo is None:
                        stats["games_skip_elo"] += 1
                        continue
                    if w_elo < min_elo or b_elo < min_elo:
                        stats["games_skip_elo"] += 1
                        continue

                # ── Replay moves ─────────────────────────────────────────────
                board = game.board()
                try:
                    moves_list = list(game.mainline_moves())
                except Exception:
                    stats["games_skip_malformed"] += 1
                    continue

                if not moves_list:
                    stats["games_skip_malformed"] += 1
                    continue

                game_ok = True
                half_move = 0
                game_positions: list[tuple[str, float]] = []

                for move in moves_list:
                    if not board.is_legal(move):
                        game_ok = False
                        break

                    if half_move >= skip_opening * 2:
                        if is_quiet(board, move):
                            fen = board.fen()
                            key = fen_key(fen)
                            stats["positions_candidate"] += 1
                            if key not in existing_keys:
                                game_positions.append((fen, result))
                            else:
                                stats["positions_duplicate"] += 1

                    board.push(move)
                    half_move += 1

                if not game_ok:
                    stats["games_skip_malformed"] += 1
                    continue

                stats["games_accepted"] += 1

                # Add game positions, respecting global limit and updating key set
                for fen, res in game_positions:
                    key = fen_key(fen)
                    if key not in existing_keys:  # double-check (another game may have added it)
                        existing_keys.add(key)
                        new_positions.append((fen, res))
                        stats["positions_new"] += 1
                        if len(new_positions) >= max_new_positions:
                            break

        elapsed = time.time() - t0
        print(f"\n  ✓ Done {pgn_path.name}: {file_games:,} games | +{stats['positions_new']:,} new | {elapsed:.1f}s")

    return new_positions, stats


# ─── Phase 4: Backup + Append ─────────────────────────────────────────────────

def backup_dataset(dataset_path: Path) -> Path:
    """Create a .bak backup of the dataset. Returns the backup path."""
    bak = dataset_path.with_suffix(".txt.bak")
    shutil.copy2(dataset_path, bak)
    return bak


def append_to_dataset(
    dataset_path: Path,
    new_positions: list[tuple[str, float]],
    source_label: str,
) -> None:
    """Append new positions to dataset_path with a header comment block."""
    dataset_path.parent.mkdir(parents=True, exist_ok=True)

    # Create file with header if it doesn't exist
    if not dataset_path.exists():
        with open(dataset_path, "w", encoding="utf-8") as f:
            f.write("# ChessMinds Texel-Tuning Position Dataset\n")
            f.write("# Format: FEN <TAB> result  (1.0=white wins, 0.5=draw, 0.0=black wins)\n")
            f.write("# " + "─" * 60 + "\n")

    import datetime
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    with open(dataset_path, "a", encoding="utf-8") as f:
        f.write(f"\n# ── Appended {len(new_positions):,} positions from: {source_label} at {timestamp} ──\n")
        for fen, result in new_positions:
            f.write(f"{fen}\t{result}\n")


# ─── Stats-only mode ─────────────────────────────────────────────────────────

def print_dataset_stats(dataset_path: Path) -> None:
    """Print statistics about the existing dataset without modifying it."""
    if not dataset_path.exists():
        print(f"  Dataset not found: {dataset_path}")
        return

    size_mb = dataset_path.stat().st_size / 1e6
    total = 0
    white_wins = draws = black_wins = 0
    t0 = time.time()

    with open(dataset_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 2:
                continue
            total += 1
            try:
                r = float(parts[1])
                if r == 1.0:   white_wins += 1
                elif r == 0.0: black_wins += 1
                else:          draws += 1
            except ValueError:
                pass
            if total % 1_000_000 == 0:
                print(f"    ... scanned {total:,} positions", end="\r", flush=True)

    elapsed = time.time() - t0
    print(f"\n{'='*60}")
    print(f"  Dataset Statistics: {dataset_path.name}")
    print(f"{'='*60}")
    print(f"  File size       : {size_mb:.1f} MB")
    print(f"  Total positions : {total:,}")
    print(f"  White wins (1.0): {white_wins:,}  ({white_wins/max(total,1)*100:.1f}%)")
    print(f"  Draws (0.5)     : {draws:,}  ({draws/max(total,1)*100:.1f}%)")
    print(f"  Black wins (0.0): {black_wins:,}  ({black_wins/max(total,1)*100:.1f}%)")
    print(f"  Scanned in      : {elapsed:.1f}s")
    print(f"{'='*60}")


# ─── Main ─────────────────────────────────────────────────────────────────────

def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="ChessMinds PGN Integrator — append new positions to existing dataset",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument(
        "--input", "-i",
        action="append",
        type=Path,
        default=None,
        help="PGN file or directory to integrate (repeat for multiple inputs)",
    )
    parser.add_argument(
        "--dataset", "-d",
        type=Path,
        default=DEFAULT_DATASET,
        help=f"Existing position dataset to append to (default: {DEFAULT_DATASET})",
    )
    parser.add_argument(
        "--min-elo",
        type=int,
        default=2000,
        help="Minimum Elo for both players; 0 = no filter (default: 2000)",
    )
    parser.add_argument(
        "--skip-opening",
        type=int,
        default=8,
        help="Skip first N full moves per game (default: 8)",
    )
    parser.add_argument(
        "--max-positions",
        type=int,
        default=5_000_000,
        help="Maximum new positions to append per run (default: 5,000,000)",
    )
    parser.add_argument(
        "--stats-only",
        action="store_true",
        help="Print statistics about the existing dataset and exit (no PGN processing)",
    )
    parser.add_argument(
        "--no-backup",
        action="store_true",
        help="Skip creating a .bak backup before appending (not recommended)",
    )
    args = parser.parse_args(argv)

    if not args.stats_only and not args.input:
        parser.error("the following arguments are required: --input/-i (unless --stats-only is specified)")

    dataset_path = args.dataset
    if not dataset_path.is_absolute():
        dataset_path = ROOT / dataset_path

    print("=" * 60)
    print("  ChessMinds PGN Integrator")
    print("=" * 60)
    print(f"  Dataset         : {dataset_path}")
    print(f"  Min Elo         : {args.min_elo}")
    print(f"  Skip opening    : {args.skip_opening} full moves")
    print(f"  Max new pos     : {args.max_positions:,}")
    print("=" * 60)

    # ── Stats-only mode ──────────────────────────────────────────────────────
    if args.stats_only:
        print_dataset_stats(dataset_path)
        return 0

    # ── Collect PGN files ────────────────────────────────────────────────────
    try:
        pgn_files = collect_pgn_files(args.input)
    except FileNotFoundError as e:
        print(f"\n  ERROR: {e}", file=sys.stderr)
        return 1

    print(f"\n  PGN input files : {len(pgn_files)}")
    for f in pgn_files:
        print(f"    {f.name}  ({f.stat().st_size / 1e6:.1f} MB)")

    # ── Load existing keys ───────────────────────────────────────────────────
    print()
    existing_keys = load_existing_keys(dataset_path)
    baseline_count = len(existing_keys)

    # ── Extract new positions ────────────────────────────────────────────────
    print()
    source_label = ", ".join(f.name for f in pgn_files)
    new_positions, stats = extract_positions_from_pgn(
        pgn_files=pgn_files,
        existing_keys=existing_keys,
        min_elo=args.min_elo,
        skip_opening=args.skip_opening,
        max_new_positions=args.max_positions,
    )

    # ── Print extraction stats ───────────────────────────────────────────────
    print(f"\n{'='*60}")
    print(f"  Extraction Summary")
    print(f"{'='*60}")
    print(f"  PGN files processed  : {stats['files_processed']}")
    print(f"  Games read           : {stats['games_read']:,}")
    print(f"  Games accepted       : {stats['games_accepted']:,}")
    print(f"  Games skip (result)  : {stats['games_skip_result']:,}")
    print(f"  Games skip (Elo)     : {stats['games_skip_elo']:,}")
    print(f"  Games skip (malform) : {stats['games_skip_malformed']:,}")
    print(f"  Candidate positions  : {stats['positions_candidate']:,}")
    print(f"  Duplicates skipped   : {stats['positions_duplicate']:,}")
    print(f"  New positions found  : {stats['positions_new']:,}")
    print(f"{'='*60}")

    if not new_positions:
        print("\n  No new positions to append — dataset unchanged.")
        return 0

    # ── Backup ───────────────────────────────────────────────────────────────
    if not args.no_backup and dataset_path.exists():
        bak = backup_dataset(dataset_path)
        print(f"\n  Backup created: {bak.name}")

    # ── Append ───────────────────────────────────────────────────────────────
    print(f"\n  Appending {len(new_positions):,} positions to {dataset_path.name} ...")
    append_to_dataset(dataset_path, new_positions, source_label)

    # ── Verify ───────────────────────────────────────────────────────────────
    new_keys = load_existing_keys(dataset_path, verbose=False)
    final_count = len(new_keys)

    print(f"\n{'='*60}")
    print(f"  Integration Complete")
    print(f"{'='*60}")
    print(f"  Positions before : {baseline_count:,}")
    print(f"  Positions appended: {len(new_positions):,}")
    print(f"  Positions after  : {final_count:,}")
    print(f"  Net change       : +{final_count - baseline_count:,}")
    print(f"  Dataset file     : {dataset_path}")
    print(f"{'='*60}")

    if final_count < baseline_count:
        print("\n  ERROR: Dataset shrank — this should never happen!", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
