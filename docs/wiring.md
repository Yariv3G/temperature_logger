# Wiring and sensor-bus guidance

## DS18B20 bus

Use externally powered, three-wire mode for reliable longevity testing.

| DS18B20 lead | Arduino Uno connection |
| --- | --- |
| VCC (usually red) | 5 V |
| GND (usually black) | GND |
| DATA (usually yellow or white) | D2 |

Install one 4.7 kΩ resistor between D2 and 5 V. All probes share the same
three conductors and are identified by their unique 64-bit OneWire addresses.
Check the probe vendor's datasheet before trusting lead colours.

```text
Arduino 5 V  -----+------------- VCC (all probes)
                  |
                 4.7 kΩ
                  |
Arduino D2   -----+------------- DATA (all probes)
Arduino GND  ------------------- GND (all probes)
```

Prefer a daisy-chain or linear trunk. Avoid long star branches. For an ATE
fixture, 1–10 probes on one bus is a conservative target. More may work, but
cable capacitance, topology, EMI, pull-up value, and supply quality become the
limiting factors. The firmware discovers up to eight probes by default; change
`MAX_SENSORS` in `config.h` only after validating the physical bus.

## Shield and alarm pins

The default firmware targets an Arduino Ethernet Shield using W5100:

| Function | Pin |
| --- | --- |
| DS18B20 OneWire data | D2 |
| SD card chip select | D4 |
| Active-high TTL alarm | D6 |
| W5100 chip select | D10 |
| SPI bus | D11–D13 |

The D6 output is 5 V logic, not a power output. Connect the monitored device
ground to Arduino GND. Use a transistor, optocoupler, or relay driver for loads;
do not drive a relay coil directly from D6.

The alarm is HIGH while acquisition is running and any valid temperature is
outside the configured limits. A disconnected/invalid sensor also asserts the
alarm as a fail-safe condition. Stop forces D6 LOW.
