#!/usr/bin/env bash
# Build the AMEN MIDI Teensy 4.1 firmware with arduino-cli.
# The portable core is copied into the sketch (teensy/amen_midi/src) with
# relative includes by scripts/prepare_sketch.py, because arduino-cli's
# library-detection pass does not receive custom -I flags.
# Requires: arduino-cli + teensy:avr platform (core list check below).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$ROOT/teensy/amen_midi"
OUT="$ROOT/teensy_build"

arduino-cli core update-index >/dev/null
arduino-cli core install teensy:avr >/dev/null 2>&1 || true
arduino-cli lib update-index >/dev/null
arduino-cli lib install "Adafruit SSD1306" "Adafruit GFX Library" >/dev/null

python3 "$ROOT/scripts/prepare_sketch.py" "$ROOT/src" "$SKETCH/src" "$SKETCH"

arduino-cli compile \
  --fqbn "teensy:avr:teensy41:usb=serialmidi" \
  --output-dir "$OUT" \
  "$SKETCH"

echo "=== hex ==="
ls -la "$OUT"/*.hex
echo "Flash with: arduino-cli upload -p <PORT> --fqbn teensy:avr:teensy41:usb=serialmidi $SKETCH"
