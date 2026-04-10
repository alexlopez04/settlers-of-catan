# Board Configuration Guide

This document explains the board's data model and how to configure the hardware
mapping (sensors, LEDs) for your physical Settlers of Catan board.

---

## Architecture Overview

The firmware separates **topology** (the mathematical board graph) from
**hardware mapping** (which pins and LEDs connect to which board elements).

```
┌───────────────────────────────────────────────────────────┐
│  board_types.h          Shared enums & coordinate types   │
├───────────────────────────────────────────────────────────┤
│  board_topology.h/cpp   Static graph: tiles, vertices,    │
│                         edges, ports, adjacency tables    │
├───────────────────────────────────────────────────────────┤
│  pin_map.h/cpp          Sensor → GPIO / I2C expander pin  │
├───────────────────────────────────────────────────────────┤
│  led_map.h/cpp          Board element → LED strip index   │
├───────────────────────────────────────────────────────────┤
│  config.h               Hardware constants (pin numbers,  │
│                         expander addresses, counts)       │
└───────────────────────────────────────────────────────────┘
```

### What you edit when wiring your board:

| Task | File |
|---|---|
| Change expander addresses or count | `config.h` |
| Change LED strip data pin or total count | `config.h` |
| Map sensors to tiles / vertices / edges | `pin_map.cpp` |
| Map LED strip addresses to tiles / ports | `led_map.cpp` |
| Change port type assignments | `board_topology.cpp` → `PORT_TOPO` |

You should **never** need to edit `board_topology.h` or the topology arrays
unless you're supporting a non-standard board layout (e.g., 5–6 player
expansion). Those tables are auto-generated.

---

## Terminology

The board is modelled as a **planar graph** over a hex grid:

| Game Concept | Graph Term | Count | ID Range |
|---|---|---|---|
| Resource hexagon | **Tile** (face) | 19 | T00–T18 |
| Intersection (settlement/city site) | **Vertex** (node) | 54 | V00–V53 |
| Road site | **Edge** | 72 | E00–E71 |
| Harbour | **Port** | 9 | P0–P8 |

### Axial Hex Coordinates

Each tile has an axial coordinate `(q, r)` using a **pointy-top** orientation.
The center tile is at `(0, 0)`:

```
              T15(0,-2)  T16(1,-2)  T17(2,-2)
          T14(-1,-1) T05(0,-1) T06(1,-1) T18(2,-1)
      T13(-2,0) T04(-1,0) T00(0,0) T01(1,0) T07(2,0)
          T12(-2,1) T03(-1,1) T02(0,1) T08(1,1)
              T11(-2,2) T10(-1,2) T09(0,2)
```

### Corner & Edge Ordering

Each tile's 6 vertices and 6 edges are stored **clockwise from North**:

```
          V0 (N)
         / \
    E5 /     \ E0
      /       \
  V5 (NW)   V1 (NE)
    |         |
 E4 |  tile   | E1
    |         |
  V4 (SW)   V2 (SE)
      \       /
    E3 \     / E2
         \ /
          V3 (S)
```

---

## Adjacency Relationships

The topology tables encode all adjacency data:

| Relationship | Stored In | Notes |
|---|---|---|
| Tile → 6 vertices | `TILE_TOPO[t].vertices[6]` | CW from N |
| Tile → 6 edges | `TILE_TOPO[t].edges[6]` | CW from N |
| Tile → up to 6 neighbors | `TILE_TOPO[t].neighbors[6]` | `NONE` for border |
| Vertex → up to 3 tiles | `VERTEX_TOPO[v].tiles[3]` | `NONE` if coastal |
| Vertex → up to 3 edges | `VERTEX_TOPO[v].edges[3]` | |
| Edge → 2 vertices | `EDGE_TOPO[e].vertices[2]` | |
| Edge → up to 2 tiles | `EDGE_TOPO[e].tiles[2]` | `NONE` if coastal |
| Port → 2 vertices | `PORT_TOPO[p].vertices[2]` | Both coastal |

Helper functions `tilesForVertex()` and `tilesForEdge()` provide convenient
lookup from vertex/edge ID to adjacent tile IDs.

---

## Sensor Pin Mapping (`pin_map.cpp`)

Every tile, vertex, and edge has a row in its respective sensor table. Each
row is a `SensorPin` with three fields:

```cpp
struct SensorPin {
    SensorSource source;       // DIRECT_GPIO or I2C_EXPANDER
    uint8_t      pin;          // GPIO pin number, or bit index on expander
    uint8_t      expander_idx; // Index into EXPANDER_ADDRS[]
};
```

### Convenience Macros

Use these in `pin_map.cpp` for readable tables:

| Macro | Meaning |
|---|---|
| `GPIO(pin)` | Direct Arduino digital pin (INPUT_PULLUP) |
| `EXPANDER(bit, idx)` | I2C expander #`idx`, bit position `bit` |
| `NO_SENSOR` | No physical sensor connected |

