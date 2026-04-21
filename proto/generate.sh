#!/bin/bash
# =============================================================================
# generate.sh — regenerates NanoPB C sources for all three firmware targets.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")"

OUTS=(
    "../firmware/board/src/proto"
    "../firmware/bridge/src/proto"
    "../firmware/player/src/proto"
)

for OUT in "${OUTS[@]}"; do
    mkdir -p "$OUT"
    protoc --nanopb_out="$OUT" catan.proto
    cp catan.options "$OUT/"
    echo "  -> $OUT"
done

echo "Proto generation complete."
