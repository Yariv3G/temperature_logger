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
        self.history: list[list[float]] = []
        self.history_times: list[int] = []

    def update(self) -> None:
        now = time.monotonic()
        if not self.running:
            return
        elapsed = now - self.last_sample
        samples = int(elapsed / self.interval)
        if samples <= 0:
            return
        for _ in range(samples):
            self.seq += 1
            self.sample_time += self.interval
            self.temperatures = [
                23.5 + math.sin(self.sample_time / 8.0),
                24.1 + 0.7 * math.sin(self.sample_time / 10.0),
            ]
            self.history.append(list(self.temperatures))
            self.history_times.append(self.sample_time)
        self.history = self.history[-300:]
        self.history_times = self.history_times[-300:]
        self.last_sample += samples * self.interval

    def temperature_metric(self, sensor: int, metric: str) -> float | None:
        with self.lock:
            self.update()
            if sensor < 0 or sensor >= len(self.temperatures):
                return None
            current = self.temperatures[sensor]
            values = [row[sensor] for row in self.history]
            if metric == "current":
                return current
            if not values:
                return None
            if metric == "min":
                return min(values)
            if metric == "max":
                return max(values)
            if metric in {"average", "avg"}:
                return sum(values) / len(values)
            return None

    def sensor_history(self, sensor: int) -> list[list[float]]:
        with self.lock:
            self.update()
            return [
                [sample_time, row[sensor]]
                for sample_time, row in zip(self.history_times, self.history)
            ]

    def status(self) -> dict:
        with self.lock:
            self.update()
            alarm = self.running and any(
                value < self.lower or value > self.upper for value in self.temperatures
            )
            sensors = []
            for index, value in enumerate(self.temperatures):
                values = [row[index] for row in self.history]
                sensors.append(
                    {
                        "address": f"28FF0000000000{index + 1:02X}",
                        "valid": True,
                        "c": value,
                        "min": min(values) if values else None,
                        "max": max(values) if values else None,
                        "average": sum(values) / len(values) if values else None,
                        "sampleCount": len(values),
                    }
                )
            return {
                "running": self.running,
                "alarm": alarm,
                "sd": True,
                "wifi": True,
                "apMode": False,
                "ip": "127.0.0.1",
                "interval": self.interval,
                "lower": self.lower,
                "upper": self.upper,
                "time": self.sample_time,
                "seq": self.seq,
                "sensorCount": len(self.temperatures),
                "sensors": sensors,
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
            sensor = int(parse_qs(parsed.query).get("sensor", ["0"])[0])
            self.send_json(
                {
                    "sensor": sensor,
                    "points": self.state.sensor_history(sensor),
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
        elif parsed.path == "/api/temperature":
            query = parse_qs(parsed.query)
            metric = query.get("metric", [""])[0].lower()
            if metric == "avg":
                metric = "average"
            try:
                sensor = int(query.get("sensor", ["0"])[0])
            except ValueError:
                self.send_json({"ok": False, "message": "Sensor unavailable"}, 404)
                return
            if metric not in {"current", "min", "max", "average"}:
                self.send_json(
                    {"ok": False, "message": "Metric must be current, max, min, or average"},
                    400,
                )
                return
            value = self.state.temperature_metric(sensor, metric)
            if value is None:
                self.send_json({"ok": False, "message": "Sensor unavailable"}, 404)
                return
            self.send_json(
                {
                    "ok": True,
                    "sensor": sensor,
                    "metric": metric,
                    "value": round(value, 3),
                    "unit": "C",
                    "time": status["time"],
                    "valid": True,
                }
            )
        elif parsed.path == "/api/csv":
            sensor = int(parse_qs(parsed.query).get("sensor", ["0"])[0])
            rows = ["time,temperature"]
            rows.extend(
                f"{sample_time},{temperature:.3f}"
                for sample_time, temperature in self.state.sensor_history(sensor)
            )
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
                if self.state.running:
                    self.state.history = []
                    self.state.history_times = []
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
