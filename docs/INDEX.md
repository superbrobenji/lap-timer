# Document index

Every tracked `*.md` file on its current path (DEC-0146): its purpose and its owning stream. The purpose is the file's own first heading (a file whose first heading is a section name, or that has none, shows that heading and its first line, cut at 170 characters). The owning stream follows the ownership table in `docs/superpowers/playbooks/two-streams.md`; `unassigned` marks a path neither row names (the owner assigns it at the next sync, DEC-0157). Stream A owns this file; every later change goes through the pipeline.

Completeness check (prints nothing when the index lists every tracked document and no other path):

```
comm -3 <(git ls-files '*.md' | sort) <(grep -o '^| `[^`]*`' docs/INDEX.md | sed 's/^| `//; s/`$//' | sort)
```

| path | purpose | stream |
|---|---|---|
| `.github/PULL_REQUEST_TEMPLATE.md` | Session -- Roadmap session: <!-- e.g. 1.2 --> | A |
| `CLAUDE.md` | Project instructions (strict delivery) | A |
| `README.md` | lap-timer | B |
| `docs/INDEX.md` | Document index | A |
| `docs/bench/next-bench-day.md` | Next bench day — checklist (opened 2026-10-05) | B |
| `docs/display-glyphs.md` | Display glyphs — what each icon in the status strip means | B |
| `docs/hardware/bom.md` | Prototype bill of materials | B |
| `docs/hardware/devkit-port.md` | Dev-kit port — hardware contract | B |
| `docs/measurements.md` | Measurements | B |
| `docs/pitch.md` | Trackday Lap Timer | B |
| `docs/power-of-10-checklist.md` | Power of 10 — reviewer checklist | A |
| `docs/power-of-10-deviations.md` | Power of 10 deviation register | A |
| `docs/reviews/2026-09-21-fable-whole-codebase-review.md` | Fable whole-codebase review — lap-timer + dev controller (2026-09-21) | A |
| `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` | Audit 2026-10-13 — roadmap state vs evidence, and code vs spec | A |
| `docs/reviews/2026-10-16-audit-post-fix-wave.md` | Audit 2026-10-16 — main after the fix wave (F0 F3 F1a F1b F2a F2b F4), re-audited against code, spec and bench doc | A |
| `docs/reviews/2026-10-17-audit-drift-and-gaps.md` | Audit drift and fix-wave gap analysis — 2026-10-13 vs 2026-10-16 | A |
| `docs/superpowers/plans/2026-09-14-plan-00-dev-environment.md` | Plan 00: Development Environment Implementation Plan | B |
| `docs/superpowers/plans/2026-09-14-plan-01-core-foundation.md` | Plan 01: Core Foundation Implementation Plan | B |
| `docs/superpowers/plans/2026-09-14-plan-02-core-engines.md` | Session 2.7 — replay engine end-to-end and regression fixtures (plan-closing) -- Roadmap exit criterion: `ctest` runs the replay regressions and the 5 Hz/10 Hz error b... | B |
| `docs/superpowers/plans/2026-09-14-plan-03-firmware-skeleton.md` | Plan 03: Firmware Skeleton Implementation Plan | B |
| `docs/superpowers/plans/2026-09-14-plan-04-host-ui.md` | Plan 04: Host-Rendered UI Implementation Plan | B |
| `docs/superpowers/plans/2026-09-14-roadmap.md` | Lap Timer Roadmap — state of record | B |
| `docs/superpowers/plans/2026-09-17-plan-4.5-power-of-10.md` | Plan 4.5: Power of 10 Compliance Implementation Plan | B |
| `docs/superpowers/plans/2026-09-20-plan-05-lap-timer-prod-link.md` | Plan 5 (sub-project A): Lap-timer Production Link — Implementation Plan | B |
| `docs/superpowers/plans/2026-09-20-plan-5.5-dev-controller.md` | Dev Controller (sub-project B) Implementation Plan | B |
| `docs/superpowers/plans/2026-09-20-plan-dram-reclaim.md` | Static DRAM Reclaim (safe tier) Implementation Plan | B |
| `docs/superpowers/plans/2026-09-21-peer-presence-detect.md` | Peer Presence Detect (GPIO detect line) Implementation Plan | B |
| `docs/superpowers/plans/2026-09-23-plan-5.6-devkit-interface.md` | Plan 5.6 — Dev-kit as Primary Dev Interface — Implementation Plan | B |
| `docs/superpowers/plans/2026-09-25-plan-7-display.md` | Plan 7 — Display (Waveshare 2.13" V4 e-paper) Implementation Plan | B |
| `docs/superpowers/plans/2026-09-27-plan-7b-glanceable-ui.md` | Plan 7b — Glanceable Riding Screens Implementation Plan | B |
| `docs/superpowers/plans/2026-09-29-plan-7c-display-closure.md` | Plan 7c — Display Closure Implementation Plan | B |
| `docs/superpowers/plans/2026-09-30-debt-sweep-a.md` | Debt Sweep A — Implementation Plan | B |
| `docs/superpowers/plans/2026-10-03-debt-sweep-b.md` | Debt Sweep B — Implementation Plan | B |
| `docs/superpowers/plans/2026-10-05-ota-screen-and-glyphs.md` | OTA Progress Screen + SIM/MOVING Header Glyphs Implementation Plan | B |
| `docs/superpowers/plans/2026-10-06-glyphs-drag-menu-closure.md` | Glyphs, Drag Card and Menu Closure Implementation Plan | B |
| `docs/superpowers/plans/2026-10-07-bench-day-4-fixes.md` | Bench Day 4 Fixes Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-audit-fix-wave.md` | Audit Fix Wave — Umbrella Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F0-pr101-closeout.md` | Fix Wave F0 — PR #101 Closeout Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md` | F1a — Core Engines Fix Wave Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F1b-core-io.md` | F1b — core io + ui core Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F2a-fw-supervision-ota.md` | F2a — Firmware supervision + OTA Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F2b-fw-logger-cmd.md` | F2b — Firmware logger + cmd Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F3-sim-devkit.md` | Fix Wave F3 — Sim Drivers + Dev-Kit Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F4-process-docs.md` | F4 — Process, tooling and docs Implementation Plan | B |
| `docs/superpowers/plans/2026-10-13-fix-F5-close-out.md` | Fix Wave F5 — Close-Out Wave (skeleton) | B |
| `docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md` | Strict Delivery S1 — Registers and Core Gates Implementation Plan | A |
| `docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md` | Strict Delivery S2 — Pipeline Gates and Checkpoints Implementation Plan | A |
| `docs/superpowers/plans/2026-10-17-strict-delivery-S3-p10-reach.md` | Strict Delivery S3 — Power of 10 Full Reach and the Exception Register — Implementation Plan | A |
| `docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md` | Strict Delivery S4: Remaining Gates, Post-merge Automation, Workflows | A |
| `docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md` | Strict Delivery S5 — Validation, Bootstrap, Conformance Implementation Plan | A |
| `docs/superpowers/plans/2026-10-17-strict-delivery.md` | Strict Delivery Implementation Plan (umbrella) | A |
| `docs/superpowers/plans/2026-10-29-docs-reorganisation.md` | Docs reorganisation -- one uniform place (stream A) -- Implementation Plan | A |
| `docs/superpowers/plans/2026-10-29-program-tooling.md` | Program tooling -- board, context contract, index, drift check (stream A, first block) -- Implementation Plan | A |
| `docs/superpowers/plans/2026-10-29-prototype-field-ready.md` | Prototype: ready for field testing on the track (stream B) -- Implementation Plan | B |
| `docs/superpowers/playbooks/two-streams.md` | Playbook: two parallel streams (DEC-0133) | A |
| `docs/superpowers/specs/2026-09-14-lap-timer-design.md` | ESP32 Lap Timer — Design Specification | B |
| `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md` | Power of 10 Compliance — Design | A |
| `docs/superpowers/specs/2026-09-20-connectivity-dev-controller-design.md` | Connectivity + Dev-Controller — Architecture Design (Plan 5 replan) | B |
| `docs/superpowers/specs/2026-09-20-dev-controller-B-design.md` | Dev Controller (sub-project B) — Architecture Design | B |
| `docs/superpowers/specs/2026-09-21-peer-presence-detect-design.md` | Lap-timer ↔ dev-controller peer presence + stream gating (2026-09-21) | B |
| `docs/superpowers/specs/2026-09-23-devkit-primary-interface-design.md` | Dev-kit as Primary Dev Interface — Design (Plan 5.6) | B |
| `docs/superpowers/specs/2026-09-25-plan-7-display-deltas.md` | Plan 7 (Display) — design deltas to the lap-timer spec §20 | B |
| `docs/superpowers/specs/2026-09-27-plan-7b-glanceable-ui-design.md` | Plan 7b — Glanceable riding screens (design) | B |
| `docs/superpowers/specs/2026-09-29-plan-7c-display-closure-design.md` | Plan 7c — Display closure (design) | B |
| `docs/superpowers/specs/2026-09-30-debt-sweep-a-design.md` | Debt sweep A — lap-timer robustness (design) | B |
| `docs/superpowers/specs/2026-10-03-debt-sweep-b-design.md` | Debt sweep B — pre-bench closure (design) | B |
| `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` | Audit fix wave — design and rulings | B |
| `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` | Strict delivery: skill, gates, hooks and workflows — design | A |
| `docs/superpowers/state/README.md` | Strict-delivery state | A |
| `docs/superpowers/state/program-tooling/ledger.md` | Program tooling ledger (block b1, branch block/program-tooling, base 39a3e8a, estimate 2 days, started 2026-10-30) | A |
| `docs/superpowers/state/program/board.md` | Stream board (playbook `docs/superpowers/playbooks/two-streams.md`) | A |
| `docs/superpowers/state/program/sources/MANIFEST.md` | Imported workspace sources (DEC-0159) | A |
| `docs/superpowers/state/program/sources/b5-rca-report.md` | b5 process root-cause investigation (investigator, opus, read-only) | A |
| `docs/superpowers/state/program/sources/b5r14-draft-helper.md` | S2-b5-process-code: the D32 shared fail-closed helper -- D24 table and D28 whitelist (B5-PROC-2) -- Surface (base 6a7b2f9; the gate code is unchanged since f1057e5: `g... | A |
| `docs/superpowers/state/program/sources/b5r14-draft-precommit.md` | S2-b5-process-code: the D34 pre-commit gate -- D24 table and D28 whitelist (B5-PROC-2) -- Surface (base f1057e5; spec D34 :48, §8 Git hooks :230, S2 plan :7185 at 6a7b... | A |
| `docs/superpowers/state/program/sources/b5r14-draft-skeleton.md` | S2-b5-process-code: D31 skeleton generator -- D24 table and D28 whitelist (B5-PROC-2) -- Surface (spec D31 :45, D32 :46, S2 plan :7185 (subsection :7183), row B5-PROC-... | A |
| `docs/superpowers/state/program/sources/b5r14-tablerev-helper.md` | gates run -- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) /usr/bin/python3 tools/gates/run_all.py` (clone at f1057e5) -> rc=0, 100 PASS, 0 FAIL. The bri... | A |
| `docs/superpowers/state/program/sources/b5r14-tablerev-precommit.md` | Findings (R-PC-<n>; each replacement is a hypothesis) -- R-PC-1 MEDIUM, PC.53 / W5. The draft has two quotes that contradict each other. PC.53 says "whole-string `--pr... | A |
| `docs/superpowers/state/program/sources/b5r14-tablerev-skeleton.md` | commits -- none (review only; I edited nothing but this report). | A |
| `docs/superpowers/state/program/sources/b5r15-budget/controller-live.md` | Strict block: controller | A |
| `docs/superpowers/state/program/sources/b5r15-budget/controller-staged.md` | Strict block: controller | A |
| `docs/superpowers/state/program/sources/b5r15-budget/r0/controller-staged.md` | Strict block: controller | A |
| `docs/superpowers/state/program/sources/b5r15-budget/r0/spec-new.md` | Strict delivery: skill, gates, hooks and workflows — design | A |
| `docs/superpowers/state/program/sources/b5r15-budget/spec-new.md` | Strict delivery: skill, gates, hooks and workflows — design | A |
| `docs/superpowers/state/program/sources/b5r15-d30-investigation.md` | b5 round 15 D30 investigation: gate_plan fails open when git fails (B5-PROC-2 context) | A |
| `docs/superpowers/state/program/sources/b5r15-rr-helper.md` | gates run -- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d) /usr/bin/python3 tools/gates/run_all.py` (clone, 6a7b2f9) -> rc=0, 100 PASS. The clone holds n... | A |
| `docs/superpowers/state/program/sources/b5r15-rr-precommit.md` | Gates run (clone at 6a7b2f9, fresh prefix each) -- `/usr/bin/python3 tools/gates/run_all.py` -> rc=0, PASS 100, FAIL 0. The clone has no review packages: PC.51. | A |
| `docs/superpowers/state/program/sources/b5r15-rr-skeleton.md` | commits -- none (review only; I edited nothing but this report). | A |
| `docs/superpowers/state/program/sources/b5r15-subcontroller-report.md` | commits -- e17dc90 chore(state): b5 round 14 item B + round 15 registrations -- 22 findings rows, DEC-0102..0122 | A |
| `docs/superpowers/state/program/sources/b5r17-docs-draft.md` | b5 round 17 item 1: documentation and class-as-epic design (draft for owner approval) | A |
| `docs/superpowers/state/program/sources/b5r17-rr2-helper.md` | Gates run -- `PYTHONDONTWRITEBYTECODE=1 PYTHONPYCACHEPREFIX=$(mktemp -d $S/pyc/x.XXXX) /usr/bin/python3 tools/gates/run_all.py` in the clone at 2e43f13 -> rc=0, 100 PA... | A |
| `docs/superpowers/state/program/sources/b5r17-rr2-precommit.md` | commits -- None. The only file I wrote outside the clone is this report. | A |
| `docs/superpowers/state/program/sources/b5r17-rr2-skeleton.md` | commits -- none (review only; no tracked file touched; checkout `git status --porcelain \| wc -l` -> 0 after the run). | A |
| `docs/superpowers/state/program/sources/b5r17-subcontroller-report.md` | commits -- ec1c3c5 chore(state): b5 round 17 item 3 -- B5-FIX-5 D33 siblings per DEC-0128; register B5-UR-16..23 | A |
| `docs/superpowers/state/program/sources/program-docs-fix1-report.md` | commits -- none (the dispatch said no commit in the checkout). | A |
| `docs/superpowers/state/program/sources/program-docs-fix2-report.md` | commits -- none (the dispatch said no commit in the checkout). | A |
| `docs/superpowers/state/program/sources/program-docs-rereview2.md` | commits -- none (read-only review). | A |
| `docs/superpowers/state/program/sources/program-docs-review.md` | commits -- none (read-only review). Checkout untouched: `git status --short` empty, HEAD 2134a43. | A |
| `docs/superpowers/state/program/sources/tracks-research.md` | Track research: Killarney Full / Full Reverse, Kyalami GP (2026-10-29, investigator, read-only) | A |
| `docs/superpowers/state/program/sources/wiring-extract.md` | As-built prototype wiring, extracted from owner transcripts (read-only investigation, 2026-10-29) | A |
| `docs/superpowers/state/prototype/handoff.md` | Stream B handoff (playbook `docs/superpowers/playbooks/two-streams.md`), written at the 2026-10-29 replan; stream B has not started. | B |
| `docs/superpowers/state/prototype/ledger.md` | Ledger -- prototype (stream B) | B |
| `docs/superpowers/state/strict-delivery/decision-sheets/b5r16.md` | b5 round 16: owner decision sheet (DEC-0126) | A |
| `docs/superpowers/state/strict-delivery/handoff.md` | Stream A handoff (playbook `docs/superpowers/playbooks/two-streams.md`), written at the 2026-10-29 replan; it replaces the b4 handoff. | A |
| `docs/superpowers/state/strict-delivery/ledger.md` | Block b1 (branch block/strict-delivery-b1) -- base BASE_B1=73cd3cc | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-T1.md` | Task review 2 (scoped re-review of fix round 1): S2 Task 1 gate_dispatch (SD-S2-T1), fix d9b74f3..8fd1934, task range 6a41cf6..8fd1934 | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-T2.md` | Task 2 review 3 (scoped re-review of fix round 2; final verdict for a85d73c..2e6a2fa) | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-T3.md` | Verdict (whole task range ee6e675..20f807d) -- spec ✅: D3 holds at 20f807d. Every python/markdown fenced block in plan Task 3 (plan:1223-1621, 12 blocks) is a byte sub... | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-T4.md` | Task 4 re-review 4 (fix round 3): block b5, round 2ce84d2..5b82a32, task d533315..5b82a32 | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-T5.md` | Task 5 scoped re-review 3 (S2-T5): range b66deaf..869ed10 (fix round 2), package over 39baf52..869ed10 | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-T6.md` | S2 Task 6 (SD-S2-T6, gate_plan) scoped re-review 6, after fix round 5 (head 02398e3, package c96c1ad..02398e3) | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-b4-fixwave.md` | b4 scoped re-review of the fix wave (B4-FIX-1), range 26148d6..7e5684c | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-b5-caps.md` | S2-b5-caps re-review (b5 round 15) | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-b5-finalfix.md` | Commits reviewed -- 173ae2d fix(gates): B5-FIX-4 -- pin title refuses any Unicode mark neighbour; unspaced dash controls (round 2; da3dc0e..173ae2d, 4 files +50/-22: S... | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-b5-finalfix2.md` | Commits reviewed -- c0316eb fix(gates): B5-FIX-5, B5-FIX-6 -- in-flight reads HEAD, index and working tree; --all FAILs a non-directory .superpowers/sdd (range ea19922... | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-b5-finalfix3.md` | S2-b5-finalfix3 scoped re-reviews (b5 round 10) | A |
| `docs/superpowers/state/strict-delivery/reviews/S2-b5-fixwave.md` | Commits (wave range 7569366..73bdf9f, HEAD 73bdf9f, tree clean) -- 1a6ff7c fix(gates): B5-FIX-2, B5-FIX-3 -- opus fallback for the Fable roles (DEC-0018); gate_plan pi... | A |
| `tools/gates/README.md` | tools/gates -- strict-delivery gate scripts | A |
| `tools/gates/tests/fixtures/dispatch/good_implementer.md` | Role: implementer -- You are an implementer under strict delivery. | A |
| `tools/gates/tests/fixtures/dispatch/good_reviewer.md` | Role: task_reviewer -- You are a reviewer under strict delivery. | A |
| `tools/gates/tests/fixtures/dispatch/prompts/implementer.md` | You are an implementer under strict delivery. | A |
| `tools/gates/tests/fixtures/dispatch/prompts/reviewer.md` | Checks -- Re-run every gate clean. Execute every claim in the report, the commits and the diff comments. | A |
| `tools/gates/tests/fixtures/plan/good.md` | Fixture Implementation Plan | A |
| `tools/gates/tests/fixtures/register/report_unknown/docs/superpowers/state/briefs/b1-brief.md` | A registered brief | A |
| `tools/gates/tests/fixtures/register/valid/docs/reviews/2026-01-01-registered.md` | A registered review | A |
| `tools/gates/tests/fixtures/register/valid/docs/reviews/2026-01-02-unmarked.md` | An unmarked review (no marker line): ignored by the gate | A |
| `tools/gates/tests/fixtures/register/valid/docs/superpowers/state/demo/review/T1-PIN-1.md` | status: DONE | A |
| `tools/lint/fixtures/register_cross.md` | \| id \| rule \| file:symbol \| compliant alternative considered \| why rejected \| benefit \| reviewer \| | A |
| `tools/lint/fixtures/register_none.md` | \| id \| rule \| file:symbol \| compliant alternative considered \| why rejected \| benefit \| reviewer \| | A |
| `tools/lint/fixtures/register_sinks.md` | \| id \| rule \| file:symbol \| compliant alternative considered \| why rejected \| benefit \| reviewer \| | A |
| `tools/replay/README.md` | tools/replay | B |
