"""
ChessMinds Local Web Bridge & Server
Communicates with the C++20 ChessMinds engine via standard UCI protocol.
Zero external dependencies (uses standard library http.server, subprocess, threading).
"""

import http.server
import socketserver
import subprocess
import threading
import json
import os
import sys
import webbrowser

PORT = 8080
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(BASE_DIR)

# Locate the compiled chessmind executable
POSSIBLE_ENGINE_PATHS = [
    os.path.join(PROJECT_DIR, "build", "chessmind.exe"),
    os.path.join(PROJECT_DIR, "build", "chessmind"),
    os.path.join(PROJECT_DIR, "chessmind.exe"),
    os.path.join(PROJECT_DIR, "chessmind"),
]

ENGINE_PATH = None
for path in POSSIBLE_ENGINE_PATHS:
    if os.path.exists(path):
        ENGINE_PATH = path
        break

class EngineBridge:
    def __init__(self, exe_path):
        self.exe_path = exe_path
        self.process = None
        self.lock = threading.Lock()
        self.start_engine()

    def start_engine(self):
        if not self.exe_path or not os.path.exists(self.exe_path):
            print(f"[EngineBridge] Warning: ChessMinds binary not found. Searching build paths...")
            return

        if self.process:
            try:
                self.process.terminate()
            except Exception:
                pass

        print(f"[EngineBridge] Starting engine process: {self.exe_path}")
        self.process = subprocess.Popen(
            [self.exe_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            bufsize=1
        )
        self.send_command("uci")
        # Wait for uciok
        while True:
            line = self.process.stdout.readline()
            if not line:
                break
            line = line.strip()
            if line == "uciok":
                break
        self.send_command("isready")
        while True:
            line = self.process.stdout.readline()
            if not line:
                break
            line = line.strip()
            if line == "readyok":
                break
        print("[EngineBridge] ChessMinds engine initialized and ready.")

    def ensure_engine_running(self):
        if not self.process or self.process.poll() is not None:
            self.start_engine()

    def send_command(self, cmd):
        if self.process and self.process.stdin:
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()

    def query_move(self, fen=None, moves=None, depth=6, movetime=None):
        with self.lock:
            self.ensure_engine_running()
            if not self.process:
                return {"error": "Engine binary not running. Please build with CMake."}

            if fen and fen != "startpos":
                pos_cmd = f"position fen {fen}"
            else:
                pos_cmd = "position startpos"

            if moves:
                pos_cmd += " moves " + " ".join(moves)

            self.send_command(pos_cmd)

            if movetime:
                go_cmd = f"go movetime {movetime}"
            elif depth:
                go_cmd = f"go depth {depth}"
            else:
                go_cmd = "go depth 6"

            self.send_command(go_cmd)

            result = {
                "bestmove": None,
                "depth": 0,
                "score": 0,
                "score_raw": 0,
                "nodes": 0,
                "nps": 0,
                "time": 0,
                "pv": ""
            }

            # Determine whether it's Black's turn to search
            is_black_turn = False
            if fen and " b " in fen:
                is_black_turn = True
            elif moves and (len(moves) % 2 == 1):
                is_black_turn = True

            while True:
                line = self.process.stdout.readline()
                if not line:
                    break
                line = line.strip()
                if line.startswith("info"):
                    parts = line.split()
                    for i in range(len(parts)):
                        if parts[i] == "depth" and i + 1 < len(parts):
                            result["depth"] = int(parts[i + 1])
                        elif parts[i] == "score" and i + 2 < len(parts):
                            if parts[i + 1] == "cp":
                                raw = int(parts[i + 2])
                                result["score_raw"] = raw
                                result["score"] = -raw if is_black_turn else raw
                                result["score_type"] = "cp"
                            elif parts[i + 1] == "mate":
                                mate_val = int(parts[i + 2])
                                sign = 1 if mate_val > 0 else -1
                                score_val = (30000 - abs(mate_val) * 10) * sign
                                result["score_raw"] = score_val
                                result["score"] = -score_val if is_black_turn else score_val
                                result["mate"] = mate_val
                                result["score_type"] = "mate"
                        elif parts[i] == "nodes" and i + 1 < len(parts):
                            result["nodes"] = int(parts[i + 1])
                        elif parts[i] == "nps" and i + 1 < len(parts):
                            result["nps"] = int(parts[i + 1])
                        elif parts[i] == "time" and i + 1 < len(parts):
                            result["time"] = int(parts[i + 1])
                        elif parts[i] == "pv" and i + 1 < len(parts):
                            result["pv"] = " ".join(parts[i + 1:])
                elif line.startswith("bestmove"):
                    parts = line.split()
                    if len(parts) >= 2:
                        result["bestmove"] = parts[1]
                    break

            return result

    def query_legal_moves(self, fen=None, moves=None):
        """Query legal moves from engine using dedicated 'legalmoves' command."""
        with self.lock:
            self.ensure_engine_running()
            if not self.process:
                return []

            if fen and fen != "startpos":
                pos_cmd = f"position fen {fen}"
            else:
                pos_cmd = "position startpos"

            if moves:
                pos_cmd += " moves " + " ".join(moves)

            self.send_command(pos_cmd)
            self.send_command("legalmoves")

            legal_moves = []
            while True:
                line = self.process.stdout.readline()
                if not line:
                    break
                line = line.strip()
                if line.startswith("legalmoves"):
                    parts = line.split()
                    legal_moves = parts[1:]
                    break
                elif ":" in line and not line.startswith("Nodes"):
                    move_str = line.split(":")[0].strip()
                    if len(move_str) >= 4 and move_str[0].isalpha():
                        legal_moves.append(move_str)
                elif line.startswith("Nodes") or line.startswith("Total"):
                    break
            return legal_moves

bridge = EngineBridge(ENGINE_PATH)

class ChessRequestHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=BASE_DIR, **kwargs)

    def translate_path(self, path):
        # Support /ChessMinds/ base path transparently
        if path.startswith("/ChessMinds/"):
            path = "/" + path[len("/ChessMinds/"):]
        elif path == "/ChessMinds":
            path = "/"
        return super().translate_path(path)

    def send_json_response(self, data, status=200):
        try:
            payload = json.dumps(data).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
        except (ConnectionAbortedError, ConnectionResetError, BrokenPipeError, OSError):
            pass

    @property
    def normalized_path(self):
        p = self.path
        if p.startswith("/ChessMinds/"):
            return "/" + p[len("/ChessMinds/"):]
        elif p == "/ChessMinds":
            return "/"
        return p

    def do_POST(self):
        path = self.normalized_path
        if path == "/api/move":
            content_length = int(self.headers.get("Content-Length", 0))
            post_data = self.rfile.read(content_length)
            try:
                data = json.loads(post_data.decode("utf-8"))
            except Exception as e:
                self.send_error(400, f"Invalid JSON: {e}")
                return

            fen = data.get("fen", "startpos")
            moves = data.get("moves", [])
            depth = data.get("depth", 6)
            movetime = data.get("movetime", None)

            response = bridge.query_move(fen=fen, moves=moves, depth=depth, movetime=movetime)
            self.send_json_response(response, 200)

        elif path == "/api/evaluate":
            content_length = int(self.headers.get("Content-Length", 0))
            post_data = self.rfile.read(content_length)
            try:
                data = json.loads(post_data.decode("utf-8"))
            except Exception as e:
                self.send_error(400, f"Invalid JSON: {e}")
                return

            fen = data.get("fen", "startpos")
            moves = data.get("moves", [])
            depth = data.get("depth", 6)

            response = bridge.query_move(fen=fen, moves=moves, depth=depth)
            self.send_json_response(response, 200)

        elif path == "/api/legalmoves":
            content_length = int(self.headers.get("Content-Length", 0))
            post_data = self.rfile.read(content_length)
            try:
                data = json.loads(post_data.decode("utf-8"))
            except Exception as e:
                self.send_error(400, f"Invalid JSON: {e}")
                return

            fen = data.get("fen", "startpos")
            moves = data.get("moves", [])

            legal = bridge.query_legal_moves(fen=fen, moves=moves)
            self.send_json_response({"moves": legal}, 200)
        else:
            self.send_error(404, "Endpoint not found")

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        path = self.normalized_path
        if path == "/api/status":
            status = {
                "engine_loaded": bridge.process is not None,
                "engine_path": ENGINE_PATH,
                "version": "1.0.0"
            }
            self.send_json_response(status, 200)
        else:
            super().do_GET()

class ThreadedTCPServer(socketserver.ThreadingMixIn, socketserver.TCPServer):
    daemon_threads = True
    allow_reuse_address = True

def run_server():
    with ThreadedTCPServer(("127.0.0.1", PORT), ChessRequestHandler) as httpd:
        print(f"\n========================================================")
        print(f"  ChessMinds GUI Web Server running on:")
        print(f"  --> http://127.0.0.1:{PORT}")
        print(f"  Press Ctrl+C to terminate the server.")
        print(f"========================================================\n")
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nShutting down server...")
            if bridge.process:
                bridge.send_command("quit")
                bridge.process.terminate()

if __name__ == "__main__":
    run_server()

