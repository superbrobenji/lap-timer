#!/usr/bin/env bash
# Installs the strict-delivery git hooks (spec §8 "Git hooks"; pre-push from S1 block b3, owner
# decision DEC-0005). Idempotent. Each installed hook (pre-commit, pre-push) is a shim carrying the
# marker line below; it runs the TRACKED tools/gates/hooks/git-<hook>.sh of whichever tree git runs
# in, so an update to the scripts needs no re-install. A foreign hook is moved aside, never
# overwritten; a symlink is foreign too (the link itself is moved, so nothing is written through
# it); every hook is checked before any is written, so a refusal installs nothing.
set -euo pipefail
MARKER='# strict-delivery-gates-hook'
HOOKS=(pre-commit pre-push)
root=$(git rev-parse --show-toplevel)
hooks=$(git -C "$root" rev-parse --git-path hooks)
case "$hooks" in /*) ;; *) hooks="$root/$hooks" ;; esac
common=$(cd "$root" && cd "$(git rev-parse --git-common-dir)" && pwd -P)
if [ -d "$hooks" ]; then real=$(cd "$hooks" && pwd -P); else real="$hooks"; fi
case "$real" in
  "$common"/*) ;;
  *) echo "install-hooks: refusing: the hooks directory $hooks (core.hooksPath?) is outside $common; it may be shared with other repositories" >&2; exit 1 ;;
esac
foreign() { [ -L "$1" ] || { [ -e "$1" ] && ! grep -qF "$MARKER" "$1"; }; }
exists() { [ -e "$1" ] || [ -L "$1" ]; }    # a dangling symlink exists too
for hook in "${HOOKS[@]}"; do
  dst="$hooks/$hook"
  if foreign "$dst" && exists "$dst.pre-strict-delivery"; then
    echo "install-hooks: refusing: $dst.pre-strict-delivery already exists and $dst is not ours; merge or remove one of them" >&2
    exit 1
  fi
done
mkdir -p "$hooks"
for hook in "${HOOKS[@]}"; do
  dst="$hooks/$hook"
  if foreign "$dst"; then
    mv "$dst" "$dst.pre-strict-delivery"
    echo "install-hooks: moved the existing $hook hook to $dst.pre-strict-delivery (it no longer runs)"
  fi
  sed "s/@HOOK@/$hook/g" > "$dst" <<'SHIM'
#!/bin/sh
# strict-delivery-gates-hook
# Installed by tools/gates/install-hooks.sh. Runs the tracked script of the tree git runs in;
# a tree without it is refused (fail closed).
script="$(git rev-parse --show-toplevel)/tools/gates/hooks/git-@HOOK@.sh"
if [ ! -f "$script" ]; then
  echo "FAIL @HOOK@.missing: this tree has no tools/gates/hooks/git-@HOOK@.sh (it predates the strict-delivery gates); rebase it onto main or ask the owner" >&2
  exit 1
fi
exec sh "$script" "$@"
SHIM
  chmod +x "$dst"
  echo "install-hooks: $hook -> tools/gates/hooks/git-$hook.sh ($dst)"
done
