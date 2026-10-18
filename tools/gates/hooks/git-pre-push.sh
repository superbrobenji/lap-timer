#!/bin/sh
# strict-delivery-gates-hook
# git pre-push (spec §8 "Git hooks"; owner decision DEC-0005): the register and claim gates on the
# tree being pushed, however the push was launched (the Claude Code hook sees only a literal
# `git push` in the command text). The check is git-pre-commit.sh's, in its --pre-push mode: the
# paths the gates read must not differ from HEAD (FAIL pre-push.uncommitted), then
# `run_all.py --only register,claims`. git passes <remote> <url> as arguments and one line per ref on
# stdin; both are ignored at S1 block b3, and nothing here reads stdin, so S4 (merge-readiness) can
# read the ref lines in a loop before the exec. The gates read the working tree, which then equals
# HEAD for the paths they read; a push of a ref other than HEAD is checked against HEAD.
# Installed by tools/gates/install-hooks.sh.
set -eu
exec sh "$(dirname "$0")/git-pre-commit.sh" --pre-push
