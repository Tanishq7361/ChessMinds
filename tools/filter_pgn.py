"""
ChessMinds PGN Filter & Quiet-Position Extractor
=================================================
Phase 1: Filters raw Lichess PGN files by:
  - Minimum Elo for both players (default: 2000)
  - Minimum moves (default: 10)
  - Excludes time-forfeited / abandoned / incomplete games
  - Excludes bullet games (< 60s base time) unless flag set
  - Only keeps decisive results (1-0, 0-1) or draws (1/2-1/2)

Phase 2: Extracts quiet positions for Texel tuning:
  - Skips the first N moves (opening book territory)
  - Skips positions where the last move was a capture/check/promotion
  - Saves: FEN  <TAB>  result  (1.0 / 0.5 / 0.0 from white's perspective)

Output:
  data/filtered/*.pgn   -- filtered PGN files
  data/positions/*.txt  -- quiet positions: "FEN\tresult"

Usage:
  python tools/filter_pgn.py [--min-elo 2000] [--min-moves 10]
                             [--skip-opening 8] [--max-per-file 500000]
                             [--bullet] [--no-draw]

Competition note:
  Uses only human game results as labels. No engine evaluation labels.
  Fully compliant with competition rules.
"""

import chess
import chess.pgn
import os
import sys
import argparse
import time
import io
from pathlib import Path

# ─── Paths ────────────────────────────────────────────────────────────────────
ROOT = Path(__file__).resolve().parent.parent
RAW_DIR      = ROOT / "data" / "raw"
FILTERED_DIR = ROOT / "data" / "filtered"
POSITIONS_DIR= ROOT / "data" / "positions"

FILTERED_DIR.mkdir(parents=True, exist_ok=True)
POSITIONS_DIR.mkdir(parents=True, exist_ok=True)

# ─── Helpers ──────────────────────────────────────────────────────────────────

def result_to_float(result_str: str) -> float | None:
    """Convert PGN result string to white-perspective float."""
    if result_str == "1-0":   return 1.0
    if result_str == "0-1":   return 0.0
    if result_str == "1/2-1/2": return 0.5
    return None   # '*' or unknown → discard

def parse_time_control(tc: str) -> tuple[int, int]:
    """Parse 'base+inc' string → (base_seconds, increment_seconds). Returns (-1,-1) on failure."""
    try:
        if '+' in tc:
            parts = tc.split('+')
            return int(parts[0]), int(parts[1])
        elif tc.isdigit():
            return int(tc), 0
    except Exception:
        pass
    return -1, -1

def is_quiet(board: chess.Board, move: chess.Move) -> bool:
    """
    Returns True if this move creates a 'quiet' position suitable for Texel tuning:
    - Not a capture
    - Not a promotion
    - Not a move that leaves the opponent in check
    """
    if board.is_capture(move):
        return False
    if move.promotion is not None:
        return False
    # Apply move temporarily to check if it gives check
    board.push(move)
    in_check = board.is_check()
    board.pop()
    return not in_check

