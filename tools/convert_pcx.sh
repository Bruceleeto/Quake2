#!/bin/bash

# Run from ~/Downloads/Quake

BASEDIR="cd/baseq2"

next_pow2() {
    local n=$1
    local pow=8
    while [ $pow -lt $n ]; do
        pow=$((pow * 2))
    done
    echo $pow
}

echo "Converting PCX to PNG with power-of-two padding..."

find "$BASEDIR" -name "*.pcx" | while read pcx; do
    # Skip colormap.pcx - needed for wal2png
    if [[ "$pcx" == *"colormap.pcx" ]]; then
        echo "SKIP: $pcx (needed for wal2png)"
        continue
    fi
    
    dims=$(identify -format "%w %h" "$pcx" 2>/dev/null)
    w=$(echo $dims | cut -d' ' -f1)
    h=$(echo $dims | cut -d' ' -f2)
    
    new_w=$(next_pow2 $w)
    new_h=$(next_pow2 $h)
    
    png="${pcx%.pcx}.png"
    
    # Sprites need transparency, everything else gets black padding
    if [[ "$pcx" == *"/sprites/"* || "$pcx" == *"/pics/"* ]]; then
        echo "$pcx (${w}x${h}) -> $png (${new_w}x${new_h}) [SPRITE]"
        # Sprites: magenta or Q2 palette 255 (#9f5b53) as transparent, transparent padding
        magick "$pcx" -fuzz 1% -transparent "#FF00FF" -transparent "#9f5b53" \
            -background none -gravity NorthWest -extent ${new_w}x${new_h} "$png"
    else
        echo "$pcx (${w}x${h}) -> $png (${new_w}x${new_h})"
        # Textures/UI: no transparency, black padding
        magick "$pcx" -background black -gravity NorthWest -extent ${new_w}x${new_h} "$png"
    fi
    
    if [ $? -eq 0 ]; then
        # rm "$pcx"  # Keep for comparison
        echo "  OK"
    else
        echo "  FAILED: $pcx"
    fi
done

echo "Done!"