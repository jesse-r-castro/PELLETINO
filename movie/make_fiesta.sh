#!/bin/bash

# Defaults
INPUT_FILE="${1:-Pac_Man_Fiesta_Float_Animation.mp4}"
TARGET_SIZE_KB="${2:-3000}"   # 3MB limit (app partition 3.5MB - current app 3.2MB = 300KB headroom)
OUTPUT_HEADER="${3:-fiesta_data.h}"
TEMP_MJPEG="temp_fiesta.mjpeg"

# Configuration Constraints
MAX_DURATION=8.0    # FULL VIDEO! (was 4.5s, now complete 8s)
FPS=24              # Frames per second (smoother playback)
WIDTH=240
HEIGHT=280

echo "========================================"
echo "FIESTA MEDAL VIDEO CRUSHER"
echo "Input: $INPUT_FILE"
echo "Target Size: < ${TARGET_SIZE_KB}KB"
echo "Length: ${MAX_DURATION}s @ ${FPS}fps"
echo "========================================"

# Check if input exists
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: Input file '$INPUT_FILE' not found!"
    exit 1
fi

# The Loop: Start with high quality (low q scale) and degrade until it fits
# q:v range in ffmpeg is typically 2-31 (linear scale)
# We will start at 2 (best) and go up to 15 (good)
FOUND_FIT=false

for Q_SCALE in {2..15}; do
    echo -n "Trying Quality Factor $Q_SCALE... "
    
    # Run FFmpeg (Quietly) - Scale to fill screen, crop if needed, deinterlace
    ffmpeg -y -v error -i "$INPUT_FILE" \
    -t "$MAX_DURATION" \
    -vf "yadif=0:-1:0,scale=${WIDTH}:${HEIGHT}:force_original_aspect_ratio=increase,crop=${WIDTH}:${HEIGHT}" \
    -vcodec mjpeg \
    -q:v "$Q_SCALE" \
    -r "$FPS" \
    -pix_fmt yuvj420p \
    -an \
    "$TEMP_MJPEG"

    # Check file size
    FILE_SIZE_BYTES=$(wc -c < "$TEMP_MJPEG" | tr -d ' ')
    FILE_SIZE_KB=$((FILE_SIZE_BYTES / 1024))

    echo "${FILE_SIZE_KB}KB"

    if [ "$FILE_SIZE_KB" -le "$TARGET_SIZE_KB" ]; then
        echo "✅ SUCCESS! Found a fit at Quality $Q_SCALE ($FILE_SIZE_KB KB)"
        FOUND_FIT=true
        break
    fi
done

if [ "$FOUND_FIT" = false ]; then
    echo "❌ FAILED: Even at lowest quality (31), file is ${FILE_SIZE_KB}KB."
    echo "   Suggestion: Decrease FPS or Duration in the script."
    exit 1
fi

# ---------------------------------------------------------
# PYTHON CONVERSION STEP (Embedded inside the bash script)
# ---------------------------------------------------------
echo "converting to C Header format..."

python3 -c "
import sys

input_file = '$TEMP_MJPEG'
output_file = '$OUTPUT_HEADER'
array_name = 'fiesta_video'

try:
    with open(input_file, 'rb') as f:
        data = f.read()
    
    file_len = len(data)

    with open(output_file, 'w') as f:
        f.write('#ifndef FIESTA_DATA_H\n')
        f.write('#define FIESTA_DATA_H\n\n')
        f.write('#include <stdint.h>\n')
        f.write('#include <stddef.h>\n\n')
        f.write(f'const size_t {array_name}_size = {file_len};\n')
        f.write(f'const uint8_t {array_name}[] = {{\n')
        
        # Write hex data
        for i in range(0, len(data), 12):
            chunk = data[i:i+12]
            hex_str = ', '.join(f'0x{b:02X}' for b in chunk)
            f.write(f'  {hex_str},\n')
            
        f.write('};\n\n')
        f.write('#endif // FIESTA_DATA_H\n')

    print(f'Done! generated {output_file}')

except Exception as e:
    print(f'Error converting: {e}')
    sys.exit(1)
"

# Cleanup
rm "$TEMP_MJPEG"
echo "========================================"
echo "Ready to compile! Include '$OUTPUT_HEADER' in your project."
