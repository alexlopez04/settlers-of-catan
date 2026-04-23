#!/bin/bash
# =============================================================================
# generate.sh — regenerate NanoPB C sources for the board + player firmwares.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

OUTS=(
    "../firmware/board/src/proto"
    "../firmware/hub/src/proto"
)

for OUT in "${OUTS[@]}"; do
    mkdir -p "$OUT"
    cp catan.options "$OUT/"
    protoc --nanopb_out="$OUT" catan.proto
    echo "  -> $OUT"
done

echo "Proto generation complete."
