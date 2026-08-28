#!/usr/bin/env python3
"""Smoke-test the logger API and host collector, with optional live hardware."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from mock_arduino_server import create_server


ROOT = Path(__file__).resolve().parents[1]
COLLECTOR = ROOT / "host" / "collector.py"


def request(base_url: str, path: str, form: dict[str, str] | None = None) -> bytes:
    data = urlencode(form).encode() if form is not None else None
    req = Request(f"{base_url}{path}", data=data)
    with urlopen(req, timeout=3) as response:
        if response.status != 200:
            raise AssertionError(f"{path} returned HTTP {response.status}")
        return response.read()


class OfflineSmokeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.server = create_server()
        cls.base_url = f"http://127.0.0.1:{cls.server.server_port}"
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def test_api_control_configuration_and_csv(self) -> None:
        initial = json.loads(request(self.base_url, "/api/status"))
        self.assertFalse(initial["running"])
        self.assertEqual(initial["sensorCount"], 2)

        configured = json.loads(
            request(
                self.base_url,
                "/api/config",
                {"interval": "1", "lower": "20", "upper": "30"},
            )
        )
        self.assertTrue(configured["ok"])

        started = json.loads(
            request(self.base_url, "/api/control", {"action": "start"})
        )
        self.assertTrue(started["ok"])
        running = json.loads(request(self.base_url, "/api/status"))
        self.assertTrue(running["running"])

        latest = request(self.base_url, "/api/latest?sensor=0").decode()
        self.assertEqual(latest.splitlines()[0], "time,temperature")
        self.assertEqual(len(list(csv.reader(latest.splitlines()))[1]), 2)

        history = json.loads(request(self.base_url, "/api/history?sensor=0"))
        self.assertEqual(history["sensor"], 0)
        self.assertEqual(len(history["points"][0]), 2)

        downloaded = request(self.base_url, "/api/csv").decode()
        self.assertEqual(
            downloaded.splitlines()[0], "time_s,sensor,temperature_c"
        )

        stopped = json.loads(
            request(self.base_url, "/api/control", {"action": "stop"})
        )
        self.assertTrue(stopped["ok"])
        self.assertFalse(json.loads(request(self.base_url, "/api/status"))["running"])

    def test_collector_writes_two_column_csv(self) -> None:
        request(self.base_url, "/api/control", {"action": "start"})
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "run.csv"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(COLLECTOR),
                    "--base-url",
                    self.base_url,
                    "--sensor",
                    "0",
                    "--output",
                    str(output),
                    "--poll",
                    "0.1",
                    "--duration",
                    "1.4",
                ],
                check=False,
                capture_output=True,
                text=True,
                timeout=8,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            with output.open(newline="", encoding="utf-8") as stream:
                rows = list(csv.reader(stream))
            self.assertGreaterEqual(len(rows), 2)
            self.assertEqual(rows[0], ["time", "temperature"])
            self.assertTrue(all(len(row) == 2 for row in rows))
        request(self.base_url, "/api/control", {"action": "stop"})


def live_smoke(ip: str) -> None:
    base_url = f"http://{ip}"
    status = json.loads(request(base_url, "/api/status"))
    required = {"running", "alarm", "interval", "sensorCount", "sensors", "seq"}
    missing = required.difference(status)
    if missing:
        raise AssertionError(f"Live status is missing fields: {sorted(missing)}")
    latest = request(base_url, "/api/latest?sensor=0").decode().splitlines()
    if not latest or latest[0] != "time,temperature":
        raise AssertionError("Live latest endpoint has an invalid CSV header")
    print(
        f"Live smoke passed: {status['sensorCount']} sensor(s), "
        f"running={status['running']}, alarm={status['alarm']}"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--live", action="store_true", help="Also test real hardware")
    parser.add_argument("--ip", default="10.100.102.247")
    args = parser.parse_args()

    suite = unittest.defaultTestLoader.loadTestsFromTestCase(OfflineSmokeTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if not result.wasSuccessful():
        return 1
    if args.live:
        live_smoke(args.ip)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
