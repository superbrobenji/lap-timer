#!/usr/bin/env bash
# usage: source tools/idf-env.sh
IDF_VER="$(cat "$(dirname "${BASH_SOURCE[0]}")/../.idf-version")"
export IDF_PATH="$HOME/esp/esp-idf-$IDF_VER"
[ -d "$IDF_PATH" ] || { echo "ESP-IDF $IDF_VER not found at $IDF_PATH"; return 1; }
. "$IDF_PATH/export.sh" > /dev/null
echo "ESP-IDF $(idf.py --version)"
