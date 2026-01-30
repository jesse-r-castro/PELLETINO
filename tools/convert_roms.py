#!/usr/bin/env python3
"""
convert_roms.py - Convert Pac-Man / Ms. Pac-Man ROM files for PELLETINO

Converts original Pac-Man and Ms. Pac-Man ROM files into C header files 
suitable for the ESP32-C6 PELLETINO project.

Based on Galagino's ROM conversion scripts.

Usage:
    python3 convert_roms.py [rom_directory] [output_directory] [--game pacman|mspacman]

If no arguments provided, uses:
    rom_directory: ../../../rom/
    output_directory: ../main/roms/
    game: pacman (auto-detects Ms. Pac-Man if boot1 present)
"""

import sys
import os
import argparse
from pathlib import Path

# ROM file definitions for Pac-Man
PACMAN_ROM_FILES = {
    'program': ['pacman.6e', 'pacman.6f', 'pacman.6h', 'pacman.6j'],
    'tiles': ['pacman.5e'],
    'sprites': ['pacman.5f'],
    'color_prom': ['82s123.7f'],
    'palette_prom': ['82s126.4a'],
    'sound_prom': ['82s126.1m', '82s126.3m'],
}

# ROM file definitions for Ms. Pac-Man
# Ms. Pac-Man uses "boot" ROMs that patch the original Pac-Man code
MSPACMAN_ROM_FILES = {
    'program': ['boot1', 'boot2', 'boot3', 'boot4', 'boot5', 'boot6'],
    'tiles': ['5e'],           # Character tiles (often same as Pac-Man)
    'sprites': ['5f'],          # Sprite graphics (similar with Ms. Pac-Man additions)
    'color_prom': ['82s123.7f'],
    'palette_prom': ['82s126.4a'],
    'sound_prom': ['82s126.1m', '82s126.3m'],
}

# Alternative Ms. Pac-Man ROM names (some MAME sets use these)
MSPACMAN_ALT_FILES = {
    'program': ['pacman.6e', 'pacman.6f', 'pacman.6h', 'pacman.6j',
                'u5', 'u6', 'u7'],  # 4 base + 3 patches
    'tiles': ['5e.cpu', 'pacman.5e'],
    'sprites': ['5f.cpu', 'pacman.5f'],
}

# Global to track which game we're converting
ROM_FILES = PACMAN_ROM_FILES
GAME_NAME = 'pacman'


def read_rom_file(path):
    """Read a binary ROM file."""
    with open(path, 'rb') as f:
        return f.read()


def find_rom_file(rom_dir, candidates):
    """Find a ROM file from a list of possible names."""
    for name in candidates:
        path = rom_dir / name
        if path.exists():
            return path, name
    return None, None


def to_c_array(data, name, type_str='uint8_t', items_per_line=16):
    """Convert binary data to a C array declaration."""
    lines = [f'static const {type_str} {name}[] = {{']
    
    for i in range(0, len(data), items_per_line):
        chunk = data[i:i+items_per_line]
        hex_values = ', '.join(f'0x{b:02X}' for b in chunk)
        lines.append(f'    {hex_values},')
    
    lines.append('};')
    lines.append(f'#define {name.upper()}_SIZE {len(data)}')
    return '\n'.join(lines)


def convert_program_rom(rom_dir, out_dir):
    """Convert program ROMs to a single header file."""
    global GAME_NAME
    print("Converting program ROMs...")
    
    rom_data = bytearray()
    
    if GAME_NAME == 'mspacman':
        # Ms. Pac-Man uses boot ROMs
        for rom_file in ROM_FILES['program']:
            path = rom_dir / rom_file
            if not path.exists():
                print(f"  ERROR: Missing {rom_file}")
                return False
            data = read_rom_file(path)
            rom_data.extend(data)
            print(f"  {rom_file}: {len(data)} bytes")
        
        header_name = 'mspacman_rom'
        guard_name = 'MSPACMAN_ROM_H'
        comment = 'Ms. Pac-Man Program ROM'
    else:
        # Regular Pac-Man
        for rom_file in ROM_FILES['program']:
            path = rom_dir / rom_file
            if not path.exists():
                print(f"  ERROR: Missing {rom_file}")
                return False
            data = read_rom_file(path)
            rom_data.extend(data)
            print(f"  {rom_file}: {len(data)} bytes")
        
        header_name = 'pacman_rom'
        guard_name = 'PACMAN_ROM_H'
        comment = 'Pac-Man Program ROM'
    
    output = [
        f'/* {header_name}.h - {comment} */',
        '/* AUTO-GENERATED - DO NOT EDIT */',
        '',
        f'#ifndef {guard_name}',
        f'#define {guard_name}',
        '',
        '#include <stdint.h>',
        '',
        to_c_array(rom_data, header_name),
        '',
        f'#endif // {guard_name}',
    ]
    
    out_path = out_dir / f'{header_name}.h'
    with open(out_path, 'w') as f:
        f.write('\n'.join(output))
    
    print(f"  Wrote {out_path} ({len(rom_data)} bytes)")
    return True


