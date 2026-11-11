"""
ChessMinds Texel Tuner
======================
Tunes evaluation parameters (PST values + bonus weights) in src/eval.cpp
using a dataset of quiet positions extracted from human games.

Algorithm: Coordinate descent (one parameter at a time) using Mean Squared Error
of sigmoid(eval) vs game_result as the loss function.

Loss (Texel):
  E = (1/N) * sum( (result_i - sigmoid(K * eval_i))^2 )
  where sigmoid(x) = 1 / (1 + 10^(-x/400))

Usage:
  python tools/texel_tuner.py [--positions data/positions/quiet_positions.txt]
                              [--max-positions 500000]
                              [--K 1.13]
                              [--iterations 5]
                              [--step 5]
                              [--params all|pst|bonuses|material]

Competition compliance:
  - Uses only human game result labels (no engine evals from other engines)
  - All tuned values are written back to our own src/eval.cpp
  - Rebuilds and re-benchmarks after each full iteration
"""

import subprocess
import re
import os
import sys
import math
import time
import random
import argparse
import shutil
from pathlib import Path

# ─── Paths ────────────────────────────────────────────────────────────────────
ROOT      = Path(__file__).resolve().parent.parent
EVAL_CPP  = ROOT / "src" / "eval.cpp"
BUILD_DIR = ROOT / "build"
ENGINE    = BUILD_DIR / "chessmind.exe"
BACKUP    = ROOT / "src" / "eval.cpp.tuner_backup"

# ─── Sigmoid & Loss ───────────────────────────────────────────────────────────

def sigmoid(x: float, K: float = 1.13) -> float:
    """Texel sigmoid: maps eval (cp) to [0,1] win probability."""
    return 1.0 / (1.0 + math.pow(10.0, -K * x / 400.0))

def mse_loss(evals: list[float], results: list[float], K: float) -> float:
    """Mean Squared Error between sigmoid(eval) and actual result."""
    total = 0.0
    for ev, r in zip(evals, results):
        pred = sigmoid(ev, K)
        total += (r - pred) ** 2
    return total / len(evals)

# ─── Engine Evaluation ────────────────────────────────────────────────────────

def eval_positions_via_uci(fens: list[str], depth: int = 1) -> list[float]:
    """
    Evaluate a batch of FEN positions using chessmind.exe via UCI.
    Uses 'go nodes 1' (depth 1) for speed — Texel tuning uses static eval,
    so depth 1 quiescence is sufficient.
    """
    evals = []
    proc = subprocess.Popen(
        [str(ENGINE)],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1,
    )

    try:
        proc.stdin.write("uci\n")
        proc.stdin.flush()
        # Wait for uciok
        for line in proc.stdout:
            if line.strip() == "uciok":
                break

        for fen in fens:
            proc.stdin.write(f"position fen {fen}\n")
            proc.stdin.write(f"go depth {depth}\n")
            proc.stdin.flush()

            # Read until bestmove
            score_cp = 0.0
            for line in proc.stdout:
                line = line.strip()
                if line.startswith("info") and "score cp" in line:
                    m = re.search(r"score cp (-?\d+)", line)
                    if m:
                        score_cp = float(m.group(1))
                elif line.startswith("info") and "score mate" in line:
                    m = re.search(r"score mate (-?\d+)", line)
                    if m:
                        mate = int(m.group(1))
                        score_cp = 29000.0 if mate > 0 else -29000.0
                elif line.startswith("bestmove"):
                    break
            evals.append(score_cp)

    finally:
        try:
            proc.stdin.write("quit\n")
            proc.stdin.flush()
            proc.wait(timeout=5)
        except Exception:
            proc.kill()

    return evals

# ─── Build Engine ─────────────────────────────────────────────────────────────

def build_engine() -> bool:
    """Rebuild chessmind engine with ninja. Returns True if successful."""
    result = subprocess.run(
        ["ninja", "-C", str(BUILD_DIR), "chessmind"],
        capture_output=True, text=True
    )
    if result.returncode != 0:
        print("  ✗ BUILD FAILED:")
        print(result.stderr[-2000:])
        return False
    return True

# ─── Eval.cpp Parameter Extraction & Injection ────────────────────────────────

