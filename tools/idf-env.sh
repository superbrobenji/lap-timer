#!/usr/bin/env bash
# usage: source tools/idf-env.sh   (bash or zsh, any working directory; do not execute)
# Refuse direct execution in bash (return fails outside a sourced file) OR zsh (ZSH_EVAL_CONTEXT ends
# in :file only when the file is sourced; `zsh idf-env.sh` runs it as :toplevel, where zsh's top-level
# `return` succeeds and would slip past a bash-style guard -- issue #6).
if [ -n "${ZSH_VERSION:-}" ]; then
  case "${ZSH_EVAL_CONTEXT:-}" in
    *:file) ;;
    *) echo "source this file: source tools/idf-env.sh" >&2; return 1 2>/dev/null || exit 1 ;;
  esac
else
  (return 0 2>/dev/null) || { echo "source this file: source tools/idf-env.sh" >&2; exit 1; }
fi
if [ -n "${ZSH_VERSION:-}" ]; then
  _idf_env_self="${(%):-%x}"
else
  _idf_env_self="${BASH_SOURCE[0]}"
fi
_idf_env_dir="$(cd "$(dirname "$_idf_env_self")" && pwd)"
_idf_env_ver="$(cat "$_idf_env_dir/../.idf-version")"
export IDF_PATH="$HOME/esp/esp-idf-$_idf_env_ver"
[ -d "$IDF_PATH" ] || { echo "ESP-IDF $_idf_env_ver not found at $IDF_PATH"; unset _idf_env_self _idf_env_dir _idf_env_ver; return 1; }
# ESP-IDF 5.3 needs Python >= 3.10; macOS ships 3.9. Prefer Homebrew's python@3.12 (or 3.11) when present.
for _idf_env_py in /opt/homebrew/opt/python@3.12/libexec/bin /opt/homebrew/opt/python@3.11/libexec/bin; do
  if [ -x "$_idf_env_py/python3" ]; then export PATH="$_idf_env_py:$PATH"; break; fi
done
if ! python3 -c 'import sys; sys.exit(0 if sys.version_info >= (3, 10) else 1)'; then
  echo "ESP-IDF $_idf_env_ver needs Python >= 3.10; found $(python3 --version 2>&1). Install with: brew install python@3.12"
  unset _idf_env_self _idf_env_dir _idf_env_ver _idf_env_py; return 1
fi
if ! _idf_env_out="$(. "$IDF_PATH/export.sh" 2>&1)"; then
  echo "$_idf_env_out"; echo "ESP-IDF export.sh failed"
  unset _idf_env_self _idf_env_dir _idf_env_ver _idf_env_py _idf_env_out; return 1
fi
. "$IDF_PATH/export.sh" > /dev/null 2>&1
unset _idf_env_self _idf_env_dir _idf_env_ver _idf_env_py _idf_env_out
idf.py --version
