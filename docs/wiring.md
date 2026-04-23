# Wiring

Hardware topology for the new single-hub architecture.

```
                       ┌────────────────┐
                       │  Phone (×1..4) │
                       └───────┬────────┘
                               │   BLE GATT (NimBLE)
                               │   Service CA7A0001
                               ▼
                    ┌──────────────────────┐
                    │  ESP32-C6-WROOM-1    │
                    │  "Catan-Board" hub   │
                    │  (one device, up to  │
                    │   4 BLE centrals)    │
                    └────────┬─────────────┘
                             │ UART (3.3V)
                             │ 115200 8N1
                             │ + level shifter
                             ▼
                    ┌──────────────────────┐
                    │  Arduino Mega 2560   │
                    │  Game FSM + LEDs     │
                    │  + I²C sensor master │
                    └────────┬─────────────┘
                             │ I²C @100 kHz (5 V)
                             ▼
              ┌──────────────────────────────┐
              │  PCF8574(A) input expanders  │
              │  0x20 .. 0x27 (presence)     │
              └──────────────────────────────┘
```

There is **only one ESP32**. All player phones connect to the same hub over
Bluetooth; the hub multiplexes everything onto a single UART link to the Mega.

---

## ESP32-C6 ↔ Arduino Mega (UART)

| Signal | ESP32-C6 (GPIO) | Mega (Serial1) | Notes                            |
|--------|-----------------|----------------|----------------------------------|
| TX1    | **GPIO4**       | RX1 (pin 19)   | hub → Mega; level shift required |
| RX1    | **GPIO5**       | TX1 (pin 18)   | Mega → hub; level shift required |
| GND    | GND             | GND            | common ground                    |

> **Why not GPIO16/17?** On the ESP32-C6 those are UART0 pins, wired to the
> USB-to-serial bridge used for logging. Assigning `Serial1` to them would
> mix binary link frames into the USB console output.

* Baud: **115200 8N1**
* The Mega runs at **5 V** and the ESP32-C6 at **3.3 V** — use a bidirectional
  level shifter (e.g. TXS0108E or two MOSFET stages) on both lines. Driving
  the C6 RX directly from a 5 V Mega TX will damage the C6.
* Wire format: `[0xCA magic][type:u8][len:u8][payload..len][crc8]`
  (CRC-8 polynomial 0x07, init 0x00, covers `type ∥ len ∥ payload`).
* Three message types are exchanged on this link:
  - `0x01 BoardState`     — Mega → hub, every 200 ms (full game snapshot)
  - `0x02 PlayerInput`    — hub → Mega, on every player BLE write
  - `0x03 PlayerPresence` — hub → Mega, every BLE connect/disconnect + 1 Hz keep-alive

## ESP32-C6 power & boot

* 5 V or USB power into the dev board's regulator (≥ 500 mA budget).
* Decoupling: 100 nF + 10 µF as close to 3V3 as feasible.
* No additional GPIO is required from the C6 — it has no LEDs or sensors of
  its own; it only does BLE and UART.

## Mega ↔ sensor expanders (I²C)

The I²C bus on the Mega is **dedicated to sensor input** now (the player
slaves are gone). Pull-ups: 4.7 kΩ each on SDA/SCL to 5 V.

| Mega pin | Function | Goes to                |
|----------|----------|------------------------|
| 20 (SDA) | I²C SDA  | PCF8574 SDA, all units |
| 21 (SCL) | I²C SCL  | PCF8574 SCL, all units |
| 5 V      | VCC      | PCF8574 VCC            |
| GND      | GND      | PCF8574 GND            |

Expander addresses (8 boards = 64 inputs total) and their assignment to
vertices / edges / tiles are defined in
[`firmware/board/src/pin_map.cpp`](../firmware/board/src/pin_map.cpp).

## LEDs

WS2812 strips are driven from a Mega GPIO via a level shifter (74AHCT125 or
similar). See [`firmware/board/src/led_map.cpp`](../firmware/board/src/led_map.cpp)
for the LED-index → tile/port mapping.

---

## Quick checklist

1. Common ground between Mega, ESP32-C6, level shifter, and LED PSU.
2. Level-shift both UART directions; **never** tie 5 V Mega TX directly to the C6.
3. I²C pull-ups present on the sensor bus (and only there).
4. One — and only one — ESP32-C6 powered up on the bench. Multiple boards
   advertising as `Catan-Board` will confuse phones.
