#!/usr/bin/env bash
# usage: source tools/idf-env.sh   (works from bash and zsh, any working directory)
if [ -n "${ZSH_VERSION:-}" ]; then
  _idf_env_self="${(%):-%x}"
else
  _idf_env_self="${BASH_SOURCE[0]}"
fi
_idf_env_dir="$(cd "$(dirname "$_idf_env_self")" && pwd)"
IDF_VER="$(cat "$_idf_env_dir/../.idf-version")"
export IDF_PATH="$HOME/esp/esp-idf-$IDF_VER"
[ -d "$IDF_PATH" ] || { echo "ESP-IDF $IDF_VER not found at $IDF_PATH"; return 1; }
# ESP-IDF 5.3 needs Python >= 3.10; macOS ships 3.9. Prefer Homebrew's python@3.12 (or 3.11) when present.
for py in /opt/homebrew/opt/python@3.12/libexec/bin /opt/homebrew/opt/python@3.11/libexec/bin; do
  if [ -x "$py/python3" ]; then export PATH="$py:$PATH"; break; fi
done
unset _idf_env_self _idf_env_dir
. "$IDF_PATH/export.sh" > /dev/null
echo "ESP-IDF $(idf.py --version)"
