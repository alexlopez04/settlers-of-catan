# New System Plan: Automated Catan Game Engine

This document outlines the high-level changes needed to implement full automated game logic.
The core architecture shift is: **the board firmware becomes the single source of truth for all game state** — resources, development cards, victory points, and trade. The mobile app moves from a self-reporting client to a pure display/input surface.

---

## Current Architecture (what exists today)

- **Board (Arduino Mega)**: tracks phases, dice, piece placement (settlements/cities/roads), robber tile, and re-broadcasts whatever VP/resources players self-report.
- **Hub (ESP32-C6)**: bridges BLE ↔ UART. No game logic.
- **Mobile app**: players manually adjust their own resource counts and VP. Sends `ACTION_REPORT` to sync them to the board.

The limitation: nothing is validated or computed server-side. Resources are honor-system only.

---

## 1 — Auto Resource Distribution

### How it works in Catan
After each dice roll (not a 7), every tile whose number token matches the roll produces one resource of that tile's biome for each adjacent settlement (1 card) or city (2 cards) that isn't covered by the robber.

### What the board already knows
- Tile biomes and number tokens (`tiles_packed`)
- Which vertices have settlements/cities and who owns them (`vertex_owners`)
- The robber tile
- The dice roll result (`die1 + die2`)

### Changes needed

**Firmware:**
- Add a `distributeResources(uint8_t roll)` function in `game_state.cpp`. When `ACTION_ROLL_DICE` is processed and the total is not 7, the board iterates all 19 tiles, finds tiles whose token == roll and are not the robber tile, then for each adjacent vertex awards 1 resource (settlement) or 2 resources (city) to the owning player.
- Biome → resource mapping: Forest→Lumber, Pasture→Wool, Field→Grain, Hill→Brick, Mountain→Ore.
- Resource counts are stored server-side in `reported_res_` (already exists). The board is now the writer; `ACTION_REPORT` from the client is no longer trusted for raw counts (or removed entirely).
- During initial placement (round 2 of the snake draft), award resources for the second settlement immediately when `PLACE_DONE` is received.

**Protocol:**
- Add a `resources_just_distributed` flag (bool) to `BoardState` — set for one broadcast cycle after a roll distributes resources so the app knows to show a confirmation dialog.
- Alternatively, a `last_distribution` repeated field listing what each player received this roll, so the dialog can be descriptive.

**App:**
- Remove the manual resource steppers.
- When `resources_just_distributed` is set, show a modal to each player listing what they received this roll (auto-dismissed after a few seconds, or with a tap-to-confirm). The counts come from BoardState — no manual input.
- Display resource counts as read-only, sourced from `BoardState.res_*[playerId]`.

### Edge cases / Catan rules to get right
- Robber tile: zero resources from that tile regardless of settlements on it.
- Desert tile: never produces anything (no number token).
- Multiple tiles with the same number that are both in production: player collects from all of them.
- Bank depletion: if the bank doesn't have enough of a resource, nobody gets any of that type for that roll (standard rule). Track a bank supply (19 of each resource). If the requested amount exceeds supply, produce nothing for that resource this roll.

---

## 2 — Trade System

Trades come in two flavours: bank/port trades (1 player) and player-to-player trades (2 players).

### Bank & Port Trades
A player can trade with the bank at:
- **4:1** always (any 4 of the same resource for 1 of any other)
- **3:1** if they have a settlement/city on a generic port vertex
- **2:1** if they have a settlement/city on a specific-resource port vertex

The app already has `PORTS` and `vertex_owners` — it can compute which port rates the player has access to client-side.

**Changes needed:**
- Add a new `PlayerAction`: `ACTION_BANK_TRADE`
- Add fields to `PlayerInput`: `trade_give_res`, `trade_give_count`, `trade_want_res`, `trade_want_count`
- Board validates the trade (player has the resources, port rate is correct), deducts from player, adds the wanted resource, broadcasts updated `BoardState`.
- App shows a trade screen: pick "give" resource (shows available port rates), pick "want" resource, confirm. Only valid trades are shown/enabled.

### Player-to-Player Trades
During your turn (after rolling), you may offer a trade to one or more opponents.

**Changes needed — Protocol:**
- New `PlayerAction` values: `ACTION_TRADE_OFFER`, `ACTION_TRADE_ACCEPT`, `ACTION_TRADE_DECLINE`
- New `BoardState` fields for a pending trade: `trade_from_player`, `trade_to_player` (0xFF = open offer to anyone), `trade_offer_res_*` (5 resource counts being offered), `trade_want_res_*` (5 resource counts being requested)
- `PlayerInput` for an offer includes the offering resources and wanted resources.

