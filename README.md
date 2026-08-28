# DS18B20 Ethernet Temperature Logger

Arduino Uno/W5100 firmware and a host-PC collector for temperature longevity
tests. The Arduino samples up to eight DS18B20 probes, logs to the shield's SD
card, serves a browser dashboard at `http://10.100.102.247/`, and drives an
active-high TTL alarm when a temperature leaves its configured window.

## Hardware

- Arduino Uno
- W5100 Arduino Ethernet Shield with SD card
- One or more externally powered DS18B20 probes
- One 4.7 kΩ pull-up resistor
- FAT16/FAT32 SD card (optional but recommended)

Connect every probe in parallel: VCC to 5 V, GND to GND, and DATA to D2. Place
the 4.7 kΩ resistor between D2 and 5 V. TTL alarm output is D6. See
[`docs/wiring.md`](docs/wiring.md) for the pin map and bus-capacity guidance.

## Firmware installation

1. Install the Arduino IDE and select **Arduino Uno**.
2. Install `OneWire`, `Ethernet`, and `SD` from Library Manager.
3. Open `firmware/temperature_logger/temperature_logger.ino`.
4. Review IP, pins, limits, and `MAX_SENSORS` in
   `firmware/temperature_logger/config.h`.
5. Copy [`sd/index.htm`](sd/index.htm) to the root of a FAT16/FAT32 SD card,
   insert it in the shield, then compile and upload the sketch.
6. Open Serial Monitor at 115200 baud for startup status.
7. Put the PC on the `10.100.102.0/24` network and browse to
   `http://10.100.102.247/`.

The dashboard is served from the SD card and works without an internet
connection. It includes a live
time-versus-temperature graph, sensor cards, upper/lower limits, a
1/2/5/10/custom-second sampling control, Start/Stop controls, alarm indication,
and CSV download.

## Logging behavior

The Arduino writes `log.csv` to the SD card with:

```text
time_s,sensor,temperature_c
```

`time_s` is seconds since Arduino boot because the Uno/shield has no real-time
clock. The controlling PC adds ISO-8601 wall-clock time and writes exactly:

```text
time,temperature
```

Run one collector process per desired sensor:

```powershell
python host\collector.py --ip 10.100.102.247 --sensor 0 --output data\run.csv
```

The collector follows the Arduino's configured sample interval and records only
new samples while acquisition is running. It reconnects after temporary network
failures. Press Ctrl+C to close the file cleanly.

## HTTP API

- `GET /api/status` — state, limits, sensor addresses, and latest samples
- `GET /api/history` — latest sample in graph-compatible JSON
- `POST /api/control` with `action=start` or `action=stop`
- `POST /api/config` with form fields `interval`, `lower`, and `upper`
- `GET /api/latest?sensor=0` — latest sample as two-column CSV
- `GET /api/csv` — download the SD log

The Uno has only 2 KB of SRAM, so graph history is retained in the browser and
durable history is kept on SD/PC rather than in an in-memory firmware buffer.

## Tests

The smoke suite uses only the Python standard library and needs no hardware:

```powershell
python tests\smoke_test.py
```

To test a connected Arduino:

```powershell
python tests\smoke_test.py --live --ip 10.100.102.247
```

## Troubleshooting

- **No sensors / `-127 °C`:** verify the 4.7 kΩ pull-up, 5 V supply, lead
  ordering, common ground, and cable topology.
- **Dashboard does not open:** verify link LEDs and that the PC can reach
  `10.100.102.247` on the same subnet. Disable VPN routes that overlap it.
- **SD unavailable:** format the card FAT16/FAT32 and ensure no other SPI device
  drives chip select. Acquisition and PC streaming continue without SD.
- **Unexpected alarm:** an invalid/disconnected sensor is deliberately
  fail-safe and asserts D6 while acquisition is running.
- **Wrong shield:** this firmware uses Arduino's `Ethernet` library for W5100/
  W5500. ENC28J60 shields require different hardware and firmware support.
