# ESP32 Wi-Fi logger

This variant embeds the complete landing page in ESP32 flash. No SD card or
internet connection is needed after installation.

## Setup

1. Install the `esp32` board package by Espressif Systems in Arduino IDE.
2. Install the `OneWire` library.
3. Select **ESP32 Dev Module** and the correct COM port.
4. Open `esp32_wifi_logger.ino`, compile, and upload.
5. Open Serial Monitor at **115200 baud**.

## Serial Wi-Fi deployment

On every boot the logger offers a short serial configuration window:

1. **Scan** — press `c` within 3 seconds to start setup, or wait to connect with
   saved credentials.
2. **Select network** — the firmware scans nearby SSIDs and lists them. Enter the
   list number or type the SSID manually.
3. **Enter password** — type the Wi-Fi password (blank for open networks).
4. **Verify sensors** — the logger connects, takes a temperature reading, and
   prints each DS18B20 value on Serial.
5. **Confirm** — press **ENTER** after verifying the readings. The landing page
   is enabled only after this step.
6. **Note the IP** — Serial prints `Landing page: http://<assigned-ip>/`.

Credentials are stored in ESP32 flash and reused on later boots. Press `c` during
the boot window to reconfigure for another network.

## Pins

- DS18B20 DATA: GPIO27, with a 4.7 kΩ pull-up to 3V3
- TTL alarm: GPIO26, active HIGH

See [`../../docs/esp32-wiring.md`](../../docs/esp32-wiring.md).

## Third-party temperature queries

Each request takes a fresh sensor reading and returns JSON:

- `GET /api/temperature?metric=current&sensor=0`
- `GET /api/temperature?metric=max&sensor=0`
- `GET /api/temperature?metric=min&sensor=0`
- `GET /api/temperature?metric=average&sensor=0`

`max`, `min`, and `average` are computed from the in-memory history buffer plus
the current reading.

## Storage

Selecting **Start** resets the per-sensor minimum, maximum, average, sample
count, and the firmware's history buffer. Statistics therefore describe the
current acquisition run rather than the ESP32's entire uptime.

The browser accumulates the complete run while it remains open. **Zoom In** and
**Zoom Out** adjust an auto-following time window; **Fit All / Cumulative**
shows all browser-held points. **Refresh** merges the latest 300 samples from
the ESP32 without deleting older browser points.

Firmware retains only the latest 300 samples in RAM for `/api/history` and
`/api/csv`, and this buffer is lost on reset. A newly opened browser can recover
only those retained samples. Run
[`../../host/collector.py`](../../host/collector.py) for the durable,
two-column CSV record required by long longevity tests.