def convert_tiles(rom_dir, out_dir):
    """Convert tile graphics ROM."""
    global GAME_NAME
    print("Converting tile graphics...")
    
    # Try to find the tile ROM
    tile_names = ROM_FILES['tiles']
    if GAME_NAME == 'mspacman':
        tile_names = MSPACMAN_ROM_FILES['tiles'] + MSPACMAN_ALT_FILES.get('tiles', [])
    
    path, found_name = find_rom_file(rom_dir, tile_names)
    if not path:
        # Fall back to Pac-Man tile ROM if Ms. Pac-Man one not found
        path = rom_dir / 'pacman.5e'
        if not path.exists():
            print(f"  ERROR: Missing tile ROM (tried: {', '.join(tile_names)})")
            return False
        found_name = 'pacman.5e'
    
    raw_data = read_rom_file(path)
    print(f"  {found_name}: {len(raw_data)} bytes")
    
    def parse_chr(data):
        """Parse a single 8x8 tile from 16 bytes of ROM data.
        
        Based on Galagino's tile parsing algorithm.
        """
        char = []
        for y in range(8):
            row = []
            for x in range(8):
                # Galagino's proven indexing formula for tiles
                byte = data[15 - x - 2 * (y & 4)]
                c0 = 1 if byte & (0x08 >> (y & 3)) else 0
                c1 = 2 if byte & (0x80 >> (y & 3)) else 0
                row.append(c0 + c1)
            char.append(row)
        return char
    
    def dump_chr(char_data):
        """Convert tile to packed 16-bit words for each row."""
        words = []
        for y in range(8):
            val = 0
            for x in range(8):
                val = (val >> 2) + (char_data[y][x] << 14)
            words.append(val & 0xFFFF)
        return words
    
    # Parse and convert all 256 tiles
    tile_words = []
    for tile_idx in range(256):
        tile_data = raw_data[16 * tile_idx : 16 * (tile_idx + 1)]
        char = parse_chr(tile_data)
        words = dump_chr(char)
        tile_words.extend(words)
    
    prefix = 'mspacman' if GAME_NAME == 'mspacman' else 'pacman'
    
    output = [
        f'/* {prefix}_tilemap.h - {GAME_NAME.title()} Tile Graphics */',
        '/* AUTO-GENERATED - DO NOT EDIT */',
        '',
        f'#ifndef {prefix.upper()}_TILEMAP_H',
        f'#define {prefix.upper()}_TILEMAP_H',
        '',
        '#include <stdint.h>',
        '',
        f'// {len(tile_words)} 16-bit words = 256 tiles × 8 rows',
        f'static const uint16_t {prefix}_5e[] = {{',
    ]
    
    for i in range(0, len(tile_words), 8):
        chunk = tile_words[i:i+8]
        hex_values = ', '.join(f'0x{w:04X}' for w in chunk)
        output.append(f'    {hex_values},')
    
    output.extend([
        '};',
        '',
        f'#endif // {prefix.upper()}_TILEMAP_H',
    ])
    
    out_path = out_dir / f'{prefix}_tilemap.h'
    with open(out_path, 'w') as f:
        f.write('\n'.join(output))
    
    print(f"  Wrote {out_path}")
    return True


