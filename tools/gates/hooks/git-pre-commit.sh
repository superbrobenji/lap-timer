#!/bin/sh
# strict-delivery-gates-hook
# git pre-commit (spec §8 "Git hooks"): the register and claim gates on the tree being committed.
# The gates read the working tree, so every path they read must have no worktree-side change: no
# unstaged edit, no untracked or ignored file, no file removed from the index but left on disk.
# Staged-only changes pass, so the tree the gates see equals the tree being committed for those
# paths. The paths: the registers, briefs and the state README (docs/superpowers/state), the
# registered reports (docs/reviews; gate_register opens EVERY file under it, so an ignored .md
# counts, and only the Finder's .DS_Store, which can never hold the marker line, is exempt), and the
# code the gates import. The import path: `python3 tools/gates/run_all.py` starts with
# sys.path[0] = tools/gates, and run_all.py then inserts the repository root ahead of it, so imports
# search the root, then tools/gates, then the stdlib and site-packages. An untracked module or
# package in either directory (yaml.py, yaml/__init__.py, a sourceless .pyc or an extension .so) is
# imported in place of PyYAML or the stdlib and decides the verdict. So the guard covers all of
# tools/gates (the gate code; the installed shim runs the working-tree copy of this script) and every
# root-level *.py, *.pyc, *.so and */__init__.* (a package's __init__ may be a .py, .pyc or .so).
# Exempt: tools/gates/tests and README.md (not inputs to the verdict), __pycache__ directories and
# .DS_Store (a sourceless .pyc outside __pycache__ is importable, so it is not exempt).
# Installed by tools/gates/install-hooks.sh.
set -eu
unset CLAUDE_PROJECT_DIR   # gate the tree being committed (e.g. a worktree), not the session's project
root=$(git rev-parse --show-toplevel)
cd "$root"
# porcelain XY: X = index vs HEAD, Y = worktree vs index; a non-blank Y (incl. ?? and !!) is worktree-side
dirty=$(git status --porcelain --untracked-files=all --ignored -- \
  docs/superpowers/state docs/reviews tools/gates ':(exclude)tools/gates/tests' ':(exclude)tools/gates/README.md' \
  ':(glob)*.py' ':(glob)*.pyc' ':(glob)*.so' ':(glob)*/__init__.*' \
  ':(exclude,glob)**/.DS_Store' ':(exclude,glob)tools/gates/**/__pycache__/**' \
  | awk 'substr($0, 2, 1) != " "')
if [ -n "$dirty" ]; then
  echo "FAIL pre-commit.unstaged: the gates read or run from these paths and they differ from the index; stage them (git add <path>) to commit them, or revert, delete or move them:" >&2
  echo "$dirty" >&2
  exit 1
fi
exec python3 tools/gates/run_all.py --only register,claims
