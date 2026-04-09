
# Electronic Settlers of Catan Board

Arduino Mega firmware for an electronic Settlers of Catan board with addressable
LEDs, hall-effect sensors, buttons, and an I2C character LCD.

## Hardware

| Component | Details |
|---|---|
| **Controller** | Arduino Mega 2560 |
| **LEDs** | WS2812B addressable strip — 2 per tile (×19 = 38) + 1 per port (×9 = 47 minimum) |
| **Hall Sensors** | One per vertex (54), edge (72), and tile (19) — via GPIO + PCF8574 I2C expanders |
| **Buttons** | 3× tactile — Next (pin 8), Enter (pin 9), Dice (pin 10) |
| **Display** | 20×4 I2C character LCD at address `0x27` |
| **I2C Expanders** | Up to 4× PCF8574 at `0x20`–`0x23` (8 GPIO each) |

## Pin Mapping

### Buttons

| Function | Pin | Mode |
|---|---|---|
| Next | 8 | `INPUT_PULLUP` (active-low) |
| Enter | 9 | `INPUT_PULLUP` (active-low) |
| Dice / Roll | 10 | `INPUT_PULLUP` (active-low) |

### LEDs

| Range | Purpose |
|---|---|
| 0–37 | Tile LEDs (2 per tile, 19 tiles) |
| 38–46 | Port LEDs (1 per port, 9 ports) |

The LED data line connects to **pin 3**.

### Hall Sensors — Direct GPIO

Mega digital pins 22–53 (and A0–A15) can be used directly. Configure in
`board_layout.cpp` → `g_vertex_sensors[]`, `g_edge_sensors[]`, `g_tile_sensors[]`.

### Hall Sensors — I2C Expanders

PCF8574 expanders on the I2C bus (SDA=20, SCL=21) extend GPIO. Each provides
8 inputs. Configure addresses in `config.h` → `EXPANDER_ADDRS[]`.

## Firmware Structure

```
firmware/
├── platformio.ini
└── src/
    ├── main.cpp            ← Setup/loop, phase state machine
    ├── config.h            ← Pin mappings, timing, board geometry sizes
    ├── board_layout.h/cpp  ← Tile/port/vertex/edge tables, adjacency, randomization
    ├── led_manager.h/cpp   ← LED control, animations, biome coloring
    ├── sensor_manager.h/cpp← Hall sensor reading (GPIO + I2C expanders)
    ├── button_manager.h/cpp← Debounced button input
    ├── display_manager.h/cpp← I2C LCD display helpers
    ├── game_state.h/cpp    ← Game logic, turns, ownership, robber tracking
    └── dice.h/cpp          ← Random dice utility
```

## Building

```sh
cd firmware
pio run            # Compile
pio run -t upload  # Flash to Mega
pio device monitor # Serial monitor (115200 baud)
```

## Gameplay Flow

### 1. Player Select
On boot, the display prompts for player count. Press **Next** to cycle 2→3→4→2…,
then **Enter** to confirm.

### 2. Wait to Start
Press any button to begin board setup.

### 3. Board Setup
Biomes and numbers are randomly assigned per standard Catan rules:
- 4× Forest, 4× Pasture, 4× Field, 3× Hill, 3× Mountain, 1× Desert
- Numbers 2–12 (excluding 7), distributed across non-desert tiles
- Ports assigned to fixed coastal vertices

Tiles light up in their biome color; ports light up by type.

### 4. Number Reveal
Each number (2, 3, 4, 5, 6, 8, 9, 10, 11, 12) is shown in sequence. Matching
tiles turn white. Press **Next** to advance. After all numbers are shown,
gameplay begins.

### 5. Playing
- **Dice** button: rolls two dice, flashes matching tiles.
- **Next** button: ends the current player's turn.
- **Vertex sensors**: detect settlements/cities. Adjacent tiles flash in the
  player's color. Placing again on an owned settlement upgrades to a city.
- **Edge sensors**: detect roads. Adjacent tiles flash.
- **Tile sensors**: detect robber placement. The robber's tile dims; the
  previous tile is restored.

## Configuration

### Changing Board Wiring

Edit the tables in `board_layout.cpp`:

- `g_tiles[]` — LED indices, sensor pins, vertex/edge IDs per tile
- `g_ports[]` — LED index and vertex pair per port
- `g_vertex_sensors[]` — maps vertex IDs to GPIO pins or expander bits
- `g_edge_sensors[]` — maps edge IDs to GPIO pins or expander bits
- `g_tile_sensors[]` — maps tile IDs to GPIO pins or expander bits

### Changing Biome Distribution

Edit `randomizeBoardLayout()` in `board_layout.cpp`. The biome array and number
token array are defined at the top of that function.

### Changing Number/Reveal Rules

The reveal order and number tokens are in `game_state.cpp` (`kRevealOrder[]`)
and `board_layout.cpp` (`numbers[]` array).

### Adding More I2C Expanders

Increase `EXPANDER_COUNT` in `config.h` and add addresses to `EXPANDER_ADDRS[]`.
Then reference the new expander index in the sensor tables.

### Changing LED Colors

Biome→color mapping is in `led_manager.cpp` → `biomeColor()`.
Port→color mapping is in `main.cpp` → `portColor()`.
Player colors are in `main.cpp` → `kPlayerColors[]`.

## Project Structure

- `firmware/`: Source code for two components:
	- `board/`: Main board firmware
	- `player/`: Player station firmware
- `proto/`: Protocol Buffer definitions and generation scripts

## Getting Started

1. **Install dependencies**
	 - PlatformIO and NanoPB are required.
	 - Example for macOS:
		 ```sh
		 brew install platformio nanopb
		 ```

2. **Generate Protocol Buffer files**
	 - Run the generation script:
		 ```sh
		 cd proto
		 ./generate.sh
		 ```

3. **Build firmware**
	 - Use PlatformIO to compile either board or player firmware:
		 ```sh
		 cd firmware/board  # or firmware/player
		 pio run
		 ```

## Notes

- Make sure all dependencies are installed before building.
- Generated Protobuf files are located in `proto_gen/` and should not be committed.
- Platform IO output is in `firmware/*/.pio/` and should also not be committed.
