#!/bin/sh
# strict-delivery-gates-hook
# git pre-push (spec §8 "Git hooks"; owner decisions DEC-0005, DEC-0006): the register and claim
# gates on the tree being pushed, however the push was launched (the Claude Code hook sees only a
# literal `git push` in the command text). git passes <remote> <url> as arguments (ignored) and one
# `<local ref> <local sha> <remote ref> <remote sha>` line per ref on stdin. The gates read the
# working tree, which is HEAD's, so every pushed local sha must be HEAD (DEC-0006, read literally):
# each is peeled to a commit (an annotated tag of HEAD is HEAD; an object that is not a commit is
# refused) and a line whose commit is not HEAD is refused first (FAIL pre-push.not-head), whatever
# the ref kind: a branch, `bad~0:refs/heads/y`, `<sha>:refs/heads/z`, a tag of an older commit.
# Push it from that commit checked out. `HEAD:refs/heads/x`, a tag of HEAD and a delete (the
# all-zero local sha) pass. stdin is read to the end, then git-pre-commit.sh runs in its --pre-push
# mode: the paths the gates read must not differ from HEAD (FAIL pre-push.uncommitted), then
# `run_all.py --only register,claims`. S4 (merge-readiness) extends this script.
# Installed by tools/gates/install-hooks.sh.
set -eu
head=$(git rev-parse HEAD)
refused=0
while read -r local_ref local_sha remote_ref remote_sha; do
  case "$local_sha" in *[!0]*) ;; *) continue ;; esac     # all zeros: a delete
  commit=$(git rev-parse --verify -q "$local_sha^{commit}" 2>/dev/null || true)
  if [ "$commit" != "$head" ]; then
    echo "FAIL pre-push.not-head: $local_ref ($(printf '%.7s' "$local_sha")) is not the checked-out HEAD; check it out and push from it, so the gates see its tree" >&2
    refused=1
  fi
done
if [ "$refused" = 1 ]; then exit 1; fi
exec sh "$(dirname "$0")/git-pre-commit.sh" --pre-push </dev/null
