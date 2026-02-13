#!/usr/bin/env bash
set -euo pipefail

VIDEO_DIR="cd/baseq2/video"

if [ ! -d "$VIDEO_DIR" ]; then
    echo "No video directory found at $VIDEO_DIR, skipping"
    exit 0
fi

shopt -s nullglob
cin_files=("$VIDEO_DIR"/*.cin)

if [ ${#cin_files[@]} -eq 0 ]; then
    echo "No .cin files found in $VIDEO_DIR, skipping"
    exit 0
fi

for f in "${cin_files[@]}"; do
    out="${f%.cin}.mpg"
    if [ -f "$out" ]; then
        echo "Skipping $(basename "$f") — already converted"
        continue
    fi
    echo "Converting $(basename "$f") -> $(basename "$out")"
    ffmpeg -y -fflags "+genpts" -i "$f" \
        -vf "scale=320:240" -r 24 \
        -c:v mpeg1video -b:v 742k \
        -ac 1 -ar 32000 -c:a mp2 -b:a 64k \
        -f mpeg -packet_size 2048 \
        "$out" 2>/dev/null
done

echo "Video conversion complete"