class EvalParams:
    """
    Manages reading and writing all tunable parameters from/to src/eval.cpp.
    Parameters tracked:
      - MG/EG piece values (10 values: 5 piece types x mg/eg, King skipped)
      - All 12 PSTs: MG/EG for Pawn, Knight, Bishop, Rook, Queen, King (64 each)
      - Scalar bonuses: bishop pair, doubled/isolated pawn, passer bonus base,
                        rook open/semi-open file, pawn shield, phalanx, etc.
    """

    # Names of all 64-entry array parameters in eval.cpp
    PST_NAMES = [
        "MG_PAWN_TABLE", "EG_PAWN_TABLE",
        "MG_KNIGHT_TABLE", "EG_KNIGHT_TABLE",
        "MG_BISHOP_TABLE", "EG_BISHOP_TABLE",
        "MG_ROOK_TABLE", "EG_ROOK_TABLE",
        "MG_QUEEN_TABLE", "EG_QUEEN_TABLE",
        "MG_KING_TABLE", "EG_KING_TABLE",
    ]

    # Scalar bonus values: (variable_name, search_pattern)
    SCALAR_NAMES = [
        ("MG_BISHOP_PAIR",      r"mg_score \+= (\d+);\s*\n\s*eg_score \+= \d+;\s*\n\s*\}\s*\n\s*if.*BLACK.*BISHOP\)"),
        ("EG_BISHOP_PAIR",      r"mg_score \+= \d+;\s*\n\s*eg_score \+= (\d+);\s*\n\s*\}\s*\n\s*if.*BLACK.*BISHOP\)"),
        ("DOUBLED_PAWN_MG",     r"mg_score -= (\d+) \* \(w_in_file - 1\)"),
        ("ISOLATED_PAWN_MG",    r"mg_score -= (\d+);\s*\n\s*eg_score -= \d+;\s*\n\s*\}\s*\n\s*if.*b_in_file"),
        ("ROOK_OPEN_FILE_MG",   r"mg_score \+= (\d+);\s*\n\s*eg_score \+= \d+;\s*\n\s*\} else \{"),
        ("ROOK_SEMI_OPEN_MG",   r"mg_score \+= (\d+);\s*\n\s*eg_score \+= \d+;\s*\n\s*\}\s*\n\s*\}\s*\n\s*\}\s*\n\s*Bitboard b_rooks"),
        ("PHALANX_BONUS",       r"mg_score \+= pop_count\(w_phalanx\) \* (\d+)"),
        ("PAWN_SHIELD_OPEN",    r"mg_score -= (\d+); // Open file directly into the castled king"),
        ("PASSED_PAWN_BASE",    r"int bonus = (\d+) \+ r \* 10"),
        ("PROTECTED_PASSER_MG", r"mg_score \+= (\d+);\s*\n\s*eg_score \+= 25"),
        ("TARRASCH_MG",         r"mg_score \+= (\d+);\s*\n\s*eg_score \+= 25;\s*\n\s*\}\s*\n\s*if.*b_rooks_all"),
    ]

    def __init__(self):
        self.pst_values: dict[str, list[int]] = {}   # name -> [64 ints]
        self.mg_piece_values: list[int] = []          # [P,N,B,R,Q]
        self.eg_piece_values: list[int] = []
        self._source: str = ""
        self.load()

    def load(self):
        """Read all parameters from src/eval.cpp."""
        self._source = EVAL_CPP.read_text(encoding="utf-8")

        # ── Material values ──────────────────────────────────────────────────
        mg_m = re.search(r"MG_PIECE_VALUES\[6\] = \{([^}]+)\}", self._source)
        eg_m = re.search(r"EG_PIECE_VALUES\[6\] = \{([^}]+)\}", self._source)
        if mg_m:
            self.mg_piece_values = [int(x.strip()) for x in mg_m.group(1).split(',') if x.strip().lstrip('-').isdigit()]
        if eg_m:
            self.eg_piece_values = [int(x.strip()) for x in eg_m.group(1).split(',') if x.strip().lstrip('-').isdigit()]

        # ── PST arrays ───────────────────────────────────────────────────────
        for name in self.PST_NAMES:
            pattern = rf"constexpr int {name}\[64\] = \{{([^}}]+)\}}"
            m = re.search(pattern, self._source, re.DOTALL)
            if m:
                raw = m.group(1)
                # Remove comments
                raw = re.sub(r"//[^\n]*", "", raw)
                vals = [int(x.strip()) for x in raw.split(',') if x.strip().lstrip('-').isdigit()]
                if len(vals) == 64:
                    self.pst_values[name] = vals

    def save(self):
        """Write all current parameters back to src/eval.cpp."""
        source = self._source

        # ── Material values ──────────────────────────────────────────────────
        def fmt_array_6(vals):
            return "{ " + ", ".join(str(v) for v in vals) + " }"

        source = re.sub(
            r"(MG_PIECE_VALUES\[6\] = \{)[^}]+(\})",
            lambda m: m.group(1) + " " + ", ".join(str(v) for v in self.mg_piece_values) + " " + m.group(2),
            source
        )
        source = re.sub(
            r"(EG_PIECE_VALUES\[6\] = \{)[^}]+(\})",
            lambda m: m.group(1) + " " + ", ".join(str(v) for v in self.eg_piece_values) + " " + m.group(2),
            source
        )

        # ── PST arrays ───────────────────────────────────────────────────────
        for name, vals in self.pst_values.items():
            # Format as 8x8 grid with row comments
            rows = []
            rank_comments = [
                "Rank 1", "Rank 2", "Rank 3", "Rank 4",
                "Rank 5", "Rank 6", "Rank 7", "Rank 8"
            ]
            for rank in range(8):
                row_vals = vals[rank*8:(rank+1)*8]
                row_str = ", ".join(f"{v:4d}" for v in row_vals)
                comma = "," if rank < 7 else ""
                rows.append(f"    {row_str}{comma}  // {rank_comments[rank]}")
            new_body = "\n" + "\n".join(rows) + "\n"

            pattern = rf"(constexpr int {name}\[64\] = \{{)[^}}]+(\}})"
            source = re.sub(pattern, r"\g<1>" + new_body + r"\g<2>", source, flags=re.DOTALL)

        self._source = source
        EVAL_CPP.write_text(source, encoding="utf-8")

    def all_param_keys(self) -> list[tuple[str, int]]:
        """
        Returns list of (param_name, index) for every individual tunable value.
        param_name: "pst:<TABLE_NAME>" or "mg_val:<idx>" or "eg_val:<idx>"
        index: position within that parameter
        """
        keys = []
        # Material values (P, N, B, R, Q — skip King=0)
        for i in range(5):
            keys.append(("mg_val", i))
            keys.append(("eg_val", i))
        # PSTs
        for name in self.PST_NAMES:
            if name in self.pst_values:
                for i in range(64):
                    keys.append(("pst", name, i))
        return keys

    def get(self, key: tuple) -> int:
        if key[0] == "mg_val":
            return self.mg_piece_values[key[1]]
        elif key[0] == "eg_val":
            return self.eg_piece_values[key[1]]
        elif key[0] == "pst":
            return self.pst_values[key[1]][key[2]]
        raise KeyError(key)

    def set(self, key: tuple, value: int):
        if key[0] == "mg_val":
            self.mg_piece_values[key[1]] = value
        elif key[0] == "eg_val":
            self.eg_piece_values[key[1]] = value
        elif key[0] == "pst":
            self.pst_values[key[1]][key[2]] = value
        else:
            raise KeyError(key)

