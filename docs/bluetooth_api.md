# Bluetooth API

The ESP32-C6 firmware advertises a single GATT service. Up to 4 phones
connect simultaneously, each assigned a player slot (0..3) by the board.

- Device name: `Catan-Board`
- Negotiated MTU: 512 (notify payload <= 509 bytes)
- Wire schema version: 8 (see [proto/catan.proto](../proto/catan.proto))
- Encoding on BLE: bare nanopb bytes (no framing envelope; ATT carries length)

Player slot is derived from the BLE device name the phone presents:
`Catan-P1`..`Catan-P4` map to player IDs 0..3.

---

## Service `CA7A0001-CA7A-4C4E-8000-00805F9B34FB`

| Char UUID  | Name     | Properties   | Format                         |
|------------|----------|--------------|--------------------------------|
| `CA7A0002` | State    | read, notify | `BoardState` (nanopb)          |
| `CA7A0003` | Input    | write        | `PlayerInput` (nanopb)         |
| `CA7A0004` | Identity | write        | UTF-8 client ID (max 39 bytes) |
| `CA7A0005` | Slot     | read, notify | 1 byte: assigned player_id     |

### Connection flow

1. Phone connects and requests MTU 512.
2. Phone subscribes to State and Slot characteristics.
3. Phone writes Identity (a stable UUID stored in AsyncStorage).
4. Board notifies Slot with the assigned player ID (0..3).
5. Board notifies State at ~5 Hz.
6. Phone writes Input when the player takes an action.

---

## `BoardState` fields

See [proto/catan.proto](../proto/catan.proto) for the canonical definition.
Key fields:

| Tag | Name                  | Notes                                                       |
|-----|-----------------------|-------------------------------------------------------------|
| 1   | `proto_version`       | Must equal 8; reject on mismatch                            |
| 2   | `phase`               | GamePhase enum (see below)                                  |
| 3   | `num_players`         | Highest occupied slot + 1                                   |
| 4   | `current_player`      | 0..3                                                        |
| 6   | `has_rolled`          | Current player has rolled this turn                         |
| 7-8 | `die1`, `die2`        | Last roll values                                            |
| 10  | `winner_id`           | 0..3, or 0xFF if no winner                                  |
| 11  | `robber_tile`         | 0..18, or 0xFF if unplaced                                  |
| 12  | `connected_mask`      | Bitmask — bit i set means player i has a phone linked       |
| 13  | `tiles_packed`        | 19 bytes — high nibble biome, low nibble number token       |
| 14  | `vp`                  | Public VP per player (excludes hidden VP dev cards)         |
| 16  | `vertex_owners`       | 27 bytes, 4-bit nibbles (0-3 settlement, 4-7 city, 0xF empty) |
| 17  | `edge_owners`         | 36 bytes, 4-bit nibbles (0-3 road, 0xF empty)               |
| 20-24 | `res_lumber..ore`   | Resource counts per player (4 entries each)                 |
| 30  | `dev_cards`           | 20 bytes — 5 card types x 4 players                        |
| 32  | `largest_army_player` | 0..3 or 0xFF                                                |
| 33  | `longest_road_player` | 0..3 or 0xFF                                                |
| 37  | `discard_required_mask` | Players who must discard (roll of 7)                      |
| 39  | `steal_eligible_mask` | Players adjacent to new robber tile with resources          |
| 50-53 | `trade_from/to/offer/want` | Pending trade offer (0xFF = none/open)               |
| 61  | `last_distribution`   | Resources just distributed (set for one broadcast cycle)    |
| 63  | `has_saved_game`      | Board has a resumable saved game (lobby phase only)         |

### `GamePhase` values

| Value | Name                  |
|-------|-----------------------|
| 0     | `PHASE_LOBBY`         |
| 1     | `PHASE_BOARD_SETUP`   |
| 2     | `PHASE_NUMBER_REVEAL` |
| 3     | `PHASE_INITIAL_PLACEMENT` |
| 4     | `PHASE_PLAYING`       |
| 5     | `PHASE_ROBBER`        |
| 6     | `PHASE_DISCARD`       |
| 7     | `PHASE_GAME_OVER`     |

