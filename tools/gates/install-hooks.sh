#!/usr/bin/env bash
# Installs the strict-delivery git hooks (spec §8 "Git hooks"). Idempotent. The installed
# pre-commit is a shim carrying the marker line below; it runs the TRACKED
# tools/gates/hooks/git-pre-commit.sh of whichever tree is being committed, so an update to the
# script needs no re-install. A foreign pre-commit is moved aside, never overwritten.
set -euo pipefail
MARKER='# strict-delivery-gates-hook'
root=$(git rev-parse --show-toplevel)
hooks=$(git -C "$root" rev-parse --git-path hooks)
case "$hooks" in /*) ;; *) hooks="$root/$hooks" ;; esac
mkdir -p "$hooks"
dst="$hooks/pre-commit"
if [ -e "$dst" ] && ! grep -qF "$MARKER" "$dst"; then
  mv "$dst" "$dst.pre-strict-delivery"
  echo "install-hooks: moved the existing pre-commit hook to $dst.pre-strict-delivery (it no longer runs)"
fi
cat > "$dst" <<'SHIM'
#!/bin/sh
# strict-delivery-gates-hook
# Installed by tools/gates/install-hooks.sh. Runs the tracked script of the tree being committed;
# a tree without it cannot commit (fail closed).
script="$(git rev-parse --show-toplevel)/tools/gates/hooks/git-pre-commit.sh"
if [ ! -f "$script" ]; then
  echo "FAIL pre-commit.missing: this tree has no tools/gates/hooks/git-pre-commit.sh (it predates the strict-delivery gates); rebase it onto main or ask the owner" >&2
  exit 1
fi
exec sh "$script" "$@"
SHIM
chmod +x "$dst"
echo "install-hooks: pre-commit -> tools/gates/hooks/git-pre-commit.sh ($dst)"
