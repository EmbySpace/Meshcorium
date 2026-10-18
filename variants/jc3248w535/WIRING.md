# JC3248W535 + EBYTE E22-900M30S — wiring

Firmware env: `JC3248W535_companion_radio_ble_usb_wifi`

## Signal wires

| E22-900M30S pin | Signal | Board connector | Board GPIO |
|---|---|---|---|
| 18 | SCK  | P2 | IO6  |
| 17 | MOSI | P2 | IO7  |
| 16 | MISO | P2 | IO15 |
| 14 | BUSY | P2 | IO16 |
| 13 | DIO1 | P2 | IO9  |
| 6  | RXEN | P2 | IO14 |
| 19 | NSS  | P3 | IO17 |
| 15 | NRST | P3 | IO18 |

## On the module itself

| From | To | Why |
|---|---|---|
| E22 pin 7 TXEN | E22 pin 8 DIO2 | SX1262 drives the TX switch through DIO2 (firmware: `SX126X_DIO2_AS_RF_SWITCH`) |

## Power

| E22-900M30S pin | Connect to |
|---|---|
| 9, 10 VCC | +5 V boost converter output (datasheet: 5.0–5.5 V for full power, >5.5 V destroys the module, peak TX current ~650 mA) |
| any GND (1–5, 11, 12, 20, 22) | boost converter GND **and** P3 GND (common ground with the board) |
| 21 ANT | antenna — never transmit without an antenna |

The datasheet recommends an external filter capacitor on VCC. Suggested (not from the datasheet): 100 µF electrolytic + 100 nF ceramic across VCC/GND, right at the module.

## Not used

* IO5 (P2) — connected on the board to the battery voltage divider (R25/R26/R27); the firmware uses it to measure the Li-ion battery on P5. Leave this P2 pin unconnected.
* IO46 (P2) — ESP32-S3 strapping pin; must not be high while entering download mode.
* P3/P4 3V3 — the E22-900M30S is powered from 5 V, not from 3V3.

## P2 connector order (as printed on the PCB, top → bottom)

IO5 · IO6 · IO7 · IO15 · IO16 · IO46 · IO9 · IO14

## P3 connector order (as printed on the PCB)

GND · 3V3 · IO17 · IO18  (P4, the small QWIIC socket, carries the same four signals)
