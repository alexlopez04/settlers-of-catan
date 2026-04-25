// =============================================================================
// pin_map.cpp — Physical sensor wiring tables.
//
// *** WIRING GUIDE ***
// Each table has one row per board element.  Use the macros from pin_map.h:
//
//   GPIO(pin)             — sensor on Arduino digital pin (INPUT_PULLUP)
//   EXPANDER(bit, idx)    — sensor on I2C expander #idx, bit position
//   NO_SENSOR             — no sensor connected to this element
//
// Expander indices refer to EXPANDER_ADDRS[] in config.h:
//   idx 0 → 0x20, idx 1 → 0x21, ... idx 7 → 0x27
//
// Hall effect sensors are active-low (magnet present → pin reads LOW).
// =============================================================================

#include "pin_map.h"

// ── Tile Sensors (19 tiles — robber / piece-on-hex detection) ───────────────
// Row index = tile ID (T00–T18).  See board_topology.cpp for hex coordinates.
const SensorPin TILE_SENSOR_MAP[TILE_COUNT] = {
    /* T00 ( 0, 0) */ NO_SENSOR,   // Expander 0x22 bit 0
    /* T01 ( 1, 0) */ NO_SENSOR,   // Expander 0x22 bit 1
    /* T02 ( 0, 1) */ NO_SENSOR,   // Expander 0x22 bit 2
    /* T03 (-1, 1) */ NO_SENSOR,   // Expander 0x22 bit 3
    /* T04 (-1, 0) */ NO_SENSOR,   // Expander 0x22 bit 4
    /* T05 ( 0,-1) */ NO_SENSOR,   // Expander 0x22 bit 5
    /* T06 ( 1,-1) */ NO_SENSOR,   // Expander 0x22 bit 6
    /* T07 ( 2, 0) */ NO_SENSOR,   // Expander 0x22 bit 7
    /* T08 ( 1, 1) */ NO_SENSOR,   // Expander 0x23 bit 0
    /* T09 ( 0, 2) */ NO_SENSOR,   // Expander 0x23 bit 1
    /* T10 (-1, 2) */ NO_SENSOR,   // Expander 0x23 bit 2
    /* T11 (-2, 2) */ NO_SENSOR,   // Expander 0x23 bit 3
    /* T12 (-2, 1) */ NO_SENSOR,   // Expander 0x23 bit 4
    /* T13 (-2, 0) */ NO_SENSOR,   // Expander 0x23 bit 5
    /* T14 (-1,-1) */ NO_SENSOR,   // Expander 0x23 bit 6
    /* T15 ( 0,-2) */ NO_SENSOR,   // Expander 0x23 bit 7
    /* T16 ( 1,-2) */ NO_SENSOR,   // Expander 0x24 bit 0
    /* T17 ( 2,-2) */ NO_SENSOR,   // Expander 0x24 bit 1
    /* T18 ( 2,-1) */ NO_SENSOR,   // Expander 0x24 bit 2
};

