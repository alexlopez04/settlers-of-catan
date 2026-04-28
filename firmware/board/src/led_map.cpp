// =============================================================================
// led_map.cpp — LED strip address tables for tiles and ports.
//
// *** WIRING GUIDE ***
// The LED strip is a single WS2812B strand.  Each entry maps a board element
// to one or more positions in the strip (0-based index).
//
// For tiles:  up to MAX_LEDS_PER_TILE LEDs.  Default layout uses 2 per tile
//             (indices 0–37 for 19 tiles × 2 LEDs).
// For ports:  up to MAX_LEDS_PER_PORT LEDs.  Default layout uses 1 per port
//             (indices 38–46 for 9 ports × 1 LED).
//
// To add more LEDs to a tile, increase MAX_LEDS_PER_TILE in config.h and
// add the extra indices here.  Set count to 0 to disable LEDs for an element.
// =============================================================================

#include "led_map.h"

// ── LED Assignment Diagram ─────────────────────────────────────────────────
// Pointy-top axial hex (q right, r down-right).  LED strip indices shown
// below each tile.  !! marks conflicting entries (see BUG note below).
//
//               T15( 0,-2)  T16( 1,-2)  T17( 2,-2)
//                 30,31       32,33       34,35
//
//         T14(-1,-1)  T05( 0,-1)  T06( 1,-1)  T18( 2,-1)
//           28,29       10,11       12,13       36,37
//
// T13(-2, 0)  T04(-1, 0)  T00( 0, 0)  T01( 1, 0)  T07( 2, 0)
//   26,27        8, 9        1, 2        3, 4       14,15
//
//         T12(-2, 1)  T03(-1, 1)  T02( 0, 1)  T08( 1, 1)
//           24,25      !!6, 7      !!6, 7       16,17
//
//               T11(-2, 2)  T10(-1, 2)  T09( 0, 2)
//                 22,23       20,21       18,19
//
// !! BUG: T02 and T03 are both mapped to {6, 7} — T03 is almost certainly
//    a copy-paste error from T02.  Verify physical wiring and correct T03.
// !! LED index 0 is unassigned (T00 starts at 1).
// !! LED index 5 is unassigned (gap: T01 ends at 4, T02/T03 start at 6).
// ──────────────────────────────────────────────────────────────────────────

// ── Tile LED Map (19 tiles) ─────────────────────────────────────────────────
// Row index = tile ID.  {indices[], count}
// Default: 2 LEDs per tile, sequential on the strip.
const TileLedMap TILE_LED_MAP[TILE_COUNT] = {
    /* T00 ( 0, 0) */ { { 22, 23}, 2 },
    /* T01 ( 1, 0) */ { { 24, 25}, 2 },
    /* T02 ( 0, 1) */ { { 32, 33}, 2 },
    /* T03 (-1, 1) */ { { 34,  35}, 2 },
    /* T04 (-1, 0) */ { { 20,  21}, 2 },
    /* T05 ( 0,-1) */ { {13, 14}, 2 },
    /* T06 ( 1,-1) */ { {11, 12}, 2 },
    /* T07 ( 2, 0) */ { {26, 27}, 2 },
    /* T08 ( 1, 1) */ { {30, 31}, 2 },
    /* T09 ( 0, 2) */ { {46, 47}, 2 },
    /* T10 (-1, 2) */ { {43, 44}, 2 },
    /* T11 (-2, 2) */ { {41, 42}, 2 },
    /* T12 (-2, 1) */ { {36, 37}, 2 },
    /* T13 (-2, 0) */ { {18, 19}, 2 },
    /* T14 (-1,-1) */ { {15, 16}, 2 },
    /* T15 ( 0,-2) */ { {1, 2}, 2 },
    /* T16 ( 1,-2) */ { {3, 4}, 2 },
    /* T17 ( 2,-2) */ { {6, 7}, 2 },
    /* T18 ( 2,-1) */ { {9, 10}, 2 },
};

// ── Port LED Map (9 ports) ──────────────────────────────────────────────────
// Row index = port ID.  {indices[], count}
// Default: 1 LED per port, sequential after the tile LEDs.
const PortLedMap PORT_LED_MAP[PORT_COUNT] = {
    /* P0 (3:1)       */ { {48}, 1 },
    /* P1 (Lumber 2:1)*/ { {28}, 1 },
    /* P2 (3:1)       */ { {8}, 1 },
    /* P3 (Wool 2:1)  */ { {5}, 1 },
    /* P4 (3:1)       */ { {0}, 1 },
    /* P5 (Grain 2:1) */ { {17}, 1 },
    /* P6 (Brick 2:1) */ { {38}, 1 },
    /* P7 (3:1)       */ { {40}, 1 },
    /* P8 (Ore 2:1)   */ { {45}, 1 },
};
