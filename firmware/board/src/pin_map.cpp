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
    /* T00 ( 0, 0) */ EXPANDER(0, 2),   // Expander 0x22 bit 0
    /* T01 ( 1, 0) */ EXPANDER(1, 2),   // Expander 0x22 bit 1
    /* T02 ( 0, 1) */ EXPANDER(2, 2),   // Expander 0x22 bit 2
    /* T03 (-1, 1) */ EXPANDER(3, 2),   // Expander 0x22 bit 3
    /* T04 (-1, 0) */ EXPANDER(4, 2),   // Expander 0x22 bit 4
    /* T05 ( 0,-1) */ EXPANDER(5, 2),   // Expander 0x22 bit 5
    /* T06 ( 1,-1) */ EXPANDER(6, 2),   // Expander 0x22 bit 6
    /* T07 ( 2, 0) */ EXPANDER(7, 2),   // Expander 0x22 bit 7
    /* T08 ( 1, 1) */ EXPANDER(0, 3),   // Expander 0x23 bit 0
    /* T09 ( 0, 2) */ EXPANDER(1, 3),   // Expander 0x23 bit 1
    /* T10 (-1, 2) */ EXPANDER(2, 3),   // Expander 0x23 bit 2
    /* T11 (-2, 2) */ EXPANDER(3, 3),   // Expander 0x23 bit 3
    /* T12 (-2, 1) */ EXPANDER(4, 3),   // Expander 0x23 bit 4
    /* T13 (-2, 0) */ EXPANDER(5, 3),   // Expander 0x23 bit 5
    /* T14 (-1,-1) */ EXPANDER(6, 3),   // Expander 0x23 bit 6
    /* T15 ( 0,-2) */ EXPANDER(7, 3),   // Expander 0x23 bit 7
    /* T16 ( 1,-2) */ EXPANDER(0, 4),   // Expander 0x24 bit 0
    /* T17 ( 2,-2) */ EXPANDER(1, 4),   // Expander 0x24 bit 1
    /* T18 ( 2,-1) */ EXPANDER(2, 4),   // Expander 0x24 bit 2
};

// ── Vertex Sensors (54 vertices — settlement / city detection) ──────────────
// Row index = vertex ID (V00–V53).  Interior vertices (V00–V23) touch 3 tiles;
// coastal vertices (V24–V53) touch 1–2 tiles.
const SensorPin VERTEX_SENSOR_MAP[VERTEX_COUNT] = {
    /* V00 */ EXPANDER(0, 0),   // Expander 0x20 bit 0
    /* V01 */ EXPANDER(1, 0),   // Expander 0x20 bit 1
    /* V02 */ EXPANDER(2, 0),   // Expander 0x20 bit 2
    /* V03 */ EXPANDER(3, 0),   // Expander 0x20 bit 3
    /* V04 */ EXPANDER(4, 0),   // Expander 0x20 bit 4
    /* V05 */ EXPANDER(5, 0),   // Expander 0x20 bit 5
    /* V06 */ EXPANDER(6, 0),   // Expander 0x20 bit 6
    /* V07 */ EXPANDER(7, 0),   // Expander 0x20 bit 7
    /* V08 */ EXPANDER(0, 1),   // Expander 0x21 bit 0
    /* V09 */ EXPANDER(1, 1),   // Expander 0x21 bit 1
    /* V10 */ EXPANDER(2, 1),   // Expander 0x21 bit 2
    /* V11 */ EXPANDER(3, 1),   // Expander 0x21 bit 3
    /* V12 */ EXPANDER(4, 1),   // Expander 0x21 bit 4
    /* V13 */ EXPANDER(5, 1),   // Expander 0x21 bit 5
    /* V14 */ EXPANDER(6, 1),   // Expander 0x21 bit 6
    /* V15 */ EXPANDER(7, 1),   // Expander 0x21 bit 7
    /* V16 */ GPIO(22),         // Arduino Mega pin 22
    /* V17 */ GPIO(23),         // Arduino Mega pin 23
    /* V18 */ GPIO(24),         // Arduino Mega pin 24
    /* V19 */ GPIO(25),         // Arduino Mega pin 25
    /* V20 */ GPIO(26),         // Arduino Mega pin 26
    /* V21 */ GPIO(27),         // Arduino Mega pin 27
    /* V22 */ GPIO(28),         // Arduino Mega pin 28
    /* V23 */ GPIO(29),         // Arduino Mega pin 29
    /* V24 */ GPIO(30),         // Arduino Mega pin 30
    /* V25 */ GPIO(31),         // Arduino Mega pin 31
    /* V26 */ GPIO(32),         // Arduino Mega pin 32
    /* V27 */ GPIO(33),         // Arduino Mega pin 33
    /* V28 */ GPIO(34),         // Arduino Mega pin 34
    /* V29 */ GPIO(35),         // Arduino Mega pin 35
    /* V30 */ GPIO(36),         // Arduino Mega pin 36
    /* V31 */ GPIO(37),         // Arduino Mega pin 37
    /* V32 */ GPIO(38),         // Arduino Mega pin 38
    /* V33 */ GPIO(39),         // Arduino Mega pin 39
    /* V34 */ GPIO(40),         // Arduino Mega pin 40
    /* V35 */ GPIO(41),         // Arduino Mega pin 41
    /* V36 */ GPIO(42),         // Arduino Mega pin 42
    /* V37 */ GPIO(43),         // Arduino Mega pin 43
    /* V38 */ GPIO(44),         // Arduino Mega pin 44
    /* V39 */ GPIO(45),         // Arduino Mega pin 45
    /* V40 */ GPIO(46),         // Arduino Mega pin 46
    /* V41 */ GPIO(47),         // Arduino Mega pin 47
    /* V42 */ GPIO(48),         // Arduino Mega pin 48
    /* V43 */ GPIO(49),         // Arduino Mega pin 49
    /* V44 */ GPIO(50),         // Arduino Mega pin 50
    /* V45 */ GPIO(51),         // Arduino Mega pin 51
    /* V46 */ GPIO(52),         // Arduino Mega pin 52
    /* V47 */ GPIO(53),         // Arduino Mega pin 53
    /* V48 */ NO_SENSOR,        // TODO: wire to expander or GPIO
    /* V49 */ NO_SENSOR,        // TODO: wire to expander or GPIO
    /* V50 */ NO_SENSOR,        // TODO: wire to expander or GPIO
    /* V51 */ NO_SENSOR,        // TODO: wire to expander or GPIO
    /* V52 */ NO_SENSOR,        // TODO: wire to expander or GPIO
    /* V53 */ NO_SENSOR,        // TODO: wire to expander or GPIO
};

