#!/bin/bash

# Check if required arguments are provided
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <input_path> <output_pipe>"
    exit 1
fi

INPUT_PATH="$1"
OUTPUT_PIPE="/tmp/dovi_utils_p7_p8.pipe"

# Execute the chained process
/usr/lib/jellyfin-ffmpeg/ffmpeg \
    -hide_banner \
    -loglevel error \
    -i "$INPUT_PATH" \
    -c:v copy \
    -bsf:v hevc_mp4toannexb \
    -f hevc - | \
/usr/bin/dovi_tool \
    --start-code annex-b \
    --drop-hdr10plus \
    -m 2 \
    convert \
    --discard - \
    -o "$OUTPUT_PIPE"