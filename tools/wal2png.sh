#!/bin/bash

# Run this from ~/Downloads/Quake

WALDIR="cd/baseq2"
TOOL="$(pwd)/tools/wal2png-master/wal2png"

next_pow2() {
    local n=$1
    local pow=8
    while [ $pow -lt $n ]; do
        pow=$((pow * 2))
    done
    echo $pow
}

# Need to run from where colormap.pcx is
cd "$WALDIR"

if [ ! -f "pics/colormap.pcx" ] && [ ! -f "colormap.pcx" ]; then
    echo "ERROR: colormap.pcx not found!"
    echo "Looking for it..."
    find . -name "colormap.pcx"
    exit 1
fi

# Copy colormap.pcx to current dir if it's in pics/
[ -f "pics/colormap.pcx" ] && cp pics/colormap.pcx .

echo "Converting WAL to PNG with power-of-two padding..."

find . -name "*.wal" | while read wal; do
    "$TOOL" "$wal"
    
    png="${wal%.wal}.png"
    
    if [ -f "$png" ]; then
        dims=$(identify -format "%w %h" "$png" 2>/dev/null)
        w=$(echo $dims | cut -d' ' -f1)
        h=$(echo $dims | cut -d' ' -f2)
        
        new_w=$(next_pow2 $w)
        new_h=$(next_pow2 $h)
        
        echo "$wal (${w}x${h}) -> $png (${new_w}x${new_h})"
        
        magick "$png" -background black -gravity NorthWest -extent ${new_w}x${new_h} "$png"
    else
        echo "FAILED: $wal"
    fi
done

echo "Removing WAL files..."
find . -name "*.wal" -delete

echo "Done!"