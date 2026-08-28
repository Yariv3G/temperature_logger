# ESP32 Wi-Fi logger

This variant embeds the complete landing page in ESP32 flash. No SD card or
internet connection is needed after installation.

## Setup

1. Install the `esp32` board package by Espressif Systems in Arduino IDE.
2. Install the `OneWire` library.
3. Select **ESP32 Dev Module** and COM21.
4. Copy `secrets.example.h` to `secrets.h` and enter the Wi-Fi SSID/password.
   `secrets.h` is excluded from git.
5. Open `esp32_wifi_logger.ino`, compile, and upload.
6. Open Serial Monitor at 115200 baud to see the selected URL.

The default station address is `http://10.100.102.247/`. Do not run the Uno
and ESP32 versions simultaneously with that address. If station connection
fails after 20 seconds, join the open `Temperature-Logger-Setup` access point
and open `http://192.168.4.1/`.

## Pins

- DS18B20 DATA: GPIO27, with a 4.7 kΩ pull-up to 3V3
- TTL alarm: GPIO26, active HIGH

See [`../../docs/esp32-wiring.md`](../../docs/esp32-wiring.md).

## Storage

The embedded graph retains 300 points in the browser. Firmware also retains
the latest 300 samples in RAM for `/api/history` and `/api/csv`. This buffer is
lost on reset. Run [`../../host/collector.py`](../../host/collector.py) for a
durable, two-column PC CSV file during longevity tests.
