#!/usr/bin/env bash
# aab-to-apk.sh — Extract a universal APK from an EAS-produced AAB.
#
# Usage:
#   ./scripts/aab-to-apk.sh path/to/build.aab [output-dir]
#
# The universal APK will be written to <output-dir>/universal.apk.
# <output-dir> defaults to the same directory as the input AAB.
#
# If KEYSTORE_PATH / KEYSTORE_PASS / KEY_ALIAS / KEY_PASS are set in the
# environment, the APK will be signed with that keystore (required for
# sideloading). Otherwise an unsigned APK is produced.

set -euo pipefail

# ── Argument handling ─────────────────────────────────────────────────────────

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <path-to-build.aab> [output-dir]" >&2
  exit 1
fi

AAB_PATH="$(realpath "$1")"

if [[ ! -f "$AAB_PATH" ]]; then
  echo "Error: AAB file not found: $AAB_PATH" >&2
  exit 1
fi

if [[ "$AAB_PATH" != *.aab ]]; then
  echo "Warning: file does not have .aab extension — proceeding anyway."
fi

OUTPUT_DIR="${2:-$(dirname "$AAB_PATH")}"
mkdir -p "$OUTPUT_DIR"
OUTPUT_DIR="$(realpath "$OUTPUT_DIR")"

APKS_FILE="$OUTPUT_DIR/$(basename "${AAB_PATH%.aab}").apks"
APKS_EXTRACT_DIR="$OUTPUT_DIR/$(basename "${AAB_PATH%.aab}")-apks"
FINAL_APK="$OUTPUT_DIR/$(basename "${AAB_PATH%.aab}")-universal.apk"

# ── Dependency check ──────────────────────────────────────────────────────────

if ! command -v bundletool &>/dev/null; then
  echo "bundletool not found. Install it with:"
  echo "  brew install bundletool"
  exit 1
fi

echo "bundletool: $(bundletool version 2>/dev/null || echo 'unknown version')"

# ── Build APKS set ────────────────────────────────────────────────────────────

echo ""
echo "Input:  $AAB_PATH"
echo "Output: $FINAL_APK"
echo ""

BUNDLETOOL_ARGS=(
  build-apks
  --bundle="$AAB_PATH"
  --output="$APKS_FILE"
  --mode=universal
  --overwrite
)

# Append signing args if keystore env vars are provided.
if [[ -n "${KEYSTORE_PATH:-}" ]]; then
  if [[ ! -f "$KEYSTORE_PATH" ]]; then
    echo "Error: KEYSTORE_PATH set but file not found: $KEYSTORE_PATH" >&2
    exit 1
  fi
  echo "Signing with keystore: $KEYSTORE_PATH"
  BUNDLETOOL_ARGS+=(
    --ks="$KEYSTORE_PATH"
    --ks-pass="pass:${KEYSTORE_PASS:?KEYSTORE_PASS must be set when KEYSTORE_PATH is provided}"
    --ks-key-alias="${KEY_ALIAS:?KEY_ALIAS must be set when KEYSTORE_PATH is provided}"
    --key-pass="pass:${KEY_PASS:?KEY_PASS must be set when KEYSTORE_PATH is provided}"
  )
else
  echo "No KEYSTORE_PATH set — producing unsigned APK."
fi

bundletool "${BUNDLETOOL_ARGS[@]}"

# ── Extract universal APK ─────────────────────────────────────────────────────

rm -rf "$APKS_EXTRACT_DIR"
mkdir -p "$APKS_EXTRACT_DIR"
unzip -q "$APKS_FILE" -d "$APKS_EXTRACT_DIR"

if [[ ! -f "$APKS_EXTRACT_DIR/universal.apk" ]]; then
  echo "Error: universal.apk not found in extracted APKS set." >&2
  echo "Contents of $APKS_EXTRACT_DIR:"
  ls "$APKS_EXTRACT_DIR"
  exit 1
fi

cp "$APKS_EXTRACT_DIR/universal.apk" "$FINAL_APK"

# ── Cleanup ───────────────────────────────────────────────────────────────────

rm -f  "$APKS_FILE"
rm -rf "$APKS_EXTRACT_DIR"

echo ""
echo "Done: $FINAL_APK"
echo "Size: $(du -sh "$FINAL_APK" | cut -f1)"
