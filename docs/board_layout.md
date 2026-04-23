# Board Layout Reference

This document describes the tile coordinate system, port numbering, and graph element IDs used by the firmware. All data lives in `firmware/board/src/board_topology.cpp` and `firmware/board/src/led_map.cpp`.

---

## Coordinate System

The board uses **pointy-top axial hex coordinates** `(q, r)`.

```
     q →
  r     0    +1   +2
  ↓
  -2   T15  T16  T17
  -1   T05  T06  T18
   0   T00  T01  T07
  +1   T03  T02  T08
  +2   T10  T09  ...
```

- **q** increases to the right.
- **r** increases down-right (so each row shifts half a hex to the right as r increases by 1).
- The center tile is `T00 (q=0, r=0)`.

Within each hex, vertices and edges are numbered **clockwise from North**:

```
  Vertices:         Edges:

       0 (N)             0 (NE)
      / \              /     \
  5 /   \ 1       5 /         \ 1 (E)
(NW)     (NE)   (NW)           
    |   |              |       |
  4 \   / 2       4 \         / 2 (SE)
  (SW) (SE)       (W)   3   (SE)
       3 (S)             (SW)
```

---

## Tile Map

19 tiles, IDs T00–T18. Rows are offset to form the standard 3-4-5-4-3 Catan hexagon:

```
                T15(0,-2)    T16(1,-2)    T17(2,-2)
           T14(-1,-1)  T05(0,-1)  T06(1,-1)  T18(2,-1)
      T13(-2, 0)  T04(-1, 0)  T00(0, 0)  T01(1, 0)  T07(2, 0)
           T12(-2, 1)  T03(-1, 1)  T02(0, 1)  T08(1, 1)
                T11(-2, 2)  T10(-1, 2)  T09(0, 2)
```

Full table:

| ID  | q  | r  | Row position        |
|-----|----|----|---------------------|
| T00 |  0 |  0 | Center              |
| T01 |  1 |  0 | Center-right        |
| T02 |  0 |  1 | Lower-center        |
| T03 | -1 |  1 | Lower-left-center   |
| T04 | -1 |  0 | Left-center         |
| T05 |  0 | -1 | Upper-center        |
| T06 |  1 | -1 | Upper-right-center  |
| T07 |  2 |  0 | Right corner        |
| T08 |  1 |  1 | Lower-right-center  |
| T09 |  0 |  2 | Bottom-right        |
| T10 | -1 |  2 | Bottom-center       |
| T11 | -2 |  2 | Bottom-left corner  |
| T12 | -2 |  1 | Lower-left          |
| T13 | -2 |  0 | Left corner         |
| T14 | -1 | -1 | Upper-left-center   |
| T15 |  0 | -2 | Top-center          |
| T16 |  1 | -2 | Top-right           |
| T17 |  2 | -2 | Top-right corner    |
| T18 |  2 | -1 | Right-upper         |

---

## Port Map

9 ports, IDs P0–P8. Each port sits on a gap between two coastal tiles and occupies two adjacent coastal vertices. Going **clockwise from the east side**:

```
                 P3 (Wool 2:1)
         P4(3:1)     N      P2(3:1)
        /      \           /      \
   P5(Grain)   NW        NE   P1(Lumber)
      |                           |
   P6(Brick)   SW        SE   P0 (3:1)
        \      /           \      /
         P7(3:1)     S      P8(Ore 2:1)
```

Full table (vertices are the two intersection points the port touches):

| ID | Type         | Between tiles   | Vertex A | Vertex B | Direction on board |
|----|--------------|-----------------|----------|----------|--------------------|
| P0 | 3:1          | T07 – T08       | V24      | V25      | East               |
| P1 | Lumber 2:1   | T18 – T07       | V27      | V53      | Northeast          |
| P2 | 3:1          | T16 – T17       | V48      | V52      | Northeast          |
| P3 | Wool 2:1     | T15 – T16       | V45      | V46      | North              |
| P4 | 3:1          | T14 – T15       | V43      | V44      | Northwest          |
| P5 | Grain 2:1    | T12 – T13       | V38      | V42      | West               |
| P6 | Brick 2:1    | T11 – T12       | V36      | V37      | Southwest          |
| P7 | 3:1          | T10 – T11       | V33      | V34      | South              |
| P8 | Ore 2:1      | T08 – T09       | V28      | V31      | Southeast          |

Port types are editable in `PORT_TOPO[]` inside `board_topology.cpp`; only the `type` field needs to change — the vertex pairs are fixed by geometry.

---

## Vertices (54 total)

Vertices are intersection points between hexes. Coastal vertices touch 1–2 tiles; interior vertices touch exactly 3. Vertex IDs are referenced by port definitions and settlement/city placement logic.

Key coastal vertices (those belonging to ports) shown above. Full list is in `VERTEX_TOPO[54]` in `board_topology.cpp`.

---

## Edges (72 total)

Edges are the sides of hexes, connecting exactly two vertices. Coastal edges (30 total) border only one tile; interior edges border two. Edge IDs are used for road placement. Full list is in `EDGE_TOPO[72]`.

---

## LED Strip Mapping

The single WS2812B strip is addressed sequentially. The default assignment:

- **Tiles** (2 LEDs each): strip indices 1–47, with a few gaps (index 0 unused; index 5 unused; indices 8, 17 unused between some tiles — see `TILE_LED_MAP` for exact assignments).
- **Ports** (1 LED each): strip indices 38–46.

> ⚠️ Note: The tile and port LED ranges overlap in the current `led_map.cpp` — port LEDs start at index 38, which collides with tile T09's LEDs (46, 47). This is a known issue; the strip index assignments need to be reconciled if both tile and port LEDs are driven simultaneously.

To relocate LEDs, edit the index arrays in `TILE_LED_MAP` and `PORT_LED_MAP` inside `led_map.cpp`. Increase `MAX_LEDS_PER_TILE` / `MAX_LEDS_PER_PORT` in `config.h` before adding more LEDs per element.