// ── Edge Sensors (72 edges — road detection) ────────────────────────────────
// Row index = edge ID (E00–E71).  Most edges will use I2C expanders.
// Fill in as you wire your board.  NO_SENSOR = not yet connected.
const SensorPin EDGE_SENSOR_MAP[EDGE_COUNT] = {
    /* E00 */ EXPANDER(0, 5),   // Expander 0x25 bit 0
    /* E01 */ EXPANDER(1, 5),   // Expander 0x25 bit 1
    /* E02 */ EXPANDER(2, 5),   // Expander 0x25 bit 2
    /* E03 */ EXPANDER(3, 5),   // Expander 0x25 bit 3
    /* E04 */ EXPANDER(4, 5),   // Expander 0x25 bit 4
    /* E05 */ EXPANDER(5, 5),   // Expander 0x25 bit 5
    /* E06 */ EXPANDER(6, 5),   // Expander 0x25 bit 6
    /* E07 */ EXPANDER(7, 5),   // Expander 0x25 bit 7
    /* E08 */ EXPANDER(0, 6),   // Expander 0x26 bit 0
    /* E09 */ EXPANDER(1, 6),   // Expander 0x26 bit 1
    /* E10 */ EXPANDER(2, 6),   // Expander 0x26 bit 2
    /* E11 */ EXPANDER(3, 6),   // Expander 0x26 bit 3
    /* E12 */ EXPANDER(4, 6),   // Expander 0x26 bit 4
    /* E13 */ EXPANDER(5, 6),   // Expander 0x26 bit 5
    /* E14 */ EXPANDER(6, 6),   // Expander 0x26 bit 6
    /* E15 */ EXPANDER(7, 6),   // Expander 0x26 bit 7
    /* E16 */ EXPANDER(0, 7),   // Expander 0x27 bit 0
    /* E17 */ EXPANDER(1, 7),   // Expander 0x27 bit 1
    /* E18 */ EXPANDER(2, 7),   // Expander 0x27 bit 2
    /* E19 */ EXPANDER(3, 7),   // Expander 0x27 bit 3
    /* E20 */ EXPANDER(4, 7),   // Expander 0x27 bit 4
    /* E21 */ EXPANDER(5, 7),   // Expander 0x27 bit 5
    /* E22 */ EXPANDER(6, 7),   // Expander 0x27 bit 6
    /* E23 */ EXPANDER(7, 7),   // Expander 0x27 bit 7
    /* E24 */ NO_SENSOR,        // TODO: wire to expander or GPIO
    /* E25 */ NO_SENSOR,
    /* E26 */ NO_SENSOR,
    /* E27 */ NO_SENSOR,
    /* E28 */ NO_SENSOR,
    /* E29 */ NO_SENSOR,
    /* E30 */ NO_SENSOR,
    /* E31 */ NO_SENSOR,
    /* E32 */ NO_SENSOR,
    /* E33 */ NO_SENSOR,
    /* E34 */ NO_SENSOR,
    /* E35 */ NO_SENSOR,
    /* E36 */ NO_SENSOR,
    /* E37 */ NO_SENSOR,
    /* E38 */ NO_SENSOR,
    /* E39 */ NO_SENSOR,
    /* E40 */ NO_SENSOR,
    /* E41 */ NO_SENSOR,
    /* E42 */ NO_SENSOR,
    /* E43 */ NO_SENSOR,
    /* E44 */ NO_SENSOR,
    /* E45 */ NO_SENSOR,
    /* E46 */ NO_SENSOR,
    /* E47 */ NO_SENSOR,
    /* E48 */ NO_SENSOR,
    /* E49 */ NO_SENSOR,
    /* E50 */ NO_SENSOR,
    /* E51 */ NO_SENSOR,
    /* E52 */ NO_SENSOR,
    /* E53 */ NO_SENSOR,
    /* E54 */ NO_SENSOR,
    /* E55 */ NO_SENSOR,
    /* E56 */ NO_SENSOR,
    /* E57 */ NO_SENSOR,
    /* E58 */ NO_SENSOR,
    /* E59 */ NO_SENSOR,
    /* E60 */ NO_SENSOR,
    /* E61 */ NO_SENSOR,
    /* E62 */ NO_SENSOR,
    /* E63 */ NO_SENSOR,
    /* E64 */ NO_SENSOR,
    /* E65 */ NO_SENSOR,
    /* E66 */ NO_SENSOR,
    /* E67 */ NO_SENSOR,
    /* E68 */ NO_SENSOR,
    /* E69 */ NO_SENSOR,
    /* E70 */ NO_SENSOR,
    /* E71 */ NO_SENSOR,
};
