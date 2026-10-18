#!/bin/sh
# strict-delivery-gates-hook
# git pre-commit (spec §8 "Git hooks"): the register and claim gates on the tree being committed.
# The gates read the working tree, so every path they read must have no worktree-side change: no
# unstaged edit, no untracked or ignored file, no file removed from the index but left on disk.
# Staged-only changes pass, so the tree the gates see equals the tree being committed for those
# paths. The paths: the registers, briefs and the state README (docs/superpowers/state), the
# registered reports (docs/reviews) and the gate config (tools/gates/config.yml).
# Installed by tools/gates/install-hooks.sh.
set -eu
unset CLAUDE_PROJECT_DIR   # gate the tree being committed (e.g. a worktree), not the session's project
root=$(git rev-parse --show-toplevel)
cd "$root"
# porcelain XY: X = index vs HEAD, Y = worktree vs index; a non-blank Y (incl. ?? and !!) is worktree-side
dirty=$(git status --porcelain --untracked-files=all --ignored -- \
  docs/superpowers/state docs/reviews tools/gates/config.yml | awk 'substr($0, 2, 1) != " "')
if [ -n "$dirty" ]; then
  echo "FAIL pre-commit.unstaged: the gates read these paths and they differ from the index (stage them with git add, or revert or move them):" >&2
  echo "$dirty" >&2
  exit 1
fi
exec python3 tools/gates/run_all.py --only register,claims
