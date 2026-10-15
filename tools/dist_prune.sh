#!/usr/bin/env bash
# tools/dist_prune.sh -- move signed images that do NOT match the current HEAD description (older
# builds, and images built on another branch) into dist/archive/ (R34). dist/ is git-ignored either way; this only keeps the directory readable.
# Usage: tools/dist_prune.sh [--dry-run]
set -euo pipefail
cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dry=0
case "${1:-}" in
    "") ;;
    --dry-run) dry=1 ;;
    *) echo "usage: tools/dist_prune.sh [--dry-run]" >&2; exit 2 ;;
esac
[ -d dist ] || { echo "no dist/ -- nothing to prune"; exit 0; }
keep="$(git describe --tags --match 'v*' --always)"
moved=0
for f in dist/*.bin dist/*.sha256; do
    [ -e "$f" ] || continue
    case "$(basename "$f")" in
        *"$keep"*) continue ;;                     # the current image stays
    esac
    if [ "$dry" = 1 ]; then
        echo "would archive $f"
    else
        mkdir -p dist/archive
        mv "$f" dist/archive/
    fi
    moved=$((moved + 1))                           # not ((moved++)): that returns 1 at 0 under set -e
done
verb=archived; [ "$dry" = 1 ] && verb="would be archived"
echo "dist_prune: $moved file(s) $verb (kept anything matching '$keep')"
