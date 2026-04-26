// =============================================================================
// led_manager.cpp — Addressable LED control (FastLED on the ESP32-C6 RMT
// peripheral). All animations are non-blocking and driven by tick(now_ms);
// the game task simply enqueues a flash request and keeps marching.
// =============================================================================

#include "led_manager.h"
#include "board_topology.h"
#include "led_map.h"

#include <Arduino.h>
#include <string.h>

namespace {

CRGB g_leds[TOTAL_LED_COUNT];
CRGB g_tile_base[TILE_COUNT];

CRGB biomeColor(Biome b) {
    switch (b) {
        case Biome::FOREST:   return CRGB(0, 200, 0);
        case Biome::PASTURE:  return CRGB(255, 255, 0);
        case Biome::FIELD:    return CRGB(255, 165, 0);
        case Biome::HILL:     return CRGB(255, 0, 0);
        case Biome::MOUNTAIN: return CRGB(128, 0, 128);
        case Biome::DESERT:   return CRGB(255, 255, 255);
        default:              return CRGB::Black;
    }
}

// ── Non-blocking flash queue ───────────────────────────────────────────────
struct FlashJob {
    bool     active;
    uint8_t  tiles[TILE_COUNT];
    uint8_t  num_tiles;
    CRGB     color;
    uint8_t  blinks_remaining;  // total half-cycles left
    bool     showing_color;     // current half: showing flash color vs base
    uint16_t interval_ms;
    uint32_t next_toggle_ms;
};

constexpr uint8_t MAX_FLASH_JOBS = 4;
FlashJob g_jobs[MAX_FLASH_JOBS] = {};

void renderJob(const FlashJob& j) {
    for (uint8_t i = 0; i < j.num_tiles; ++i) {
        const TileLedMap& m = TILE_LED_MAP[j.tiles[i]];
        CRGB c = j.showing_color ? j.color : g_tile_base[j.tiles[i]];
        for (uint8_t k = 0; k < m.count; ++k) g_leds[m.indices[k]] = c;
    }
}

}  // namespace

namespace led {

void init() {
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(g_leds, TOTAL_LED_COUNT);
    FastLED.clear(true);
    memset(g_tile_base, 0, sizeof(g_tile_base));
}

void setTileColor(uint8_t tile_id, const CRGB& color) {
    if (tile_id >= TILE_COUNT) return;
    const TileLedMap& m = TILE_LED_MAP[tile_id];
    for (uint8_t i = 0; i < m.count; ++i) g_leds[m.indices[i]] = color;
    g_tile_base[tile_id] = color;
}

void setAllTiles(const CRGB& color) {
    for (uint8_t i = 0; i < TILE_COUNT; ++i) setTileColor(i, color);
}

void setPortColor(uint8_t port_id, const CRGB& color) {
    if (port_id >= PORT_COUNT) return;
    const PortLedMap& m = PORT_LED_MAP[port_id];
    for (uint8_t i = 0; i < m.count; ++i) g_leds[m.indices[i]] = color;
}

void setAllPorts(const CRGB& color) {
    for (uint8_t i = 0; i < PORT_COUNT; ++i) setPortColor(i, color);
}

void colorTileByBiome(uint8_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    setTileColor(tile_id, biomeColor(g_tile_state[tile_id].biome));
}

void colorAllTilesByBiome() {
    for (uint8_t i = 0; i < TILE_COUNT; ++i) colorTileByBiome(i);
}

void flashTiles(const uint8_t* tile_ids, uint8_t num_tiles,
                const CRGB& color, uint8_t count, uint16_t interval_ms) {
    if (num_tiles == 0 || count == 0) return;
    for (auto& j : g_jobs) {
        if (j.active) continue;
        j.active = true;
        j.num_tiles = (num_tiles > TILE_COUNT) ? TILE_COUNT : num_tiles;
        memcpy(j.tiles, tile_ids, j.num_tiles);
        j.color = color;
        // Each "blink" = 2 half-cycles (color on, base off).
        j.blinks_remaining = (uint8_t)(count * 2);
        j.showing_color = true;
        j.interval_ms = interval_ms;
        j.next_toggle_ms = millis() + interval_ms;
        renderJob(j);
        FastLED.show();
        return;
    }
    // No free job slot: just blink immediately as a fall-back.
    for (uint8_t i = 0; i < num_tiles; ++i) setTileColor(tile_ids[i], color);
    FastLED.show();
}

void highlightTile(uint8_t tile_id, const CRGB& color, uint16_t duration_ms) {
    if (tile_id >= TILE_COUNT) return;
    uint8_t one[1] = { tile_id };
    flashTiles(one, 1, color, 1, duration_ms);
}

void dimTile(uint8_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    CRGB dimmed = g_tile_base[tile_id];
    dimmed.nscale8(64);
    const TileLedMap& m = TILE_LED_MAP[tile_id];
    for (uint8_t i = 0; i < m.count; ++i) g_leds[m.indices[i]] = dimmed;
}

void undimTile(uint8_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    const TileLedMap& m = TILE_LED_MAP[tile_id];
    for (uint8_t i = 0; i < m.count; ++i) g_leds[m.indices[i]] = g_tile_base[tile_id];
}

void show() { FastLED.show(); }

void clear() {
    FastLED.clear(true);
    memset(g_tile_base, 0, sizeof(g_tile_base));
}

void tick(uint32_t now_ms) {
    bool dirty = false;
    for (auto& j : g_jobs) {
        if (!j.active) continue;
        if ((int32_t)(now_ms - j.next_toggle_ms) < 0) continue;
        j.showing_color = !j.showing_color;
        if (j.blinks_remaining > 0) --j.blinks_remaining;
        if (j.blinks_remaining == 0) {
            // Restore tiles to base before retiring.
            j.showing_color = false;
            renderJob(j);
            j.active = false;
        } else {
            renderJob(j);
            j.next_toggle_ms = now_ms + j.interval_ms;
        }
        dirty = true;
    }
    if (dirty) FastLED.show();
}

}  // namespace led
