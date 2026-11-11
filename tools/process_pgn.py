#!/usr/bin/env python3
"""Stream human PGN games into a compact quiet-position dataset."""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import os
import sys
import tempfile
from collections import Counter
from pathlib import Path
from typing import TextIO

import chess
import chess.pgn

RESULTS = {"1-0", "0-1", "1/2-1/2"}
RATING_BANDS = ((2000, 2200), (2200, 2400), (2400, 2600), (2600, None))
CSV_FIELDS = (
    "source_file",
    "game_number",
    "ply",
    "fen",
    "move_uci",
    "result",
    "white_elo",
    "black_elo",
)

class QuietGameBuilder(chess.pgn.GameBuilder):
    def handle_error(self, error: Exception) -> None:
        self.game.errors.append(error)


def parse_rating(value: str | None) -> int | None:
    if value is None:
        return None
    try:
        rating = int(value)
    except ValueError:
        return None
    return rating if rating > 0 else None


def rating_band(rating: int) -> str:
    for lower, upper in RATING_BANDS:
        if upper is None or rating < upper:
            return f"{lower}-{upper - 1}" if upper is not None else f"{lower}+"
    raise AssertionError("unreachable rating band")


def open_pgn(path: Path) -> TextIO:
    if path.suffix.lower() == ".gz":
        return gzip.open(path, "rt", encoding="utf-8", errors="replace")
    return path.open("rt", encoding="utf-8", errors="replace")


def collect_quiet_positions(
    game: chess.pgn.Game, source_file: str, game_number: int, minimum_rating: int = 2000
) -> tuple[str | None, list[dict[str, str | int]]]:
    if game.errors:
        return "malformed", []

    result = game.headers.get("Result", "")
    if result not in RESULTS:
        return "invalid_result", []

    white_elo = parse_rating(game.headers.get("WhiteElo"))
    black_elo = parse_rating(game.headers.get("BlackElo"))
    if white_elo is None or black_elo is None:
        return "invalid_ratings", []
    if white_elo < minimum_rating or black_elo < minimum_rating:
        return "below_rating_threshold", []

    board = game.board()
    if not board.is_valid():
        return "malformed", []

    positions: list[dict[str, str | int]] = []
    try:
        for ply, move in enumerate(game.mainline_moves(), start=1):
            if not board.is_legal(move):
                return "malformed", []
            if (
                not board.is_check()
                and not board.is_capture(move)
                and not board.gives_check(move)
            ):
                positions.append(
                    {
                        "source_file": source_file,
                        "game_number": game_number,
                        "ply": ply,
                        "fen": board.fen(),
                        "move_uci": move.uci(),
                        "result": result,
                        "white_elo": white_elo,
                        "black_elo": black_elo,
                    }
                )
            board.push(move)
    except (ValueError, AssertionError):
        return "malformed", []

    if not board.is_valid():
        return "malformed", []
    return None, positions


def collect_input_files(inputs: list[Path]) -> list[Path]:
    files: list[Path] = []
    for item in inputs:
        if item.is_dir():
            files.extend(
                path
                for path in item.rglob("*")
                if path.is_file()
                and (path.name.lower().endswith(".pgn") or path.name.lower().endswith(".pgn.gz"))
            )
        elif item.is_file():
            files.append(item)
        else:
            raise FileNotFoundError(f"PGN input does not exist: {item}")
    result = sorted(set(path.resolve() for path in files))
    if not result:
        raise FileNotFoundError("No PGN files found in the supplied input.")
    return result


