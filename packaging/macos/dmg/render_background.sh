#!/bin/sh
# Renders background.html to background.png (1x) and background@2x.png (2x) with headless Chrome.
set -eu
cd "$(dirname "$0")"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
profile=$(mktemp -d)
trap 'rm -rf "$profile"' EXIT HUP INT TERM
for scale in 1 2; do
  if [ "$scale" = 1 ]; then out=background.png; else out="background@2x.png"; fi
  "$CHROME" --headless --disable-gpu --hide-scrollbars --user-data-dir="$profile" \
    --force-device-scale-factor="$scale" --window-size=760,600 \
    --virtual-time-budget=5000 --screenshot="$PWD/$out" "file://$PWD/background.html"
done
