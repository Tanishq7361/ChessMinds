#!/usr/bin/env python3
"""
ChessMinds UCI-Based Benchmarking Framework
===========================================
A professional, automated benchmarking framework for the modern C++20 ChessMinds
engine communicating via the standard Universal Chess Interface (UCI) protocol.

Features:
  - Subprocess UCI management with full crash, timeout, and malformed line recovery
  - Configurable search depth and movetime limits
  - Automated multi-position testing with repeated rounds
  - Comprehensive telemetry: depth, seldepth, nodes, NPS, time, score, PV, bestmove
  - Statistical calculations: average NPS, median NPS, weighted NPS, nodes, time
  - Output formats: Rich live console table, CSV raw results, JSON summary report
  - Cross-platform support (Windows PowerShell, Linux, macOS)

PowerShell Usage Examples:
  # 1. Run standard benchmark at default depth (depth 7):
  python tools/benchmark.py

  # 2. Benchmark using custom FEN suite at depth 8 with 3 repeated rounds:
  python tools/benchmark.py --fens data/benchmark_suite.fen --depth 8 --rounds 3

  # 3. Benchmark with fixed 500ms time limit per position:
  python tools/benchmark.py --movetime 500

  # 4. Save custom CSV and JSON summary reports:
  python tools/benchmark.py --depth 8 --csv reports/bench.csv --json reports/bench.json

  # 5. Specify custom engine executable path:
  python tools/benchmark.py --engine build/chessmind.exe --depth 9
"""

