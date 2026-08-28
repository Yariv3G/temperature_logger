# ESP32 wiring

These defaults target a classic ESP32 DevKit / ESP-WROOM-32.

| Function | ESP32 connection |
| --- | --- |
| DS18B20 VCC | 3V3 |
| DS18B20 GND | GND |
| DS18B20 DATA | GPIO27 |
| OneWire pull-up | 4.7 kΩ between GPIO27 and 3V3 |
| Active-high TTL alarm | GPIO26 |

Wire all DS18B20 probes in parallel on the same three conductors. GPIO27 was
chosen to avoid the common ESP32 boot-strapping pins. The firmware discovers
up to 16 probes; 1–10 probes remains a conservative ATE target unless the bus
has been validated for its actual cable length and topology.

The ESP32 uses 3.3 V logic. Do not pull DATA up to 5 V and do not apply 5 V to
GPIO26. Use a level shifter, transistor, optocoupler, or relay driver when the
external tester requires 5 V TTL or drives a load.

The GPIO26 alarm is HIGH while acquisition is running and any sensor is
outside the configured limits. A missing or invalid probe is treated as a
fail-safe alarm. Stop forces the output LOW.
