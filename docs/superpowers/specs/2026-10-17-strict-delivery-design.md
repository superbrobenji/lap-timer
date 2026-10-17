# Strict delivery: skill, gates, hooks and workflows — design

Date: 2026-10-17. Status: DRAFT for owner review. Owner: the repository owner. Author: the assistant session, from the brainstorm of 2026-10-17.

## 0. Why

Two audits bracket the 2026-10-13..15 fix wave. The first (`docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md`) found 4 CRITICAL / 14 HIGH; the second (`docs/reviews/2026-10-16-audit-post-fix-wave.md`) found 0 / 14 / 44 / 111 after the wave. The drift and gap analysis (`docs/reviews/2026-10-17-audit-drift-and-gaps.md`) traced 136 audit-1 rows: 58 fixed and pinned, 20 fixed but unpinned, 25 with a residual class, 23 deferred, 2 claimed-fixed-but-false, 8 never mapped. Of 37 new HIGH/MEDIUM findings, 12 were introduced by the wave and 16 were on paths audit 1 had in scope but never exercised. 160 items have no owner. Fourteen mechanisms let this happen; six of them are orchestration decisions (definition of done, acceptance of unexecuted claims, instance-not-class rulings, a merge recommendation with "fix before merge" open, finding ids never transcribed, state documents written before the event), and a fifteenth is the 2026-10-17 attempt to rule on triage and start a plan without presenting the audit.

This document designs the process that prevents each mechanism from recurring. It changes no product behaviour. Nothing in it is a fix; every fix waits for it.

## 1. Decisions taken in the brainstorm (binding for this design)

| # | Decision |
|---|---|
| D1 | Rules live in both layers: a personal skill in `~/.claude/skills/strict-delivery/` and project enforcement in this repo (`CLAUDE.md`, `.claude/`, `tools/gates/`, CI, branch protection). |
| D2 | Superpowers stays installed and unmodified; our layers override by precedence; per-role prompt blocks are appended to every dispatch. |
| D3 | No claim in any document is trusted. Every claim is verified before execution by a direct check and by proof by contradiction (assume the negation, derive an observable, test it). |
| D4 | Claim coverage: a claim register cleans what exists (every tracked document); per-execution verification covers everything an agent touches or depends on. A divergence found is fixed. |
| D5 | Fix direction by document class: non-binding text is fixed inline by the verifying agent with evidence; spec-vs-code divergence is a decision question for the owner, execution blocked until answered; plan-vs-spec is corrected to the spec by the controller and ledgered. |
| D6 | Zero-bug rule. A bug produced by an agent's own change makes the feature incomplete until a fix agent closes it and the re-review is clean; no "minor" carve-out. An unrelated bug found gets an investigation agent, an issue, and an owner in a plan; a gate fails on any finding without an owner. Subagent `gh` writes are denied by the auto-mode classifier, so issue bodies are queued in a tracked file and posted by the orchestrator under the owner's standing grant; queued-but-unposted blocks. |
| D7 | "Pinned" = a test through the production call path (the real file, not an extracted helper) plus a recorded mutation run (fix reverted or bug re-injected → test fails → restored → passes), both in the review package; the reviewer re-runs the mutation. Automated mutation testing in CI is a later spike. |
| D8 | Reviewers execute every claim independently: they re-run the full gate set clean, never trust implementer output, list every behavioural claim in report, commits and PR body and give each a direct and a contradiction check with command and output; a claim with no executable check is a finding. |
| D9 | Merge readiness is machine-checked; the orchestrator presents the script output and never recommends a merge with a red line. |
| D10 | Decision classes are explicit (owner-only vs controller rulings); controller rulings only inside an approved plan, only plan-vs-spec or task-vs-task conflicts settled by the spec, max 5 per block, listed at the checkpoint; owner-only decisions are written as questions with options and cost into a decisions file and the path halts; a gate blocks anything referencing an unanswered decision. |
| D11 | One findings register; roadmap owner tables and close-out lists are derived from it; lifecycle states are gated. |
| D12 | State documents' facts are generated and CI-checked; remaining hand-written facts carry claim rows; the stale-comment gate is pattern-based; the tag rule is enforced or deleted (owner picks in §11). |
| D13 | Power of 10 by every implementer, always: full-reach linter on all trees, zero grace, register-only exceptions, probe fixtures per rule, implementer runs it before reporting, reviewer re-runs, CI blocks. |
| D14 | Exceptions: two tiers. Tier A (SDK contract) allowed by category with one row per site and the pointed-to set listed. Tier B allowed only when a scored benefit exceeds a scored cost, argued against by an adversarial reviewer, with owner and expiry. |
| D15 | Audits: coverage lists before review, verbatim finding capture into the register, an adversarial verifier per reviewer, bench rows only with runnability transcripts. |
| D16 | Sibling sweep per fix; contradiction pass on the fix's new surface; final whole-branch review = opus reviewer + Fable adversarial verifier re-running every gate and every pin. |
| D17 | Hooks block the loop: `PreToolUse` on Agent and on git/gh commands, `Stop` at block end; git hooks and CI run the same scripts. No logging hook. |
| D18 | Models: controllers, task reviewers, final reviewer, audit reviewers and verifiers on opus; implementers on sonnet (haiku only for transcription of complete code); Fable on the per-plan final adversarial verifier and the exception reviewer only; the dispatch hook enforces the map. |
| D19 | Validation: red/green tests for gate scripts (probe fixtures) and for the skill text (writing-skills pressure scenarios), then a dry run on a throwaway finding. |
| D20 | The skill applies to every change, with no size threshold. |
| D21 | Build both A (data-driven gates + thin skill) and B (Workflow scripts for the audit fan-out and the block loop). |
| D22 | Reset checkpoints: bounded agent lifetime; a checkpoint produces a status update, a committed handoff document and a resume prompt; the tree is clean (no worktree but main, no open PR from the plan, no live subagent, no in-progress task); block = PR; the agent merges on all-green (every CI job including advisory) under a per-plan standing grant; parallel worktree tasks allowed with a file-overlap gate and a reviewed integration task; handoff, ledger and decisions committed under `docs/superpowers/state/`; triggers = 3 tasks / 250k tokens / 90 min for a controller, 1 task / 150k for an implementer, 1 review / 120k for a reviewer, any 429; one block per context, the session stops after each block for the owner's `/clear`. |
| D23 | Rollout ends with a conformance sweep that brings the current tree to green on every gate, run through the pipeline itself. |

