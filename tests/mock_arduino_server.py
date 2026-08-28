#!/usr/bin/env python3
"""Standard-library mock of the Arduino logger HTTP API."""

from __future__ import annotations

import argparse
import json
import math
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse


class DeviceState:
    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.running = False
        self.interval = 1
        self.lower = 10.0
        self.upper = 40.0
        self.seq = 0
        self.sample_time = 0
        self.last_sample = time.monotonic()
        self.temperatures = [23.5, 24.1]

    def update(self) -> None:
        now = time.monotonic()
        if not self.running:
            return
        elapsed = now - self.last_sample
        samples = int(elapsed / self.interval)
        if samples <= 0:
            return
        self.seq += samples
        self.sample_time += samples * self.interval
        self.last_sample += samples * self.interval
        self.temperatures = [
            23.5 + math.sin(self.sample_time / 8.0),
            24.1 + 0.7 * math.sin(self.sample_time / 10.0),
        ]

    def status(self) -> dict:
        with self.lock:
            self.update()
            alarm = self.running and any(
                value < self.lower or value > self.upper for value in self.temperatures
            )
            return {
                "running": self.running,
                "alarm": alarm,
                "sd": True,
                "interval": self.interval,
                "lower": self.lower,
                "upper": self.upper,
                "time": self.sample_time,
                "seq": self.seq,
                "sensorCount": len(self.temperatures),
                "sensors": [
                    {
                        "address": f"28FF0000000000{index + 1:02X}",
                        "valid": True,
                        "c": value,
                    }
                    for index, value in enumerate(self.temperatures)
                ],
            }


class MockHandler(BaseHTTPRequestHandler):
    server_version = "ATEArduinoMock/1.0"

    @property
    def state(self) -> DeviceState:
        return self.server.device_state  # type: ignore[attr-defined]

    def send_payload(
        self, status: int, content_type: str, payload: bytes, disposition: str | None = None
    ) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(payload)))
        self.send_header("Cache-Control", "no-store")
        if disposition:
            self.send_header("Content-Disposition", disposition)
        self.end_headers()
        self.wfile.write(payload)

    def send_json(self, payload: dict, status: int = 200) -> None:
        self.send_payload(
            status,
            "application/json",
            json.dumps(payload, separators=(",", ":")).encode("utf-8"),
        )

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        status = self.state.status()
        if parsed.path == "/":
            self.send_payload(200, "text/html", b"<h1>ATE Arduino mock</h1>")
        elif parsed.path == "/api/status":
            self.send_json(status)
        elif parsed.path == "/api/history":
            self.send_json(
                {
                    "time": status["time"],
                    "seq": status["seq"],
                    "temperatures": [item["c"] for item in status["sensors"]],
                }
            )
        elif parsed.path == "/api/latest":
            query = parse_qs(parsed.query)
            try:
                sensor = int(query.get("sensor", ["0"])[0])
                temperature = status["sensors"][sensor]["c"]
            except (ValueError, IndexError):
                self.send_payload(404, "text/plain", b"Sensor unavailable")
                return
            payload = f"time,temperature\n{status['time']},{temperature:.3f}\n".encode()
            self.send_payload(200, "text/csv", payload)
        elif parsed.path == "/api/csv":
            rows = ["time_s,sensor,temperature_c"]
            for index, sensor in enumerate(status["sensors"]):
                rows.append(f"{status['time']},{index},{sensor['c']:.3f}")
            payload = ("\n".join(rows) + "\n").encode()
            self.send_payload(
                200,
                "text/csv",
                payload,
                'attachment; filename="temperature-log.csv"',
            )
        else:
            self.send_payload(404, "text/plain", b"Not found")

    def do_POST(self) -> None:
        length = int(self.headers.get("Content-Length", "0"))
        form = parse_qs(self.rfile.read(length).decode("utf-8"))
        if self.path == "/api/control":
            action = form.get("action", [""])[0]
            if action not in {"start", "stop"}:
                self.send_json({"ok": False, "message": "Invalid action"}, 400)
                return
            with self.state.lock:
                self.state.running = action == "start"
                self.state.last_sample = time.monotonic() - (
                    self.state.interval if self.state.running else 0
                )
            self.send_json({"ok": True, "message": f"Acquisition {action}ed"})
        elif self.path == "/api/config":
            try:
                interval = int(form["interval"][0])
                lower = float(form["lower"][0])
                upper = float(form["upper"][0])
                if not 1 <= interval <= 3600 or not -55 <= lower < upper <= 125:
                    raise ValueError
            except (KeyError, ValueError):
                self.send_json({"ok": False, "message": "Invalid configuration"}, 400)
                return
            with self.state.lock:
                self.state.interval = interval
                self.state.lower = lower
                self.state.upper = upper
            self.send_json({"ok": True, "message": "Configuration applied"})
        else:
            self.send_payload(404, "text/plain", b"Not found")

    def log_message(self, format: str, *args: object) -> None:
        return


def create_server(host: str = "127.0.0.1", port: int = 0) -> ThreadingHTTPServer:
    server = ThreadingHTTPServer((host, port), MockHandler)
    server.device_state = DeviceState()  # type: ignore[attr-defined]
    return server


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    server = create_server(args.host, args.port)
    print(f"Mock Arduino listening on http://{args.host}:{server.server_port}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
