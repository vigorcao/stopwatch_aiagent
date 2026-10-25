#!/bin/zsh
# shot.sh name "query" [w h]
W=${0:A:h}; G="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"; mkdir -p $W/shots
"$G" --headless=new --hide-scrollbars --user-data-dir="$(mktemp -d)" --force-device-scale-factor=1.5 --window-size=${3:-1000},${4:-900} --virtual-time-budget=2500 --screenshot="$W/shots/$1.png" "file://$W/stopwatch-pet.html?$2" 2>/dev/null
"$G" --headless=new --user-data-dir="$(mktemp -d)" --virtual-time-budget=2500 --dump-dom "file://$W/stopwatch-pet.html?$2" 2>/dev/null | grep -o 'data-errors="[^"]*"' | head -2
