#pragma once
// =============================================================================
// led_manager.h — Addressable LED control for tiles and ports.
// =============================================================================

#include <stdint.h>
#include <FastLED.h>
#include "config.h"

namespace led {

void init();

// ── Tile LEDs (2 per tile) ──────────────────────────────────────────────────
void setTileColor(uint8_t tile_id, const CRGB& color);
void setAllTiles(const CRGB& color);

// ── Port LEDs (1 per port) ──────────────────────────────────────────────────
void setPortColor(uint8_t port_id, const CRGB& color);
void setAllPorts(const CRGB& color);

// ── Biome-based coloring ────────────────────────────────────────────────────
void colorTileByBiome(uint8_t tile_id);
void colorAllTilesByBiome();

// ── Animations ──────────────────────────────────────────────────────────────
// Flash specific tiles `count` times at `interval_ms`.
void flashTiles(const uint8_t* tile_ids, uint8_t num_tiles,
                const CRGB& color, uint8_t count = 3, uint16_t interval_ms = 150);

// Highlight a single tile briefly (e.g. number reveal).
void highlightTile(uint8_t tile_id, const CRGB& color, uint16_t duration_ms = 400);

// Dim a tile to indicate the robber is present.
void dimTile(uint8_t tile_id);
void undimTile(uint8_t tile_id);

// ── Show (commit changes) ───────────────────────────────────────────────────
void show();
void clear();

}  // namespace led
