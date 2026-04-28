# Catan App

Expo / React Native mobile app for the Catan electronic board.

Players connect via BLE to the board controller and use this app as their
game display and input surface. The board handles all game logic; the app
renders state and sends player actions.

## Setup

```sh
pnpm install
pnpm start      # Expo dev server
```

For a device build (required for BLE):

```sh
pnpm ios        # requires Xcode + connected device or simulator
pnpm android
```

After adding or removing native dependencies, rebuild the native project:

```sh
pnpm expo prebuild --clean
cd ios && pod install
```

## Structure

| Path                        | Description                              |
|-----------------------------|------------------------------------------|
| `src/app/`                  | Expo Router screens                      |
| `src/components/game/`      | Game phase UI components                 |
| `src/components/ui/`        | Shared UI (board map, overlays)          |
| `src/context/`              | BLE, settings, and tutorial contexts     |
| `src/constants/`            | Board topology, theme, BLE UUIDs         |
| `src/services/proto.ts`     | Hand-rolled protobuf encode/decode       |
| `src/utils/board-rotation.ts` | Board orientation helpers              |

## BLE connection

The app scans for a device named `Catan-Board` and connects to the GATT
service at `CA7A0001-...`. See [../docs/bluetooth_api.md](../docs/bluetooth_api.md)
for the full API.

Player slot is derived from the BLE device name `Catan-P1`..`Catan-P4`
(0-indexed internally as 0..3).

