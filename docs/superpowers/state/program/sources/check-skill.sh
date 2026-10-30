#!/usr/bin/env bash
# Acceptance check for the strict-delivery skill skeleton (S1 Task 9).
set -u
D="$HOME/.claude/skills/strict-delivery"
bad=0
need() { grep -qF -- "$2" "$1" || { echo "MISSING in $(basename "$1"): $2"; bad=1; }; }
[ -f "$D/SKILL.md" ] || { echo "MISSING $D/SKILL.md"; exit 1; }
head -4 "$D/SKILL.md" | grep -q '^name: strict-delivery$' || { echo "SKILL.md: bad frontmatter name"; bad=1; }
for s in "## When it applies" "## Precedence" "## Roles and models" "## Decision classes" "## Verification method" "## Pipeline" "## Judgment, not gated"; do need "$D/SKILL.md" "$s"; done
for r in controller implementer reviewer verifier exception-reviewer investigator audit-reviewer audit-verifier; do
  f="$D/prompts/$r.md"
  [ -f "$f" ] || { echo "MISSING $f"; bad=1; continue; }
  n=$(wc -c < "$f"); [ "$n" -le 5500 ] || { echo "$r.md is $n bytes (> 5500)"; bad=1; }
  [ "$(tail -c 1 "$f" | od -An -c | tr -d ' ')" = '\n' ] || { echo "$r.md: no trailing newline"; bad=1; }
  for p in "Verification method." "by contradiction" "Zero bugs." "Owner-only decisions." "NEEDS_DECISION" "Budget." "Background work." "Report contract." 'first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`'; do need "$f" "$p"; done
  if [ "$r" = controller ]; then need "$f" "You are the only role that dispatches."; else need "$f" "No subagents."; fi
  case "$r" in reviewer|verifier)
    for p in "Execute, never trust." "Re-run every gate" "Re-run each mutation transcript" "Sibling check" "Contradiction pass on the new surface"; do need "$f" "$p"; done ;;
  esac
done
[ "$bad" = 0 ] && echo "skill skeleton OK"
exit "$bad"