// ── Vertex Sensors (54 vertices — settlement / city detection) ──────────────
// Row index = vertex ID (V00–V53).  Interior vertices (V00–V23) touch 3 tiles;
// coastal vertices (V24–V53) touch 1–2 tiles.
const SensorPin VERTEX_SENSOR_MAP[VERTEX_COUNT] = {
    /* V00 */ EXPANDER(5, 5),   // Expander 0x20 bit 0
    /* V01 */ EXPANDER(14, 5),   // Expander 0x20 bit 1
    /* V02 */ EXPANDER(15, 5),   // Expander 0x20 bit 2
    /* V03 */ EXPANDER(11, 2),   // Expander 0x20 bit 3
    /* V04 */ EXPANDER(13, 2),   // Expander 0x20 bit 4
    /* V05 */ EXPANDER(5, 0),   // Expander 0x20 bit 5
    /* V06 */ EXPANDER(15, 4),   // Expander 0x20 bit 6
    /* V07 */ EXPANDER(5, 4),   // Expander 0x20 bit 7
    /* V08 */ EXPANDER(9, 2),   // Expander 0x21 bit 0
    /* V09 */ EXPANDER(8, 2),   // Expander 0x21 bit 1
    /* V10 */ EXPANDER(5, 6),   // Expander 0x21 bit 2
    /* V11 */ EXPANDER(10, 5),   // Expander 0x21 bit 3
    /* V12 */ EXPANDER(6, 6),   // Expander 0x21 bit 4
    /* V13 */ EXPANDER(7, 6),   // Expander 0x21 bit 5
    /* V14 */ EXPANDER(2, 7),   // Expander 0x21 bit 6
    /* V15 */ EXPANDER(8, 6),   // Expander 0x21 bit 7
    /* V16 */ EXPANDER(6, 0),         // Arduino Mega pin 22
    /* V17 */ EXPANDER(14, 0),         // Arduino Mega pin 23
    /* V18 */ EXPANDER(11, 0),         // Arduino Mega pin 24
    /* V19 */ EXPANDER(0, 2),         // Arduino Mega pin 25
    /* V20 */ EXPANDER(11, 1),         // Arduino Mega pin 26
    /* V21 */ EXPANDER(14, 1),         // Arduino Mega pin 27
    /* V22 */ EXPANDER(3, 2),         // Arduino Mega pin 28
    /* V23 */ EXPANDER(4, 3),         // Arduino Mega pin 29
    /* V24 */ EXPANDER(3, 4),         // Arduino Mega pin 30
    /* V25 */ EXPANDER(6, 4),         // Arduino Mega pin 31
    /* V26 */ EXPANDER(0, 5),         // Arduino Mega pin 32
    /* V27 */ EXPANDER(5, 2),         // Arduino Mega pin 33
    /* V28 */ EXPANDER(11, 4),         // Arduino Mega pin 34
    /* V29 */ EXPANDER(9, 4),         // Arduino Mega pin 35
    /* V30 */ EXPANDER(0, 6),         // Arduino Mega pin 36
    /* V31 */ EXPANDER(13, 4),         // Arduino Mega pin 37
    /* V32 */ EXPANDER(16, 6),         // Arduino Mega pin 38
    /* V33 */ EXPANDER(13, 6),         // Arduino Mega pin 39
    /* V34 */ EXPANDER(3, 6),         // Arduino Mega pin 40
    /* V35 */ EXPANDER(10, 7),         // Arduino Mega pin 41
    /* V36 */ EXPANDER(7, 7),         // Arduino Mega pin 42
    /* V37 */ EXPANDER(1, 7),         // Arduino Mega pin 43
    /* V38 */ EXPANDER(3, 5),         // Arduino Mega pin 44
    /* V39 */ EXPANDER(5, 7),         // Arduino Mega pin 45
    /* V40 */ EXPANDER(0, 1),         // Arduino Mega pin 46
    /* V41 */ EXPANDER(8, 0),         // Arduino Mega pin 47
    /* V42 */ EXPANDER(12, 0),         // Arduino Mega pin 48
    /* V43 */ EXPANDER(2, 1),         // Arduino Mega pin 49
    /* V44 */ EXPANDER(7, 1),         // Arduino Mega pin 50
    /* V45 */ EXPANDER(6, 3),         // Arduino Mega pin 51
    /* V46 */ EXPANDER(5, 3),         // Arduino Mega pin 52
    /* V47 */ EXPANDER(12, 1),         // Arduino Mega pin 53
    /* V48 */ EXPANDER(7, 3),        // TODO: wire to expander or GPIO
    /* V49 */ EXPANDER(3, 3),        // TODO: wire to expander or GPIO
    /* V50 */ EXPANDER(8, 3),        // TODO: wire to expander or GPIO
    /* V51 */ EXPANDER(11, 3),        // TODO: wire to expander or GPIO
    /* V52 */ EXPANDER(2, 3),        // TODO: wire to expander or GPIO
    /* V53 */ EXPANDER(6, 2),        // TODO: wire to expander or GPIO
};