**Flow:**
1. Current player opens the Trade screen, selects what to offer and what they want.
2. They either target a specific player or broadcast to all.
3. Board sets the pending trade in `BoardState` and broadcasts.
4. Target player(s) see a trade dialog with the offer. The "Accept" button is disabled if they don't have the requested resources.
5. If accepted: board swaps the resources, clears the pending trade, broadcasts.
6. If declined or current player cancels: board clears the pending trade.
7. Only one pending trade at a time. Only the current player can initiate trades.

### Edge cases
- A player can counter-propose by declining and sending a new offer (initiated by their device) — this is optional complexity; can be deferred.
- You can make multiple bank trades per turn but only one player trade per turn (standard rule — actually Catan allows multiple player trades; consider allowing multiple offers until player ends turn).

---

## 3 — Purchasing (Store)

### Costs
| Item | Cost |
|------|------|
| Road | 1 Brick + 1 Lumber |
| Settlement | 1 Brick + 1 Lumber + 1 Wool + 1 Grain |
| City | 2 Grain + 3 Ore |
| Development Card | 1 Ore + 1 Wool + 1 Grain |

### Changes needed

**Protocol:**
- New `PlayerAction` values: `ACTION_BUY_ROAD`, `ACTION_BUY_SETTLEMENT`, `ACTION_BUY_CITY`, `ACTION_BUY_DEV_CARD`
- Purchase intent is separate from physical placement. Flow: player taps "Buy Road" in the store → board validates resources → deducts cost → sets a flag `pending_road_count` (or similar) in `BoardState` → player places the road on the physical board → firmware detects the placement → confirms.

**Firmware:**
- When `ACTION_BUY_*` is received: validate player has resources and it's their turn and phase is PLAYING. Deduct resources. Grant the "pending purchase" token.
- For roads and settlements, placement is already detected by firmware via sensor grid. Placement is only accepted if the player has a pending purchase of the right type. Return `REJECT_REASON` 10 (new: `NOT_PURCHASED`) if placement attempted without purchase.
- For cities: `ACTION_BUY_CITY` deducts resources. The firmware detects a settlement being removed and then placed on the same vertex as an existing settlement — this upgrades it. Alternatively, the board can simply accept the next `upgradeToCity` event for that player once a city purchase is pending.
- For development cards: dealt from the deck immediately upon purchase (see Section 5). New card is flagged as "bought this turn" and cannot be played until next turn.

**App:**
- Add a "Store" tab/sheet in the game screen. Shows the four purchasable items with their costs. Items the player can't afford are grayed out. Tapping an item sends the `ACTION_BUY_*`.
- After a successful purchase, show feedback (haptic + toast). For roads/settlements, the board visual shows a pending placement indicator.

### Edge cases
- Maximum pieces: 15 roads, 5 settlements, 4 cities per player. Board should reject purchases if the player is at the piece limit.
- You cannot build a settlement on a vertex where you already have one (enforced by existing placement validation).
- You must have a road connecting to a new settlement (already enforced by firmware).

---

## 4 — Robber

### Triggers
- Dice roll of 7
- Playing a Knight development card (see Section 5)

### Full Robber Flow (Catan rules)

1. **Discard phase** (if dice = 7 only, not knight): any player with more than 7 resource cards must immediately discard half (rounded down). This happens before the active player moves the robber.
2. **Robber placement**: the active player picks a new tile to place the robber on (cannot stay on the same tile).
3. **Steal**: the active player may steal one random resource from any opponent who has a settlement/city adjacent to the new robber tile. If multiple opponents qualify, the player chooses who to steal from.

### Changes needed

**Protocol:**
- New `PlayerAction` values:
  - `ACTION_PLACE_ROBBER` — payload includes `robber_tile_id` (new `PlayerInput` field)
  - `ACTION_STEAL_FROM` — payload includes `steal_target_player_id`
  - `ACTION_DISCARD` — payload includes resource counts being discarded (re-uses res fields)
- New `GamePhase` (or sub-state): `PHASE_DISCARD` between rolling 7 and `PHASE_ROBBER`. Alternatively, add a `discard_required_mask` bitmask to `BoardState` — each player with >7 cards must send `ACTION_DISCARD` before the robber can be placed. Board tracks which players have discarded.
- `BoardState` may need: `steal_eligible_mask` (bitmask of players adjacent to newly placed robber tile who have resources) so the app can show the correct selection.

**Firmware:**
- On roll of 7: compute `discard_required_mask` (players with >7 cards). If mask is non-zero, enter `PHASE_DISCARD`. When all required discards received, enter `PHASE_ROBBER`.
- On `ACTION_PLACE_ROBBER`: validate new tile != current robber tile. Move robber. Compute `steal_eligible_mask` and broadcast.
- On `ACTION_STEAL_FROM`: validate target is in `steal_eligible_mask` and target has at least 1 resource. Pick a random resource from the target and transfer to the active player. Broadcast. Return to `PHASE_PLAYING`, advance past roll step (has_rolled = true).
- If no eligible players adjacent to new robber tile, skip steal step automatically.

