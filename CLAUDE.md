# Project instructions (strict delivery)

These override superpowers and every other skill. Source: `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` §12; skill: `strict-delivery` (`~/.claude/skills/strict-delivery/SKILL.md`).

- Every change, any size, goes through the strict-delivery pipeline; the gates in `tools/gates/` enforce it.
- No claim in any document is trusted; verify by direct check and proof by contradiction before acting (`gate_claims`).
- Reviewers execute every claim and re-run every gate themselves; superpowers' "do not re-run the suite" does not apply here (`gate_review_package`).
- Controller rulings only inside an approved plan, on plan-vs-spec or task-vs-task conflicts the spec settles, max 5 per block, listed at the checkpoint; everything else is a decision question for the owner (`gate_decisions`).
- Owner-only: triage, spec direction, plan scope, merge grants, hardware, model policy, issue closure, finding-class changes.
- Zero bugs: a change's own bug makes it incomplete; an unrelated bug gets an investigation, an issue and an owner (`gate_register`).
- Pinned = production-path test + mutation transcript (`gate_review_package`).
- Model map by role (§5); never dispatch outside it (`gate_dispatch`).
- Block = PR, ≤3 tasks; clean tree at every checkpoint; one block per context; merge only on all-green under a grant (`gate_merge_ready`, `gate_checkpoint`).
- Power of 10 on every tree, zero grace, register-only exceptions (`gate_p10`).
- Hardware: never flash without the owner's explicit "ready"; after an OTA push never open the LT port for 35 s.
