# PELLETINO Agent Reference

Quick reference for AI agents working on this project.

## ESP-IDF Environment Setup

**ESP-IDF Version:** 5.3.4  
**Target:** ESP32-C6  
**Python Environment:** idf5.3_py3.14_env

### Source the Environment

```bash
export IDF_PYTHON_ENV_PATH=/Users/jecastro/.espressif/python_env/idf5.3_py3.14_env
. /Users/jecastro/esp/v5.3.4/esp-idf/export.sh
```

### Common Commands

```bash
# Build only
idf.py build

# Build and flash
idf.py flash

# Flash and monitor (Ctrl+] to exit monitor)
idf.py flash monitor

# Monitor only
idf.py monitor

# Clean build
idf.py fullclean

# Menuconfig
idf.py menuconfig
```

### One-liner for build/flash/monitor

```bash
cd /Users/jecastro/Projects/msp/PELLETINO && export IDF_PYTHON_ENV_PATH=/Users/jecastro/.espressif/python_env/idf5.3_py3.14_env && . /Users/jecastro/esp/v5.3.4/esp-idf/export.sh && idf.py flash monitor
```

## ROM Conversion

### Pac-Man ROMs
```bash
python3 tools/convert_roms.py /path/to/pacman/roms pacman
```

### Ms. Pac-Man ROMs
```bash
python3 tools/convert_roms.py /path/to/mspacman/roms mspacman
```

**Ms. Pac-Man ROM variants supported:**
- `boot1-boot6` standalone ROMs (pre-patched)
- `pacman.6e/f/h/j` + `u5/u6/u7` patch ROMs (midway variant)

## Game Selection

In `main/src/main.cpp`, uncomment the desired game:

```cpp
// #define GAME_PACMAN
#define GAME_MSPACMAN
```

## Hardware

- **MCU:** ESP32-C6 @ 160MHz
- **Display:** ST7789 240×280 @ 80MHz SPI
- **Audio:** ES8311 I2S codec @ 24kHz
- **Board:** FIESTA26

## Project Structure

```
PELLETINO/
├── main/
│   ├── src/main.cpp      # Main entry point, game selection
│   ├── include/          # Headers
│   └── roms/             # Generated ROM headers
├── tools/
│   └── convert_roms.py   # ROM converter
├── pacman/               # Pac-Man ROM headers (generated)
└── mspacman/             # Ms. Pac-Man ROM headers (generated)
```

## Known Issues

- Ms. Pac-Man u5/u6/u7 patching may need refinement if game crashes
- Audio has slight scratchiness (as good as it gets for now)

## Key Files

| File | Purpose |
|------|---------|
| `main/src/main.cpp` | Main loop, game selection |
| `tools/convert_roms.py` | ROM to C header converter |
| `sdkconfig.defaults` | ESP-IDF default configuration |
| `partitions.csv` | Flash partition layout |

## Debugging Tips

- Use `idf.py monitor` to see serial output
- Press Ctrl+] to exit monitor
- Check `build/pelletino.elf` for symbols
- Binary size should be ~340KB (84% of partition free)
