#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

BOARD_OUT="../firmware/board/src/proto"
PLAYER_OUT="../firmware/player/src/proto"

mkdir -p "$BOARD_OUT" "$PLAYER_OUT"

# Generate NanoPB C code for both targets
for OUT in "$BOARD_OUT" "$PLAYER_OUT"; do
    protoc --nanopb_out="$OUT" catan.proto
    cp catan.options "$OUT/"
    echo "  → $OUT"
done

echo "Proto generation complete."
