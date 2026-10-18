#!/bin/sh
# strict-delivery-gates-hook
# git pre-commit (spec §8 "Git hooks"): the register and claim gates on the tree being committed.
# git-pre-push.sh runs this script with --pre-push for the tree being pushed (owner decision
# DEC-0005). The gates read the working tree, so every path they read must have no worktree-side
# change: no unstaged edit, no untracked or ignored file, no file removed from the index but left
# on disk. Staged-only changes pass a commit, so the tree the gates see equals the tree being
# committed for those paths; a push sends HEAD, so --pre-push also refuses staged changes and
# runs the gates with an index built from HEAD (git ls-files then lists what is pushed) (the
# FAIL ids name the hook: pre-commit.unstaged, pre-push.uncommitted). The paths: the registers,
# briefs and the state README (docs/superpowers/state), the registered reports (docs/reviews;
# gate_register opens EVERY file under it, so an ignored .md counts, and only the Finder's
# .DS_Store, which can never hold the marker line, is exempt), and the code the gates import. The
# import path: `python3 tools/gates/run_all.py` starts with sys.path[0] = tools/gates, and
# run_all.py then inserts the repository root ahead of it, so imports search the root, then
# tools/gates, then the stdlib and site-packages. An untracked module or package in either
# directory (yaml.py, yaml/__init__.py, a sourceless .pyc or an extension .so) is imported in
# place of PyYAML or the stdlib and decides the verdict. So the guard covers all of tools/gates
# (the gate code; the installed shim runs the working-tree copy of this script) and every
# root-level *.py, *.pyc, *.so and */__init__.* (a package's __init__ may be a .py, .pyc or .so).
# Exempt: tools/gates/tests and README.md (not inputs to the verdict), __pycache__ directories and
# .DS_Store (a sourceless .pyc outside __pycache__ is importable, so it is not exempt).
# A finding's evidence file may lie anywhere in the repository: the register gates it itself (it
# must be a file git tracks, else FAIL register.evidence), not this path guard.
# Installed by tools/gates/install-hooks.sh.
set -eu
# gate the tree git commits or pushes (a worktree too), not the session's project
unset CLAUDE_PROJECT_DIR
staged=0
if [ "${1-}" = "--pre-push" ]; then staged=1; fi
root=$(git rev-parse --show-toplevel)
cd "$root"
# porcelain XY: X = index vs HEAD, Y = worktree vs index; a non-blank Y (incl. ?? and !!) is
# worktree-side, and with --pre-push any line (a non-blank X too) differs from HEAD
dirty=$(git status --porcelain --untracked-files=all --ignored -- \
  docs/superpowers/state docs/reviews tools/gates ':(exclude)tools/gates/tests' ':(exclude)tools/gates/README.md' \
  ':(glob)*.py' ':(glob)*.pyc' ':(glob)*.so' ':(glob)*/__init__.*' \
  ':(exclude,glob)**/.DS_Store' ':(exclude,glob)tools/gates/**/__pycache__/**' \
  | awk -v staged="$staged" 'staged || substr($0, 2, 1) != " "')
if [ -n "$dirty" ]; then
  if [ "$staged" = 1 ]; then
    echo "FAIL pre-push.uncommitted: the gates read or run from these paths and they differ from HEAD, which is what a push sends; commit them, or revert, delete or move them:" >&2
  else
    echo "FAIL pre-commit.unstaged: the gates read or run from these paths and they differ from the index; stage them (git add <path>) to commit them, or revert, delete or move them:" >&2
  fi
  echo "$dirty" >&2
  exit 1
fi
if [ "$staged" = 1 ]; then
  # a push sends HEAD, not the index: the gates read HEAD's file list (a tracked evidence file,
  # staged files) from a temporary index built from HEAD
  tmp=$(mktemp -d "${TMPDIR:-/tmp}/strict-delivery-pre-push.XXXXXX")
  trap 'rm -rf "$tmp"' EXIT
  GIT_INDEX_FILE="$tmp/index"
  export GIT_INDEX_FILE
  git read-tree HEAD
  python3 tools/gates/run_all.py --only register,claims
  exit 0
fi
exec python3 tools/gates/run_all.py --only register,claims
