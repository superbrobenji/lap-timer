#!/bin/sh
# strict-delivery-gates-hook
# git pre-push (spec §8 "Git hooks"; owner decisions DEC-0005, DEC-0006): the register and claim
# gates on the tree being pushed, however the push was launched (the Claude Code hook sees only a
# literal `git push` in the command text). git passes <remote> <url> as arguments (ignored) and one
# `<local ref> <local sha> <remote ref> <remote sha>` line per ref on stdin. The gates read the
# working tree, which is HEAD's, so a branch push (local ref under refs/heads/) whose local sha is
# not HEAD is refused first (FAIL pre-push.not-head): check that branch out and push from it.
# `HEAD:refs/heads/x` passes (its local sha is HEAD); tag refs and deletes (the all-zero local sha)
# pass. stdin is read to the end, then git-pre-commit.sh runs in its --pre-push mode: the paths the
# gates read must not differ from HEAD (FAIL pre-push.uncommitted), then
# `run_all.py --only register,claims`. S4 (merge-readiness) extends this script.
# Installed by tools/gates/install-hooks.sh.
set -eu
head=$(git rev-parse HEAD)
refused=0
while read -r local_ref local_sha remote_ref remote_sha; do
  case "$local_ref" in refs/heads/*) ;; *) continue ;; esac
  case "$local_sha" in *[!0]*) ;; *) continue ;; esac     # all zeros: a delete
  if [ "$local_sha" != "$head" ]; then
    echo "FAIL pre-push.not-head: $local_ref ($(printf '%.7s' "$local_sha")) is not the checked-out HEAD; check it out and push from it, so the gates see its tree" >&2
    refused=1
  fi
done
if [ "$refused" = 1 ]; then exit 1; fi
exec sh "$(dirname "$0")/git-pre-commit.sh" --pre-push </dev/null
