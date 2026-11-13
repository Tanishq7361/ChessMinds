# ChessMinds

**ChessMinds** is an original, competitive, high-performance chess engine written from scratch in modern **C++20**. It is built in full compliance with the **Chess Agent Competition Guidelines** and the **Universal Chess Interface (UCI)** protocol.

---

## 1. Approach Summary

ChessMinds implements the classical engine architecture combining **Way 3 (Alpha-Beta Pruning & Search Enhancements)** and **Way 4 (Hand-crafted Tapered Evaluation)**:

1. **Board Representation & Move Generation (Built from Scratch)**:
   - 64-bit Bitboard representation (`uint64_t`) with C++20 `<bit>` intrinsic operations (`popcount`, `countr_zero`).
   - Fully validated against standard Perft benchmarks (KiwiPete & start position) for 100% legal chess rules: castling, en passant, pawn promotions, checks, pinned pieces, and state undo.
   - Deterministic 64-bit Zobrist hashing for board state, castling rights, en passant file, and side to move.
   - Draw detection: 50-move rule, threefold/twofold repetition, and insufficient material.

2. **Search Architecture (Way 3)**:
   - **Iterative Deepening** from depth 1 upwards.
   - **Negamax with Alpha-Beta Pruning**.
   - **Null-Move Pruning (NMP)**: Bypasses subtrees when the position is commanding enough that passing a turn still maintains a fail-high cutoff.
   - **Late Move Reductions (LMR)**: Dynamically reduces search depth on late non-tactical candidate moves, re-searching at full depth only on fail-highs.
   - **Check Extension**: Deepens tactical search by 1 ply when in check.
   - **Triangular Principal Variation (PV) Table**: Reconstructs and outputs the complete multi-ply best line of play.
   - **Quiescence Search**: Evaluates captures with delta pruning to eliminate the horizon effect.
   - **Move Ordering**:
     1. Transposition Table best move
     2. Winning & equal captures ordered by MVV-LVA (Most Valuable Victim - Least Valuable Attacker)
     3. Killer Move Heuristic (two killer slots per ply for quiet beta cutoffs)
     4. History Heuristic (butterfly table for quiet moves)
   - **Transposition Table**: 16 MB default cache storing Zobrist keys, search depth, flags (EXACT, LOWERBOUND, UPPERBOUND), and best moves with ply-distance mate adjustments.

3. **Hand-Crafted Tapered Evaluation (Way 4)**:
   - **Centipawn Material Values**: Pawn: 100, Knight: 320, Bishop: 330, Rook: 500, Queen: 900.
   - **Piece-Square Tables (PST)**: Separate Middlegame (MG) and Endgame (EG) positional tables for all piece types.
   - **Game Phase Tapering**: Dynamically blends MG and EG evaluations according to remaining non-pawn material.
   - **Pawn Structure Evaluation**: Penalizes doubled and isolated pawns, and rewards passed pawns scaled with rank advancement.
   - **Positional Bonuses**: Bishop pair bonus (+30 cp).

4. **Tournament Time Management**:
   - Designed for the official competition time control of **3 minutes + 2 seconds increment** (and 10+5 rapid).
   - Dynamically calculates the thinking time budget per move from `wtime`, `btime`, `winc`, `binc`, and `movetime`.
   - Periodically checks the elapsed time every 2,048 search nodes, ensuring the engine never forfeits on time.

---

## 2. Competition Rule Compliance & Originality Declaration

- **Originality**: All move generation, search algorithms, evaluation functions, and UCI protocol handlers are written 100% by the team from scratch.
- **Zero Prohibited Resources**:
  - No existing chess engines (Stockfish, LCZero, Komodo, etc.) are used, called, wrapped, or copied.
  - No opening books (Polyglot, etc.) or endgame tablebases (Syzygy, etc.) are used.
  - No external web APIs, cloud evaluation, or language models are contacted during play.
- **Clean Standard Output**:
  - Only valid UCI responses (`uciok`, `readyok`, `info ...`, `bestmove ...`) are written to standard output (`stdout`). All debug information is routed to `stderr`.

---

## 3. Prerequisites

- **CMake**: 3.20 or newer
- **C++20 Compliant Compiler**:
  - Windows: GCC / MinGW-w64 11.0+ or MSVC 2019 16.10+ / 2022
  - Linux: GCC 11.0+ or Clang 13.0+
  - macOS: Apple Clang (Xcode 13.0+) or LLVM Clang

---

## 4. Building the Engine

### Windows (PowerShell)

