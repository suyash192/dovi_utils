#!/bin/bash

PIPE_PATH="/tmp/dovi_utils_p7_p8.pipe"
INPUT_FILE="$1"
SEEK_ARG="$2"      # e.g., "-ss 00:01:00" or empty ""
AUDIO_INDEX="$3"   # e.g., "0"
SUB_INDEX="$4"     # e.g., "0"
AUDIO_CODEC="$5"   # e.g., "ac3" or "libopus"

/usr/bin/cat "$PIPE_PATH" | \
/usr/bin/hevc_to_mkv | \
/usr/lib/jellyfin-ffmpeg/ffmpeg \
    -hide_banner \
    -loglevel error \
    $SEEK_ARG \
    -f matroska -i "$INPUT_FILE" \
    -f matroska -i pipe:0 \
    -map 1:v:0 \
    -map 0:a:"$AUDIO_INDEX" \
    -map 0:s:"$SUB_INDEX" \
    -c:v copy \
    -c:a "$AUDIO_CODEC" \
    -c:s copy \
    -live 1 \
    -y \
    -f matroska pipe:1