def process_files(inputs: list[Path], output: Path) -> dict:
    minimum_rating = 2000
    files = collect_input_files(inputs)
    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    report_path = output.with_name(output.name + ".report.json")
    output_temp = None
    report_temp = None
    rejected: Counter[str] = Counter()
    accepted_results: Counter[str] = Counter()
    rating_distribution: Counter[str] = Counter()
    games_read = 0
    accepted_games = 0
    positions_extracted = 0
    bytes_read = 0

    try:
        fd, output_temp = tempfile.mkstemp(prefix=output.name, suffix=".tmp", dir=output.parent)
        os.close(fd)
        with gzip.open(output_temp, "wt", encoding="utf-8", newline="") as compressed:
            writer = csv.DictWriter(compressed, fieldnames=CSV_FIELDS)
            writer.writeheader()
            for path in files:
                bytes_read += path.stat().st_size
                with open_pgn(path) as pgn_stream:
                    game_number = 0
                    while True:
                        game_offset = pgn_stream.tell()
                        if path.suffix.lower() == ".gz":
                            game = chess.pgn.read_game(pgn_stream, Visitor=QuietGameBuilder)
                            if game is None:
                                break
                            game_number += 1
                            games_read += 1
                        else:
                            headers = chess.pgn.read_headers(pgn_stream)
                            if headers is None:
                                break
                            game_number += 1
                            games_read += 1
                        if games_read % 50_000 == 0:
                            print(f"Processed {games_read:,} games...", file=sys.stderr, flush=True)

                        if path.suffix.lower() != ".gz":
                            result = headers.get("Result", "")
                            white_elo = parse_rating(headers.get("WhiteElo"))
                            black_elo = parse_rating(headers.get("BlackElo"))
                            if result not in RESULTS:
                                rejected["invalid_result"] += 1
                                continue
                            if white_elo is None or black_elo is None:
                                rejected["invalid_ratings"] += 1
                                continue
                            if white_elo < minimum_rating or black_elo < minimum_rating:
                                rejected["below_rating_threshold"] += 1
                                continue
                            pgn_stream.seek(game_offset)
                            game = chess.pgn.read_game(pgn_stream, Visitor=QuietGameBuilder)
                            if game is None:
                                rejected["malformed"] += 1
                                continue

                        reason, positions = collect_quiet_positions(
                            game, path.name, game_number, minimum_rating
                        )
                        if reason:
                            rejected[reason] += 1
                            continue
                        accepted_games += 1
                        accepted_results[game.headers["Result"]] += 1
                        rating_distribution[rating_band(int(game.headers["WhiteElo"]))] += 1
                        rating_distribution[rating_band(int(game.headers["BlackElo"]))] += 1
                        writer.writerows(positions)
                        positions_extracted += len(positions)

        report = {
            "input_files": [str(path) for path in files],
            "input_bytes": bytes_read,
            "minimum_player_rating": minimum_rating,
            "quiet_position_rule": "Exclude positions where the side to move is in check, or the move captures or gives check.",
            "games_read": games_read,
            "games_accepted": accepted_games,
            "positions_extracted": positions_extracted,
            "games_rejected": sum(rejected.values()),
            "games_move_validated": accepted_games + rejected["malformed"],
            "games_rejected_by_headers_without_move_validation": (
                rejected["invalid_result"]
                + rejected["invalid_ratings"]
                + rejected["below_rating_threshold"]
            ),
            "malformed_game_count_scope": (
                "Only games meeting the result and both-player rating filters are fully parsed; "
                "other games are excluded by their headers without move validation."
            ),
            "rejections_by_reason": {
                reason: rejected[reason]
                for reason in (
                    "malformed",
                    "invalid_result",
                    "invalid_ratings",
                    "below_rating_threshold",
                )
            },
            "results": {result: accepted_results[result] for result in sorted(RESULTS)},
            "accepted_player_rating_distribution": {
                band: rating_distribution[band]
                for band in ("2000-2199", "2200-2399", "2400-2599", "2600+")
            },
            "output_csv_gzip": str(output),
        }

        fd, report_temp = tempfile.mkstemp(
            prefix=report_path.name, suffix=".tmp", dir=report_path.parent
        )
        with os.fdopen(fd, "w", encoding="utf-8", newline="\n") as report_stream:
            json.dump(report, report_stream, indent=2)
            report_stream.write("\n")
        os.replace(output_temp, output)
        output_temp = None
        os.replace(report_temp, report_path)
        report_temp = None
        return report
    finally:
        for temporary in (output_temp, report_temp):
            if temporary and os.path.exists(temporary):
                os.unlink(temporary)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--input",
        action="append",
        type=Path,
        required=True,
        help="PGN file or directory (repeat to process multiple inputs)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=Path("data/processed/quiet_positions.csv.gz"),
        help="Output compressed CSV path (default: data/processed/quiet_positions.csv.gz)",
    )
    args = parser.parse_args(argv)

    try:
        report = process_files(args.input, args.output)
    except (OSError, EOFError) as error:
        print(f"PGN processing failed: {error}", file=sys.stderr)
        return 1
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
