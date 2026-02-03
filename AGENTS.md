# PELLETINO Agent Reference

Quick reference for AI agents working on this project.

## TODO

- [x] Upgrade audio sample rate to 44.1kHz (completed 2026-02-03)

## ESP-IDF Environment Setup

**ESP-IDF Version:** 5.3+  
**Target:** ESP32-C6  
**Python:** 3.8+

### Source the Environment

```bash
# Adjust paths to your ESP-IDF installation
. $HOME/esp/esp-idf/export.sh

# Or if using custom path:
# . /path/to/esp-idf/export.sh
```

### Common Commands

```bash
# Set target (first time only)
idf.py set-target esp32c6

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

# Size analysis
idf.py size-components
```

### Alternative Build & Flash (if idf.py not in PATH)

```bash
# Source ESP-IDF environment first
source ~/esp/v5.3.4/esp-idf/export.sh

# Then run commands from PELLETINO directory
cd ~/Projects/msp/PELLETINO
idf.py -p /dev/cu.usbmodem5101 flash

# Or using esptool directly (from build directory)
cd ~/Projects/msp/PELLETINO/build
python3 -m esptool --chip esp32c6 --port /dev/cu.usbmodem5101 --baud 460800 \
  --before default_reset --after hard_reset write_flash \
  --flash_mode dio --flash_freq 80m --flash_size 4MB \
  0x0 bootloader/bootloader.bin \
  0x10000 pelletino.bin \
  0x8000 partition_table/partition-table.bin
```

**Flash Timing:** At 460800 baud, flashing ~866KB should take 15-20 seconds. If it takes >3 minutes, the connection may have hung - try resetting the board or checking the USB connection.

## Video Feature

The project includes an MJPEG video player that displays on the ST7789 screen at full 240×280 resolution.

### Generating Video Files

**IMPORTANT:** Always use `make_fiesta.sh` to generate the video header file. This script:
- Scales video to exactly 240×280 (full screen, cropped to fill)
- Encodes at 24 FPS for smooth playback
- Compresses to target file size using quality tuning (iterates q:v from 10-31)
- Outputs directly to `fiesta_data.h` format with correct C types

```bash
cd movie
./make_fiesta.sh Pac_Man_Fiesta_Float_Animation.mp4 3000 fiesta_data.h
```

Parameters:
- Arg 1: Input video file (any ffmpeg-supported format)
- Arg 2: Target size in KB (use 3000 for ~3MB, max for partition)
- Arg 3: Output header file name

**Do NOT use `convert_video.py` directly** - it doesn't handle resolution/FPS/cropping properly.

### Video Playback Behavior

- **At Boot:** Video plays immediately after display initialization, before game starts
- **At Game Over:** Video plays after lives reach 0 (5-second delay for game over sequence)
- **During Demo:** Video will trigger after demo game over
- Video is decoded using TinyJPEG (tjpgd) at 24 FPS
- 240×280 full screen with proper aspect ratio (cropped, not padded)
- ~2.3 MB video file, ~143 frames (6 seconds at 24 FPS)

### Implementation Files

- Video decoder: `main/src/fiesta_video.c`
- Game over detection: `main/src/game_state.c` (monitors RAM address 0x4E14 for lives)
- Video data: `movie/fiesta_data.h` (generated, 2.2-2.3 MB)
- Generation script: `movie/make_fiesta.sh`

### Troubleshooting

- **Black screen:** Check serial monitor - video might be playing but not visible
- **Static/corruption:** Regenerate video with `make_fiesta.sh` using correct resolution
- **Early game over:** Game state detection waits 5 seconds (300 frames) before monitoring
- **Binary too large:** Reduce target size (arg 2) or video duration in script

## ROM Conversion

### Required ROMs

Place Pac-Man ROM files in `tools/` directory:
- `pacman.6e`, `pacman.6f`, `pacman.6h`, `pacman.6j` (program ROMs)
- `pacman.5e` (tiles), `pacman.5f` (sprites)
- `82s123.7f`, `82s126.4a`, `82s126.1m`, `82s126.3m` (PROMs)

### Convert ROMs

```bash
cd tools
python3 convert_roms.py
```

Output files are generated in `main/roms/`:
- `pacman_rom.h` - Program code
- `pacman_tilemap.h` - Tile graphics
- `pacman_spritemap.h` - Sprite graphics
- `pacman_cmap.h` - Color palette
- `pacman_wavetable.h` - Audio waveforms

## Hardware Target

- **MCU:** ESP32-C6 RISC-V @ 160MHz
- **Display:** ST7789 240×280 @ 40MHz SPI
- **Audio:** ES8311 I2S codec @ 22.05kHz
- **Board:** Waveshare ESP32-C6-LCD-1.69
- **IMU:** QMI8658 6-axis (optional tilt control)

## Project Structure

```
PELLETINO/
├── main/
│   ├── main.c            # Main entry point
│   ├── src/              # Game logic
│   ├── include/          # Headers
│   └── roms/             # Generated ROM headers (gitignored)
├── components/
│   ├── audio_hal/        # ES8311 + WSG synthesis
│   ├── display/          # ST7789 driver
│   ├── z80_cpu/          # Z80 emulator
│   ├── pacman_hw/        # Hardware emulation
│   └── input/            # Button/IMU input
├── tools/
│   └── convert_roms.py   # ROM converter
└── docs/                 # Documentation
```

## Configuration

### Display Settings

`components/display/include/st7789.h`:
```c
#define LCD_WIDTH  240
#define LCD_HEIGHT 280
#define LCD_SPI_FREQ_HZ (40 * 1000 * 1000)
```

### Audio Settings

`components/audio_hal/include/wsg_synth.h`:
```c
#define WSG_SAMPLE_RATE 22050
#define WSG_CHANNELS 3
```

### Performance Tuning

In `idf.py menuconfig`:
- CPU frequency: 160 MHz
- FreeRTOS tick rate: 1000 Hz
- Compiler optimization: Performance (-O2)

## Development Workflow

1. **Make code changes**
2. **Build:** `idf.py build`
3. **Check errors:** Review compiler output
4. **Flash:** `idf.py flash`
5. **Monitor:** `idf.py monitor` (Ctrl+] to exit)
6. **Debug:** Use serial output and ESP-IDF logging

## Debugging

### Enable verbose logging

In code:
```c
#include "esp_log.h"
static const char *TAG = "PACMAN";

ESP_LOGI(TAG, "Info message");
ESP_LOGW(TAG, "Warning message");
ESP_LOGE(TAG, "Error message");
```

In `idf.py menuconfig`:
- Component config → Log output → Default log verbosity → Info

### Common Issues

**Build fails:** Ensure ROM headers exist (run `convert_roms.py`)
**Flash fails:** Check USB connection and port permissions
**No display:** Verify SPI connections and menuconfig settings
**No audio:** Check I2S/I2C wiring to ES8311

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
