#!/usr/bin/env python3
"""Stream one Arduino temperature channel to a two-column host CSV file."""

from __future__ import annotations

import argparse
import csv
import io
import json
import sys
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen


def fetch(url: str, timeout: float) -> bytes:
    request = Request(url, headers={"User-Agent": "ATE-Temperature-Collector/1.0"})
    with urlopen(request, timeout=timeout) as response:
        return response.read()


def fetch_json(url: str, timeout: float) -> dict:
    return json.loads(fetch(url, timeout).decode("utf-8"))


def fetch_latest(base_url: str, sensor: int, timeout: float) -> float:
    payload = fetch(f"{base_url}/api/latest?sensor={sensor}", timeout).decode("utf-8")
    rows = list(csv.DictReader(io.StringIO(payload)))
    if len(rows) != 1 or "temperature" not in rows[0]:
        raise ValueError("Arduino returned malformed latest-sample CSV")
    return float(rows[0]["temperature"])


def iso_timestamp() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="milliseconds")


def collect(args: argparse.Namespace) -> int:
    base_url = args.base_url or f"http://{args.ip}"
    base_url = base_url.rstrip("/")
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    new_file = not output.exists() or output.stat().st_size == 0
    last_sequence: int | None = None
    started_at = time.monotonic()
    rows_written = 0

    with output.open("a", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        if new_file:
            writer.writerow(("time", "temperature"))
            stream.flush()

        print(f"Collecting sensor {args.sensor} from {base_url} into {output}")
        try:
            while args.duration is None or time.monotonic() - started_at < args.duration:
                try:
                    status = fetch_json(f"{base_url}/api/status", args.timeout)
                    sensor_count = int(status["sensorCount"])
                    if args.sensor < 0 or args.sensor >= sensor_count:
                        raise ValueError(
                            f"sensor {args.sensor} is unavailable; device reports {sensor_count}"
                        )

                    sequence = int(status["seq"])
                    if status["running"] and sequence != last_sequence:
                        temperature = fetch_latest(
                            base_url, args.sensor, args.timeout
                        )
                        writer.writerow((iso_timestamp(), f"{temperature:.3f}"))
                        stream.flush()
                        last_sequence = sequence
                        rows_written += 1
                        print(
                            f"{iso_timestamp()} sensor={args.sensor} "
                            f"temperature={temperature:.3f} C"
                        )
                except (HTTPError, URLError, TimeoutError, json.JSONDecodeError, ValueError) as error:
                    print(f"Warning: {error}; retrying", file=sys.stderr)
                time.sleep(args.poll)
        except KeyboardInterrupt:
            print("\nStopping collector")

    print(f"Collector stopped; wrote {rows_written} sample(s)")
    return 0


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ip", default="10.100.102.247", help="Arduino IP address")
    parser.add_argument(
        "--base-url",
        help="Complete HTTP base URL; useful for tests and non-standard ports",
    )
    parser.add_argument("--sensor", type=int, default=0, help="Zero-based sensor index")
    parser.add_argument("--output", default="data/temperature.csv", help="Output CSV")
    parser.add_argument("--poll", type=float, default=0.5, help="Status poll period in seconds")
    parser.add_argument("--timeout", type=float, default=2.0, help="HTTP timeout in seconds")
    parser.add_argument(
        "--duration",
        type=float,
        help="Stop after this many seconds; omit for an unlimited test",
    )
    args = parser.parse_args(argv)
    if args.poll <= 0 or args.timeout <= 0 or (args.duration is not None and args.duration <= 0):
        parser.error("poll, timeout, and duration must be positive")
    return args


if __name__ == "__main__":
    raise SystemExit(collect(parse_args()))
