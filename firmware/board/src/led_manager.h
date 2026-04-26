#pragma once
// =============================================================================
// led_manager.h — Addressable LED control for tiles and ports.
// =============================================================================

#include <stdint.h>
#include <FastLED.h>
#include "config.h"

namespace led {

void init();

void setTileColor(uint8_t tile_id, const CRGB& color);
void setAllTiles(const CRGB& color);

void setPortColor(uint8_t port_id, const CRGB& color);
void setAllPorts(const CRGB& color);

void colorTileByBiome(uint8_t tile_id);
void colorAllTilesByBiome();

void flashTiles(const uint8_t* tile_ids, uint8_t num_tiles,
                const CRGB& color, uint8_t count = 3, uint16_t interval_ms = 150);
void highlightTile(uint8_t tile_id, const CRGB& color, uint16_t duration_ms = 400);
void dimTile(uint8_t tile_id);
void undimTile(uint8_t tile_id);

// Cancel all in-flight flash animations immediately.  Call before any bulk
// LED reset to prevent stale jobs from overwriting the new display state.
void cancelAllJobs();

// Mark g_leds as modified; FastLED.show() will fire on the next commit().
// This is called by all LED-write helpers (show(), tick(), flashTiles(), etc.)
// — callers should not call FastLED.show() directly.
void show();

// Transmit g_leds to the strip if anything changed since the last commit().
// Call exactly ONCE per loop() iteration — this is the sole FastLED.show()
// call site, preventing back-to-back RMT transmissions that corrupt WS2812B
// data for unrelated LEDs further down the strip.
void commit();

void clear();

// Drive non-blocking flash animations forward. Call once per loop tick.
void tick(uint32_t now_ms);

}  // namespace led