---

## `PlayerInput` fields

| Tag   | Name            | Notes                                               |
|-------|-----------------|-----------------------------------------------------|
| 1     | `proto_version` | Should match 8                                      |
| 2     | `player_id`     | Board re-stamps from the BLE slot; client value ignored |
| 3     | `action`        | `PlayerAction` enum (see below)                     |
| 4     | `client_id`     | Informational; board re-stamps authoritatively      |
| 11-15 | `res_lumber..ore` | Resource counts (DISCARD, BANK_TRADE give, TRADE_OFFER give) |
| 16-20 | `want_lumber..ore` | Wanted resource counts (BANK_TRADE, TRADE_OFFER)  |
| 21    | `robber_tile`   | Destination tile for ACTION_PLACE_ROBBER (0..18)    |
| 22    | `target_player` | Target for ACTION_STEAL_FROM or ACTION_TRADE_OFFER  |
| 23    | `monopoly_res`  | Resource index for ACTION_PLAY_MONOPOLY (0..4)      |
| 24-25 | `card_res_1/2` | Resource indices for ACTION_PLAY_YEAR_OF_PLENTY     |

### `PlayerAction` values

| Value | Name                      | Notes                                       |
|-------|---------------------------|---------------------------------------------|
| 0     | `ACTION_NONE`             | Sentinel                                    |
| 1     | `ACTION_READY`            | Toggle ready flag in lobby                  |
| 2     | `ACTION_START_GAME`       | Lobby -> board setup                        |
| 3     | `ACTION_NEXT_NUMBER`      | Advance number-reveal display               |
| 4     | `ACTION_PLACE_DONE`       | End initial placement turn                  |
| 5     | `ACTION_ROLL_DICE`        | Current player rolls                        |
| 6     | `ACTION_END_TURN`         | Current player ends turn                    |
| 7     | `ACTION_SKIP_ROBBER`      | Skip steal after robber move (no targets)   |
| 10    | `ACTION_BUY_ROAD`         | Purchase a road (deducts resources)         |
| 11    | `ACTION_BUY_SETTLEMENT`   | Purchase a settlement                       |
| 12    | `ACTION_BUY_CITY`         | Purchase a city upgrade                     |
| 13    | `ACTION_BUY_DEV_CARD`     | Draw a development card                     |
| 14    | `ACTION_PLACE_ROBBER`     | Move robber to `robber_tile`                |
| 15    | `ACTION_STEAL_FROM`       | Steal from `target_player`                  |
| 16    | `ACTION_DISCARD`          | Discard resources (roll of 7)               |
| 17    | `ACTION_BANK_TRADE`       | Trade with bank/port                        |
| 18    | `ACTION_TRADE_OFFER`      | Offer a player trade                        |
| 19    | `ACTION_TRADE_ACCEPT`     | Accept the pending trade offer              |
| 20    | `ACTION_TRADE_DECLINE`    | Decline the pending trade offer             |
| 21    | `ACTION_TRADE_CANCEL`     | Retract an open trade offer                 |
| 22    | `ACTION_PLAY_KNIGHT`      | Play Knight card (triggers robber flow)     |
| 23    | `ACTION_PLAY_ROAD_BUILDING` | Play Road Building card (2 free roads)    |
| 24    | `ACTION_PLAY_YEAR_OF_PLENTY` | Play Year of Plenty (`card_res_1/2`)     |
| 25    | `ACTION_PLAY_MONOPOLY`    | Play Monopoly (`monopoly_res`)              |
| 26    | `ACTION_SET_DIFFICULTY`   | Player 0 sets board difficulty in lobby     |
| 27    | `ACTION_RESUME_YES`       | Resume saved game (lobby phase)             |
| 28    | `ACTION_RESUME_NO`        | Discard saved game, start fresh             |

---

## Tiles packed format

`BoardState.tiles_packed` is 19 bytes, one per tile (T00..T18):

- High nibble: biome (0=Desert, 1=Forest, 2=Pasture, 3=Field, 4=Hill, 5=Mountain)
- Low nibble: number token (2..12; 0 = no token / desert)

See [board_layout.md](board_layout.md) for tile coordinates.

