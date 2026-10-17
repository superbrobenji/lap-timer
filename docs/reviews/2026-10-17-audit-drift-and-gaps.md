# Audit drift and fix-wave gap analysis — 2026-10-13 vs 2026-10-16

Scope: the two audits of `superbrobenji/lap-timer` and the fix wave between them. Audit 1 = `docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` (main f4d4db2 + PR #101 df75497; 6 opus reviewers + orchestrator re-verification). Audit 2 = `docs/reviews/2026-10-16-audit-post-fix-wave.md` (main 7265059; 7 reviewers + 7 adversarial verifiers + synthesis). Between them: fix wave F0 #101 → F3 #113 → F1a #121 → F1b #122 → F2a #126 → F2b #127 → F4 #138, every PR merged by the owner. This document presents both audits side by side, shows where the second diverged from the first, traces where the drift happened, and reports why the fix wave left gaps. It makes no rulings; §6 lists the decisions that are the owner's.

## 1. Audit 1 (2026-10-13) — what it said

- Gates real: host 47/47, dev-kit 28/28, linter 0, both firmwares 0 warnings; DRAM free 4,192 B (moto_sim) / 5,616 B (moto_neo6m).
- "Plan 4.5 done" untrue as written: static-analysis not a required check; linter blind to most of what it claimed (rule 3 non-blocking, rule 9 one spelling, physical-line length, rules 1/2/6/7 ungated outside core).
- Roadmap stale (three "pending" gates had passed); measurements.md ended at Plan 04; hardware evidence only in git-ignored ledgers and PR bodies.
- moto_neo6m "not a lap timer yet": opens no session, OTA can never validate (stub gps_poll), fusion never calibrated, §17.2 supervision beyond heartbeats absent.
- Timing holes reproduced: <5 s dropout near S/F → wrong lap VALID (CRITICAL); sector gate near S/F → every third lap INCOMPLETE; best sectors leak across venues; unknown layout id silently disables timing; marginal-g stall emits DONE; `cfg.lap.*` inert.
- Exporters: negative `gps_us` → garbage with valid checksum (CRITICAL); JSON summary drops every lap and reports success (CRITICAL).
- Spec wrong vs tree in ~30 places; 60+ false comments/docs; PR #101 MERGE-AFTER-FIXES.
- Totals: 4 CRITICAL · 14 HIGH · ~40 MEDIUM · ~45 LOW · 60+ false claims · 7 linter gaps. §8 gave the fix order; §10 was the status table the wave was to fill.

## 2. Audit 2 (2026-10-16) — what it said

- Gates real and reproduced: host 51/51, dev-kit 33/33, tools 71 + 12 + 5, P10 CI form rc 0, four firmware builds 0 warnings, nine required checks enforced; 0 fix-wave verdicts BROKEN, 0 findings REFUTED by the verifiers.
- Wave closed its targets: all 4 CRITICALs FIXED or PARTIAL (2 + 2), 12 of 13 HIGHs FIXED or PARTIAL, none REGRESSED; dev-kit OTA pacing NOT FIXED (mitigated, #57).
- DRAM no longer a constraint: 66,472 / 66,640 B free (BT off, R33).
- moto_neo6m still exercises almost nothing (GPS/IMU stubs return `-ENODEV`); only bench items 38, 40, 49, 62 run on it.
- New: exports succeed on CRC-damaged logs (IO-N1/N2), JSON summary dies with the evicted `.log` (IO-N3), VBO never carries `[laptiming]` (IO-N4); every rotated session's `.sum` VENUE blank (APP-N1, PR #127's own "fix before merge" item); sign_release can sign a stale `CFG_FW_VERSION` (DRV-N1); three gates weaker than documented (R29 "permanent test" absent, rule 9 blind to esp_console callbacks, spec_api_check ignores macros); four latent engine HIGHs (CE-N2..N5); brownout 2.43 V vs spec 2.8 V; bench helpers assert DTR/RTS (OTA rollback hazard).
- Bench doc: of 51 rows, 6 cannot run, 6 would false-pass, 15 need a rewrite, 25 run as written.
- Totals: 0 CRITICAL · 14 HIGH · 44 MEDIUM · 111 LOW; 120 false-claim rows (30 linked to code defects). Triage proposal A 8 · B 66 · C 89 · D 6 — not yet ruled on by the owner.

## 3. Where the two audits diverge

### 3.1 Severity picture

| | Audit 1 (f4d4db2) | Audit 2 (7265059) | Movement |
|---|---|---|---|
| CRITICAL | 4 | 0 | all four addressed: 2 FIXED + pinned, 2 PARTIAL (code fixed, class residual: OTA gate on neo6m untested on host; positive-absurd `gps_us` still exports) |
| HIGH | 14 (13 scored + ISR row re-graded) | 14 | 7–8 of the old HIGHs FIXED, 4–5 PARTIAL, 1 NOT FIXED (#57); the 14 new HIGHs are different findings (§4 below) |
| MEDIUM | ~40 | 44 | ~25 old FIXED, ~13 PARTIAL; 24 new code MEDIUMs + 20 standalone text/bench rows |
| LOW | ~45 | 111 | the larger count is mostly audit 2's wider claim surface (612 claims checked vs audit 1's narrower set) and its bench-doc / spec row-by-row register |
| False claims | 60+ | 120 rows (30 linked to defects) | the fix wave fixed most audit-1 rows and the documents it wrote (bench doc, PR bodies, FW design, roadmap rewrite) added new ones |
| Linter gaps | 7 | 3 FIXED, 5 PARTIAL, 1 NOT FIXED + 2 new (TCI-T2 esp_console, TCI-T5 spec_api_check) | |
| Gates | 47/47, 28/28, 5 required checks | 51/51, 33/33, 9 required checks | process goals met |
| DRAM free | 4,192 / 5,616 B | 66,472 / 66,640 B | R33 |

### 3.2 What audit 2 found that audit 1 did not (14 new HIGH by origin)

| id | finding | why audit 1 did not have it |
|---|---|---|
| IO-N1, IO-N2, IO-N3, IO-N4 | export verdict on CRC-damaged logs; JSON from `.log` after eviction; VBO `[laptiming]` never emitted | audit 1's io reviewer exercised the exporters on well-formed records (negative `gps_us`, JSON header) but not the reader's CRC-resync path, eviction, or the layout field; audit 2 compiled the real cmd.c into a harness |
| APP-N1 | rotated sessions' `.sum` VENUE blank | introduced by F2b (#127) — the session-rotation code did not exist on f4d4db2; PR #127's review found it (M-1) and it was merged open |
| DRV-N1 | sign_release signs a stale version | sign_release had no guards at all in audit 1 (M5 "refuses neither sim nor dirty"); F4 added the guards and the version check was not among them |
| TCI-T2, TCI-T5 | rule 9 blind to esp_console callbacks; spec_api_check ignores macros | audit 1 LG-3 named "function addresses as arguments" generally; F4 fixed the declarator spelling and left the `.func = func` store; spec_api_check did not exist on f4d4db2 (F4 wrote it) |
| DC-FC-1 | R29 "permanent test" does not exist | F3 fixed the drag position bug (drv:H2) and PR #113 claimed a permanent test; no mutation check was run at review |
| CE-N2, CE-N3, CE-N4, CE-N5 | Doppler at arrival not epoch; forced layout keeps the lock; lap best crosses venues; fusion never oriented so drag cannot arm on a tilt | all four pre-existing on f4d4db2 and adjacent to audit-1 findings (H2 best-sector leak was fixed for §10.8 only; M1 `g_lon` clamp fixed the symptom not the un-oriented cause); audit 2's engine reviewer wrote new reproducers (drag-latency, drag-stale-doppler, lap-layout, uncal-tilt) that audit 1 did not |

(The per-row evidence for this table is in `fix-wave-gaps.md` §3; the "why" column here is the synthesis of that file and the two audit reports.)

### 3.3 Where audit 2 contradicts the fix wave's own record (§2.6 of audit 2)

Ten headline claims in PR bodies and the FW design are false on 7265059: PR #113's "permanent test"; PR #121's "property of the chord, not of its duration"; PR #121's bench items 26–29 provability; PR #122's four fault glyphs (bit 1 is rewritten by ui.c); PR #126's "every on-device claim is a bench item"; PR #127's "fix before merge, PR stays a draft" (merged with M-1/M-2 present, on the orchestrator's "merge now" recommendation, the owner's choice); PR #138's "GRACE-STALE fails the job" and "draft until the user has read the rulings"; FW §7b "#107 fixed by R5"; FW §4 "every finding id appears in exactly one plan".

## 4. Why the fix wave did not properly address audit 1 (from `fix-wave-gaps.md`, 136 audit-1 rows traced through plan → PR → audit-2 verdict)

### 4.1 Where every audit-1 finding ended up

| Gap class | Rows | Meaning |
|---|---|---|
| FIXED and pinned | 58 (36 code/test, 22 doc/process) | removing the fix would fail a committed test, or the document now matches the tree |
| Code fixed, not pinned | 20 | the defect is gone on 7265059 but no test would catch its return (e.g. the CRITICAL OTA validate gate `pipeline_sensors_ok()`; task WDT subscription; tracks/coredump ops) |
| Partially fixed, residual class | 25 | the reported instance is fixed, a sibling case is live (e.g. negative `gps_us` guarded, positive-absurd not; best-sector tables cleared, lap best not → CE-N4) |
| Deferred by design | 23 | FW design / §7b routed it to F5, Plan 8 or an issue (e.g. #114, #123, #128, #57) |
| Claimed fixed but false | 2 primary (4 rows carry it) | R29 "permanent test" (mutation passes), "#107 fixed by R5", PR #121 chord claim, rule-9 documentation |
| Never mapped | 8 | pr:L5–L11 (routed in FW §5, transcribed into no sub-plan task) and the gen_fonts drift check |
| Regressed | 0 | |

### 4.2 Mechanisms (78 rows not fixed-and-pinned; a row can carry several)

| # | Mechanism | Rows | Strongest example (cited in the investigation) |
|---|---|---|---|
| 1 | The wave's definition of done never required a per-finding pinning test | 17 | F2a Task 2's only verification of the CRITICAL's validate gate is "verify it fails to link"; C-1 rests on bench item 40, which audit 2 rates NEEDS REWRITE |
| 2 | Pinning test written against a different unit than the defect | 5 | `test_sim_profile` tests `sim_drag_lat_e7` with a constant origin and never compiles gps_sim.c; the F3 plan itself wrote "now as a permanent test" (F3 plan:2200); task review, final review and controller all accepted it; a mutation restoring the bug passes 4/4 |
| 3 | Reviews accepted claims without executing them | 3 + 2 later claims | TCI-T1: the ledger recorded "rc 0 with 2 GRACE-STALE" before the PR #138 body said GRACE-STALE "fails the job"; PR #122 "four glyphs" while ui.c rewrites bit 1 |
| 4 | Rulings fixed the reported instance, not the class | 11 | the 2026-10-13 reviewer named the lap-best leak (`L->best`, core-algorithms.md:37); audit 1's summary and ruling R25 kept only the best-sector table; now CE-N4 HIGH |
| 5 | Explicit deferral to an issue / F5 / Plan 8 | 30 | the reachable pipeline half of core:H3 → #114 (OPEN, no F5 task); F5 not started |
| 6 | Merged with known open defects | 3 + 3 new findings known pre-merge | PR #127 body "fix before merge (Minor ×2), PR stays a draft"; the orchestrator recommended "merge #127 now" saying the triggers needed a full queue or a 60 s stall, while the PR's own M-1 text named "a plain mode rotation"; the owner ruled merge; M-1 is now APP-N1 HIGH |
| 7 | The fix introduced the residual | 9 rows; 12 new findings with the wave as primary origin | APP-N1 (pipeline.c, e0defd3), APP-N2 (ota.c, f38d8a6), CE-N6 (R21's own rule aborts on one low Doppler sample) |
| 8 | Audit-1 coverage: path in scope but not exercised | 16 new findings pre-existing and missed, 1 out of scope | IO-N4: the VBO `[laptiming]` writer was "verified correct" in isolation while the only production caller hard-codes `has_sf = 0` (cmd.c:998) |
| 9 | Audit-1 synthesis compressed away reviewer detail | 1 confirmed | CE-N4 (`L->best` in the reviewer report, absent from §7.5) |
| 10 | File-ownership partition handed the reachable half to another plan | 2 (+ IO-N2's cmd.c/core split) | core:H3 engine half in F1a, pipeline half → #114; F4's "logger.c is F2b's file and has no host harness" |
| 11 | Routed ids never transcribed into a sub-plan task | 9 | pr:L5–L11: FW §5 routes them, no sub-plan names them, F0 plan:658 gives a third routing; FW §4's "every id in exactly one plan" check was never run |
| 12 | State records written before the event, or not updated after | 3 | roadmap rewritten in F4 Task 10 before #138 merged (still says main = e0defd3, F4 draft); FW §7b "#107 fixed by R5" left after F4 Task 12 rejected the close |
| 13 | Stale-comment gate is an instance list | 3 | ui.c:631-634 "(plan 06)" passes ci.yml's regex |
| 14 | Bench as the only proof, bench not run or not runnable | 5 | the OTA-on-neo6m CRITICAL's only proof is bench item 40 |

### 4.3 Where audit 2's 37 new HIGH/MEDIUM findings came from

| Origin | Count |
|---|---|
| Introduced by the fix wave | 12 |
| Pre-existing on f4d4db2, missed by audit 1 (path in scope, not exercised) | 16 |
| Flagged by audit 1, then narrowed or deferred by a ruling | 6 |
| Known before the wave and deferred | 2 |
| Out of audit 1's scope (bench helpers etc.) | 1 |

### 4.4 Orchestrator's own part (stated plainly)

Of the mechanisms above, 1, 3, 4, 6, 11 and 12 are orchestration failures, not implementer failures: the definition of done, the review acceptance criteria, the rulings that narrowed scope, the merge recommendation for #127, the untranscribed ids and the pre-written state records were all the orchestrator's (this assistant's) decisions across the 2026-10-13..15 sessions. Mechanism 9 (audit-1 synthesis dropping `L->best`) is the same. The 2026-10-17 attempt to rule on audit 2's triage and start a plan without presenting the report was the same pattern again and was stopped by the owner.

## 5. Roadmap vs audit 2 — drift diff (from `roadmap-drift.md`, read-only investigation, 68 roadmap claims checked)

### 5.1 Counts

| Measure | Result |
|---|---|
| Roadmap claim rows | 68: TRUE 49 · FALSE 9 · UNPROVEN 2 · STALE 8 |
| Spec §2.1 requirements F1–F12 (MET = acceptance holds on the release target moto_neo6m with evidence) | MET 0 · PARTIAL 9 (F1 F2 F3 F4 F6 F7 F8 F11 F12) · UNMET 3 (F5 in production, F9 BLE export, F10 power) |
| Roadmap plans (27) | CLOSED 8 (0, 1, 2, 4, 7b, dsA, dsB, OTA screen) · closable host-only 3 (4.5, 7c, F4) · closable with host work + a bench run on owned hardware 11 (3, 5, 5.5, 5.6, F0/#101, F1a, F1b, F2a, F2b, F3, F5) · hardware-gated 5 (7 ws29v2 panel, 6, 8, 9, C) |
| Untracked items (no roadmap row, plan, F5 skeleton item or open issue) | 160: 63 of audit 2's 79 §4 code findings (10 HIGH, 19 MEDIUM, 34 LOW) + 29 bench rows + 51 standalone false claims + 13 unpinned fix-wave residuals + 3 dropped audit-1 findings (pr:L5–L7) + 1 new roadmap drift |
| Issues the roadmap owner table assigns to F5 that the F5 skeleton omits | 17 |

### 5.2 The three largest drifts

1. **"Fix wave DONE" vs its own goal.** Roadmap lines 269–277 list F0–F4 DONE (true as merges) and line 97 says the six engine defects are "all fixed by F1a". Audit 2 finds 14 HIGH after the wave, 4 of the 6 F1a headline fixes with residuals (CE-N1, CE-N4, APP-N7/#114, CE-N5), three gates weaker than their documents (TCI-T2, TCI-T5, DC-FC-1) and 35 FALSE spec rows. The umbrella's stated goal (close every hardware-independent finding, make every enforcement claim true, reconcile the spec) is not met; the sub-plans are merged, not closed.
2. **The "next" pointer runs into a bench doc that cannot run.** Roadmap sequence F4 → bench day → F5 → Plan 8; audit 2 §5: 6 rows cannot run, 6 would false-pass, 15 need a rewrite, helpers are a rollback hazard. No roadmap row or plan in the tree covers a pre-bench step.
3. **Release target vs requirements.** Nothing is MET on moto_neo6m (no fix, no IMU). The four latent HIGH engine defects that switch on with Plan 8 (CE-N2, CE-N3, CE-N4, CE-N5) are absent from the roadmap's Plan 8 section; only CE-N5 reaches an issue (#93).

New drift found by the investigation and verified by this synthesis: roadmap line 177 says the ws29v2 build "is gated in CI"; `.github/workflows/firmware.yml:14` matrix = `[moto_neo6m, moto_sim, devcontroller]`, no ws29v2 job.

### 5.3 What re-aligns the codebase (options, not rulings)

The investigation lists every disagreement with its two directions (code → text, or text → code) and a relative cost: 9 roadmap/process rows (P1–P9), 17 spec rows (S-a … S-q), 7 bench-doc groups (BD-1 … BD-7). The ones that change what a rider sees, grouped by the plan whose closure they block:

| Plan blocked | Disagreement | Code → spec | Spec → code |
|---|---|---|---|
| 5 (cmd/export), F9 | §14.1 VBO `[laptiming]`, LIST layout (IO-N4, IO-V1) | fill from the track tables + test (M, host) | amend §14.1/§14.3 |
| 5, F8 | §12.6 / §14.3 damaged-log export (IO-N1, N2, N3) | JSON from `.sum`; `frames_bad` into the verdict; deltas dropped to the next KEY (M, host) | amend §14.3 to "may export short/shifted" (contradicts F8) |
| 2, F1/F3 | §10.5 force-layout locks immediately (CE-N3); §10.8 best per venue (CE-N4, CE-V-2) | re-lock on force; reset lap best + pipeline `s_best` + ui board on venue change (M, host) | amend §10.5/§10.8 |
| F2b, 5 | §12.1 session carries its venue (APP-N1) | one `logger_set_venue` in `session_open` (S, host) | none sensible |
| 5.4, F12 | §21.5 signed image carries `git describe` (DRV-N1) | reconfigure + refuse on mismatch, pinned in `test_sign_release.sh` (S, host) | none sensible |
| 4.5 | rule 9 / spec_api_check claims (TCI-T2, T5); GRACE-STALE (T1); CI "Nothing to do" (T7) | make the tools see what the docs say (M, host) | amend P10 docs / spec §5.2 to the tools' real reach |
| 3, F11 | §15.3 continuity bench-able on the sim (#103; items 55/57) | sim clock epoch in `RTC_NOINIT_ATTR` (F5 task 4) | bench doc: real GPS only |
| 6, F10 | §17.1 brownout 2.8 V (DRV-N4) | one sdkconfig line + USB-power boot check | amend §17.1 to 2.43 V until Plan 6 |
| 8, F4/F5 | §6.6 Doppler epoch (CE-N2); §9.2/§9.3 orientation (CE-N5) | Plan 8 driver epoch + N-sample abort; F5 task 9 wiring then Plan 8.4 | amend tolerances; date-mark §9 |
| 7 | ws29v2 "gated in CI" | add a ws29v2 job to firmware.yml + protection | amend line 177 |

The full tables are in the investigation file (git-ignored) and rendered in the companion page.

## 6. Verification status of audit 2, and what remains for the owner

### 6.1 How audit 2's findings were verified (already done)

Every reviewer report was attacked by a separate adversarial verifier that re-ran every reproducer, re-read every cited line and tried to refute every verdict in both directions (VERIFIER-BRIEF.md). Result: 0 REFUTED findings, 0 BROKEN fix verdicts; 612 claims checked; harnesses compiled the real cmd.c, pipeline.c and logger.c (27 + 18 checks green). Two independent read-only investigations (this document's §4 and §5) re-checked the citations they relied on and found one new drift (ws29v2 CI) and no contradiction with audit 2. What is NOT verified: the UNVERIFIABLE list in audit 2 §7 (hardware-only items), the fix-wave rows audit 2 never scored (pr:H1, H2, L1–L4, #95–#99, #102), pr:L8/L9 state, and whether APP-N6 was reachable before the wave.

### 6.2 Decisions that are the owner's (no ruling made here)

1. **Independent re-verification of audit 2:** accept the existing verifier layer, or dispatch a fresh set of verifiers (different model/agents) over all 79 §4 code rows, or over the 14 HIGH + 24 MEDIUM only. Cost: roughly one agent per area (7), 1–2 hours wall clock.
2. **Process gaps to close before the next fix plan** (§4.2): which of the 14 mechanisms become rules of the next plan's definition of done. Candidates the evidence supports: a per-finding pinning test that fails on a mutation (1, 2); reviewers must execute, not read, every "fixed"/"permanent test"/"fails the job" claim (3); rulings address the class the reviewer named, not the summary's instance (4, 9); no merge recommendation while a "fix before merge" item is open (6); every routed finding id is grep-checked into exactly one task before execution starts (11); state documents written after the event they record (12); the stale-comment gate matches a pattern, not a list (13); nothing that cannot run on the bench counts as proof (14).
3. **Audit 2 §6 triage** (A 8 / B 66 / C 89 / D 6): accept as proposed, widen A (the orchestrator's withdrawn P-1 list: IO-N1..N4, CE-N3, CE-N4/V-2, APP-N2, TCI-T1, DRV-N4, DRV-N2), or re-bucket.
4. **Re-alignment direction per disagreement** (§5.3 and the investigation's P1–P9, S-a..S-q, BD-1..BD-7): code → spec or spec → code, each.
5. **Untracked work** (160 items): file issues for the C bucket now, or carry them in a plan document, or both.
6. **Plan closure targets:** which of the 11 "host work + owned-hardware bench" plans and the 3 host-only plans the pre-bench plan should close, versus leaving F-plans "merged, not closed" and tracking residuals in F5.
7. **Verification of hardware-only items** stays for bench day; flashing only on the owner's explicit "ready".

## 7. Method

Audit reports read in full; both investigations were read-only opus agents with no gh writes, no builds and no subagents; their citations spot-checked by this synthesis against the tree (ws29v2 absent from firmware.yml:14; roadmap:5 main = e0defd3; 17 F5-owned issues absent from the F5 skeleton; F3 plan:2200 "now as a permanent test"; core-algorithms.md:37 `L->best`; umbrella ledger :105–106 merge timeline). Investigation files (git-ignored): `.superpowers/reviews/2026-10-17-gap-investigation/fix-wave-gaps.md` (507 lines), `roadmap-drift.md` (317 lines); both are rendered in full in the companion page.