def filter_and_extract(
    pgn_path: Path,
    min_elo: int,
    min_moves: int,
    skip_opening: int,
    max_positions: int,
    allow_bullet: bool,
    allow_draws: bool,
    pos_writer,
    pgn_writer,
    stats: dict,
    seen_fens: set,
):
    """Process one PGN file, filter games, extract quiet positions."""
    file_size = pgn_path.stat().st_size
    print(f"\n{'─'*70}")
    print(f"  Processing: {pgn_path.name}  ({file_size / 1e6:.1f} MB)")
    print(f"{'─'*70}")

    games_read    = 0
    games_kept    = 0
    pos_extracted = 0
    start = time.time()

    with open(pgn_path, encoding="utf-8", errors="replace") as f:
        while True:
            try:
                game = chess.pgn.read_game(f)
            except Exception:
                continue
            if game is None:
                break

            games_read += 1

            # ── Progress every 5000 games ──
            if games_read % 5000 == 0:
                elapsed = time.time() - start
                print(f"    [{pgn_path.name}] Read {games_read:,} games | "
                      f"Kept {games_kept:,} | Positions {pos_extracted:,} | "
                      f"{elapsed:.0f}s elapsed")
                sys.stdout.flush()

            headers = game.headers

            # ── Filter: result ──────────────────────────────────────────────
            result_str = headers.get("Result", "*")
            result = result_to_float(result_str)
            if result is None:
                stats["skip_result"] += 1
                continue
            if not allow_draws and result == 0.5:
                stats["skip_draw"] += 1
                continue

            # ── Filter: termination ─────────────────────────────────────────
            termination = headers.get("Termination", "").lower()
            if "time forfeit" in termination or "abandoned" in termination:
                stats["skip_termination"] += 1
                continue

            # ── Filter: Elo ─────────────────────────────────────────────────
            try:
                w_elo = int(headers.get("WhiteElo", "0"))
                b_elo = int(headers.get("BlackElo", "0"))
            except ValueError:
                stats["skip_elo"] += 1
                continue
            if w_elo < min_elo or b_elo < min_elo:
                stats["skip_elo"] += 1
                continue

            # ── Filter: time control (no bullet unless flag) ─────────────────
            tc = headers.get("TimeControl", "")
            base, inc = parse_time_control(tc)
            if not allow_bullet and base != -1 and base < 60:
                stats["skip_tc"] += 1
                continue

            # ── Filter: minimum move count ───────────────────────────────────
            moves_list = list(game.mainline_moves())
            if len(moves_list) < min_moves * 2:   # half-moves
                stats["skip_short"] += 1
                continue

            # ── Game passed all filters → write to filtered PGN ─────────────
            games_kept += 1
            stats["games_kept"] += 1
            pgn_writer.write(str(game) + "\n\n")

            # ── Extract quiet positions ──────────────────────────────────────
            if pos_extracted >= max_positions:
                continue

            board = game.board()
            half_move = 0

            for move in moves_list:
                # Skip opening moves
                if half_move < skip_opening * 2:
                    board.push(move)
                    half_move += 1
                    continue

                # Only record quiet positions (before the move is played)
                if is_quiet(board, move):
                    fen = board.fen()
                    if fen not in seen_fens:
                        seen_fens.add(fen)
                        pos_writer.write(f"{fen}\t{result}\n")
                        pos_extracted += 1
                        stats["positions_extracted"] += 1
                        if pos_extracted >= max_positions:
                            break

                board.push(move)
                half_move += 1

        elapsed = time.time() - start
        print(f"\n  ✓ Done {pgn_path.name}: {games_read:,} read | "
              f"{games_kept:,} kept | {pos_extracted:,} positions | "
              f"{elapsed:.1f}s")

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="ChessMinds PGN Filter & Quiet-Position Extractor"
    )
    parser.add_argument("--min-elo",       type=int, default=2000,
                        help="Minimum Elo for both players (default: 2000)")
    parser.add_argument("--min-moves",     type=int, default=10,
                        help="Minimum full moves in game (default: 10)")
    parser.add_argument("--skip-opening",  type=int, default=8,
                        help="Skip first N full moves per game (default: 8)")
    parser.add_argument("--max-positions", type=int, default=5_000_000,
                        help="Max quiet positions to extract total (default: 5M)")
    parser.add_argument("--bullet",        action="store_true",
                        help="Allow bullet games (< 60s base time)")
    parser.add_argument("--no-draw",       action="store_true",
                        help="Exclude drawn games")
    parser.add_argument("--files",         nargs="*",
                        help="Specific PGN filenames to process (default: all in data/raw)")
    args = parser.parse_args()

    # ── Gather input files ───────────────────────────────────────────────────
    if args.files:
        pgn_files = []
        for f in args.files:
            p = Path(f)
            if p.is_absolute() or p.exists():
                pgn_files.append(p)
            else:
                pgn_files.append(RAW_DIR / f)
    else:
        pgn_files = sorted(RAW_DIR.glob("*.pgn"))

    if not pgn_files:
        print("ERROR: No PGN files found in data/raw/")
        sys.exit(1)

    total_size_gb = sum(f.stat().st_size for f in pgn_files) / 1e9
    print("=" * 70)
    print("  ChessMinds PGN Filter & Position Extractor")
    print("=" * 70)
    print(f"  Input files  : {len(pgn_files)} PGN files ({total_size_gb:.2f} GB total)")
    print(f"  Min Elo      : {args.min_elo}")
    print(f"  Min moves    : {args.min_moves}")
    print(f"  Skip opening : {args.skip_opening} moves")
    print(f"  Max positions: {args.max_positions:,}")
    print(f"  Allow bullet : {args.bullet}")
    print(f"  Allow draws  : {not args.no_draw}")
    print("=" * 70)

    stats = {
        "games_kept": 0,
        "positions_extracted": 0,
        "skip_result": 0,
        "skip_draw": 0,
        "skip_termination": 0,
        "skip_elo": 0,
        "skip_tc": 0,
        "skip_short": 0,
    }

    global_start = time.time()
    seen_fens: set[str] = set()

    # ── Open output files ────────────────────────────────────────────────────
    filtered_pgn_path   = FILTERED_DIR / "filtered_games.pgn"
    positions_txt_path  = POSITIONS_DIR / "quiet_positions.txt"

    with open(filtered_pgn_path, "w", encoding="utf-8") as pgn_out, \
         open(positions_txt_path, "w", encoding="utf-8") as pos_out:

        pos_out.write("# ChessMinds Texel-Tuning Position Dataset\n")
        pos_out.write("# Format: FEN <TAB> result  (1.0=white wins, 0.5=draw, 0.0=black wins)\n")
        pos_out.write(f"# Filters: min_elo={args.min_elo}, min_moves={args.min_moves}, "
                      f"skip_opening={args.skip_opening}\n")
        pos_out.write("# " + "─" * 60 + "\n")

        for pgn_file in pgn_files:
            if stats["positions_extracted"] >= args.max_positions:
                print(f"\n  ✓ Max positions ({args.max_positions:,}) reached. Stopping early.")
                break
            remaining = args.max_positions - stats["positions_extracted"]
            filter_and_extract(
                pgn_path=pgn_file,
                min_elo=args.min_elo,
                min_moves=args.min_moves,
                skip_opening=args.skip_opening,
                max_positions=remaining,
                allow_bullet=args.bullet,
                allow_draws=not args.no_draw,
                pos_writer=pos_out,
                pgn_writer=pgn_out,
                stats=stats,
                seen_fens=seen_fens,
            )

    total_elapsed = time.time() - global_start

    print()
    print("=" * 70)
    print("  FILTER COMPLETE")
    print("=" * 70)
    print(f"  Games kept          : {stats['games_kept']:,}")
    print(f"  Positions extracted : {stats['positions_extracted']:,}")
    print(f"  Skipped (Elo)       : {stats['skip_elo']:,}")
    print(f"  Skipped (Time forfeit/abandon): {stats['skip_termination']:,}")
    print(f"  Skipped (short game): {stats['skip_short']:,}")
    print(f"  Skipped (draws)     : {stats['skip_draw']:,}")
    print(f"  Skipped (bad result): {stats['skip_result']:,}")
    print(f"  Skipped (bullet TC) : {stats['skip_tc']:,}")
    print(f"  Total time          : {total_elapsed:.1f}s ({total_elapsed/60:.1f} min)")
    print()
    print(f"  Output PGN      → {filtered_pgn_path}")
    print(f"  Output positions→ {positions_txt_path}")
    print("=" * 70)

if __name__ == "__main__":
    main()

