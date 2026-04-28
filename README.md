# Settlers of Catan

Firmware, protocol, and mobile app for the Settlers of Catan senior design
project.

## Architecture

```
phone(s)  ──BLE──▶  ESP32-C6 hub  ──UART──▶  Arduino Mega  ──I²C──▶  PCF8574 sensors
                   ("Catan-Board")            (game FSM, LEDs)
```

* The Mega owns the game finite-state machine, the LED strip, and the I²C
  sensor bus.
* A single ESP32-C6 acts as the BLE hub. It accepts up to 4 simultaneous
  phone connections, hands each a stable player slot (0..3) backed by NVS,
  and bridges everything to/from the Mega over a framed UART link.
* Phones speak only BLE — they never talk I²C and never see the UART frame
  envelope.

See [docs/wiring.md](docs/wiring.md) for the pinout and
[docs/bluetooth_api.md](docs/bluetooth_api.md) for the GATT contract.

## Project structure

| Path           | Description                                           |
|----------------|-------------------------------------------------------|
| `proto/`       | nanopb schema, generation script, options             |
| `firmware/board/` | Arduino Mega 2560 firmware (game FSM, LEDs, sensors) |
| `firmware/hub/`   | ESP32-C6 BLE hub firmware (NimBLE multi-conn)        |
| `app/`         | Expo / React Native mobile app                        |
| `docs/`        | Wiring + Bluetooth API documentation                  |

## Getting started

### Toolchain

* [PlatformIO](https://platformio.org/) for both firmware targets
* [nanopb](https://jpa.kapsi.fi/nanopb/) + [protoc](https://protobuf.dev/)
  to regenerate `*.pb.{c,h}` files
* [pnpm](https://pnpm.io/) for the mobile app

```sh
brew install platformio nanopb protobuf pnpm
```

### Regenerate protobuf sources

```sh
cd proto
./generate.sh           # writes into firmware/board/src/proto and firmware/hub/src/proto
```

### Build firmware

```sh
# Mega game controller
cd firmware/board
pio run -e megaatmega2560

# ESP32-C6 BLE hub
cd firmware/hub
pio run -e hub

# Native simulation of the game FSM (no hardware required)
cd firmware/board
pio run -e native -t exec
```

### Mobile app

```sh
cd app
pnpm install
pnpm start              # Expo dev server
# pnpm ios / pnpm android once a development build is installed
```

The first time the app launches it generates a UUIDv4 client identifier and
stores it via AsyncStorage. That identifier is what the hub uses to keep your
slot consistent across reconnects.
