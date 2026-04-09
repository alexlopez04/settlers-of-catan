// =============================================================================
// led_manager.cpp — Addressable LED control.
// =============================================================================

#include "led_manager.h"
#include "board_layout.h"

namespace {
    CRGB leds[TOTAL_LED_COUNT];

    CRGB biomeColor(Biome b) {
        switch (b) {
            case Biome::FOREST:   return CRGB(0, 200, 0);      // Green (Wood)
            case Biome::PASTURE:  return CRGB(255, 255, 0);    // Yellow (Sheep)
            case Biome::FIELD:    return CRGB(255, 165, 0);    // Orange (Grain)
            case Biome::HILL:     return CRGB(255, 0, 0);      // Red (Brick)
            case Biome::MOUNTAIN: return CRGB(128, 0, 128);    // Purple (Stone)
            case Biome::DESERT:   return CRGB(255, 255, 255);  // White (Nothing)
            default:              return CRGB::Black;
        }
    }

    // Store the "base" color so we can restore after dimming/flashing.
    CRGB tile_base_color[TILE_COUNT];
}

namespace led {

void init() {
    FastLED.addLeds<WS2812B, LED_DATA_PIN, GRB>(leds, TOTAL_LED_COUNT);
    FastLED.clear(true);
    memset(tile_base_color, 0, sizeof(tile_base_color));
}

// ── Tile LEDs ───────────────────────────────────────────────────────────────

void setTileColor(uint8_t tile_id, const CRGB& color) {
    if (tile_id >= TILE_COUNT) return;
    const Tile& t = g_tiles[tile_id];
    leds[t.led_index_a] = color;
    leds[t.led_index_b] = color;
    tile_base_color[tile_id] = color;
}

void setAllTiles(const CRGB& color) {
    for (uint8_t i = 0; i < TILE_COUNT; ++i) {
        setTileColor(i, color);
    }
}

// ── Port LEDs ───────────────────────────────────────────────────────────────

void setPortColor(uint8_t port_id, const CRGB& color) {
    if (port_id >= PORT_COUNT) return;
    leds[g_ports[port_id].led_index] = color;
}

void setAllPorts(const CRGB& color) {
    for (uint8_t i = 0; i < PORT_COUNT; ++i) {
        setPortColor(i, color);
    }
}

// ── Biome-based coloring ────────────────────────────────────────────────────

void colorTileByBiome(uint8_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    setTileColor(tile_id, biomeColor(g_tiles[tile_id].biome));
}

void colorAllTilesByBiome() {
    for (uint8_t i = 0; i < TILE_COUNT; ++i) {
        colorTileByBiome(i);
    }
}

// ── Animations ──────────────────────────────────────────────────────────────

void flashTiles(const uint8_t* tile_ids, uint8_t num_tiles,
                const CRGB& color, uint8_t count, uint16_t interval_ms) {
    for (uint8_t c = 0; c < count; ++c) {
        // On
        for (uint8_t i = 0; i < num_tiles; ++i) {
            setTileColor(tile_ids[i], color);
        }
        show();
        delay(interval_ms);

        // Off (restore base)
        for (uint8_t i = 0; i < num_tiles; ++i) {
            colorTileByBiome(tile_ids[i]);
        }
        show();
        delay(interval_ms);
    }
}

void highlightTile(uint8_t tile_id, const CRGB& color, uint16_t duration_ms) {
    if (tile_id >= TILE_COUNT) return;
    setTileColor(tile_id, color);
    show();
    delay(duration_ms);
    colorTileByBiome(tile_id);
    show();
}

void dimTile(uint8_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    const Tile& t = g_tiles[tile_id];
    CRGB dimmed = tile_base_color[tile_id];
    dimmed.nscale8(64);  // ~25% brightness
    leds[t.led_index_a] = dimmed;
    leds[t.led_index_b] = dimmed;
}

void undimTile(uint8_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    const Tile& t = g_tiles[tile_id];
    leds[t.led_index_a] = tile_base_color[tile_id];
    leds[t.led_index_b] = tile_base_color[tile_id];
}

// ── Show / Clear ────────────────────────────────────────────────────────────

void show() {
    FastLED.show();
}

void clear() {
    FastLED.clear(true);
}

}  // namespace led