### Example

```cpp
const SensorPin VERTEX_SENSOR_MAP[VERTEX_COUNT] = {
    /* V00 */ EXPANDER(0, 0),   // Expander 0x20, bit 0
    /* V01 */ EXPANDER(1, 0),   // Expander 0x20, bit 1
    /* V02 */ GPIO(22),         // Arduino Mega pin 22
    /* V03 */ NO_SENSOR,        // Not wired yet
    // ...
};
```

### I2C Expander Addresses

Configured in `config.h`:

```cpp
static constexpr uint8_t EXPANDER_COUNT = 8;
static constexpr uint8_t EXPANDER_ADDRS[EXPANDER_COUNT] = {
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27
};
```

| Index | Address | Default Use |
|---|---|---|
| 0 | 0x20 | Vertex sensors V00–V07 |
| 1 | 0x21 | Vertex sensors V08–V15 |
| 2 | 0x22 | Tile sensors T00–T07 |
| 3 | 0x23 | Tile sensors T08–T15 |
| 4 | 0x24 | Tile sensors T16–T18 (+ spare) |
| 5 | 0x25 | Edge sensors E00–E07 |
| 6 | 0x26 | Edge sensors E08–E15 |
| 7 | 0x27 | Edge sensors E16–E23 |

Adjust addresses to match the jumper settings on your PCF8574 or MCP23017
modules. If using MCP23017 (16 pins per expander), set `PINS_PER_EXPANDER = 16`
in `config.h`.

---

## LED Strip Mapping (`led_map.cpp`)

Each tile and port has a `TileLedMap` or `PortLedMap` struct specifying which
LED strip indices belong to it:

```cpp
struct TileLedMap {
    uint16_t indices[MAX_LEDS_PER_TILE];  // Strip positions
    uint8_t  count;                       // How many LEDs (0 = none)
};
```

### Default Layout

| Strip Range | Element | Count |
|---|---|---|
| 0–37 | Tiles (2 LEDs each × 19 tiles) | 38 |
| 38–46 | Ports (1 LED each × 9 ports) | 9 |
| 47–56 | Spare | 10 |

### Changing LED Count Per Tile

To use 3 LEDs per tile instead of 2:

1. In `config.h`, set `MAX_LEDS_PER_TILE = 4` (or higher).
2. In `led_map.cpp`, update the entries:
   ```cpp
   /* T00 */ { { 0, 1, 2 }, 3 },
   ```
3. Adjust `TOTAL_LED_COUNT` in `config.h` to fit the total.

### Changing LED Count Per Port

Same approach — update `MAX_LEDS_PER_PORT` in `config.h` and add indices:

```cpp
/* P0 */ { { 38, 39 }, 2 },  // Two LEDs for this port
```

---

## Regenerating Topology

If you modify the hex layout (e.g., for a 5–6 player expansion), regenerate
the topology tables:

```bash
python3 tools/generate_topology.py
```

This prints C++ table definitions for `TILE_TOPO`, `VERTEX_TOPO`, `EDGE_TOPO`,
and `PORT_TOPO`. Copy the output into `board_topology.cpp`, replacing the
existing tables. Update the geometry constants in `config.h` to match.

---

## File Reference

| File | Purpose | Edit When... |
|---|---|---|
| `config.h` | Hardware constants | Changing pin numbers, expander count, LED total |
| `board_types.h` | Enums (Biome, PortType, SensorSource) | Adding new resource types |
| `board_topology.h` | Topology struct definitions | Never (unless expanding board) |
| `board_topology.cpp` | Topology data + randomization | Changing port types, modifying biome distribution |
| `pin_map.h` | SensorPin struct + macros | Never |
| `pin_map.cpp` | Sensor wiring tables | **When wiring sensors** |
| `led_map.h` | LedMap struct definitions | Never |
| `led_map.cpp` | LED strip address tables | **When wiring LEDs** |
| `sensor_manager.h/cpp` | Sensor reading logic | Changing sensor behavior |
| `led_manager.h/cpp` | LED animation logic | Adding new animations |
| `game_state.h/cpp` | Game rules and state | Changing gameplay logic |
| `main.cpp` | Phase handlers | Changing UI flow |
| `tools/generate_topology.py` | Topology generator | Supporting custom board sizes |

---

## Quick-Start Checklist

1. Wire your LED strip and note which strip index goes where.
2. Edit `led_map.cpp` — set the correct strip indices for each tile and port.
3. Wire your hall-effect sensors (direct GPIO and/or I2C expanders).
4. Set expander addresses in `config.h`.
5. Edit `pin_map.cpp` — set `GPIO(pin)`, `EXPANDER(bit, idx)`, or `NO_SENSOR` for each element.
6. Build: `cd firmware && pio run`
7. Upload: `pio run -t upload`
8. Monitor: `pio device monitor`
