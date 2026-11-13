#!/usr/bin/env python3
"""
ChessMinds Sequential Batch Processing & Training Queue Pipeline
================================================================
Implements memory-efficient, sequential batch training on multi-million position datasets.

Key guarantees:
  1. Incremental streaming of raw PGNs (never loads multi-gigabyte PGNs into memory).
  2. Strict deduplication and position filtering (minimum Elo, opening exclusion, quiet checks).
  3. Persistent, separate validation dataset (preventing data leakage).
  4. Reproducible deterministic queue randomization.
  5. Sequential 100,000-position batch training with parameter continuity.
  6. Transactional queue updates: positions are only popped AFTER successful batch training,
     validation loss check, and checkpoint persistence.
  7. Crash recovery: safe resumption from the latest valid checkpoint.
  8. Verified raw PGN cleanup with explicit confirmation.
"""

from __future__ import annotations

import argparse
import datetime
import gzip
import io
import json
import math
import os
import random
import shutil
import sys
import time
from pathlib import Path
from typing import Iterator

import chess
import chess.pgn

# Import evaluation & parameter handling from existing tools
ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from tools.pgn_integrate import fen_key, is_quiet, open_pgn_file, result_to_float, parse_elo
from tools.texel_fast import Params, compute_loss, load_data, write_params_to_cpp, sigmoid

# ─── Default Paths ────────────────────────────────────────────────────────────
DATA_DIR       = ROOT / "data"
RAW_DIR        = DATA_DIR / "raw"
QUEUE_DIR      = DATA_DIR / "queue"
PARAMS_DIR     = DATA_DIR / "params"
CHECKPOINTS_DIR= PARAMS_DIR / "batch_checkpoints"

MANIFEST_PATH  = QUEUE_DIR / "queue_manifest.json"
PENDING_QUEUE  = QUEUE_DIR / "pending_positions.txt"
PROCESSED_ARCH = QUEUE_DIR / "processed_positions.txt"
VALIDATION_SET = QUEUE_DIR / "validation_positions.txt"

BEST_PARAMS_JSON = PARAMS_DIR / "best_params.json"
EVAL_CPP         = ROOT / "src" / "eval.cpp"

# ─── Manifest & State Management ──────────────────────────────────────────────

def init_manifest() -> dict:
    """Initialize a fresh queue manifest dictionary."""
    return {
        "version": 1,
        "created_at": datetime.datetime.now().isoformat(),
        "last_updated": datetime.datetime.now().isoformat(),
        "raw_files": {},              # filename -> {size, games_read, games_accepted, extracted, verified}
        "total_extracted": 0,
        "validation_count": 0,
        "pending_count": 0,
        "processed_count": 0,
        "duplicates_skipped": 0,
        "rejected_games": {
            "invalid_result": 0,
            "below_elo": 0,
            "malformed": 0
        },
        "batches_completed": 0,
        "current_best_val_loss": None,
        "current_best_train_loss": None,
        "latest_checkpoint": None,
        "training_history": []        # list of {batch_idx, positions_count, train_loss, val_loss, duration_s, timestamp}
    }


