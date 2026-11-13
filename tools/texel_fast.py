"""
ChessMinds Fast Texel Tuner — Static Eval Mode
===============================================
Faster alternative to texel_tuner.py:
Instead of calling the engine via UCI (slow), this script:
  1. Uses python-chess to load FEN positions
  2. Maintains a pure-Python mirror of the eval function
  3. Runs gradient descent entirely in Python (no rebuild per step)
  4. After convergence, writes optimal parameters back to src/eval.cpp ONCE
  5. Then rebuilds the engine once at the end

This is 100-1000x faster than texel_tuner.py and suitable for large datasets.

Usage:
  python tools/texel_fast.py [--max-positions 1000000] [--iterations 10]
"""

import chess
import re
import os
import sys
import math
import time
import random
import argparse
import shutil
import json
from pathlib import Path

ROOT     = Path(__file__).resolve().parent.parent
EVAL_CPP = ROOT / "src" / "eval.cpp"
BUILD_DIR= ROOT / "build"
BACKUP   = ROOT / "src" / "eval.cpp.fast_tuner_backup"

# ─── Parameter container ──────────────────────────────────────────────────────

class Params:
    """All tunable eval parameters as Python lists (mirrors eval.cpp)."""

    def __init__(self):
        self._load_from_cpp()

    def _load_from_cpp(self):
        src = EVAL_CPP.read_text(encoding="utf-8")

        def extract_array6(name):
            m = re.search(rf"{name}\[6\] = \{{([^}}]+)\}}", src)
            if m:
                raw = re.sub(r"//[^\n]*", "", m.group(1))
                return [int(x.strip()) for x in raw.split(',') if x.strip().lstrip('-').isdigit()]
            return [100, 320, 330, 500, 900, 0]

        def extract_pst(name):
            m = re.search(rf"constexpr int {name}\[64\] = \{{([^}}]+)\}}", src, re.DOTALL)
            if m:
                raw = re.sub(r"//[^\n]*", "", m.group(1))
                vals = [int(x.strip()) for x in raw.split(',') if x.strip().lstrip('-').isdigit()]
                if len(vals) == 64:
                    return vals
            return [0] * 64

        self.mg_pv = extract_array6("MG_PIECE_VALUES")
        self.eg_pv = extract_array6("EG_PIECE_VALUES")

        self.mg_pawn   = extract_pst("MG_PAWN_TABLE")
        self.eg_pawn   = extract_pst("EG_PAWN_TABLE")
        self.mg_knight = extract_pst("MG_KNIGHT_TABLE")
        self.eg_knight = extract_pst("EG_KNIGHT_TABLE")
        self.mg_bishop = extract_pst("MG_BISHOP_TABLE")
        self.eg_bishop = extract_pst("EG_BISHOP_TABLE")
        self.mg_rook   = extract_pst("MG_ROOK_TABLE")
        self.eg_rook   = extract_pst("EG_ROOK_TABLE")
        self.mg_queen  = extract_pst("MG_QUEEN_TABLE")
        self.eg_queen  = extract_pst("EG_QUEEN_TABLE")
        self.mg_king   = extract_pst("MG_KING_TABLE")
        self.eg_king   = extract_pst("EG_KING_TABLE")

        # Scalar bonuses (extracted by regex from eval.cpp)
        self.bishop_pair_mg    = self._extract_scalar(src, r"mg_score \+= (\d+);\s*\n\s*eg_score \+= \d+;\s*\n\s*\}\s*\n\s*if.*BLACK.*BISHOP\)", 30)
        self.bishop_pair_eg    = self._extract_scalar(src, r"mg_score \+= \d+;\s*\n\s*eg_score \+= (\d+);\s*\n\s*\}\s*\n\s*if.*BLACK.*BISHOP\)", 30)
        self.doubled_pawn_mg   = self._extract_scalar(src, r"mg_score -= (\d+) \* \(w_in_file - 1\)", 15)
        self.isolated_pawn_mg  = self._extract_scalar(src, r"mg_score -= (\d+);\s*\n\s*eg_score -= \d+;\s*\n\s*\}\s*\n\s*if.*b_in_file", 15)
        self.rook_open_mg      = self._extract_scalar(src, r"mg_score \+= (\d+);\s*\n\s*eg_score \+= \d+;\s*\n\s*\} else \{", 25)
        self.rook_semiopen_mg  = self._extract_scalar(src, r"// Semi-open file.*\n\s*mg_score \+= (\d+)", 15)
        self.phalanx_bonus     = self._extract_scalar(src, r"pop_count\(w_phalanx\) \* (\d+)", 12)
        self.passed_base       = self._extract_scalar(src, r"int bonus = (\d+) \+ r \* 10", 15)
        self.protected_passer  = self._extract_scalar(src, r"mg_score \+= (\d+);\s*\n\s*eg_score \+= 25", 15)
        self.tarrasch_mg       = self._extract_scalar(src, r"mg_score \+= (\d+);\s*\n\s*eg_score \+= 25;\s*\n\s*\}\s*\n\s*if.*b_rooks_all", 15)
        self.exchange_safety   = self._extract_scalar(src, r"mg_score -= pop_count\(w_threatened\) \* (\d+);", 20)

    def _extract_scalar(self, src, pattern, default):
        m = re.search(pattern, src)
        if m:
            return int(m.group(1))
        return default

    def to_dict(self) -> dict:
        """Export all parameters to a serializable dictionary."""
        return {
            "mg_pv": list(self.mg_pv),
            "eg_pv": list(self.eg_pv),
            "mg_pawn": list(self.mg_pawn),
            "eg_pawn": list(self.eg_pawn),
            "mg_knight": list(self.mg_knight),
            "eg_knight": list(self.eg_knight),
            "mg_bishop": list(self.mg_bishop),
            "eg_bishop": list(self.eg_bishop),
            "mg_rook": list(self.mg_rook),
            "eg_rook": list(self.eg_rook),
            "mg_queen": list(self.mg_queen),
            "eg_queen": list(self.eg_queen),
            "mg_king": list(self.mg_king),
            "eg_king": list(self.eg_king),
            "bishop_pair_mg": self.bishop_pair_mg,
            "bishop_pair_eg": self.bishop_pair_eg,
            "doubled_pawn_mg": self.doubled_pawn_mg,
            "isolated_pawn_mg": self.isolated_pawn_mg,
            "rook_open_mg": self.rook_open_mg,
            "rook_semiopen_mg": self.rook_semiopen_mg,
            "phalanx_bonus": self.phalanx_bonus,
            "passed_base": self.passed_base,
            "protected_passer": self.protected_passer,
            "tarrasch_mg": self.tarrasch_mg,
            "exchange_safety": self.exchange_safety,
        }

    def from_dict(self, d: dict):
        """Import parameters from dictionary, verifying expected fields."""
        for k, v in d.items():
            if hasattr(self, k):
                setattr(self, k, list(v) if isinstance(v, list) else v)

    def save_json(self, path: Path | str):
        """Save parameters to a JSON file."""
        p = Path(path)
        p.parent.mkdir(parents=True, exist_ok=True)
        with open(p, "w", encoding="utf-8") as f:
            json.dump(self.to_dict(), f, indent=2)

    def load_json(self, path: Path | str):
        """Load parameters from a JSON file."""
        p = Path(path)
        with open(p, "r", encoding="utf-8") as f:
            d = json.load(f)
        self.from_dict(d)

    # ── Flat vector interface ─────────────────────────────────────────────────

    def to_vector(self) -> list[int]:
        """Flatten all params to a single list."""
        v = []
        v.extend(self.mg_pv[:5])      # 5 material values (no king)
        v.extend(self.eg_pv[:5])
        v.extend(self.mg_pawn)         # 64 each
        v.extend(self.eg_pawn)
        v.extend(self.mg_knight)
        v.extend(self.eg_knight)
        v.extend(self.mg_bishop)
        v.extend(self.eg_bishop)
        v.extend(self.mg_rook)
        v.extend(self.eg_rook)
        v.extend(self.mg_queen)
        v.extend(self.eg_queen)
        v.extend(self.mg_king)
        v.extend(self.eg_king)
        # Scalar bonuses
        v.append(self.bishop_pair_mg)
        v.append(self.bishop_pair_eg)
        v.append(self.doubled_pawn_mg)
        v.append(self.isolated_pawn_mg)
        v.append(self.rook_open_mg)
        v.append(self.rook_semiopen_mg)
        v.append(self.phalanx_bonus)
        v.append(self.passed_base)
        v.append(self.protected_passer)
        v.append(self.tarrasch_mg)
        v.append(self.exchange_safety)
        return v

    def from_vector(self, v: list[int]):
        """Load params from flat list."""
        idx = 0
        self.mg_pv[:5]   = v[idx:idx+5]; idx += 5
        self.eg_pv[:5]   = v[idx:idx+5]; idx += 5
        self.mg_pawn     = v[idx:idx+64]; idx += 64
        self.eg_pawn     = v[idx:idx+64]; idx += 64
        self.mg_knight   = v[idx:idx+64]; idx += 64
        self.eg_knight   = v[idx:idx+64]; idx += 64
        self.mg_bishop   = v[idx:idx+64]; idx += 64
        self.eg_bishop   = v[idx:idx+64]; idx += 64
        self.mg_rook     = v[idx:idx+64]; idx += 64
        self.eg_rook     = v[idx:idx+64]; idx += 64
        self.mg_queen    = v[idx:idx+64]; idx += 64
        self.eg_queen    = v[idx:idx+64]; idx += 64
        self.mg_king     = v[idx:idx+64]; idx += 64
        self.eg_king     = v[idx:idx+64]; idx += 64
        self.bishop_pair_mg   = v[idx]; idx += 1
        self.bishop_pair_eg   = v[idx]; idx += 1
        self.doubled_pawn_mg  = v[idx]; idx += 1
        self.isolated_pawn_mg = v[idx]; idx += 1
        self.rook_open_mg     = v[idx]; idx += 1
        self.rook_semiopen_mg = v[idx]; idx += 1
        self.phalanx_bonus    = v[idx]; idx += 1
        self.passed_base      = v[idx]; idx += 1
        self.protected_passer = v[idx]; idx += 1
        self.tarrasch_mg      = v[idx]; idx += 1
        self.exchange_safety  = v[idx]; idx += 1

