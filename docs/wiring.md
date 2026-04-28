# Wiring

Hardware topology for the single-MCU architecture.

```
Phone (x1-4)
     |
     | BLE GATT (NimBLE, up to 4 connections)
     |
     v
ESP32-C6-WROOM-1  ("Catan-Board")
     |          |
     | I2C      | GPIO10
     | 400 kHz  | WS2812B LED strip
     |
     v
PCF8575 sensor expanders (0x20-0x27)
```

One ESP32-C6 owns everything: game FSM, sensors, LEDs, and BLE.
There is no separate hub or game controller MCU.

---

## ESP32-C6 pinout

| GPIO | Function      | Notes                                   |
|------|---------------|-----------------------------------------|
| 6    | I2C SDA       | 4.7 kOhm pull-up to 3.3 V              |
| 7    | I2C SCL       | 4.7 kOhm pull-up to 3.3 V              |
| 10   | LED data      | WS2812B strip (RMT). May need level shift for 5 V strips |

Avoid strapping pins GPIO 8, 9, and 15.

## I2C sensor bus

PCF8575 16-bit input expanders at addresses 0x20-0x27 (8 boards = 128 inputs).

Pull-ups: 4.7 kOhm each on SDA/SCL to 3.3 V. Bus speed: 400 kHz.

Expander addresses and their assignment to vertices, edges, and tiles are
defined in [firmware/src/pin_map.cpp](../firmware/src/pin_map.cpp).

## LEDs

WS2812B strip driven from GPIO10 via FastLED (RMT peripheral). The LED-index
to tile/port mapping is in [firmware/src/led_map.cpp](../firmware/src/led_map.cpp).

If the strip operates at 5 V, use a level shifter (e.g. 74HCT1G125) or
switch to 3.3 V-tolerant SK6812 LEDs.

---

## Quick checklist

1. Common ground between the ESP32-C6, PCF8575 boards, and LED PSU.
2. I2C pull-ups present (4.7 kOhm to 3.3 V).
3. Only one ESP32-C6 powered and advertising as `Catan-Board` at a time.