// ── Edge Sensors (72 edges — road detection) ────────────────────────────────
// Row index = edge ID (E00–E71).  Most edges will use I2C expanders.
// Fill in as you wire your board.  NO_SENSOR = not yet connected.
const SensorPin EDGE_SENSOR_MAP[EDGE_COUNT] = {
    /* E00 */ EXPANDER(6, 5),   // Expander 0x25 bit 0
    /* E01 */ EXPANDER(9, 5),   // Expander 0x25 bit 1
    /* E02 */ EXPANDER(12, 2),   // Expander 0x25 bit 2
    /* E03 */ EXPANDER(0, 0),   // Expander 0x25 bit 3
    /* E04 */ EXPANDER(4, 0),   // Expander 0x25 bit 4
    /* E05 */ EXPANDER(8, 7),   // Expander 0x25 bit 5
    /* E06 */ EXPANDER(1, 4),   // Expander 0x25 bit 6
    /* E07 */ EXPANDER(10, 2),   // Expander 0x25 bit 7
    /* E08 */ EXPANDER(7, 2),   // Expander 0x26 bit 0
    /* E09 */ EXPANDER(13, 5),   // Expander 0x26 bit 1
    /* E10 */ EXPANDER(7, 5),   // Expander 0x26 bit 2
    /* E11 */ EXPANDER(14, 4),   // Expander 0x26 bit 3
    /* E12 */ EXPANDER(8, 4),   // Expander 0x26 bit 4
    /* E13 */ EXPANDER(10, 4),   // Expander 0x26 bit 5
    /* E14 */ EXPANDER(9, 6),   // Expander 0x26 bit 6
    /* E15 */ EXPANDER(10, 6),   // Expander 0x26 bit 7
    /* E16 */ EXPANDER(4, 5),   // Expander 0x27 bit 0
    /* E17 */ EXPANDER(14, 7),   // Expander 0x27 bit 1
    /* E18 */ EXPANDER(13, 7),   // Expander 0x27 bit 2
    /* E19 */ EXPANDER(2, 0),   // Expander 0x27 bit 3
    /* E20 */ EXPANDER(3, 0),   // Expander 0x27 bit 4
    /* E21 */ EXPANDER(7, 0),   // Expander 0x27 bit 5
    /* E22 */ EXPANDER(3, 7),   // Expander 0x27 bit 6
    /* E23 */ EXPANDER(1, 0),   // Expander 0x27 bit 7
    /* E24 */ EXPANDER(13, 3),        // TODO: wire to expander or GPIO
    /* E25 */ EXPANDER(13, 1),
    /* E26 */ EXPANDER(15, 0),
    /* E27 */ EXPANDER(4, 2),
    /* E28 */ EXPANDER(12, 3),
    /* E29 */ EXPANDER(1, 3),
    /* E30 */ EXPANDER(7, 4),
    /* E31 */ EXPANDER(8, 5),
    /* E32 */ EXPANDER(1, 2),
    /* E33 */ EXPANDER(14, 2),
    /* E34 */ EXPANDER(11, 5),
    /* E35 */ EXPANDER(4, 4),
    /* E36 */ EXPANDER(0, 4),
    /* E37 */ EXPANDER(2, 4),
    /* E38 */ EXPANDER(2, 6),
    /* E39 */ EXPANDER(12, 4),
    /* E40 */ EXPANDER(14, 6),
    /* E41 */ EXPANDER(1, 6),
    /* E42 */ EXPANDER(11, 6),
    /* E43 */ EXPANDER(4, 6),
    /* E44 */ EXPANDER(12, 6),
    /* E45 */ EXPANDER(6, 7),
    /* E46 */ EXPANDER(12, 7),
    /* E47 */ EXPANDER(9, 7),
    /* E48 */ EXPANDER(15, 7),
    /* E49 */ EXPANDER(4, 7),
    /* E50 */ EXPANDER(11, 7),
    /* E51 */ EXPANDER(0, 7),
    /* E52 */ EXPANDER(13, 0),
    /* E53 */ EXPANDER(6, 1),
    /* E54 */ EXPANDER(9, 0),
    /* E55 */ EXPANDER(10, 0),
    /* E56 */ EXPANDER(5, 1),
    /* E57 */ EXPANDER(15, 1),
    /* E58 */ EXPANDER(3, 1),
    /* E59 */ EXPANDER(9, 1),
    /* E60 */ EXPANDER(10, 1),
    /* E61 */ EXPANDER(1, 1),
    /* E62 */ EXPANDER(4, 1),
    /* E63 */ EXPANDER(10, 3),
    /* E64 */ EXPANDER(0, 3),
    /* E65 */ EXPANDER(15, 3),
    /* E66 */ EXPANDER(2, 2),
    /* E67 */ EXPANDER(9, 3),
    /* E68 */ EXPANDER(8, 1),
    /* E69 */ EXPANDER(14, 3),
    /* E70 */ EXPANDER(15, 2),
    /* E71 */ EXPANDER(12, 5),
};
