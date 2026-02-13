#!/bin/bash

# Convert all audio to DCA format for Dreamcast
# Run from ~/Downloads/Quake

DCACONV="$(pwd)/tools/dcaconv"
BASEDIR="cd/baseq2"

echo "Converting audio to DCA format..."

# Convert WAV files (sound effects)
find "$BASEDIR/sound" -name "*.wav" -o -name "*.WAV" | while read wav; do
    dir=$(dirname "$wav")
    base=$(basename "${wav%.*}")
    dca="$dir/$(echo "$base" | tr 'A-Z' 'a-z').dca"
    
    echo "SFX: $wav -> $dca"
    
    "$DCACONV" -i "$wav" -o "$dca" --format PCM16
    
    if [ -f "$dca" ]; then
        rm "$wav"
    else
        echo "  FAILED: $wav"
    fi
done

# Convert OGG files (music - streaming)
find "$BASEDIR/music" -name "*.ogg" | while read ogg; do
    dca="${ogg%.ogg}.dca"
    
    echo "MUSIC: $ogg"
    
    "$DCACONV" -i "$ogg" -o "$dca" --format PCM16 --long
    
    if [ -f "$dca" ]; then
        rm "$ogg"
    else
        echo "  FAILED: $ogg"
    fi
done

echo "Done!"