## 2. Scope, layers, precedence

**Covers** every change to the repository made by any agent, or by the owner through an agent: code, tests, tools, CI, docs, spec, plans, bench doc, memory notes, PR bodies, issue text. A one-line fix walks the same pipeline; only the work inside each step scales.

**Trees under the code gates:** `components/core`, `components/app`, `components/drivers`, `main`, `devcontroller`, `test`, `test_apps`, `tools` (Python: unit tests, type check, non-zero exit on any failure), `.github`. Nothing is report-only.

**Layers, highest precedence first:**
1. `CLAUDE.md` (project instructions): the overrides that must beat superpowers, one line each, each naming the gate that enforces it. Content in §12.
2. `~/.claude/skills/strict-delivery/` (personal skill): roles, decision classes, verification method, dispatch contract, pipeline, which gate runs when, per-role prompt blocks as files. Invoked at the start of every session; in a repository without the gates it applies the judgment rules and states that the gates are missing.
3. Superpowers, unmodified. Its reviewer prompt says "do not re-run the suite"; our block appended below it says the opposite, and precedence settles it.

**Enforcement points, same scripts everywhere:** Claude Code hooks, git `pre-commit`/`pre-push`, CI required checks, branch protection. A rule that cannot be scripted is written in the skill with the words "judgment, not gated".

**Out of scope:** product spec content, audit-2 triage, any fix.

## 3. Reset checkpoints

### 3.1 Triggers (defaults, owner-tunable in `tools/gates/config.yml`)
- Controller (session or sub-controller): 3 tasks completed, or 250k tokens, or 90 minutes, whichever first.
- Implementer: 1 task, or 150k tokens.
- Reviewer / verifier: 1 review, or 120k tokens.
- Any usage-limit 429: checkpoint at the current task boundary.
The dispatch hook writes the budget into the dispatch; the controller tracks children from task-notification usage lines and itself from dispatch count and notification sizes; the `Stop` hook and `gate_checkpoint` are the hard stop.

