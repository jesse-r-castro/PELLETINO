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
        # Check which Ms. Pac-Man variant we have
        if (rom_dir / 'boot1').exists():
            # Boot ROM variant (standalone Ms. Pac-Man ROMs)
            print("  Using boot ROM variant...")
            for rom_file in ['boot1', 'boot2', 'boot3', 'boot4', 'boot5', 'boot6']:
                path = rom_dir / rom_file
                if not path.exists():
                    print(f"  ERROR: Missing {rom_file}")
                    return False
                data = read_rom_file(path)
                rom_data.extend(data)
                print(f"  {rom_file}: {len(data)} bytes")
        elif (rom_dir / 'u5').exists():
            # Patch ROM variant (Pac-Man base + u5/u6/u7 patches)
            # Based on MAME's mspacman driver decryption (init_mspacman)
            print("  Using patch ROM variant (pacman.6* + u5/u6/u7)...")
            print("  Applying MAME-style decryption...")
            
            # Helper functions for MAME-style bitswap
            def bitswap8(val, b7, b6, b5, b4, b3, b2, b1, b0):
                """Rearrange bits: new bit position <- old bit position"""
                return (
                    (((val >> b7) & 1) << 7) |
                    (((val >> b6) & 1) << 6) |
                    (((val >> b5) & 1) << 5) |
                    (((val >> b4) & 1) << 4) |
                    (((val >> b3) & 1) << 3) |
                    (((val >> b2) & 1) << 2) |
                    (((val >> b1) & 1) << 1) |
                    (((val >> b0) & 1) << 0)
                )
            
            def bitswap11(val, b10, b9, b8, b7, b6, b5, b4, b3, b2, b1, b0):
                return (
                    (((val >> b10) & 1) << 10) |
                    (((val >> b9) & 1) << 9) |
                    (((val >> b8) & 1) << 8) |
                    (((val >> b7) & 1) << 7) |
                    (((val >> b6) & 1) << 6) |
                    (((val >> b5) & 1) << 5) |
                    (((val >> b4) & 1) << 4) |
                    (((val >> b3) & 1) << 3) |
                    (((val >> b2) & 1) << 2) |
                    (((val >> b1) & 1) << 1) |
                    (((val >> b0) & 1) << 0)
                )
            
            def bitswap12(val, b11, b10, b9, b8, b7, b6, b5, b4, b3, b2, b1, b0):
                return (
                    (((val >> b11) & 1) << 11) |
                    (((val >> b10) & 1) << 10) |
                    (((val >> b9) & 1) << 9) |
                    (((val >> b8) & 1) << 8) |
                    (((val >> b7) & 1) << 7) |
                    (((val >> b6) & 1) << 6) |
                    (((val >> b5) & 1) << 5) |
                    (((val >> b4) & 1) << 4) |
                    (((val >> b3) & 1) << 3) |
                    (((val >> b2) & 1) << 2) |
                    (((val >> b1) & 1) << 1) |
                    (((val >> b0) & 1) << 0)
                )
            
            # Load all source ROMs into a flat buffer at MAME addresses
            # MAME loads: pacman.6e@0x0000, pacman.6f@0x1000, pacman.6h@0x2000, pacman.6j@0x3000
            #             u5@0x8000, u6@0x9000, u7@0xb000
            src_rom = bytearray(0x10000)  # 64KB source space
            
            base_roms = [('pacman.6e', 0x0000), ('pacman.6f', 0x1000), 
                         ('pacman.6h', 0x2000), ('pacman.6j', 0x3000)]
            for rom_file, addr in base_roms:
                path = rom_dir / rom_file
                if not path.exists():
                    print(f"  ERROR: Missing base ROM {rom_file}")
                    return False
                data = read_rom_file(path)
                for i, b in enumerate(data):
                    src_rom[addr + i] = b
                print(f"  {rom_file}: {len(data)} bytes @ 0x{addr:04X}")
            
            # Load patch ROMs
            u5_path, u6_path, u7_path = rom_dir / 'u5', rom_dir / 'u6', rom_dir / 'u7'
            if not all(p.exists() for p in [u5_path, u6_path, u7_path]):
                print("  ERROR: Missing patch ROMs (u5, u6, u7)")
                return False
            
            u5_data = read_rom_file(u5_path)
            u6_data = read_rom_file(u6_path)
            u7_data = read_rom_file(u7_path)
            
            # Place at MAME addresses
            for i, b in enumerate(u5_data):
                src_rom[0x8000 + i] = b
            for i, b in enumerate(u6_data):
                src_rom[0x9000 + i] = b
            for i, b in enumerate(u7_data):
                src_rom[0xb000 + i] = b
            
            print(f"  u5: {len(u5_data)} bytes @ 0x8000")
            print(f"  u6: {len(u6_data)} bytes @ 0x9000")
            print(f"  u7: {len(u7_data)} bytes @ 0xB000")
            
            # Create decrypted ROM (DROM in MAME terminology)
            # This is the final 24KB output that the emulator will use
            drom = bytearray(0x6000)
            
            # Copy base Pac-Man ROMs unmodified (0x0000-0x3FFF)
            for i in range(0x1000):
                drom[0x0000 + i] = src_rom[0x0000 + i]  # pacman.6e
                drom[0x1000 + i] = src_rom[0x1000 + i]  # pacman.6f
                drom[0x2000 + i] = src_rom[0x2000 + i]  # pacman.6h
                # pacman.6j -> decrypt u7 for 0x3000-0x3FFF
                addr = bitswap12(i, 11, 3, 7, 9, 10, 8, 6, 5, 4, 2, 1, 0)
                drom[0x3000 + i] = bitswap8(src_rom[0xb000 + addr], 0, 4, 5, 7, 6, 3, 2, 1)
            
            # Decrypt u5 -> 0x4000-0x47FF (2KB)
            for i in range(0x800):
                addr = bitswap11(i, 8, 7, 5, 9, 10, 6, 3, 4, 2, 1, 0)
                drom[0x4000 + i] = bitswap8(src_rom[0x8000 + addr], 0, 4, 5, 7, 6, 3, 2, 1)
            
            # Decrypt u6 -> 0x4800-0x4FFF and 0x5000-0x57FF (two 2KB halves)
            for i in range(0x800):
                addr = bitswap11(i, 3, 7, 9, 10, 8, 6, 5, 4, 2, 1, 0)
                drom[0x4800 + i] = bitswap8(src_rom[0x9800 + addr], 0, 4, 5, 7, 6, 3, 2, 1)
                drom[0x5000 + i] = bitswap8(src_rom[0x9000 + addr], 0, 4, 5, 7, 6, 3, 2, 1)
            
            # Fill rest with mirrors as MAME does
            for i in range(0x800):
                drom[0x5800 + i] = src_rom[0x1800 + i]  # mirror of pacman.6f high
            
            print("  Decrypted u5/u6/u7")
            
            # Now apply the 8-byte patches from the decrypted area
            # MAME's mspacman_install_patches - exact addresses from MAME source
            # Our drom layout: 0x0000-0x3FFF = base code, 0x4000-0x47FF = u5 decrypted,
            #                  0x4800-0x4FFF = u6 high decrypted, 0x5000-0x57FF = u6 low decrypted
            # MAME's high bank is at 0x8000, our equivalent is at 0x4000
            # So 0x8xxx -> 0x4xxx (subtract 0x4000)
            def install_patches(rom):
                """Copy forty 8-byte patches into Pac-Man code
                   Exact port of MAME's mspacman_install_patches()
                """
                for i in range(8):
                    # From MAME: ROM[dest] = ROM[src] where src is in high bank (0x8xxx)
                    # Our drom maps 0x8xxx -> 0x4xxx
                    rom[0x0410+i] = rom[0x4008+i]   # 0x8008 -> 0x4008
                    rom[0x08E0+i] = rom[0x41D8+i]   # 0x81D8 -> 0x41D8
                    rom[0x0A30+i] = rom[0x4118+i]   # 0x8118 -> 0x4118
                    rom[0x0BD0+i] = rom[0x40D8+i]   # 0x80D8 -> 0x40D8
                    rom[0x0C20+i] = rom[0x4120+i]   # 0x8120 -> 0x4120
                    rom[0x0E58+i] = rom[0x4168+i]   # 0x8168 -> 0x4168
                    rom[0x0EA8+i] = rom[0x4198+i]   # 0x8198 -> 0x4198
                    
                    rom[0x1000+i] = rom[0x4020+i]   # 0x8020 -> 0x4020
                    rom[0x1008+i] = rom[0x4010+i]   # 0x8010 -> 0x4010
                    rom[0x1288+i] = rom[0x4098+i]   # 0x8098 -> 0x4098
                    rom[0x1348+i] = rom[0x4048+i]   # 0x8048 -> 0x4048
                    rom[0x1688+i] = rom[0x4088+i]   # 0x8088 -> 0x4088
                    rom[0x16B0+i] = rom[0x4188+i]   # 0x8188 -> 0x4188
                    rom[0x16D8+i] = rom[0x40C8+i]   # 0x80C8 -> 0x40C8
                    rom[0x16F8+i] = rom[0x41C8+i]   # 0x81C8 -> 0x41C8
                    rom[0x19A8+i] = rom[0x40A8+i]   # 0x80A8 -> 0x40A8
                    rom[0x19B8+i] = rom[0x41A8+i]   # 0x81A8 -> 0x41A8
                    
                    rom[0x2060+i] = rom[0x4148+i]   # 0x8148 -> 0x4148
                    rom[0x2108+i] = rom[0x4018+i]   # 0x8018 -> 0x4018
                    rom[0x21A0+i] = rom[0x41A0+i]   # 0x81A0 -> 0x41A0
                    rom[0x2298+i] = rom[0x41E8+i]   # 0x81E8 -> 0x41E8
                    rom[0x23E0+i] = rom[0x4038+i]   # 0x8038 -> 0x4038
                    rom[0x2418+i] = rom[0x4000+i]   # 0x8000 -> 0x4000
                    rom[0x2448+i] = rom[0x4058+i]   # 0x8058 -> 0x4058
                    rom[0x2470+i] = rom[0x4140+i]   # 0x8140 -> 0x4140
                    rom[0x2488+i] = rom[0x4080+i]   # 0x8080 -> 0x4080
                    rom[0x24B0+i] = rom[0x4180+i]   # 0x8180 -> 0x4180
                    rom[0x24D8+i] = rom[0x40C0+i]   # 0x80C0 -> 0x40C0
                    rom[0x24F8+i] = rom[0x41C0+i]   # 0x81C0 -> 0x41C0
                    rom[0x2748+i] = rom[0x4050+i]   # 0x8050 -> 0x4050
                    rom[0x2780+i] = rom[0x4090+i]   # 0x8090 -> 0x4090
                    rom[0x27B8+i] = rom[0x4190+i]   # 0x8190 -> 0x4190
                    rom[0x2800+i] = rom[0x4028+i]   # 0x8028 -> 0x4028
                    rom[0x2B20+i] = rom[0x4100+i]   # 0x8100 -> 0x4100
                    rom[0x2B30+i] = rom[0x4110+i]   # 0x8110 -> 0x4110
                    rom[0x2BF0+i] = rom[0x41D0+i]   # 0x81D0 -> 0x41D0
                    rom[0x2CC0+i] = rom[0x40D0+i]   # 0x80D0 -> 0x40D0
                    rom[0x2CD8+i] = rom[0x40E0+i]   # 0x80E0 -> 0x40E0
                    rom[0x2CF0+i] = rom[0x41E0+i]   # 0x81E0 -> 0x41E0
                    rom[0x2D60+i] = rom[0x4160+i]   # 0x8160 -> 0x4160
            
            install_patches(drom)
            print("  Applied 40 8-byte patches")
            
            rom_data = drom
            print(f"  Total ROM size: {len(rom_data)} bytes")
        else:
            print("  ERROR: No Ms. Pac-Man ROMs found (need boot1-6 or pacman.6* + u5/u6/u7)")
            return False
        
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
