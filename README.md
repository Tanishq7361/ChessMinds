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

## 6. Launch Command & Lichess-Bot Configuration

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

## 7. UCI Protocol Reference

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

## 8. License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