### 3.2 Block = PR
A plan is sliced into blocks of at most 3 tasks. Each block is one branch and one PR. A block ends only in a clean state (§3.4). Parallel tasks inside a block run in worktrees only when `gate_plan` confirms disjoint file sets; each gets its own reviewer; integration into the block branch is itself a reviewed task with a full gate run.

### 3.3 Checkpoint outputs (all validated by `gate_checkpoint`)
1. **Status update** to the owner: done since the last checkpoint (task ids, commits, gate results at HEAD), remaining in this plan, remaining on the roadmap after this plan, open decisions awaiting the owner, rulings made in this block, parked items.
2. **Handoff document** `docs/superpowers/state/<plan>/handoff.md`: plan and spec paths; branch, worktree, HEAD and base shas; ledger path and resume line; register ids in flight with states; claim rows verified at HEAD; live subagents (none at a clean checkpoint; listed only when `gate_checkpoint` is run mid-block for diagnosis); open decision ids; rulings so far; environment facts (IDF env script, flash rule, OTA 35 s window, model map, grant id); traps found in this plan. The script validates every sha and id against git and the registers; a handoff that disagrees with the tree fails.
3. **Resume prompt**: verbatim, short: invoke the skill, read the handoff, run the gates at HEAD, continue at task id N.

### 3.4 Clean state
Sequence at block end: final gates at HEAD → final branch reviewer (opus) and adversarial verifier (Fable) clean → `gate_merge_ready` green → PR ready → every CI job green, advisory included → merge under the plan's grant (decision entry `grant:<plan>`) → post-merge: delete remote and local branch, remove every worktree the block created (`git worktree remove`, or rm + `git worktree prune` for trees with submodules), archive the block's scratch workspace into the main checkout's `.superpowers/`, regenerate state docs and commit, set register states to `merged`, run the gates on `main` → `gate_checkpoint` passes only when `git worktree list` shows main alone, `git status` is clean, no branch besides `main` and parked ones listed in the handoff with reason and owner, no open PR from this plan, no live subagent, no task between boundaries.
If a trigger fires mid-task, the task is finished through its review loop or reverted to the last task boundary (commit range dropped, register row back to `planned`, ledger note). "Mid-loop" is not a resumable state.

### 3.5 One block per context
After the checkpoint the session posts the status update and resume prompt and stops. It does not start the next block. The owner runs `/clear` and pastes the resume prompt. A "continue" without a clear is refused by the skill. After a clear: skill first, handoff second, gate run third, then the next dispatch.

### 3.6 Sub-controllers
Same triggers and outputs; a sub-controller's handoff is merged into the umbrella handoff by the session before the session's own checkpoint.

## 4. Data: the registers

Location: `docs/superpowers/state/` (project-wide files) and `docs/superpowers/state/<plan>/` (per plan: ledger, handoff, block grants). Format: YAML, parsed by the gate library; the schema is the gate suite's fixtures.

### 4.1 `findings.yml`
One entry per finding; the only place a finding exists.
```yaml
- id: A2-IO-N2
  source: audit-2026-10-16            # audit | review | bench | agent | issue
  severity: HIGH
  class: >-                            # reviewer's statement verbatim, never shortened
    One CRC-bad FIX_DELTA is dropped by the reader and every later delta is
    applied to the un-advanced anchor until the next FIX_KEY (<= 5 s) ...
  siblings: [CRC-bad FIX_KEY, CRC-bad LAP, CRC-bad VENUE, truncated tail]
  cites: [{file: components/core/session/ses_frame.c, lines: "60-80", sha: 7265059}]
  repro: .superpowers/reviews/2026-10-15/core-io-ui-verify-repro-n2-rekey.c
  owner: {kind: plan, plan: 2026-10-xx-prebench, task: 7}   # or {kind: issue, number: 140} or {kind: roadmap, plan: 8}
  state: planned        # open | planned | in-progress | fixed-unpinned | pinned | deferred | closed | merged
  pin: {test: test_export_crc_bad_delta_fails_the_export, transcript: docs/superpowers/state/<plan>/mutations/A2-IO-N2.txt}
  ruling: null          # decision id when narrowed or deferred
  evidence: []          # commits, review report paths
  history: [{state: open, sha: 7265059, by: audit}, ...]
```
Gates: every id in any audit or review report exists here; every entry has an owner; transitions are legal (`pinned` needs test + transcript + reviewer confirmation; `deferred` needs an owner decision id; `closed` needs evidence; `merged` set only by the post-merge script).

