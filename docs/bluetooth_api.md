# Bluetooth API

The ESP32-C6 hub firmware advertises a single GATT service that all phones
connect to. Up to **4 simultaneous BLE centrals** are supported; each one is
assigned a player slot (0..3) by the hub.

* Device name: `Catan-Board`
* Negotiated MTU: 247 (we never split a payload — keep messages ≤ 200 bytes)
* Wire schema version: **6** (see [`proto/catan.proto`](../proto/catan.proto))
* Encoding on BLE: **bare nanopb bytes** (no `[0xCA][len]` envelope; the hub
  adds/strips the UART frame on its way to the Mega)

---

## Service `CA7A0001-CA7A-4C4E-8000-00805F9B34FB`

| Char UUID  | Name     | Properties        | Format                                           |
|------------|----------|-------------------|--------------------------------------------------|
| `CA7A0002` | State    | read, notify      | `BoardState` (nanopb)                            |
| `CA7A0003` | Input    | write             | `PlayerInput` (nanopb)                           |
| `CA7A0004` | Identity | write             | UTF-8 client_id (max 39 bytes + NUL)             |
| `CA7A0005` | Slot     | read, notify      | 1 byte: assigned player_id (0..3) or 0xFF        |

### Connection flow

```
phone                                                 hub
  │                                                    │
  │ 1. connect (request MTU 247)                       │
  │──────────────────────────────────────────────────▶ │
  │                                                    │
  │ 2. subscribe to State + Slot                       │
  │──────────────────────────────────────────────────▶ │
  │                                                    │
  │ 3. write Identity (client_id)                      │
  │──────────────────────────────────────────────────▶ │
  │                                                    │  ← hub looks up
  │                                                    │    or assigns slot,
  │                                                    │    persists in NVS
  │                                                    │
  │ 4. notify Slot (player_id)                         │
  │ ◀──────────────────────────────────────────────────│
  │                                                    │
  │ 5. notify State (cadence ~5 Hz)                    │
  │ ◀──────────────────────────────────────────────────│
  │                                                    │
  │ 6. write Input as the player taps buttons          │
  │ ──────────────────────────────────────────────────▶│
```

* The phone **must write Identity within 4 s** of connecting, or the hub
  drops the connection. (BLE addresses can rotate due to privacy randomisation,
  so the address alone cannot be trusted as identity.)
* The Slot characteristic returns `0xFF` until an identity is bound.
* Slot assignment is sticky: the hub stores `client_id → slot` in NVS
  (namespace `catan`, keys `slot0`..`slot3`). When the same phone reconnects
  (even after a power cycle on either side), the same slot is restored.
* If all four slots are occupied with stale bindings, the hub recycles the
  lowest disconnected slot for the new identity.

---

## `BoardState` (hub → mobile, notify)

Field tags (see `proto/catan.proto`):

| Tag | Name              | Type         | Notes                                          |
|-----|-------------------|--------------|------------------------------------------------|
| 1   | `proto_version`   | uint32       | Always 6; reject if mismatched                 |
| 2   | `phase`           | enum         | LOBBY / BOARD_SETUP / NUMBER_REVEAL / ...      |
| 3   | `num_players`     | uint32       | Highest occupied slot index + 1                |
| 4   | `current_player`  | uint32       | 0..3                                           |
| 5   | `setup_round`     | uint32       | 0 or 1 during INITIAL_PLACEMENT                |
| 6   | `has_rolled`      | bool         | Current player rolled this turn                |
| 7   | `die1`, 8 `die2`  | uint32       | Last roll (0 if not rolled)                    |
| 9   | `reveal_number`   | uint32       | 2..12 during NUMBER_REVEAL, else 0             |
| 10  | `winner_id`       | uint32       | 0..3, or `0xFF` if no winner                   |
| 11  | `robber_tile`     | uint32       | 0..18, or `0xFF`                               |
| 12  | `connected_mask`  | uint32       | Bitmask of connected slots (bit i ↔ player i)  |
| 13  | `tiles_packed`    | bytes (19)   | High nibble = biome, low nibble = number       |
| 14  | `vp` (packed)     | repeated u32 | Self-reported VP per player                    |
| 15  | `ready` (packed)  | repeated u32 | 1 if player has tapped Ready in lobby          |

## `PlayerInput` (mobile → hub, write)

| Tag | Name           | Type   | Notes                                                |
|-----|----------------|--------|------------------------------------------------------|
| 1   | `proto_version`| uint32 | Phone may set 6; hub re-stamps before forwarding     |
| 2   | `player_id`    | uint32 | Phone may set 0; hub re-stamps to the bound slot     |
| 3   | `action`       | enum   | `ACTION_*` (see below)                               |
| 4   | `client_id`    | string | Optional echo; hub re-stamps from its slot table     |
| 10  | `vp`           | uint32 | For `REPORT`; for `READY` may be 0/1 (toggle if 0)   |
| 11  | `res_lumber`   | uint32 | for `REPORT` only                                    |
| 12  | `res_wool`     | uint32 |                                                      |
| 13  | `res_grain`    | uint32 |                                                      |
| 14  | `res_brick`    | uint32 |                                                      |
| 15  | `res_ore`      | uint32 |                                                      |

The hub authoritatively rewrites `proto_version`, `player_id`, and
`client_id` from its slot table before forwarding to the Mega — clients
cannot impersonate another player.

### `PlayerAction` values

| Value | Name             | Notes                                              |
|-------|------------------|----------------------------------------------------|
| 0     | `NONE`           | Sentinel                                           |
| 1     | `READY`          | Toggle "I'm ready" in lobby (`vp=0` ⇒ toggle)      |
| 2     | `START_GAME`     | Lobby → board setup                                |
| 3     | `NEXT_NUMBER`    | Step number-reveal display                         |
| 4     | `PLACE_DONE`     | End my initial placement turn                      |
| 5     | `ROLL_DICE`      | Current player rolls                               |
| 6     | `END_TURN`       | Current player ends turn                           |
| 7     | `SKIP_ROBBER`    | Robber phase: leave robber where it is             |
| 8     | `REPORT`         | Send self-reported VP + resource counts            |

---

## Hub-internal: `PlayerPresence` (hub → Mega, UART only)

The hub also pushes a `PlayerPresence` frame to the Mega whenever the
connection mask changes (and as a 1 Hz keep-alive):

| Tag | Name             | Type             | Notes                          |
|-----|------------------|------------------|--------------------------------|
| 1   | `proto_version`  | uint32           | 6                              |
| 2   | `connected_mask` | uint32           | Bit i = slot i is connected    |
| 3   | `client_ids`     | repeated string  | Up to 4 entries, may be empty  |

This message is **not exposed over BLE** — phones learn about each other only
via `BoardState.connected_mask` and `BoardState.ready`.
