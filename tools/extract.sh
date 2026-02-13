#!/bin/bash

# Run this from ~/Downloads/Quake

PAKDIR="cd/baseq2"
OUTDIR="cd/baseq2"
TOOL="tools/pakextract"

mkdir -p "$OUTDIR"

for pak in "$PAKDIR"/*.pak; do
    echo "Extracting: $pak"
    "$TOOL" -o "$OUTDIR" "$pak"
done

echo "Removing pak files..."
rm "$PAKDIR"/*.pak

echo "Done! Files extracted to $OUTDIR"