# ─── Python eval mirror ───────────────────────────────────────────────────────

PIECE_TYPE_MAP = {
    chess.PAWN: 0, chess.KNIGHT: 1, chess.BISHOP: 2,
    chess.ROOK: 3, chess.QUEEN: 4,  chess.KING: 5
}
PHASE_WEIGHTS = [0, 1, 1, 2, 4, 0]
TOTAL_PHASE   = 24

def mirror_sq(sq: int) -> int:
    return sq ^ 56

def evaluate_python(board: chess.Board, p: Params) -> float:
    """
    Pure-Python eval mirroring ChessMinds eval.cpp logic.
    Returns centipawns from White's perspective.
    """
    mg_score = 0
    eg_score = 0
    game_phase = 0

    mg_tables = [p.mg_pawn, p.mg_knight, p.mg_bishop,
                 p.mg_rook, p.mg_queen, p.mg_king]
    eg_tables = [p.eg_pawn, p.eg_knight, p.eg_bishop,
                 p.eg_rook, p.eg_queen, p.eg_king]

    for sq in chess.SQUARES:
        piece = board.piece_at(sq)
        if piece is None:
            continue
        pt = PIECE_TYPE_MAP[piece.piece_type]
        pst_sq = sq if piece.color == chess.WHITE else mirror_sq(sq)

        if piece.color == chess.WHITE:
            mg_score += p.mg_pv[pt] + mg_tables[pt][pst_sq]
            eg_score += p.eg_pv[pt] + eg_tables[pt][pst_sq]
        else:
            mg_score -= p.mg_pv[pt] + mg_tables[pt][pst_sq]
            eg_score -= p.eg_pv[pt] + eg_tables[pt][pst_sq]
        game_phase += PHASE_WEIGHTS[pt]

    # Bishop pair
    w_bishops = len(board.pieces(chess.BISHOP, chess.WHITE))
    b_bishops = len(board.pieces(chess.BISHOP, chess.BLACK))
    if w_bishops >= 2: mg_score += p.bishop_pair_mg; eg_score += p.bishop_pair_eg
    if b_bishops >= 2: mg_score -= p.bishop_pair_mg; eg_score -= p.bishop_pair_eg

    # Pawn structure
    w_pawns_bb = board.pieces(chess.PAWN, chess.WHITE)
    b_pawns_bb = board.pieces(chess.PAWN, chess.BLACK)
    for f in range(8):
        w_on_file = sum(1 for sq in w_pawns_bb if chess.square_file(sq) == f)
        b_on_file = sum(1 for sq in b_pawns_bb if chess.square_file(sq) == f)
        adj_files = [f2 for f2 in [f-1, f+1] if 0 <= f2 <= 7]

        if w_on_file > 1:
            mg_score -= p.doubled_pawn_mg * (w_on_file - 1)
            eg_score -= p.doubled_pawn_mg * (w_on_file - 1)
        if b_on_file > 1:
            mg_score += p.doubled_pawn_mg * (b_on_file - 1)
            eg_score += p.doubled_pawn_mg * (b_on_file - 1)

        w_has_adj = any(any(chess.square_file(sq) == f2 for sq in w_pawns_bb) for f2 in adj_files)
        b_has_adj = any(any(chess.square_file(sq) == f2 for sq in b_pawns_bb) for f2 in adj_files)

        if w_on_file > 0 and not w_has_adj:
            mg_score -= p.isolated_pawn_mg; eg_score -= p.isolated_pawn_mg
        if b_on_file > 0 and not b_has_adj:
            mg_score += p.isolated_pawn_mg; eg_score += p.isolated_pawn_mg

    # Rook open files
    for sq in board.pieces(chess.ROOK, chess.WHITE):
        f = chess.square_file(sq)
        w_pf = any(chess.square_file(s) == f for s in w_pawns_bb)
        b_pf = any(chess.square_file(s) == f for s in b_pawns_bb)
        if not w_pf:
            mg_score += p.rook_open_mg if not b_pf else p.rook_semiopen_mg
    for sq in board.pieces(chess.ROOK, chess.BLACK):
        f = chess.square_file(sq)
        b_pf = any(chess.square_file(s) == f for s in b_pawns_bb)
        w_pf = any(chess.square_file(s) == f for s in w_pawns_bb)
        if not b_pf:
            mg_score -= p.rook_open_mg if not w_pf else p.rook_semiopen_mg

    # Exchange safety: non-pawn pieces attacked by enemy pawns
    w_pawns = int(w_pawns_bb)
    b_pawns = int(b_pawns_bb)
    w_pawn_attacks = ((w_pawns & ~chess.BB_FILE_A) << 7) | ((w_pawns & ~chess.BB_FILE_H) << 9)
    b_pawn_attacks = ((b_pawns & ~chess.BB_FILE_H) >> 7) | ((b_pawns & ~chess.BB_FILE_A) >> 9)

    w_mm = int(board.pieces(chess.KNIGHT, chess.WHITE) | board.pieces(chess.BISHOP, chess.WHITE) |
               board.pieces(chess.ROOK, chess.WHITE) | board.pieces(chess.QUEEN, chess.WHITE))
    b_mm = int(board.pieces(chess.KNIGHT, chess.BLACK) | board.pieces(chess.BISHOP, chess.BLACK) |
               board.pieces(chess.ROOK, chess.BLACK) | board.pieces(chess.QUEEN, chess.BLACK))

    w_threatened = w_mm & b_pawn_attacks
    b_threatened = b_mm & w_pawn_attacks

    if w_threatened:
        cnt = bin(w_threatened).count("1")
        mg_score -= cnt * p.exchange_safety
        eg_score -= cnt * (p.exchange_safety * 3 // 4)
    if b_threatened:
        cnt = bin(b_threatened).count("1")
        mg_score += cnt * p.exchange_safety
        eg_score += cnt * (p.exchange_safety * 3 // 4)

    # Interpolate MG/EG
    phase = min(game_phase, TOTAL_PHASE)
    score = (mg_score * phase + eg_score * (TOTAL_PHASE - phase)) // TOTAL_PHASE

    # Return from side-to-move perspective
    return float(score if board.turn == chess.WHITE else -score)

# ─── Loss ─────────────────────────────────────────────────────────────────────

def sigmoid(x: float, K: float = 1.13) -> float:
    return 1.0 / (1.0 + math.pow(10.0, -K * x / 400.0))

def compute_loss(boards: list, results: list[float], p: Params, K: float) -> float:
    total = 0.0
    for board, result in zip(boards, results):
        ev = evaluate_python(board, p)
        # Flip result to side-to-move perspective for loss
        actual = result if board.turn == chess.WHITE else 1.0 - result
        pred = sigmoid(ev, K)
        total += (actual - pred) ** 2
    return total / len(boards)

# ─── Load Data ────────────────────────────────────────────────────────────────

def load_data(path: str, max_count: int) -> tuple[list, list[float]]:
    boards, results = [], []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                continue
            try:
                board = chess.Board(parts[0])
                result = float(parts[1])
                boards.append(board)
                results.append(result)
            except Exception:
                continue
            if len(boards) % 50_000 == 0:
                print(f"    Loaded {len(boards):,} positions...", end="\r", flush=True)
            if len(boards) >= max_count:
                break
    return boards, results

# ─── Write back to eval.cpp ───────────────────────────────────────────────────

def write_params_to_cpp(p: Params):
    """Write all tuned parameters back to src/eval.cpp."""
    src = EVAL_CPP.read_text(encoding="utf-8")

    def fmt_pst(vals):
        rank_comments = ["Rank 1","Rank 2","Rank 3","Rank 4","Rank 5","Rank 6","Rank 7","Rank 8"]
        rows = []
        for rank in range(8):
            row = vals[rank*8:(rank+1)*8]
            comma = "," if rank < 7 else ""
            rows.append("    " + ", ".join(f"{v:4d}" for v in row) + f"{comma}  // {rank_comments[rank]}")
        return "\n" + "\n".join(rows) + "\n"

    pst_map = {
        "MG_PAWN_TABLE": p.mg_pawn, "EG_PAWN_TABLE": p.eg_pawn,
        "MG_KNIGHT_TABLE": p.mg_knight, "EG_KNIGHT_TABLE": p.eg_knight,
        "MG_BISHOP_TABLE": p.mg_bishop, "EG_BISHOP_TABLE": p.eg_bishop,
        "MG_ROOK_TABLE": p.mg_rook,   "EG_ROOK_TABLE": p.eg_rook,
        "MG_QUEEN_TABLE": p.mg_queen, "EG_QUEEN_TABLE": p.eg_queen,
        "MG_KING_TABLE": p.mg_king,   "EG_KING_TABLE": p.eg_king,
    }

    for name, vals in pst_map.items():
        pattern = rf"(constexpr int {name}\[64\] = \{{)[^}}]+(\}})"
        src = re.sub(pattern, r"\g<1>" + fmt_pst(vals) + r"\g<2>", src, flags=re.DOTALL)

    # Material values
    mg_str = " " + ", ".join(str(v) for v in p.mg_pv) + " "
    eg_str = " " + ", ".join(str(v) for v in p.eg_pv) + " "
    src = re.sub(r"(MG_PIECE_VALUES\[6\] = \{)[^}]+(\})", r"\g<1>" + mg_str + r"\g<2>", src)
    src = re.sub(r"(EG_PIECE_VALUES\[6\] = \{)[^}]+(\})", r"\g<1>" + eg_str + r"\g<2>", src)

    # Scalar bonuses
    src = re.sub(r"(mg_score -= pop_count\(w_threatened\) \* )\d+;", rf"\g<1>{p.exchange_safety};", src)
    src = re.sub(r"(eg_score -= pop_count\(w_threatened\) \* )\d+;", rf"\g<1>{p.exchange_safety * 3 // 4};", src)
    src = re.sub(r"(mg_score \+= pop_count\(b_threatened\) \* )\d+;", rf"\g<1>{p.exchange_safety};", src)
    src = re.sub(r"(eg_score \+= pop_count\(b_threatened\) \* )\d+;", rf"\g<1>{p.exchange_safety * 3 // 4};", src)

    EVAL_CPP.write_text(src, encoding="utf-8")
    print(f"  Wrote tuned parameters to {EVAL_CPP}")

# ─── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="ChessMinds Fast Texel Tuner")
    parser.add_argument("--positions",     default="data/positions/quiet_positions.txt",
                        help="Path to training positions file (default: data/positions/quiet_positions.txt)")
    parser.add_argument("--max-positions", type=int, default=1_000_000,
                        help="Max positions to load for tuning (default: 1,000,000)")
    parser.add_argument("--K",             type=float, default=1.13,
                        help="Sigmoid scaling constant (default: 1.13)")
    parser.add_argument("--iterations",    type=int, default=10,
                        help="Number of coordinate descent iterations (default: 10)")
    parser.add_argument("--step",          type=int, default=5,
                        help="Parameter adjustment step size in centipawns (default: 5)")
    parser.add_argument("--params-file",   default=None,
                        help="Optional JSON file with initial parameters (default: read from current src/eval.cpp)")
    parser.add_argument("--output-params", default=None,
                        help="Optional JSON file to save final tuned parameters to")
    parser.add_argument("--export-params", default=None,
                        help="Export current parameters to JSON file and exit without training")
    parser.add_argument("--no-rebuild",    action="store_true",
                        help="Skip rebuilding the C++ engine after tuning")
    args = parser.parse_args()

    # ── Export parameters mode ──────────────────────────────────────────────
    if args.export_params:
        p = Params()
        if args.params_file:
            pf = Path(args.params_file)
            if not pf.is_absolute():
                pf = ROOT / pf
            p.load_json(pf)
            print(f"Loaded parameters from {pf}")
        out = Path(args.export_params)
        if not out.is_absolute():
            out = ROOT / out
        p.save_json(out)
        print(f"Exported parameters to {out}")
        return

    pos_path = Path(args.positions)
    if not pos_path.is_absolute():
        pos_path = ROOT / pos_path

    if not pos_path.exists():
        print(f"ERROR: {pos_path} not found. Run tools/pgn_integrate.py first.")
        sys.exit(1)

    print("=" * 70)
    print("  ChessMinds Fast Texel Tuner (Python eval mirror)")
    print("=" * 70)
    print(f"  Positions   : {pos_path}")
    print(f"  Max pos     : {args.max_positions:,}")
    print(f"  K           : {args.K}")
    print(f"  Iterations  : {args.iterations}")
    print(f"  Step        : ±{args.step} cp")
    if args.params_file:
        print(f"  Init params : {args.params_file} (checkpoint resume)")
    else:
        print(f"  Init params : {EVAL_CPP} (current tuned engine weights)")
    if args.output_params:
        print(f"  Save params : {args.output_params}")
    print("=" * 70)

    shutil.copy(EVAL_CPP, BACKUP)
    print(f"  Backup: {BACKUP}\n")

    print("  Loading positions...")
    t0 = time.time()
    boards, results = load_data(str(pos_path), args.max_positions)
    print(f"\n  Loaded {len(boards):,} positions in {time.time()-t0:.1f}s")

    p = Params()
    if args.params_file:
        pf = Path(args.params_file)
        if not pf.is_absolute():
            pf = ROOT / pf
        if not pf.exists():
            print(f"ERROR: Parameters file not found: {pf}")
            sys.exit(1)
        p.load_json(pf)
        print(f"  ✓ Initialized parameters from checkpoint: {pf}")
    else:
        print(f"  ✓ Initialized parameters from active engine source: {EVAL_CPP}")

    vec = p.to_vector()
    n_params = len(vec)
    print(f"  Tunable parameters: {n_params:,}")

    print("\n  Computing initial loss...")
    t0 = time.time()
    baseline_loss = compute_loss(boards, results, p, args.K)
    print(f"  Initial MSE loss: {baseline_loss:.8f}  ({time.time()-t0:.1f}s)")

    best_loss = baseline_loss
    total_improvements = 0

    for iteration in range(1, args.iterations + 1):
        print(f"\n{'─'*70}")
        print(f"  ITERATION {iteration}/{args.iterations}  |  Loss: {best_loss:.8f}")
        print(f"{'─'*70}")

        iter_start = time.time()
        iter_improvements = 0
        indices = list(range(n_params))
        random.shuffle(indices)

        for count, i in enumerate(indices):
            original = vec[i]
            improved = False

            for delta in [+args.step, -args.step]:
                vec[i] = original + delta
                p.from_vector(vec)
                loss = compute_loss(boards, results, p, args.K)

                if loss < best_loss:
                    best_loss = loss
                    iter_improvements += 1
                    total_improvements += 1
                    improved = True
                    original = original + delta
                    if count % 100 == 0:
                        print(f"    param {i:4d}: {original-delta:+5d}→{original:+5d}  "
                              f"loss={best_loss:.8f}  ({iter_improvements} improv)")
                    break
                else:
                    vec[i] = original

            if not improved:
                vec[i] = original

            if count % 500 == 0:
                elapsed = time.time() - iter_start
                remaining = elapsed / (count+1) * (n_params - count - 1)
                print(f"  Progress: {count+1:4d}/{n_params}  "
                      f"improved: {iter_improvements}  "
                      f"eta: {remaining:.0f}s", end="\r")

        print()
        p.from_vector(vec)
        print(f"\n  Iteration {iteration}: {iter_improvements} improvements | "
              f"loss={best_loss:.8f} | {time.time()-iter_start:.0f}s")

        # Write checkpoint
        write_params_to_cpp(p)
        shutil.copy(EVAL_CPP, ROOT / "src" / f"eval.cpp.iter{iteration}")
        if args.output_params:
            iter_json = Path(args.output_params).with_suffix(f".iter{iteration}.json")
            p.save_json(iter_json)

        if iter_improvements == 0:
            print("  Converged — stopping early.")
            break

    # Final write & optional json save
    write_params_to_cpp(p)
    if args.output_params:
        out_path = Path(args.output_params)
        if not out_path.is_absolute():
            out_path = ROOT / out_path
        p.save_json(out_path)
        print(f"  ✓ Saved final tuned parameters to: {out_path}")

    if not args.no_rebuild:
        print("\n  Rebuilding engine with tuned parameters...")
        result = __import__("subprocess").run(
            ["ninja", "-C", str(BUILD_DIR), "chessmind"],
            capture_output=True, text=True
        )
        if result.returncode == 0:
            print("  ✓ Engine rebuilt successfully!")
        else:
            print("  ✗ Build failed:", result.stderr[-500:])
    else:
        print("\n  Skipped engine rebuild (--no-rebuild)")

    print()
    print("=" * 70)
    print(f"  TUNING COMPLETE")
    print(f"  Initial loss : {baseline_loss:.8f}")
    print(f"  Final loss   : {best_loss:.8f}")
    delta = baseline_loss - best_loss
    print(f"  Improvement  : -{delta:.8f} ({delta/baseline_loss*100:.2f}%)")
    print(f"  Total params : {total_improvements} improved")
    print("=" * 70)

if __name__ == "__main__":
    main()