def convert_sprites(rom_dir, out_dir):
    """Convert sprite graphics ROM."""
    global GAME_NAME
    print("Converting sprite graphics...")
    
    # Try to find the sprite ROM
    sprite_names = ROM_FILES['sprites']
    if GAME_NAME == 'mspacman':
        sprite_names = MSPACMAN_ROM_FILES['sprites'] + MSPACMAN_ALT_FILES.get('sprites', [])
    
    path, found_name = find_rom_file(rom_dir, sprite_names)
    if not path:
        # Fall back to Pac-Man sprite ROM if Ms. Pac-Man one not found
        path = rom_dir / 'pacman.5f'
        if not path.exists():
            print(f"  ERROR: Missing sprite ROM (tried: {', '.join(sprite_names)})")
            return False
        found_name = 'pacman.5f'
    
    raw_data = read_rom_file(path)
    print(f"  {found_name}: {len(raw_data)} bytes")
    
    def parse_sprite(data):
        """Parse a single 16x16 sprite from 64 bytes of ROM data.
        
        Based on Galagino's sprite parsing - Pac-Man format has a specific
        layout where the top 4 rows are actually the bottom 4.
        """
        sprite = []
        for y in range(16):
            row = []
            for x in range(16):
                # Galagino's proven indexing formula
                idx = ((y & 8) << 1) + (((x & 8) ^ 8) << 2) + (7 - (x & 7)) + 2 * (y & 4)
                if idx < len(data):
                    # Extract 2-bit pixel from planar data
                    c0 = 1 if data[idx] & (0x08 >> (y & 3)) else 0
                    c1 = 2 if data[idx] & (0x80 >> (y & 3)) else 0
                    row.append(c0 + c1)
                else:
                    row.append(0)
            sprite.append(row)
        
        # Pac-Man format: bottom 4 rows become top 4 rows
        sprite = sprite[4:] + sprite[:4]
        return sprite
    
    def dump_sprite(sprite_data, flip_x, flip_y):
        """Convert sprite to packed 32-bit words for each row."""
        rows = []
        y_range = reversed(range(16)) if flip_y else range(16)
        
        for y in y_range:
            val = 0
            for x in range(16):
                if not flip_x:
                    val = (val >> 2) + (sprite_data[y][x] << 30)
                else:
                    val = (val << 2) + sprite_data[y][x]
            rows.append(val & 0xFFFFFFFF)
        return rows
    
    # Parse all 64 sprites
    parsed_sprites = []
    for sprite_idx in range(64):
        sprite_data = raw_data[64 * sprite_idx : 64 * (sprite_idx + 1)]
        parsed_sprites.append(parse_sprite(sprite_data))
    
    # Convert sprites for 4 flip variants
    # Galagino order: mode 0=none, mode 1=flip_y, mode 2=flip_x, mode 3=both
    all_sprites = []
    for flip_mode in range(4):
        flip_y = (flip_mode & 1) != 0  # bit 0 = flip_y
        flip_x = (flip_mode & 2) != 0  # bit 1 = flip_x
        
        for sprite in parsed_sprites:
            rows = dump_sprite(sprite, flip_x, flip_y)
            all_sprites.extend(rows)
    
    prefix = 'mspacman' if GAME_NAME == 'mspacman' else 'pacman'
    
    output = [
        f'/* {prefix}_spritemap.h - {GAME_NAME.title()} Sprite Graphics */',
        '/* AUTO-GENERATED - DO NOT EDIT */',
        '',
        f'#ifndef {prefix.upper()}_SPRITEMAP_H',
        f'#define {prefix.upper()}_SPRITEMAP_H',
        '',
        '#include <stdint.h>',
        '',
        '// 4 flip modes × 64 sprites × 16 rows = 4096 32-bit words',
        f'static const uint32_t {prefix}_sprites[4][64][16] = {{',
    ]
    
    idx = 0
    for flip in range(4):
        output.append(f'  {{ // Flip mode {flip}')
        for sprite in range(64):
            row_data = all_sprites[idx:idx+16]
            hex_values = ', '.join(f'0x{w:08X}' for w in row_data)
            output.append(f'    {{ {hex_values} }},')
            idx += 16
        output.append('  },')
    
    output.extend([
        '};',
        '',
        f'#endif // {prefix.upper()}_SPRITEMAP_H',
    ])
    
    out_path = out_dir / f'{prefix}_spritemap.h'
    with open(out_path, 'w') as f:
        f.write('\n'.join(output))
    
    print(f"  Wrote {out_path}")
    return True