```powershell
# 1. Configure build directory
& "C:\Program Files\CMake\bin\cmake.exe" -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release

# 2. Build the engine and test suite
& "C:\Program Files\CMake\bin\cmake.exe" --build build

# 3. Run Perft verification suite
.\build\test_perft.exe
```

### Linux / macOS (Terminal)

```bash
# 1. Configure build directory
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. Build targets
cmake --build build

# 3. Run Perft verification suite
./build/test_perft
```

---

## 5. Web Frontend Interface (Interactive MVP)

ChessMinds includes a zero-dependency web interface with an interactive chessboard, real-time evaluation bar, engine telemetry (NPS, depth, nodes, PV), move navigation, and sound effects.

### Starting the Web Interface

#### Quick Start:
* **Windows**: Double-click `launch_gui.bat` or run:
  ```powershell
  python web/server.py
  ```
* **Browser**: Open [http://127.0.0.1:8080](http://127.0.0.1:8080) in any browser.

The server automatically bridges HTTP requests to the compiled `chessmind.exe` engine via standard UCI commands over stdio pipes.

---

## 6. GitHub Pages Deployment

The repository includes `.github/workflows/deploy.yml`, which builds the web interface and deploys `dist/` whenever changes are pushed to `main`.

Before the first deployment, open the repository’s **Settings → Pages** and set **Build and deployment → Source** to **GitHub Actions**. GitHub Pages serves the static interface at:

`https://tanishq7361.github.io/ChessMinds/`

GitHub Pages cannot run the local C++ engine or the Python API server, so engine analysis and move generation require running the project locally with `python web/server.py`.

---

## 7. Launch Command & Lichess-Bot Configuration

### Launch Command
- Windows: `.\build\chessmind.exe`
- Linux / macOS: `./build/chessmind`

### Connecting to Lichess via `lichess-bot`
1. Clone the official bridge:
   ```bash
   git clone https://github.com/lichess-bot-devs/lichess-bot.git
   cd lichess-bot
   pip install -r requirements.txt
   ```
2. In `config.yml`:
   ```yaml
   token: "YOUR_LICHESS_BOT_PLAY_TOKEN"
   engine:
     dir: "path/to/ChessMinds/build"
     name: "chessmind.exe"   # or "chessmind" on Linux/macOS
     protocol: "uci"
     silence_stderr: false
     online_moves:
       chessdb_book:
         enable: false
       lichess_cloud_eval:
         enable: false
       lichess_opening_explorer:
         enable: false
     polyglot:
       enabled: false
     draw_or_resign:
       resign:
         enabled: true
         score: -1000
         two_player: false
   ```
3. Run the bot:
   ```bash
   python lichess-bot.py
   ```

---

## 8. Rating Benchmark & Standard Elo Estimation

ChessMinds includes a dedicated tactical suite and Elo calibration benchmark executable (`rating_benchmark`) with 25 curated positions (Easy, Medium, Hard) drawn from standard chess tactical test suites (Bratko-Kopec, Win at Chess, Encyclopedia of Chess Middlegames).

### Running the Rating Benchmark Manually:

```powershell
# Run benchmark at default depth (6 plies):
.\build\rating_benchmark.exe

# Run benchmark at custom depth (e.g., depth 8):
.\build\rating_benchmark.exe 8
```

On Linux / macOS:
```bash
./build/rating_benchmark 6
```

### Benchmark Output & Metrics:
* **Per-position status**: Move chosen vs expected best moves, search time (ms), and tactical description.
* **Accuracy by tier**: Easy, Medium, and Hard tactical solve rates.
* **Calibrated Standard Rating**: Estimated Elo rating mapped to CCRL-standard tournament equivalents (currently ~2250 - 2350 Elo).

---

## 9. Professional UCI Benchmarking Suite

ChessMinds features an automated Python benchmarking harness (`tools/benchmark.py`) testing an 8-position diverse test suite (`benchmarks/benchmark_suite.fen`):
1. **Startpos (Opening)**
2. **KiwiPete (Complex Tactics)**
3. **Italian Game (Open Tactics)**
4. **Closed King's Indian (Middlegame Strategy)**
5. **Rook & Pawn Endgame**
6. **WAC 001 (Greek Gift Strike)**
7. **Deflection & Back-Rank Attack**
8. **Minor Piece Endgame**

### Running the UCI Benchmark Harness:

```powershell
# Run the 8-position suite at Depth 10 across 3 independent rounds:
python tools/benchmark.py --fens benchmarks/benchmark_suite.fen --depth 10 --rounds 3

# Run with custom time limit (e.g., 500 ms per position):
python tools/benchmark.py --fens benchmarks/benchmark_suite.fen --movetime 500 --rounds 1
```

### Verified Benchmark Performance (MinGW-w64 GCC 14.2 Release):
* **Weighted NPS Target**: ~649K NPS $\rightarrow$ **Achieved: 878,641 nodes/sec** (exceeds target by +35%).
* **Average NPS**: **1,565,988 nodes/sec** (Peak: 6,198,111 nodes/sec in endgames).
* **Target Depth Compliance**: Depth 10 reached in 7 of 8 positions (Position 6 terminates early with forced Mate-in-2).
* **Export Artifacts**: Formatted CSV (`benchmark_results.csv`), summary CSV (`benchmark_summary.csv`), and JSON report (`benchmark_summary.json`).

---

## 10. Automated External Engine Tournament Framework (Phase 8)

ChessMinds includes a dedicated automated tournament and strength evaluation framework (`tools/tournament.py`) capable of running head-to-head matches against any standard UCI engine (e.g. Stockfish, Berserk, Koivisto) or conducting SPRT testing between two versions of ChessMinds.

### Key Capabilities:
- **UCI Protocol Management**: Process management, options negotiation (`Hash`, `Threads`, `Skill Level`), error detection, and pipe cleanup.
- **Fair Paired Matches**: Alternates colors for every opening from a diverse 16-opening test suite (`benchmarks/tournament_openings.json`).
- **Full Output Logging**: Exports standard PGN files with opening names, FENs, and termination reasons, along with structured CSV and JSON reports.
- **Statistical Rigor**: Computes maximum-likelihood Elo difference with a 95% Confidence Interval ($1.96 \times \text{SE}$) and color-specific performance breakdowns.
- **Sequential Probability Ratio Test (SPRT)**: Early termination based on Log-Likelihood Ratio (LLR) boundaries for rapid hypothesis testing ($H_0$ vs $H_1$).
- **Reproducibility**: Configurable random seeds for opening order and pairings.

### Usage Commands:

```powershell
# 1. Run a 10-game match against Stockfish Skill Level 5 (100 ms/move):
python tools/tournament.py --engine1 build/chessmind.exe --name1 "ChessMinds" --engine2 engines/stockfish.exe --name2 "Stockfish-L5" --skill2 5 --movetime 100 --games 10 --pgn matches_sf5.pgn --csv matches_sf5.csv --json matches_sf5.json

# 2. Run a 50-game SPRT comparison between Release and candidate builds:
python tools/tournament.py --engine1 build/chessmind.exe --name1 "ChessMinds-New" --engine2 build_prev/chessmind.exe --name2 "ChessMinds-Old" --depth 6 --games 50 --sprt --elo0 0 --elo1 35 --seed 42

# 3. Run automated tournament unit tests:
python -m unittest tests/test_tournament.py -v
```

---

## 11. Offline Human-Game PGN Dataset

The offline PGN pipeline streams one game at a time, checks headers first to skip games below the rating threshold, fully parses eligible games, and writes legal quiet human positions to a gzip-compressed CSV. A JSON sidecar reports accepted/rejected game counts, rejection reasons, results, and rating distribution; it distinguishes header-filtered games from games whose moves were validated. It uses only PGN moves and game results; it does not call the engine or use engine evaluations, books, or tablebases.

```powershell
python -m pip install -r requirements-pgn.txt
python tools\process_pgn.py --input data\raw --output data\processed\quiet_positions.csv.gz
python -m unittest tests.test_process_pgn -v
```

Quiet positions exclude positions where the side to move is in check and moves that capture or give check. The output CSV stores the pre-move FEN, human move in UCI notation, actual game result, both ratings, source filename, game number, and ply. Processed data is written under `data\processed` and is not tracked by Git.

---

## 12. UCI Protocol Reference

| Command | Response | Description |
|---|---|---|
| `uci` | `id name ChessMinds`<br>`id author Tanishq Shah`<br>`uciok` | Identifies the engine |
| `isready` | `readyok` | Synchronizes engine readiness |
| `ucinewgame` | *(silent)* | Clears transposition table & resets game |
| `position startpos [moves ...]` | *(silent)* | Sets up position from starting rank |
| `position fen <fen> [moves ...]` | *(silent)* | Sets up position from FEN string |
| `go [wtime X] [btime Y] ...` | `info ...`<br>`bestmove <move>` | Searches current position within limits |
| `stop` | `bestmove <move>` | Aborts search immediately and returns best move |
| `bench [depth]` | `[BENCH] ...` | Runs 5 standard benchmark positions and reports NPS |
| `quit` | *(exits process)* | Terminates engine |

---

## 13. License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.