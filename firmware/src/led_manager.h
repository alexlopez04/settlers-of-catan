#pragma once
// =============================================================================
// led_manager.h — Addressable LED control for tiles and ports.
//
// Uses led_map.h tables for LED-to-strip-index mapping, so the number of
// LEDs per tile/port is fully configurable.
// =============================================================================

#include <stdint.h>
#include <FastLED.h>
#include "config.h"

namespace led {

void init();

// ── Tile LEDs (variable count per tile, from led_map) ───────────────────────
void setTileColor(uint8_t tile_id, const CRGB& color);
void setAllTiles(const CRGB& color);

// ── Port LEDs (variable count per port, from led_map) ───────────────────────
void setPortColor(uint8_t port_id, const CRGB& color);
void setAllPorts(const CRGB& color);

// ── Biome-based coloring ────────────────────────────────────────────────────
void colorTileByBiome(uint8_t tile_id);
void colorAllTilesByBiome();

// ── Animations ──────────────────────────────────────────────────────────────
void flashTiles(const uint8_t* tile_ids, uint8_t num_tiles,
                const CRGB& color, uint8_t count = 3, uint16_t interval_ms = 150);
void highlightTile(uint8_t tile_id, const CRGB& color, uint16_t duration_ms = 400);
void dimTile(uint8_t tile_id);
void undimTile(uint8_t tile_id);

// ── Show (commit changes) ───────────────────────────────────────────────────
void show();
void clear();

}  // namespace led