### 4.2 `claims.yml`
One entry per factual statement in a tracked document.
```yaml
- id: C-spec-14.1-laptiming
  doc: docs/superpowers/specs/2026-09-14-lap-timer-design.md
  anchor: "§14.1"
  text: "VBO carries [laptiming] Start when a layout was locked"
  direct: "test/build/test_exp_vbo --case laptiming_when_locked"
  contra: "export a locked-layout session; a VBO without [laptiming] falsifies the claim"
  cites: [components/core/export/exp.c, components/app/cmd/cmd.c]
  verified_at: null
  by: null
  state: unverified     # unverified | verified | stale | false
  finding: A2-IO-N4     # required when state == false
```
Gates: a dispatch depending on a document blocks if any of that document's rows the brief cites is not `verified` at HEAD; a commit touching a cited file marks dependent rows `stale`; a `false` row must point to a finding or a doc-fix commit.

### 4.3 `decisions.yml`
```yaml
- id: DEC-0007
  class: triage            # triage | spec-direction | scope | merge-grant | hardware | model-policy | issue-close | finding-class
  question: "A2 §6 bucket A: accept 8 rows as proposed, widen with P-1 list, or re-bucket?"
  options:
    - {label: accept, cost: "pre-bench plan 8 code tasks"}
    - {label: widen, cost: "~18 code tasks before bench day"}
  asked_at: {sha: 5d95a53, by: session}
  answer: null
  answered_at: null
```
Open ids block any dispatch, plan or brief that references them. Only the owner answers. Controller rulings are not here; they live in the plan ledger with cost-if-wrong and are listed at the checkpoint.

### 4.4 Exception register
`docs/power-of-10-deviations.md` stays the single exception mechanism, extended per §9. The grace list file and its linter flag are deleted.

### 4.5 Derived views
Roadmap owner table, close-out lists, open-finding counts in roadmap and README, each plan's "findings this plan closes" header: generated by `gate_state_docs`, never hand-edited; drift fails CI.

### 4.6 Bootstrap
The existing backlog (audit-1 residuals, audit-2's 169 findings, the drift analysis's 160 untracked items, the 120 claim rows and every factual sentence in spec, roadmap, README, bench doc, FW design, P10 docs) is loaded by an audit-style job: reviewers write rows, verifiers run them; nothing enters as `verified` or `pinned` without a run. Triage proposals become decision entries for the owner.

## 5. Roles, models, dispatch contract

