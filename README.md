# DS18B20 ATE Temperature Logger

Web-based temperature acquisition for longevity tests, with two supported
controllers:

| Variant | Network | Landing-page storage | Durable logging |
| --- | --- | --- | --- |
| ESP32 DevKit | Wi-Fi | Embedded in ESP32 flash | Host PC collector |
| Arduino Uno + W5100/W5500 shield | Ethernet | Shield SD card | SD + host PC |

Both variants expose the same browser controls and compatible HTTP API:

- live time-versus-temperature graph
- automatic DS18B20 discovery
- Start/Stop
- 1/2/5/10/custom-second sampling
- upper/lower limits
- fail-safe active-high TTL alarm
- two-column host CSV output (`time,temperature`)
- per-sensor current/minimum/maximum/average values for the active run
- Refresh plus cumulative, Zoom In, and Zoom Out graph controls

## Project structure

```text
firmware/
├── esp32_wifi_logger/       ESP32 Wi-Fi firmware and embedded landing page
└── uno_ethernet_logger/     Uno Ethernet firmware and SD landing page
host/
└── collector.py             Shared durable PC CSV collector
tests/
├── smoke_test.py            Offline API/collector smoke suite
└── mock_arduino_server.py   Hardware-independent API simulator
docs/
├── esp32-wiring.md
└── uno-wiring.md
```

Controller-specific installation:

- [`firmware/esp32_wifi_logger/README.md`](firmware/esp32_wifi_logger/README.md)
- [`firmware/uno_ethernet_logger/README.md`](firmware/uno_ethernet_logger/README.md)

## ESP32 quick start

The current ESP32 target is a classic **ESP32 Dev Module / ESP-WROOM-32**.
Install Espressif's `esp32` board package and `OneWire`, then:

1. Upload `firmware/esp32_wifi_logger/esp32_wifi_logger.ino`.
2. Open Serial Monitor at 115200 baud.
3. Press `c` during the 3-second boot window to configure Wi-Fi, or wait for
   saved credentials to connect.
4. Enter the SSID and password when prompted, verify the printed temperature
   readings, then press ENTER.
5. Browse to the IP address printed on Serial (for example
   `http://192.168.1.42/`).

Connect DS18B20 DATA to GPIO27 with one 4.7 kΩ pull-up to 3V3. GPIO26 is the
3.3 V active-high alarm. See [`docs/esp32-wiring.md`](docs/esp32-wiring.md).

## Host CSV collector

The collector works with either controller and writes exactly two columns:

```powershell
python host\collector.py --ip <esp-ip-from-serial> --sensor 0 --output data\run.csv
```

It records only new samples while acquisition is running and reconnects after
temporary network failures. Run one process per sensor when separate files are
required.

## ESP32 graph and statistics

Selecting **Start** begins a new run and resets the ESP32's run statistics and
300-sample recovery buffer. The browser continues accumulating points for the
full open session. **Zoom In** and **Zoom Out** change the auto-following time
window; **Fit All / Cumulative** displays the complete browser-held run.

**Refresh** synchronizes the latest 300 samples retained by the ESP32 without
discarding older points already held by the browser. A browser opened or
reloaded late can recover only those 300 retained samples. Use the host
collector as the durable record for long longevity tests.

## HTTP API

- `GET /` — landing page
- `GET /api/status` — state, limits, alarm, sensor addresses and readings
- `GET /api/history?sensor=0` — recent in-memory samples
- `POST /api/control` — form field `action=start` or `action=stop`
- `POST /api/config` — `interval`, `lower`, and `upper`
- `GET /api/latest?sensor=0` — latest two-column CSV row
- `GET /api/csv?sensor=0` — available history as two-column CSV
- `GET /api/temperature?metric=current|max|min|average&sensor=0` — fresh JSON reading

## Smoke tests

The hardware-independent suite uses only Python's standard library:

```powershell
python tests\smoke_test.py
```

Optional live test after uploading:

```powershell
python tests\smoke_test.py --live --ip <esp-ip-from-serial>
```

## ATE notes

- Use externally powered three-wire DS18B20 probes.
- Prefer a linear/daisy-chain bus and short stubs.
- Invalid or disconnected probes assert the alarm while running.
- Neither board has a real-time clock; the host collector timestamps rows with
  the PC's ISO-8601 wall-clock time.
- Do not power the Uno and ESP32 variants together on the same network while
  the Uno is configured for `10.100.102.247`.
