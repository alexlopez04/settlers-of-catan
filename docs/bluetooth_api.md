# Settlers of Catan — Bluetooth Low Energy API

## Overview

Each ESP32-C6 player station exposes a BLE GATT server.  Mobile devices can connect to receive live game-state updates and send player commands with full parity to the physical buttons on the station.

---

## Advertising

| Field          | Value                                  |
|----------------|----------------------------------------|
| Device name    | `Catan-P1` … `Catan-P4`               |
| AD flags       | LE General Discoverable, BR/EDR unsupported |
| Advertised UUID| Catan Service (see below)              |
| Interval       | 100–200 ms                             |

The device name suffix corresponds to the player number (1-based).  Only one concurrent BLE connection is supported per station.

---

## GATT Service

**Service UUID**: `CA7A0001-CA7A-4C4E-8000-00805F9B34FB`

### Characteristics

#### GameState  —  `CA7A0002-CA7A-4C4E-8000-00805F9B34FB`

| Property   | Supported |
|------------|-----------|
| READ       | ✓         |
| NOTIFY     | ✓         |
| WRITE      | ✗         |

**Payload**: NanoPB-encoded `BoardToPlayer` protobuf message (raw bytes, no framing).

**Behaviour**:
- **READ**: Returns the most recently received game state at any time.
- **NOTIFY**: A notification is pushed to all subscribed centrals immediately after the board delivers a new `BoardToPlayer` message over I2C.  Enable notifications by writing `0x0001` to the CCCD (standard BLE procedure).

---

#### Command  —  `CA7A0003-CA7A-4C4E-8000-00805F9B34FB`

| Property       | Supported |
|----------------|-----------|
| READ           | ✗         |
| WRITE          | ✓         |
| WRITE NO RSP   | ✓         |

**Payload**: NanoPB-encoded `PlayerToBoard` protobuf message (raw bytes, no framing).

**Behaviour**: Writing this characteristic injects a command into the station, identical in effect to pressing a physical button.  The station processes the command and forwards it to the central board on the next I2C read cycle.

---

## Protobuf Definitions

