#!/bin/sh
# strict-delivery-gates-hook
# git pre-commit (spec §8 "Git hooks"): the register and claim gates on the tree being committed.
# The gates read the working tree, so the registers must have no unstaged edits -- then what is
# checked is exactly what is committed. Installed by tools/gates/install-hooks.sh.
set -eu
unset CLAUDE_PROJECT_DIR   # gate the tree being committed (e.g. a worktree), not the session's project
root=$(git rev-parse --show-toplevel)
cd "$root"
if ! git diff --quiet -- docs/superpowers/state; then
  echo "FAIL pre-commit.unstaged: docs/superpowers/state has unstaged edits; stage them (git add docs/superpowers/state) or revert them" >&2
  exit 1
fi
exec python3 tools/gates/run_all.py --only register,claims
