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

The current ESP32 target is a classic **ESP32 Dev Module / ESP-WROOM-32** on
COM21. Install Espressif's `esp32` board package and `OneWire`, then:

1. Copy `firmware/esp32_wifi_logger/secrets.example.h` to `secrets.h`.
2. Enter the Wi-Fi credentials in `secrets.h`.
3. Upload `firmware/esp32_wifi_logger/esp32_wifi_logger.ino`.
4. Open Serial Monitor at 115200 baud.
5. Browse to `http://10.100.102.247/`.

If Wi-Fi connection fails, the ESP32 creates `Temperature-Logger-Setup`; join
it and browse to `http://192.168.4.1/`.

Connect DS18B20 DATA to GPIO27 with one 4.7 kΩ pull-up to 3V3. GPIO26 is the
3.3 V active-high alarm. See [`docs/esp32-wiring.md`](docs/esp32-wiring.md).

## Host CSV collector

The collector works with either controller and writes exactly two columns:

```powershell
python host\collector.py --ip 10.100.102.247 --sensor 0 --output data\run.csv
```

It records only new samples while acquisition is running and reconnects after
temporary network failures. Run one process per sensor when separate files are
required.

## HTTP API

- `GET /` — landing page
- `GET /api/status` — state, limits, alarm, sensor addresses and readings
- `GET /api/history?sensor=0` — recent in-memory samples
- `POST /api/control` — form field `action=start` or `action=stop`
- `POST /api/config` — `interval`, `lower`, and `upper`
- `GET /api/latest?sensor=0` — latest two-column CSV row
- `GET /api/csv?sensor=0` — available history as two-column CSV

## Smoke tests

The hardware-independent suite uses only Python's standard library:

```powershell
python tests\smoke_test.py
```

Optional live test after uploading:

```powershell
python tests\smoke_test.py --live --ip 10.100.102.247
```

## ATE notes

- Use externally powered three-wire DS18B20 probes.
- Prefer a linear/daisy-chain bus and short stubs.
- Invalid or disconnected probes assert the alarm while running.
- Neither board has a real-time clock; the host collector timestamps rows with
  the PC's ISO-8601 wall-clock time.
- Do not power the Uno and ESP32 variants together while both use
  `10.100.102.247`.
