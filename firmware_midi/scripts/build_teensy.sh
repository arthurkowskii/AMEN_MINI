#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$ROOT/teensy/amen_midi"
OUT="$ROOT/teensy_build"

arduino-cli compile \
  --fqbn "teensy:avr:teensy41:usb=serialmidi" \
  --warnings all \
  --output-dir "$OUT" \
  "$SKETCH"

echo "=== hex ==="
ls -la "$OUT"/*.hex
echo "Flash with: arduino-cli upload -p <PORT> --fqbn teensy:avr:teensy41:usb=serialmidi $SKETCH"
