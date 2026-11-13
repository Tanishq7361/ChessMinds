#!/usr/bin/env python3
"""
ChessMinds Phase 8: External Engine Strength & Tournament Framework
===================================================================
A professional, automated tournament and strength testing framework for ChessMinds
supporting external UCI engines (Stockfish, Berserk, Koivisto, etc.) or comparing
two versions of ChessMinds.

Features:
  - Robust subprocess management for multiple UCI engines
  - Configurable engine paths, hash size, threads, skill levels, depths, time controls
  - Paired games with color alternation from varied opening positions (FEN / JSON suite)
  - Configurable tournament rounds / game counts (50, 100, 500+ games)
  - Full PGN export with standard headers, FENs, results, and termination reasons
  - Detection of illegal moves, crashes, time forfeits, 3-fold repetition, 50-move rule
  - Comprehensive statistical calculations:
      * Win / Draw / Loss counts and score percentage
      * Average game length (plies and moves)
      * Color-separated breakdown (White score vs Black score)
      * Maximum-likelihood Elo difference with 95% Confidence Interval
  - Sequential Probability Ratio Test (SPRT) support for hypothesis testing (H0 vs H1)
  - Fully reproducible tournaments with configurable random seeds
  - Export to CSV summary table and JSON detailed report

Usage Examples:
  # 1. Quick 10-game match between ChessMinds and Stockfish:
  python tools/tournament.py --engine1 build/chessmind.exe --engine2 engines/stockfish.exe --games 10 --depth 6

  # 2. Time-controlled tournament (1000ms movetime) with PGN export:
  python tools/tournament.py --engine1 build/chessmind.exe --engine2 engines/stockfish.exe --games 50 --movetime 1000 --pgn matches.pgn

  # 3. SPRT comparison between ChessMinds Release and previous build:
  python tools/tournament.py --engine1 build/chessmind.exe --engine2 build_prev/chessmind.exe --sprt --elo0 0 --elo1 35 --games 200
"""

import argparse
import csv
import datetime
import json
import math
import os
import platform
import queue
import random
import re
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

import chess
import chess.pgn