import argparse
import csv
import datetime
import json
import os
import platform
import queue
import re
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Default built-in benchmark positions when no FEN file is provided
DEFAULT_BENCHMARK_POSITIONS: List[Tuple[str, str]] = [
    ("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "Startpos (Opening)"),
    ("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", "KiwiPete (Complex Tactics)"),
    ("r1bqkb1r/pppp1ppp/2n5/4p3/2B1n3/5N2/PPPP1PPP/RNBQK2R w KQkq - 0 5", "Italian Game (Open Tactics)"),
    ("r2q1rk1/ppp2ppp/2n1bn2/2b1p3/3pP3/3P1NPP/PPP1NPB1/R1BQ1RK1 b - - 0 9", "Closed King's Indian (Middlegame)"),
    ("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", "Rook & Pawn Endgame"),
    ("2rr3k/pp3pp1/1nnqbN1p/3pN3/2pP4/2P3Q1/PPB4P/R4RK1 w - - 0 1", "WAC 001 (Greek Gift Strike)"),
    ("r1b2rk1/ppp2ppp/8/2n1p3/2B5/4P3/PPP3PP/2KR3R w - - 0 1", "Deflection & Back-Rank Attack"),
    ("8/8/4k3/8/2K5/5N2/8/8 w - - 0 1", "Minor Piece Endgame"),
]


def find_default_engine() -> Optional[str]:
    """Auto-detects the compiled ChessMinds executable in standard build directories."""
    repo_root = Path(__file__).resolve().parent.parent
    candidates = [
        repo_root / "build" / "chessmind.exe",
        repo_root / "build" / "chessmind",
        repo_root / "build" / "Release" / "chessmind.exe",
        repo_root / "build" / "Release" / "chessmind",
        repo_root / "chessmind.exe",
        repo_root / "chessmind",
    ]
    for cand in candidates:
        if cand.is_file() and os.access(cand, os.X_OK if platform.system() != "Windows" else os.F_OK):
            return str(cand)
    return None


class UCIEngine:
    """Manages robust communication with the ChessMinds engine over UCI."""

    def __init__(self, exe_path: str, timeout_sec: float = 30.0, verbose: bool = False):
        self.exe_path = exe_path
        self.timeout_sec = timeout_sec
        self.verbose = verbose
        self.process: Optional[subprocess.Popen] = None
        self.engine_name = "ChessMinds"
        self.engine_author = "Unknown"
        self._stdout_queue: queue.Queue = queue.Queue()
        self._reader_thread: Optional[threading.Thread] = None
        self._stop_reader = threading.Event()

    def start(self) -> None:
        """Launches the engine process and verifies UCI handshakes."""
        if not os.path.isfile(self.exe_path):
            raise FileNotFoundError(
                f"Engine executable not found at '{self.exe_path}'. "
                f"Please build the project first with: cmake --build build"
            )

        self._stop_reader.clear()
        self.process = subprocess.Popen(
            [self.exe_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            universal_newlines=True,
        )

        # Background thread to avoid I/O pipe blocking across Windows and Unix
        self._reader_thread = threading.Thread(target=self._enqueue_output, daemon=True)
        self._reader_thread.start()

        # Send UCI command & wait for uciok
        self.send_command("uci")
        uciok_received = False
        start_t = time.time()

        while time.time() - start_t < 10.0:
            line = self._read_line(timeout=0.5)
            if line is None:
                continue
            if self.verbose:
                print(f"[UCI <<] {line}")
            if line.startswith("id name "):
                self.engine_name = line[len("id name "):].strip()
            elif line.startswith("id author "):
                self.engine_author = line[len("id author "):].strip()
            elif line.strip() == "uciok":
                uciok_received = True
                break

        if not uciok_received:
            self.terminate()
            raise RuntimeError(f"Engine at '{self.exe_path}' failed to respond with 'uciok' within 10s.")

        # Send isready & wait for readyok
        self.wait_ready()

    def _enqueue_output(self) -> None:
        """Helper thread continuously buffering stdout into a thread-safe queue."""
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
        """Writes a command string to the engine stdin."""
        if not self.process or self.process.poll() is not None:
            raise RuntimeError("Engine process is not running.")
        if self.verbose:
            print(f"[UCI >>] {cmd}")
        try:
            assert self.process.stdin is not None
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()
        except (BrokenPipeError, OSError) as e:
            raise RuntimeError(f"Failed to send command '{cmd}' to engine: {e}")

    def wait_ready(self, timeout_sec: float = 10.0) -> bool:
        """Sends 'isready' and waits for 'readyok'."""
        self.send_command("isready")
        start_t = time.time()
        while time.time() - start_t < timeout_sec:
            line = self._read_line(timeout=0.2)
            if line is None:
                continue
            if line.strip() == "readyok":
                return True
        return False

    def new_game(self) -> None:
        """Sends 'ucinewgame' to clear transposition tables and search state."""
        self.send_command("ucinewgame")
        self.wait_ready()

    def search(
        self,
        fen: str,
        depth: Optional[int] = None,
        movetime_ms: Optional[int] = None,
        timeout_sec: Optional[float] = None,
    ) -> Dict[str, Any]:
        """Runs a UCI search on the given FEN and returns parsed metrics."""
        if timeout_sec is None:
            timeout_sec = self.timeout_sec

        # Ensure engine is alive or restart
        if not self.process or self.process.poll() is not None:
            self.restart()

        self.new_game()

        # Set board position
        if fen == "startpos":
            self.send_command("position startpos")
        else:
            self.send_command(f"position fen {fen}")

        # Build go command
        if movetime_ms is not None and movetime_ms > 0:
            go_cmd = f"go movetime {movetime_ms}"
            effective_timeout = max(timeout_sec, (movetime_ms / 1000.0) + 15.0)
        elif depth is not None and depth > 0:
            go_cmd = f"go depth {depth}"
            effective_timeout = timeout_sec
        else:
            go_cmd = "go depth 6"
            effective_timeout = timeout_sec

        self.send_command(go_cmd)

        result: Dict[str, Any] = {
            "status": "OK",
            "depth": 0,
            "seldepth": 0,
            "nodes": 0,
            "nps": 0,
            "time_ms": 0,
            "score_type": "cp",
            "score": 0,
            "pv": "",
            "bestmove": "",
            "raw_info_lines": 0,
            "error_msg": "",
        }

        start_time = time.time()
        bestmove_received = False

        while not bestmove_received:
            # Check for subprocess crash
            if self.process and self.process.poll() is not None:
                stderr_output = ""
                if self.process.stderr:
                    try:
                        stderr_output = self.process.stderr.read()
                    except Exception:
                        pass
                result["status"] = "CRASH"
                result["error_msg"] = f"Engine process crashed (Exit code {self.process.poll()}): {stderr_output.strip()}"
                return result

            # Check timeout
            elapsed = time.time() - start_time
            if elapsed > effective_timeout:
                # Try to stop gracefully first
                try:
                    self.send_command("stop")
                    stop_wait = time.time()
                    while time.time() - stop_wait < 2.0:
                        line = self._read_line(timeout=0.2)
                        if line and line.startswith("bestmove"):
                            parts = line.split()
                            result["bestmove"] = parts[1] if len(parts) > 1 else ""
                            bestmove_received = True
                            break
                except Exception:
                    pass

                if not bestmove_received:
                    result["status"] = "TIMEOUT"
                    result["error_msg"] = f"Search exceeded timeout limit of {effective_timeout:.1f}s without returning bestmove."
                    self.restart()
                    return result

            line = self._read_line(timeout=0.1)
            if line is None:
                continue

            if self.verbose:
                print(f"[UCI <<] {line}")

            if line.startswith("info"):
                result["raw_info_lines"] += 1
                self._parse_info_line(line, result)
            elif line.startswith("bestmove"):
                parts = line.split()
                if len(parts) >= 2:
                    result["bestmove"] = parts[1]
                bestmove_received = True
                break

        # If time_ms is 0 or unmeasured, fall back to elapsed wall-clock
        if result["time_ms"] == 0:
            result["time_ms"] = max(1, int((time.time() - start_time) * 1000))

        # Re-derive NPS if engine reported 0 (fast iterations < 1 ms)
        if result["nps"] == 0 and result["nodes"] > 0 and result["time_ms"] > 0:
            result["nps"] = int((result["nodes"] / (result["time_ms"] / 1000.0)))

        return result

    @staticmethod
    def _parse_info_line(line: str, result: Dict[str, Any]) -> None:
        """Extracts depth, seldepth, score, nodes, time, nps, and pv from a UCI info string."""
        tokens = line.split()
        n = len(tokens)
        i = 1
        while i < n:
            token = tokens[i]
            try:
                if token == "depth" and i + 1 < n:
                    result["depth"] = int(tokens[i + 1])
                    i += 2
                elif token == "seldepth" and i + 1 < n:
                    result["seldepth"] = int(tokens[i + 1])
                    i += 2
                elif token == "time" and i + 1 < n:
                    result["time_ms"] = int(tokens[i + 1])
                    i += 2
                elif token == "nodes" and i + 1 < n:
                    result["nodes"] = int(tokens[i + 1])
                    i += 2
                elif token == "nps" and i + 1 < n:
                    result["nps"] = int(tokens[i + 1])
                    i += 2
                elif token == "score" and i + 2 < n:
                    result["score_type"] = tokens[i + 1]
                    result["score"] = int(tokens[i + 2])
                    i += 3
                elif token == "pv" and i + 1 < n:
                    result["pv"] = " ".join(tokens[i + 1:])
                    break  # PV is always trailing in UCI spec
                else:
                    i += 1
            except (ValueError, IndexError):
                i += 1

    def restart(self) -> None:
        """Safely restarts the engine process."""
        self.terminate()
        time.sleep(0.1)
        self.start()

    def terminate(self) -> None:
        """Terminates the engine and shuts down reader threads."""
        self._stop_reader.set()
        if self.process:
            try:
                self.send_command("quit")
                self.process.wait(timeout=1.0)
            except Exception:
                try:
                    self.process.terminate()
                    self.process.wait(timeout=1.0)
                except Exception:
                    try:
                        self.process.kill()
                    except Exception:
                        pass
            self.process = None


def load_fens(fens_path: Optional[str]) -> List[Tuple[str, str]]:
    """Loads positions from a file, skipping empty lines and comments."""
    if not fens_path:
        repo_root = Path(__file__).resolve().parent.parent
        default_suite = repo_root / "benchmarks" / "benchmark_suite.fen"
        if default_suite.is_file():
            fens_path = str(default_suite)
        else:
            return DEFAULT_BENCHMARK_POSITIONS

    path = Path(fens_path)
    if not path.is_file():
        raise FileNotFoundError(f"FEN file not found: {fens_path}")

    positions: List[Tuple[str, str]] = []
    with open(path, "r", encoding="utf-8") as f:
        for idx, raw_line in enumerate(f, start=1):
            line = raw_line.strip()
            if not line or line.startswith("#") or line.startswith("//"):
                continue

            # Support "FEN ; Name" or "FEN"
            if ";" in line:
                parts = line.split(";", 1)
                fen = parts[0].strip()
                name = parts[1].strip() or f"Position {idx}"
            else:
                fen = line
                name = f"Position {idx}"

            positions.append((fen, name))

    if not positions:
        raise ValueError(f"No valid FEN positions found in '{fens_path}'.")

    return positions


class BenchmarkFramework:
    """Executes benchmark runs, computes statistics, and formats reports."""

    def __init__(
        self,
        engine_path: str,
        depth: Optional[int] = 7,
        movetime_ms: Optional[int] = None,
        rounds: int = 1,
        timeout_sec: float = 30.0,
        verbose: bool = False,
    ):
        self.engine_path = engine_path
        self.depth = depth
        self.movetime_ms = movetime_ms
        self.rounds = max(1, rounds)
        self.timeout_sec = timeout_sec
        self.verbose = verbose
        self.engine = UCIEngine(engine_path, timeout_sec=timeout_sec, verbose=verbose)

    def run(self, positions: List[Tuple[str, str]]) -> Tuple[List[Dict[str, Any]], Dict[str, Any]]:
        """Executes all positions across configured rounds and generates summary statistics."""
        print("\n" + "=" * 78)
        print("          ChessMinds Professional UCI Benchmarking Framework          ")
        print("=" * 78)

        self.engine.start()
        print(f" Engine:     {self.engine.engine_name} by {self.engine.engine_author}")
        print(f" Binary:     {self.engine_path}")
        print(f" Mode:       {'Depth ' + str(self.depth) if self.depth else 'Movetime ' + str(self.movetime_ms) + ' ms'}")
        print(f" Positions:  {len(positions)} test positions")
        print(f" Rounds:     {self.rounds} measurement(s) per position")
        print(f" System:     {platform.system()} {platform.release()} ({platform.machine()})")
        print("=" * 78 + "\n")

        raw_results: List[Dict[str, Any]] = []
        total_runs = len(positions) * self.rounds
        current_run = 0

        header_fmt = "{:<4} {:<6} {:<24} {:<7} {:<10} {:<11} {:<8} {:<9} {:<8}"
        print(header_fmt.format("Idx", "Round", "Position Name", "Depth", "Nodes", "NPS", "Time", "BestMove", "Score"))
        print("-" * 92)

        start_wall_time = time.time()

        for pos_idx, (fen, name) in enumerate(positions, start=1):
            short_name = name[:23] if len(name) > 23 else name

            for rnd in range(1, self.rounds + 1):
                current_run += 1
                res = self.engine.search(
                    fen=fen,
                    depth=self.depth,
                    movetime_ms=self.movetime_ms,
                    timeout_sec=self.timeout_sec,
                )

                score_str = f"{res['score_type']} {res['score']}" if res['status'] == "OK" else res['status']
                row_str = "{:<4} {:<6} {:<24} {:<7} {:<10} {:<11} {:<8} {:<9} {:<8}".format(
                    f"[{pos_idx}]",
                    f"{rnd}/{self.rounds}",
                    short_name,
                    res["depth"],
                    f"{res['nodes']:,}",
                    f"{res['nps']:,}",
                    f"{res['time_ms']} ms",
                    res["bestmove"] or "-",
                    score_str,
                )
                print(row_str)

                # Append raw measurement record
                raw_results.append({
                    "position_index": pos_idx,
                    "position_name": name,
                    "fen": fen,
                    "round": rnd,
                    "status": res["status"],
                    "depth": res["depth"],
                    "seldepth": res["seldepth"],
                    "score_type": res["score_type"],
                    "score": res["score"],
                    "nodes": res["nodes"],
                    "nps": res["nps"],
                    "time_ms": res["time_ms"],
                    "bestmove": res["bestmove"],
                    "pv": res["pv"],
                    "error_msg": res["error_msg"],
                })

        total_wall_elapsed = time.time() - start_wall_time
        self.engine.terminate()

        # Compute aggregate statistics
        summary = self._compute_aggregates(raw_results, len(positions), total_wall_elapsed)
        self._print_summary_table(summary)

        return raw_results, summary

    def _compute_aggregates(
        self,
        results: List[Dict[str, Any]],
        num_positions: int,
        wall_elapsed_sec: float,
    ) -> Dict[str, Any]:
        """Calculates statistical aggregates across successful benchmark runs."""
        valid_runs = [r for r in results if r["status"] == "OK"]
        failed_runs = [r for r in results if r["status"] != "OK"]

        if not valid_runs:
            return {
                "engine_name": self.engine.engine_name,
                "engine_author": self.engine.engine_author,
                "engine_path": self.engine_path,
                "timestamp": datetime.datetime.now().isoformat(),
                "total_positions": num_positions,
                "total_runs": len(results),
                "successful_runs": 0,
                "failed_runs": len(failed_runs),
                "total_wall_time_sec": round(wall_elapsed_sec, 2),
                "error": "All benchmark runs failed.",
            }

        nps_list = [r["nps"] for r in valid_runs if r["nps"] > 0]
        nodes_list = [r["nodes"] for r in valid_runs]
        time_list = [r["time_ms"] for r in valid_runs]
        depth_list = [r["depth"] for r in valid_runs]

        total_nodes = sum(nodes_list)
        total_time_ms = sum(time_list)
        weighted_nps = int(total_nodes / (total_time_ms / 1000.0)) if total_time_ms > 0 else 0

        avg_nps = int(statistics.mean(nps_list)) if nps_list else 0
        median_nps = int(statistics.median(nps_list)) if nps_list else 0
        min_nps = min(nps_list) if nps_list else 0
        max_nps = max(nps_list) if nps_list else 0

        avg_nodes = int(statistics.mean(nodes_list)) if nodes_list else 0
        avg_time_ms = round(statistics.mean(time_list), 1) if time_list else 0.0
        max_depth = max(depth_list) if depth_list else 0
        avg_depth = round(statistics.mean(depth_list), 1) if depth_list else 0.0

        # Group metrics per position for deep inspection
        per_position_stats: Dict[str, Any] = {}
        for r in valid_runs:
            name = r["position_name"]
            if name not in per_position_stats:
                per_position_stats[name] = {"nodes": [], "nps": [], "time_ms": [], "depth": []}
            per_position_stats[name]["nodes"].append(r["nodes"])
            per_position_stats[name]["nps"].append(r["nps"])
            per_position_stats[name]["time_ms"].append(r["time_ms"])
            per_position_stats[name]["depth"].append(r["depth"])

        pos_summary = {}
        for name, data in per_position_stats.items():
            pos_summary[name] = {
                "avg_nps": int(statistics.mean(data["nps"])) if data["nps"] else 0,
                "median_nps": int(statistics.median(data["nps"])) if data["nps"] else 0,
                "avg_nodes": int(statistics.mean(data["nodes"])) if data["nodes"] else 0,
                "avg_time_ms": round(statistics.mean(data["time_ms"]), 1) if data["time_ms"] else 0.0,
                "max_depth": max(data["depth"]) if data["depth"] else 0,
            }

        return {
            "engine": {
                "name": self.engine.engine_name,
                "author": self.engine.engine_author,
                "path": os.path.abspath(self.engine_path),
            },
            "environment": {
                "system": platform.system(),
                "release": platform.release(),
                "machine": platform.machine(),
                "python_version": platform.python_version(),
                "timestamp": datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            },
            "configuration": {
                "target_depth": self.depth,
                "movetime_ms": self.movetime_ms,
                "rounds": self.rounds,
                "timeout_sec": self.timeout_sec,
                "total_positions": num_positions,
                "total_runs": len(results),
                "successful_runs": len(valid_runs),
                "failed_runs": len(failed_runs),
            },
            "summary_metrics": {
                "total_nodes_searched": total_nodes,
                "total_search_time_ms": total_time_ms,
                "total_wall_time_sec": round(wall_elapsed_sec, 2),
                "average_nps": avg_nps,
                "median_nps": median_nps,
                "weighted_nps": weighted_nps,
                "min_nps": min_nps,
                "max_nps": max_nps,
                "average_nodes_per_pos": avg_nodes,
                "maximum_depth": max_depth,
                "average_depth": avg_depth,
                "average_search_time_ms": avg_time_ms,
            },
            "per_position_breakdown": pos_summary,
        }

    @staticmethod
    def _print_summary_table(summary: Dict[str, Any]) -> None:
        """Renders an executive summary card to stdout."""
        m = summary.get("summary_metrics", {})
        c = summary.get("configuration", {})

        print("\n" + "=" * 78)
        print("                       BENCHMARK SUMMARY REPORT                       ")
        print("=" * 78)
        print(f" Status:             {c.get('successful_runs', 0)} / {c.get('total_runs', 0)} runs completed successfully")
        print(f" Total Wall Time:    {m.get('total_wall_time_sec', 0.0)} seconds")
        print(f" Total Nodes:        {m.get('total_nodes_searched', 0):,} nodes")
        print("-" * 78)
        print(f" Weighted NPS:       {m.get('weighted_nps', 0):,} nodes/sec  (Total Nodes / Total Search Time)")
        print(f" Average NPS:        {m.get('average_nps', 0):,} nodes/sec")
        print(f" Median NPS:         {m.get('median_nps', 0):,} nodes/sec")
        print(f" NPS Range (Min/Max):{m.get('min_nps', 0):,} / {m.get('max_nps', 0):,} nodes/sec")
        print("-" * 78)
        print(f" Average Nodes/Run:  {m.get('average_nodes_per_pos', 0):,} nodes")
        print(f" Maximum Depth:      {m.get('maximum_depth', 0)} plies (Average: {m.get('average_depth', 0.0)})")
        print(f" Average Search Time:{m.get('average_search_time_ms', 0.0)} ms per position")
        print("=" * 78 + "\n")


def save_csv(results: List[Dict[str, Any]], filepath: str) -> None:
    """Exports raw measurement data to CSV."""
    path = Path(filepath)
    path.parent.mkdir(parents=True, exist_ok=True)

    headers = [
        "position_index",
        "position_name",
        "fen",
        "round",
        "status",
        "depth",
        "seldepth",
        "score_type",
        "score",
        "nodes",
        "nps",
        "time_ms",
        "bestmove",
        "pv",
        "error_msg",
    ]

    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=headers)
        writer.writeheader()
        for row in results:
            writer.writerow(row)

    print(f"[Export] Saved individual benchmark results to CSV: {os.path.abspath(path)}")


def save_json(summary: Dict[str, Any], filepath: str) -> None:
    """Exports full JSON summary report."""
    path = Path(filepath)
    path.parent.mkdir(parents=True, exist_ok=True)

    with open(path, "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    print(f"[Export] Saved executive summary report to JSON:    {os.path.abspath(path)}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="ChessMinds UCI-Based Automated Benchmarking Framework",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
PowerShell Usage Examples:
  # Quick benchmark at depth 7 using built-in standard suite:
  python tools/benchmark.py

  # Benchmark with custom FEN list at depth 8 with 3 repeated rounds:
  python tools/benchmark.py --fens data/benchmark_suite.fen --depth 8 --rounds 3

  # Benchmark with 500ms time limit per position:
  python tools/benchmark.py --movetime 500

  # Save CSV and JSON reports to a custom directory:
  python tools/benchmark.py --csv reports/bench_depth8.csv --json reports/bench_depth8.json
        """,
    )

    parser.add_argument(
        "--engine",
        type=str,
        default=None,
        help="Path to ChessMinds executable (default: auto-detects build/chessmind.exe).",
    )
    parser.add_argument(
        "--fens",
        type=str,
        default=None,
        help="Path to text file containing test FENs (one per line, optional '; name').",
    )
    parser.add_argument(
        "--depth",
        "-d",
        type=int,
        default=7,
        help="Fixed search depth in plies (default: 7).",
    )
    parser.add_argument(
        "--movetime",
        "-t",
        type=int,
        default=None,
        help="Fixed search time limit per position in milliseconds (overrides depth).",
    )
    parser.add_argument(
        "--rounds",
        "-r",
        type=int,
        default=1,
        help="Number of repeated measurement rounds per position (default: 1).",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=30.0,
        help="Maximum timeout in seconds before treating engine as hung (default: 30.0).",
    )
    parser.add_argument(
        "--csv",
        type=str,
        default="benchmark_results.csv",
        help="Output file path for raw CSV records (default: benchmark_results.csv).",
    )
    parser.add_argument(
        "--json",
        type=str,
        default="benchmark_summary.json",
        help="Output file path for JSON summary report (default: benchmark_summary.json).",
    )
    parser.add_argument(
        "--verbose",
        "-v",
        action="store_true",
        help="Enable verbose raw UCI logging for debugging.",
    )

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    # 1. Locate engine
    engine_path = args.engine or find_default_engine()
    if not engine_path:
        print("[Error] Could not automatically locate 'chessmind.exe' or 'chessmind'.", file=sys.stderr)
        print("Please build the engine first or specify the binary with --engine <path>.", file=sys.stderr)
        print("Example: cmake --build build; python tools/benchmark.py --engine build/chessmind.exe", file=sys.stderr)
        return 1

    # 2. Load positions
    try:
        positions = load_fens(args.fens)
    except Exception as e:
        print(f"[Error] Failed to load FEN positions: {e}", file=sys.stderr)
        return 1

    # 3. Configure search bounds
    depth = None if args.movetime else args.depth
    movetime_ms = args.movetime

    # 4. Run benchmark
    framework = BenchmarkFramework(
        engine_path=engine_path,
        depth=depth,
        movetime_ms=movetime_ms,
        rounds=args.rounds,
        timeout_sec=args.timeout,
        verbose=args.verbose,
    )

    try:
        raw_results, summary = framework.run(positions)
    except KeyboardInterrupt:
        print("\n[Aborted] Benchmark interrupted by user.")
        framework.engine.terminate()
        return 130
    except Exception as e:
        print(f"\n[Fatal Error] Benchmark execution failed: {e}", file=sys.stderr)
        framework.engine.terminate()
        return 1

    # 5. Export results
    if args.csv:
        save_csv(raw_results, args.csv)
    if args.json:
        save_json(summary, args.json)

    return 0


if __name__ == "__main__":
    sys.exit(main())