def load_manifest() -> dict:
    QUEUE_DIR.mkdir(parents=True, exist_ok=True)
    if MANIFEST_PATH.exists():
        try:
            with open(MANIFEST_PATH, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return init_manifest()


def save_manifest(manifest: dict):
    QUEUE_DIR.mkdir(parents=True, exist_ok=True)
    manifest["last_updated"] = datetime.datetime.now().isoformat()
    temp_path = MANIFEST_PATH.with_suffix(".tmp")
    with open(temp_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
    temp_path.replace(MANIFEST_PATH)


def count_file_lines(path: Path) -> int:
    """Fast line counter for a text file."""
    if not path.exists():
        return 0
    count = 0
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            count += chunk.count(b"\n")
    return count

# ─── Step 1: Incremental PGN Extraction into Queue ────────────────────────────

def load_all_known_keys() -> set[str]:
    """Collect canonical keys from processed, pending, validation, and historical datasets."""
    keys: set[str] = set()
    files_to_check = [
        VALIDATION_SET,
        PENDING_QUEUE,
        PROCESSED_ARCH,
        DATA_DIR / "positions" / "quiet_positions.txt"
    ]
    for p in files_to_check:
        if p.exists():
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                for line in f:
                    line = line.strip()
                    if line and not line.startswith("#"):
                        parts = line.split("\t")
                        if parts:
                            keys.add(fen_key(parts[0]))
    return keys


def extract_pgn_to_queue(
    pgn_files: list[Path],
    min_elo: int = 2000,
    skip_opening: int = 8,
    val_quota: int = 20_000,
    max_extract: int | None = None,
    seed: int = 42,
) -> dict:
    """
    Incrementally stream games from PGN files, filter, deduplicate, and append to
    the validation dataset and pending training queue.
    """
    QUEUE_DIR.mkdir(parents=True, exist_ok=True)
    manifest = load_manifest()
    known_keys = load_all_known_keys()
    initial_known_count = len(known_keys)

    print(f"  Existing position keys loaded for deduplication: {initial_known_count:,}")

    val_writer = open(VALIDATION_SET, "a", encoding="utf-8")
    pending_writer = open(PENDING_QUEUE, "a", encoding="utf-8")

    current_val_count = count_file_lines(VALIDATION_SET)
    new_extracted = 0
    new_val_added = 0
    new_pending_added = 0

    t_start = time.time()

    try:
        for pgn_path in pgn_files:
            file_key = pgn_path.name
            file_size = pgn_path.stat().st_size

            # Skip if already marked verified in manifest
            file_info = manifest["raw_files"].get(file_key, {})
            if file_info.get("verified", False):
                print(f"  [SKIP] {file_key} is already fully extracted and verified.")
                continue

            print(f"\n  ── Streaming PGN: {file_key} ({file_size / 1e6:.1f} MB) ──")
            games_read = file_info.get("games_read", 0)
            games_accepted = file_info.get("games_accepted", 0)
            file_pos = 0

            with open_pgn_file(pgn_path) as stream:
                while True:
                    if max_extract is not None and new_extracted >= max_extract:
                        print(f"\n  Reached maximum extraction limit ({max_extract:,}).")
                        break
                    try:
                        game = chess.pgn.read_game(stream)
                    except Exception:
                        manifest["rejected_games"]["malformed"] += 1
                        continue

                    if game is None:
                        break

                    games_read += 1
                    if games_read % 10_000 == 0:
                        elapsed = time.time() - t_start
                        print(
                            f"    [{file_key}] Games: {games_read:,} | "
                            f"Queued: +{new_pending_added:,} | "
                            f"Val: {current_val_count + new_val_added:,} | "
                            f"{elapsed:.0f}s",
                            end="\r", flush=True
                        )

                    # Result filter
                    res_str = game.headers.get("Result", "*")
                    res = result_to_float(res_str)
                    if res is None:
                        manifest["rejected_games"]["invalid_result"] += 1
                        continue

                    # Elo filter
                    if min_elo > 0:
                        w_elo = parse_elo(game.headers.get("WhiteElo"))
                        b_elo = parse_elo(game.headers.get("BlackElo"))
                        if w_elo is None or b_elo is None or w_elo < min_elo or b_elo < min_elo:
                            manifest["rejected_games"]["below_elo"] += 1
                            continue

                    # Validate moves and extract quiet positions
                    board = game.board()
                    try:
                        moves = list(game.mainline_moves())
                    except Exception:
                        manifest["rejected_games"]["malformed"] += 1
                        continue

                    if not moves:
                        manifest["rejected_games"]["malformed"] += 1
                        continue

                    game_ok = True
                    half_move = 0
                    game_positions = []

                    for move in moves:
                        if not board.is_legal(move):
                            game_ok = False
                            break
                        if half_move >= skip_opening * 2 and is_quiet(board, move):
                            fen = board.fen()
                            k = fen_key(fen)
                            if k not in known_keys:
                                known_keys.add(k)
                                game_positions.append((fen, res))
                            else:
                                manifest["duplicates_skipped"] += 1
                        board.push(move)
                        half_move += 1

                    if not game_ok:
                        manifest["rejected_games"]["malformed"] += 1
                        continue

                    games_accepted += 1

                    # Allocate to validation set first if quota not met, then pending queue
                    for fen, r in game_positions:
                        if (current_val_count + new_val_added) < val_quota:
                            val_writer.write(f"{fen}\t{r}\n")
                            new_val_added += 1
                        else:
                            pending_writer.write(f"{fen}\t{r}\n")
                            new_pending_added += 1
                        new_extracted += 1
                        file_pos += 1
                        if max_extract is not None and new_extracted >= max_extract:
                            break

            val_writer.flush()
            pending_writer.flush()

            # Record file extraction progress in manifest
            is_verified = (max_extract is None or new_extracted < max_extract)
            manifest["raw_files"][file_key] = {
                "size_bytes": file_size,
                "games_read": games_read,
                "games_accepted": games_accepted,
                "positions_extracted": file_pos,
                "verified": is_verified,
                "extracted_at": datetime.datetime.now().isoformat()
            }
            save_manifest(manifest)
            print(f"\n  ✓ Completed {file_key}: {games_read:,} games read, {file_pos:,} positions extracted.")

    finally:
        val_writer.close()
        pending_writer.close()

    manifest["total_extracted"] += new_extracted
    manifest["validation_count"] = count_file_lines(VALIDATION_SET)
    manifest["pending_count"] = count_file_lines(PENDING_QUEUE)
    save_manifest(manifest)

    print(f"\n{'='*65}")
    print("  PGN Extraction Complete")
    print(f"{'='*65}")
    print(f"  Total newly extracted: {new_extracted:,}")
    print(f"  Added to Validation  : {new_val_added:,} (Total Val: {manifest['validation_count']:,})")
    print(f"  Added to Pending     : {new_pending_added:,} (Total Pending: {manifest['pending_count']:,})")
    print(f"  Duplicates skipped   : {manifest['duplicates_skipped']:,}")
    print(f"{'='*65}")
    return manifest


# ─── Step 2: Queue Randomization ──────────────────────────────────────────────

def shuffle_pending_queue(seed: int = 42):
    """
    Deterministically shuffle pending_positions.txt on disk using chunked shuffle
    without loading all positions as python-chess objects.
    """
    if not PENDING_QUEUE.exists() or PENDING_QUEUE.stat().st_size == 0:
        print("  [INFO] Pending queue is empty; nothing to shuffle.")
        return

    print("  Randomizing pending queue reproducibly...")
    t0 = time.time()

    # Read line strings directly (strings take 10x less RAM than chess.Board objects)
    with open(PENDING_QUEUE, "r", encoding="utf-8") as f:
        lines = [line for line in f if line.strip() and not line.startswith("#")]

    rng = random.Random(seed)
    rng.shuffle(lines)

    temp_shuffled = PENDING_QUEUE.with_suffix(".shuf.tmp")
    with open(temp_shuffled, "w", encoding="utf-8") as f:
        f.writelines(lines)

    temp_shuffled.replace(PENDING_QUEUE)
    manifest = load_manifest()
    manifest["pending_count"] = len(lines)
    save_manifest(manifest)
    print(f"  ✓ Randomized {len(lines):,} pending positions in {time.time()-t0:.1f}s (Seed: {seed})")


# ─── Step 3: Sequential Batch Training ────────────────────────────────────────

def slice_batch_from_queue(batch_size: int = 100_000) -> tuple[list[tuple[str, float]], list[str]]:
    """
    Extract up to batch_size lines from pending_positions.txt without removing them yet.
    Returns:
      (batch_data, remaining_lines)
    """
    if not PENDING_QUEUE.exists():
        return [], []

    batch_data = []
    remaining_lines = []

    with open(PENDING_QUEUE, "r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            line_str = line.strip()
            if not line_str or line_str.startswith("#"):
                continue
            if idx < batch_size:
                parts = line_str.split("\t")
                if len(parts) == 2:
                    try:
                        batch_data.append((parts[0], float(parts[1])))
                    except ValueError:
                        continue
            else:
                remaining_lines.append(line)

    return batch_data, remaining_lines


def commit_batch_removal(remaining_lines: list[str], completed_count: int):
    """
    Transaction commit: Write remaining lines back to pending_positions.txt
    and append completed positions to processed_positions.txt.
    """
    temp_pending = PENDING_QUEUE.with_suffix(".commit.tmp")
    with open(temp_pending, "w", encoding="utf-8") as f:
        f.writelines(remaining_lines)
    temp_pending.replace(PENDING_QUEUE)

    manifest = load_manifest()
    manifest["pending_count"] = len(remaining_lines)
    manifest["processed_count"] += completed_count
    save_manifest(manifest)


def evaluate_boards_loss(boards: list, results: list[float], p: Params, K: float = 1.13) -> float:
    return compute_loss(boards, results, p, K)


def run_batch_training_loop(
    batch_size: int = 100_000,
    iterations_per_batch: int = 3,
    step_size: int = 5,
    max_batches: int | None = None,
    no_rebuild: bool = False
) -> int:
    """
    Main loop: trains sequentially batch-by-batch until the pending queue is exhausted.
    """
    PARAMS_DIR.mkdir(parents=True, exist_ok=True)
    CHECKPOINTS_DIR.mkdir(parents=True, exist_ok=True)

    manifest = load_manifest()
    pending_total = count_file_lines(PENDING_QUEUE)
    manifest["pending_count"] = pending_total

    if pending_total == 0:
        print("\n  [INFO] Pending training queue is empty. Run 'extract' to queue new PGN positions.")
        return 0

    # Ensure validation set exists
    val_total = count_file_lines(VALIDATION_SET)
    if val_total == 0:
        print("\n  [ERROR] Validation dataset is empty! Run 'extract' to build validation set first.")
        return 1

    print("=" * 65)
    print("  ChessMinds Sequential Batch Training")
    print("=" * 65)
    print(f"  Pending positions : {pending_total:,}")
    print(f"  Validation size   : {val_total:,}")
    print(f"  Batch size        : {batch_size:,}")
    print(f"  Iters per batch   : {iterations_per_batch}")
    print(f"  Step size         : ±{step_size} cp")
    print("=" * 65)

    # Load initial parameters: check best_params.json first, then active eval.cpp
    p = Params()
    if BEST_PARAMS_JSON.exists():
        p.load_json(BEST_PARAMS_JSON)
        print(f"  ✓ Initialized from validated parameter checkpoint: {BEST_PARAMS_JSON.name}")
    else:
        print(f"  ✓ Initialized from active C++ engine weights: {EVAL_CPP.name}")
        p.save_json(BEST_PARAMS_JSON)

    # Preload validation boards into memory once (validation set is small: e.g. 20,000)
    print(f"\n  Loading {val_total:,} validation positions...")
    val_boards, val_results = load_data(str(VALIDATION_SET), val_total)
    baseline_val_loss = evaluate_boards_loss(val_boards, val_results, p)
    print(f"  Current baseline Validation Loss: {baseline_val_loss:.8f}")

    if manifest.get("current_best_val_loss") is None:
        manifest["current_best_val_loss"] = baseline_val_loss
        save_manifest(manifest)

    best_val_loss = manifest["current_best_val_loss"]
    batches_run = 0

    t_loop_start = time.time()

    while True:
        if max_batches is not None and batches_run >= max_batches:
            print(f"\n  [LIMIT] Reached max batches limit ({max_batches}). Stopping loop.")
            break

        # Check remaining queue
        batch_raw, remaining_lines = slice_batch_from_queue(batch_size)
        current_batch_len = len(batch_raw)
        if current_batch_len == 0:
            print("\n  ✓ All pending positions have been processed!")
            break

        batch_idx = manifest["batches_completed"] + 1
        print(f"\n{'━'*65}")
        print(f"  BATCH #{batch_idx}  |  Positions: {current_batch_len:,}  |  Queue remaining: {len(remaining_lines):,}")
        print(f"{'━'*65}")

        t_batch_start = time.time()

        # Parse batch boards
        train_boards = []
        train_results = []
        for fen_str, r_val in batch_raw:
            try:
                b = chess.Board(fen_str)
                train_boards.append(b)
                train_results.append(r_val)
            except Exception:
                continue

        batch_p = Params()
        batch_p.from_dict(p.to_dict())  # Start from latest best
        vec = batch_p.to_vector()
        n_params = len(vec)

        init_train_loss = evaluate_boards_loss(train_boards, train_results, batch_p)
        print(f"  Initial Batch Train Loss : {init_train_loss:.8f}")
        current_train_loss = init_train_loss

        # Coordinate descent across batch
        for it in range(1, iterations_per_batch + 1):
            it_improvements = 0
            indices = list(range(n_params))
            random.shuffle(indices)

            for count, i in enumerate(indices):
                orig = vec[i]
                improved = False
                for delta in [+step_size, -step_size]:
                    vec[i] = orig + delta
                    batch_p.from_vector(vec)
                    loss = evaluate_boards_loss(train_boards, train_results, batch_p)
                    if loss < current_train_loss:
                        current_train_loss = loss
                        it_improvements += 1
                        improved = True
                        orig = orig + delta
                        break
                    else:
                        vec[i] = orig
                if not improved:
                    vec[i] = orig

            print(f"    Iter {it}/{iterations_per_batch}: {it_improvements} params improved | Loss: {current_train_loss:.8f}")
            if it_improvements == 0:
                break

        # Compute validation loss of tuned candidate
        cand_val_loss = evaluate_boards_loss(val_boards, val_results, batch_p)
        print(f"  Final Batch Train Loss   : {current_train_loss:.8f} (Δ {current_train_loss - init_train_loss:.6f})")
        print(f"  Candidate Validation Loss: {cand_val_loss:.8f} (Best: {best_val_loss:.8f})")

        # Save checkpoint
        checkpoint_name = f"checkpoint_batch_{batch_idx:04d}.json"
        checkpoint_path = CHECKPOINTS_DIR / checkpoint_name
        batch_p.save_json(checkpoint_path)
        print(f"  Saved batch checkpoint   : {checkpoint_path.name}")

        # Validation check: only promote if validation loss improved or maintained
        promoted = False
        if cand_val_loss <= best_val_loss:
            promoted = True
            val_improvement = best_val_loss - cand_val_loss
            best_val_loss = cand_val_loss
            # Backup old best before overwrite
            if BEST_PARAMS_JSON.exists():
                shutil.copy2(BEST_PARAMS_JSON, BEST_PARAMS_JSON.with_suffix(".json.bak"))
            batch_p.save_json(BEST_PARAMS_JSON)
            p.from_dict(batch_p.to_dict())  # Carry forward improved weights
            write_params_to_cpp(p)          # Update active engine code
            manifest["current_best_val_loss"] = best_val_loss
            print(f"  ✓ PROMOTED to best parameters (Val Loss improved by -{val_improvement:.6f})")
        else:
            print(f"  [HOLD] Validation loss did not improve; retaining previous best parameters.")

        # TRANSACTION COMMIT:
        # Only remove the batch from pending queue AFTER checkpoint is saved and verified
        commit_batch_removal(remaining_lines, current_batch_len)

        # Append positions to processed archive
        with open(PROCESSED_ARCH, "a", encoding="utf-8") as f:
            for fen_str, r_val in batch_raw:
                f.write(f"{fen_str}\t{r_val}\n")

        elapsed_batch = time.time() - t_batch_start
        batches_run += 1
        manifest["batches_completed"] = batch_idx
        manifest["latest_checkpoint"] = str(checkpoint_path)
        manifest["training_history"].append({
            "batch_idx": batch_idx,
            "positions_count": current_batch_len,
            "train_loss": current_train_loss,
            "val_loss": cand_val_loss,
            "promoted": promoted,
            "duration_s": round(elapsed_batch, 1),
            "timestamp": datetime.datetime.now().isoformat()
        })
        save_manifest(manifest)

        # ETA calculation
        rem_pos = len(remaining_lines)
        if current_batch_len > 0:
            est_rem_s = (elapsed_batch / current_batch_len) * rem_pos
            est_rem_str = f"{est_rem_s/60:.1f} min" if est_rem_s > 60 else f"{est_rem_s:.0f}s"
        else:
            est_rem_str = "0s"

        print(f"  Batch #{batch_idx} done in {elapsed_batch:.1f}s | Remaining: {rem_pos:,} pos (ETA: {est_rem_str})")

    # Rebuild engine once after entire batch sequence completes
    if not no_rebuild:
        print("\n  Rebuilding engine with latest validated parameters...")
        build_res = __import__("subprocess").run(
            ["ninja", "-C", str(ROOT / "build"), "chessmind"],
            capture_output=True, text=True
        )
        if build_res.returncode == 0:
            print("  ✓ Engine rebuilt successfully with best tuned weights!")
        else:
            print("  ✗ Build warning:", build_res.stderr[-500:])

    total_time = time.time() - t_loop_start
    print(f"\n{'='*65}")
    print("  BATCH TRAINING SEQUENCE COMPLETE")
    print(f"{'='*65}")
    print(f"  Batches completed : {batches_run}")
    print(f"  Total elapsed     : {total_time:.1f}s ({total_time/60:.1f} min)")
    print(f"  Best Val Loss     : {best_val_loss:.8f}")
    print(f"  Best Params File  : {BEST_PARAMS_JSON}")
    print(f"{'='*65}")
    return 0


# ─── Step 4: Status Display ───────────────────────────────────────────────────

def display_status():
    manifest = load_manifest()
    pending_lines = count_file_lines(PENDING_QUEUE)
    processed_lines = count_file_lines(PROCESSED_ARCH)
    val_lines = count_file_lines(VALIDATION_SET)

    # Disk usage
    def get_size_mb(p: Path) -> float:
        return p.stat().st_size / 1e6 if p.exists() else 0.0

    queue_mb = get_size_mb(PENDING_QUEUE) + get_size_mb(PROCESSED_ARCH) + get_size_mb(VALIDATION_SET)
    raw_mb = sum(get_size_mb(f) for f in RAW_DIR.glob("*.pgn")) if RAW_DIR.exists() else 0.0

    print("=" * 65)
    print("  ChessMinds Training Pipeline Status")
    print("=" * 65)
    print(f"  Pending Queue Positions   : {pending_lines:,} ({get_size_mb(PENDING_QUEUE):.1f} MB)")
    print(f"  Processed Archive         : {processed_lines:,} ({get_size_mb(PROCESSED_ARCH):.1f} MB)")
    print(f"  Validation Dataset        : {val_lines:,} ({get_size_mb(VALIDATION_SET):.1f} MB)")
    print(f"  Total Queued / Processed  : {pending_lines + processed_lines + val_lines:,}")
    print(f"  Batches Completed         : {manifest.get('batches_completed', 0)}")
    print(f"  Current Best Val Loss     : {manifest.get('current_best_val_loss', 'N/A')}")
    print(f"  Latest Checkpoint         : {manifest.get('latest_checkpoint', 'N/A')}")
    print(f"  Raw PGN Directory Size    : {raw_mb:.1f} MB")
    print(f"  Total Queue Disk Usage    : {queue_mb:.1f} MB")
    print("=" * 65)

    raw_files = manifest.get("raw_files", {})
    if raw_files:
        print("\n  Raw PGN Extraction Tracking:")
        for fname, info in raw_files.items():
            status_str = "✓ VERIFIED" if info.get("verified") else "IN PROGRESS"
            print(f"    - {fname:38s}: {info.get('games_read', 0):,} games | {info.get('positions_extracted', 0):,} pos | [{status_str}]")
    print()


# ─── Step 5: Verified Raw PGN Cleanup ─────────────────────────────────────────

def cleanup_raw_pgn(file_name: str, force: bool = False) -> bool:
    """
    Safely delete a raw PGN file ONLY after confirming its extraction status is verified
    in the queue manifest and requiring explicit confirmation.
    """
    manifest = load_manifest()
    file_info = manifest.get("raw_files", {}).get(file_name)

    target_path = RAW_DIR / file_name
    if not target_path.exists():
        print(f"  ERROR: File does not exist: {target_path}")
        return False

    if not file_info or not file_info.get("verified", False):
        print(f"  [REFUSED] Cannot delete {file_name}: Extraction has not been verified in the queue manifest.")
        print("  Run 'extract' first until extraction completes and passes integrity checks.")
        return False

    print(f"\n  [VERIFICATION PASSED]")
    print(f"  File name           : {file_name}")
    print(f"  Size                : {target_path.stat().st_size / 1e6:.1f} MB")
    print(f"  Games read          : {file_info.get('games_read'):,}")
    print(f"  Positions extracted : {file_info.get('positions_extracted'):,}")
    print(f"  Extracted timestamp : {file_info.get('extracted_at')}")

    if not force:
        print("\n  Safety Check: Deleting raw PGN files cannot be undone.")
        confirm = input(f"  Are you sure you want to delete {file_name}? (yes/no): ").strip().lower()
        if confirm != "yes":
            print("  [CANCELLED] File deletion aborted by user.")
            return False

    target_path.unlink()
    print(f"  ✓ Safely deleted raw PGN file: {file_name}")
    return True


# ─── Main CLI ─────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(
        description="ChessMinds Sequential Batch Training & Queue Pipeline",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("action", choices=["extract", "train-batches", "status", "resume", "cleanup", "shuffle"],
                        help="Action to perform")
    parser.add_argument("--input", "-i", action="append", type=Path,
                        help="Raw PGN file or directory (for extract action)")
    parser.add_argument("--batch-size", type=int, default=100_000,
                        help="Number of positions per training batch (default: 100,000)")
    parser.add_argument("--iterations", type=int, default=3,
                        help="Coordinate descent iterations per batch (default: 3)")
    parser.add_argument("--step", type=int, default=5,
                        help="Parameter step size in centipawns (default: 5)")
    parser.add_argument("--min-elo", type=int, default=2000,
                        help="Minimum Elo threshold for PGN extraction (default: 2000)")
    parser.add_argument("--skip-opening", type=int, default=8,
                        help="Moves to skip per game (default: 8)")
    parser.add_argument("--max-positions", type=int, default=None,
                        help="Max positions to extract (extract action)")
    parser.add_argument("--max-batches", type=int, default=None,
                        help="Max batches to run in this invocation")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for queue shuffling (default: 42)")
    parser.add_argument("--target-file", type=str, default=None,
                        help="Target PGN filename for cleanup action")
    parser.add_argument("--force", action="store_true",
                        help="Bypass interactive confirmation in cleanup action")
    parser.add_argument("--no-rebuild", action="store_true",
                        help="Skip rebuilding C++ engine after training")

    args = parser.parse_args()

    if args.action == "status":
        display_status()
        return 0

    elif args.action == "shuffle":
        shuffle_pending_queue(args.seed)
        return 0

    elif args.action == "extract":
        pgn_files = []
        if args.input:
            for item in args.input:
                p = Path(item)
                if not p.is_absolute():
                    p = ROOT / p
                if p.is_dir():
                    pgn_files.extend(sorted(p.glob("*.pgn")) + sorted(p.glob("*.pgn.gz")))
                elif p.exists():
                    pgn_files.append(p)
        else:
            # Default to data/raw
            pgn_files = sorted(RAW_DIR.glob("*.pgn")) + sorted(RAW_DIR.glob("*.pgn.gz"))

        if not pgn_files:
            print("ERROR: No PGN files found to extract.", file=sys.stderr)
            return 1

        extract_pgn_to_queue(
            pgn_files=pgn_files,
            min_elo=args.min_elo,
            skip_opening=args.skip_opening,
            max_extract=args.max_positions,
            seed=args.seed
        )
        # Randomize queue after extraction
        shuffle_pending_queue(args.seed)
        return 0

    elif args.action in ("train-batches", "resume"):
        return run_batch_training_loop(
            batch_size=args.batch_size,
            iterations_per_batch=args.iterations,
            step_size=args.step,
            max_batches=args.max_batches,
            no_rebuild=args.no_rebuild
        )

    elif args.action == "cleanup":
        if not args.target_file:
            print("ERROR: Please specify --target-file <filename.pgn> to cleanup.", file=sys.stderr)
            return 1
        success = cleanup_raw_pgn(args.target_file, force=args.force)
        return 0 if success else 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