# Default 16-opening test suite covering tactical, open, semi-open, and closed games
DEFAULT_OPENINGS: List[Tuple[str, str]] = [
    ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "Standard Start"),
    ("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "Open Game (1.e4 e5)"),
    ("rnbqkbnr/ppp1pppp/8/3p4/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 2", "Queen Pawn Game (1.d4 d5)"),
    ("rnbqkbnr/pp1ppppp/8/2p5/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "Sicilian Defense (1.e4 c5)"),
    ("rnbqkbnr/pppp1ppp/4p3/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "French Defense (1.e4 e6)"),
    ("rnbqkbnr/pp1ppppp/2p5/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "Caro-Kann Defense (1.e4 c6)"),
    ("r1bqkbnr/pppp1ppp/2n5/1B2p3/4P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", "Ruy Lopez / Spanish"),
    ("r1bqkbnr/pppp1ppp/2n5/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R b KQkq - 3 3", "Italian Game"),
    ("rnbqkb1r/ppp2ppp/4pn2/3p4/2PP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4", "Queen's Gambit Declined"),
    ("rnbq1rk1/ppp1ppbp/3p1np1/8/2PPP3/2N2N2/PP2BPPP/R1BQK2R b KQ - 1 6", "King's Indian Defense"),
    ("rnbqk2r/pppp1ppp/4pn2/8/1bPP4/2N5/PP2PPPP/R1BQKBNR w KQkq - 2 4", "Nimzo-Indian Defense"),
    ("rnbqkbnr/pppp1ppp/8/4p3/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 2", "English Opening (1.c4 e5)"),
    ("rnbqkbnr/ppp1pppp/8/3p4/2P5/5N2/PP1PPPPP/RNBQKB1R b KQkq - 1 2", "Reti Opening (1.Nf3 d5 2.c4)"),
    ("rnbqkbnr/ppp1pppp/8/3p4/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "Scandinavian Defense"),
    ("rnbqkb1r/pppppppp/5n2/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 1 2", "Alekhine Defense"),
    ("rnbqkbnr/ppp1pppp/3p4/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2", "Pirc Defense"),
]


@dataclass
class EngineConfig:
    name: str
    path: str
    depth: Optional[int] = None
    movetime_ms: Optional[int] = None
    wtime_ms: Optional[int] = None
    btime_ms: Optional[int] = None
    winc_ms: int = 0
    binc_ms: int = 0
    hash_mb: int = 64
    threads: int = 1
    skill_level: Optional[int] = None
    options: Dict[str, Any] = field(default_factory=dict)


class UCISubprocess:
    """Robust, thread-safe asynchronous UCI process wrapper."""

    def __init__(self, config: EngineConfig, timeout_sec: float = 30.0, verbose: bool = False):
        self.config = config
        self.timeout_sec = timeout_sec
        self.verbose = verbose
        self.process: Optional[subprocess.Popen] = None
        self.actual_name = config.name
        self.author = "Unknown"
        self._stdout_queue: queue.Queue = queue.Queue()
        self._reader_thread: Optional[threading.Thread] = None
        self._stop_reader = threading.Event()

    def start(self) -> None:
        if not os.path.isfile(self.config.path):
            raise FileNotFoundError(f"Engine binary '{self.config.path}' not found on disk.")

        self._stop_reader.clear()
        self.process = subprocess.Popen(
            [self.config.path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            universal_newlines=True,
        )

        self._reader_thread = threading.Thread(target=self._enqueue_output, daemon=True)
        self._reader_thread.start()

        self.send_command("uci")
        uciok = False
        t0 = time.time()
        while time.time() - t0 < 10.0:
            line = self._read_line(timeout=0.2)
            if line is None:
                continue
            if line.startswith("id name "):
                self.actual_name = line[len("id name "):].strip()
            elif line.startswith("id author "):
                self.author = line[len("id author "):].strip()
            elif line.strip() == "uciok":
                uciok = True
                break

        if not uciok:
            self.terminate()
            raise RuntimeError(f"Engine '{self.config.path}' failed to respond to 'uci' with 'uciok'.")

        # Configure standard options (Hash, Threads, Skill Level)
        if self.config.hash_mb:
            self.set_option("Hash", self.config.hash_mb)
        if self.config.threads:
            self.set_option("Threads", self.config.threads)
        if self.config.skill_level is not None:
            self.set_option("Skill Level", self.config.skill_level)
        for opt_name, opt_val in self.config.options.items():
            self.set_option(opt_name, opt_val)

        self.wait_ready()

    def _enqueue_output(self) -> None:
        if not self.process or not self.process.stdout:
            return
        try:
            for line in iter(self.process.stdout.readline, ""):
                if self._stop_reader.is_set():
                    break
                self._stdout_queue.put(line.strip())
        except Exception:
            pass

    def _read_line(self, timeout: float = 0.5) -> Optional[str]:
        try:
            return self._stdout_queue.get(timeout=timeout)
        except queue.Empty:
            return None

    def send_command(self, cmd: str) -> None:
        if not self.process or self.process.poll() is not None:
            raise RuntimeError(f"Engine '{self.actual_name}' process is not running.")
        if self.verbose:
            print(f"[{self.actual_name} >>] {cmd}")
        try:
            assert self.process.stdin is not None
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            raise RuntimeError(f"Communication pipe broke with '{self.actual_name}': {e}")

    def set_option(self, name: str, value: Any) -> None:
        self.send_command(f"setoption name {name} value {value}")

    def wait_ready(self, timeout_sec: float = 10.0) -> bool:
        self.send_command("isready")
        t0 = time.time()
        while time.time() - t0 < timeout_sec:
            line = self._read_line(timeout=0.1)
            if line and line.strip() == "readyok":
                return True
        return False

    def new_game(self) -> None:
        self.send_command("ucinewgame")
        self.wait_ready()

    def set_position(self, fen: str, moves: List[str]) -> None:
        cmd = f"position fen {fen}"
        if moves:
            cmd += " moves " + " ".join(moves)
        self.send_command(cmd)

    def get_move(
        self,
        board: chess.Board,
        wtime: Optional[int] = None,
        btime: Optional[int] = None,
        winc: int = 0,
        binc: int = 0,
    ) -> Tuple[str, Dict[str, Any]]:
        """Sends 'go' command and extracts bestmove and search metadata."""
        if not self.process or self.process.poll() is not None:
            self.start()

        # Build go arguments
        go_tokens = ["go"]
        effective_timeout = self.timeout_sec

        if self.config.movetime_ms is not None:
            go_tokens.append(f"movetime {self.config.movetime_ms}")
            effective_timeout = max(self.timeout_sec, (self.config.movetime_ms / 1000.0) + 10.0)
        elif wtime is not None and btime is not None:
            go_tokens.append(f"wtime {wtime} btime {btime} winc {winc} binc {binc}")
            side_time = wtime if board.turn == chess.WHITE else btime
            effective_timeout = max(self.timeout_sec, (side_time / 1000.0) + 10.0)
        elif self.config.depth is not None:
            go_tokens.append(f"depth {self.config.depth}")
        else:
            go_tokens.append("depth 6")

        go_cmd = " ".join(go_tokens)
        self.send_command(go_cmd)

        bestmove = None
        info_data: Dict[str, Any] = {
            "depth": None,
            "seldepth": None,
            "nodes": None,
            "nps": None,
            "score": None,
            "time_ms": None,
        }

        t_start = time.time()
        while time.time() - t_start < effective_timeout:
            line = self._read_line(timeout=0.2)
            if line is None:
                continue
            if self.verbose:
                print(f"[{self.actual_name} <<] {line}")

            if line.startswith("bestmove "):
                parts = line.split()
                if len(parts) >= 2:
                    bestmove = parts[1]
                break
            elif line.startswith("info "):
                self._parse_info_line(line, info_data)

        elapsed_ms = int((time.time() - t_start) * 1000)
        info_data["elapsed_ms"] = elapsed_ms

        if not bestmove:
            self.terminate()
            raise TimeoutError(f"Engine '{self.actual_name}' timed out after {effective_timeout:.1f}s without bestmove.")

        return bestmove, info_data

    def _parse_info_line(self, line: str, data: Dict[str, Any]) -> None:
        tokens = line.split()
        i = 1
        while i < len(tokens):
            t = tokens[i]
            if t == "depth" and i + 1 < len(tokens):
                try: data["depth"] = int(tokens[i + 1])
                except ValueError: pass
                i += 2
            elif t == "seldepth" and i + 1 < len(tokens):
                try: data["seldepth"] = int(tokens[i + 1])
                except ValueError: pass
                i += 2
            elif t == "nodes" and i + 1 < len(tokens):
                try: data["nodes"] = int(tokens[i + 1])
                except ValueError: pass
                i += 2
            elif t == "nps" and i + 1 < len(tokens):
                try: data["nps"] = int(tokens[i + 1])
                except ValueError: pass
                i += 2
            elif t == "time" and i + 1 < len(tokens):
                try: data["time_ms"] = int(tokens[i + 1])
                except ValueError: pass
                i += 2
            elif t == "score" and i + 2 < len(tokens):
                score_type = tokens[i + 1]
                try:
                    score_val = int(tokens[i + 2])
                    data["score"] = f"M{score_val}" if score_type == "mate" else f"{score_val / 100.0:+.2f}"
                except ValueError: pass
                i += 3
            else:
                i += 1

    def terminate(self) -> None:
        self._stop_reader.set()
        if self.process:
            try:
                self.send_command("quit")
            except Exception:
                pass
            try:
                self.process.terminate()
                self.process.wait(timeout=2.0)
            except Exception:
                try:
                    self.process.kill()
                except Exception:
                    pass
            try:
                if self.process.stdin:
                    self.process.stdin.close()
                if self.process.stdout:
                    self.process.stdout.close()
                if self.process.stderr:
                    self.process.stderr.close()
            except Exception:
                pass
            self.process = None


@dataclass
class GameRecord:
    game_id: int
    opening_name: str
    opening_fen: str
    white_name: str
    black_name: str
    result: str              # "1-0", "0-1", "1/2-1/2", "*"
    termination: str         # "checkmate", "stalemate", "repetition", "50-move", "illegal_move", "timeout"
    plies: int
    moves_uci: List[str]
    time_elapsed_s: float
    error_msg: Optional[str] = None


# -----------------------------------------------------------------------------
# Sequential Probability Ratio Test (SPRT)
# -----------------------------------------------------------------------------
class SPRT:
    """Sequential Probability Ratio Test for Elo testing (LLR evaluation)."""

    def __init__(self, elo0: float = 0.0, elo1: float = 35.0, alpha: float = 0.05, beta: float = 0.05):
        self.elo0 = elo0
        self.elo1 = elo1
        self.alpha = alpha
        self.beta = beta
        # Boundaries for decision
        self.lower_bound = math.log(beta / (1.0 - alpha))
        self.upper_bound = math.log((1.0 - beta) / alpha)
        self.llr = 0.0

    @staticmethod
    def elo_to_score(elo: float) -> float:
        return 1.0 / (1.0 + 10.0 ** (-elo / 400.0))

    def update(self, wins: int, draws: int, losses: int) -> Tuple[float, str]:
        n = wins + draws + losses
        if n == 0:
            return 0.0, "CONTINUE"

        s = (wins + 0.5 * draws) / n
        s0 = self.elo_to_score(self.elo0)
        s1 = self.elo_to_score(self.elo1)

        # Variance estimation
        variance = (wins * (1.0 - s) ** 2 + draws * (0.5 - s) ** 2 + losses * (0.0 - s) ** 2) / n
        if variance < 1e-4:
            variance = 0.25

        # LLR approximation for trinomial/Bernoulli match model
        delta_s = s1 - s0
        self.llr = (s - (s0 + s1) / 2.0) * delta_s / variance * n

        if self.llr >= self.upper_bound:
            return self.llr, "H1_ACCEPTED"
        elif self.llr <= self.lower_bound:
            return self.llr, "H0_ACCEPTED"
        else:
            return self.llr, "CONTINUE"


# -----------------------------------------------------------------------------
# Statistics & Elo Calculations
# -----------------------------------------------------------------------------
def calculate_elo_difference(wins: int, draws: int, losses: int) -> Tuple[float, float]:
    """Calculates maximum-likelihood Elo difference and 95% Confidence Interval error margin."""
    total = wins + draws + losses
    if total == 0:
        return 0.0, 0.0

    score = (wins + 0.5 * draws) / total

    # Clamp score to avoid singularity in log
    p = max(0.001, min(0.999, score))
    elo_diff = -400.0 * math.log10(1.0 / p - 1.0)

    # Standard error of score
    # Sample variance: s^2 = (W*(1-p)^2 + D*(0.5-p)^2 + L*(0-p)^2) / N
    var = (wins * (1.0 - score) ** 2 + draws * (0.5 - score) ** 2 + losses * (0.0 - score) ** 2) / total
    se_score = math.sqrt(var / total) if total > 1 else 0.5

    # Delta method approximation for Elo variance:
    # d(Elo)/d(p) = 400 / (ln(10) * p * (1-p))
    factor = 400.0 / (math.log(10.0) * p * (1.0 - p))
    se_elo = se_score * factor
    ci_95 = 1.96 * se_elo

    return elo_diff, ci_95


# -----------------------------------------------------------------------------
# Tournament Manager
# -----------------------------------------------------------------------------
class Tournament:
    """Orchestrates paired matches, records telemetry, and exports reports."""

    def __init__(
        self,
        config1: EngineConfig,
        config2: EngineConfig,
        openings: List[Tuple[str, str]],
        rounds: int,
        pgn_file: Optional[str] = None,
        csv_file: Optional[str] = None,
        json_file: Optional[str] = None,
        sprt_enabled: bool = False,
        elo0: float = 0.0,
        elo1: float = 35.0,
        seed: Optional[int] = None,
        verbose: bool = False,
    ):
        self.config1 = config1
        self.config2 = config2
        self.openings = openings
        self.rounds = rounds
        self.pgn_file = pgn_file
        self.csv_file = csv_file
        self.json_file = json_file
        self.sprt_enabled = sprt_enabled
        self.sprt = SPRT(elo0, elo1) if sprt_enabled else None
        self.seed = seed
        self.verbose = verbose
        self.records: List[GameRecord] = []

    def run(self) -> Dict[str, Any]:
        if self.seed is not None:
            random.seed(self.seed)

        # Build pair queue: each opening played twice with alternating colors
        pair_openings = self.openings[:]
        random.shuffle(pair_openings)

        # Scale opening pairs to reach the desired target game count
        target_games = self.rounds * 2
        schedule: List[Tuple[str, str]] = []
        while len(schedule) < self.rounds:
            for op in pair_openings:
                schedule.append(op)
                if len(schedule) >= self.rounds:
                    break

        print("\n" + "=" * 78)
        print("          ChessMinds Automated External Engine Tournament Framework       ")
        print("=" * 78)
        print(f" Engine 1:      {self.config1.name} ({self.config1.path})")
        print(f" Engine 2:      {self.config2.name} ({self.config2.path})")
        print(f" Total Pairs:   {len(schedule)} ({len(schedule) * 2} games planned)")
        print(f" Openings:      {len(self.openings)} diverse setups")
        if self.config1.movetime_ms:
            print(f" Time Control:  Fixed {self.config1.movetime_ms} ms/move")
        elif self.config1.depth:
            print(f" Search Limit:  Depth {self.config1.depth}")
        if self.sprt_enabled:
            print(f" SPRT Bounds:   H0: {self.sprt.elo0:+.1f} Elo | H1: {self.sprt.elo1:+.1f} Elo")
        print("=" * 78 + "\n")

        p1 = UCISubprocess(self.config1, verbose=self.verbose)
        p2 = UCISubprocess(self.config2, verbose=self.verbose)

        try:
            p1.start()
            p2.start()
        except Exception as e:
            print(f"[Error] Failed to initialize engines: {e}")
            p1.terminate()
            p2.terminate()
            raise

        game_idx = 0
        w1, d1, l1 = 0, 0, 0

        header_str = f"{'Game':<6} {'Pair':<6} {'White':<14} {'Black':<14} {'Result':<9} {'Plies':<6} {'Reason':<16} {'Time':<8}"
        print(header_str)
        print("-" * len(header_str))

        try:
            for pair_idx, (fen, op_name) in enumerate(schedule, 1):
                # Game A: Engine 1 is White, Engine 2 is Black
                game_idx += 1
                rec_a = self._play_game(game_idx, p1, p2, fen, op_name, self.config1.name, self.config2.name)
                self.records.append(rec_a)
                res_a = self._update_scores(rec_a, self.config1.name, w1, d1, l1)
                w1, d1, l1 = res_a
                self._print_game_line(rec_a, pair_idx)

                if self.sprt_enabled:
                    llr, decision = self.sprt.update(w1, d1, l1)
                    if decision != "CONTINUE":
                        print(f"\n[SPRT] Early stopping condition met: {decision} (LLR = {llr:.3f})")
                        break

                # Game B: Engine 2 is White, Engine 1 is Black (Color Alternation)
                game_idx += 1
                rec_b = self._play_game(game_idx, p2, p1, fen, op_name, self.config2.name, self.config1.name)
                self.records.append(rec_b)
                res_b = self._update_scores(rec_b, self.config1.name, w1, d1, l1)
                w1, d1, l1 = res_b
                self._print_game_line(rec_b, pair_idx)

                if self.sprt_enabled:
                    llr, decision = self.sprt.update(w1, d1, l1)
                    if decision != "CONTINUE":
                        print(f"\n[SPRT] Early stopping condition met: {decision} (LLR = {llr:.3f})")
                        break

        finally:
            p1.terminate()
            p2.terminate()

        summary = self._compile_summary()
        self._export_reports(summary)
        return summary

    def _play_game(
        self,
        game_id: int,
        p_white: UCISubprocess,
        p_black: UCISubprocess,
        opening_fen: str,
        opening_name: str,
        white_name: str,
        black_name: str,
    ) -> GameRecord:
        board = chess.Board(opening_fen)
        p_white.new_game()
        p_black.new_game()

        moves_uci: List[str] = []
        max_plies = 200
        t0 = time.time()
        result = "*"
        termination = "unknown"
        error_msg = None

        while not board.is_game_over() and len(moves_uci) < max_plies:
            current_engine = p_white if board.turn == chess.WHITE else p_black

            # Sync position
            current_engine.set_position(opening_fen, moves_uci)

            try:
                best_uci, info = current_engine.get_move(board)
            except Exception as e:
                result = "0-1" if board.turn == chess.WHITE else "1-0"
                termination = "engine_crash_or_timeout"
                error_msg = str(e)
                break

            # Validate move legality
            try:
                move = chess.Move.from_uci(best_uci)
                if move not in board.legal_moves:
                    result = "0-1" if board.turn == chess.WHITE else "1-0"
                    termination = "illegal_move"
                    error_msg = f"Illegal move {best_uci} in position {board.fen()}"
                    break
            except Exception:
                result = "0-1" if board.turn == chess.WHITE else "1-0"
                termination = "malformed_move"
                error_msg = f"Malformed UCI move string '{best_uci}'"
                break

            board.push(move)
            moves_uci.append(best_uci)

        elapsed = time.time() - t0

        if result == "*":
            if board.is_checkmate():
                result = "1-0" if board.turn == chess.BLACK else "0-1"
                termination = "checkmate"
            elif board.is_stalemate():
                result = "1/2-1/2"
                termination = "stalemate"
            elif board.is_insufficient_material():
                result = "1/2-1/2"
                termination = "insufficient_material"
            elif board.can_claim_threefold_repetition():
                result = "1/2-1/2"
                termination = "threefold_repetition"
            elif board.can_claim_fifty_moves():
                result = "1/2-1/2"
                termination = "fifty_moves"
            elif len(moves_uci) >= max_plies:
                result = "1/2-1/2"
                termination = "adjudication_ply_cap"

        return GameRecord(
            game_id=game_id,
            opening_name=opening_name,
            opening_fen=opening_fen,
            white_name=white_name,
            black_name=black_name,
            result=result,
            termination=termination,
            plies=len(moves_uci),
            moves_uci=moves_uci,
            time_elapsed_s=round(elapsed, 2),
            error_msg=error_msg,
        )

    def _update_scores(self, rec: GameRecord, target_name: str, w: int, d: int, l: int) -> Tuple[int, int, int]:
        if rec.result == "1/2-1/2":
            return w, d + 1, l
        elif rec.result == "1-0":
            return (w + 1, d, l) if rec.white_name == target_name else (w, d, l + 1)
        elif rec.result == "0-1":
            return (w + 1, d, l) if rec.black_name == target_name else (w, d, l + 1)
        return w, d, l

    def _print_game_line(self, rec: GameRecord, pair_idx: int) -> None:
        print(
            f"#{rec.game_id:<5} P{pair_idx:<5} {rec.white_name:<14} {rec.black_name:<14} "
            f"{rec.result:<9} {rec.plies:<6} {rec.termination:<16} {rec.time_elapsed_s:>5.1f}s"
        )

    def _compile_summary(self) -> Dict[str, Any]:
        total_games = len(self.records)
        target = self.config1.name
        opp = self.config2.name

        wins, draws, losses = 0, 0, 0
        white_wins, white_draws, white_losses = 0, 0, 0
        black_wins, black_draws, black_losses = 0, 0, 0
        total_plies = 0
        total_time = 0.0

        for r in self.records:
            total_plies += r.plies
            total_time += r.time_elapsed_s
            is_target_white = (r.white_name == target)

            if r.result == "1/2-1/2":
                draws += 1
                if is_target_white:
                    white_draws += 1
                else:
                    black_draws += 1
            elif r.result == "1-0":
                if is_target_white:
                    wins += 1
                    white_wins += 1
                else:
                    losses += 1
                    black_losses += 1
            elif r.result == "0-1":
                if is_target_white:
                    losses += 1
                    white_losses += 1
                else:
                    wins += 1
                    black_wins += 1

        score = wins + 0.5 * draws
        score_pct = (score / total_games * 100.0) if total_games > 0 else 0.0
        avg_plies = (total_plies / total_games) if total_games > 0 else 0.0
        avg_moves = avg_plies / 2.0

        elo_diff, ci_95 = calculate_elo_difference(wins, draws, losses)

        summary = {
            "engine1": target,
            "engine2": opp,
            "total_games": total_games,
            "wins": wins,
            "draws": draws,
            "losses": losses,
            "score": score,
            "score_pct": round(score_pct, 2),
            "elo_difference": round(elo_diff, 1),
            "ci_95": round(ci_95, 1),
            "white_stats": {"wins": white_wins, "draws": white_draws, "losses": white_losses},
            "black_stats": {"wins": black_wins, "draws": black_draws, "losses": black_losses},
            "avg_plies": round(avg_plies, 1),
            "avg_moves": round(avg_moves, 1),
            "total_time_s": round(total_time, 2),
            "seed": self.seed,
        }

        # Terminal reporting banner
        print("\n" + "=" * 78)
        print("                        TOURNAMENT FINAL REPORT                       ")
        print("=" * 78)
        print(f" Matchup:           {target} vs {opp}")
        print(f" Total Games:       {total_games}")
        print(f" Final Score:       {wins}W - {draws}D - {losses}L ({score:.1f}/{total_games} = {score_pct:.1f}%)")
        print(f" Elo Difference:    {elo_diff:+.1f} +/- {ci_95:.1f} (95% CI)")
        print(f" As White:          {white_wins}W - {white_draws}D - {white_losses}L")
        print(f" As Black:          {black_wins}W - {black_draws}D - {black_losses}L")
        print(f" Average Length:    {avg_plies:.1f} plies ({avg_moves:.1f} full moves)")
        print(f" Total Time:        {total_time:.1f} seconds")
        print("=" * 78 + "\n")

        return summary

    def _export_reports(self, summary: Dict[str, Any]) -> None:
        # 1. PGN Export
        if self.pgn_file:
            pgn_path = Path(self.pgn_file)
            pgn_path.parent.mkdir(parents=True, exist_ok=True)
            with open(pgn_path, "w", encoding="utf-8") as f:
                for r in self.records:
                    game = chess.pgn.Game()
                    game.headers["Event"] = "ChessMinds Engine Match"
                    game.headers["Site"] = platform.node()
                    game.headers["Date"] = datetime.datetime.now().strftime("%Y.%m.%d")
                    game.headers["Round"] = str(r.game_id)
                    game.headers["White"] = r.white_name
                    game.headers["Black"] = r.black_name
                    game.headers["Result"] = r.result
                    game.headers["Termination"] = r.termination
                    game.headers["Opening"] = r.opening_name
                    if r.opening_fen != chess.STARTING_FEN:
                        game.headers["SetUp"] = "1"
                        game.headers["FEN"] = r.opening_fen

                    node = game
                    b = chess.Board(r.opening_fen)
                    for uci_str in r.moves_uci:
                        m = chess.Move.from_uci(uci_str)
                        node = node.add_variation(m)
                        b.push(m)

                    exporter = chess.pgn.FileExporter(f)
                    game.accept(exporter)
                    f.write("\n\n")
            print(f"[Export] Saved complete tournament PGN to: {pgn_path.resolve()}")

        # 2. CSV Export
        if self.csv_file:
            csv_path = Path(self.csv_file)
            csv_path.parent.mkdir(parents=True, exist_ok=True)
            with open(csv_path, "w", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow([
                    "Game_ID", "Opening_Name", "White", "Black", "Result", 
                    "Plies", "Termination", "Time_Seconds", "Error"
                ])
                for r in self.records:
                    writer.writerow([
                        r.game_id, r.opening_name, r.white_name, r.black_name,
                        r.result, r.plies, r.termination, r.time_elapsed_s, r.error_msg or ""
                    ])
                writer.writerow([])
                writer.writerow(["# TOURNAMENT SUMMARY"])
                writer.writerow(["# Matchup", f"{summary['engine1']} vs {summary['engine2']}"])
                writer.writerow(["# Score", f"{summary['wins']}W - {summary['draws']}D - {summary['losses']}L ({summary['score_pct']}%)"])
                writer.writerow(["# Elo_Diff", f"{summary['elo_difference']} +/- {summary['ci_95']}"])
                writer.writerow(["# Avg_Plies", summary["avg_plies"]])
            print(f"[Export] Saved tournament CSV summary to:   {csv_path.resolve()}")

        # 3. JSON Export
        if self.json_file:
            json_path = Path(self.json_file)
            json_path.parent.mkdir(parents=True, exist_ok=True)
            report = {
                "summary": summary,
                "games": [
                    {
                        "id": r.game_id,
                        "opening": r.opening_name,
                        "white": r.white_name,
                        "black": r.black_name,
                        "result": r.result,
                        "termination": r.termination,
                        "plies": r.plies,
                        "time_s": r.time_elapsed_s,
                        "error": r.error_msg,
                    }
                    for r in self.records
                ],
            }
            with open(json_path, "w", encoding="utf-8") as f:
                json.dump(report, f, indent=2)
            print(f"[Export] Saved detailed tournament JSON to: {json_path.resolve()}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="ChessMinds Automated External Engine Tournament Framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--engine1", type=str, default="build/chessmind.exe", help="Path to primary engine binary")
    parser.add_argument("--name1", type=str, default="ChessMinds", help="Display name for engine 1")
    parser.add_argument("--engine2", type=str, required=True, help="Path to opponent engine binary (e.g. Stockfish)")
    parser.add_argument("--name2", type=str, default="Opponent", help="Display name for engine 2")
    parser.add_argument("--games", type=int, default=20, help="Total number of games to play (must be even, default: 20)")
    parser.add_argument("--depth", type=int, default=None, help="Fixed search depth per move for both engines")
    parser.add_argument("--depth1", type=int, default=None, help="Search depth for engine 1")
    parser.add_argument("--depth2", type=int, default=None, help="Search depth for engine 2")
    parser.add_argument("--movetime", type=int, default=None, help="Fixed movetime in milliseconds per move")
    parser.add_argument("--hash", type=int, default=64, help="Transposition table hash size in MB (default: 64)")
    parser.add_argument("--threads", type=int, default=1, help="Number of search threads (default: 1)")
    parser.add_argument("--skill2", type=int, default=None, help="UCI 'Skill Level' option for opponent (e.g., Stockfish 0-20)")
    parser.add_argument("--openings", type=str, default="benchmarks/tournament_openings.json", help="Path to openings JSON file")
    parser.add_argument("--pgn", type=str, default="tournament_games.pgn", help="Output PGN file path")
    parser.add_argument("--csv", type=str, default="tournament_summary.csv", help="Output CSV file path")
    parser.add_argument("--json", type=str, default="tournament_report.json", help="Output JSON file path")
    parser.add_argument("--sprt", action="store_true", help="Enable Sequential Probability Ratio Test (SPRT)")
    parser.add_argument("--elo0", type=float, default=0.0, help="SPRT H0 Elo boundary (default: 0.0)")
    parser.add_argument("--elo1", type=float, default=35.0, help="SPRT H1 Elo boundary (default: +35.0)")
    parser.add_argument("--seed", type=int, default=42, help="Random seed for opening selection and reproducibility")
    parser.add_argument("--verbose", action="store_true", help="Print verbose UCI protocol traffic")
    return parser.parse_args()


def load_openings(path_str: str) -> List[Tuple[str, str]]:
    p = Path(path_str)
    if not p.is_file():
        return DEFAULT_OPENINGS
    try:
        with open(p, "r", encoding="utf-8") as f:
            data = json.load(f)
            return [(item["fen"], item.get("name", "Opening")) for item in data if "fen" in item]
    except Exception:
        return DEFAULT_OPENINGS


def main() -> int:
    args = parse_args()

    # Enforce even game count for paired alternation
    total_games = args.games
    if total_games % 2 != 0:
        total_games += 1
    rounds = total_games // 2

    d1 = args.depth1 if args.depth1 is not None else args.depth
    d2 = args.depth2 if args.depth2 is not None else args.depth

    config1 = EngineConfig(
        name=args.name1,
        path=args.engine1,
        depth=d1,
        movetime_ms=args.movetime,
        hash_mb=args.hash,
        threads=args.threads,
    )
    config2 = EngineConfig(
        name=args.name2,
        path=args.engine2,
        depth=d2,
        movetime_ms=args.movetime,
        hash_mb=args.hash,
        threads=args.threads,
        skill_level=args.skill2,
    )

    openings = load_openings(args.openings)

    tourney = Tournament(
        config1=config1,
        config2=config2,
        openings=openings,
        rounds=rounds,
        pgn_file=args.pgn,
        csv_file=args.csv,
        json_file=args.json,
        sprt_enabled=args.sprt,
        elo0=args.elo0,
        elo1=args.elo1,
        seed=args.seed,
        verbose=args.verbose,
    )

    try:
        tourney.run()
        return 0
    except Exception as e:
        print(f"[Error] Tournament execution terminated with error: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
