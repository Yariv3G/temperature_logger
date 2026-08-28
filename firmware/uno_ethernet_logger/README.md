# Arduino Uno + Ethernet shield logger

This variant targets an Arduino Uno with a W5100/W5500 Ethernet shield and SD
slot. It uses static address `10.100.102.247`.

## Setup

1. Install `OneWire`, `Ethernet`, and `SD` in Arduino IDE.
2. Copy `sd/index.htm` to the root of a FAT16/FAT32 SD card.
3. Insert the card into the shield.
4. Select **Arduino Uno**, open `uno_ethernet_logger.ino`, and upload.
5. Browse to `http://10.100.102.247/`.

The complete landing page is stored on SD because the Uno sketch already uses
nearly all of its 32 KB flash. A small setup page is available if `index.htm`
is absent.

See [`../../docs/uno-wiring.md`](../../docs/uno-wiring.md) for wiring and
electrical guidance.
