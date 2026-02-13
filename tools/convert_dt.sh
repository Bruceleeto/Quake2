#!/bin/bash

# PNG to DT converter for Quake 2 textures
# Run from ~/Downloads/Quake

PVRTEX="/opt/toolchains/dc/kos/utils/pvrtex/pvrtex"
BASEDIR="cd/baseq2"

echo "Converting PNG to DT..."

find "$BASEDIR" -type f -iname "*.png" | while read png; do
    dt="${png%.png}.dt"
    
    echo "Converting: $png -> $dt"
    
    "$PVRTEX" -i "$png" -o "$dt" -f AUTO -c
    
    if [ $? -eq 0 ]; then
        rm "$png"
    else
        echo "  FAILED: $png"
    fi
done

echo "Done!"