def convert_colormap(rom_dir, out_dir):
    """Convert color PROMs to RGB565 palette."""
    global GAME_NAME
    print("Converting color palette...")
    
    color_path = rom_dir / ROM_FILES['color_prom'][0]
    palette_path = rom_dir / ROM_FILES['palette_prom'][0]
    
    if not color_path.exists():
        print(f"  ERROR: Missing {ROM_FILES['color_prom'][0]}")
        return False
    if not palette_path.exists():
        print(f"  ERROR: Missing {ROM_FILES['palette_prom'][0]}")
        return False
    
    color_prom = read_rom_file(color_path)
    palette_prom = read_rom_file(palette_path)
    
    print(f"  {ROM_FILES['color_prom'][0]}: {len(color_prom)} bytes")
    print(f"  {ROM_FILES['palette_prom'][0]}: {len(palette_prom)} bytes")
    
    # Color PROM maps 3-bit values to RGB
    # Palette PROM maps attribute to 4 colors
    
    # First, decode the 32 base colors from color PROM
    base_colors = []
    for i in range(32):
        if i < len(color_prom):
            val = color_prom[i]
            # Pac-Man color format: xxRRRGGGBBB (resistor weighted)
            r = ((val >> 0) & 1) * 0x21 + ((val >> 1) & 1) * 0x47 + ((val >> 2) & 1) * 0x97
            g = ((val >> 3) & 1) * 0x21 + ((val >> 4) & 1) * 0x47 + ((val >> 5) & 1) * 0x97
            b = ((val >> 6) & 1) * 0x51 + ((val >> 7) & 1) * 0xAE
        else:
            r, g, b = 0, 0, 0
        
        # Convert to RGB565
        rgb565 = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        base_colors.append(rgb565)
    
    # Now build the 64 palettes (4 colors each) from palette PROM
    colormap = []
    for pal_idx in range(64):
        for color_idx in range(4):
            prom_addr = pal_idx * 4 + color_idx
            if prom_addr < len(palette_prom):
                color_ref = palette_prom[prom_addr] & 0x1F
                rgb565 = base_colors[color_ref] if color_ref < len(base_colors) else 0
            else:
                rgb565 = 0
            
            # Color 0 is transparent (black)
            if color_idx == 0:
                rgb565 = 0
            
            colormap.append(rgb565)
    
    prefix = 'mspacman' if GAME_NAME == 'mspacman' else 'pacman'
    
    output = [
        f'/* {prefix}_cmap.h - {GAME_NAME.title()} Color Palette (RGB565) */',
        '/* AUTO-GENERATED - DO NOT EDIT */',
        '',
        f'#ifndef {prefix.upper()}_CMAP_H',
        f'#define {prefix.upper()}_CMAP_H',
        '',
        '#include <stdint.h>',
        '',
        '// 64 palettes × 4 colors = 256 RGB565 values',
        f'static const uint16_t {prefix}_colormap[64][4] = {{',
    ]
    
    for pal_idx in range(64):
        colors = colormap[pal_idx * 4 : pal_idx * 4 + 4]
        hex_values = ', '.join(f'0x{c:04X}' for c in colors)
        output.append(f'  {{ {hex_values} }},  // Palette {pal_idx}')
    
    output.extend([
        '};',
        '',
        f'#endif // {prefix.upper()}_CMAP_H',
    ])
    
    out_path = out_dir / f'{prefix}_cmap.h'
    with open(out_path, 'w') as f:
        f.write('\n'.join(output))
    
    print(f"  Wrote {out_path}")
    return True