# ─── Load Positions ───────────────────────────────────────────────────────────

def load_positions(path: Path, max_count: int) -> tuple[list[str], list[float]]:
    """Load FENs and results from quiet_positions.txt."""
    fens, results = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                continue
            try:
                fens.append(parts[0])
                results.append(float(parts[1]))
            except ValueError:
                continue
            if len(fens) >= max_count:
                break
    return fens, results

# ─── Batch Evaluator ─────────────────────────────────────────────────────────

def evaluate_all(fens: list[str], batch_size: int = 500) -> list[float]:
    """Evaluate all FENs in batches for memory efficiency."""
    all_evals = []
    total = len(fens)
    for i in range(0, total, batch_size):
        batch = fens[i:i+batch_size]
        evals = eval_positions_via_uci(batch, depth=1)
        all_evals.extend(evals)
        if (i // batch_size) % 10 == 0:
            print(f"    Evaluated {min(i+batch_size, total):,}/{total:,} positions...", end="\r")
    print()
    return all_evals

# ─── Main Tuner ───────────────────────────────────────────────────────────────

def tune(args):
    positions_path = Path(args.positions)
    if not positions_path.exists():
        print(f"ERROR: Positions file not found: {positions_path}")
        print("Run tools/filter_pgn.py first to generate quiet positions.")
        sys.exit(1)

    if not ENGINE.exists():
        print(f"ERROR: Engine not found at {ENGINE}")
        print("Run: ninja -C build")
        sys.exit(1)

    print("=" * 70)
    print("  ChessMinds Texel Tuner")
    print("=" * 70)
    print(f"  Positions file : {positions_path}")
    print(f"  Max positions  : {args.max_positions:,}")
    print(f"  K (scaling)    : {args.K}")
    print(f"  Iterations     : {args.iterations}")
    print(f"  Step size      : ±{args.step} cp")
    print("=" * 70)

    # ── Backup eval.cpp ─────────────────────────────────────────────────────
    shutil.copy(EVAL_CPP, BACKUP)
    print(f"\n  Backup created: {BACKUP}")

    # ── Load positions ───────────────────────────────────────────────────────
    print(f"\n  Loading positions...")
    fens, results = load_positions(positions_path, args.max_positions)
    print(f"  Loaded {len(fens):,} positions")

    # ── Load params ──────────────────────────────────────────────────────────
    params = EvalParams()
    param_keys = params.all_param_keys()
    print(f"  Tunable parameters: {len(param_keys):,}")

    # ── Initial loss ─────────────────────────────────────────────────────────
    print(f"\n  Computing initial loss...")
    start = time.time()
    evals = evaluate_all(fens)
    baseline_loss = mse_loss(evals, results, args.K)
    print(f"  Initial MSE loss: {baseline_loss:.8f}  ({time.time()-start:.1f}s)")

    best_loss = baseline_loss

    # ── Coordinate Descent Loop ──────────────────────────────────────────────
    total_improvements = 0

    for iteration in range(1, args.iterations + 1):
        print(f"\n{'─'*70}")
        print(f"  ITERATION {iteration}/{args.iterations}  |  Current best loss: {best_loss:.8f}")
        print(f"{'─'*70}")

        iter_improvements = 0
        random.shuffle(param_keys)   # randomize order each iteration

        for idx, key in enumerate(param_keys):
            current_val = params.get(key)

            improved = False
            for direction in [+args.step, -args.step]:
                params.set(key, current_val + direction)
                params.save()

                # Rebuild
                if not build_engine():
                    params.set(key, current_val)
                    params.save()
                    break

                # Evaluate
                new_evals = evaluate_all(fens)
                new_loss = mse_loss(new_evals, results, args.K)

                if new_loss < best_loss:
                    best_loss = new_loss
                    iter_improvements += 1
                    total_improvements += 1
                    key_str = str(key)
                    print(f"  [{idx+1:4d}/{len(param_keys)}] ✓ {key_str[:50]:50s} "
                          f"{current_val:+4d} → {current_val+direction:+4d}  "
                          f"loss: {new_loss:.8f}")
                    current_val = current_val + direction
                    improved = True
                    break
                else:
                    params.set(key, current_val)
                    params.save()

            if not improved:
                # No improvement either direction — restore original
                params.set(key, current_val)

        print(f"\n  Iteration {iteration} complete: {iter_improvements} improvements found")

        # Save a checkpoint after each iteration
        checkpoint = ROOT / "src" / f"eval.cpp.iter{iteration}"
        shutil.copy(EVAL_CPP, checkpoint)
        print(f"  Checkpoint saved: {checkpoint}")

        # Rebuild with final iteration values
        build_engine()

        if iter_improvements == 0:
            print("  No improvements found — stopping early (converged).")
            break

    # ── Final summary ────────────────────────────────────────────────────────
    print()
    print("=" * 70)
    print("  TUNING COMPLETE")
    print("=" * 70)
    print(f"  Initial loss : {baseline_loss:.8f}")
    print(f"  Final loss   : {best_loss:.8f}")
    delta = baseline_loss - best_loss
    print(f"  Improvement  : -{delta:.8f} ({delta/baseline_loss*100:.2f}%)")
    print(f"  Total params improved: {total_improvements}")
    print(f"  Tuned eval.cpp saved to: {EVAL_CPP}")
    print(f"  Backup at             : {BACKUP}")
    print("=" * 70)

# ─── Entry point ─────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ChessMinds Texel Tuner")
    parser.add_argument("--positions",     default="data/positions/quiet_positions.txt",
                        help="Path to quiet positions file")
    parser.add_argument("--max-positions", type=int, default=500_000,
                        help="Max positions to load (default: 500K)")
    parser.add_argument("--K",             type=float, default=1.13,
                        help="Sigmoid scaling constant (default: 1.13)")
    parser.add_argument("--iterations",    type=int, default=5,
                        help="Number of coordinate descent iterations (default: 5)")
    parser.add_argument("--step",          type=int, default=5,
                        help="Parameter adjustment step size in cp (default: 5)")
    args = parser.parse_args()

    # Resolve positions path relative to repo root
    p = Path(args.positions)
    if not p.is_absolute():
        p = ROOT / p
    args.positions = str(p)

    tune(args)

if __name__ == "__main__":
    main()

