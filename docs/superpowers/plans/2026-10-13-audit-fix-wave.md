# Audit Fix Wave — Umbrella Plan

> **For agentic workers:** this is the index. Execute the seven sub-plans below with superpowers:subagent-driven-development, one sub-plan per branch/worktree, in the order and parallel groups given. Each sub-plan is self-contained and carries its own tasks, tests, gates and commit steps.

**Goal:** Close every finding of the 2026-10-13 audit that does not need hardware the project does not own, make every enforcement claim true, and reconcile the spec with the tree.

**Architecture:** Seven plans partitioned by file ownership so they can run in parallel worktrees without merge conflicts: PR #101 closeout (F0) first; then core engines (F1a), core io + ui core (F1b), sim + dev-kit (F3) and process/tooling/docs (F4) in parallel; then firmware supervision + OTA (F2a) and firmware logger + cmd (F2b) serially on top. F4's spec-reconciliation task runs last of all so the spec describes the merged tree.

**Tech Stack:** C11 under Power of 10, ESP-IDF v5.3.2, Unity host harnesses (lap-timer `test/`, dev-kit `devcontroller/test/`), Python 3 tooling, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-13-audit-fix-wave-design.md` (binding rulings R1–R32, event-code allocation, gates, filed items). Audit: `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md`. Fact sheets: `.superpowers/reviews/2026-10-13/*.md` + reproducers (git-ignored).

## Global Constraints

- Every sub-plan inherits design §4 gates: host suites 100 % on clang + gcc-16, `power_of_10.py --enforce-fnptr --fail-on-violation` 0, `venue_announce.py` 0, ccache-off clean `moto_sim` / `moto_neo6m` / `PANEL=ws29v2 moto_sim` / `test_apps/core_selftest` builds with 0 warnings, free static DRAM reported against the baselines (`moto_sim` 4192 B, `moto_neo6m` 5616 B).
- Nobody flashes or opens a serial port during execution; one bench day after F2b merges, driven from `docs/bench/next-bench-day.md`, which each sub-plan appends to.
- Every audit finding id appears in exactly one sub-plan's tasks or in design §5 (filed). The final wave of each sub-plan lists the ids it closed.
- Ledgers: `.superpowers/sdd/2026-10-13-fix-<plan>/progress.md` per sub-plan (git-ignored), updated after every task.
- Commit messages end with the trailer in `git log -1 --format=%B` on `main`.

---

## Execution order

```
F0  PR #101 closeout            branch glyphs-drag-menu-closure (worktree gdm)   → squash-merge #101
      │
      ├── F1a core engines        branch fix-core-engines     ┐
      ├── F1b core io + ui core   branch fix-core-io          │ PARALLEL worktrees, independent PRs
      ├── F3  sim + dev-kit       branch fix-sim-devkit       │ (disjoint files, see design §1)
      └── F4  process/tooling     branch fix-process-docs     ┘  (its spec task waits for the end)
                │ F1a merged
            F2a firmware supervision + OTA   branch fix-fw-supervision
                │ F2a merged
            F2b firmware logger + cmd        branch fix-fw-logger-cmd
                │ F2b merged
            F4 final tasks: spec reconciliation, roadmap rewrite, cleanup sweep, blocking flip of the new linter checks
                │
            Bench day (next-bench-day.md items from every plan)
                │
            F5 close-out wave: design §7/§7b items (#53 #57 #61 #91 #93-wiring #100 #103 #104 #105 #106, s5.2 rebase), parallel by file
```

| # | Sub-plan | File | Depends on | Est. tasks |
|---|---|---|---|---|
| F0 | PR #101 closeout | `2026-10-13-fix-F0-pr101-closeout.md` | — | 8 tasks, 3 sessions |
| F1a | Core engines | `2026-10-13-fix-F1a-core-engines.md` | F0 | 14 tasks, 4 sessions |
| F1b | Core io + ui core | `2026-10-13-fix-F1b-core-io.md` | F0 | 16 tasks, 4 sessions |
| F3 | Sim + dev-kit | `2026-10-13-fix-F3-sim-devkit.md` | F0 | 12 tasks, 3 sessions |
| F4 | Process, tooling, docs | `2026-10-13-fix-F4-process-docs.md` | F0; Tasks 9–14 after F2b | 14 tasks, 4 sessions |
| F2a | Firmware supervision + OTA | `2026-10-13-fix-F2a-fw-supervision-ota.md` | F1a | 10 tasks, 3 sessions |
| F2b | Firmware logger + cmd | `2026-10-13-fix-F2b-fw-logger-cmd.md` | F2a | 9 tasks, 3 sessions |
| F5 | Close-out wave (open issues, in-progress plan items, cleanup verification) | `2026-10-13-fix-F5-close-out.md` (skeleton now; filled after the bench day) | F2b + bench day | medium |

## Ownership exceptions (declared in the sub-plans; precedence by merge order)

- F1b edits two lines outside its tree (`main/app_main.c` `boot_config`, `ui.c:1498-1514` `s_fb.dirty` asserts) — design R26/R27.
- F2a lists 11 line-exact foreign-file edits (pipeline.c accessors, logger.c WDT reset calls, cmd.h `CMD_JSON_CAP`, `CMakeLists.txt` `CFG_CHRG_WIRED`, `core_selftest`/`test/CMakeLists.txt` for `sup_policy.c`, `batt_trim.c`). F2a merges before F2b, so F2b's executors re-read line numbers from the merged tree (F2b cites the F0 tree).
- F2b adds the cfg RMW mutex creation to `lt_nvs_init()` and two `core_selftest` SRCS lines (design R10a).
- F3 hands spec §4.6 text to F4 (its PR body carries the block).
- Where two plans would touch the same line, the later-merging plan rebases and keeps the earlier plan's change.

## Checkpoint protocol (/clear-safe sessions — user rule 2026-10-13)

No session runs more than one "Session" block of a sub-plan (≤ 4 tasks). Every task ends with a **CHECKPOINT** step: the task's commit is pushed, the sub-plan's ledger (`.superpowers/sdd/2026-10-13-fix-<plan>/progress.md`) gets `Task N complete <sha>` plus the exact resume prompt, and the operator may `/clear`. To resume after `/clear`, paste the ledger's last resume prompt, e.g.:

```
Resume plan docs/superpowers/plans/2026-10-13-fix-F1a-core-engines.md at Task 5
(ledger .superpowers/sdd/2026-10-13-fix-F1a/progress.md); worktree ../lap-timer-worktrees/f1a; branch fix-core-engines.
```

The resuming session reads the ledger, the plan's Global Constraints and the task text, re-runs the host suite once, and continues. Session-end checkpoints additionally re-run lint and (for firmware plans) one clean build. The umbrella's own ledger `.superpowers/sdd/2026-10-13-audit-fix-wave/progress.md` records which sub-plans are in flight, their worktrees and their last checkpoint.

## Worktree protocol (per `feedback-parallel-tasks`)

1. After F0 merges: `git worktree add ../lap-timer-worktrees/<short> -b <branch> main` for F1a, F1b, F3, F4 and run them concurrently (one subagent per task inside each plan; tasks labelled PARALLEL inside a plan may themselves fan out in nested worktrees).
2. Merge order among the parallel four is free (disjoint files); rebase each PR on `main` before its final wave.
3. F2a branches from `main` after F1a merges; F2b from `main` after F2a merges.
4. F4's last task (spec reconciliation, blocking flip) is executed on a fresh branch after F2b merges, then the bench day.

## Done criteria for the wave

- All seven PRs merged; `main` green on the nine required checks (design R13).
- `docs/measurements.md` carries the bench-day record for this wave.
- Audit §7 ids: each closed in a PR or filed per design §5; the audit report gains a closing "Status" section listing them.
- Memory: `project-audit-2026-10-13` updated with the merge commits.
