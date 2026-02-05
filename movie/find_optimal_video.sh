#!/bin/bash

# Find optimal video encoding parameters for PELLETINO
# Constraints:
# - 4MB total flash
# - App partition: 3.5MB (0x370000 bytes = 3,604,480 bytes)
# - Video embedded in .rodata section
# - Original video: 8 seconds, 192 frames @ 24fps
# - Current: 4.5s @ quality 4, ~800KB

INPUT_FILE="Pac_Man_Fiesta_Float_Animation.mp4"
TEMP_MJPEG="test_optimal.mjpeg"
WIDTH=240
HEIGHT=280
FPS=24

echo "=========================================="
echo "PELLETINO VIDEO OPTIMIZER"
echo "=========================================="
echo "Flash: 4MB | App Partition: 3.5MB"
echo "Original video: 8.0 seconds"
echo "Current config: 4.5s @ quality 4 (~800KB)"
echo ""

# Check original video
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: $INPUT_FILE not found!"
    exit 1
fi

echo "Original video info:"
ffprobe -v error -show_entries format=duration,size -of default=noprint_wrappers=1:nokey=1 "$INPUT_FILE" | head -2

echo ""
echo "=========================================="
echo "STRATEGY: Find max video size that fits"
echo "=========================================="
echo ""

# App partition is 3.5MB. Let's leave headroom for code growth.
# Current build size unknown, but let's test several size targets:
# Conservative: 2.5MB (leaves 1MB for code)
# Moderate: 3.0MB (leaves 500KB for code)
# Aggressive: 3.2MB (leaves 300KB for code)

SIZE_TARGETS=(2500 2800 3000 3200)
DURATIONS=(5.0 5.5 6.0 6.5 7.0 7.5 8.0)

echo "Testing combinations of duration vs quality..."
echo ""

RESULTS_FILE="optimization_results.txt"
echo "Duration,Quality,Size_KB,Fits_Target,Note" > "$RESULTS_FILE"

for DURATION in "${DURATIONS[@]}"; do
    echo "Testing ${DURATION}s duration..."
    
    for Q_SCALE in {2..12}; do
        # Generate video
        ffmpeg -y -v error -i "$INPUT_FILE" \
            -t "$DURATION" \
            -vf "yadif=0:-1:0,scale=${WIDTH}:${HEIGHT}:force_original_aspect_ratio=increase,crop=${WIDTH}:${HEIGHT}" \
            -vcodec mjpeg \
            -q:v "$Q_SCALE" \
            -r "$FPS" \
            -pix_fmt yuvj420p \
            -an \
            "$TEMP_MJPEG" 2>&1 | grep -v "deprecated"
        
        # Get size
        FILE_SIZE_BYTES=$(wc -c < "$TEMP_MJPEG" | tr -d ' ')
        FILE_SIZE_KB=$((FILE_SIZE_BYTES / 1024))
        
        # Check against each target
        for TARGET in "${SIZE_TARGETS[@]}"; do
            if [ "$FILE_SIZE_KB" -le "$TARGET" ]; then
                FITS="YES"
                break
            else
                FITS="NO"
            fi
        done
        
        # Save result
        NOTE=""
        if [ "$FILE_SIZE_KB" -le 2500 ]; then
            NOTE="CONSERVATIVE_FIT"
        elif [ "$FILE_SIZE_KB" -le 2800 ]; then
            NOTE="MODERATE_FIT"
        elif [ "$FILE_SIZE_KB" -le 3000 ]; then
            NOTE="GOOD_FIT"
        elif [ "$FILE_SIZE_KB" -le 3200 ]; then
            NOTE="AGGRESSIVE_FIT"
        else
            NOTE="TOO_LARGE"
        fi
        
        echo "$DURATION,$Q_SCALE,$FILE_SIZE_KB,$FITS,$NOTE" >> "$RESULTS_FILE"
        
        # Print progress
        printf "  Q=%2d: %4dKB [%s]\n" "$Q_SCALE" "$FILE_SIZE_KB" "$NOTE"
        
        # If we found a good fit, we can stop searching higher quality for this duration
        if [ "$NOTE" = "CONSERVATIVE_FIT" ] || [ "$NOTE" = "MODERATE_FIT" ]; then
            break
        fi
    done
    echo ""
done

# Cleanup
rm -f "$TEMP_MJPEG"

echo "=========================================="
echo "ANALYSIS COMPLETE"
echo "=========================================="
echo ""
echo "Results saved to: $RESULTS_FILE"
echo ""

# Find the best options
echo "RECOMMENDATIONS:"
echo ""

echo "1. MAXIMUM LENGTH (Full 8s video):"
grep "^8.0," "$RESULTS_FILE" | grep "CONSERVATIVE_FIT\|MODERATE_FIT\|GOOD_FIT" | head -1 | \
    awk -F',' '{printf "   Duration: %ss | Quality: %s | Size: %sKB | Status: %s\n", $1, $2, $3, $5}'

echo ""
echo "2. BEST QUALITY (within 2.5MB conservative limit):"
grep "CONSERVATIVE_FIT" "$RESULTS_FILE" | sort -t',' -k2,2n | head -1 | \
    awk -F',' '{printf "   Duration: %ss | Quality: %s | Size: %sKB | Status: %s\n", $1, $2, $3, $5}'

echo ""
echo "3. BALANCED (longest duration + decent quality, <2.8MB):"
grep "MODERATE_FIT\|CONSERVATIVE_FIT" "$RESULTS_FILE" | sort -t',' -k1,1nr -k2,2n | head -1 | \
    awk -F',' '{printf "   Duration: %ss | Quality: %s | Size: %sKB | Status: %s\n", $1, $2, $3, $5}'

echo ""
echo "Full results table:"
echo "Duration | Quality | Size    | Status"
echo "---------|---------|---------|---------------"
cat "$RESULTS_FILE" | grep -v "TOO_LARGE" | tail -20 | \
    awk -F',' '{printf "%-8s | Q=%-5s | %4sKB | %s\n", $1"s", $2, $3, $5}'

echo ""
echo "=========================================="