| Role | Minimum model | Lifetime | Writes |
|---|---|---|---|
| Session (umbrella controller) | opus (or the session's model) | one block per context | ledger, handoff, decision questions, dispatches, merge on grant |
| Sub-controller | opus | one block | same, scoped |
| Implementer | sonnet; haiku only when the brief carries complete code | one task | code, tests, mutation transcript, report |
| Task reviewer | opus | one review | review report with executed claims |
| Final branch reviewer | opus | one branch | report |
| Final adversarial verifier | Fable | one branch | report |
| Exception (P10) reviewer | Fable | one row | adversarial cost report |
| Audit reviewer / verifier | opus | one area | reports, reproducers, register rows |
| Investigator (read-only) | opus | one question | report |

No role but a controller dispatches subagents. A denied permission is reported verbatim; no agent asks another to perform a denied action.

**Dispatch contract** (composed by the controller from the skill's prompt-block files; `gate_dispatch` refuses anything missing):
1. Brief path; exact values live only there.
2. Register ids the task closes, with class statements and sibling lists copied from the register.
3. Claim rows the task depends on, all `verified` at HEAD.
4. The role's strict block: verification method (direct + contradiction for every claim the agent makes or reads), zero-bug rule, no subagents, report path and contract, token/time budget, model, "write a decision question and stop on any owner-only decision".
5. Parked findings in the touched area, by register query.
6. Reviewers additionally: review package path, mutation transcript path, the gate command list to execute, and the instruction that every claim in the implementer's report, commit messages and diff comments is a hypothesis to execute.
No pasted session history; a size cap on the prompt enforces it.

**Reports:** full report to the file; return ≤10 lines: status, commits, gate results, claims executed (count), findings raised (ids), denials, budget used. Statuses: DONE, DONE_WITH_FINDINGS, NEEDS_DECISION (decision id), BLOCKED, DENIED.

**Controller rulings:** only inside an approved plan; only plan-vs-spec or task-vs-task conflicts the spec settles; at most 5 per block; ledgered as `Ruling: what — why — cost if wrong — spec cite`; listed at the checkpoint for the owner. Anything else is a decision question and a stop.

**Decision classes (owner-only, always):** audit triage buckets; spec-vs-code direction; plan scope (what enters, what defers); merge grants; anything touching hardware; model and cost policy; closing an issue; any ruling that narrows or widens a finding's class.

## 6. The change pipeline

1. **Intake.** The change exists as a register entry (finding, requirement with claim rows, or maintenance item) before any work. The dispatch hook checks.
2. **Claim verification.** Every document the change depends on has its cited claim rows `verified` at HEAD. Divergence → D5.
3. **Plan or micro-plan.** Plans through writing-plans with added header blocks (register ids per task, sibling lists, pinning step, gate list, block boundaries). A micro-plan (one-line fix) carries the same fields in the brief, no plan file. `gate_plan` prints the file-overlap table and the id-coverage table (each routed id in exactly one task; every id from the source report somewhere) and fails on placeholders.
4. **Dispatch** through the hook (§5).
5. **Implement, TDD.** Failing test first through the production call path; mutation transcript; every sibling tested or filed as its own row; full local gate run (host suite, dc-host, tools tests, P10 full reach, venue lint, spec-api check, all firmware builds at zero warnings, DRAM budget, comment pattern gate); report with each claim marked by the command that proves it.
6. **Task review, executed.** Reviewer re-runs the gate set clean and the mutation; executes every claim in report, commits and diff comments (direct + contradiction); checks siblings against the class; runs a contradiction pass on the new surface ("assume the fix is wrong for input X"); files new findings as register rows (from this change → task incomplete; unrelated → investigation dispatch + owner). Verdict needs spec ✅, quality ✅, claims-executed ✅.
7. **Fix loop.** Rounds 1–3 resume the implementer; 4–5 a fresh agent one tier up; a round limit hit is a decision question, not a parked finding.
8. **Block end** (§3.4).
9. **Post-merge audit hook.** Files changed by a merged block become coverage rows for the next audit.

## 7. Gate scripts (`tools/gates/`)

Common contract: one script, one concern; exit 0/1; output a table of named lines (`PASS id` / `FAIL id: reason`); `--json`; a missing input is a FAIL, never "nothing to do"; shared parsing in `tools/gates/lib/`; every script ships fixtures that must fail and must pass; the gate-test suite is a required CI job.

| Gate | Checks | Invoked by |
|---|---|---|
| `gate_register` | schema; every id in reports present; owner on every row; legal transitions and their evidence | pre-commit, CI, dispatch hook |
| `gate_claims` | schema; rows cited by a brief `verified` at HEAD; stale marking on cited-file change; `false` rows resolved | pre-commit, CI, dispatch hook |
| `gate_decisions` | no reference to an unanswered id; owner-only classes never answered by an agent (answer author = owner) | dispatch hook, CI |
| `gate_plan` | header blocks; register ids per task; id-coverage table; file-overlap table; block size ≤3; placeholder scan | plan approval, dispatch hook |
| `gate_dispatch` | brief, ids, claim rows, role block, budget, model ≥ role minimum, report path; prompt size cap | PreToolUse Agent |
| `gate_review_package` | commit list, diff, mutation transcript showing the test failing on revert, gate outputs, sibling table, claims-executed table with commands | before reviewer dispatch; merge-readiness |
| `gate_p10` | extended linter (§9) on changed files locally, all files in CI; register-only exceptions; expired rows fail | implementer, reviewer, pre-commit, CI |
| `gate_comments` | comment pattern (plan/session/task/PR/issue/"later"/"lands in"/"until Plan N"/"wired in") must resolve to a register id or open issue | pre-commit, CI |
| `gate_state_docs` | regenerates roadmap/README/FW-status facts and derived tables; fails on drift | CI, post-merge |
| `gate_bench` | every bench row has a runnability transcript or `[hardware-only]`; hardware-only rows never cited as a pin | pre-commit on the bench doc, CI |
| `gate_merge_ready` | PR body free of unresolved markers (`fix before merge`, `draft`, `parked`, `TODO`, `not for merge`); every register row for the PR `pinned`/`closed`/`deferred`-with-decision; claim rows verified; both final reports clean; every CI job green incl. advisory; grant present; tag rule (§11) | PreToolUse on `gh pr ready`/`gh pr merge`, pre-push |
| `gate_checkpoint` | handoff complete and consistent with git and registers; worktrees = main; status clean; no open PR from the plan; no live subagent; ledger has the block's rulings list | Stop hook, resume path |
| `gate_tools_py` | tools unit tests; type check; non-zero exit on any failure path | CI, pre-commit |

## 8. Hooks and workflows

**Claude Code hooks** (`.claude/settings.json`, committed; scripts in `tools/gates/hooks/`):
- `PreToolUse` Agent: `gate_dispatch`, `gate_claims`, `gate_decisions`, model map; exit 2 with the failing lines.
- `PreToolUse` Bash matching `git commit|git push|gh pr ready|gh pr merge|git merge|git worktree`: `gate_register`, `gate_claims`, `gate_comments`, `gate_p10` on changed files, `gate_merge_ready` for PR verbs, grant check for merge.
- `Stop`: `gate_checkpoint`; returns missing fields.
- `SessionStart`: prints whether the gates are installed and the handoff path if one exists; installs git hooks if missing.
- `UserPromptSubmit`: reminder only.

**Git hooks** (`tools/gates/install-hooks.sh`): `pre-commit` → register/claims/comments/p10 on staged files; `pre-push` → merge-readiness when the branch has a PR.

**CI** (`.github/workflows/gates.yml`, required): the gate suite and the gate-script tests; existing jobs continue; devcontroller lint blocking; any job that cannot find its input fails.

**Workflows** (`.claude/workflows/`, run only when the skill says so, model per role explicit, refuse without a grant and a `gate_plan`-clean plan):
- `audit.js`: areas → reviewers → verifiers → synthesis; coverage lists and incremental progress files; outputs register rows; a killed agent resumes from its progress file.
- `block.js`: per task implementer → `gate_review_package` → reviewer → fix rounds (≤5 with escalation) → next; then integration task, final reviewer + Fable verifier, merge-readiness, merge-on-grant, cleanup, checkpoint.
Fallback: the skill runs the same sequence with the Agent tool; the hooks make skipping a step impossible either way.

## 9. Power of 10: reach and exceptions

**Reach** (`tools/lint/power_of_10.py`, every tree; host tests may declare a per-file rule-3 allowance):
- Rule 1: call-graph recursion (indirect included); no `goto`/`longjmp`.
- Rule 2: every loop statically bounded or bound-asserted; `for (;;)` only in registered task entries.
- Rule 3: heap census incl. `heap_caps_*`, `*_prefer`, `memalign`, `alloca`, `strdup`/`strndup`/`asprintf`, `#define` wrappers; only listed pre-init sites.
- Rule 4: statement-counted lengths.
- Rule 5: assertion density per function, excluding literal tautologies and `#if 0` / `#ifdef NEVER`.
- Rules 6, 7, 8, 10: as the compliance design, now outside core too.
- Rule 9: declarators, by-value sinks, struct fields, typedef'd params and fields; the pointed-to set printed and enumerable.
- Probe fixtures per rule (negative must FAIL); the audit-1/2 probes are the first fixtures.

**Exceptions** (`docs/power-of-10-deviations.md`): columns id, rule, site, tier, compliant alternative, why rejected, cost score with breakdown, benefit score with breakdown, adversarial report path, owner, expiry sha. Tier A (SDK contract): allowed by category, one row per site, pointed-to set listed. Tier B: benefit must exceed cost (equality refuses); adversarial Fable reviewer argues against; expired rows fail the linter; every audit re-argues every row; a deviation row cannot land in the same PR as the code it excuses without the adversarial report.

Cost scale: blast radius 1–3 (tooling 1, UI 2, timing/data-integrity/supervision 3) + verifiability lost 0–2 + reach 0–2 (transient single site 0, stored or ≥3 sites 2). Benefit scale: complexity avoided 1–3 (helper 1, new module 2, new task/queue or ≥60 lines 3) + measured runtime gain 0–2 with the measurement cited. Owner-tunable in `tools/gates/config.yml`.

## 10. Audits, state documents, bench rows, comments

**Audits:** brief fixes the method (hypothesis per statement, direct + contradiction, reproducers against real production files, harness stubs under `test/harness/`); reviewer coverage list first (call paths incl. callers, claims, reproducers) and a progress file per finding; verifier coverage list includes "paths the reviewer omitted" and re-runs every reproducer; findings enter the register verbatim; synthesis adds proposals and cross-references, never edits a class statement; triage becomes decision entries. Scheduled after every merged plan for its new surface; full audit on the owner's call.

**State documents:** facts generated by `gate_state_docs`; remaining hand-written facts carry claim rows; a state commit lands only after the event it records.

**Bench rows:** only with a runnability transcript (verb exists on the named build, path compiled in, observable printed) stored next to the row; `[hardware-only]` rows cannot pin or close a finding; results written back by the same script.

**Comments:** pattern gate; intent comments name a register id or issue, or go.

## 11. Open choices for the owner (answer in review)

1. Skill name: `strict-delivery` (default) or another.
2. Tag rule: enforce `<plan>-b<N>` on every merged block via `gate_merge_ready`, or delete the rule from the roadmap.
3. Trigger defaults (§3.1) and exception scales (§9): accept or tune.
4. Register format: YAML (default) or Markdown tables.
5. Audit cadence: per merged plan automatically (default) or on call only.

## 12. `CLAUDE.md` content (project overrides)

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

## 13. Validation and rollout

**Validation:** (1) gate fixtures that must fail (package without mutation transcript; PR body "fix before merge"; brief citing an unregistered id; stale claim row; comment "(plan 06)"; `esp_console` callback; a required job with its input deleted; a handoff whose sha differs from HEAD) and must pass; gate-test suite required in CI from its first commit. (2) Skill text by the writing-skills method: baseline scenarios without the skill (reviewer handed `test_sim_profile` and a "permanent test" claim; controller handed a "fix before merge" PR called Minor; controller handed an audit triage and told to get on with it; implementer handed plan text contradicting the spec), rationalisations recorded verbatim, then with the skill, loopholes closed, re-run; scenarios and transcripts under `tools/gates/skill-tests/`. (3) Dry run: one throwaway finding end to end through hooks, mutation, executed review, Fable verifier, merge-on-grant, cleanup, checkpoint, `/clear`, resume.

**Rollout** (each step a block = PR, gated by the gates that already exist):
1. Gate library, schemas, `gate_register`, `gate_claims`, `gate_decisions`, their tests; registers bootstrapped empty.
2. Hooks (Claude Code, git, CI job) wired to those three; `CLAUDE.md`; skill skeleton with dispatch contract and prompt blocks.
3. `gate_dispatch`, `gate_review_package`, `gate_plan`, `gate_checkpoint`; handoff format; checkpoint flow.
4. P10 reach, probe fixtures, register scoring, grace list removal (violations surfaced become register rows, not inline fixes).
5. `gate_comments`, `gate_state_docs`, `gate_bench`, `gate_tools_py`, `gate_merge_ready`.
6. Workflows `audit.js`, `block.js`.
7. Skill red/green tests; dry run.
8. Bootstrap load (§4.6); owner answers the decision entries.
9. Conformance sweep: bring the current tree to green on every gate (P10 full reach, comments, state docs, bench rows, claim rows, tools exit codes), run through the pipeline itself, every violation a register row with an owner; the first fix effort starts from a tree that passes.

Steps 1–9 precede any product fix.

## 14. Risks and costs

More agent seats per change (executed reviews, Fable verifier per block); the owner runs `/clear` after every block; P10 full reach will flag existing code (routed to the register); the claim bootstrap is audit-sized; hooks add the gate runtime to commits (seconds for text gates, minutes when firmware builds are in the changed set); a bug in a gate blocks all work until fixed, so the gate suite's own tests are the first required check; Fable seats draw on the shared weekly limit (plan-level only, roughly +5 % per plan at a 2× weight). Nothing here changes product behaviour.