All messages are defined in [`proto/catan.proto`](../proto/catan.proto) and compiled with [NanoPB](https://jpa.kapsi.fi/nanopb/) for embedded targets.

### `BoardToPlayer` (GameState payload — station → mobile)

| Field            | Type     | Description                                          |
|------------------|----------|------------------------------------------------------|
| `proto_version`  | uint32   | Schema version; currently **2**                      |
| `type`           | MsgType  | Always `MSG_GAME_STATE` for state notifications      |
| `phase`          | GamePhase| Current game phase (see enum below)                  |
| `current_player` | uint32   | 0-based index of the player whose turn it is         |
| `num_players`    | uint32   | Number of connected players                          |
| `your_player_id` | uint32   | 0-based ID assigned to this station                  |
| `die1`           | uint32   | Value of the first die (1–6)                         |
| `die2`           | uint32   | Value of the second die (1–6)                        |
| `dice_total`     | uint32   | Sum of both dice                                     |
| `reveal_number`  | uint32   | Current number being revealed (NUMBER_REVEAL phase)  |
| `line1`          | string   | Primary status text (max 22 chars)                   |
| `line2`          | string   | Secondary status text (max 22 chars)                 |
| `btn_left`       | string   | Label for the left action (max 8 chars)              |
| `btn_center`     | string   | Label for the centre action (max 8 chars)            |
| `btn_right`      | string   | Label for the right action (max 8 chars)             |
| `vp0`–`vp3`      | uint32   | Victory points for player slots 0–3                  |
| `res_lumber`     | uint32   | Lumber cards held by **this** player                 |
| `res_wool`       | uint32   | Wool cards                                           |
| `res_grain`      | uint32   | Grain cards                                          |
| `res_brick`      | uint32   | Brick cards                                          |
| `res_ore`        | uint32   | Ore cards                                            |
| `winner_id`      | uint32   | Winner player ID; `0xFF` if no winner yet            |
| `setup_round`    | uint32   | Setup round number (1 or 2)                          |
| `has_rolled`     | bool     | True if the current player has already rolled        |

### `PlayerToBoard` (Command payload — mobile → station)

| Field    | Type         | Description                                                    |
|----------|--------------|----------------------------------------------------------------|
| `type`   | MsgType      | `MSG_BUTTON_EVENT` or `MSG_ACTION`                             |
| `button` | ButtonId     | Raw button press (see enum).  Use when mapping directly.       |
| `action` | PlayerAction | Semantic action (see enum).  **Preferred for mobile clients.** |

> When `action` is non-zero the station maps it to the appropriate `ButtonId` before forwarding to the board, so the board firmware requires no changes.

---

## Enums

### `GamePhase`

| Value | Name                      | Description                              |
|-------|---------------------------|------------------------------------------|
| 0     | `PHASE_WAITING_FOR_PLAYERS` | Lobby — waiting for players to connect |
| 1     | `PHASE_BOARD_SETUP`         | Board tiles being randomised           |
| 2     | `PHASE_NUMBER_REVEAL`       | Revealing number tokens one by one     |
| 3     | `PHASE_INITIAL_PLACEMENT`   | Players place starting settlements     |
| 4     | `PHASE_PLAYING`             | Main game loop                         |
| 5     | `PHASE_ROBBER`              | Current player must move the robber    |
| 6     | `PHASE_TRADE`               | Active player is trading               |
| 7     | `PHASE_GAME_OVER`           | Game ended                             |

### `ButtonId`

| Value | Name         |
|-------|--------------|
| 0     | `BTN_NONE`   |
| 1     | `BTN_LEFT`   |
| 2     | `BTN_CENTER` |
| 3     | `BTN_RIGHT`  |

### `PlayerAction` (semantic — preferred for BLE)

| Value | Name                 | Phase context              | Equivalent button |
|-------|----------------------|----------------------------|-------------------|
| 0     | `ACTION_NONE`        | —                          | —                 |
| 1     | `ACTION_BTN_LEFT`    | Any                        | `BTN_LEFT`        |
| 2     | `ACTION_BTN_CENTER`  | Any                        | `BTN_CENTER`      |
| 3     | `ACTION_BTN_RIGHT`   | Any                        | `BTN_RIGHT`       |
| 4     | `ACTION_ROLL_DICE`   | `PHASE_PLAYING` (pre-roll) | `BTN_LEFT`        |
| 5     | `ACTION_END_TURN`    | `PHASE_PLAYING` (post-roll)| `BTN_RIGHT`       |
| 6     | `ACTION_TRADE`       | `PHASE_TRADE`              | `BTN_LEFT`        |
| 7     | `ACTION_SKIP_ROBBER` | `PHASE_ROBBER`             | `BTN_CENTER`      |
| 8     | `ACTION_PLACE_DONE`  | `PHASE_INITIAL_PLACEMENT`  | `BTN_CENTER`      |
| 9     | `ACTION_START_GAME`  | `PHASE_WAITING_FOR_PLAYERS`| `BTN_LEFT`        |
| 10    | `ACTION_NEXT_NUMBER` | `PHASE_NUMBER_REVEAL`      | `BTN_CENTER`      |

---

## Wire Format

| Transport | Format                          |
|-----------|---------------------------------|
| **BLE**   | Raw NanoPB bytes (no header)    |
| **I2C**   | `[0xCA][payload_len][pb_bytes]` |

BLE ATT packets carry their own length so no additional framing is needed.  The I2C path uses the `0xCA` magic byte + 1-byte length prefix to allow robust frame validation on the slave side.

---

## Connecting: Step-by-Step (mobile)

1. Scan for devices advertising service UUID `CA7A0001-CA7A-4C4E-8000-00805F9B34FB` or named `Catan-P*`.
2. Connect to the desired station.
3. Discover the Catan service and its characteristics.
4. Write `0x0001` to the CCCD of the **GameState** characteristic to enable notifications.
5. Optionally READ the **GameState** characteristic to get the current state immediately.
6. Process incoming **GameState** notifications; decode using the `BoardToPlayer` protobuf schema.
7. To perform a player action, encode a `PlayerToBoard` message with the desired `action` field and WRITE it to the **Command** characteristic.

### Minimal command example (pseudo-code)

```python
# Encode ACTION_ROLL_DICE using the protobuf library of your choice
cmd = PlayerToBoard()
cmd.type   = MSG_ACTION
cmd.action = ACTION_ROLL_DICE
payload = cmd.SerializeToString()
ble_client.write_characteristic(COMMAND_UUID, payload)
```

### Minimal subscription example (pseudo-code)

```python
def on_notify(data: bytes):
    state = BoardToPlayer()
    state.ParseFromString(data)
    print(f"Phase={state.phase}, Turn=P{state.current_player+1}, "
          f"Dice={state.die1}+{state.die2}={state.dice_total}")

ble_client.enable_notify(GAMESTATE_UUID, on_notify)
```

---

## Architecture Notes

- **One connection only**: The station supports one BLE central at a time.  After disconnect it automatically restarts advertising.
- **State freshness**: Notifications are edge-triggered (sent on every I2C state message from the board, typically every 200 ms during active play).  A READ on the GameState characteristic returns the last known state even when no notification has been received.
- **Command ordering**: BLE commands and physical button presses share the same pending-input slot.  A physical button press takes priority if both are pending simultaneously (extremely unlikely in practice).
- **Proto version**: Clients should check `proto_version == 2` and warn on mismatch.  Unknown fields are safely ignored by NanoPB on both sides.
