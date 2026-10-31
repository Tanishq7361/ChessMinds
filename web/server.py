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

        print(f"[EngineBridge] Starting engine process: {self.exe_path}")
        self.process = subprocess.Popen(
            [self.exe_path],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
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

    def send_command(self, cmd):
        if self.process and self.process.stdin:
            self.process.stdin.write(cmd + "\n")
            self.process.stdin.flush()

    def query_move(self, fen=None, moves=None, depth=6, movetime=None):
        with self.lock:
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
                "nodes": 0,
                "nps": 0,
                "time": 0,
                "pv": ""
            }

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
                        elif parts[i] == "score" and i + 2 < len(parts) and parts[i + 1] == "cp":
                            result["score"] = int(parts[i + 2])
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

bridge = EngineBridge(ENGINE_PATH)

class ChessRequestHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=BASE_DIR, **kwargs)

    def do_POST(self):
        if self.path == "/api/move":
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

            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(response).encode("utf-8"))
        else:
            self.send_error(404, "Endpoint not found")

    def do_GET(self):
        if self.path == "/api/status":
            status = {
                "engine_loaded": bridge.process is not None,
                "engine_path": ENGINE_PATH,
                "version": "1.0.0"
            }
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.end_headers()
            self.wfile.write(json.dumps(status).encode("utf-8"))
        else:
            super().do_GET()

def run_server():
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("127.0.0.1", PORT), ChessRequestHandler) as httpd:
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

