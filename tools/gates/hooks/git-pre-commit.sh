#!/bin/sh
# strict-delivery-gates-hook
# git pre-commit (spec §8 "Git hooks"): the register and claim gates on the tree being committed.
# The gates read the working tree, so every path they read must have no worktree-side change: no
# unstaged edit, no untracked or ignored file, no file removed from the index but left on disk.
# Staged-only changes pass, so the tree the gates see equals the tree being committed for those
# paths. The paths: the registers, briefs and the state README (docs/superpowers/state), the
# registered reports (docs/reviews; gate_register opens EVERY file under it, so an ignored .md
# counts, and only the Finder's .DS_Store, which can never hold the marker line, is exempt), and the
# files whose working-tree content decides the verdict: run_all.py, the gate_*.py modules, __init__.py,
# lib/, config.yml and this script (the installed shim runs the working-tree copy). Python bytecode
# caches are exempt; tools/gates/tests and README.md are not inputs to the verdict.
# Installed by tools/gates/install-hooks.sh.
set -eu
unset CLAUDE_PROJECT_DIR   # gate the tree being committed (e.g. a worktree), not the session's project
root=$(git rev-parse --show-toplevel)
cd "$root"
# porcelain XY: X = index vs HEAD, Y = worktree vs index; a non-blank Y (incl. ?? and !!) is worktree-side
dirty=$(git status --porcelain --untracked-files=all --ignored -- \
  docs/superpowers/state docs/reviews tools/gates/run_all.py tools/gates/__init__.py 'tools/gates/gate_*.py' \
  tools/gates/lib tools/gates/config.yml tools/gates/hooks/git-pre-commit.sh \
  ':(exclude,glob)**/.DS_Store' ':(exclude,glob)tools/gates/**/__pycache__/**' | awk 'substr($0, 2, 1) != " "')
if [ -n "$dirty" ]; then
  echo "FAIL pre-commit.unstaged: the gates read or run from these paths and they differ from the index; stage them (git add <path>) to commit them, or revert, delete or move them:" >&2
  echo "$dirty" >&2
  exit 1
fi
exec python3 tools/gates/run_all.py --only register,claims