def convert_wavetable(rom_dir, out_dir):
    """Convert sound PROMs to wavetable."""
    global GAME_NAME
    print("Converting audio wavetable...")
    
    path1 = rom_dir / ROM_FILES['sound_prom'][0]
    path2 = rom_dir / ROM_FILES['sound_prom'][1]
    
    if not path1.exists():
        print(f"  ERROR: Missing {ROM_FILES['sound_prom'][0]}")
        return False
    if not path2.exists():
        print(f"  ERROR: Missing {ROM_FILES['sound_prom'][1]}")
        return False
    
    prom1 = read_rom_file(path1)
    prom2 = read_rom_file(path2)
    
    print(f"  {ROM_FILES['sound_prom'][0]}: {len(prom1)} bytes")
    print(f"  {ROM_FILES['sound_prom'][1]}: {len(prom2)} bytes")
    
    # Combine both PROMs into wavetable
    # Each PROM has 8 waveforms × 32 samples = 256 bytes
    # Values are 0-15, convert to signed -7 to +8 (like Galagino: value - 7)
    wavetable = []
    
    # First PROM: waves 0-7
    for wave_idx in range(8):
        for sample_idx in range(32):
            byte_val = prom1[wave_idx * 32 + sample_idx] if (wave_idx * 32 + sample_idx) < len(prom1) else 0
            # Galagino uses value - 7 (not value - 8)
            signed_val = (byte_val & 0x0F) - 7
            wavetable.append(signed_val)
    
    # Second PROM: waves 8-15
    for wave_idx in range(8):
        for sample_idx in range(32):
            byte_val = prom2[wave_idx * 32 + sample_idx] if (wave_idx * 32 + sample_idx) < len(prom2) else 0
            signed_val = (byte_val & 0x0F) - 7
            wavetable.append(signed_val)
    
    prefix = 'mspacman' if GAME_NAME == 'mspacman' else 'pacman'
    
    output = [
        f'/* {prefix}_wavetable.h - {GAME_NAME.title()} Audio Wavetable */',
        '/* AUTO-GENERATED - DO NOT EDIT */',
        '',
        f'#ifndef {prefix.upper()}_WAVETABLE_H',
        f'#define {prefix.upper()}_WAVETABLE_H',
        '',
        '#include <stdint.h>',
        '',
        '// 16 waveforms × 32 samples = 512 signed bytes',
        f'static const int8_t {prefix}_wavetable[16][32] = {{',
    ]
    
    for wave_idx in range(16):
        samples = wavetable[wave_idx * 32 : wave_idx * 32 + 32]
        hex_values = ', '.join(f'{s:3d}' for s in samples)
        output.append(f'  {{ {hex_values} }},  // Wave {wave_idx}')
    
    output.extend([
        '};',
        '',
        f'#endif // {prefix.upper()}_WAVETABLE_H',
    ])
    
    out_path = out_dir / f'{prefix}_wavetable.h'
    with open(out_path, 'w') as f:
        f.write('\n'.join(output))
    
    print(f"  Wrote {out_path}")
    return True


def detect_game(rom_dir):
    """Auto-detect which game based on ROM files present."""
    # Check for Ms. Pac-Man boot ROMs
    if (rom_dir / 'boot1').exists():
        return 'mspacman'
    # Check for alternate Ms. Pac-Man ROMs (u5, u6, u7 patches)
    if (rom_dir / 'u5').exists() and (rom_dir / 'u6').exists():
        return 'mspacman'
    # Default to Pac-Man
    return 'pacman'


def main():
    global ROM_FILES, GAME_NAME
    
    # Parse command line arguments
    parser = argparse.ArgumentParser(
        description='Convert Pac-Man / Ms. Pac-Man ROMs for PELLETINO'
    )
    parser.add_argument('rom_dir', nargs='?', help='ROM directory')
    parser.add_argument('output_dir', nargs='?', help='Output directory')
    parser.add_argument('--game', choices=['pacman', 'mspacman'],
                        help='Game to convert (auto-detects if not specified)')
    args = parser.parse_args()
    
    # Determine directories
    script_dir = Path(__file__).parent.resolve()
    
    if args.rom_dir:
        rom_dir = Path(args.rom_dir)
    else:
        rom_dir = script_dir.parent.parent.parent / 'rom'
    
    if args.output_dir:
        out_dir = Path(args.output_dir)
    else:
        out_dir = script_dir.parent / 'main' / 'roms'
    
    print(f"ROM directory: {rom_dir}")
    print(f"Output directory: {out_dir}")
    
    if not rom_dir.exists():
        print(f"ERROR: ROM directory not found: {rom_dir}")
        return 1
    
    # Detect or set game type
    if args.game:
        GAME_NAME = args.game
    else:
        GAME_NAME = detect_game(rom_dir)
    
    if GAME_NAME == 'mspacman':
        ROM_FILES = MSPACMAN_ROM_FILES
        print(f"Game: Ms. Pac-Man")
    else:
        ROM_FILES = PACMAN_ROM_FILES
        print(f"Game: Pac-Man")
    print()
    
    # Create output directory
    out_dir.mkdir(parents=True, exist_ok=True)
    
    # Convert all ROM types
    success = True
    success &= convert_program_rom(rom_dir, out_dir)
    success &= convert_tiles(rom_dir, out_dir)
    success &= convert_sprites(rom_dir, out_dir)
    success &= convert_colormap(rom_dir, out_dir)
    success &= convert_wavetable(rom_dir, out_dir)
    
    if success:
        print()
        print(f"{GAME_NAME.title()} ROM conversion complete!")
        return 0
    else:
        print()
        print("ROM conversion failed - check for missing files above")
        return 1


if __name__ == '__main__':
    sys.exit(main())