**App:**
- During `PHASE_DISCARD`: if the local player is in `discard_required_mask`, show a mandatory discard modal (cannot dismiss). Player selects resources to discard. Board validates the count.
- During `PHASE_ROBBER`: show an instruction to the active player to tap a tile on the board overview. The board overview becomes a tile-selection UI. Selected tile highlights; confirm button sends `ACTION_PLACE_ROBBER`.
- After placement, if `steal_eligible_mask > 0`, show a player selection dialog to the active player. Selected player sends `ACTION_STEAL_FROM`.
- For non-active players: board overview shows the robber tile dimmed. A "waiting for robber" overlay is shown.

### Edge cases
- Knight card: same flow starting at step 2 (no discard, since hand size wasn't caused by a 7). Can be played before rolling dice — in that case `has_rolled` is still false and the player rolls after the robber is resolved.
- Robber cannot be moved to the desert (or the tile it already occupies — standard rules say it must move).

---

## 5 — Development Cards

### Deck composition (25 cards)
| Type | Count |
|------|-------|
| Knight | 14 |
| Victory Point | 5 |
| Road Building | 2 |
| Year of Plenty | 2 |
| Monopoly | 2 |

### Storage
**Firmware:**
- Board owns a shuffled deck (array of 25 card type bytes, shuffled at `START_GAME`).
- Per-player card inventory: count of each type (Knights held, VPs held, Road Building held, etc.) + a `bought_this_turn` flag per card type (or a simple count of cards bought this turn — can't play any of them until next turn).
- New `BoardState` fields: `dev_cards_*` repeated counts per player per type (Knights, VP, Road Building, Year of Plenty, Monopoly). VP cards are secret from other players — broadcast only the owner's own counts; other players see totals without type breakdown. (Or simplify: broadcast all counts since the board does validation anyway.)
- Track `knights_played[4]` for Largest Army. Track `largest_army_player` and `longest_road_player` for VP award.

### Playing cards
**Protocol — new `PlayerAction` values:**
- `ACTION_PLAY_KNIGHT` — triggers robber flow
- `ACTION_PLAY_ROAD_BUILDING` — grants 2 free road placements (sets `free_roads_remaining` = 2 in BoardState)
- `ACTION_PLAY_YEAR_OF_PLENTY` — payload: two resource types chosen by player
- `ACTION_PLAY_MONOPOLY` — payload: resource type to monopolize
- (VP cards are never "played" — they are counted automatically at end-of-game check)

**Firmware handling:**
- All play actions: validate it's the player's turn, they own the card, and it wasn't bought this turn.
- Knight: increment `knights_played[player]`, recalculate Largest Army holder, then trigger the robber flow. Knight can be played before OR after rolling (standard rule: before or after — actually standard Catan allows playing a knight before rolling; simplest to allow it at any point in the turn outside of placement).
- Road Building: set `free_roads_remaining[player] = 2`. The next two road placements for this player cost no resources.
- Year of Plenty: immediately add 2 resources of the chosen types from the bank. Validate bank supply.
- Monopoly: for each other player, take all of the specified resource type from them and give it to the active player.

**App:**
- Add a "Cards" panel (sheet or tab) showing the player's dev card inventory. Each card shows what it does. Cards bought this turn are greyed out.
- Tap a card to play it. Knight and Road Building trigger subsequent UI flows (see robber, placement). Year of Plenty shows a resource picker (select 2). Monopoly shows a single resource picker.

### Edge cases
- Only 1 development card may be played per turn (standard rule). Board tracks `card_played_this_turn` flag.
- Road Building: if only 1 road placement is possible (player at 14 roads, or no valid positions), the second free road is forfeited.
- VP cards are never revealed until the game ends (or when a player reaches 10 VP including hidden VPs) — board should not broadcast opponent VP card counts.

---

## 6 — Automatic Victory Point Tracking

### VP sources
| Source | Points |
|--------|--------|
| Settlement | 1 per settlement |
| City | 2 per city |
| Longest Road | 2 (player with ≥5 roads and the longest continuous road) |
| Largest Army | 2 (player with ≥3 knights played and the most) |
| VP Development Card | 1 per card |

### Changes needed

**Firmware:**
- Remove dependency on `ACTION_REPORT` for VP. Compute VP server-side every time any relevant state changes (placement, city upgrade, dev card purchase, longest road/army award).
- **Settlements/Cities**: count vertices per player from `vertex_owners` — trivial.
- **Largest Army**: tracked as `knights_played[4]`. The current `largest_army_player` holds the title (0xFF if no one has ≥3 yet). When a knight is played, check if the new count exceeds the current holder. Ties: existing holder keeps it (standard rule).
- **Longest Road**: requires a DFS/backtracking path algorithm over the edge graph. For each player, find the longest simple path through their connected roads, respecting that an opponent's settlement on a vertex breaks the road. The board already has `EDGE_VERTICES` topology. Minimum 5 to claim. Same tie-breaking rule as army.
- Write `computeVp(player)` that sums all the above and stores into `reported_vp_` (or a new `computed_vp_` array). Winner check runs after every VP update.
- Check for 10 VP (or include hidden VP dev cards — board must include them in the private total for the owning player but not broadcast them in `vp[]` until the game ends or the player wins).

**Protocol:**
- `BoardState.vp[]` now contains board-computed values. Remove `ACTION_REPORT` VP field (or ignore it).

**App:**
- Remove the VP stepper entirely.
- Display VP as a read-only badge on each player card, sourced from `BoardState.vp[playerId]`.
- Show Longest Road and Largest Army holders with a visual badge on the board/scoreboard.

### Edge cases
- Longest Road can be broken: if an opponent places a settlement on a vertex in the middle of your road chain, your road is split and might drop below 5. The title is lost immediately and is unawarded until someone builds a new longest (≥5). If a non-holder's road is now the longest (≥5) and longer than the current holder's, the title transfers. Standard rule: the title stays with the current holder in a tie.
- A player can reach 10 VP mid-turn (e.g., place a settlement that gives them 10). The game ends immediately.
- VP dev cards are secret: the board knows the real total but broadcasts a "public VP" (excluding hidden cards) for the scoreboard, while the winning check uses the true total.

---

## Summary of Protocol Changes

### New `PlayerAction` values
```
ACTION_BUY_ROAD           = 10
ACTION_BUY_SETTLEMENT     = 11
ACTION_BUY_CITY           = 12
ACTION_BUY_DEV_CARD       = 13
ACTION_PLACE_ROBBER       = 14   // payload: robber_tile_id
ACTION_STEAL_FROM         = 15   // payload: steal_target_player_id
ACTION_DISCARD            = 16   // payload: res_* fields
ACTION_BANK_TRADE         = 17   // payload: trade fields
ACTION_TRADE_OFFER        = 18   // payload: trade fields + target player
ACTION_TRADE_ACCEPT       = 19
ACTION_TRADE_DECLINE      = 20
ACTION_PLAY_KNIGHT        = 21
ACTION_PLAY_ROAD_BUILDING = 22
ACTION_PLAY_YEAR_OF_PLENTY = 23  // payload: want_res_1, want_res_2
ACTION_PLAY_MONOPOLY      = 24   // payload: monopoly_res
```

### New `PlayerInput` fields
- `robber_tile_id` — for ACTION_PLACE_ROBBER
- `steal_target` — for ACTION_STEAL_FROM
- `trade_give_res`, `trade_give_count`, `trade_want_res`, `trade_want_count` — for trades
- `target_player` — for TRADE_OFFER (0xFF = offer to all)
- `card_res_1`, `card_res_2` — for Year of Plenty choices
- `monopoly_res` — for Monopoly

### New `BoardState` fields
- `discard_required_mask` — bitmask of players who must discard before robber moves
- `steal_eligible_mask` — bitmask of players who can be stolen from after robber placement
- `pending_robber_placement` — bool, active player must place robber
- `free_roads_remaining` per player — from Road Building card
- `card_played_this_turn` — bool, a dev card was already played this turn
- Dev card inventory per player per type (5 types × 4 players)
- `knights_played` per player
- `largest_army_player`, `longest_road_player`
- Pending trade fields (see Section 2)
- `resources_just_distributed` — triggers distribution confirmation dialog
- `bank_supply` per resource type (or omit if bank depletion rule is not needed initially)

---

## Implementation Order (suggested)

1. **Proto + firmware resource authority**: remove self-reporting, board computes and owns all resource counts. This is the prerequisite for everything else.
2. **Auto resource distribution** (Feature 1): smallest delta once the board owns resources.
3. **Purchasing / Store** (Feature 3): enables the rest of the game to make sense physically.
4. **Robber** (Feature 4): builds on the discard mechanism which needs resources.
5. **Development Cards** (Feature 5): builds on robber (Knight) and requires the deck.
6. **Trade** (Feature 2): can be added incrementally (bank-only first, then player-to-player).
7. **Auto VP** (Feature 6): payoff feature — comes after all the underlying data is correct.
