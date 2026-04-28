# Settlers of Catan

Firmware, protocol, and mobile app for an electronic Settlers of Catan board.

## Architecture

A single ESP32-C6 owns the entire game: FSM, Hall-effect sensor bus
(PCF8575 expanders over I2C), WS2812B LED strip (RMT via FastLED), and
BLE peripheral (NimBLE, up to 4 simultaneous connections). Phone connect
via the mobile app and interact with the board over a Bluetooth API.

See [docs/bluetooth_api.md](docs/bluetooth_api.md) for the GATT contract
and [docs/board_layout.md](docs/board_layout.md) for the coordinate system.

## Project structure

| Path         | Description                                          |
|--------------|------------------------------------------------------|
| `proto/`     | nanopb schema, generation script, options            |
| `firmware/`  | ESP32-C6 firmware (game FSM, LEDs, sensors, BLE)     |
| `app/`       | Expo / React Native mobile app                       |
| `docs/`      | API and board layout reference                       |

## Getting started

### Toolchain

* [PlatformIO](https://platformio.org/) for firmware
* [nanopb](https://jpa.kapsi.fi/nanopb/) + [protoc](https://protobuf.dev/)
  to regenerate `*.pb.{c,h}` files
* [pnpm](https://pnpm.io/) for the mobile app

```sh
brew install platformio nanopb protobuf pnpm
```

### Regenerate protobuf sources

```sh
cd proto
./generate.sh   # writes catan.pb.{c,h} into firmware/src/proto/
```

### Build firmware

```sh
cd firmware

# Production build for the ESP32-C6
pio run -e board

# Host-side simulation of the game FSM (no hardware required)
pio run -e native
./.pio/build/native/program

# I2C expander monitor (useful for sensor mapping)
pio run -e debug_i2c
```

### Mobile app

```sh
cd app
pnpm install
pnpm start      # Expo dev server
# pnpm ios / pnpm android once a development build is installed
```
