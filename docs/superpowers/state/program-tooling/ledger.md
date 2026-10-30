plan: docs/superpowers/plans/2026-10-29-program-tooling.md

# Program tooling ledger (block b1, branch block/program-tooling, base 39a3e8a, estimate 2 days, started 2026-10-30)

- block start: rows PROG-T1..PROG-T3 registered planned; run_all at 39a3e8a rc 0, 239 PASS, 0 FAIL
- Task 1 (PROG-T1): 96fbb73 build; 45f5349 transcript format; 184e12b fix round 1 (16,000-byte token-safe cap, path whitelist, output boundary); cf824cd fix round 2 (isprintable whitelist, measurement text); dd1c382 fix round 3 (pinned checks, escaped FAIL lines, malformed structure fails closed, stale list removed). D34 green at each.
- Rulings: (1) a subagent's read-list kind is its dispatch's `## Context: <kind>`, mapped to agent_id (plan :5, :82, :85); (2) receipt coverage is the union of Read ranges at the current blob (playbook :49).
- Round-3 re-review: F1-F4 addressed; new F5 (FAIL output unbounded vs the 16 KB rule) and F6 (PermissionError on expected-list removal not failing closed). D30 trigger met three rounds running; DEC-0145 lifts D30 rounds for developer tooling; F5 needs a rule the plan lacks (D25). HALTED on owner questions DEC-0171 (how Task 1 proceeds) and DEC-0172 (F5 rule).
- Asked for the owner: DEC-0166..0170 (growing ranges, stale sync ref, plan in the read list, PR template owner, path field markers). Registered PROG-U1 (issue #150, unrelated).
- Conduct incidents: round-1 re-review ran `git -c user.name/-c user.email` once in a scratch repo (dispatch said no git config overrides); round-2 re-review wrote one scratch copy to /tmp/x (deleted). Both self-reported.
- Resume: after the owner answers DEC-0171 and DEC-0172, dispatch round 4 per the answer (rounds 4-5: fresh agent one tier up, opus); review workspace .superpowers/sdd/2026-10-29-program-tooling/ (progress.md, task-1-report.md, task-1-rereview-3.md); package .superpowers/sdd/program-tooling/review/T1/.
