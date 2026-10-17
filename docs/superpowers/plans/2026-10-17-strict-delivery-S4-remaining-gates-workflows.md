# Strict Delivery S4: Remaining Gates, Post-merge Automation, Workflows

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This is sub-plan S4 of the umbrella `docs/superpowers/plans/2026-10-17-strict-delivery.md`; the umbrella's Shared interfaces section is binding and is not restated here except where S4 adds to it.

**Goal:** Ship the last five gates (`gate_comments`, `gate_tools_py`, `gate_bench`, `gate_state_docs`, `gate_merge_ready`), the git pre-push hook, the PR-verb branch of the Claude Code Bash hook, the post-merge script, and the two Workflow scripts (`audit.js`, `block.js`), each pinned by fixture tests, with the current tree green on every new gate.

**Architecture:** Every gate follows the umbrella Gate contract (`check(args) -> Result`, `main(argv) -> run_gate(...)`). Text gates (`comments`, `bench`) read tracked files plus the findings register; `gate_tools_py` executes the tools' unit tests, `compileall` and a fixture-driven exit-code harness; `gate_state_docs` rewrites `<!-- gen:<name> -->…<!-- /gen -->` regions from git, GitHub and the registers and fails on drift; `gate_merge_ready` reads the PR through `gh` and judges it. `postmerge.py` runs the spec §3.4 sequence step by step and stops at the first FAIL. The workflows are pure orchestration: every shell action is executed by a runner agent, and every gate is a command the script cannot skip.

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6), `python3 -m unittest`, bash, `gh` CLI, Claude Code hooks, Claude Code Workflow scripts (plain JavaScript), Node.js (`node --check`) for the workflow lint.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (APPROVED 2026-10-17): §3.4 clean state, §7 gates table, §8 hooks + workflows, §10 state docs / bench rows / comments, §11.2 tag rule ENFORCED (`<plan>-b<N>`).

## Global Constraints

- Umbrella Global Constraints apply verbatim (block = PR ≤ 3 tasks, all-green merge under `grant:strict-delivery` = `DEC-0001`, tags `strict-delivery-b10/b11/b12`, model map, zero warnings, every Python script non-zero on every failure path, a missing input is a FAIL).
- Gate output lines are `PASS <id>` / `FAIL <id>: <reason>`; ids are prefixed by the gate name (`comments.`, `tools_py.`, `bench.`, `state_docs.`, `merge_ready.`).
- `gh` failures are `GateError` (→ `FAIL <gate>.input`), never "assume open", never "skip".
- Comment patterns are case-sensitive Python regexes from `tools/gates/config.yml::comment_patterns`.
- Block branches are named `block/<plan>-b<N>` (e.g. `block/strict-delivery-b10`); tags are `<plan>-b<N>`; a branch never shares a name with a tag.
- Plan files that `gate_merge_ready` reads mark blocks with `## Block b<N>` and tasks with `### Task <n>:` (this file follows it; S5 must too).
- No source in this plan writes a comment or docstring that matches a `comment_patterns` entry (the gate added in Task 1 would fail on it). Strings in code are not comments and are fine.
- Nothing in this plan flashes, opens a serial port, or writes to GitHub from a subagent; `gh` writes (`pr create`, `pr ready`, `pr merge`) run only from the session or from `postmerge.py` launched by the session.

## Review Focus

1. **`gh` unreachable or unauthenticated during a gate.** Expected: FAIL, not PASS. Pinned by `test_gate_comments.py::test_gh_failure_is_fail` (Task 1) and `test_gate_state_docs.py::test_gh_failure_fails` (Task 4).
2. **A merge with an advisory job red or a marker in the PR body.** Expected: `gate_merge_ready` FAIL. Pinned by `test_gate_merge_ready.py::test_advisory_red_fails` and `::test_body_marker_fails` (Task 5).
3. **A hardware-only (or no-transcript) bench item used as evidence for a finding.** Expected: FAIL. Pinned by `test_gate_bench.py::test_hardware_only_item_cited_as_pin_fails` (Task 3).
4. **`postmerge.py` interrupted or re-run.** Expected: it stops at the first FAIL with later steps not printed, and a re-run after a partial success passes the steps already done (tag already on the merge commit). Pinned by `test_postmerge.py::test_stops_at_first_fail` and `::test_tag_already_on_merge_commit_passes` (Task 6).
5. **A tool that exits 0 on failure gets fixed silently, or a new tool lands with no exit-code case.** Expected: an expected-fail case that starts passing FAILs until the register row moves; an uncovered `tools/**/*.py` FAILs. Pinned by `test_gate_tools_py.py::test_expected_fail_now_fixed_fails` and `::test_uncovered_script_fails` (Task 2).

---

## Repo facts verified for this plan (2026-10-17, branch `pre-bench-wrap-up`, HEAD 24aa0c7, `main` = 7265059)

| Fact | Where verified |
|---|---|
| The stale-comment CI step is `.github/workflows/ci.yml` lines 22–74 (step name line 22, `exit "$bad"` line 74, blank 75, `host-tests:` line 76); its grep pattern is case-sensitive and adds `arrives in [0-9]`, `stubbed until`, `stub until`, `not in plan [0-9]`, `Plan 04 ships no display` | `sed -n 22,76p` |
| The CI tools unittest list is `ci.yml` lines 133–140: `tools/test_devkit.py tools/lint/test_power_of_10.py tools/lint/test_venue_announce.py tools/test_spec_api_check.py tools/tracks/test_gen_tracks.py` | `sed -n 125,145p` |
| Tracked Python tools (non-test): `tools/devkit.py`, `tools/ota_push.py`, `tools/spec_api_check.py`, `tools/lint/power_of_10.py`, `tools/lint/venue_announce.py`, `tools/tracks/gen_tracks.py`, `tools/fonts/gen_fonts.py` | `git ls-files 'tools/*.py'` |
| Known-bad invocations today: `spec_api_check.py --spec /nonexistent.md` rc 1; `power_of_10.py --paths /nonexistent_dir --fail-on-violation` **rc 0** (prints `totals: … =0`); `venue_announce.py --paths /nonexistent_dir --fail-on-violation` rc 1; `gen_tracks.py /nonexistent.json -o x` rc 1; `ota_push.py --port /dev/nonexistent --image /nonexistent.bin` rc 2; `devkit.py --port /dev/nonexistent status` rc 2; `gen_fonts.main()` with `TTF_PATH` patched missing → `SystemExit('missing TTF: …')` | executed |
| DC-N3 (audit 2026-10-16 line 419): `tools/devkit.py:373` appends `--json`; `:464` returns `0 if obj.get("rc") == 0`; `cmd_lt.c:530` sets `rc = (total > 0) ? 0 : …`; `:518` truncates the body at `LT_JSON_BODY_MAX` (512). `_cmd_lt(FakeSerial(reply), args)` returns **0** for an echoed unknown verb, an `ERR 0x0703` body, and a 512-byte truncated body | executed with a scratch FakeSerial |
| Comment-pattern hits on the tree with the umbrella patterns + `arrives in \d` + `stub(bed)? until`, case-sensitive, comments only (C `//` `/* */`, `#` in py/sh/yml, `<!-- -->` in md): **65** sites in 37 files (2 in `ci.yml` vanish with the step; `pipeline.c:541` cites `#93`, `pipeline.c:1816` cites `#123`, both OPEN) | scratch scanner |
| Bench doc: `## Open items` at line 15, next `## ` heading at line 581 (`## Closed on 2026-10-10`); 72 rows `- [ ]`/`- [x]` in between (62 unticked, 10 ticked), ids `1`…`71` and `F0/#102`, all unique | scratch parser |
| Roadmap facts: line 5 `` `main` = e0defd3) ``; lines 39–42 the nine required checks; lines 271–277 fix-wave status cells (F4 line 277 still reads `OPEN — this branch …`, false: PR #138 MERGED 2026-10-15 → 7265059); line 282 `**moto_sim 66,472 B, moto_neo6m 66,640 B**`; line 300 heading `37 open on 2026-10-15` (GitHub: **36** open); lines 301–304 claim the hand table is "the single source" | `sed`, `gh issue list`, `gh pr view` |
| PR merges: #101 2026-10-13 a390151; #113 2026-10-14 18c5eac; #121 2026-10-14 430b4bc; #122 2026-10-14 9395a2f; #126 2026-10-14 f38d8a6; #127 2026-10-15 e0defd3; #138 2026-10-15 7265059 | `gh pr view --json mergedAt,mergeCommit` |
| README lines 76–79: protection sentence with the nine checks | `sed -n 70,84p README.md` |
| `docs/measurements.md` line 8 `## Static DRAM after R33 …`, table header line 13 with a `Free static DRAM` column, rows 15–16 | `sed -n 1,20p` |
| Branch protection: required contexts readable without admin at `gh api repos/superbrobenji/lap-timer/branches/main` → `.protection.required_status_checks.contexts`; `/protection` says `strict: true`, `enforce_admins: true`, `required_linear_history: true` (so no direct push to `main`, and a PR is always up to date with `main` when it merges) | `gh api` |
| `.superpowers/` and `.claude/worktrees/` are git-ignored; `.superpowers/sdd/` exists | `.gitignore` |
| Local toolchain: Python 3.9.6, PyYAML 6.0.3, Node v24.18.1 | executed |

**Facts that contradict the umbrella or spec (handled here, flagged for the owner):**
- Spec §3.4 / the S4 brief say post-merge "regenerate state docs and commit" on `main`. `main` refuses direct pushes (required checks + `enforce_admins`). Per the umbrella correction of 2026-10-17: `gate_state_docs --write` is the block branch's last commit before the final review; there is no `main-sha` region; on `main`, gh/tag-derived drift WARNs and only file/register-derived drift FAILs; `postmerge.py` leaves the `merged` row edit and one ledger line uncommitted for the next block branch.
- The brief orders "tag" after "state docs regen". With no regeneration in `postmerge.py`, the tag comes right after syncing `main` and before any cleanup.
- Review packages live git-ignored under `.superpowers/sdd/<plan>/review/<task>/` (umbrella correction 2026-10-17). The umbrella CLI table still shows `gate_review_package --dir docs/superpowers/state/<plan>/review/<task>`; `block.js` uses the `.superpowers/sdd/…` path and keeps the command in one constant.
- Umbrella hook protocol: "`git merge` → merge_ready + grant". Block integration merges task branches with `git merge` into the block branch, where a PR-level gate cannot pass yet. The hook here blocks `git merge` only when the current branch is `main` (main moves only by `gh pr merge`) and lets it through on any other branch.
- Spec §8 "pre-push → merge-readiness when the branch has a PR". The commits being pushed have no check runs yet, so the pre-push mode (`--pre-push`) checks every static rule and leaves out the check-run rule. It runs only when the PR is no longer a draft.
- Spec §7 `gate_tools_py` lists a "type check". No type checker is pinned in `tools/requirements.txt` and the brief leaves it out. S4 doesn't ship one; it's an open item for the owner (a new dependency is a scope decision).
- Gates that land on a tree which already violates them (65 comment sites, 72 bench rows, the `power_of_10.py` and `devkit.py` exit codes) would fail at every commit until the conformance sweep (spec rollout step 9, S5). S4 records each class as one register row owned by the umbrella's conformance work and marks each site with that id, so the gate passes today and the sweep has an exact work list. The bench gate gets one extra marker for this, `[no-transcript: <finding-id>]`, which the brief did not list.

## File structure (S4 creates or modifies exactly these)

```
.github/workflows/ci.yml                         Modify: delete lines 22-74 (stale-comment step)         T1
.github/workflows/gates.yml                      Modify: fetch-depth 0, GH_TOKEN, new gate names          T1,T2,T3,T4
tools/gates/config.yml                           Modify: +2 comment patterns, comment_exclude             T1
tools/gates/lib/gitinfo.py                       Modify: append S4 helpers                                T1,T4
tools/gates/run_all.py                           Modify: GATES += comments,bench,tools_py,state_docs,merge_ready  T1-T5
tools/gates/gate_comments.py                     Create                                                   T1
tools/gates/gate_tools_py.py                     Create                                                   T2
tools/gates/gate_bench.py                        Create                                                   T3
tools/gates/gate_state_docs.py                   Create                                                   T4
tools/gates/gate_merge_ready.py                  Create                                                   T5
tools/gates/hooks/pr_verbs.py                    Create (PR-verb branch of the Bash hook)                 T5
tools/gates/hooks/pre_tool_use.py                Modify: call pr_verbs from the Bash branch               T5
tools/gates/hooks/git-pre-push.sh                Create                                                   T5
tools/gates/install-hooks.sh                     Modify: install pre-push                                 T5
tools/gates/postmerge.py                         Create (no commit on main; leaves 2 files pending)       T6
tools/gates/tests/s4kit.py                       Create (fixture repos, register rows, fake gh)           T1
tools/gates/tests/fixtures/fakegh/gh             Create (executable fake gh)                              T1
tools/gates/tests/fixtures/comments/*            Create                                                   T1
tools/gates/tests/fixtures/tools_rc/{cases.yml,call_runner.py,*.txt}  Create                              T2
tools/gates/tests/fixtures/bench/*               Create                                                   T3
tools/gates/tests/test_gate_comments.py          Create                                                   T1
tools/gates/tests/test_gate_tools_py.py          Create                                                   T2
tools/gates/tests/test_gate_bench.py             Create                                                   T3
tools/gates/tests/test_gate_state_docs.py        Create                                                   T4
tools/gates/tests/test_gate_merge_ready.py       Create                                                   T5
tools/gates/tests/test_hooks_s4.py               Create                                                   T1,T5
tools/gates/tests/test_postmerge.py              Create                                                   T6
tools/gates/tests/test_workflows.py              Create                                                   T9
tools/gates/README.md                            Modify: gates table rows, Workflows section               T1-T9
.claude/workflows/audit.js                       Create                                                   T7
.claude/workflows/block.js                       Create                                                   T8
docs/superpowers/plans/2026-09-14-roadmap.md     Modify: gen markers                                      T4
README.md                                        Modify: gen marker                                       T4
docs/bench/next-bench-day.md                     Modify: one marker per open row                          T3
docs/superpowers/state/findings.yml              Modify: S4 rows, conformance rows                        T1-T9
docs/superpowers/state/strict-delivery/mutations/SD-S4-T<n>.txt   Create                                  T1-T9
components/** main/** devcontroller/** test/** tools/** (comment lines only)  Modify: append SD-CMT-1     T1
```

`tools/gates/tests/s4kit.py` is the one file outside the umbrella's locked list: a test-support module (not `test_*.py`, so unittest discovery does not run it). Every S4 test imports it.

## Consumed from S1–S3 (exact names; verify each with `grep -n` before use, and correct this plan and ledger if one differs)

- `tools/gates/lib/__init__.py`: `Line`, `Result` (`.fail(id, reason)`, `.passed(id)`, `.ok()`, `.render(as_json)`), `GateError`, `repo_root()`, `run_gate(name, check, argv)`.
- `tools/gates/lib/registers.py`: `load_findings(root)`, `load_claims(root)`, `load_decisions(root)`, `finding_by_id(findings, id)`, `grant_for(decisions, plan)`, `validate_finding(f)`.
- `tools/gates/lib/config.py`: `load(root) -> dict`.
- `tools/gates/lib/gitinfo.py`: `head_sha(root)`, `branch(root)`, `status_clean(root)`, `worktrees(root)`, `changed_files(root, base, head="HEAD")`, `staged_files(root)`, `pr_for_branch(root)` (returns `None` when the branch has no PR), `tags_at(root, sha)`.
- `tools/gates/run_all.py`: `GATES`, `run(names, args) -> Result`, `main(argv)`; per-gate argv comes from `args.get(<name>, [])`.
- `tools/gates/hooks/pre_tool_use.py`: the Bash branch (find it with `grep -n "def " tools/gates/hooks/pre_tool_use.py`); the `git commit` branch runs register/claims/comments/p10 over staged files.
- `tools/gates/gate_register.py` (`python3 tools/gates/gate_register.py` with no argument validates the register), `gate_plan.py`, `gate_review_package.py`, `gate_checkpoint.py`, the review-package maker (S2), `gate_p10.py`.
- Every gate script begins with the same import header as `tools/gates/gate_register.py`. The code below uses this header; if S1's differs, copy S1's:

```python
import os
import sys
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
```

## Register rows for S4 (added in Task 1 Step 1)

Each task's work is a maintenance row (spec §6 step 1: the change exists as a register entry before any work). Rows move `planned → in-progress` at task start and `in-progress → pinned` at task end (pin = the task's mutation-pinned test + transcript). `postmerge.py` moves them to `merged`.

Save as `.superpowers/sdd/strict-delivery/rows-s4.yml` (git-ignored scratch). The `history` sha is filled by the register edit tool:

```yaml
- id: SD-S4-T1
  source: agent
  severity: MEDIUM
  class: >-
    No gate checks comments for forward-looking ledger wording against the findings register and open
    issues; the ci.yml step uses a hand-kept per-site allow-list and a narrower pattern set than spec §7.
  siblings: [staged-mode scan in the git pre-commit hook, md HTML comments, Python docstrings]
  cites: [{file: .github/workflows/ci.yml, lines: "22-74", sha: 7265059}]
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T1}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T1}]
- id: SD-S4-T2
  source: agent
  severity: MEDIUM
  class: >-
    No gate runs the tools unit tests, compiles every tools script, or proves that each tools script exits
    non-zero on a known-bad invocation.
  siblings: [cli tools, serial-relay tools called with a fake port, generator scripts without argv]
  cites: [{file: .github/workflows/ci.yml, lines: "125-145", sha: 7265059}]
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T2}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T2}]
- id: SD-S4-T3
  source: agent
  severity: MEDIUM
  class: >-
    No gate requires a runnability transcript or a hardware-only tag on bench rows, or forbids a
    hardware-only row as a pin or evidence.
  siblings: [ticked rows still in the open section, rows in the F2a/F2b subsections]
  cites: [{file: docs/bench/next-bench-day.md, lines: "15-580", sha: 7265059}]
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T3}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T3}]
- id: SD-S4-T4
  source: agent
  severity: MEDIUM
  class: >-
    Roadmap and README facts (main sha, open-issue count, required checks, DRAM, owner table, plan status)
    are hand-written and drift; the roadmap's line 5 sha, line 277 F4 status and line 300 count are false today.
  siblings: [README required-checks sentence, fix-wave table cells, owner table]
  cites: [{file: docs/superpowers/plans/2026-09-14-roadmap.md, lines: "5", sha: 7265059}, {file: README.md, lines: "76-79", sha: 7265059}]
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T4}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T4}]
- id: SD-S4-T5
  source: agent
  severity: HIGH
  class: >-
    Merge readiness is not machine-checked: no gate reads the PR body, every check run including advisory
    ones, the block's register rows, the final reports and the grant before gh pr ready or gh pr merge.
  siblings: [git pre-push, gh pr ready, gh pr merge, git merge on main]
  cites: []
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T5}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T5}]
- id: SD-S4-T6
  source: agent
  severity: MEDIUM
  class: >-
    The post-merge cleanup (tag, branches, worktrees, scratch archive, register merged, state docs, gates on
    main) is manual and was skipped or done out of order in past blocks.
  siblings: [tag already present, worktree with submodules, dirty worktree, state PR]
  cites: []
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T6}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T6}]
- id: SD-S4-T7
  source: agent
  severity: MEDIUM
  class: >-
    No scripted audit fan-out exists: coverage lists, per-reviewer adversarial verifiers and verbatim register
    capture depend on the controller remembering them.
  siblings: [killed reviewer resume, synthesis editing a class statement, duplicate ids across areas]
  cites: []
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T7}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T7}]
- id: SD-S4-T8
  source: agent
  severity: MEDIUM
  class: >-
    No scripted block loop exists: implementer, review package, executed review, fix rounds, final review,
    merge readiness, merge on grant, post-merge and checkpoint can be skipped or reordered.
  siblings: [PARALLEL tasks and integration, fix round 4 escalation, denied gh writes]
  cites: []
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T8}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T8}]
- id: SD-S4-T9
  source: agent
  severity: LOW
  class: >-
    The workflow scripts' shell commands are not checked against the gate CLIs S1-S3 shipped, nobody knows
    whether the PreToolUse Agent hook sees Workflow agent spawns, and the scripts are undocumented.
  siblings: [audit.js, block.js, tools/gates/README.md]
  cites: []
  repro: null
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, task: T9}
  state: planned
  pin: null
  ruling: null
  evidence: []
  history: [{state: planned, sha: "0", by: S4-T9}]
```

## Register edit tool (used by every task)

Save once as `.superpowers/sdd/strict-delivery/regedit.py` (git-ignored scratch, not committed). It keeps the leading comment block of `findings.yml`, stamps `history` with the real short HEAD sha, and runs `gate_register` after every edit:

```python
#!/usr/bin/env python3
"""Edit docs/superpowers/state/findings.yml without losing its leading comment block.

usage: regedit.py add <rows.yml>
       regedit.py set <ID> <STATE> <BY> [--pin-test T --pin-transcript P] [--evidence E ...]
"""
import argparse
import subprocess
import sys

import yaml

P = "docs/superpowers/state/findings.yml"


def load():
    with open(P, encoding="utf-8") as fh:
        text = fh.read()
    head = []
    for line in text.split("\n"):
        if line.startswith("#") or not line.strip():
            head.append(line)
        else:
            break
    return "\n".join(head).rstrip("\n"), (yaml.safe_load(text) or [])


def save(head, rows):
    body = yaml.safe_dump(rows, sort_keys=False, allow_unicode=True, width=100)
    with open(P, "w", encoding="utf-8") as fh:
        fh.write((head + "\n" if head else "") + body)


def main():
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd", required=True)
    a1 = sub.add_parser("add")
    a1.add_argument("rows")
    a2 = sub.add_parser("set")
    a2.add_argument("id")
    a2.add_argument("state")
    a2.add_argument("by")
    a2.add_argument("--pin-test")
    a2.add_argument("--pin-transcript")
    a2.add_argument("--evidence", action="append", default=[])
    a = ap.parse_args()
    head, rows = load()
    sha = subprocess.run(["git", "rev-parse", "--short", "HEAD"], capture_output=True, text=True,
                         check=True).stdout.strip()
    if a.cmd == "add":
        with open(a.rows, encoding="utf-8") as fh:
            new = yaml.safe_load(fh)
        have = {r["id"] for r in rows}
        dup = [r["id"] for r in new if r["id"] in have]
        if dup:
            sys.exit("already in the register: %s" % dup)
        for r in new:
            r["history"] = [{"state": r["state"], "sha": sha, "by": r["history"][0]["by"]}]
        rows.extend(new)
    else:
        hit = [r for r in rows if r["id"] == a.id]
        if len(hit) != 1:
            sys.exit("%s: %d rows" % (a.id, len(hit)))
        r = hit[0]
        r["state"] = a.state
        if a.pin_test:
            r["pin"] = {"test": a.pin_test, "transcript": a.pin_transcript}
        r["evidence"].extend(a.evidence)
        r["history"].append({"state": a.state, "sha": sha, "by": a.by})
    save(head, rows)
    rc = subprocess.run([sys.executable, "tools/gates/gate_register.py"]).returncode
    if rc != 0:
        sys.exit("gate_register FAILED after the edit -- fix the row, do not commit")


if __name__ == "__main__":
    main()
```

## Mutation transcript recipe (every task's pin)

The task's named test is run three times and recorded in `docs/superpowers/state/strict-delivery/mutations/SD-S4-T<n>.txt` in the umbrella format (`finding:`, `test:`, `step 1 (fix present): <cmd> -> PASS`, `step 2 (fix reverted: <how>): <cmd> -> FAIL`, `step 3 (fix restored): <cmd> -> PASS`). "Reverted" is the exact one-line `sed` given in the task, applied to the committed file, and restored with `git checkout -- <file>`. The transcript is committed with the register `pinned` edit.

---

## Block b10 — `gate_comments`, `gate_tools_py`, `gate_bench` (tag `strict-delivery-b10`, branch `block/strict-delivery-b10`)

Tasks 1–3 are SERIAL (all three edit `run_all.py`, `gates.yml`, `README.md` and `findings.yml`).

### Task 1: `gate_comments` + fake `gh` kit + delete the ci.yml allow-list step

**Register:** SD-S4-T1 (closes), SD-CMT-1 (creates, owner = conformance). **Siblings:** staged mode; md HTML comments; Python docstrings; `#` comments in sh/yml. **Gates:** `run_all.py`, gate-test suite.

**Files:**
- Create: `tools/gates/gate_comments.py`, `tools/gates/tests/s4kit.py`, `tools/gates/tests/fixtures/fakegh/gh`, `tools/gates/tests/fixtures/comments/{plan06.c,owned_same_line.c,owned_next_line.c,string_literal.c,doc.py,wf.yml,notes.md}`, `tools/gates/tests/test_gate_comments.py`, `tools/gates/tests/test_hooks_s4.py`
- Modify: `tools/gates/config.yml`, `tools/gates/lib/gitinfo.py` (append), `tools/gates/run_all.py` (`GATES`), `.github/workflows/ci.yml:22-74` (delete), `.github/workflows/gates.yml`, `tools/gates/README.md`, every file the gate flags (comment lines only)

**Interfaces:**
- Consumes: `Result`, `GateError`, `repo_root`, `run_gate`; `registers.load_findings`; `config.load`; `gitinfo.staged_files`.
- Produces:
  - `gitinfo._s4_cmd(root, cmd: list) -> str`, `gitinfo.gh_json(root, args: list) -> object`, `gitinfo.issue_state(root, number: int, cache: dict) -> str`.
  - `gate_comments.check(args) -> Result` with argv `[] | ["--staged"] | ["--files", p, ...]`; `gate_comments.extract(path: str, text: str) -> dict[int, str]` (line → comment text).
  - `s4kit.make_repo(files, findings, claims, decisions, branch) -> str`, `s4kit.commit(root, files, msg) -> sha`, `s4kit.write(root, files)`, `s4kit.sh(root, *cmd) -> str`, `s4kit.env(root, gh: dict) (context manager; yields the fake gh call-log path)`, `s4kit.run_main(main, argv) -> (rc, stdout)`, `s4kit.finding(...)`, `s4kit.claim(...)`, `s4kit.grant(...)`.
  - Fake gh map format: `{"<argv prefix>": {"stdout": <str or JSON value>, "stderr": str, "rc": int, "sh": "<shell run first>"}}`; the longest key that equals the argv or prefixes it (at a space) wins; an unmapped call exits 1.

- [ ] **Step 1: Branch, scratch tools, register rows**

```bash
git switch main && git pull --no-rebase --ff-only && git switch -c block/strict-delivery-b10
mkdir -p .superpowers/sdd/strict-delivery docs/superpowers/state/strict-delivery/mutations
# write regedit.py and rows-s4.yml from the two sections above into .superpowers/sdd/strict-delivery/
python3 .superpowers/sdd/strict-delivery/regedit.py add .superpowers/sdd/strict-delivery/rows-s4.yml
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T1 in-progress S4-T1
```
Expected: no output from `add`/`set` except gate_register's PASS lines; rc 0.

- [ ] **Step 2: Write the fake `gh`**

`tools/gates/tests/fixtures/fakegh/gh` (commit with the executable bit: `git add --chmod=+x`):

```python
#!/usr/bin/env python3
"""Fake gh for the gate tests: answers from the JSON map named by FAKE_GH_MAP, logs to FAKE_GH_LOG."""
import json
import os
import subprocess
import sys

argv = " ".join(sys.argv[1:])
log = os.environ.get("FAKE_GH_LOG")
if log:
    with open(log, "a", encoding="utf-8") as fh:
        fh.write(argv + "\n")
with open(os.environ["FAKE_GH_MAP"], encoding="utf-8") as fh:
    table = json.load(fh)
keys = [k for k in table if argv == k or argv.startswith(k + " ")]
if not keys:
    sys.stderr.write("fakegh: no mapping for: %s\n" % argv)
    sys.exit(1)
entry = table[max(keys, key=len)]
if entry.get("sh"):
    subprocess.run(entry["sh"], shell=True, check=True)
out = entry.get("stdout", "")
sys.stdout.write(out if isinstance(out, str) else json.dumps(out))
sys.stderr.write(entry.get("stderr", ""))
sys.exit(entry.get("rc", 0))
```

- [ ] **Step 3: Write `s4kit.py`**

`tools/gates/tests/s4kit.py`:

```python
"""Test support for the S4 gate tests: throwaway git repos, register rows, a fake gh on PATH."""
import contextlib
import io
import json
import os
import shutil
import subprocess
import tempfile

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))
FIXTURES = os.path.join(HERE, "fixtures")
FAKE_GH_DIR = os.path.join(FIXTURES, "fakegh")
OWNER = "superbrobenji"


def sh(root, *cmd):
    p = subprocess.run(list(cmd), cwd=root, capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise AssertionError("%s: %s" % (" ".join(cmd), p.stderr.strip()))
    return p.stdout.strip()


def finding(fid, state="open", owner=None, pin=None, evidence=None, siblings=None):
    return {"id": fid, "source": "agent", "severity": "LOW", "class": "fixture finding %s" % fid,
            "siblings": siblings or [], "cites": [], "repro": None,
            "owner": owner or {"kind": "roadmap", "plan": 8}, "state": state, "pin": pin,
            "ruling": None, "evidence": evidence or [],
            "history": [{"state": state, "sha": "0000000", "by": "fixture"}]}


def claim(cid, doc, cites, state="verified"):
    return {"id": cid, "doc": doc, "anchor": "s1", "text": "fixture claim", "direct": "true",
            "contra": "false", "cites": cites, "verified_at": "0000000" if state == "verified" else None,
            "by": "fixture", "state": state, "finding": None}


def grant(plan, answer="yes", did="DEC-0001"):
    return {"id": did, "class": "merge-grant", "question": "grant:%s" % plan, "options": [],
            "asked_at": {"sha": "0000000", "by": "fixture"}, "answer": answer,
            "answered_at": {"sha": "0000000", "by": OWNER} if answer is not None else None}


def write(root, files):
    for rel, content in files.items():
        path = os.path.join(root, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb" if isinstance(content, bytes) else "w") as fh:
            fh.write(content)


def make_repo(files=None, findings=None, claims=None, decisions=None, branch="main"):
    root = tempfile.mkdtemp(prefix="s4gate-")
    sh(root, "git", "init", "-q", "-b", "main")
    sh(root, "git", "config", "user.email", "fixture@example.invalid")
    sh(root, "git", "config", "user.name", "fixture")
    sh(root, "git", "config", "commit.gpgsign", "false")
    os.makedirs(os.path.join(root, "tools", "gates"))
    shutil.copy(os.path.join(REPO, "tools", "gates", "config.yml"),
                os.path.join(root, "tools", "gates", "config.yml"))
    state = os.path.join(root, "docs", "superpowers", "state")
    os.makedirs(state)
    for name, rows in (("findings.yml", findings), ("claims.yml", claims), ("decisions.yml", decisions)):
        with open(os.path.join(state, name), "w") as fh:
            yaml.safe_dump(rows or [], fh, sort_keys=False)
    write(root, dict({".gitignore": ".superpowers/\n"}, **(files or {})))
    sh(root, "git", "add", "-A")
    sh(root, "git", "commit", "-qm", "fixture base")
    if branch != "main":
        sh(root, "git", "checkout", "-qb", branch)
    return root


def commit(root, files, msg="fixture change"):
    write(root, files)
    sh(root, "git", "add", "-A")
    sh(root, "git", "commit", "-qm", msg)
    return sh(root, "git", "rev-parse", "HEAD")


@contextlib.contextmanager
def env(root, gh=None):
    """CLAUDE_PROJECT_DIR=root and a fake gh first on PATH (an empty map: every gh call fails)."""
    saved = dict(os.environ)
    tmp = tempfile.mkdtemp(prefix="fakegh-")
    mapping = os.path.join(tmp, "map.json")
    with open(mapping, "w") as fh:
        json.dump(gh or {}, fh)
    os.environ["CLAUDE_PROJECT_DIR"] = root
    os.environ["FAKE_GH_MAP"] = mapping
    os.environ["FAKE_GH_LOG"] = os.path.join(tmp, "calls.log")
    os.environ["PATH"] = FAKE_GH_DIR + os.pathsep + saved.get("PATH", "")
    try:
        yield os.environ["FAKE_GH_LOG"]
    finally:
        os.environ.clear()
        os.environ.update(saved)


def run_main(main, argv):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = main(argv)
    return rc, buf.getvalue()


def calls(log_path):
    if not os.path.exists(log_path):
        return []
    with open(log_path) as fh:
        return [l.rstrip("\n") for l in fh if l.strip()]
```

- [ ] **Step 4: Write the comment fixtures**

`tools/gates/tests/fixtures/comments/plan06.c` (the spec §13 must-fail fixture):
```c
/* The menu entry is wired later (plan 06). */
int menu_entries = 3;
```
`owned_same_line.c`:
```c
/* The power task lands in Plan 6 (SD-FIX-1). */
int power_state = 0;
```
`owned_next_line.c`:
```c
/* The real I2C driver lands with the
 * IMU work owned by #129. */
int imu = 0;
```
`string_literal.c`:
```c
const char *msg = "this lands in 3.4 and is a string, not a comment";
```
`doc.py`:
```python
"""Module docstring: the export lands in 3.4."""
X = "plan 06 inside a string is not a comment"  # session 2 hash comment
```
`wf.yml`:
```yaml
on: push
# until Plan 8 delivers the driver
jobs: {}
```
`notes.md`:
```markdown
Plain prose about plan 06 is not a comment.
<!-- the table lands in 3.4 -->
```

- [ ] **Step 5: Write the failing tests**

`tools/gates/tests/test_gate_comments.py`:

```python
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402
from tools.gates import gate_comments  # noqa: E402

FIX = os.path.join(s4kit.FIXTURES, "comments")


def fixture(name):
    with open(os.path.join(FIX, name)) as fh:
        return fh.read()


def gh_issue(state):
    return {"issue view": {"stdout": {"state": state}}}


class CommentsGateTest(unittest.TestCase):
    def run_gate(self, files, findings=None, gh=None, argv=None):
        root = s4kit.make_repo(files=files, findings=findings)
        with s4kit.env(root, gh) as log:
            rc, out = s4kit.run_main(gate_comments.main, argv or [])
            return rc, out, s4kit.calls(log)

    def test_plan_06_fixture_fails(self):
        rc, out, _ = self.run_gate({"src/plan06.c": fixture("plan06.c")})
        self.assertEqual(rc, 1)
        self.assertIn("FAIL comments.src/plan06.c:1", out)

    def test_register_id_same_line_passes(self):
        rc, out, _ = self.run_gate({"src/a.c": fixture("owned_same_line.c")},
                                   findings=[s4kit.finding("SD-FIX-1")])
        self.assertEqual(rc, 0, out)

    def test_unknown_register_id_fails(self):
        rc, out, _ = self.run_gate({"src/a.c": fixture("owned_same_line.c")})
        self.assertEqual(rc, 1)
        self.assertIn("FAIL comments.src/a.c:1", out)

    def test_open_issue_on_next_line_passes(self):
        rc, out, calls = self.run_gate({"src/b.c": fixture("owned_next_line.c")}, gh=gh_issue("OPEN"))
        self.assertEqual(rc, 0, out)
        self.assertIn("issue view 129 --json state", calls)

    def test_closed_issue_fails(self):
        rc, out, _ = self.run_gate({"src/b.c": fixture("owned_next_line.c")}, gh=gh_issue("CLOSED"))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL comments.src/b.c:1", out)

    def test_gh_failure_is_fail(self):
        rc, out, _ = self.run_gate({"src/b.c": fixture("owned_next_line.c")},
                                   gh={"issue view": {"rc": 1, "stderr": "HTTP 401"}})
        self.assertEqual(rc, 1)
        self.assertIn("FAIL comments.input", out)

    def test_issue_lookup_is_cached_per_run(self):
        text = fixture("owned_next_line.c")
        rc, out, calls = self.run_gate({"src/b.c": text, "src/c.c": text}, gh=gh_issue("OPEN"))
        self.assertEqual(rc, 0, out)
        self.assertEqual(sum(1 for c in calls if c.startswith("issue view 129")), 1)

    def test_string_literal_is_not_a_comment(self):
        rc, out, _ = self.run_gate({"src/s.c": fixture("string_literal.c")})
        self.assertEqual(rc, 0, out)

    def test_python_docstring_and_hash_comment_hit(self):
        rc, out, _ = self.run_gate({"tools/doc.py": fixture("doc.py")})
        self.assertEqual(rc, 1)
        self.assertIn("FAIL comments.tools/doc.py:1", out)
        self.assertIn("FAIL comments.tools/doc.py:2", out)

    def test_yaml_hash_comment_hits(self):
        rc, out, _ = self.run_gate({".github/workflows/wf.yml": fixture("wf.yml")})
        self.assertIn("FAIL comments..github/workflows/wf.yml:2", out)

    def test_markdown_only_html_comments_count(self):
        rc, out, _ = self.run_gate({"docs/notes.md": fixture("notes.md")})
        self.assertIn("FAIL comments.docs/notes.md:2", out)
        self.assertNotIn("docs/notes.md:1", out)

    def test_staged_mode_scans_only_the_index(self):
        root = s4kit.make_repo(files={"src/ok.c": "int a;\n"})
        s4kit.write(root, {"src/new.c": fixture("plan06.c"), "src/ok.c": fixture("plan06.c")})
        s4kit.sh(root, "git", "add", "src/new.c")
        with s4kit.env(root):
            rc, out = s4kit.run_main(gate_comments.main, ["--staged"])
        self.assertEqual(rc, 1)
        self.assertIn("src/new.c:1", out)
        self.assertNotIn("src/ok.c", out)

    def test_excluded_fixture_dir_is_not_scanned(self):
        rc, out, _ = self.run_gate({"tools/gates/tests/fixtures/comments/plan06.c": fixture("plan06.c")})
        self.assertEqual(rc, 0, out)

    def test_missing_patterns_is_fail(self):
        root = s4kit.make_repo(files={"src/a.c": "int a;\n"})
        s4kit.write(root, {"tools/gates/config.yml": "owner: {handle: x}\n"})
        with s4kit.env(root):
            rc, out = s4kit.run_main(gate_comments.main, [])
        self.assertEqual(rc, 1)
        self.assertIn("FAIL comments.input", out)

    def test_ci_allow_list_step_is_gone(self):
        with open(os.path.join(s4kit.REPO, ".github", "workflows", "ci.yml")) as fh:
            ci = fh.read()
        self.assertNotIn("No stale forward-looking comments", ci)
        self.assertNotIn("allow=(", ci)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 6: Run the tests to verify they fail**

Run: `python3 -m unittest tools/gates/tests/test_gate_comments.py -v`
Expected: ERROR `ImportError: cannot import name 'gate_comments'` for the module; `test_ci_allow_list_step_is_gone` is not reached.

- [ ] **Step 7: Config additions**

Append to the `comment_patterns` list in `tools/gates/config.yml` (the old ci.yml grep carried these two and the umbrella list does not):
```yaml
  - 'arrives in \d'
  - 'stub(bed)? until'
```
Add a top-level key after `comment_patterns`:
```yaml
comment_exclude:    # path prefixes never scanned: the gate's own must-fail fixtures
  - 'tools/gates/tests/fixtures/'
```

- [ ] **Step 8: gitinfo helpers**

Append to `tools/gates/lib/gitinfo.py` (add `import json` / `import subprocess` / `from tools.gates.lib import GateError` at the top if S1's file lacks them):

```python
# ---- S4 helpers -------------------------------------------------------------------------------

def _s4_cmd(root, cmd):
    """Run cmd in root; return stdout; GateError carrying stderr on a non-zero exit."""
    p = subprocess.run(cmd, cwd=root, capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise GateError("%s: %s" % (" ".join(cmd), (p.stderr or p.stdout).strip()))
    return p.stdout


def gh_json(root, args):
    """Run `gh <args>` and parse its stdout as JSON."""
    out = _s4_cmd(root, ["gh"] + list(args))
    try:
        return json.loads(out)
    except ValueError as e:
        raise GateError("gh %s: output is not JSON: %s" % (" ".join(args), e))


def issue_state(root, number, cache):
    """'OPEN' or 'CLOSED' for issue #number; `cache` is the caller's per-run memo."""
    if number not in cache:
        data = gh_json(root, ["issue", "view", str(number), "--json", "state"])
        if not isinstance(data, dict) or "state" not in data:
            raise GateError("gh issue view %d: no state field" % number)
        cache[number] = data["state"]
    return cache[number]
```

- [ ] **Step 9: Write `gate_comments.py`**

```python
#!/usr/bin/env python3
"""gate_comments -- forward-looking ledger wording in a comment must name its owner (spec sections 7, 10).

A comment line that matches config.comment_patterns passes only when that line or the next one
carries a findings-register id that exists, or #<n> of an issue that is OPEN on GitHub. Comments
are: C/C++ // and /* */, Python # comments and docstrings, # comments in shell and YAML, and
<!-- --> in Markdown. String literals are not comments.

usage: gate_comments.py [--json] [--staged | --files PATH ...]
"""
import argparse
import io
import os
import re
import subprocess
import sys
import tokenize

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from tools.gates.lib import GateError, Result, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as cfgmod  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

EXTS = (".c", ".h", ".py", ".sh", ".yml", ".md")
ID_RE = re.compile(r"\b[A-Z][A-Z0-9]*(?:-[A-Za-z0-9]+)+\b")
ISSUE_RE = re.compile(r"(?<![\w&])#(\d+)\b")
HASH_RE = re.compile(r"(?:^|\s)(#.*)$")


def _add(out, line, text):
    out[line] = out.get(line, "") + text


def c_comments(text):
    out, line, i, n, mode = {}, 1, 0, len(text), None
    while i < n:
        ch = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if mode is None:
            if ch == "/" and nxt == "/":
                mode, i = "line", i + 2
                continue
            if ch == "/" and nxt == "*":
                mode, i = "block", i + 2
                continue
            if ch in "\"'":
                mode = ch
        elif mode == "line":
            if ch == "\n":
                mode = None
            else:
                _add(out, line, ch)
        elif mode == "block":
            if ch == "*" and nxt == "/":
                mode, i = None, i + 2
                continue
            if ch != "\n":
                _add(out, line, ch)
        else:
            if ch == "\\":
                if nxt == "\n":
                    line += 1
                i += 2
                continue
            if ch == mode or ch == "\n":
                mode = None
        if ch == "\n":
            line += 1
        i += 1
    return out


def py_comments(text):
    out, prev = {}, None
    try:
        toks = list(tokenize.generate_tokens(io.StringIO(text).readline))
    except (tokenize.TokenError, SyntaxError, IndentationError) as e:
        raise GateError("tokenize: %s" % e)
    for t in toks:
        if t.type == tokenize.COMMENT:
            _add(out, t.start[0], t.string)
        elif t.type == tokenize.STRING and (prev is None or prev.type in (
                tokenize.NEWLINE, tokenize.INDENT, tokenize.DEDENT)):
            for k, piece in enumerate(t.string.split("\n")):
                _add(out, t.start[0] + k, piece)
        if t.type not in (tokenize.NL, tokenize.COMMENT):
            prev = t
    return out


def hash_comments(text):
    out = {}
    for i, raw in enumerate(text.split("\n"), 1):
        m = HASH_RE.search(raw)
        if m:
            out[i] = m.group(1)
    return out


def md_comments(text):
    out = {}
    for m in re.finditer(r"<!--(.*?)-->", text, re.S):
        first = text.count("\n", 0, m.start()) + 1
        for k, piece in enumerate(m.group(1).split("\n")):
            _add(out, first + k, piece)
    return out


def extract(path, text):
    if path.endswith((".c", ".h")):
        return c_comments(text)
    if path.endswith(".py"):
        return py_comments(text)
    if path.endswith((".sh", ".yml")):
        return hash_comments(text)
    if path.endswith(".md"):
        return md_comments(text)
    return {}


def _tracked(root):
    out = subprocess.run(["git", "ls-files", "--"] + ["*" + e for e in EXTS], cwd=root,
                         capture_output=True, text=True, check=False)
    if out.returncode != 0:
        raise GateError("git ls-files: %s" % out.stderr.strip())
    return [p for p in out.stdout.split("\n") if p]


def _index_text(root, path):
    p = subprocess.run(["git", "show", ":" + path], cwd=root, capture_output=True, check=False)
    if p.returncode != 0:
        return None
    return p.stdout.decode("utf-8", errors="replace")


def _disk_text(root, path):
    with open(os.path.join(root, path), encoding="utf-8", errors="replace") as fh:
        return fh.read()


def _owner(text, ids, root, cache):
    for m in ID_RE.finditer(text):
        if m.group(0) in ids:
            return m.group(0)
    for m in ISSUE_RE.finditer(text):
        n = int(m.group(1))
        if gitinfo.issue_state(root, n, cache) == "OPEN":
            return "#%d" % n
    return None


def check(args):
    ap = argparse.ArgumentParser(prog="gate_comments")
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--staged", action="store_true")
    g.add_argument("--files", nargs="+")
    a = ap.parse_args(args)
    root = repo_root()
    cfg = cfgmod.load(root)
    pats = cfg.get("comment_patterns")
    if not pats:
        raise GateError("config.comment_patterns missing or empty")
    rx = re.compile("|".join("(?:%s)" % p for p in pats))
    exclude = tuple(cfg.get("comment_exclude") or [])
    ids = {f["id"] for f in registers.load_findings(root)}
    if a.staged:
        paths, read = gitinfo.staged_files(root), _index_text
    elif a.files:
        paths, read = a.files, _disk_text
    else:
        paths, read = _tracked(root), _disk_text
    paths = sorted(p for p in paths if p.endswith(EXTS) and not p.startswith(exclude))
    r = Result("comments")
    if not paths:
        if a.staged:
            r.passed("comments.staged.none")
            return r
        raise GateError("no tracked file with a scanned extension")
    cache, hits, scanned = {}, 0, 0
    for p in paths:
        text = read(root, p)
        if text is None:
            continue
        scanned += 1
        raw = text.split("\n")
        for ln, com in sorted(extract(p, text).items()):
            m = rx.search(com)
            if not m:
                continue
            hits += 1
            near = raw[ln - 1] + "\n" + (raw[ln] if ln < len(raw) else "")
            who = _owner(near, ids, root, cache)
            if who is None:
                r.fail("comments.%s:%d" % (p, ln), "'%s' names no register id or open issue on this "
                       "line or the next" % m.group(0))
    r.passed("comments.scan: %d file(s), %d pattern hit(s)" % (scanned, hits))
    return r


def main(argv=None):
    return run_gate("comments", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 10: Run the tests**

Run: `python3 -m unittest tools/gates/tests/test_gate_comments.py -v`
Expected: every test PASS except `test_ci_allow_list_step_is_gone` (FAIL until Step 11).

- [ ] **Step 11: Delete the ci.yml step**

Delete `.github/workflows/ci.yml` lines 22–74 (from `      - name: No stale forward-looking comments` through `          exit "$bad"`), keep the blank line before `  host-tests:`. Verify:
```bash
sed -n 18,24p .github/workflows/ci.yml   # last hygiene step is "spec §5.2 API blocks ..." then blank, then "  host-tests:"
python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))" && echo yaml-ok
```
Run: `python3 -m unittest tools/gates/tests/test_gate_comments.py -v` → all PASS.

- [ ] **Step 12: Conformance: record and mark every existing hit**

Run `python3 tools/gates/gate_comments.py --json > .superpowers/sdd/strict-delivery/comments-before.json; echo rc=$?`. Expected: rc 1, 60 FAIL lines (65 hits − 2 ci.yml lines gone − 2 citing open #93/#123 − 1 whose next line cites an open issue; a dry run of this exact procedure on a clone at 24aa0c7 with the step still present gave 62 sites in 38 files and rc 0 afterwards). The gate's own list is the one that counts. Add the conformance row (save as `.superpowers/sdd/strict-delivery/rows-cmt.yml`):

```yaml
- id: SD-CMT-1
  source: agent
  severity: LOW
  class: >-
    Comments at every site marked SD-CMT-1 use forward-looking or ledger wording (a plan, session or task
    label, "lands in", "until Plan N", "later session", "wired in", "stub until") that names no owner;
    each is to be rewritten as a true statement or tied to an owning register row or open issue.
  siblings: [rewrite as a true statement, cite the owning finding id, cite an open issue, delete]
  cites: []
  repro: "python3 tools/gates/gate_comments.py with the SD-CMT-1 marks removed"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: [.superpowers/sdd/strict-delivery/comments-before.json]
  history: [{state: open, sha: "0", by: S4-T1}]
```
```bash
python3 .superpowers/sdd/strict-delivery/regedit.py add .superpowers/sdd/strict-delivery/rows-cmt.yml
python3 - <<'PY'
import json, re
lines = json.load(open(".superpowers/sdd/strict-delivery/comments-before.json"))["lines"]
sites = {}
for l in lines:
    m = re.match(r"comments\.(.+):(\d+)$", l["id"])
    if l["status"] == "FAIL" and m:
        sites.setdefault(m.group(1), []).append(int(m.group(2)))
for path, nums in sites.items():
    text = open(path, encoding="utf-8").read().split("\n")
    for n in nums:
        s = text[n - 1]
        cut = min([i for i in (s.find("*/"), s.find('"""'), s.find("'''")) if i >= 0] or [len(s)])
        text[n - 1] = s[:cut].rstrip() + " SD-CMT-1" + (" " + s[cut:] if cut < len(s) else "")
    open(path, "w", encoding="utf-8").write("\n".join(text))
print(sum(len(v) for v in sites.values()), "sites in", len(sites), "files")
PY
python3 tools/gates/gate_comments.py; echo rc=$?
git diff --stat | tail -1
git diff -U0 -- . ':!docs' ':!tools/gates' ':!.github' | grep '^[-+][^-+]' | cut -c2- | sed 's/ SD-CMT-1//' | sort | uniq -u | head
```
Expected: the gate rc 0; the last command prints nothing (every changed line differs from its old text only by ` SD-CMT-1`). If a mark landed outside a comment (the gate still fails on that line, or a build breaks), move it inside the comment by hand. Then run the host suite and the P10 gate on the changed files to prove the change is comment-only: `cmake --build test/build && ctest --test-dir test/build --output-on-failure` and `python3 tools/gates/run_all.py --only p10`.

- [ ] **Step 13: Wire into run_all, the commit hook, CI, README; hook test**

`tools/gates/run_all.py`: add `"comments"` to `GATES` in umbrella order (`... "p10", "comments", ...`). For the hook's staged run pass `["--staged"]` as the `comments` argv (S1's per-gate argv mapping: when the hook runs with staged files, `args["comments"] = ["--staged"]`). Confirm S1's `git commit` branch names `comments`: `grep -n comments tools/gates/hooks/pre_tool_use.py`; add it to that list if absent.

`.github/workflows/gates.yml`, gate-suite job: set `actions/checkout@v4` `with: {fetch-depth: 0}`, add `env: {GH_TOKEN: ${{ github.token }}}` to the job, and add `comments` to the run_all `--only` list if S1 used one.

`tools/gates/README.md` gates table, add:
```markdown
| `gate_comments` | comment lines matching `config.comment_patterns` must cite an existing register id or an OPEN issue on the same or next line; `--staged` scans the index | pre-commit, `git commit` hook, CI |
```

`tools/gates/tests/test_hooks_s4.py`:

```python
import json
import os
import subprocess
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402

HOOK = os.path.join(s4kit.REPO, "tools", "gates", "hooks", "pre_tool_use.py")


def hook(root, command, gh=None):
    payload = {"session_id": "t", "transcript_path": "/dev/null", "cwd": root, "permission_mode": "default",
               "hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "t1",
               "tool_input": {"command": command, "description": "test"}}
    with s4kit.env(root, gh):
        return subprocess.run([sys.executable, HOOK], input=json.dumps(payload), capture_output=True,
                              text=True, env=dict(os.environ), check=False)


class CommitHookCommentsTest(unittest.TestCase):
    def test_git_commit_with_staged_plan06_comment_is_blocked(self):
        root = s4kit.make_repo()
        with open(os.path.join(s4kit.FIXTURES, "comments", "plan06.c")) as fh:
            s4kit.write(root, {"src/plan06.c": fh.read()})
        s4kit.sh(root, "git", "add", "src/plan06.c")
        p = hook(root, "git commit -m x")
        self.assertEqual(p.returncode, 2, p.stderr)
        self.assertIn("FAIL comments.src/plan06.c:1", p.stderr)


if __name__ == "__main__":
    unittest.main()
```
Run: `python3 -m unittest tools/gates/tests/test_hooks_s4.py tools/gates/tests/test_gate_comments.py -v` → PASS. Run: `python3 tools/gates/run_all.py` → rc 0.

- [ ] **Step 14: Commit**

```bash
git add tools/gates .github/workflows docs/superpowers/state/findings.yml
git add -u    # the SD-CMT-1 marks
git add --chmod=+x tools/gates/tests/fixtures/fakegh/gh
git commit -m "gates: gate_comments replaces the ci.yml allow-list step; legacy sites marked SD-CMT-1 (SD-S4-T1)"
```

- [ ] **Step 15: Mutation transcript and pin**

Mutation: `sed -i.bak 's/            if who is None:/            if False:/' tools/gates/gate_comments.py` (the owner check made inert). Record in `docs/superpowers/state/strict-delivery/mutations/SD-S4-T1.txt`:
```
finding: SD-S4-T1
test: test_plan_06_fixture_fails
step 1 (fix present): python3 -m unittest tools.gates.tests.test_gate_comments.CommentsGateTest.test_plan_06_fixture_fails -> PASS
step 2 (fix reverted: sed 's/if who is None:/if False:/' tools/gates/gate_comments.py): python3 -m unittest tools.gates.tests.test_gate_comments.CommentsGateTest.test_plan_06_fixture_fails -> FAIL
step 3 (fix restored: git checkout -- tools/gates/gate_comments.py): python3 -m unittest tools.gates.tests.test_gate_comments.CommentsGateTest.test_plan_06_fixture_fails -> PASS
```
Run the three steps, confirm PASS / FAIL / PASS, remove the `.bak`, then:
```bash
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T1 pinned S4-T1 \
  --pin-test tools/gates/tests/test_gate_comments.py::CommentsGateTest::test_plan_06_fixture_fails \
  --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T1.txt
git add docs/superpowers/state && git commit -m "state: SD-S4-T1 pinned (mutation transcript)"
```

### Task 2: `gate_tools_py` + the exit-code harness

**Register:** SD-S4-T2 (closes); SD-TOOLS-1 (creates); DC-N3 (creates from audit 2026-10-16, verbatim). **Siblings:** cli tools; serial-relay tools called with a fake port; generators with no argv. **Gates:** `run_all.py`, gate-test suite.

**Files:**
- Create: `tools/gates/gate_tools_py.py`, `tools/gates/tests/fixtures/tools_rc/{cases.yml,call_runner.py,devkit_lt_echo_unknown.txt,devkit_lt_err_body.txt,devkit_lt_truncated.txt}`, `tools/gates/tests/test_gate_tools_py.py`
- Modify: `tools/gates/run_all.py`, `.github/workflows/gates.yml`, `tools/gates/README.md`, `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: Task 1's `s4kit`; `registers.load_findings`; `gitinfo.staged_files`.
- Produces: `gate_tools_py.check(args)` with argv `[] | ["--staged"] | ["--cases", path]`; `gate_tools_py.unittest_targets(root) -> list[str]`; `gate_tools_py.inventory(root) -> list[str]`; `gate_tools_py.run_case(root, case, cases_dir) -> (rc, tail)`; cases schema below.

Cases schema (`cases.yml`, a list): `id` (str), `kind` (`cli` | `call`), `script` (repo-relative path), `argv` (cli), `function` + `call_args` (list of positional args after the fake serial port) + optional `serial_reply` (file in the cases dir: the bytes the fake device sends) + optional `patch_path` (`{NAME: path}`, set as `pathlib.Path`) (call), optional `timeout_s` (default 60), optional `expected_fail` (a finding id: the case currently exits 0 and that row owns the defect).

- [ ] **Step 1: Register**

```bash
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T2 in-progress S4-T2
```
Save as `.superpowers/sdd/strict-delivery/rows-tools.yml` and add with `regedit.py add`:
```yaml
- id: DC-N3
  source: audit-2026-10-16
  severity: MEDIUM
  class: >-
    Relay rc is 0 whenever any byte arrived — and the lap-timer echoes every input byte — so
    `lt nosuchverb` and `ERR 0x…` replies return rc 0; with `--json` (always added by devkit.py) the
    body is cut at 512 B with rc 0 (tracks ≈1.1 KB, errlog ≥ 10 entries, coredump)
  siblings: [the relay's human form prints rc 0 the same way, devkit.py _cmd_lt trusts obj rc]
  cites:
    - {file: devcontroller/components/devconsole/cmd_lt.c, lines: "518", sha: 7265059}
    - {file: devcontroller/components/devconsole/cmd_lt.c, lines: "530", sha: 7265059}
    - {file: tools/devkit.py, lines: "373", sha: 7265059}
    - {file: tools/devkit.py, lines: "464", sha: 7265059}
  repro: tools/gates/tests/fixtures/tools_rc/cases.yml
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: [docs/reviews/2026-10-16-audit-post-fix-wave.md]
  history: [{state: open, sha: "0", by: S4-T2}]
- id: SD-TOOLS-1
  source: agent
  severity: MEDIUM
  class: >-
    tools/lint/power_of_10.py exits 0 when every --paths entry is missing (it scans nothing and reports
    zero violations under --fail-on-violation), so a renamed or deleted tree passes the lint silently.
  siblings: [one of several paths missing, a path that is a file not a directory]
  cites: [{file: tools/lint/power_of_10.py, lines: "944-1028", sha: 7265059}]
  repro: "python3 tools/lint/power_of_10.py --paths /nonexistent_dir --fail-on-violation; echo $?"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "0", by: S4-T2}]
```
(DC-N3's class is the audit's sentence copied verbatim; if S5's bootstrap later loads audit 2026-10-16, it reuses this row.) Before adding SD-TOOLS-1, re-run its repro: if S3 already made `power_of_10.py` exit non-zero there, drop the row and the `expected_fail` on its case, and ledger it.

- [ ] **Step 2: Write the harness fixtures**

`tools/gates/tests/fixtures/tools_rc/devkit_lt_echo_unknown.txt` (one line: what the dev-kit relay prints for an unknown verb; the echo is why `bytes` > 0):
```
{"cmd": "lt nosuchverb", "rc": 0, "attempts": 1, "rt_ms": 15012, "bytes": 46, "body": "nosuchverbUnrecognized command\nlt> "}
```
`devkit_lt_err_body.txt`:
```
{"cmd": "lt open 1 vbo", "rc": 0, "attempts": 1, "rt_ms": 900, "bytes": 40, "body": "open 1 vbo\nERR 0x0703: id exists\nlt> "}
```
`devkit_lt_truncated.txt` (body exactly 512 bytes, `bytes` 1730, no end marker; generate it rather than typing it):
```bash
python3 -c 'import json; print(json.dumps({"cmd": "lt errlog", "rc": 0, "attempts": 1, "rt_ms": 15020, "bytes": 1730, "body": "errlog" + "x" * 506}))' > tools/gates/tests/fixtures/tools_rc/devkit_lt_truncated.txt
```

`tools/gates/tests/fixtures/tools_rc/call_runner.py`:
```python
#!/usr/bin/env python3
"""Run one `call` case of the tools exit-code harness in a fresh interpreter.

argv: <case JSON> <repo root>. Imports the tool by path, applies patch_path, builds a fake serial
port from serial_reply, calls the function and exits with what a CLI would: its int return, a
SystemExit code (a string message counts as 1), or 1 for any other exception.
"""
import importlib.util
import json
import os
import pathlib
import sys


class FakeSerial:
    def __init__(self, rx):
        self.rx = rx
        self.timeout = 1.0
        self.written = bytearray()

    @property
    def in_waiting(self):
        return len(self.rx)

    def write(self, data):
        self.written.extend(data)
        return len(data)

    def flush(self):
        pass

    def read(self, n=1):
        out, self.rx = self.rx[:n], self.rx[n:]
        return out

    def readline(self):
        i = self.rx.find(b"\n")
        if i < 0:
            out, self.rx = self.rx, b""
            return out
        out, self.rx = self.rx[:i + 1], self.rx[i + 1:]
        return out


def exit_code(value):
    if isinstance(value, bool) or value is None:
        return 0 if not value else 1
    if isinstance(value, int):
        return value
    return 1


def main():
    case, root = json.loads(sys.argv[1]), sys.argv[2]
    path = os.path.join(root, case["script"])
    sys.path.insert(0, os.path.dirname(path))
    spec = importlib.util.spec_from_file_location("tool_under_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    for name, value in (case.get("patch_path") or {}).items():
        setattr(mod, name, pathlib.Path(value))
    args = list(case.get("call_args") or [])
    if case.get("serial_reply_abs"):
        with open(case["serial_reply_abs"], "rb") as fh:
            args.insert(0, FakeSerial(fh.read()))
    try:
        rc = exit_code(getattr(mod, case["function"])(*args))
    except SystemExit as e:
        rc = e.code if isinstance(e.code, int) else (0 if e.code is None else 1)
    except Exception as e:  # a CLI would die with a traceback: rc 1
        print("raised %s: %s" % (type(e).__name__, e), file=sys.stderr)
        rc = 1
    sys.exit(rc)


if __name__ == "__main__":
    main()
```

`tools/gates/tests/fixtures/tools_rc/cases.yml` (the production case list; every non-test `tools/**/*.py` outside `tools/gates/` needs at least one case):
```yaml
- id: spec_api_check.missing-spec
  kind: cli
  script: tools/spec_api_check.py
  argv: [--spec, /nonexistent-gate-probe/spec.md]
- id: power_of_10.missing-paths
  kind: cli
  script: tools/lint/power_of_10.py
  argv: [--paths, /nonexistent-gate-probe, --fail-on-violation]
  expected_fail: SD-TOOLS-1
- id: venue_announce.missing-paths
  kind: cli
  script: tools/lint/venue_announce.py
  argv: [--paths, /nonexistent-gate-probe, --fail-on-violation]
- id: gen_tracks.missing-input
  kind: cli
  script: tools/tracks/gen_tracks.py
  argv: [/nonexistent-gate-probe/track.json, -o, /nonexistent-gate-probe/out.h]
- id: ota_push.missing-port-and-image
  kind: cli
  script: tools/ota_push.py
  argv: [--port, /dev/nonexistent-gate-probe, --image, /nonexistent-gate-probe/x.bin]
- id: devkit.missing-port
  kind: cli
  script: tools/devkit.py
  argv: [--port, /dev/nonexistent-gate-probe, status]
- id: devkit.lt-relay-echoed-unknown-verb
  kind: call
  script: tools/devkit.py
  function: _cmd_lt
  serial_reply: devkit_lt_echo_unknown.txt
  call_args: [[nosuchverb]]
  expected_fail: DC-N3
- id: devkit.lt-relay-err-body
  kind: call
  script: tools/devkit.py
  function: _cmd_lt
  serial_reply: devkit_lt_err_body.txt
  call_args: [[open, "1", vbo]]
  expected_fail: DC-N3
- id: devkit.lt-relay-truncated-body
  kind: call
  script: tools/devkit.py
  function: _cmd_lt
  serial_reply: devkit_lt_truncated.txt
  call_args: [[errlog]]
  expected_fail: DC-N3
- id: gen_fonts.missing-ttf
  kind: call
  script: tools/fonts/gen_fonts.py
  function: main
  patch_path: {TTF_PATH: /nonexistent-gate-probe/font.ttf}
```

- [ ] **Step 3: Write the failing tests**

`tools/gates/tests/test_gate_tools_py.py`:
```python
import os
import sys
import unittest

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402
from tools.gates import gate_tools_py  # noqa: E402

CI = """jobs:
  tools-tests:
    steps:
      - uses: actions/checkout@v4
      - name: unit
        run: |
          python3 -m unittest -v \\
            tools/test_ok.py
"""
OK_TEST = "import unittest\nclass T(unittest.TestCase):\n    def test_ok(self):\n        self.assertTrue(True)\n"
BAD_TEST = OK_TEST.replace("assertTrue(True)", "assertTrue(False)")
GOOD_TOOL = "import sys, os\ndef main(argv=None):\n    argv = sys.argv[1:] if argv is None else argv\n    return 0 if os.path.exists(argv[0]) else 1\nif __name__ == '__main__':\n    sys.exit(main())\n"
BAD_TOOL = GOOD_TOOL.replace("return 0 if os.path.exists(argv[0]) else 1", "return 0")
RELAY_TOOL = "def handler(ser, verb):\n    line = ser.readline()\n    return 0\n"
CLI_CASE = {"id": "tool.missing", "kind": "cli", "script": "tools/tool.py", "argv": ["/nonexistent-probe"]}


def repo(tool=GOOD_TOOL, test=OK_TEST, cases=None, findings=None, extra=None):
    files = {".github/workflows/ci.yml": CI, "tools/test_ok.py": test, "tools/tool.py": tool,
             "tools/gates/tests/fixtures/tools_rc/cases.yml": yaml.safe_dump(cases or [CLI_CASE])}
    files.update(extra or {})
    return s4kit.make_repo(files=files, findings=findings)


def run(root, argv=None):
    with s4kit.env(root):
        return s4kit.run_main(gate_tools_py.main, argv or [])


class ToolsPyGateTest(unittest.TestCase):
    def test_clean_fixture_passes(self):
        rc, out = run(repo())
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS tools_py.unittest", out)
        self.assertIn("PASS tools_py.rc.tool.missing", out)

    def test_failing_unittest_fails(self):
        rc, out = run(repo(test=BAD_TEST))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL tools_py.unittest", out)

    def test_syntax_error_fails_compileall(self):
        rc, out = run(repo(extra={"tools/broken.py": "def f(:\n"},
                           cases=[CLI_CASE, dict(CLI_CASE, id="broken", script="tools/broken.py")]))
        self.assertIn("FAIL tools_py.compileall", out)

    def test_rc_zero_on_bad_args_fails(self):
        rc, out = run(repo(tool=BAD_TOOL))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL tools_py.rc.tool.missing: rc 0", out)

    def test_uncovered_script_fails(self):
        rc, out = run(repo(extra={"tools/other.py": GOOD_TOOL}))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL tools_py.rc.uncovered.tools/other.py", out)

    def test_expected_fail_with_open_row_passes(self):
        case = dict(CLI_CASE, expected_fail="SD-FIX-9")
        rc, out = run(repo(tool=BAD_TOOL, cases=[case], findings=[s4kit.finding("SD-FIX-9")]))
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS tools_py.rc.tool.missing.expected-fail.SD-FIX-9", out)

    def test_expected_fail_without_row_fails(self):
        rc, out = run(repo(tool=BAD_TOOL, cases=[dict(CLI_CASE, expected_fail="SD-FIX-9")]))
        self.assertEqual(rc, 1)
        self.assertIn("not in findings.yml", out)

    def test_expected_fail_now_fixed_fails(self):
        case = dict(CLI_CASE, expected_fail="SD-FIX-9")
        rc, out = run(repo(cases=[case], findings=[s4kit.finding("SD-FIX-9")]))
        self.assertEqual(rc, 1)
        self.assertIn("the defect is fixed", out)

    def test_expected_fail_on_closed_row_fails(self):
        case = dict(CLI_CASE, expected_fail="SD-FIX-9")
        rc, out = run(repo(tool=BAD_TOOL, cases=[case], findings=[s4kit.finding("SD-FIX-9", state="closed")]))
        self.assertEqual(rc, 1)

    def test_call_case_with_serial_reply_runs_the_function(self):
        case = {"id": "relay.zero", "kind": "call", "script": "tools/relay.py", "function": "handler",
                "serial_reply": "reply.txt", "call_args": ["x"]}
        rc, out = run(repo(extra={"tools/relay.py": RELAY_TOOL,
                                  "tools/gates/tests/fixtures/tools_rc/reply.txt": "{}\n"},
                           cases=[CLI_CASE, case]))
        self.assertIn("FAIL tools_py.rc.relay.zero: rc 0", out)

    def test_missing_unittest_step_fails(self):
        root = repo(extra={".github/workflows/ci.yml": "jobs: {tools-tests: {steps: []}}\n"})
        rc, out = run(root)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL tools_py.input", out)

    def test_staged_mode_without_tools_change_passes(self):
        rc, out = run(repo(), ["--staged"])
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS tools_py.staged.none-under-tools", out)


class RealTreeTest(unittest.TestCase):
    def test_real_cases_cover_every_tool(self):
        with s4kit.env(s4kit.REPO):
            inv = gate_tools_py.inventory(s4kit.REPO)
        with open(os.path.join(s4kit.REPO, gate_tools_py.CASES)) as fh:
            covered = {c["script"] for c in yaml.safe_load(fh)}
        self.assertEqual(sorted(set(inv) - covered), [])

    def test_devkit_relay_cases_still_exit_zero(self):
        with open(os.path.join(s4kit.REPO, gate_tools_py.CASES)) as fh:
            cases = [c for c in yaml.safe_load(fh) if c.get("expected_fail") == "DC-N3"]
        self.assertEqual(len(cases), 3)
        cases_dir = os.path.dirname(os.path.join(s4kit.REPO, gate_tools_py.CASES))
        for c in cases:
            rc, tail = gate_tools_py.run_case(s4kit.REPO, c, cases_dir)
            self.assertEqual(rc, 0, "%s: %s (DC-N3 fixed? update the register row and the case)" % (c["id"], tail))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Run the tests to verify they fail**

Run: `python3 -m unittest tools/gates/tests/test_gate_tools_py.py -v`
Expected: ImportError for `gate_tools_py`.

- [ ] **Step 5: Write `gate_tools_py.py`**

```python
#!/usr/bin/env python3
"""gate_tools_py -- the tools' Python is tested, compiles, and exits non-zero on failure (spec section 7).

Three checks: (1) the `python3 -m unittest tools/...` list of ci.yml's tools-tests job, run as CI
runs it; (2) `python3 -m compileall -q tools`; (3) the exit-code harness: every case in
tools/gates/tests/fixtures/tools_rc/cases.yml invokes a tool on a known-bad input and must exit
non-zero, unless it names an expected_fail finding that is still open; every non-test
tools/**/*.py outside tools/gates/ must have a case.

usage: gate_tools_py.py [--json] [--staged] [--cases PATH]
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile

import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from tools.gates.lib import GateError, Result, repo_root, run_gate  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

CI = ".github/workflows/ci.yml"
CASES = "tools/gates/tests/fixtures/tools_rc/cases.yml"
RUNNER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tests", "fixtures", "tools_rc",
                      "call_runner.py")
DONE_STATES = {"pinned", "closed", "merged"}


def _env():
    env = dict(os.environ)
    env["PYTHONPYCACHEPREFIX"] = tempfile.mkdtemp(prefix="tools-py-cache-")
    return env


def _tail(p, n=3):
    text = ((p.stderr or "") + "\n" + (p.stdout or "")).strip().split("\n")
    return " | ".join(l.strip() for l in text[-n:] if l.strip())


def unittest_targets(root):
    path = os.path.join(root, CI)
    if not os.path.isfile(path):
        raise GateError("%s missing" % CI)
    with open(path, encoding="utf-8") as fh:
        data = yaml.safe_load(fh)
    try:
        steps = data["jobs"]["tools-tests"]["steps"]
    except (KeyError, TypeError):
        raise GateError("%s: no jobs.tools-tests.steps" % CI)
    for s in steps or []:
        run = (s or {}).get("run") or ""
        if "python3 -m unittest" in run:
            targets = re.findall(r"tools/[\w/.-]+\.py", run)
            if targets:
                return targets
    raise GateError("%s: tools-tests has no 'python3 -m unittest tools/...' step" % CI)


def inventory(root):
    p = subprocess.run(["git", "ls-files", "--", "tools/*.py"], cwd=root, capture_output=True,
                       text=True, check=False)
    if p.returncode != 0:
        raise GateError("git ls-files: %s" % p.stderr.strip())
    return [f for f in p.stdout.split("\n") if f and not os.path.basename(f).startswith("test_")
            and not f.startswith("tools/gates/")]


def run_case(root, case, cases_dir):
    if case.get("kind") == "cli":
        cmd = [sys.executable, os.path.join(root, case["script"])] + [str(a) for a in case.get("argv") or []]
    elif case.get("kind") == "call":
        payload = dict(case)
        if case.get("serial_reply"):
            payload["serial_reply_abs"] = os.path.join(cases_dir, case["serial_reply"])
            if not os.path.isfile(payload["serial_reply_abs"]):
                raise GateError("case %s: serial_reply %s missing" % (case.get("id"), case["serial_reply"]))
        cmd = [sys.executable, "-I", RUNNER, json.dumps(payload), root]
    else:
        raise GateError("case %s: kind must be cli or call" % case.get("id"))
    try:
        p = subprocess.run(cmd, cwd=root, capture_output=True, text=True, check=False,
                           stdin=subprocess.DEVNULL, timeout=case.get("timeout_s", 60), env=_env())
    except subprocess.TimeoutExpired:
        return None, "timed out after %ss" % case.get("timeout_s", 60)
    return p.returncode, _tail(p)


def check(args):
    ap = argparse.ArgumentParser(prog="gate_tools_py")
    ap.add_argument("--staged", action="store_true")
    ap.add_argument("--cases", default=CASES)
    a = ap.parse_args(args)
    root = repo_root()
    r = Result("tools_py")
    if a.staged and not [p for p in gitinfo.staged_files(root) if p.startswith("tools/")]:
        r.passed("tools_py.staged.none-under-tools")
        return r
    targets = unittest_targets(root)
    missing = [t for t in targets if not os.path.isfile(os.path.join(root, t))]
    for t in missing:
        r.fail("tools_py.unittest.missing.%s" % t, "listed in %s but not in the tree" % CI)
    p = subprocess.run([sys.executable, "-m", "unittest"] + targets, cwd=root, capture_output=True,
                       text=True, check=False, timeout=1800, env=_env())
    if p.returncode == 0:
        r.passed("tools_py.unittest: %d module(s)" % len(targets))
    else:
        r.fail("tools_py.unittest", "rc %d: %s" % (p.returncode, _tail(p)))
    p = subprocess.run([sys.executable, "-m", "compileall", "-q", "tools"], cwd=root,
                       capture_output=True, text=True, check=False, env=_env())
    if p.returncode == 0:
        r.passed("tools_py.compileall")
    else:
        r.fail("tools_py.compileall", _tail(p, 5))
    cases_path = os.path.join(root, a.cases)
    if not os.path.isfile(cases_path):
        raise GateError("%s missing" % a.cases)
    with open(cases_path, encoding="utf-8") as fh:
        cases = yaml.safe_load(fh)
    if not isinstance(cases, list) or not cases:
        raise GateError("%s: expected a non-empty list of cases" % a.cases)
    covered = {c.get("script") for c in cases}
    for script in inventory(root):
        if script not in covered:
            r.fail("tools_py.rc.uncovered.%s" % script, "no exit-code case in %s" % a.cases)
    states = {f["id"]: f["state"] for f in registers.load_findings(root)}
    for case in cases:
        cid = "tools_py.rc.%s" % case.get("id")
        rc, tail = run_case(root, case, os.path.dirname(cases_path))
        exp = case.get("expected_fail")
        if rc is None:
            r.fail(cid, tail)
        elif exp is None:
            if rc != 0:
                r.passed(cid)
            else:
                r.fail(cid, "rc 0 on a known-bad invocation: %s" % tail)
        elif exp not in states:
            r.fail(cid, "expected_fail names %s, which is not in findings.yml" % exp)
        elif rc != 0:
            r.fail(cid, "now exits %d: the defect is fixed -- delete expected_fail and move %s forward"
                   % (rc, exp))
        elif states[exp] in DONE_STATES:
            r.fail(cid, "%s is %s but the tool still exits 0" % (exp, states[exp]))
        else:
            r.passed("%s.expected-fail.%s" % (cid, exp))
    return r


def main(argv=None):
    return run_gate("tools_py", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: Run the tests**

Run: `python3 -m unittest tools/gates/tests/test_gate_tools_py.py -v` → PASS (the two `RealTreeTest` cases need Pillow for `gen_fonts`: `pip3 install -r tools/requirements.txt` once). Run: `python3 tools/gates/gate_tools_py.py` on the real tree → rc 0, with `PASS tools_py.rc.devkit.lt-relay-*.expected-fail.DC-N3` (×3) and `PASS tools_py.rc.power_of_10.missing-paths.expected-fail.SD-TOOLS-1`.

- [ ] **Step 7: Wire in**

`run_all.py` `GATES`: add `"tools_py"` (umbrella order: last). The hook's staged argv for `tools_py` is `["--staged"]`. `gates.yml` gate-suite job: add a step before run_all: `pip3 install -r tools/requirements.txt` (Pillow for `gen_fonts`, pyserial for `devkit`) if S1's job does not install the whole file, and add `tools_py` to any `--only` list. README gates table:
```markdown
| `gate_tools_py` | ci.yml's tools unittest list; `compileall`; exit-code harness `tests/fixtures/tools_rc/cases.yml` (known-bad invocation must exit non-zero, or carry an open `expected_fail` row) | pre-commit when `tools/` is staged, CI |
```
Run: `python3 tools/gates/run_all.py` → rc 0.

- [ ] **Step 8: Commit, mutation, pin**

```bash
git add tools/gates .github/workflows/gates.yml docs/superpowers/state/findings.yml
git commit -m "gates: gate_tools_py with the tools exit-code harness; DC-N3 and SD-TOOLS-1 as expected fails (SD-S4-T2)"
```
Mutation: `sed -i.bak 's/^            elif exp is None:$/            elif False:/' tools/gates/gate_tools_py.py` (plain cases stop being judged; `test_rc_zero_on_bad_args_fails` then FAILs because no FAIL line is produced). Transcript `mutations/SD-S4-T2.txt` with test `test_rc_zero_on_bad_args_fails` (PASS / FAIL / PASS), then:
```bash
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T2 pinned S4-T2 \
  --pin-test tools/gates/tests/test_gate_tools_py.py::ToolsPyGateTest::test_rc_zero_on_bad_args_fails \
  --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T2.txt
git add docs/superpowers/state && git commit -m "state: SD-S4-T2 pinned (mutation transcript)"
```

### Task 3: `gate_bench`

**Register:** SD-S4-T3 (closes); SD-BENCH-1 (creates, owner = conformance). **Siblings:** ticked rows still in the open section; rows inside the `### F2a/F2b` subsections; `[no-transcript: …]` rows. **Gates:** `run_all.py`, gate-test suite.

**Files:**
- Create: `tools/gates/gate_bench.py`, `tools/gates/tests/fixtures/bench/{good.md,good_transcript.txt}`, `tools/gates/tests/test_gate_bench.py`
- Modify: `docs/bench/next-bench-day.md` (one marker on each of the 72 open-section rows), `tools/gates/run_all.py`, `.github/workflows/gates.yml`, `tools/gates/README.md`, `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: `s4kit`; `registers.load_findings`; `gitinfo.staged_files`.
- Produces: `gate_bench.check(args)` with argv `[] | ["--staged"]`; `gate_bench.rows(text) -> list[(slug, item_id, line_no, body)]`; `gate_bench.slug(item_id) -> str`; row grammar:
  - Row = a line `- [ ] **<id>. …` / `- [x] **<id>. …` / `- [ ] **<id> — …` inside `## Open items` (up to the next `## ` heading), plus every following line up to the next row or heading.
  - Evidence (one of): `[hardware-only]`; `[no-transcript: <finding-id>]` (finding exists, state not `closed`/`merged`); or `transcript: docs/bench/transcripts/<slug>.txt` **and** `observable: "<text>"`, where the transcript's first line is `$ <command>`, `<command>` appears in the row in backticks, and `<text>` appears in the transcript after line 1.
  - `slug("F0/#102") == "F0-102"`; a hardware-only or no-transcript item is cited by `bench:<slug>` or by its transcript path in no finding's `pin` or `evidence`.

- [ ] **Step 1: Register**

`regedit.py set SD-S4-T3 in-progress S4-T3`.

- [ ] **Step 2: Fixtures**

`tools/gates/tests/fixtures/bench/good.md`:
```markdown
# Bench

## Open items

- [ ] **1. Status prints the version.** Run `dbg status` on moto_sim.
  transcript: docs/bench/transcripts/1.txt observable: "ver v0.1.0"
- [ ] **2. Panel refresh.** Needs the real panel. [hardware-only]
- [x] **F0/#102 — DRAG card.** Done on hardware. [hardware-only]

## Closed on 2026-10-10

- [x] **9. Old item with no evidence.** Closed sections are not checked.
```
`good_transcript.txt`:
```
$ dbg status
boot 12 reset POWERON
ver v0.1.0 hw devkit_v1
```

- [ ] **Step 3: Write the failing tests**

`tools/gates/tests/test_gate_bench.py`:
```python
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402
from tools.gates import gate_bench  # noqa: E402

FIX = os.path.join(s4kit.FIXTURES, "bench")
DOC = "docs/bench/next-bench-day.md"


def text(name):
    with open(os.path.join(FIX, name)) as fh:
        return fh.read()


def run(doc=None, transcript=None, findings=None, argv=None):
    files = {DOC: text("good.md") if doc is None else doc}
    files["docs/bench/transcripts/1.txt"] = text("good_transcript.txt") if transcript is None else transcript
    root = s4kit.make_repo(files=files, findings=findings)
    with s4kit.env(root):
        return s4kit.run_main(gate_bench.main, argv or [])


class BenchGateTest(unittest.TestCase):
    def test_good_doc_passes(self):
        rc, out = run()
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS bench.1", out)
        self.assertIn("PASS bench.F0-102.hardware-only", out)

    def test_row_without_evidence_fails(self):
        rc, out = run(doc=text("good.md").replace(" [hardware-only]\n- [x]", "\n- [x]"))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL bench.2", out)

    def test_transcript_file_missing_fails(self):
        rc, out = run(doc=text("good.md").replace("transcripts/1.txt", "transcripts/1b.txt"))
        self.assertIn("FAIL bench.1", out)

    def test_first_line_not_the_rows_command_fails(self):
        rc, out = run(transcript="$ dbg mem\nver v0.1.0\n")
        self.assertIn("FAIL bench.1", out)
        self.assertIn("not named in the row", out)

    def test_observable_absent_fails(self):
        rc, out = run(transcript="$ dbg status\nboot 12\n")
        self.assertIn("FAIL bench.1", out)
        self.assertIn("observable", out)

    def test_observable_only_on_the_command_line_fails(self):
        rc, out = run(transcript='$ dbg status\n', doc=text("good.md").replace('"ver v0.1.0"', '"dbg status"'))
        self.assertIn("FAIL bench.1", out)

    def test_hardware_only_item_cited_as_pin_fails(self):
        f = s4kit.finding("SD-FIX-2", state="pinned", pin={"test": "bench:2", "transcript": "x.txt"})
        rc, out = run(findings=[f])
        self.assertEqual(rc, 1)
        self.assertIn("FAIL bench.2.pin", out)

    def test_hardware_only_item_cited_as_evidence_fails(self):
        rc, out = run(findings=[s4kit.finding("SD-FIX-3", evidence=["bench:F0-102 passed"])])
        self.assertIn("FAIL bench.F0-102.pin", out)

    def test_similar_slug_is_not_a_citation(self):
        rc, out = run(findings=[s4kit.finding("SD-FIX-4", evidence=["bench:20"])])
        self.assertEqual(rc, 0, out)

    def test_no_transcript_marker_needs_a_live_row(self):
        doc = text("good.md").replace("[hardware-only]\n- [x]", "[no-transcript: SD-FIX-5]\n- [x]")
        rc, out = run(doc=doc)
        self.assertIn("FAIL bench.2", out)
        rc, out = run(doc=doc, findings=[s4kit.finding("SD-FIX-5")])
        self.assertEqual(rc, 0, out)
        rc, out = run(doc=doc, findings=[s4kit.finding("SD-FIX-5", state="closed")])
        self.assertIn("FAIL bench.2", out)

    def test_closed_section_is_not_checked(self):
        rc, out = run()
        self.assertNotIn("bench.9", out)

    def test_missing_open_section_fails(self):
        rc, out = run(doc="# Bench\n\n## Closed\n")
        self.assertEqual(rc, 1)
        self.assertIn("FAIL bench.input", out)

    def test_duplicate_item_fails(self):
        doc = text("good.md").replace("**2. Panel", "**1. Panel")
        rc, out = run(doc=doc)
        self.assertIn("FAIL bench.1.duplicate", out)

    def test_staged_without_bench_doc_passes(self):
        rc, out = run(argv=["--staged"])
        self.assertEqual(rc, 0, out)
        self.assertIn("PASS bench.staged.none", out)

    def test_real_bench_doc_passes(self):
        with s4kit.env(s4kit.REPO):
            rc, out = s4kit.run_main(gate_bench.main, [])
        self.assertEqual(rc, 0, out)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 4: Run to verify failure**

Run: `python3 -m unittest tools/gates/tests/test_gate_bench.py -v` → ImportError.

- [ ] **Step 5: Write `gate_bench.py`**

```python
#!/usr/bin/env python3
"""gate_bench -- every open bench row is runnable-proven or explicitly not (spec sections 7, 10).

Rows of `## Open items` in docs/bench/next-bench-day.md (ticked or not) must carry one of:
`[hardware-only]`; `[no-transcript: <finding-id>]` naming a live findings row; or
`transcript: docs/bench/transcripts/<slug>.txt` plus `observable: "<text>"`, where the transcript's
first line is `$ <command>` with that command named in the row in backticks, and <text> appears
below it. A hardware-only or no-transcript item may not be cited by any finding's pin or evidence.

usage: gate_bench.py [--json] [--staged]
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from tools.gates.lib import GateError, Result, repo_root, run_gate  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

DOC = "docs/bench/next-bench-day.md"
TRANSCRIPTS = "docs/bench/transcripts"
OPEN_H = "## Open items"
ROW_RE = re.compile(r"^- \[( |x)\] \*\*(?P<id>[^*]+?)(?:\.(?=[\s*])| — |\*\*)")
HW = "[hardware-only]"
NOTR_RE = re.compile(r"\[no-transcript: ([A-Z0-9]+(?:-[A-Za-z0-9]+)+)\]")
TR_RE = re.compile(r"transcript: `?(docs/bench/transcripts/[^\s`]+)`?")
OBS_RE = re.compile(r'observable: "([^"]+)"')
DEAD = {"closed", "merged"}


def slug(item_id):
    return re.sub(r"[^A-Za-z0-9]+", "-", item_id).strip("-")


def rows(text):
    lines = text.split("\n")
    try:
        start = lines.index(OPEN_H)
    except ValueError:
        raise GateError("%s: no '%s' heading" % (DOC, OPEN_H))
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    out, cur = [], None
    for i in range(start + 1, end):
        line = lines[i]
        m = ROW_RE.match(line)
        if m:
            cur = [slug(m.group("id")), m.group("id"), i + 1, [line]]
            out.append(cur)
        elif line.startswith("- [") or line.startswith("#"):
            cur = None
        elif cur is not None:
            cur[3].append(line)
    return [(s, iid, ln, "\n".join(body)) for s, iid, ln, body in out]


def _cites(findings, s):
    rx = re.compile(r"bench:%s(?![A-Za-z0-9-])" % re.escape(s))
    path = "%s/%s.txt" % (TRANSCRIPTS, s)
    hits = []
    for f in findings:
        pin = f.get("pin") or {}
        texts = [str(pin.get("test") or ""), str(pin.get("transcript") or "")] + [str(e) for e in f.get("evidence") or []]
        if any(rx.search(t) or path in t for t in texts):
            hits.append(f["id"])
    return hits


def _transcript(root, s, body, r):
    rid = "bench.%s" % s
    tm, om = TR_RE.search(body), OBS_RE.search(body)
    if tm is None:
        r.fail(rid, "no [hardware-only], no [no-transcript: <id>], no transcript: link")
        return
    want = "%s/%s.txt" % (TRANSCRIPTS, s)
    if tm.group(1) != want:
        r.fail(rid, "transcript must be %s, row names %s" % (want, tm.group(1)))
        return
    path = os.path.join(root, want)
    if not os.path.isfile(path):
        r.fail(rid, "%s does not exist" % want)
        return
    with open(path, encoding="utf-8", errors="replace") as fh:
        lines = fh.read().split("\n")
    if not lines[0].startswith("$ "):
        r.fail(rid, "%s: first line must be '$ <command>'" % want)
        return
    cmd = lines[0][2:].strip()
    if ("`%s`" % cmd) not in body:
        r.fail(rid, "transcript command '%s' is not named in the row in backticks" % cmd)
        return
    if om is None:
        r.fail(rid, 'row names no observable: "<text>"')
        return
    if om.group(1) not in "\n".join(lines[1:]):
        r.fail(rid, "observable '%s' not in %s below the command" % (om.group(1), want))
        return
    r.passed(rid)


def check(args):
    ap = argparse.ArgumentParser(prog="gate_bench")
    ap.add_argument("--staged", action="store_true")
    a = ap.parse_args(args)
    root = repo_root()
    r = Result("bench")
    if a.staged and DOC not in gitinfo.staged_files(root) and not any(
            p.startswith(TRANSCRIPTS + "/") for p in gitinfo.staged_files(root)):
        r.passed("bench.staged.none")
        return r
    path = os.path.join(root, DOC)
    if not os.path.isfile(path):
        raise GateError("%s missing" % DOC)
    with open(path, encoding="utf-8") as fh:
        found = rows(fh.read())
    if not found:
        raise GateError("%s: no rows under '%s'" % (DOC, OPEN_H))
    findings = registers.load_findings(root)
    states = {f["id"]: f["state"] for f in findings}
    seen = set()
    for s, iid, ln, body in found:
        rid = "bench.%s" % s
        if s in seen:
            r.fail(rid + ".duplicate", "item id '%s' appears twice (line %d)" % (iid, ln))
            continue
        seen.add(s)
        notr = NOTR_RE.search(body)
        if HW in body or notr:
            if HW in body:
                r.passed(rid + ".hardware-only")
            elif states.get(notr.group(1)) is None or states[notr.group(1)] in DEAD:
                r.fail(rid, "[no-transcript: %s] needs a live findings row (state %s)"
                       % (notr.group(1), states.get(notr.group(1))))
            else:
                r.passed(rid + ".no-transcript." + notr.group(1))
            for fid in _cites(findings, s):
                r.fail(rid + ".pin", "%s cites a bench item that has no runnability transcript" % fid)
        else:
            _transcript(root, s, body, r)
    return r


def main(argv=None):
    return run_gate("bench", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 6: Run the fixture tests**

Run: `python3 -m unittest tools/gates/tests/test_gate_bench.py -v` → all PASS except `test_real_bench_doc_passes` (FAIL: 72 rows without evidence).

- [ ] **Step 7: Conformance: SD-BENCH-1 and the row markers**

Save as `.superpowers/sdd/strict-delivery/rows-bench.yml` and add with `regedit.py add`:
```yaml
- id: SD-BENCH-1
  source: agent
  severity: HIGH
  class: >-
    No open bench row in docs/bench/next-bench-day.md carries a runnability transcript (verb exists on the
    named build, path compiled in, observable printed) or a hardware-only tag; audit 2026-10-16 found the
    bench doc not runnable as written. Every row marked [no-transcript: SD-BENCH-1] is to get a transcript
    or a [hardware-only] tag.
  siblings: [ticked rows left in the open section, F2a subsection rows, F2b subsection rows]
  cites: [{file: docs/bench/next-bench-day.md, lines: "15-580", sha: 7265059}]
  repro: "python3 tools/gates/gate_bench.py with the SD-BENCH-1 markers removed"
  owner: {kind: plan, plan: docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, task: conformance}
  state: open
  pin: null
  ruling: null
  evidence: [docs/reviews/2026-10-16-audit-post-fix-wave.md]
  history: [{state: open, sha: "0", by: S4-T3}]
```
```bash
python3 - <<'PY'
import sys
sys.path.insert(0, ".")
from tools.gates import gate_bench
p = gate_bench.DOC
lines = open(p, encoding="utf-8").read().split("\n")
marks = 0
for _s, _iid, ln, body in gate_bench.rows("\n".join(lines)):
    if "[hardware-only]" in body or "[no-transcript:" in body:
        continue
    lines[ln - 1] = lines[ln - 1].rstrip() + " [no-transcript: SD-BENCH-1]"
    marks += 1
open(p, "w", encoding="utf-8").write("\n".join(lines))
print(marks, "rows marked")
PY
python3 tools/gates/gate_bench.py; echo rc=$?
```
Expected: `72 rows marked`; gate rc 0. Then run the fixture tests again → all PASS, including `test_real_bench_doc_passes`.

- [ ] **Step 8: Wire in, commit, mutation, pin**

`run_all.py` `GATES`: add `"bench"` (umbrella order, after `state_docs`' slot, i.e. `"comments", "bench"` until `state_docs` lands in Task 4); hook staged argv `["--staged"]`; add `bench` to S1's `git commit` gate list in `pre_tool_use.py` and to `git-pre-commit.sh` (spec §7: pre-commit on the bench doc). `gates.yml`: add `bench` to any `--only` list. README gates table:
```markdown
| `gate_bench` | each `## Open items` row: `[hardware-only]`, `[no-transcript: <live finding>]`, or `transcript:` + `observable:` proven by `docs/bench/transcripts/<slug>.txt`; such items never cited as a pin | pre-commit on the bench doc, CI |
```
```bash
python3 tools/gates/run_all.py && \
git add tools/gates .github/workflows/gates.yml docs/bench/next-bench-day.md docs/superpowers/state/findings.yml && \
git commit -m "gates: gate_bench; 72 open bench rows marked [no-transcript: SD-BENCH-1] (SD-S4-T3)"
```
Mutation: `sed -i.bak 's/^            for fid in _cites(findings, s):$/            for fid in []:/' tools/gates/gate_bench.py`; test `test_hardware_only_item_cited_as_pin_fails` PASS / FAIL / PASS in `mutations/SD-S4-T3.txt`; then `regedit.py set SD-S4-T3 pinned S4-T3 --pin-test tools/gates/tests/test_gate_bench.py::BenchGateTest::test_hardware_only_item_cited_as_pin_fails --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T3.txt`; commit `state: SD-S4-T3 pinned (mutation transcript)`.

- [ ] **Step 9: CHECKPOINT b10**

Block end per umbrella "Checkpoint protocol" (`gate_merge_ready` does not exist yet; run the `checklists/block-end.md` merge-readiness list by hand and ledger it): `python3 tools/gates/run_all.py` rc 0 → push → final reviewer (opus) + final verifier (Fable) reports → PR ready → every check green incl. advisory → `gh pr merge --squash` under `DEC-0001` → tag `strict-delivery-b10` on the squash commit, push the tag → delete branches/worktrees → handoff `docs/superpowers/state/strict-delivery/handoff.md` (`block: 10`, `next_task: S4 Task 4`) → ledger `CHECKPOINT b10` → `python3 tools/gates/gate_checkpoint.py --plan strict-delivery` rc 0 → post the status update and the resume prompt → stop.

Resume prompt:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 11 (sub-plan S4, docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, Task 4) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

---

## Block b11 — `gate_state_docs`, `gate_merge_ready` + hooks, `postmerge.py` (tag `strict-delivery-b11`, branch `block/strict-delivery-b11`)

Tasks 4–6 are SERIAL (Task 5 imports nothing from Task 4, but Task 6 imports both, and all three edit `gitinfo.py`, `run_all.py`, `README.md`). `gate_merge_ready` skips fenced code when it reads block and task headings, so the Markdown snippets below that contain `## ` lines don't end a block.

### Task 4: `gate_state_docs` and the generated regions

**Register:** SD-S4-T4. **Siblings:** README required-checks sentence; fix-wave status cells; owner table; inline vs block regions; drift on `main` after a merge. **Gates:** `run_all.py`, gate-test suite.

**Files:**
- Create: `tools/gates/gate_state_docs.py`, `tools/gates/tests/test_gate_state_docs.py`
- Modify: `tools/gates/lib/gitinfo.py` (append), `docs/superpowers/plans/2026-09-14-roadmap.md` (lines 4–5, 39–42, 271–277, 282, 300–304, new section before line 300), `README.md:76-79`, `tools/gates/run_all.py`, `.github/workflows/gates.yml`, `tools/gates/README.md`

**Interfaces:**
- Consumes: `gitinfo._s4_cmd`, `gitinfo.gh_json` (Task 1); `registers.load_findings`; `s4kit`.
- Produces (gitinfo): `PR_FIELDS: str`, `pr_view(root, selector) -> dict`, `open_issues(root) -> list[dict]`, `repo_slug(root) -> str`, `required_checks(root) -> list[str]`, `git_out(root, *args) -> str`, `origin_main(root) -> str`, `merge_base(root, a, b) -> str`, `is_ancestor(root, a, b) -> bool`, `tag_list(root) -> list[str]`, `rev(root, ref) -> str` (full sha of the commit).
- Produces (gate): `gate_state_docs.DOCS: tuple[str, str]`, `GENERATORS: dict[str, fn(root, params: dict) -> str]`, `STRICT_ON_MAIN = {"dram", "owner-table"}`, `on_main(root) -> bool`, `check(args)` with argv `[] | ["--write"]`.
- Region grammar: `<!-- gen:<name>[ key=value ...] -->BODY<!-- /gen -->`, `name` ∈ {`open-issues`, `required-checks`, `dram`, `owner-table`, `plan-status`}; a BODY that starts with a newline is a block region (regenerated as `\n<text>\n`), otherwise inline. `plan-status` takes `pr=<n>` or `plan=<slug>`. There is no `main-sha` region: a commit cannot carry the sha of the merge that lands it, and `main` takes no post-merge commit (umbrella correction 2026-10-17).
- When it runs: `gate_state_docs.py --write` is the last commit on the block branch before the final review (Task 8's `block.js`, and by hand in each CHECKPOINT). On a block branch every drift is a FAIL. On `main` (current branch `main`, or a detached HEAD equal to `origin/main`) only file- and register-derived content FAILs on drift (`dram`, and the Findings column of `owner-table`; its Open-issues column is gh-derived); gh- and tag-derived regions (`open-issues`, `required-checks`, `plan-status`) that drifted since the branch print `WARN state_docs.<id>: …` on stderr and count as PASS (`PASS state_docs.<id>.warn-drift-since-branch`), because the merge itself (closed issues, the new tag) changes them and `main` can't take a regeneration commit. The next block branch regenerates them.
- Generator outputs:
  - `open-issues`: `<n> open`.
  - `required-checks`: `<number word>: ` + the contexts sorted and backticked, comma-separated.
  - `dram`: `**<env> <free>, <env> <free>**` from the first table under the first `## Static DRAM` heading of `docs/measurements.md`, column `Free static DRAM`.
  - `owner-table`: a Markdown table `| Owner | Findings | Open issues |`; findings still needing work (state not `pinned`/`closed`/`merged`, so a block's own rows leave the table when they are pinned on the branch and stay out when `postmerge.py` sets them `merged`) grouped by owner (`plan \`<plan>\``, `roadmap Plan <n>`, `issue #<n>`); every open issue goes under the owner of register row `GH-<n>`, or under `unowned (no \`GH-<n>\` register row)`.
  - `plan-status pr=<n>`: `DONE <mergedAt date> — PR #<n> → <sha7>` | `OPEN — PR #<n>` (+ ` (draft)`) | `CLOSED without merge — PR #<n>`; `plan=<slug>`: `no block merged yet` | `<k> block(s) merged: \`<slug>-b1\` → <sha7>, …` from tags matching `^<slug>-b(\d+)$`, sorted numerically; on a branch `block/<slug>-b<N>` whose tag does not exist yet, `; \`<slug>-b<N>\` pending (this block)` is appended.

- [ ] **Step 1: Branch and register**

```bash
git switch main && git pull --no-rebase --ff-only && git switch -c block/strict-delivery-b11
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T4 in-progress S4-T4
```

- [ ] **Step 2: gitinfo helpers**

Append to `tools/gates/lib/gitinfo.py`:
```python
PR_FIELDS = ("number,title,body,state,isDraft,headRefOid,headRefName,baseRefName,mergedAt,"
             "mergeCommit,statusCheckRollup")


def pr_view(root, selector):
    return gh_json(root, ["pr", "view", str(selector), "--json", PR_FIELDS])


def open_issues(root):
    data = gh_json(root, ["issue", "list", "--state", "open", "--limit", "1000", "--json", "number,title"])
    if not isinstance(data, list):
        raise GateError("gh issue list: expected a list")
    return data


def repo_slug(root):
    return gh_json(root, ["repo", "view", "--json", "nameWithOwner"])["nameWithOwner"]


def required_checks(root):
    data = gh_json(root, ["api", "repos/%s/branches/main" % repo_slug(root)])
    try:
        ctx = data["protection"]["required_status_checks"]["contexts"]
    except (KeyError, TypeError):
        raise GateError("branches/main: no protection.required_status_checks.contexts")
    if not ctx:
        raise GateError("branches/main: no required status check")
    return list(ctx)


def git_out(root, *args):
    return _s4_cmd(root, ["git"] + list(args)).strip()


def origin_main(root):
    return git_out(root, "rev-parse", "--verify", "origin/main")


def merge_base(root, a, b):
    return git_out(root, "merge-base", a, b)


def is_ancestor(root, a, b):
    p = subprocess.run(["git", "merge-base", "--is-ancestor", a, b], cwd=root, capture_output=True,
                       text=True, check=False)
    if p.returncode not in (0, 1):
        raise GateError("git merge-base --is-ancestor %s %s: %s" % (a, b, p.stderr.strip()))
    return p.returncode == 0


def tag_list(root):
    return [t for t in git_out(root, "tag", "-l").split("\n") if t]


def rev(root, ref):
    return git_out(root, "rev-parse", "--verify", ref + "^{commit}")
```

- [ ] **Step 3: Write the failing tests**

`tools/gates/tests/test_gate_state_docs.py`:
```python
import contextlib
import io
import os
import sys
import unittest

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402
from tools.gates import gate_state_docs as g  # noqa: E402

ROADMAP = ("Issues: <!-- gen:open-issues -->0 open<!-- /gen -->.\n"
           "F4: <!-- gen:plan-status pr=138 -->OPEN<!-- /gen -->\n"
           "Demo: <!-- gen:plan-status plan=demo -->x<!-- /gen -->\n"
           "DRAM <!-- gen:dram -->x<!-- /gen -->\n"
           "<!-- gen:owner-table -->\nx\n<!-- /gen -->\n")
README = "Required: <!-- gen:required-checks -->x<!-- /gen -->.\n"
MEAS = ("# Measurements\n\n## Static DRAM after R33 (2026-10-15)\n\n"
        "| Env | DRAM used | Free static DRAM | App image |\n|---|---|---|---|\n"
        "| `moto_sim` | 1 B | **66,472 B** | 2 B |\n| `moto_neo6m` | 1 B | **66,640 B** | 2 B |\n")
GH = {
    "issue list": {"stdout": [{"number": 104, "title": "a"}, {"number": 129, "title": "b"}]},
    "pr view 138": {"stdout": {"number": 138, "state": "MERGED", "isDraft": False,
                               "mergedAt": "2026-10-15T10:48:21Z", "mergeCommit": {"oid": "7265059abf97d0ea"}}},
    "repo view": {"stdout": {"nameWithOwner": "o/r"}},
    "api repos/o/r/branches/main": {"stdout": {"protection": {"required_status_checks": {
        "contexts": ["tools-tests", "hygiene"]}}}},
}


def repo(roadmap=ROADMAP, findings=None):
    root = s4kit.make_repo(files={g.DOCS[0]: roadmap, g.DOCS[1]: README, "docs/measurements.md": MEAS},
                           findings=findings)
    s4kit.sh(root, "git", "update-ref", "refs/remotes/origin/main", "HEAD")
    s4kit.sh(root, "git", "checkout", "-qb", "block/demo-b1")
    s4kit.commit(root, {"src/x.c": "int x;\n"})
    return root


def run(root, argv=None, gh=GH):
    with s4kit.env(root, gh):
        return s4kit.run_main(g.main, argv or [])


def doc(root, i=0):
    with open(os.path.join(root, g.DOCS[i])) as fh:
        return fh.read()


def to_main(root):
    """Simulate the squash merge: main fast-forwards to the branch, origin/main follows."""
    s4kit.sh(root, "git", "checkout", "-q", "main")
    s4kit.sh(root, "git", "merge", "-q", "--ff-only", "block/demo-b1")
    s4kit.sh(root, "git", "update-ref", "refs/remotes/origin/main", "HEAD")


class StateDocsTest(unittest.TestCase):
    def test_drift_fails_and_write_regenerates(self):
        root = repo()
        rc, out = run(root)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL state_docs.%s:open-issues" % g.DOCS[0], out)
        rc, out = run(root, ["--write"])
        self.assertEqual(rc, 0, out)
        text = doc(root)
        self.assertIn("<!-- gen:open-issues -->2 open<!-- /gen -->", text)
        self.assertIn("DONE 2026-10-15 — PR #138 → 7265059", text)
        self.assertIn("**moto_sim 66,472 B, moto_neo6m 66,640 B**", text)
        self.assertIn("no block merged yet; `demo-b1` pending (this block)", text)
        self.assertIn("two: `hygiene`, `tools-tests`", doc(root, 1))
        rc, out = run(root)
        self.assertEqual(rc, 0, out)

    def test_on_main_gh_drift_warns_register_drift_fails(self):
        root = repo()
        run(root, ["--write"])
        s4kit.sh(root, "git", "commit", "-qam", "regen")
        to_main(root)
        s4kit.sh(root, "git", "tag", "-a", "demo-b1", "HEAD", "-m", "t")
        gh = dict(GH, **{"issue list": {"stdout": [{"number": 104, "title": "a"}]}})
        with s4kit.env(root, gh):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc, out = s4kit.run_main(g.main, [])
        self.assertEqual(rc, 0, out)
        self.assertIn("WARN state_docs.%s:open-issues" % g.DOCS[0], err.getvalue())
        self.assertIn("WARN state_docs.%s:plan-status:plan=demo" % g.DOCS[0], err.getvalue())
        s4kit.commit(root, {"docs/superpowers/state/findings.yml":
                            yaml.safe_dump([s4kit.finding("SD-X-9")], sort_keys=False)})
        s4kit.sh(root, "git", "update-ref", "refs/remotes/origin/main", "HEAD")
        rc, out = run(root, gh=gh)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL state_docs.%s:owner-table" % g.DOCS[0], out)

    def test_owner_table_groups_findings_and_issues(self):
        rows = [s4kit.finding("GH-104", owner={"kind": "roadmap", "plan": 8}),
                s4kit.finding("SD-X-1", owner={"kind": "plan", "plan": "p1", "task": 2}),
                s4kit.finding("SD-X-2", state="merged"), s4kit.finding("SD-X-3", state="pinned")]
        root = repo(findings=rows)
        with s4kit.env(root, GH):
            table = g.GENERATORS["owner-table"](root, {})
        self.assertIn("| plan `p1` | SD-X-1 | — |", table)
        self.assertIn("| roadmap Plan 8 | — | #104 |", table)
        self.assertIn("| unowned (no `GH-<n>` register row) | — | #129 |", table)
        self.assertNotIn("SD-X-2", table)
        self.assertNotIn("SD-X-3", table)

    def test_plan_status_from_tags(self):
        root = repo()
        s4kit.sh(root, "git", "tag", "demo-b2")
        s4kit.sh(root, "git", "tag", "demo-b10", "HEAD~1")
        out = g.GENERATORS["plan-status"](root, {"plan": "demo"})
        self.assertTrue(out.startswith("2 block(s) merged: `demo-b2` → "), out)
        self.assertLess(out.index("demo-b2"), out.index("demo-b10"))
        self.assertTrue(out.endswith("; `demo-b1` pending (this block)"), out)

    def test_unknown_generator_fails(self):
        root = repo(roadmap="x <!-- gen:nope -->y<!-- /gen -->\n")
        rc, out = run(root)
        self.assertIn("FAIL state_docs.%s:nope" % g.DOCS[0], out)

    def test_unclosed_region_fails(self):
        root = repo(roadmap="x <!-- gen:open-issues -->y\n")
        rc, out = run(root)
        self.assertEqual(rc, 1)
        self.assertIn("FAIL state_docs.%s" % g.DOCS[0], out)

    def test_doc_without_region_fails(self):
        root = repo(roadmap="no regions\n")
        rc, out = run(root)
        self.assertIn("no gen region", out)

    def test_gh_failure_fails(self):
        root = repo()
        rc, out = run(root, ["--write"], gh={})
        self.assertEqual(rc, 1)
        self.assertIn("FAIL state_docs.%s:open-issues" % g.DOCS[0], out)

    def test_real_docs_carry_every_generator(self):
        names = set()
        for rel in g.DOCS:
            with open(os.path.join(s4kit.REPO, rel)) as fh:
                names |= {m.group("name") for m in g.REGION_RE.finditer(fh.read())}
        self.assertEqual(names, set(g.GENERATORS))


if __name__ == "__main__":
    unittest.main()
```
Run: `python3 -m unittest tools/gates/tests/test_gate_state_docs.py -v` → ImportError.

- [ ] **Step 4: Write `gate_state_docs.py`**

```python
#!/usr/bin/env python3
"""gate_state_docs -- generated facts in the roadmap and README match their sources (spec sections 4.5, 10).

Every <!-- gen:<name> [k=v ...] -->...<!-- /gen --> region is regenerated from git, GitHub, the
registers and docs/measurements.md; a region whose committed text differs fails, except on main,
where gh- and tag-derived regions only WARN (the merge itself moves them and main takes no
regeneration commit). --write rewrites the regions; it runs on the block branch before the final
review.

usage: gate_state_docs.py [--json] [--write]
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from tools.gates.lib import GateError, Result, repo_root, run_gate  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

DOCS = ("docs/superpowers/plans/2026-09-14-roadmap.md", "README.md")
MEASUREMENTS = "docs/measurements.md"
REGION_RE = re.compile(r"<!-- gen:(?P<name>[a-z-]+)(?P<params>(?: [a-z]+=[A-Za-z0-9._/-]+)*) -->"
                       r"(?P<body>.*?)<!-- /gen -->", re.S)
WORDS = ["zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine", "ten",
         "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen",
         "nineteen", "twenty"]
DONE = {"pinned", "closed", "merged"}


def on_main(root):
    name = gitinfo.git_out(root, "rev-parse", "--abbrev-ref", "HEAD")
    if name == "main":
        return True
    return name == "HEAD" and gitinfo.rev(root, "HEAD") == gitinfo.origin_main(root)


def gen_open_issues(root, params):
    return "%d open" % len(gitinfo.open_issues(root))


def gen_required_checks(root, params):
    ctx = sorted(gitinfo.required_checks(root))
    word = WORDS[len(ctx)] if len(ctx) < len(WORDS) else str(len(ctx))
    return "%s: %s" % (word, ", ".join("`%s`" % c for c in ctx))


def gen_dram(root, params):
    path = os.path.join(root, MEASUREMENTS)
    if not os.path.isfile(path):
        raise GateError("%s missing" % MEASUREMENTS)
    with open(path, encoding="utf-8") as fh:
        lines = fh.read().split("\n")
    h = next((i for i, l in enumerate(lines) if l.startswith("## Static DRAM")), None)
    if h is None:
        raise GateError("%s: no '## Static DRAM' heading" % MEASUREMENTS)
    hdr = None
    for i in range(h + 1, len(lines)):
        if lines[i].startswith("## "):
            break
        if lines[i].startswith("|"):
            hdr = i
            break
    if hdr is None:
        raise GateError("%s: no table under the Static DRAM heading" % MEASUREMENTS)
    cols = [c.strip() for c in lines[hdr].strip().strip("|").split("|")]
    if "Free static DRAM" not in cols:
        raise GateError("%s: no 'Free static DRAM' column" % MEASUREMENTS)
    idx = cols.index("Free static DRAM")
    vals = []
    for line in lines[hdr + 2:]:
        if not line.startswith("|"):
            break
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        vals.append("%s %s" % (cells[0].strip("`"), cells[idx].strip("*")))
    if not vals:
        raise GateError("%s: the Static DRAM table has no rows" % MEASUREMENTS)
    return "**%s**" % ", ".join(vals)


def _owner_key(o):
    kind = (o or {}).get("kind")
    if kind == "plan":
        return "plan `%s`" % o.get("plan")
    if kind == "roadmap":
        return "roadmap Plan %s" % o.get("plan")
    if kind == "issue":
        return "issue #%s" % o.get("number")
    return "unowned (no owner field)"


def gen_owner_table(root, params):
    findings = registers.load_findings(root)
    by_id = {f["id"]: f for f in findings}
    groups = {}
    for f in findings:
        if f["state"] in DONE or re.match(r"^GH-\d+$", f["id"]):
            continue
        groups.setdefault(_owner_key(f.get("owner")), (set(), set()))[0].add(f["id"])
    for issue in gitinfo.open_issues(root):
        row = by_id.get("GH-%d" % issue["number"])
        key = _owner_key(row.get("owner")) if row else "unowned (no `GH-<n>` register row)"
        groups.setdefault(key, (set(), set()))[1].add(issue["number"])
    out = ["| Owner | Findings | Open issues |", "|---|---|---|"]
    for key in sorted(groups):
        fs, iss = groups[key]
        out.append("| %s | %s | %s |" % (key, ", ".join(sorted(fs)) or "—",
                                          ", ".join("#%d" % n for n in sorted(iss)) or "—"))
    return "\n".join(out)


def gen_plan_status(root, params):
    if "pr" in params:
        pr = gitinfo.pr_view(root, params["pr"])
        if pr.get("state") == "MERGED":
            return "DONE %s — PR #%d → %s" % (pr["mergedAt"][:10], pr["number"], pr["mergeCommit"]["oid"][:7])
        if pr.get("state") == "OPEN":
            return "OPEN — PR #%d%s" % (pr["number"], " (draft)" if pr.get("isDraft") else "")
        return "CLOSED without merge — PR #%d" % pr["number"]
    if "plan" in params:
        rx = re.compile(r"^%s-b(\d+)$" % re.escape(params["plan"]))
        tags = sorted((int(m.group(1)), t) for t in gitinfo.tag_list(root) for m in [rx.match(t)] if m)
        text = "no block merged yet" if not tags else "%d block(s) merged: %s" % (len(tags), ", ".join(
            "`%s` → %s" % (t, gitinfo.rev(root, t)[:7]) for _n, t in tags))
        m = re.match(r"^block/%s-b(\d+)$" % re.escape(params["plan"]),
                     gitinfo.git_out(root, "rev-parse", "--abbrev-ref", "HEAD"))
        if m and "%s-b%s" % (params["plan"], m.group(1)) not in {t for _n, t in tags}:
            text += "; `%s-b%s` pending (this block)" % (params["plan"], m.group(1))
        return text
    raise GateError("plan-status needs pr=<n> or plan=<slug>")


GENERATORS = {"open-issues": gen_open_issues,
              "required-checks": gen_required_checks, "dram": gen_dram,
              "owner-table": gen_owner_table, "plan-status": gen_plan_status}
STRICT_ON_MAIN = {"dram", "owner-table"}


def _findings_part(table):
    """The register-derived half of an owner table: {(owner, findings)} for rows that list findings."""
    out = set()
    for line in table.strip().split("\n")[2:]:
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) == 3 and cells[1] != "—":
            out.add((cells[0], cells[1]))
    return out


def _lenient(name, body, new):
    if name not in STRICT_ON_MAIN:
        return True
    return name == "owner-table" and _findings_part(body) == _findings_part(new)


def _params(s):
    return dict(p.split("=", 1) for p in s.split())


def check(args):
    ap = argparse.ArgumentParser(prog="gate_state_docs")
    ap.add_argument("--write", action="store_true")
    a = ap.parse_args(args)
    root = repo_root()
    r = Result("state_docs")
    lenient = on_main(root) and not a.write
    for rel in DOCS:
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            r.fail("state_docs.%s" % rel, "missing")
            continue
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        matches = list(REGION_RE.finditer(text))
        if text.count("<!-- gen:") != len(matches) or any("<!-- gen:" in m.group("body") for m in matches):
            r.fail("state_docs.%s" % rel, "an unclosed or nested gen region")
            continue
        if not matches:
            r.fail("state_docs.%s" % rel, "no gen region")
            continue
        pieces, last = [], 0
        for m in matches:
            rid = "state_docs.%s:%s%s" % (rel, m.group("name"), m.group("params").replace(" ", ":"))
            body = m.group("body")
            new = body
            fn = GENERATORS.get(m.group("name"))
            if fn is None:
                r.fail(rid, "unknown generator")
            else:
                try:
                    text_out = fn(root, _params(m.group("params")))
                    new = "\n%s\n" % text_out if body.startswith("\n") else text_out
                    if new == body:
                        r.passed(rid)
                    elif a.write:
                        r.passed(rid + ".written")
                    elif lenient and _lenient(m.group("name"), body, new):
                        print("WARN %s: drifted since the branch (committed %r, now %r); the next block "
                              "branch regenerates it" % (rid, body.strip()[:60], new.strip()[:60]), file=sys.stderr)
                        r.passed(rid + ".warn-drift-since-branch")
                    else:
                        r.fail(rid, "drift: committed %r, generated %r (run gate_state_docs.py --write)"
                               % (body.strip()[:60], new.strip()[:60]))
                except GateError as e:
                    r.fail(rid, str(e))
                    new = body
            pieces.append(text[last:m.start("body")] + new)
            last = m.end("body")
        pieces.append(text[last:])
        if a.write:
            with open(path, "w", encoding="utf-8") as fh:
                fh.write("".join(pieces))
    return r


def main(argv=None):
    return run_gate("state_docs", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```
Run: `python3 -m unittest tools/gates/tests/test_gate_state_docs.py -v` → all PASS except `test_real_docs_carry_every_generator`.

- [ ] **Step 5: Insert the markers into the real documents**

Each edit is an exact text replacement (use Edit with the old text; line numbers are from 7265059 and may have shifted):

1. Roadmap lines 4–5: replace
```
**Started:** 2026-09-14. **Last reconciled against the tree and GitHub:** 2026-10-15 (audit
`docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` + the fix wave below; `main` = e0defd3).
```
with
```
**Started:** 2026-09-14. **Generated facts** (every `gen:` region) are rewritten by
`python3 tools/gates/gate_state_docs.py --write` on each block branch before its final review, so on `main` they
describe the tree as of the last merged block; the hand-written lines were last reconciled on 2026-10-15 (audit
`docs/reviews/2026-10-13-audit-roadmap-code-vs-spec.md` + the fix wave below).
```
2. Roadmap lines 39–42: replace
```
   warnings**. Work lives on a session branch; open a PR, wait for the required checks (**nine**
   since 2026-10-15: hygiene, host-tests, dc-host-tests, tools-tests, tracks-generated,
   static-analysis, build (moto_neo6m), build (moto_sim), build (devcontroller); ruling R13;
   `coverage` is the tenth job and advisory), squash-merge, then tag
```
with
```
   warnings**. Work lives on a session branch; open a PR, wait for the required checks
   (<!-- gen:required-checks -->nine<!-- /gen -->; ruling R13 designated the first nine) and for every
   other job, advisory ones included (`gate_merge_ready`), squash-merge, then tag
```
3. Roadmap lines 271–277: in each fix-wave row replace only the last cell's status text with a region: F0 `<!-- gen:plan-status pr=101 -->DONE 2026-10-13 — PR #101 → a390151<!-- /gen -->`, F3 `pr=113`, F1a `pr=121`, F1b `pr=122`, F2a `pr=126`, F2b `pr=127`, F4 `pr=138` (the F4 body `OPEN — this branch (…)` is replaced; `--write` turns it into `DONE 2026-10-15 — PR #138 → 7265059`).
4. Roadmap line 282: `**moto_sim 66,472 B, moto_neo6m 66,640 B**` → `<!-- gen:dram -->**moto_sim 66,472 B, moto_neo6m 66,640 B**<!-- /gen -->`.
5. Roadmap: insert before the `## Open issues by owner` heading:
```
## Strict delivery (process plan `docs/superpowers/plans/2026-10-17-strict-delivery.md`)

Blocks merged: <!-- gen:plan-status plan=strict-delivery -->none<!-- /gen -->.

```
6. Roadmap lines 300–304: replace the heading and the paragraph under it
```
## Open issues by owner (37 open on 2026-10-15: 28 after the Task 12 verification pass closed #109, #115, #117, plus 9 filed by F4 Tasks 13–14, #129–#137)

Owners come from design §7b where it covers the issue (adjusted for what has merged since), else from
the owner named in the issue's own body. This table is the single source for the owner comments
posted on each issue.
```
with
```
## Open issues by owner (<!-- gen:open-issues -->37 open<!-- /gen -->)

### Owners of record (generated from `docs/superpowers/state/findings.yml`; an issue is owned through its `GH-<n>` row)

<!-- gen:owner-table -->
pending
<!-- /gen -->

### Owner notes as of 2026-10-15 (hand-written snapshot, not a source of truth; it is replaced by `GH-<n>` register rows in the S5 bootstrap)

Owners below came from design §7b where it covers the issue (adjusted for what had merged by then), else
from the owner named in the issue's own body.
```
7. README lines 76–79: replace
```
`main` is protected: pull requests only, linear history, no force pushes, enforced for admins. The
required status checks (since 2026-10-15) are nine of CI's ten jobs (the tenth, `coverage`, is advisory): `hygiene`, `host-tests`,
`dc-host-tests`, `tools-tests`, `tracks-generated`, `static-analysis` (the Power-of-10 gate),
`build (moto_neo6m)`, `build (moto_sim)` and `build (devcontroller)`.
```
with
```
`main` is protected: pull requests only, linear history, no force pushes, enforced for admins. The
required status checks are <!-- gen:required-checks -->nine<!-- /gen -->. Every other CI job (for example
`coverage`) is advisory for branch protection but must still be green before a merge (`gate_merge_ready`).
```

Then:
```bash
python3 tools/gates/gate_state_docs.py --write && python3 tools/gates/gate_state_docs.py; echo rc=$?
git diff --stat docs/superpowers/plans/2026-09-14-roadmap.md README.md
python3 tools/gates/gate_comments.py --files docs/superpowers/plans/2026-09-14-roadmap.md README.md
```
Expected: rc 0 (the strategy-plan region reads `… ; \`strict-delivery-b11\` pending (this block)`); the F4 cell now reads `DONE 2026-10-15 — PR #138 → 7265059`, the heading count equals `gh issue list --state open --limit 1000 --json number | python3 -c 'import json,sys; print(len(json.load(sys.stdin)))'`. Run the test file again → all PASS.

- [ ] **Step 6: Wire in, commit, mutation, pin**

`run_all.py` `GATES`: add `"state_docs"` after `"comments"`. `gates.yml`: GH_TOKEN and fetch-depth 0 are already set (Task 1); add `state_docs` to any `--only` list. README gates table:
```markdown
| `gate_state_docs` | regenerates every `<!-- gen:… -->` region of the roadmap and README (open issues, required checks, DRAM, owner table, plan status); drift fails on a branch; on `main` only `dram`/`owner-table` fail and gh/tag drift WARNs; `--write` is the block's last commit before the final review | CI, `block.js`, CHECKPOINT |
```
```bash
python3 tools/gates/run_all.py && git add tools/gates docs/superpowers/plans/2026-09-14-roadmap.md README.md .github/workflows/gates.yml docs/superpowers/state && \
git commit -m "gates: gate_state_docs with six generators; roadmap and README facts generated (SD-S4-T4)"
```
Mutation: `sed -i.bak 's/^                    if new == body:$/                    if True:/' tools/gates/gate_state_docs.py`; test `test_drift_fails_and_write_regenerates` PASS / FAIL / PASS in `mutations/SD-S4-T4.txt`; `regedit.py set SD-S4-T4 pinned S4-T4 --pin-test tools/gates/tests/test_gate_state_docs.py::StateDocsTest::test_drift_fails_and_write_regenerates --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T4.txt`; commit `state: SD-S4-T4 pinned (mutation transcript)`.

### Task 5: `gate_merge_ready`, the PR-verb hook branch, `git-pre-push.sh`

**Register:** SD-S4-T5. **Siblings:** git pre-push; `gh pr ready`; `gh pr merge`; `git merge` on `main`; StatusContext checks. **Gates:** `run_all.py`, gate-test suite.

**Files:**
- Create: `tools/gates/gate_merge_ready.py`, `tools/gates/hooks/pr_verbs.py`, `tools/gates/hooks/git-pre-push.sh`, `tools/gates/tests/test_gate_merge_ready.py`
- Modify: `tools/gates/hooks/pre_tool_use.py` (Bash branch), `tools/gates/install-hooks.sh`, `tools/gates/tests/test_hooks_s4.py`, `tools/gates/run_all.py`, `tools/gates/README.md`

**Interfaces:**
- Consumes: Task 4 gitinfo helpers; `registers.load_findings/load_claims/load_decisions/grant_for`; `gitinfo.head_sha/branch/changed_files/pr_for_branch/tags_at`; `config.load` (`merge_markers`, `tag_pattern`).
- Produces:
  - `gate_merge_ready.check(args)`, argv `[--pr N] [--allow-draft | --pre-push | --post]`.
  - `gate_merge_ready.OK_CONCLUSIONS = {"SUCCESS", "NEUTRAL", "SKIPPED"}`, `BRANCH_RE`, `find_block_plan(root, plan, block) -> path`, `tasks_in_block(plan_file, block) -> list[str]`, `block_rows(findings, plan_stem, tasks) -> list[dict]`, `checks_of(pr) -> list[(name, conclusion)]`.
  - `pr_verbs.gates_for(command: str) -> list[list[str]]`, `pr_verbs.run(command: str, cwd: str) -> (rc: int, text: str)` (rc 2 = block).
  - Final reports: `docs/superpowers/state/<plan>/review/final/{reviewer,verifier}.md`, line 1 `verdict: clean`, line 2 `head: <sha>`; that sha must be an ancestor of the PR head, and only files under `docs/superpowers/state/` may change after it.

- [ ] **Step 1: Register**

`regedit.py set SD-S4-T5 in-progress S4-T5`.

- [ ] **Step 2: Write the failing tests**

`tools/gates/tests/test_gate_merge_ready.py`:
```python
import copy
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402
from tools.gates import gate_merge_ready as gmr  # noqa: E402

PLAN = "docs/superpowers/plans/2026-10-17-demo.md"
OWNER = {"kind": "plan", "plan": "2026-10-17-demo", "task": 1}
PIN = {"test": "t", "transcript": "docs/superpowers/state/demo/mutations/SD-DEMO-1.txt"}


def reports(head, verdict="clean"):
    return {"docs/superpowers/state/demo/review/final/%s.md" % k: "verdict: %s\nhead: %s\n\nbody\n" % (verdict, head)
            for k in ("reviewer", "verifier")}


def fixture(row_state="pinned", claims=None, decisions="grant", report_files=None):
    rows = [s4kit.finding("SD-DEMO-1", state=row_state, owner=OWNER, pin=PIN if row_state == "pinned" else None)]
    root = s4kit.make_repo(files={PLAN: "# Demo\n\n### Preamble\n\n## Block b1 — demo\n\n### Task 1: thing\n"},
                           findings=rows, claims=claims,
                           decisions=[s4kit.grant("demo")] if decisions == "grant" else [])
    s4kit.sh(root, "git", "update-ref", "refs/remotes/origin/main", "HEAD")
    s4kit.sh(root, "git", "checkout", "-qb", "block/demo-b1")
    head1 = s4kit.commit(root, {"src/x.c": "int x;\n"})
    head2 = s4kit.commit(root, reports(head1) if report_files is None else report_files)
    pr = {"number": 7, "title": "demo", "body": "Clean body.", "state": "OPEN", "isDraft": False,
          "headRefOid": head2, "headRefName": "block/demo-b1", "baseRefName": "main", "mergedAt": None,
          "mergeCommit": None, "statusCheckRollup": [
              {"__typename": "CheckRun", "name": "hygiene", "status": "COMPLETED", "conclusion": "SUCCESS"},
              {"__typename": "CheckRun", "name": "coverage", "status": "COMPLETED", "conclusion": "SUCCESS"}]}
    gh = {"pr view": {"stdout": pr}, "repo view": {"stdout": {"nameWithOwner": "o/r"}},
          "api repos/o/r/branches/main": {"stdout": {"protection": {"required_status_checks": {"contexts": ["hygiene"]}}}}}
    return root, gh, pr, head1


def run(root, gh, argv=None):
    with s4kit.env(root, gh):
        return s4kit.run_main(gmr.main, argv or ["--pr", "7"])


def with_pr(gh, **kw):
    g = copy.deepcopy(gh)
    g["pr view"]["stdout"].update(kw)
    return g


class MergeReadyTest(unittest.TestCase):
    def test_clean_block_passes(self):
        root, gh, _, _ = fixture()
        rc, out = run(root, gh)
        self.assertEqual(rc, 0, out)

    def test_advisory_red_fails(self):
        root, gh, pr, _ = fixture()
        roll = copy.deepcopy(pr["statusCheckRollup"])
        roll[1]["conclusion"] = "FAILURE"
        rc, out = run(root, with_pr(gh, statusCheckRollup=roll))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL merge_ready.check.coverage", out)

    def test_pending_check_fails(self):
        root, gh, pr, _ = fixture()
        roll = copy.deepcopy(pr["statusCheckRollup"])
        roll[1].update(status="IN_PROGRESS", conclusion="")
        rc, out = run(root, with_pr(gh, statusCheckRollup=roll))
        self.assertIn("FAIL merge_ready.check.coverage", out)

    def test_status_context_failure_fails(self):
        root, gh, pr, _ = fixture()
        roll = pr["statusCheckRollup"] + [{"__typename": "StatusContext", "context": "ext", "state": "FAILURE"}]
        rc, out = run(root, with_pr(gh, statusCheckRollup=roll))
        self.assertIn("FAIL merge_ready.check.ext", out)

    def test_required_check_missing_fails(self):
        root, gh, pr, _ = fixture()
        rc, out = run(root, with_pr(gh, statusCheckRollup=pr["statusCheckRollup"][1:]))
        self.assertIn("FAIL merge_ready.required.hygiene", out)

    def test_no_checks_fails(self):
        root, gh, _, _ = fixture()
        rc, out = run(root, with_pr(gh, statusCheckRollup=[]))
        self.assertIn("FAIL merge_ready.checks", out)

    def test_body_marker_fails(self):
        root, gh, _, _ = fixture()
        rc, out = run(root, with_pr(gh, body="Ready. One item: fix before merge."))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL merge_ready.body.fix-before-merge", out)

    def test_draft_fails_but_allow_draft_passes(self):
        root, gh, _, _ = fixture()
        rc, out = run(root, with_pr(gh, isDraft=True))
        self.assertIn("FAIL merge_ready.draft", out)
        rc, out = run(root, with_pr(gh, isDraft=True), ["--pr", "7", "--allow-draft"])
        self.assertEqual(rc, 0, out)

    def test_missing_verifier_report_fails(self):
        root, gh, _, _ = fixture(report_files={"docs/superpowers/state/demo/review/final/reviewer.md": "verdict: clean\nhead: 0000000\n"})
        rc, out = run(root, gh)
        self.assertIn("FAIL merge_ready.report.verifier", out)

    def test_report_not_clean_fails(self):
        root, gh, _, head1 = fixture()
        head3 = s4kit.commit(root, reports(head1, verdict="changes"))
        rc, out = run(root, with_pr(gh, headRefOid=head3))
        self.assertIn("FAIL merge_ready.report.reviewer", out)

    def test_fenced_heading_does_not_end_the_block(self):
        root = s4kit.make_repo(files={PLAN: "## Block b1 — demo\n\n### Task 1: a\n\n```\n## not a heading\n```\n\n### Task 2: b\n"})
        self.assertEqual(gmr.tasks_in_block(os.path.join(root, PLAN), 1), ["1", "2"])

    def test_code_change_after_reviewed_head_fails(self):
        root, gh, _, _ = fixture()
        head3 = s4kit.commit(root, {"src/y.c": "int y;\n"})
        rc, out = run(root, with_pr(gh, headRefOid=head3))
        self.assertIn("outside docs/superpowers/state/", out)

    def test_local_head_differs_fails(self):
        root, gh, _, _ = fixture()
        s4kit.commit(root, {"docs/superpowers/state/demo/note.md": "n\n"})
        rc, out = run(root, gh)
        self.assertIn("FAIL merge_ready.head", out)

    def test_row_not_pinned_fails(self):
        root, gh, _, _ = fixture(row_state="in-progress")
        rc, out = run(root, gh)
        self.assertIn("FAIL merge_ready.row.SD-DEMO-1", out)

    def test_no_rows_fails(self):
        root, gh, _, _ = fixture()
        s4kit.commit(root, {"docs/superpowers/state/findings.yml": "[]\n"})
        rc, out = run(root, with_pr(gh, headRefOid=s4kit.sh(root, "git", "rev-parse", "HEAD")))
        self.assertIn("FAIL merge_ready.rows", out)

    def test_stale_claim_on_changed_file_fails(self):
        root, gh, _, _ = fixture(claims=[s4kit.claim("C-demo-1", "docs/a.md", ["src/x.c"], state="stale")])
        rc, out = run(root, gh)
        self.assertIn("FAIL merge_ready.claim.C-demo-1", out)

    def test_grant_missing_fails(self):
        root, gh, _, _ = fixture(decisions=None)
        rc, out = run(root, gh)
        self.assertIn("FAIL merge_ready.grant", out)

    def test_branch_not_block_fails(self):
        root, gh, _, _ = fixture()
        rc, out = run(root, with_pr(gh, headRefName="feature/x"))
        self.assertIn("FAIL merge_ready.branch", out)

    def test_post_tag_missing_fails_and_present_passes(self):
        root, gh, _, head1 = fixture(row_state="merged")
        sha = s4kit.sh(root, "git", "rev-parse", "HEAD")
        g = with_pr(gh, state="MERGED", mergeCommit={"oid": sha}, mergedAt="2026-10-20T00:00:00Z")
        rc, out = run(root, g, ["--post", "--pr", "7"])
        self.assertIn("FAIL merge_ready.tag.demo-b1", out)
        s4kit.sh(root, "git", "tag", "-a", "demo-b1", sha, "-m", "t")
        rc, out = run(root, g, ["--post", "--pr", "7"])
        self.assertEqual(rc, 0, out)

    def test_prepush_no_pr_passes(self):
        root, gh, _, _ = fixture()
        g = dict(gh, **{"pr view": {"rc": 1, "stderr": 'no pull requests found for branch "block/demo-b1"'}})
        rc, out = run(root, g, ["--pre-push"])
        self.assertEqual(rc, 0, out)

    def test_prepush_draft_passes_without_checks(self):
        root, gh, _, _ = fixture()
        rc, out = run(root, with_pr(gh, isDraft=True, body="draft: fix before merge"), ["--pre-push"])
        self.assertEqual(rc, 0, out)

    def test_prepush_ready_pr_checks_static_rules_only(self):
        root, gh, pr, _ = fixture()
        rc, out = run(root, with_pr(gh, statusCheckRollup=[]), ["--pre-push"])
        self.assertEqual(rc, 0, out)
        rc, out = run(root, with_pr(gh, body="TODO"), ["--pre-push"])
        self.assertIn("FAIL merge_ready.body.TODO", out)


if __name__ == "__main__":
    unittest.main()
```
Run: `python3 -m unittest tools/gates/tests/test_gate_merge_ready.py -v` → ImportError.

- [ ] **Step 3: Write `gate_merge_ready.py`**

```python
#!/usr/bin/env python3
"""gate_merge_ready -- machine-checked merge readiness of one block PR (spec sections 3.4, 7, 11.2).

Modes:
  (default)      before gh pr merge: PR open and not a draft, local HEAD == PR head, body free of
                 config.merge_markers, every register row of the block's tasks pinned / closed /
                 deferred with an answered ruling, claim rows on changed files verified, final
                 reviewer and verifier reports clean for the head, every check run green
                 (advisory included) and every required check present, grant:<plan> answered yes.
  --allow-draft  before gh pr ready: the same, except the PR may still be a draft.
  --pre-push     only for a block branch whose PR is no longer a draft: the static rules against
                 the local HEAD, no check runs (the pushed commits have none yet).
  --post         after the merge (needs --pr): PR merged, rows merged or deferred, tag <plan>-b<N>
                 on the merge commit.

usage: gate_merge_ready.py [--json] [--pr N] [--allow-draft | --pre-push | --post]
"""
import argparse
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from tools.gates.lib import GateError, Result, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as cfgmod  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

OK_CONCLUSIONS = {"SUCCESS", "NEUTRAL", "SKIPPED"}
BRANCH_RE = re.compile(r"^block/(?P<plan>[a-z0-9-]+)-b(?P<block>\d+)$")
BLOCK_H = re.compile(r"^## Block b(\d+)\b")
TASK_H = re.compile(r"^### Task (\d+)\b")
STATE_DIR = "docs/superpowers/state"
PLANS_DIR = "docs/superpowers/plans"


def find_block_plan(root, plan, block):
    hits = []
    for path in sorted(glob.glob(os.path.join(root, PLANS_DIR, "*%s*.md" % plan))):
        for line in _prose(path):
            m = BLOCK_H.match(line)
            if m and int(m.group(1)) == block:
                hits.append(path)
                break
    if len(hits) != 1:
        raise GateError("plan %s block b%d: expected one plan file with '## Block b%d', found %d"
                        % (plan, block, block, len(hits)))
    return hits[0]


def _prose(path):
    """The lines of a Markdown file outside fenced code blocks."""
    out, fence = [], False
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("```"):
                fence = not fence
                continue
            if not fence:
                out.append(line)
    return out


def tasks_in_block(plan_file, block):
    tasks, inside = [], False
    for line in _prose(plan_file):
        m = BLOCK_H.match(line)
        if m:
            inside = int(m.group(1)) == block
            continue
        if line.startswith("## "):
            inside = False
            continue
        t = TASK_H.match(line)
        if inside and t:
            tasks.append(t.group(1))
    if not tasks:
        raise GateError("%s: block b%d has no '### Task <n>' heading" % (plan_file, block))
    return tasks


def block_rows(findings, plan_stem, tasks):
    return [f for f in findings if isinstance(f.get("owner"), dict) and f["owner"].get("kind") == "plan"
            and f["owner"].get("plan") == plan_stem and str(f["owner"].get("task")) in tasks]


def checks_of(pr):
    out = []
    for c in pr.get("statusCheckRollup") or []:
        if c.get("__typename") == "StatusContext" or "context" in c:
            out.append((c.get("context", "?"), (c.get("state") or "").upper()))
        else:
            out.append((c.get("name", "?"), (c.get("conclusion") or c.get("status") or "").upper()))
    return out


def _answered(decisions, did):
    return any(d.get("id") == did and d.get("answer") is not None for d in decisions)


def _rows(r, rows, post, decisions):
    if not rows:
        r.fail("merge_ready.rows", "no findings-register row is owned by this block's tasks")
        return
    for f in rows:
        st, rid = f.get("state"), "merge_ready.row.%s" % f["id"]
        if post:
            ok = st in ("merged", "deferred")
        elif st == "deferred":
            ok = bool(f.get("ruling")) and _answered(decisions, f["ruling"])
        else:
            ok = st in ("pinned", "closed")
        if ok:
            r.passed(rid)
        else:
            r.fail(rid, "state %s (ruling %s)" % (st, f.get("ruling")))


def _body(r, pr, markers):
    body = pr.get("body") or ""
    for mk in markers:
        rid = "merge_ready.body.%s" % mk.replace(" ", "-")
        if re.search(r"(?<![A-Za-z])%s(?![A-Za-z])" % re.escape(mk), body, re.I):
            r.fail(rid, "PR #%s body contains '%s'" % (pr.get("number"), mk))
        else:
            r.passed(rid)


def _claims(r, root, base, head):
    changed = set(gitinfo.changed_files(root, base, head))
    bad = 0
    for c in registers.load_claims(root):
        touched = c.get("doc") in changed or bool(changed & set(c.get("cites") or []))
        if touched and c.get("state") != "verified":
            bad += 1
            r.fail("merge_ready.claim.%s" % c["id"], "state %s; the block changes its doc or a cited file"
                   % c.get("state"))
    if not bad:
        r.passed("merge_ready.claims.%d-changed-files" % len(changed))


def _reports(r, root, plan, head):
    for kind in ("reviewer", "verifier"):
        rel = "%s/%s/review/final/%s.md" % (STATE_DIR, plan, kind)
        rid = "merge_ready.report.%s" % kind
        path = os.path.join(root, rel)
        if not os.path.isfile(path):
            r.fail(rid, "%s missing" % rel)
            continue
        with open(path, encoding="utf-8") as fh:
            lines = fh.read().split("\n")
        if lines[0].strip() != "verdict: clean":
            r.fail(rid, "%s: first line is %r, not 'verdict: clean'" % (rel, lines[0][:40]))
            continue
        m = re.match(r"^head: ([0-9a-f]{7,40})$", lines[1].strip()) if len(lines) > 1 else None
        if m is None:
            r.fail(rid, "%s: second line must be 'head: <sha>'" % rel)
            continue
        try:
            if not gitinfo.is_ancestor(root, m.group(1), head):
                r.fail(rid, "reviewed head %s is not an ancestor of %s" % (m.group(1)[:7], head[:7]))
                continue
            later = [f for f in gitinfo.changed_files(root, m.group(1), head)
                     if not f.startswith(STATE_DIR + "/")]
        except GateError as e:
            r.fail(rid, str(e))
            continue
        if later:
            r.fail(rid, "%d file(s) outside %s/ changed after the reviewed head %s: %s"
                   % (len(later), STATE_DIR, m.group(1)[:7], ", ".join(later[:3])))
        else:
            r.passed(rid)


def _checks(r, root, pr):
    got = checks_of(pr)
    if not got:
        r.fail("merge_ready.checks", "PR #%s has no check runs" % pr.get("number"))
        return
    for name, concl in got:
        if concl in OK_CONCLUSIONS:
            r.passed("merge_ready.check.%s" % name)
        else:
            r.fail("merge_ready.check.%s" % name, "%s (advisory jobs count too)" % (concl or "no conclusion"))
    names = {n for n, _ in got}
    for req in gitinfo.required_checks(root):
        if req not in names:
            r.fail("merge_ready.required.%s" % req, "required check has no run on this PR")


def _grant(r, root, plan):
    g = registers.grant_for(registers.load_decisions(root), plan)
    if g is None or not str(g.get("answer") or "").strip().lower().startswith("yes"):
        r.fail("merge_ready.grant", "no decision grant:%s answered yes in decisions.yml" % plan)
    else:
        r.passed("merge_ready.grant.%s" % g["id"])


def _tag(r, root, cfg, plan, block, pr):
    tag = "%s-b%d" % (plan, block)
    if not re.match(cfg.get("tag_pattern") or "$^", tag):
        r.fail("merge_ready.tag.pattern", "%s does not match config.tag_pattern" % tag)
        return
    sha = (pr.get("mergeCommit") or {}).get("oid")
    if not sha:
        r.fail("merge_ready.tag.%s" % tag, "PR has no merge commit")
    elif tag in gitinfo.tags_at(root, sha):
        r.passed("merge_ready.tag.%s" % tag)
    else:
        r.fail("merge_ready.tag.%s" % tag, "tag missing on merge commit %s" % sha[:7])


def check(args):
    ap = argparse.ArgumentParser(prog="gate_merge_ready")
    ap.add_argument("--pr")
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--allow-draft", action="store_true")
    mode.add_argument("--pre-push", action="store_true")
    mode.add_argument("--post", action="store_true")
    a = ap.parse_args(args)
    root = repo_root()
    cfg = cfgmod.load(root)
    r = Result("merge_ready")
    if a.pre_push:
        name = gitinfo.branch(root)
        m = BRANCH_RE.match(name)
        if m is None:
            r.passed("merge_ready.prepush.not-a-block-branch")
            return r
        pr = gitinfo.pr_for_branch(root)
        if pr is None:
            r.passed("merge_ready.prepush.no-pr")
            return r
        if pr.get("isDraft"):
            r.passed("merge_ready.prepush.draft")
            return r
        head = gitinfo.head_sha(root)
    else:
        if a.post and not a.pr:
            raise GateError("--post needs --pr <number> (the branch is gone after the merge)")
        pr = gitinfo.pr_view(root, a.pr or gitinfo.branch(root))
        m = BRANCH_RE.match(pr.get("headRefName") or "")
        if m is None:
            r.fail("merge_ready.branch", "head branch %r is not block/<plan>-b<N>" % pr.get("headRefName"))
            return r
        head = pr.get("headRefOid") or ""
        want = "MERGED" if a.post else "OPEN"
        if pr.get("state") != want:
            r.fail("merge_ready.state", "PR #%s is %s, expected %s" % (pr.get("number"), pr.get("state"), want))
            return r
        r.passed("merge_ready.state.%s" % want)
    plan, block = m.group("plan"), int(m.group("block"))
    plan_file = find_block_plan(root, plan, block)
    rows = block_rows(registers.load_findings(root), os.path.splitext(os.path.basename(plan_file))[0],
                      tasks_in_block(plan_file, block))
    if a.post:
        _rows(r, rows, True, [])
        _tag(r, root, cfg, plan, block, pr)
        return r
    if not a.pre_push:
        if pr.get("isDraft") and not a.allow_draft:
            r.fail("merge_ready.draft", "PR #%s is a draft" % pr.get("number"))
        local = gitinfo.head_sha(root)
        if local != head:
            r.fail("merge_ready.head", "local HEAD %s != PR head %s: push or pull first" % (local[:7], head[:7]))
    markers = cfg.get("merge_markers")
    if not markers:
        raise GateError("config.merge_markers missing or empty")
    _body(r, pr, markers)
    _rows(r, rows, False, registers.load_decisions(root))
    _claims(r, root, gitinfo.merge_base(root, gitinfo.origin_main(root), head), head)
    _reports(r, root, plan, head)
    if not a.pre_push:
        _checks(r, root, pr)
    _grant(r, root, plan)
    return r


def main(argv=None):
    return run_gate("merge_ready", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```
Run: `python3 -m unittest tools/gates/tests/test_gate_merge_ready.py -v` → PASS. If `test_prepush_no_pr_passes` fails, S1's `pr_for_branch` raises instead of returning `None` on `no pull requests found`: fix it there (return `None` when stderr contains `no pull requests found`) and ledger the correction.

- [ ] **Step 4: The PR-verb hook branch**

`tools/gates/hooks/pr_verbs.py`:
```python
"""PR verbs in a Bash command -> merge-readiness gates (spec section 8; S4).

gh pr merge -> gate_merge_ready --pr <sel>; gh pr ready -> --pr <sel> --allow-draft (skipped with
--undo); git push -> --pre-push; git merge -> refused when the current branch is main.
"""
import contextlib
import io
import os
import shlex
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))))
from tools.gates import gate_merge_ready  # noqa: E402

SEPARATORS = {"&&", "||", ";", "|", "&", "\n"}
VALUED = {"-t", "--subject", "-b", "--body", "-F", "--body-file", "--match-head-commit", "-A",
          "--author-email", "-R", "--repo"}
GIT_MERGE = ["__git_merge__"]


def segments(command):
    lex = shlex.shlex(command, posix=True, punctuation_chars=";&|")
    lex.whitespace_split = True
    seg, out = [], []
    for tok in lex:
        if tok in SEPARATORS or set(tok) <= set(";&|"):
            if seg:
                out.append(seg)
            seg = []
        else:
            seg.append(tok)
    if seg:
        out.append(seg)
    return out


def _selector(args):
    skip = False
    for t in args:
        if skip:
            skip = False
            continue
        if t in VALUED:
            skip = True
            continue
        if not t.startswith("-"):
            return t
    return None


def gates_for(command):
    out = []
    for seg in segments(command):
        while seg and "=" in seg[0] and not seg[0].startswith("-"):
            seg = seg[1:]
        if seg[:3] == ["gh", "pr", "merge"]:
            sel = _selector(seg[3:])
            out.append(["--pr", sel] if sel else [])
        elif seg[:3] == ["gh", "pr", "ready"] and "--undo" not in seg:
            sel = _selector(seg[3:])
            out.append((["--pr", sel] if sel else []) + ["--allow-draft"])
        elif seg[:2] == ["git", "push"]:
            out.append(["--pre-push"])
        elif seg[:2] == ["git", "merge"]:
            out.append(GIT_MERGE)
    return out


def run(command, cwd):
    todo = gates_for(command)
    if not todo:
        return 0, ""
    top = subprocess.run(["git", "rev-parse", "--show-toplevel"], cwd=cwd, capture_output=True,
                         text=True, check=False)
    if top.returncode != 0:
        return 2, "FAIL hook.pr-verbs: %s is not inside a git repository" % cwd
    root = top.stdout.strip()
    saved = os.environ.get("CLAUDE_PROJECT_DIR")
    os.environ["CLAUDE_PROJECT_DIR"] = root
    try:
        for argv in todo:
            if argv == GIT_MERGE:
                br = subprocess.run(["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=root,
                                    capture_output=True, text=True, check=False).stdout.strip()
                if br == "main":
                    return 2, "FAIL hook.git-merge: main changes only through gh pr merge (gate_merge_ready)"
                continue
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_merge_ready.main(argv)
            if rc != 0:
                return 2, buf.getvalue()
        return 0, ""
    finally:
        if saved is None:
            os.environ.pop("CLAUDE_PROJECT_DIR", None)
        else:
            os.environ["CLAUDE_PROJECT_DIR"] = saved
```
In `tools/gates/hooks/pre_tool_use.py`'s Bash branch, before S1's verb dispatch:
```python
from tools.gates.hooks import pr_verbs   # with the file's other imports
...
        rc, text = pr_verbs.run(tool_input.get("command", ""), payload.get("cwd") or os.getcwd())
        if rc != 0:
            print(text, file=sys.stderr)
            sys.exit(2)
```
(`tool_input` / `payload` are S1's names for the parsed stdin; use S1's.) Append to `tools/gates/tests/test_hooks_s4.py`:
```python
from tools.gates.hooks import pr_verbs  # noqa: E402
import test_gate_merge_ready as mr  # noqa: E402


class PrVerbsTest(unittest.TestCase):
    def test_gates_for_parses_compound_commands(self):
        self.assertEqual(pr_verbs.gates_for("cd x && gh pr merge 7 --squash"), [["--pr", "7"]])
        self.assertEqual(pr_verbs.gates_for("gh pr merge --subject 'a b' 7"), [["--pr", "7"]])
        self.assertEqual(pr_verbs.gates_for("gh pr ready"), [["--allow-draft"]])
        self.assertEqual(pr_verbs.gates_for("gh pr ready 7 --undo"), [])
        self.assertEqual(pr_verbs.gates_for("GIT_TRACE=1 git push origin HEAD"), [["--pre-push"]])
        self.assertEqual(pr_verbs.gates_for("git status; ls"), [])

    def test_gh_pr_merge_with_advisory_red_is_blocked(self):
        root, gh, pr, _ = mr.fixture()
        roll = [dict(c) for c in pr["statusCheckRollup"]]
        roll[1]["conclusion"] = "FAILURE"
        p = hook(root, "gh pr merge 7 --squash", gh=mr.with_pr(gh, statusCheckRollup=roll))
        self.assertEqual(p.returncode, 2, p.stderr)
        self.assertIn("FAIL merge_ready.check.coverage", p.stderr)

    def test_gh_pr_merge_clean_passes(self):
        root, gh, _, _ = mr.fixture()
        p = hook(root, "gh pr merge 7 --squash", gh=gh)
        self.assertEqual(p.returncode, 0, p.stderr)

    def test_git_merge_on_main_is_blocked_elsewhere_allowed(self):
        root = s4kit.make_repo()
        p = hook(root, "git merge feature")
        self.assertEqual(p.returncode, 2, p.stderr)
        s4kit.sh(root, "git", "checkout", "-qb", "block/demo-b1")
        p = hook(root, "git merge block/demo-b1-t1")
        self.assertEqual(p.returncode, 0, p.stderr)
```

- [ ] **Step 5: git pre-push hook**

`tools/gates/hooks/git-pre-push.sh` (executable):
```bash
#!/usr/bin/env bash
# git pre-push (spec section 8): the commit-time text gates, then merge readiness in --pre-push
# mode (static rules only, and only when the branch is block/<plan>-b<N> with a non-draft PR).
set -euo pipefail
root="$(git rev-parse --show-toplevel)"
here="$(python3 -c 'import os,sys; print(os.path.dirname(os.path.realpath(sys.argv[1])))' "${BASH_SOURCE[0]}")"
gates="$(dirname "$here")"
export CLAUDE_PROJECT_DIR="$root"
python3 "$gates/run_all.py" --only register,claims,comments,bench
python3 "$gates/gate_merge_ready.py" --pre-push
```
`tools/gates/install-hooks.sh`: next to S1's pre-commit install line, add
```bash
ln -sf "$root/tools/gates/hooks/git-pre-push.sh" "$(git rev-parse --git-common-dir)/hooks/pre-push"
```
(`$root` is S1's variable for the toplevel; use S1's name.) Append to `test_hooks_s4.py`:
```python
class PrePushTest(unittest.TestCase):
    def test_pre_push_blocks_a_plan06_comment_committed_with_no_verify(self):
        root = s4kit.make_repo(files={"docs/bench/next-bench-day.md":
                                      "# B\n\n## Open items\n\n- [ ] **1. X.** [hardware-only]\n"})
        bare = s4kit.make_repo()
        s4kit.sh(bare, "git", "config", "receive.denyCurrentBranch", "ignore")
        s4kit.sh(root, "git", "remote", "add", "origin", bare)
        hooks = os.path.join(root, ".git", "hooks")
        os.symlink(os.path.join(s4kit.REPO, "tools", "gates", "hooks", "git-pre-push.sh"),
                   os.path.join(hooks, "pre-push"))
        with open(os.path.join(s4kit.FIXTURES, "comments", "plan06.c")) as fh:
            s4kit.write(root, {"src/plan06.c": fh.read()})
        s4kit.sh(root, "git", "add", "-A")
        s4kit.sh(root, "git", "commit", "-qm", "x", "--no-verify")
        with s4kit.env(root):
            p = subprocess.run(["git", "push", "-q", "origin", "HEAD:refs/heads/x"], cwd=root,
                               capture_output=True, text=True, env=dict(os.environ), check=False)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("comments.src/plan06.c:1", p.stdout + p.stderr)
```
Run: `python3 -m unittest tools/gates/tests/test_hooks_s4.py -v` → PASS.

- [ ] **Step 6: Wire in, commit, mutation, pin**

`run_all.py` `GATES`: add `"merge_ready"` after `"bench"`; never add it to the CI `--only` list (it judges CI's own result). README gates table:
```markdown
| `gate_merge_ready` | PR body markers, block rows pinned/closed/deferred-with-ruling, claims on changed files verified, final reviewer + verifier `verdict: clean` for the head, every check run green incl. advisory + every required check present, grant; `--allow-draft` for `gh pr ready`, `--pre-push`, `--post` (tag `<plan>-b<N>` on the merge commit) | `gh pr ready`/`gh pr merge` hook, git pre-push, `postmerge.py` |
```
```bash
python3 tools/gates/run_all.py --only register,claims,comments,bench,state_docs,tools_py && \
git add --chmod=+x tools/gates/hooks/git-pre-push.sh && git add tools/gates docs/superpowers/state && \
git commit -m "gates: gate_merge_ready + PR-verb hook branch + git pre-push (SD-S4-T5)"
```
Mutation: `sed -i.bak 's/^        if concl in OK_CONCLUSIONS:$/        if True:/' tools/gates/gate_merge_ready.py`; test `test_advisory_red_fails` PASS / FAIL / PASS in `mutations/SD-S4-T5.txt`; `regedit.py set SD-S4-T5 pinned S4-T5 --pin-test tools/gates/tests/test_gate_merge_ready.py::MergeReadyTest::test_advisory_red_fails --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T5.txt`; commit `state: SD-S4-T5 pinned (mutation transcript)`.


### Task 6: `postmerge.py`

**Register:** SD-S4-T6. **Siblings:** tag already on the merge commit; tag on another commit; dirty worktree; worktree with submodules; a re-run after success. **Gates:** `run_all.py`, gate-test suite.

`main` takes no commit after the merge (umbrella correction 2026-10-17), so `postmerge.py` changes only refs, the filesystem outside the tree, and two tracked files that it leaves **uncommitted**: `docs/superpowers/state/findings.yml` (the block's rows set `merged`) and `docs/superpowers/state/<plan>/ledger.md` (one line `state pending commit: <plan>-b<N> rows <ids> merged`). The next block's first step commits both on its own branch; `gate_checkpoint --post` (S2) tolerates exactly those two modified files.

**Files:**
- Create: `tools/gates/postmerge.py`, `tools/gates/tests/test_postmerge.py`
- Modify: `tools/gates/README.md`

**Interfaces:**
- Consumes: `gate_merge_ready.find_block_plan/tasks_in_block/block_rows/main`; `run_all.GATES`, `run_all.run`; gitinfo (`pr_view`, `is_ancestor`, `tag_list`, `rev`, `worktrees`); `registers.load_findings/validate_finding`; `config.load`.
- Produces: `postmerge.main(argv) -> int`, argv `--plan <slug> --block <N> --pr <number>` (umbrella CLI table); `postmerge.STEPS` (ordered `(name, fn)`); `postmerge.MAIN_GATES`; `postmerge.PENDING = (FINDINGS, "docs/superpowers/state/<plan>/ledger.md")`. Output: one `PASS postmerge.<step>: <detail>` / `FAIL postmerge.<step>: <why>` per step; stops at the first FAIL (rc 1).
- Step order: `verify_merged`, `sync_main`, `tag`, `archive`, `remove_worktrees`, `delete_branches`, `register_merged`, `ledger_pending`, `gates_on_main`, `post_check`. (The S4 brief put the tag after the state regeneration; with the regeneration moved onto the branch, the tag comes right after the sync, before any cleanup, so a failed tag leaves the branch and worktrees in place.)

- [ ] **Step 1: Register**

`regedit.py set SD-S4-T6 in-progress S4-T6`. Block b10's rows (SD-S4-T1..T3) merged before `postmerge.py` existed and are still `pinned`: after Step 3 passes, run `regedit.py set SD-S4-T<n> merged postmerge-b10` for n = 1, 2, 3 (the one hand-run of the post-merge row edit; ruling ledgered with cost-if-wrong "a row reads merged one block early").

- [ ] **Step 2: Write the failing tests**

`tools/gates/tests/test_postmerge.py`:
```python
import os
import sys
import tempfile
import unittest
from unittest import mock

import yaml

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402
from tools.gates import postmerge  # noqa: E402

PLAN = "docs/superpowers/plans/2026-10-17-demo.md"
LEDGER = "docs/superpowers/state/demo/ledger.md"
OWNER = {"kind": "plan", "plan": "2026-10-17-demo", "task": 1}
ARGV = ["--plan", "demo", "--block", "1", "--pr", "7"]


def world(tag_elsewhere=False, dirty_worktree=False):
    row = s4kit.finding("SD-DEMO-1", state="pinned", owner=OWNER, pin={"test": "t", "transcript": "x.txt"})
    root = s4kit.make_repo(files={PLAN: "# Demo\n\n## Block b1 — demo\n\n### Task 1: thing\n",
                                  LEDGER: "# ledger\n"}, findings=[row])
    bare = tempfile.mkdtemp(prefix="s4bare-")
    s4kit.sh(bare, "git", "init", "-q", "--bare", "-b", "main")
    s4kit.sh(root, "git", "remote", "add", "origin", bare)
    s4kit.sh(root, "git", "push", "-q", "origin", "main")
    s4kit.sh(root, "git", "checkout", "-qb", "block/demo-b1")
    s4kit.commit(root, {"src/x.c": "int x;\n"})
    s4kit.sh(root, "git", "push", "-q", "origin", "block/demo-b1", "block/demo-b1:main")
    sha = s4kit.sh(root, "git", "rev-parse", "HEAD")
    s4kit.sh(root, "git", "checkout", "-q", "main")
    s4kit.sh(root, "git", "branch", "block/demo-b1-t1", "block/demo-b1")
    wt = os.path.join(tempfile.mkdtemp(prefix="s4wt-"), "wt1")
    s4kit.sh(root, "git", "worktree", "add", "-q", wt, "block/demo-b1-t1")
    s4kit.write(wt, {".superpowers/sdd/demo/t1.md": "t1\n"})
    s4kit.write(root, {".superpowers/sdd/demo/ledger-scratch.md": "scratch\n"})
    if dirty_worktree:
        s4kit.write(wt, {"src/x.c": "int changed;\n"})
    if tag_elsewhere:
        s4kit.sh(root, "git", "tag", "-a", "demo-b1", "main", "-m", "wrong")
    merged = {"number": 7, "state": "MERGED", "isDraft": False, "headRefName": "block/demo-b1",
              "headRefOid": sha, "mergedAt": "2026-10-20T00:00:00Z", "mergeCommit": {"oid": sha},
              "body": "", "statusCheckRollup": []}
    return root, bare, wt, sha, {"pr view 7": {"stdout": merged}}


def run(root, gh):
    with s4kit.env(root, gh), mock.patch.object(postmerge, "MAIN_GATES", ["comments"]):
        return s4kit.run_main(postmerge.main, ARGV)


def steps_of(out):
    return [l.split(":")[0].split()[1] for l in out.splitlines() if l.startswith(("PASS postmerge.", "FAIL postmerge."))]


class PostmergeTest(unittest.TestCase):
    def test_full_sequence(self):
        root, bare, wt, sha, gh = world()
        rc, out = run(root, gh)
        self.assertEqual(rc, 0, out)
        self.assertEqual(steps_of(out), ["postmerge." + n for n, _ in postmerge.STEPS])
        self.assertIn("demo-b1", s4kit.sh(bare, "git", "tag", "-l"))
        self.assertEqual(s4kit.sh(bare, "git", "branch", "--list", "block/demo-b1"), "")
        self.assertFalse(os.path.exists(wt))
        self.assertEqual(s4kit.sh(root, "git", "branch", "--list", "block/*"), "")
        self.assertTrue(os.path.isfile(os.path.join(root, ".superpowers/archive/demo/b1/main/ledger-scratch.md")))
        self.assertTrue(os.path.isfile(os.path.join(root, ".superpowers/archive/demo/b1/wt1/t1.md")))
        with open(os.path.join(root, postmerge.FINDINGS)) as fh:
            self.assertEqual(yaml.safe_load(fh)[0]["state"], "merged")
        with open(os.path.join(root, LEDGER)) as fh:
            self.assertIn("state pending commit: demo-b1 rows SD-DEMO-1 merged", fh.read())
        changed = sorted(s4kit.sh(root, "git", "diff", "--name-only").splitlines())
        self.assertEqual(changed, sorted([postmerge.FINDINGS, LEDGER]))
        self.assertEqual(s4kit.sh(root, "git", "ls-files", "--others", "--exclude-standard"), "")
        self.assertEqual(s4kit.sh(root, "git", "rev-parse", "HEAD"), sha)

    def test_rerun_after_success_passes_and_changes_nothing(self):
        root, bare, wt, sha, gh = world()
        run(root, gh)
        with open(os.path.join(root, LEDGER)) as fh:
            before = fh.read()
        rc, out = run(root, gh)
        self.assertEqual(rc, 0, out)
        with open(os.path.join(root, LEDGER)) as fh:
            self.assertEqual(fh.read(), before)

    def test_stops_at_first_fail(self):
        root, bare, wt, sha, gh = world()
        gh["pr view 7"]["stdout"]["state"] = "OPEN"
        rc, out = run(root, gh)
        self.assertEqual(rc, 1)
        self.assertEqual(steps_of(out), ["postmerge.verify_merged"])

    def test_tag_on_another_commit_fails_before_cleanup(self):
        root, bare, wt, sha, gh = world(tag_elsewhere=True)
        rc, out = run(root, gh)
        self.assertIn("FAIL postmerge.tag", out)
        self.assertNotIn("postmerge.delete_branches", out)
        self.assertTrue(os.path.exists(wt))

    def test_tag_already_on_merge_commit_passes(self):
        root, bare, wt, sha, gh = world()
        s4kit.sh(root, "git", "fetch", "-q", "origin")
        s4kit.sh(root, "git", "tag", "-a", "demo-b1", sha, "-m", "earlier run")
        rc, out = run(root, gh)
        self.assertEqual(rc, 0, out)

    def test_dirty_worktree_fails(self):
        root, bare, wt, sha, gh = world(dirty_worktree=True)
        rc, out = run(root, gh)
        self.assertIn("FAIL postmerge.remove_worktrees", out)
        self.assertNotIn("postmerge.delete_branches", out)


if __name__ == "__main__":
    unittest.main()
```
Run: `python3 -m unittest tools/gates/tests/test_postmerge.py -v` → ImportError.

- [ ] **Step 3: Write `postmerge.py`**

```python
#!/usr/bin/env python3
"""postmerge -- the spec section 3.4 post-merge sequence for one merged block.

usage: postmerge.py --plan <slug> --block <N> --pr <number>    (main checkout, run by the session)

Each step prints PASS/FAIL; the script stops at the first FAIL (rc 1). main takes no commit, so
the block's register rows are set merged in findings.yml and one ledger line is appended, both
left uncommitted for the next block branch ("state pending commit"). A re-run after a fix is
safe: a tag already on the merge commit, gone branches and worktrees, rows already merged and
the ledger line already present all pass.
"""
import argparse
import contextlib
import io
import os
import re
import shutil
import subprocess
import sys

import yaml

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
from tools.gates import gate_merge_ready as gmr  # noqa: E402
from tools.gates import run_all  # noqa: E402
from tools.gates.lib import GateError, repo_root  # noqa: E402
from tools.gates.lib import config as cfgmod  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

FINDINGS = "docs/superpowers/state/findings.yml"
MAIN_GATES = [g for g in run_all.GATES if g not in ("dispatch", "review_package", "plan", "merge_ready", "checkpoint")]


class StepFail(Exception):
    pass


class Ctx:
    def __init__(self, root, plan, block, pr):
        self.root, self.plan, self.block, self.pr = root, plan, block, pr
        self.branch = "block/%s-b%d" % (plan, block)
        self.tag = "%s-b%d" % (plan, block)
        self.ledger = "docs/superpowers/state/%s/ledger.md" % plan
        self.sha = None
        self.merged_ids = []


def _run(cwd, cmd):
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise StepFail("%s: %s" % (" ".join(cmd), (p.stderr or p.stdout).strip()))
    return p.stdout.strip()


def git(c, *a):
    return _run(c.root, ["git"] + list(a))


def _is_main_checkout(c, path):
    return os.path.realpath(path) == os.path.realpath(c.root)


def verify_merged(c):
    pr = gitinfo.pr_view(c.root, c.pr)
    if pr.get("state") != "MERGED":
        raise StepFail("PR #%s is %s" % (c.pr, pr.get("state")))
    if pr.get("headRefName") != c.branch:
        raise StepFail("PR #%s head is %s, not %s" % (c.pr, pr.get("headRefName"), c.branch))
    c.sha = (pr.get("mergeCommit") or {}).get("oid")
    if not c.sha:
        raise StepFail("PR #%s has no merge commit" % c.pr)
    return "PR #%s merged as %s" % (c.pr, c.sha[:7])


def sync_main(c):
    if git(c, "rev-parse", "--abbrev-ref", "HEAD") != "main":
        git(c, "checkout", "-q", "main")
    git(c, "fetch", "-q", "--prune", "--tags", "origin")
    git(c, "pull", "-q", "--no-rebase", "--ff-only", "origin", "main")
    if not gitinfo.is_ancestor(c.root, c.sha, "HEAD"):
        raise StepFail("main does not contain %s" % c.sha[:7])
    return "main at %s" % git(c, "rev-parse", "--short", "HEAD")


def tag(c):
    if not re.match(cfgmod.load(c.root).get("tag_pattern") or "$^", c.tag):
        raise StepFail("%s does not match config.tag_pattern" % c.tag)
    if c.tag in gitinfo.tag_list(c.root):
        if gitinfo.rev(c.root, c.tag) != c.sha:
            raise StepFail("tag %s exists on %s, not on the merge commit" % (c.tag, gitinfo.rev(c.root, c.tag)[:7]))
    else:
        git(c, "tag", "-a", c.tag, c.sha, "-m", "%s block %d (PR #%s)" % (c.plan, c.block, c.pr))
    git(c, "push", "-q", "origin", "refs/tags/%s" % c.tag)
    return "%s on %s" % (c.tag, c.sha[:7])


def archive(c):
    dest = os.path.join(c.root, ".superpowers", "archive", c.plan, "b%d" % c.block)
    sources = []
    for wt in gitinfo.worktrees(c.root):
        src = os.path.join(wt, ".superpowers", "sdd", c.plan)
        if os.path.isdir(src):
            sources.append(("main" if _is_main_checkout(c, wt) else os.path.basename(wt), src))
    if not sources:
        if os.path.isdir(dest):
            return "already archived"
        raise StepFail("no scratch workspace .superpowers/sdd/%s in any worktree" % c.plan)
    for name, src in sources:
        target = os.path.join(dest, name)
        if os.path.exists(target):
            raise StepFail("%s exists: archived before" % target)
        shutil.copytree(src, target)
        if name == "main":
            shutil.rmtree(src)
    return "%d workspace(s) -> %s" % (len(sources), os.path.relpath(dest, c.root))


def remove_worktrees(c):
    for wt in gitinfo.worktrees(c.root):
        if _is_main_checkout(c, wt):
            continue
        p = subprocess.run(["git", "worktree", "remove", wt], cwd=c.root, capture_output=True, text=True, check=False)
        if p.returncode != 0:
            if "submodule" not in p.stderr:
                raise StepFail("git worktree remove %s: %s" % (wt, p.stderr.strip()))
            dirty = _run(wt, ["git", "status", "--porcelain", "--ignore-submodules=none"])
            if dirty:
                raise StepFail("worktree %s has changes: %s" % (wt, dirty.splitlines()[0]))
            shutil.rmtree(wt)
            git(c, "worktree", "prune")
    left = [w for w in gitinfo.worktrees(c.root) if not _is_main_checkout(c, w)]
    if left:
        raise StepFail("worktrees left: %s" % ", ".join(left))
    return "main checkout only"


def delete_branches(c):
    p = subprocess.run(["git", "ls-remote", "--exit-code", "--heads", "origin", c.branch], cwd=c.root,
                       capture_output=True, text=True, check=False)
    if p.returncode == 0:
        git(c, "push", "-q", "origin", "--delete", c.branch)
    elif p.returncode != 2:
        raise StepFail("git ls-remote: %s" % p.stderr.strip())
    local = [b for b in git(c, "for-each-ref", "--format=%(refname:short)", "refs/heads/").split("\n")
             if b == c.branch or b.startswith(c.branch + "-t")]
    for b in local:
        git(c, "branch", "-D", b)
    return "remote and %d local branch(es) deleted" % len(local)


def register_merged(c):
    plan_file = gmr.find_block_plan(c.root, c.plan, c.block)
    stem = os.path.splitext(os.path.basename(plan_file))[0]
    findings = registers.load_findings(c.root)
    rows = gmr.block_rows(findings, stem, gmr.tasks_in_block(plan_file, c.block))
    if not rows:
        raise StepFail("no register row is owned by this block's tasks")
    moved = 0
    for f in rows:
        if f["state"] in ("pinned", "closed"):
            f["state"] = "merged"
            f["history"].append({"state": "merged", "sha": c.sha[:7], "by": "postmerge"})
            moved += 1
        elif f["state"] not in ("deferred", "merged"):
            raise StepFail("%s is %s: not mergeable" % (f["id"], f["state"]))
        if f["state"] == "merged":
            c.merged_ids.append(f["id"])
        errs = registers.validate_finding(f)
        if errs:
            raise StepFail("%s: %s" % (f["id"], "; ".join(errs)))
    if moved:
        path = os.path.join(c.root, FINDINGS)
        with open(path, encoding="utf-8") as fh:
            head = []
            for line in fh.read().split("\n"):
                if line.startswith("#") or not line.strip():
                    head.append(line)
                else:
                    break
        lead = "\n".join(head).rstrip("\n")
        with open(path, "w", encoding="utf-8") as fh:
            fh.write((lead + "\n" if lead.strip() else "")
                     + yaml.safe_dump(findings, sort_keys=False, allow_unicode=True, width=100))
    return "%d row(s) set merged, %d already merged (uncommitted)" % (moved, len(c.merged_ids) - moved)


def ledger_pending(c):
    path = os.path.join(c.root, c.ledger)
    if not os.path.isfile(path):
        raise StepFail("%s missing" % c.ledger)
    line = "state pending commit: %s rows %s merged" % (c.tag, ", ".join(c.merged_ids) or "none")
    with open(path, encoding="utf-8") as fh:
        text = fh.read()
    if line not in text:
        with open(path, "a", encoding="utf-8") as fh:
            fh.write(("" if text.endswith("\n") else "\n") + line + "\n")
    dirty = set()
    for args in (("diff", "--name-only"), ("diff", "--cached", "--name-only"),
                 ("ls-files", "--others", "--exclude-standard")):
        dirty |= {p for p in git(c, *args).split("\n") if p}
    extra = sorted(p for p in dirty if p not in (FINDINGS, c.ledger))
    if extra:
        raise StepFail("unexpected changes in the main checkout: %s" % ", ".join(extra))
    return line


def gates_on_main(c):
    res = run_all.run(MAIN_GATES, {})
    if not res.ok():
        raise StepFail(" | ".join("%s %s" % (l.id, l.reason) for l in res.lines if l.status == "FAIL")[:400])
    return "%d gate(s) green: %s" % (len(MAIN_GATES), ",".join(MAIN_GATES))


def post_check(c):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = gmr.main(["--pr", str(c.pr), "--post"])
    if rc != 0:
        raise StepFail(" | ".join(l for l in buf.getvalue().splitlines() if l.startswith("FAIL"))[:400])
    return "merge_ready --post green"


STEPS = [("verify_merged", verify_merged), ("sync_main", sync_main), ("tag", tag), ("archive", archive),
         ("remove_worktrees", remove_worktrees), ("delete_branches", delete_branches),
         ("register_merged", register_merged), ("ledger_pending", ledger_pending),
         ("gates_on_main", gates_on_main), ("post_check", post_check)]


def main(argv=None):
    ap = argparse.ArgumentParser(prog="postmerge")
    ap.add_argument("--plan", required=True)
    ap.add_argument("--block", required=True, type=int)
    ap.add_argument("--pr", required=True, type=int)
    a = ap.parse_args(argv)
    c = Ctx(repo_root(), a.plan, a.block, a.pr)
    for name, fn in STEPS:
        try:
            detail = fn(c)
        except (StepFail, GateError, OSError, yaml.YAMLError) as e:
            print("FAIL postmerge.%s: %s" % (name, e))
            return 1
        print("PASS postmerge.%s: %s" % (name, detail))
    return 0


if __name__ == "__main__":
    sys.exit(main())
```
Run: `python3 -m unittest tools/gates/tests/test_postmerge.py -v` → PASS. (On `main` the working-tree `findings.yml` now has the rows `merged`; `gate_merge_ready --post` reads the working tree, so `post_check` sees them.)

- [ ] **Step 4: README, commit, mutation, pin, CHECKPOINT b11**

`tools/gates/README.md`, new section:
```markdown
### After a merge: `postmerge.py`

`python3 tools/gates/postmerge.py --plan <slug> --block <N> --pr <n>`, from the main checkout, run by the
session. Steps, stopping at the first FAIL: verify merged, sync main, tag `<plan>-b<N>` + push, archive
`.superpowers/sdd/<plan>` into `.superpowers/archive/<plan>/b<N>/`, remove worktrees, delete branches,
set the block's register rows `merged`, append `state pending commit: …` to the plan ledger, run the
gates on main, `gate_merge_ready --pr <n> --post`. It leaves exactly two modified files
(`findings.yml`, the ledger); the next block branch commits them first. Re-run after fixing a FAIL.
```
```bash
python3 tools/gates/run_all.py --only register,claims,comments,bench,state_docs,tools_py && \
git add tools/gates docs/superpowers/state && git commit -m "gates: postmerge.py -- the spec 3.4 sequence, stop on first FAIL, state left pending (SD-S4-T6)"
```
Mutation: `sed -i.bak 's/^            return 1$/            continue/' tools/gates/postmerge.py` (the script stops stopping); test `test_stops_at_first_fail` PASS / FAIL / PASS in `mutations/SD-S4-T6.txt`; `regedit.py set SD-S4-T6 pinned S4-T6 --pin-test tools/gates/tests/test_postmerge.py::PostmergeTest::test_stops_at_first_fail --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T6.txt`; commit `state: SD-S4-T6 pinned (mutation transcript)`.

CHECKPOINT b11 (`gate_merge_ready` and `postmerge.py` are used from this block on): `python3 tools/gates/gate_state_docs.py --write` and commit (`state: regenerate state docs`) as the branch's last code-free commit → `python3 tools/gates/run_all.py` rc 0 → push → final reviewer (opus) + final verifier (Fable) write `docs/superpowers/state/strict-delivery/review/final/{reviewer,verifier}.md` (`verdict: clean`, `head: <sha>`), commit + push → `gh pr ready <n>` (hook: `gate_merge_ready --pr <n> --allow-draft`) → every check green → `gh pr merge <n> --squash` (hook: `gate_merge_ready --pr <n>` + grant `DEC-0001`) → `python3 tools/gates/postmerge.py --plan strict-delivery --block 11 --pr <n>` all PASS → handoff (`block: 11`, `next_task: S4 Task 7`, "state pending commit" noted) → ledger `CHECKPOINT b11` → `python3 tools/gates/gate_checkpoint.py --plan strict-delivery --post` rc 0 → status update + resume prompt → stop.

Resume prompt:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 12 (sub-plan S4, docs/superpowers/plans/2026-10-17-strict-delivery-S4-remaining-gates-workflows.md, Task 7); its Step 1 commits the state left pending by postmerge. Use superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

---

## Block b12 — Workflow scripts `audit.js`, `block.js` (tag `strict-delivery-b12`, branch `block/strict-delivery-b12`)

Tasks 7–9 are SERIAL (one test file grows across them). Facts for every task in this block:
- Workflow scripts are plain JavaScript run by the Workflow tool; the body may use `agent`, `parallel`, `pipeline`, `phase`, `log`, `args`; there is no filesystem or process access, and `Date.now()`, `Math.random()` and argless `new Date()` throw. So every shell action (reading the brief, running a gate, `git`, `gh`) is done by a **runner** agent: a controller-role mechanical step on opus at `effort: 'low'` that runs exactly the commands it is given and returns `{rc, output}`. Runner prompts are not role dispatches and carry no strict block.
- Input (umbrella CLI table): `{brief: <path>}`. The brief is a YAML file the session writes before invoking (formats in Tasks 7 and 8). The first runner reads it, runs `gate_plan.py --plan <path>` and the grant check, and returns the prompt-block files with their byte counts. The script refuses to start (`status: 'REFUSED'`) on a non-zero `gate_plan` or grant check, or on a prompt whose UTF-8 length differs from `wc -c` of its file (a garbled copy).
- Every role dispatch is built by `compose(role, model, fields)` in the umbrella's dispatch grammar and preceded by `preflight()`. A runner rebuilds the same prompt on disk from the header, `cat` of the prompt file and the task text, checks its byte length against the script's prompt, and runs `gate_dispatch.py --prompt <file> --prompts-dir ~/.claude/skills/strict-delivery/prompts`. So `gate_dispatch` runs whether or not the PreToolUse Agent hook sees Workflow spawns (Task 9 Step 2 probes that).
- Every `agent(...)` call passes a literal `model:`. block.js: implementers `'sonnet'` (fix rounds 4–5 `'opus'`), task and final reviewer `'opus'`, final verifier `'fable'` (the only Fable seat), runners `'opus'`. audit.js: every call `'opus'`.
- `gh` writes from a subagent may be refused by the auto-mode classifier (spec D6). A runner that is refused returns `rc 126` with `DENIED`. block.js then returns `{status: 'HANDOFF', next_commands: [...]}` with the exact remaining commands, and the session runs them itself.

### Task 7: `audit.js` + the workflow lint harness

**Register:** SD-S4-T7. **Siblings:** killed reviewer resume (progress file); synthesis editing a class statement; duplicate ids across areas; a reviewer row with no verifier verdict. **Gates:** gate-test suite (`test_workflows.py`).

**Files:**
- Create: `.claude/workflows/audit.js`, `tools/gates/tests/test_workflows.py`
- Modify: `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: `gate_plan.py --plan <path>`; `gate_dispatch.py --prompt <file> --prompts-dir <dir>`; `registers.grant_for`; prompt files `controller.md`, `audit-reviewer.md`, `audit-verifier.md`.
- Produces: workflow `audit`, input `{brief}`; audit brief YAML: `kind: audit`, `audit_id` (slug), `areas: [{id: <slug>, scope: <text>}]`; grant decision `grant:<audit_id>`; outputs under `docs/superpowers/state/audits/<audit_id>/<area>/{coverage.md,progress.md,review.md,rows.json,verify.md,verdicts.json}` and `docs/superpowers/state/audits/<audit_id>/{synthesis.md,rows.yml}`; return `{status: 'DONE', rows_file, rows, refuted, omitted_paths}` or `{status: 'REFUSED'|'BLOCKED', detail}`. `rows.yml` is a staging file (rows carry `owner: null`); it enters `findings.yml` only through triage decisions the owner answers.
- Lint helpers in `test_workflows.py`: `meta_literal(src) -> (literal, body)`, `calls(src, name) -> list[str]`, `node(*args, stdin)`, `eval_meta(literal) -> dict`.

- [ ] **Step 1: Branch, pending state, register**

```bash
git switch main && git pull --no-rebase --ff-only
git switch -c block/strict-delivery-b12          # carries the two files postmerge left modified
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/ledger.md
git commit -m "state: strict-delivery-b11 rows merged (pending from postmerge)"
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T7 in-progress S4-T7
```

- [ ] **Step 2: Write the failing lint tests**

`tools/gates/tests/test_workflows.py`:
```python
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s4kit  # noqa: E402

SCRIPTS = {"audit": ".claude/workflows/audit.js", "block": ".claude/workflows/block.js"}
MODELS = ("opus", "sonnet", "fable")


def read(rel):
    with open(os.path.join(s4kit.REPO, rel), encoding="utf-8") as fh:
        return fh.read()


def meta_literal(src):
    m = re.match(r"\s*export const meta = (\{.*?\n\})\n", src, re.S)
    if m is None:
        raise AssertionError("script does not begin with `export const meta = {...}`")
    return m.group(1), src[m.end():]


def node(*args, stdin=None):
    exe = shutil.which("node")
    if exe is None:
        raise AssertionError("node is not on PATH: the workflow lint cannot run (install Node.js)")
    return subprocess.run([exe] + list(args), input=stdin, capture_output=True, text=True, check=False)


def eval_meta(literal):
    code = ("const vm=require('vm');let s='';process.stdin.on('data',d=>s+=d).on('end',()=>{"
            "process.stdout.write(JSON.stringify(vm.runInNewContext('('+s+')',{})))})")
    p = node("-e", code, stdin=literal)
    if p.returncode != 0:
        raise AssertionError("meta is not a pure literal: %s" % p.stderr.strip()[-300:])
    return json.loads(p.stdout)


def calls(src, name="agent"):
    out = []
    for m in re.finditer(r"(?<![\w.])%s\(" % name, src):
        i, depth, quote = m.end(), 1, None
        while i < len(src) and depth:
            ch = src[i]
            if quote:
                if ch == "\\":
                    i += 2
                    continue
                if ch == quote:
                    quote = None
            elif ch in "'\"`":
                quote = ch
            elif ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            i += 1
        out.append(src[m.start():i])
    return out


def model_of(call):
    m = re.search(r"\bmodel:\s*'(%s)'" % "|".join(MODELS), call)
    return m.group(1) if m else None


class WorkflowLint(object):
    """Mixin: the checks every workflow script must pass. Subclasses set NAME."""
    NAME = None

    def src(self):
        return read(SCRIPTS[self.NAME])

    def test_parses_with_node(self):
        literal, body = meta_literal(self.src())
        tmp = os.path.join(tempfile.mkdtemp(prefix="wf-"), "%s.js" % self.NAME)
        with open(tmp, "w") as fh:
            fh.write("const meta = %s\n;(async () => {\n%s\n})()\n" % (literal, body))
        p = node("--check", tmp)
        self.assertEqual(p.returncode, 0, p.stderr)

    def test_meta_is_a_pure_literal_with_name_description_phases(self):
        meta = eval_meta(meta_literal(self.src())[0])
        self.assertEqual(meta["name"], self.NAME)
        self.assertTrue(meta["description"])
        self.assertTrue(meta["phases"])

    def test_phase_titles_match_meta(self):
        literal, body = meta_literal(self.src())
        titles = {p["title"] for p in eval_meta(literal)["phases"]}
        used = set(re.findall(r"\bphase\('([^']+)'\)", body)) | set(re.findall(r"\bphase: '([^']+)'", body))
        self.assertEqual(used, titles)

    def test_every_agent_call_names_a_literal_model(self):
        cs = calls(meta_literal(self.src())[1])
        self.assertTrue(cs)
        for c in cs:
            self.assertIsNotNone(model_of(c), "agent call without a literal model: %s" % c[:120])

    def test_compose_role_model_matches_the_call(self):
        for c in calls(meta_literal(self.src())[1]):
            m = re.search(r"compose\('(\w+)', '(\w+)'", c)
            if m:
                self.assertEqual(m.group(2), model_of(c), c[:120])

    def test_no_nondeterministic_builtins(self):
        body = meta_literal(self.src())[1]
        for bad in ("Date.now(", "Math.random(", "new Date()"):
            self.assertNotIn(bad, body)

    def test_refuses_without_gate_plan_and_grant(self):
        body = meta_literal(self.src())[1]
        self.assertIn("gate_plan_rc !== 0", body)
        self.assertIn("grant_rc !== 0", body)
        self.assertIn("halt('REFUSED'", body)


class AuditWorkflowTest(WorkflowLint, unittest.TestCase):
    NAME = "audit"

    def test_all_models_opus(self):
        self.assertEqual({model_of(c) for c in calls(meta_literal(self.src())[1])}, {"opus"})

    def test_phases_in_order(self):
        meta = eval_meta(meta_literal(self.src())[0])
        self.assertEqual([p["title"] for p in meta["phases"]],
                         ["Preflight", "Coverage", "Review", "Verify", "Synthesis"])

    def test_class_statements_are_checked_verbatim(self):
        body = meta_literal(self.src())[1]
        self.assertIn("got.get(id) !== cls", body)


if __name__ == "__main__":
    unittest.main()
```
Run: `python3 -m unittest tools/gates/tests/test_workflows.py -v` → errors: `.claude/workflows/audit.js` missing.

- [ ] **Step 3: Write `.claude/workflows/audit.js`**

```js
export const meta = {
  name: 'audit',
  description: 'Strict-delivery audit fan-out: per area a coverage list, an executed review and an adversarial verifier, then one synthesis that stages verbatim register rows',
  whenToUse: 'Only when the strict-delivery skill says to run an audit; input {brief: <path to the audit brief YAML>}',
  phases: [
    { title: 'Preflight', detail: 'read the brief and prompt files, gate_plan, grant' },
    { title: 'Coverage', detail: 'one opus audit reviewer per area writes its coverage list first' },
    { title: 'Review', detail: 'the area review against the coverage list, a progress file, rows by schema' },
    { title: 'Verify', detail: 'one opus adversarial verifier per reviewer re-runs every reproducer' },
    { title: 'Synthesis', detail: 'opus synthesis stages rows.yml; class statements stay verbatim' },
  ],
}

const A = args || {}
const PROMPTS_DIR = '~/.claude/skills/strict-delivery/prompts'
const PROMPT_FILE = { controller: 'controller', audit_reviewer: 'audit-reviewer', audit_verifier: 'audit-verifier' }
const BUDGET = { controller: [120000, 60], audit_reviewer: [120000, 90], audit_verifier: [120000, 90] }
const SEVERITIES = ['CRITICAL', 'HIGH', 'MEDIUM', 'LOW', 'INFO']

function halt(status, detail) { const e = new Error(detail); e.halt = { status, detail }; throw e }
function shq(s) { return "'" + String(s).replace(/'/g, "'\\''") + "'" }
function utf8len(s) { return unescape(encodeURIComponent(s)).length }

const RUN = { type: 'object', properties: { rc: { type: 'integer' }, output: { type: 'string' } }, required: ['rc', 'output'] }
const ROW = {
  type: 'object',
  properties: {
    id: { type: 'string', pattern: '^[A-Z0-9]+(-[A-Za-z0-9]+)+$' },
    severity: { type: 'string', enum: SEVERITIES },
    class: { type: 'string', minLength: 1 },
    siblings: { type: 'array', items: { type: 'string' } },
    cites: { type: 'array', items: { type: 'object', properties: { file: { type: 'string' }, lines: { type: 'string' }, sha: { type: 'string' } }, required: ['file', 'lines', 'sha'] } },
    repro: { type: ['string', 'null'] },
    evidence: { type: 'array', items: { type: 'string' } },
  },
  required: ['id', 'severity', 'class', 'siblings', 'cites', 'repro', 'evidence'],
}
const BRIEF = {
  type: 'object',
  properties: {
    kind: { type: 'string' }, audit_id: { type: 'string' },
    areas: { type: 'array', items: { type: 'object', properties: { id: { type: 'string' }, scope: { type: 'string' } }, required: ['id', 'scope'] } },
    gate_plan_rc: { type: 'integer' }, grant_rc: { type: 'integer' }, output: { type: 'string' },
    prompts: { type: 'object', properties: { controller: { type: 'string' }, 'audit-reviewer': { type: 'string' }, 'audit-verifier': { type: 'string' } }, required: ['controller', 'audit-reviewer', 'audit-verifier'] },
    prompt_bytes: { type: 'object', properties: { controller: { type: 'integer' }, 'audit-reviewer': { type: 'integer' }, 'audit-verifier': { type: 'integer' } }, required: ['controller', 'audit-reviewer', 'audit-verifier'] },
  },
  required: ['kind', 'audit_id', 'areas', 'gate_plan_rc', 'grant_rc', 'output', 'prompts', 'prompt_bytes'],
}
const COVERAGE = { type: 'object', properties: { coverage_path: { type: 'string' }, items: { type: 'array', minItems: 1, items: { type: 'string' } } }, required: ['coverage_path', 'items'] }
const REVIEWED = { type: 'object', properties: { progress_path: { type: 'string' }, rows_path: { type: 'string' }, rows: { type: 'array', items: ROW } }, required: ['progress_path', 'rows_path', 'rows'] }
const VERIFIED = {
  type: 'object',
  properties: {
    coverage_path: { type: 'string' }, verdicts_path: { type: 'string' },
    omitted_paths: { type: 'array', items: { type: 'string' } },
    verdicts: { type: 'array', items: { type: 'object', properties: { id: { type: 'string' }, verdict: { type: 'string', enum: ['confirmed', 'refuted', 'narrowed'] }, note: { type: 'string' } }, required: ['id', 'verdict', 'note'] } },
    new_rows: { type: 'array', items: ROW },
  },
  required: ['coverage_path', 'verdicts_path', 'omitted_paths', 'verdicts', 'new_rows'],
}
const SYNTH = { type: 'object', properties: { rows_file: { type: 'string' }, rows: { type: 'array', items: ROW } }, required: ['rows_file', 'rows'] }

async function run(label, phaseName, commands) {
  const p = 'You are a command runner for the strict-delivery audit workflow (a controller-role mechanical step). From the repository root, run exactly these shell commands in order with Bash, stopping at the first one that exits non-zero. Do not edit any file, do not retry, do not interpret, do not run anything else. Return rc = the exit code of the last command you ran (0 if all succeeded) and output = the last 80 lines of its combined stdout and stderr, verbatim. If the permission system refuses a command, return rc = 126 and output starting with DENIED and the refusal text.\n\n' + commands.map((c, i) => `${i + 1}. ${c}`).join('\n')
  const r = await agent(p, { label: `run ${label}`, phase: phaseName, schema: RUN, model: 'opus', effort: 'low' })
  if (!r) halt('BLOCKED', `runner "${label}" died`)
  return r
}

let S = null
function header(role, model, f) {
  return [`## Role: ${role}`, `## Brief: ${A.brief}`, '## Findings: none', '## Claims: none',
    `## Budget: tokens=${BUDGET[role][0]} minutes=${BUDGET[role][1]}`, `## Model: ${model}`, `## Report: ${f.report}`].join('\n')
}
function compose(role, model, f) {
  return [header(role, model, f), '## Strict block', S.prompts[PROMPT_FILE[role]], '## Task', f.task].join('\n')
}
async function preflight(label, phaseName, role, model, f) {
  const file = `.superpowers/sdd/audit-${S.audit_id}/dispatch/${label}.prompt.md`
  const r = await run(`preflight ${label}`, phaseName, [
    `mkdir -p .superpowers/sdd/audit-${S.audit_id}/dispatch`,
    `{ printf '%s\\n' ${shq(header(role, model, f))}; printf '## Strict block\\n'; cat ${PROMPTS_DIR}/${PROMPT_FILE[role]}.md; printf '\\n## Task\\n'; printf '%s' ${shq(f.task)}; } > ${file}`,
    `test "$(wc -c < ${file} | tr -d ' ')" = "${utf8len(compose(role, model, f))}"`,
    `python3 tools/gates/gate_dispatch.py --prompt ${file} --prompts-dir ${PROMPTS_DIR}`,
  ])
  if (r.rc !== 0) halt('BLOCKED', `gate_dispatch refused ${label}: ${r.output}`)
}

async function main() {
  phase('Preflight')
  if (typeof A.brief !== 'string' || !A.brief) halt('REFUSED', 'input must be {brief: <path>}')
  const grantCheck = "python3 -c \"import sys; sys.path.insert(0, '.'); from tools.gates.lib import registers; g = registers.grant_for(registers.load_decisions('.'), sys.argv[1]); sys.exit(0 if g and str(g.get('answer') or '').strip().lower().startswith('yes') else 1)\" <audit_id>"
  S = await agent(`You are a command runner for the strict-delivery audit workflow. Read the YAML audit brief ${A.brief} and return kind, audit_id and areas exactly as written. Then run, from the repository root: (1) python3 tools/gates/gate_plan.py --plan ${A.brief} and put its exit code in gate_plan_rc; (2) ${grantCheck} with <audit_id> replaced by the brief's audit_id, exit code in grant_rc; (3) for each name in controller, audit-reviewer, audit-verifier: cat ${PROMPTS_DIR}/<name>.md into prompts.<name> byte for byte, and wc -c < ${PROMPTS_DIR}/<name>.md into prompt_bytes.<name>. output = the last 40 lines of the gate_plan and grant outputs. Edit nothing.`,
    { label: 'run read brief', phase: 'Preflight', schema: BRIEF, model: 'opus', effort: 'low' })
  if (!S) halt('BLOCKED', 'the brief reader died')
  if (S.kind !== 'audit' || !/^[a-z0-9-]+$/.test(S.audit_id)) halt('REFUSED', `not an audit brief: kind=${S.kind} audit_id=${S.audit_id}`)
  if (!S.areas.length || S.areas.some(a => !/^[a-z0-9-]+$/.test(a.id))) halt('REFUSED', 'areas must be a non-empty list of {id: <slug>, scope}')
  if (S.gate_plan_rc !== 0) halt('REFUSED', `gate_plan is not clean on ${A.brief}: ${S.output}`)
  if (S.grant_rc !== 0) halt('REFUSED', `no answered grant:${S.audit_id} in decisions.yml`)
  for (const k of Object.keys(S.prompt_bytes)) {
    if (utf8len(S.prompts[k]) !== S.prompt_bytes[k]) halt('REFUSED', `prompt ${k}.md came back garbled (${utf8len(S.prompts[k])} != ${S.prompt_bytes[k]} bytes)`)
  }
  const OUT = `docs/superpowers/state/audits/${S.audit_id}`

  phase('Coverage')
  const results = await pipeline(S.areas,
    async (area) => {
      const f = { report: `${OUT}/${area.id}/coverage.md`, task: `Audit ${S.audit_id}, area ${area.id}: ${area.scope}. Write your coverage list to ${OUT}/${area.id}/coverage.md before reviewing anything: every call path in scope with its callers, every claim in scope, every reproducer you will build. Do not review yet. Return the path and the list items.` }
      await preflight(`${area.id}-coverage`, 'Coverage', 'audit_reviewer', 'opus', f)
      const cov = await agent(compose('audit_reviewer', 'opus', f), { label: `${area.id} coverage opus`, phase: 'Coverage', schema: COVERAGE, model: 'opus' })
      if (!cov) halt('BLOCKED', `coverage for ${area.id} died`)
      return cov
    },
    async (cov, area) => {
      const f = { report: `${OUT}/${area.id}/review.md`, task: `Audit ${S.audit_id}, area ${area.id}. Review against your coverage list ${cov.coverage_path}. Progress file: ${OUT}/${area.id}/progress.md; if it exists, read it first and continue from the first item not marked done (a killed reviewer resumes there); append one line per item as you finish it. For every claim: a hypothesis, a direct check and a contradiction check, each with the command and its output; reproducers run against the real production files (harness stubs under test/harness/). Write every finding as a register row to ${OUT}/${area.id}/rows.json (a JSON list) and return the same rows; the class statement is the one you would defend, never shortened later.` }
      await preflight(`${area.id}-review`, 'Review', 'audit_reviewer', 'opus', f)
      const rev = await agent(compose('audit_reviewer', 'opus', f), { label: `${area.id} reviewer opus`, phase: 'Review', schema: REVIEWED, model: 'opus' })
      if (!rev) halt('BLOCKED', `review for ${area.id} died`)
      return { cov, rev }
    },
    async (prev, area) => {
      const f = { report: `${OUT}/${area.id}/verify.md`, task: `Audit ${S.audit_id}, area ${area.id}: adversarially verify the reviewer. Inputs: ${prev.cov.coverage_path}, ${prev.rev.rows_path}, ${prev.rev.progress_path}. Write your own coverage list first (${OUT}/${area.id}/verify-coverage.md), including the paths the reviewer omitted. Re-run every reproducer yourself; try to refute every row; give each row id exactly one verdict (confirmed, refuted, narrowed) with the command output that decides it, written to ${OUT}/${area.id}/verdicts.json. New findings on omitted paths go in new_rows.` }
      await preflight(`${area.id}-verify`, 'Verify', 'audit_verifier', 'opus', f)
      const ver = await agent(compose('audit_verifier', 'opus', f), { label: `${area.id} verifier opus`, phase: 'Verify', schema: VERIFIED, model: 'opus' })
      if (!ver) halt('BLOCKED', `verify for ${area.id} died`)
      const judged = new Set(ver.verdicts.map(v => v.id))
      const unjudged = prev.rev.rows.filter(r => !judged.has(r.id)).map(r => r.id)
      if (unjudged.length) halt('BLOCKED', `${area.id}: rows without a verifier verdict: ${unjudged.join(', ')}`)
      return { area: area.id, rev: prev.rev, ver }
    },
  )
  const dead = S.areas.filter((a, i) => !results[i]).map(a => a.id)
  if (dead.length) halt('BLOCKED', `areas that did not finish: ${dead.join(', ')}`)

  const expected = new Map()
  for (const r of results) {
    for (const row of r.rev.rows.concat(r.ver.new_rows)) {
      if (expected.has(row.id)) halt('BLOCKED', `duplicate finding id across areas: ${row.id}`)
      expected.set(row.id, row.class)
    }
  }
  const refuted = results.flatMap(r => r.ver.verdicts.filter(v => v.verdict === 'refuted').map(v => v.id))
  const omitted = results.flatMap(r => r.ver.omitted_paths.map(p => `${r.area}: ${p}`))

  phase('Synthesis')
  const files = results.map(r => `${r.rev.rows_path} ${r.ver.verdicts_path}`).join(' ')
  const f = { report: `${OUT}/synthesis.md`, task: `Audit ${S.audit_id} synthesis. Read every rows and verdicts file: ${files}. Write ${OUT}/rows.yml: one YAML entry per row (${expected.size} rows), keys id, source (audit-${S.audit_id}), severity, class (copied verbatim, never edited), siblings, cites, repro, owner (null; triage assigns it), state (open), pin (null), ruling (null), evidence (the row's evidence plus one line with the verifier verdict and note), history ([{state: open, sha: <git rev-parse --short HEAD>, by: audit-${S.audit_id}}]). Proposals, triage options and cross-references go only in ${OUT}/synthesis.md. Return rows_file and the rows.` }
  await preflight('synthesis', 'Synthesis', 'controller', 'opus', f)
  const syn = await agent(compose('controller', 'opus', f), { label: 'synthesis opus', phase: 'Synthesis', schema: SYNTH, model: 'opus' })
  if (!syn) halt('BLOCKED', 'synthesis died')
  const got = new Map(syn.rows.map(r => [r.id, r.class]))
  const missing = [...expected.keys()].filter(id => !got.has(id))
  const edited = [...expected.entries()].filter(([id, cls]) => got.has(id) && got.get(id) !== cls).map(([id]) => id)
  if (missing.length || edited.length) halt('BLOCKED', `synthesis lost ${missing.join(', ') || 'none'} and edited the class of ${edited.join(', ') || 'none'}`)
  const ids = [...expected.keys()].sort().join(',')
  const chk = await run('check rows file', 'Synthesis', [
    `python3 -c "import sys, yaml; rows = yaml.safe_load(open(sys.argv[1])); sys.exit(0 if sorted(r['id'] for r in rows) == sorted(sys.argv[2].split(',')) else 1)" ${syn.rows_file} ${shq(ids)}`,
  ])
  if (chk.rc !== 0) halt('BLOCKED', `${syn.rows_file} does not hold exactly the ${expected.size} rows: ${chk.output}`)
  return { status: 'DONE', rows_file: syn.rows_file, rows: expected.size, refuted, omitted_paths: omitted }
}

try {
  return await main()
} catch (e) {
  if (e && e.halt) return e.halt
  throw e
}
```

- [ ] **Step 4: Run the lint**

Run: `python3 -m unittest tools.gates.tests.test_workflows.AuditWorkflowTest -v` → all PASS. (`test_parses_with_node` wraps the body in an async function, so the top-level `return`/`await` parse.)

- [ ] **Step 5: Commit, mutation, pin**

```bash
git add .claude/workflows/audit.js tools/gates/tests/test_workflows.py docs/superpowers/state
git commit -m "workflows: audit.js -- coverage, review, adversarial verify, verbatim synthesis (SD-S4-T7)"
```
Mutation: `sed -i.bak 's/got.get(id) !== cls/got.get(id) === got.get(id)/' .claude/workflows/audit.js` (the verbatim-class check made inert); test `test_class_statements_are_checked_verbatim` PASS / FAIL / PASS in `mutations/SD-S4-T7.txt`; `regedit.py set SD-S4-T7 pinned S4-T7 --pin-test tools/gates/tests/test_workflows.py::AuditWorkflowTest::test_class_statements_are_checked_verbatim --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T7.txt`; commit `state: SD-S4-T7 pinned (mutation transcript)`.

### Task 8: `block.js`

**Register:** SD-S4-T8. **Siblings:** PARALLEL tasks + integration; fix round 4 escalation; round limit → decision; denied `gh` writes; a final review that is not clean. **Gates:** gate-test suite.

**Files:**
- Create: `.claude/workflows/block.js`
- Modify: `tools/gates/tests/test_workflows.py` (append `BlockWorkflowTest`), `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: `gate_plan.py --plan`, `gate_dispatch.py --prompt --prompts-dir`, the S2 review-package maker, `gate_review_package.py --dir`, `run_all.py`, `gate_state_docs.py [--write]`, `gate_merge_ready.py --pr N [--allow-draft]`, `postmerge.py --plan --block --pr`, `gate_checkpoint.py --plan <slug> --post`, `registers.grant_for`; prompt files `controller.md`, `implementer.md`, `reviewer.md`, `verifier.md`; the S2 checkpoint checklist `~/.claude/skills/strict-delivery/checklists/checkpoint.md`.
- Produces: workflow `block`, input `{brief}`; block brief YAML: `kind: block`, `plan` (slug), `plan_file`, `block` (int), `pr` (int, the draft PR of `block/<plan>-b<N>`), `tasks: [{id, brief, findings: [ids], claims: [ids], parallel: bool}]` (1–3), `integration: {id, brief, findings, claims, parallel: false}` (required when any task is parallel); review packages `.superpowers/sdd/<plan>/review/<task>/`; final reports `docs/superpowers/state/<plan>/review/final/{reviewer,verifier}.md`; return `{status: 'MERGED', pr, tag, checkpoint}` | `{status: 'HANDOFF', step, next_commands}` | `{status: 'NEEDS_DECISION'|'BLOCKED'|'REFUSED', detail}`.

- [ ] **Step 1: Register**

`regedit.py set SD-S4-T8 in-progress S4-T8`.

- [ ] **Step 2: Append the failing tests**

Append to `tools/gates/tests/test_workflows.py` (before the `if __name__` block):
```python
class BlockWorkflowTest(WorkflowLint, unittest.TestCase):
    NAME = "block"

    def body(self):
        return meta_literal(self.src())[1]

    def test_fable_only_on_the_final_verifier(self):
        fable = [c for c in calls(self.body()) if model_of(c) == "fable"]
        self.assertEqual(len(fable), 1)
        self.assertIn("compose('final_verifier', 'fable'", fable[0])

    def test_sonnet_only_for_implementers(self):
        for c in calls(self.body()):
            if model_of(c) == "sonnet":
                self.assertIn("compose('implementer', 'sonnet'", c)

    def test_fix_rounds_bounded_with_escalation(self):
        b = self.body()
        self.assertIn("const MAX_ROUNDS = 5", b)
        self.assertIn("const ESCALATE_AT = 4", b)
        self.assertIn("compose('implementer', 'opus'", b)
        self.assertIn("halt('NEEDS_DECISION'", b)

    def test_merge_only_after_grant_and_merge_ready(self):
        self.assertRegex(self.body(), r"\[CMD\.grant\(S\.plan\),[^\]]*CMD\.mergeReady\(S\.pr\),[^\]]*gh pr merge \$\{S\.pr\} --squash")

    def test_every_gate_is_called(self):
        b = self.body()
        for name in ("gatePlan", "grant", "dispatch", "reviewPackage", "gateReviewPackage", "runAll",
                     "stateDocsWrite", "stateDocsCheck", "mergeReady", "postmerge", "checkpoint"):
            self.assertIn("  %s:" % name, b, "CMD.%s is not defined" % name)
            self.assertRegex(b, r"CMD\.%s\b" % name, "CMD.%s is defined but never used" % name)

    def test_state_docs_regenerated_before_the_final_review(self):
        b = self.body()
        self.assertLess(b.index("async function regenAndPush"), b.index("async function finalPair"))
        self.assertLess(b.index("await regenAndPush()"), b.index("await finalPair("))
```
Run: `python3 -m unittest tools.gates.tests.test_workflows.BlockWorkflowTest -v` → errors (file missing).

- [ ] **Step 3: Write `.claude/workflows/block.js`**

```js
export const meta = {
  name: 'block',
  description: 'Strict-delivery block loop: per task implementer, review package gate, executed review and fix rounds; state docs; final reviewer and verifier; merge readiness, merge on grant, postmerge, checkpoint',
  whenToUse: 'Only when the strict-delivery skill says to run a block; input {brief: <path to the block brief YAML>}',
  phases: [
    { title: 'Preflight', detail: 'read the brief and prompt files, gate_plan, grant, branch and draft PR' },
    { title: 'Tasks', detail: 'implementer sonnet, review package gate, reviewer opus, fix rounds up to 5 (opus from round 4)' },
    { title: 'Integration', detail: 'PARALLEL task branches merged by a reviewed integration task' },
    { title: 'Final', detail: 'state docs regenerated, final reviewer opus, final verifier fable' },
    { title: 'Merge', detail: 'gate_merge_ready, gh pr merge --squash on grant, postmerge.py, gate_checkpoint' },
  ],
}

const A = args || {}
const MAX_ROUNDS = 5
const ESCALATE_AT = 4
const PROMPTS_DIR = '~/.claude/skills/strict-delivery/prompts'
const PROMPT_FILE = { controller: 'controller', implementer: 'implementer', task_reviewer: 'reviewer', final_reviewer: 'reviewer', final_verifier: 'verifier' }
const BUDGET = { controller: [60000, 30], implementer: [150000, 90], task_reviewer: [120000, 60], final_reviewer: [120000, 90], final_verifier: [120000, 90] }
const DOCS = 'docs/superpowers/plans/2026-09-14-roadmap.md README.md'
const CMD = {
  gatePlan: (planFile) => `python3 tools/gates/gate_plan.py --plan ${planFile}`,
  grant: (plan) => `python3 -c "import sys; sys.path.insert(0, '.'); from tools.gates.lib import registers; g = registers.grant_for(registers.load_decisions('.'), sys.argv[1]); sys.exit(0 if g and str(g.get('answer') or '').strip().lower().startswith('yes') else 1)" ${plan}`,
  dispatch: (file) => `python3 tools/gates/gate_dispatch.py --prompt ${file} --prompts-dir ${PROMPTS_DIR}`,
  reviewPackage: (plan, task, base, head, dir) => `python3 tools/gates/make_review_package.py --plan ${plan} --task ${task} --base ${base} --head ${head} --out ${dir}`,
  gateReviewPackage: (dir) => `python3 tools/gates/gate_review_package.py --dir ${dir}`,
  runAll: 'python3 tools/gates/run_all.py',
  stateDocsWrite: 'python3 tools/gates/gate_state_docs.py --write',
  stateDocsCheck: 'python3 tools/gates/gate_state_docs.py',
  mergeReady: (pr, extra) => `python3 tools/gates/gate_merge_ready.py --pr ${pr}${extra ? ' ' + extra : ''}`,
  postmerge: (plan, block, pr) => `python3 tools/gates/postmerge.py --plan ${plan} --block ${block} --pr ${pr}`,
  checkpoint: (plan) => `python3 tools/gates/gate_checkpoint.py --plan ${plan} --post`,
}

function halt(status, detail) { const e = new Error(detail); e.halt = { status, detail }; throw e }
function shq(s) { return "'" + String(s).replace(/'/g, "'\\''") + "'" }
function utf8len(s) { return unescape(encodeURIComponent(s)).length }
function sha40(text) {
  const m = String(text).match(/\b[0-9a-f]{40}\b/g)
  if (!m) halt('BLOCKED', 'no commit sha in runner output: ' + String(text).slice(-200))
  return m[m.length - 1]
}

const RUN = { type: 'object', properties: { rc: { type: 'integer' }, output: { type: 'string' } }, required: ['rc', 'output'] }
const IMPL = { type: 'object', properties: { status: { type: 'string', enum: ['DONE', 'DONE_WITH_FINDINGS', 'NEEDS_DECISION', 'BLOCKED', 'DENIED'] }, commits: { type: 'array', items: { type: 'string' } }, report: { type: 'string' }, decision: { type: 'string' } }, required: ['status', 'commits', 'report'] }
const REVIEW = { type: 'object', properties: { verdict: { type: 'string', enum: ['clean', 'changes'] }, findings: { type: 'array', items: { type: 'string' } }, report: { type: 'string' } }, required: ['verdict', 'findings', 'report'] }
const TASK = { type: 'object', properties: { id: { type: 'string' }, brief: { type: 'string' }, findings: { type: 'array', items: { type: 'string' } }, claims: { type: 'array', items: { type: 'string' } }, parallel: { type: 'boolean' } }, required: ['id', 'brief', 'findings', 'claims', 'parallel'] }
const PROMPTS = { type: 'object', properties: { controller: { type: 'string' }, implementer: { type: 'string' }, reviewer: { type: 'string' }, verifier: { type: 'string' } }, required: ['controller', 'implementer', 'reviewer', 'verifier'] }
const BYTES = { type: 'object', properties: { controller: { type: 'integer' }, implementer: { type: 'integer' }, reviewer: { type: 'integer' }, verifier: { type: 'integer' } }, required: ['controller', 'implementer', 'reviewer', 'verifier'] }
const BRIEF = {
  type: 'object',
  properties: {
    kind: { type: 'string' }, plan: { type: 'string' }, plan_file: { type: 'string' }, block: { type: 'integer' }, pr: { type: 'integer' },
    tasks: { type: 'array', items: TASK }, integration: TASK,
    gate_plan_rc: { type: 'integer' }, grant_rc: { type: 'integer' }, output: { type: 'string' }, prompts: PROMPTS, prompt_bytes: BYTES,
  },
  required: ['kind', 'plan', 'plan_file', 'block', 'pr', 'tasks', 'gate_plan_rc', 'grant_rc', 'output', 'prompts', 'prompt_bytes'],
}

let S = null
let BRANCH = ''
let SCRATCH = ''
let STATE = ''

async function run(label, phaseName, commands) {
  const p = 'You are a command runner for the strict-delivery block workflow (a controller-role mechanical step). From the repository root, run exactly these shell commands in order with Bash, stopping at the first one that exits non-zero. Do not edit any file, do not retry, do not interpret, do not run anything else. Return rc = the exit code of the last command you ran (0 if all succeeded) and output = the last 80 lines of its combined stdout and stderr, verbatim. If the permission system refuses a command, return rc = 126 and output starting with DENIED and the refusal text.\n\n' + commands.map((c, i) => `${i + 1}. ${c}`).join('\n')
  const r = await agent(p, { label: `run ${label}`, phase: phaseName, schema: RUN, model: 'opus', effort: 'low' })
  if (!r) halt('BLOCKED', `runner "${label}" died`)
  return r
}

function fields(role, t, report, task) {
  return { role, brief: t.brief, findings: t.findings, claims: t.claims, report, task }
}
function header(role, model, f) {
  return [`## Role: ${role}`, `## Brief: ${f.brief}`,
    `## Findings: ${f.findings.length ? f.findings.join(', ') : 'none'}`,
    `## Claims: ${f.claims.length ? f.claims.join(', ') : 'none'}`,
    `## Budget: tokens=${BUDGET[role][0]} minutes=${BUDGET[role][1]}`, `## Model: ${model}`, `## Report: ${f.report}`].join('\n')
}
function compose(role, model, f) {
  return [header(role, model, f), '## Strict block', S.prompts[PROMPT_FILE[role]], '## Task', f.task].join('\n')
}
async function preflight(label, phaseName, role, model, f) {
  const file = `${SCRATCH}/dispatch/${label}.prompt.md`
  const r = await run(`preflight ${label}`, phaseName, [
    `mkdir -p ${SCRATCH}/dispatch`,
    `{ printf '%s\\n' ${shq(header(role, model, f))}; printf '## Strict block\\n'; cat ${PROMPTS_DIR}/${PROMPT_FILE[role]}.md; printf '\\n## Task\\n'; printf '%s' ${shq(f.task)}; } > ${file}`,
    `test "$(wc -c < ${file} | tr -d ' ')" = "${utf8len(compose(role, model, f))}"`,
    CMD.dispatch(file),
  ])
  if (r.rc !== 0) halt('BLOCKED', `gate_dispatch refused ${label}: ${r.output}`)
}
function expectDone(res, id, what) {
  if (!res) halt('BLOCKED', `task ${id} ${what}: agent died`)
  if (res.status === 'NEEDS_DECISION') halt('NEEDS_DECISION', `task ${id} ${what}: ${res.decision || res.report}`)
  if (res.status === 'BLOCKED' || res.status === 'DENIED') halt('BLOCKED', `task ${id} ${what}: ${res.status} (${res.report})`)
}

async function packageAndReview(t, phaseName, base, head) {
  const dir = `.superpowers/sdd/${S.plan}/review/${t.id}`
  const pk = await run(`package ${t.id}`, phaseName, [CMD.reviewPackage(S.plan, t.id, base, head, dir), CMD.gateReviewPackage(dir)])
  if (pk.rc !== 0) halt('BLOCKED', `task ${t.id}: review package refused: ${pk.output}`)
  const f = fields('task_reviewer', t, `${dir}/review.md`, `Review task ${t.id} of ${S.plan_file}. Package: ${dir}/. Re-run ${CMD.runAll} yourself on a clean tree and every mutation transcript; treat every claim in the implementer report, the commit messages and the diff comments as a hypothesis and execute a direct and a contradiction check for each; check every sibling against the class; run a contradiction pass on the new surface. verdict is clean only when spec, quality and claims-executed are all clean.`)
  await preflight(`t${t.id}-review`, phaseName, 'task_reviewer', 'opus', f)
  const rv = await agent(compose('task_reviewer', 'opus', f), { label: `task ${t.id} reviewer opus`, phase: phaseName, schema: REVIEW, model: 'opus' })
  if (!rv) halt('BLOCKED', `task ${t.id}: reviewer died`)
  return rv
}

async function fixLoop(t, phaseName, iso, first, again, where) {
  let review = first
  for (let round = 1; review.verdict !== 'clean'; round++) {
    if (round > MAX_ROUNDS) halt('NEEDS_DECISION', `task ${t.id}: ${MAX_ROUNDS} fix rounds without a clean review; last report ${review.report}`)
    const f = fields('implementer', t, `${SCRATCH}/task-${t.id}/fix-${round}.md`, `Fix round ${round} of task ${t.id} of ${S.plan_file}. ${where} Read ${review.report} and fix every finding in it (no minor carve-out); keep TDD; update the mutation transcripts; run ${CMD.runAll}; commit.`)
    let res = null
    if (round < ESCALATE_AT) {
      await preflight(`t${t.id}-fix${round}`, phaseName, 'implementer', 'sonnet', f)
      res = await agent(compose('implementer', 'sonnet', f), { label: `task ${t.id} fix ${round} sonnet`, phase: phaseName, schema: IMPL, model: 'sonnet', ...(iso ? { isolation: 'worktree' } : {}) })
    } else {
      if (round === ESCALATE_AT) log(`task ${t.id}: escalating fix round ${round} to opus`)
      await preflight(`t${t.id}-fix${round}`, phaseName, 'implementer', 'opus', f)
      res = await agent(compose('implementer', 'opus', f), { label: `task ${t.id} fix ${round} opus`, phase: phaseName, schema: IMPL, model: 'opus', ...(iso ? { isolation: 'worktree' } : {}) })
    }
    expectDone(res, t.id, `fix ${round}`)
    review = await again()
  }
  return review
}

async function runTask(t, phaseName, iso) {
  const head = iso ? `${BRANCH}-t${t.id}` : BRANCH
  const b = await run(`base ${t.id}`, phaseName, [iso ? `git branch ${BRANCH}-t${t.id} ${BRANCH}` : `git switch ${BRANCH}`, `git rev-parse ${BRANCH}`])
  if (b.rc !== 0) halt('BLOCKED', `task ${t.id}: ${b.output}`)
  const base = sha40(b.output)
  const where = iso ? `You run in a fresh git worktree: run git switch ${BRANCH}-t${t.id} first, commit only there, never merge.` : `Work in the main checkout on branch ${BRANCH}.`
  const f = fields('implementer', t, `${SCRATCH}/task-${t.id}/implementer.md`, `Implement task ${t.id} of ${S.plan_file}. ${where} Follow the brief; failing test first through the production call path; record each mutation transcript under docs/superpowers/state/${S.plan}/mutations/; test every sibling or file it as its own row; run ${CMD.runAll} before reporting; commit per the plan.`)
  await preflight(`t${t.id}-impl`, phaseName, 'implementer', 'sonnet', f)
  const res = await agent(compose('implementer', 'sonnet', f), { label: `task ${t.id} implementer sonnet`, phase: phaseName, schema: IMPL, model: 'sonnet', ...(iso ? { isolation: 'worktree' } : {}) })
  expectDone(res, t.id, 'implementer')
  const first = await packageAndReview(t, phaseName, base, head)
  return fixLoop(t, phaseName, iso, first, () => packageAndReview(t, phaseName, base, head), where)
}

async function regenAndPush() {
  const r = await run('state docs and push', 'Final', [
    `git switch ${BRANCH}`, CMD.stateDocsWrite, `git add ${DOCS}`,
    `git diff --cached --quiet || git commit -m "state: regenerate state docs (${S.plan}-b${S.block})"`,
    CMD.stateDocsCheck, CMD.runAll, `git push origin ${BRANCH}`, 'git rev-parse HEAD',
  ])
  if (r.rc !== 0) halt('BLOCKED', `state docs / gates / push failed: ${r.output}`)
  return sha40(r.output)
}

async function finalPair(head) {
  const all = S.tasks.concat(S.integration ? [S.integration] : [])
  const t = { id: 'final', brief: S.plan_file, findings: all.flatMap(x => x.findings), claims: all.flatMap(x => x.claims) }
  const rf = fields('final_reviewer', t, `${STATE}/review/final/reviewer.md`, `Final whole-branch review of ${BRANCH} at ${head} against ${S.plan_file} and its spec. Re-run every gate (${CMD.runAll}) and every mutation transcript. Write the report: first line "verdict: clean" or "verdict: changes", second line "head: ${head}".`)
  const vf = fields('final_verifier', t, `${STATE}/review/final/verifier.md`, `Adversarial verification of ${BRANCH} at ${head}: try to refute every claim of the block (report, commits, PR body, review reports); re-run every gate and every pin yourself. Write the report: first line "verdict: clean" or "verdict: changes", second line "head: ${head}".`)
  await preflight('final-reviewer', 'Final', 'final_reviewer', 'opus', rf)
  await preflight('final-verifier', 'Final', 'final_verifier', 'fable', vf)
  const pair = await parallel([
    () => agent(compose('final_reviewer', 'opus', rf), { label: 'final reviewer opus', phase: 'Final', schema: REVIEW, model: 'opus' }),
    () => agent(compose('final_verifier', 'fable', vf), { label: 'final verifier fable', phase: 'Final', schema: REVIEW, model: 'fable' }),
  ])
  if (!pair[0] || !pair[1]) halt('BLOCKED', 'a final agent died')
  const clean = pair[0].verdict === 'clean' && pair[1].verdict === 'clean'
  return { verdict: clean ? 'clean' : 'changes', findings: pair[0].findings.concat(pair[1].findings), report: `${pair[0].report} and ${pair[1].report}` }
}

function handoff(step, r, rest) {
  if (r.rc === 126 || /DENIED/.test(r.output)) return { status: 'HANDOFF', step, detail: r.output, next_commands: rest }
  halt('BLOCKED', `${step}: ${r.output}`)
}

async function main() {
  phase('Preflight')
  if (typeof A.brief !== 'string' || !A.brief) halt('REFUSED', 'input must be {brief: <path>}')
  S = await agent(`You are a command runner for the strict-delivery block workflow. Read the YAML block brief ${A.brief} and return kind, plan, plan_file, block, pr, tasks and integration (when present) exactly as written. Then run, from the repository root: (1) ${CMD.gatePlan('<plan_file>')} with <plan_file> replaced by the brief's plan_file and put its exit code in gate_plan_rc; (2) ${CMD.grant('<plan>')} with <plan> replaced by the brief's plan, exit code in grant_rc; (3) for each name in controller, implementer, reviewer, verifier: cat ${PROMPTS_DIR}/<name>.md into prompts.<name> byte for byte, and wc -c < ${PROMPTS_DIR}/<name>.md into prompt_bytes.<name>. output = the last 40 lines of the gate_plan and grant outputs. Edit nothing.`,
    { label: 'run read brief', phase: 'Preflight', schema: BRIEF, model: 'opus', effort: 'low' })
  if (!S) halt('BLOCKED', 'the brief reader died')
  if (S.kind !== 'block' || !/^[a-z0-9-]+$/.test(S.plan) || S.block < 1) halt('REFUSED', `not a block brief: kind=${S.kind} plan=${S.plan} block=${S.block}`)
  if (S.tasks.length < 1 || S.tasks.length > 3) halt('REFUSED', `a block has 1..3 tasks, the brief has ${S.tasks.length}`)
  const par = S.tasks.filter(t => t.parallel)
  if (par.length && !S.integration) halt('REFUSED', 'PARALLEL tasks need an integration task in the brief')
  if (S.gate_plan_rc !== 0) halt('REFUSED', `gate_plan is not clean on ${S.plan_file}: ${S.output}`)
  if (S.grant_rc !== 0) halt('REFUSED', `no answered grant:${S.plan} in decisions.yml`)
  for (const k of Object.keys(S.prompt_bytes)) {
    if (utf8len(S.prompts[k]) !== S.prompt_bytes[k]) halt('REFUSED', `prompt ${k}.md came back garbled (${utf8len(S.prompts[k])} != ${S.prompt_bytes[k]} bytes)`)
  }
  BRANCH = `block/${S.plan}-b${S.block}`
  SCRATCH = `.superpowers/sdd/${S.plan}/b${S.block}`
  STATE = `docs/superpowers/state/${S.plan}`
  const pre = await run('branch and PR', 'Preflight', [
    `git switch ${BRANCH}`, 'test -z "$(git status --porcelain)"',
    `test "$(gh pr view ${S.pr} --json headRefName,isDraft --jq '.headRefName + " " + (.isDraft | tostring)')" = "${BRANCH} true"`,
    CMD.runAll,
  ])
  if (pre.rc !== 0) halt('REFUSED', `branch ${BRANCH} clean, draft PR #${S.pr} and green gates are required: ${pre.output}`)

  phase('Tasks')
  for (const t of S.tasks.filter(x => !x.parallel)) await runTask(t, 'Tasks', false)
  if (par.length) {
    const done = await parallel(par.map(t => () => runTask(t, 'Tasks', true)))
    const lost = par.filter((t, i) => !done[i]).map(t => t.id)
    if (lost.length) halt('BLOCKED', `parallel tasks that did not finish: ${lost.join(', ')}`)
    phase('Integration')
    const it = S.integration
    const branches = par.map(t => `${BRANCH}-t${t.id}`).join(' ')
    const b = await run('integration base', 'Integration', [`git switch ${BRANCH}`, `git rev-parse ${BRANCH}`])
    const base = sha40(b.output)
    const where = `Work in the main checkout on ${BRANCH}.`
    const f = fields('implementer', it, `${SCRATCH}/task-${it.id}/implementer.md`, `Integration task ${it.id}: merge ${branches} into ${BRANCH} with git merge --no-ff, in that order; resolve conflicts only as the brief says; run ${CMD.runAll}; commit; report every conflict and how it was resolved.`)
    await preflight(`t${it.id}-impl`, 'Integration', 'implementer', 'sonnet', f)
    const res = await agent(compose('implementer', 'sonnet', f), { label: `integration ${it.id} implementer sonnet`, phase: 'Integration', schema: IMPL, model: 'sonnet' })
    expectDone(res, it.id, 'integration')
    const first = await packageAndReview(it, 'Integration', base, BRANCH)
    await fixLoop(it, 'Integration', false, first, () => packageAndReview(it, 'Integration', base, BRANCH), where)
  }

  phase('Final')
  const all = S.tasks.concat(S.integration ? [S.integration] : [])
  const finalTask = { id: 'final', brief: S.plan_file, findings: all.flatMap(x => x.findings), claims: all.flatMap(x => x.claims) }
  let head = await regenAndPush()
  const first = await finalPair(head)
  await fixLoop(finalTask, 'Final', false, first, async () => { head = await regenAndPush(); return finalPair(head) }, `Work in the main checkout on ${BRANCH}.`)
  const rep = await run('commit final reports', 'Final', [
    `git switch ${BRANCH}`, `git add ${STATE}/review/final`, `git commit -m "state: final review and verification of ${S.plan}-b${S.block} (clean at ${head.slice(0, 7)})"`, `git push origin ${BRANCH}`,
  ])
  if (rep.rc !== 0) halt('BLOCKED', `committing the final reports failed: ${rep.output}`)

  phase('Merge')
  const readyCmds = [`gh pr checks ${S.pr} --watch --interval 30`, CMD.mergeReady(S.pr, '--allow-draft'), `gh pr ready ${S.pr}`]
  const mergeCmds = [CMD.grant(S.plan), `gh pr checks ${S.pr} --watch --interval 30`, CMD.mergeReady(S.pr), `gh pr merge ${S.pr} --squash`]
  const postCmds = [CMD.postmerge(S.plan, S.block, S.pr)]
  const ready = await run('ready', 'Merge', readyCmds)
  if (ready.rc !== 0) return handoff('ready', ready, readyCmds.concat(mergeCmds, postCmds))
  const merged = await run('merge on grant', 'Merge', mergeCmds)
  if (merged.rc !== 0) return handoff('merge', merged, mergeCmds.concat(postCmds))
  const post = await run('postmerge', 'Merge', postCmds)
  if (post.rc !== 0) return handoff('postmerge', post, postCmds)
  const cw = await agent(`You are the controller's checkpoint writer for ${S.plan} block ${S.block} (PR #${S.pr} merged, tag ${S.plan}-b${S.block}). Follow ~/.claude/skills/strict-delivery/checklists/checkpoint.md exactly: write ${STATE}/handoff.md, append "CHECKPOINT b${S.block}" to ${STATE}/ledger.md, and run ${CMD.checkpoint(S.plan)}. Return rc = its exit code and output = its last 40 lines.`,
    { label: 'checkpoint writer opus', phase: 'Merge', schema: RUN, model: 'opus' })
  if (!cw || cw.rc !== 0) halt('BLOCKED', `gate_checkpoint failed: ${cw ? cw.output : 'writer died'}`)
  const cp = await run('gate_checkpoint', 'Merge', [CMD.checkpoint(S.plan)])
  if (cp.rc !== 0) halt('BLOCKED', `gate_checkpoint failed on re-run: ${cp.output}`)
  return { status: 'MERGED', pr: S.pr, tag: `${S.plan}-b${S.block}`, checkpoint: cp.output }
}

try {
  return await main()
} catch (e) {
  if (e && e.halt) return e.halt
  throw e
}
```
`test_merge_only_after_grant_and_merge_ready` reads the literal `mergeCmds` array to prove the grant check and `gate_merge_ready` come before `gh pr merge`; the merge runner executes exactly that array.

- [ ] **Step 4: Run the lint**

Run: `python3 -m unittest tools/gates/tests/test_workflows.py -v` → all PASS.

- [ ] **Step 5: Commit, mutation, pin**

```bash
git add .claude/workflows/block.js tools/gates/tests/test_workflows.py docs/superpowers/state
git commit -m "workflows: block.js -- tasks, review package, executed review, fix rounds, final pair, merge on grant, postmerge, checkpoint (SD-S4-T8)"
```
Mutation: `sed -i.bak 's/const mergeCmds = \[CMD.grant(S.plan), /const mergeCmds = [/' .claude/workflows/block.js` (the grant check dropped from the merge step); test `test_merge_only_after_grant_and_merge_ready` PASS / FAIL / PASS in `mutations/SD-S4-T8.txt`; `regedit.py set SD-S4-T8 pinned S4-T8 --pin-test tools/gates/tests/test_workflows.py::BlockWorkflowTest::test_merge_only_after_grant_and_merge_ready --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T8.txt`; commit `state: SD-S4-T8 pinned (mutation transcript)`.

### Task 9: verify the scripts against the live CLIs and hooks; README section

**Register:** SD-S4-T9. **Siblings:** audit.js, block.js, `tools/gates/README.md`. **Gates:** gate-test suite, `run_all.py`.

**Files:**
- Modify: `.claude/workflows/block.js` / `audit.js` (only the `CMD` strings, if Step 1 finds a mismatch), `tools/gates/README.md`, `tools/gates/tests/test_workflows.py`, `docs/superpowers/state/findings.yml`

**Interfaces:**
- Consumes: Tasks 7–8 scripts; the S1–S3 CLIs.
- Produces: README section `## Workflows`; test `test_readme_documents_workflows`; test `test_cmd_scripts_exist`; a ledger line recording whether the PreToolUse Agent hook fires for Workflow agents.

- [ ] **Step 1: Check every command string against the real CLIs**

```bash
python3 .superpowers/sdd/strict-delivery/regedit.py set SD-S4-T9 in-progress S4-T9
for s in gate_plan gate_dispatch gate_review_package gate_state_docs gate_merge_ready gate_checkpoint postmerge run_all; do
  echo "== $s"; python3 tools/gates/$s.py --help 2>&1 | head -15
done
ls tools/gates/ | grep -i package
```
Expected: `gate_plan --plan`, `gate_dispatch --prompt --prompts-dir`, `gate_review_package --dir`, `gate_checkpoint --plan` (and `--post`), `gate_merge_ready --pr --allow-draft`, `postmerge --plan --block --pr`. The review-package maker's real name and flags are whatever S2 shipped: put them in `CMD.reviewPackage` (its output dir must be `.superpowers/sdd/<plan>/review/<task>/`). Fix any other mismatch in the `CMD` object only and ledger it. Re-run `python3 -m unittest tools/gates/tests/test_workflows.py`.

Append to `test_workflows.py`:
```python
class CommandsExistTest(unittest.TestCase):
    def test_cmd_scripts_exist(self):
        for name in ("audit", "block"):
            for path in set(re.findall(r"python3 (tools/gates/[\w/]+\.py)", read(SCRIPTS[name]))):
                self.assertTrue(os.path.isfile(os.path.join(s4kit.REPO, path)), "%s: %s missing" % (name, path))

    def test_readme_documents_workflows(self):
        text = read("tools/gates/README.md")
        self.assertIn("## Workflows", text)
        for needle in (".claude/workflows/audit.js", ".claude/workflows/block.js", "{brief:", "HANDOFF"):
            self.assertIn(needle, text)
```

- [ ] **Step 2: Probe whether the Agent hook sees Workflow spawns**

The session (not a subagent) runs a one-agent probe workflow whose prompt has no `## Role:` line:
```
Workflow({script: "export const meta = {name: 'hook-probe', description: 'probe', phases: [{title: 'Probe'}]}\nphase('Probe')\nreturn await agent('Reply with the word ok.', {label: 'probe', model: 'opus', effort: 'low'})"})
```
If the result is `ok`, the PreToolUse Agent hook does **not** gate Workflow spawns: the scripts' `preflight()` is the only `gate_dispatch` on that path. If the spawn is refused with `gate_dispatch` FAIL lines, the hook gates them too: runner prompts (plain text, no role grammar) will then be refused as well, so record that as a finding row (owner: this plan, Task 9) and stop the block for a decision (the fix is either a runner role in the grammar, which is spec-direction, or a hook exemption, which is scope). Ledger the observed behaviour verbatim in both cases.

- [ ] **Step 3: README section**

Append to `tools/gates/README.md`:
```markdown
## Workflows

Two Workflow scripts sequence the pipeline. The session runs them only when the strict-delivery skill says
to; without them, the skill runs the same sequence with the Agent tool and the hooks still gate every step.

- `Workflow({scriptPath: ".claude/workflows/audit.js", args: {brief: "<audit brief YAML>"}})`. Brief keys:
  `kind: audit`, `audit_id`, `areas: [{id, scope}]`. Needs `gate_plan.py --plan <brief>` clean and an
  answered `grant:<audit_id>`. Per area: coverage list, review (resumes from its progress file), adversarial
  verifier; then one synthesis that writes `docs/superpowers/state/audits/<audit_id>/rows.yml` with every class
  statement verbatim. Rows reach `findings.yml` only through the owner's triage decisions.
- `Workflow({scriptPath: ".claude/workflows/block.js", args: {brief: "<block brief YAML>"}})`. Brief keys:
  `kind: block`, `plan`, `plan_file`, `block`, `pr` (the draft PR of `block/<plan>-b<N>`), `tasks: [{id, brief,
  findings, claims, parallel}]`, `integration` (needed when any task is parallel). Needs a clean branch, green
  `run_all.py`, `gate_plan` clean, an answered `grant:<plan>`. Per task: implementer (sonnet; fix rounds 4-5
  opus) → review package → `gate_review_package` → reviewer (opus) → at most 5 fix rounds; then integration;
  then `gate_state_docs --write` + push, final reviewer (opus) + final verifier (Fable); then `gate_merge_ready`,
  `gh pr ready`, merge on grant, `postmerge.py`, checkpoint.
- Every role dispatch is preceded by a runner that rebuilds the prompt on disk and runs `gate_dispatch.py`.
- Return values: `MERGED` / `DONE`; `REFUSED` (preconditions); `BLOCKED` (a gate or agent failed);
  `NEEDS_DECISION` (round limit or an owner-only question: the session writes the decision entry and stops);
  `HANDOFF` (a `gh` write was denied to a subagent: the session runs `next_commands` itself, in order).
- Resume a killed run with `Workflow({scriptPath, resumeFromRunId})`; audit reviewers also resume from their
  progress files.
```

- [ ] **Step 4: Run, commit, mutation, pin, CHECKPOINT b12**

```bash
python3 -m unittest tools/gates/tests/test_workflows.py -v && python3 tools/gates/run_all.py && \
git add tools/gates .claude/workflows docs/superpowers/state && \
git commit -m "workflows: CLI strings verified against S1-S3, hook probe ledgered, README section (SD-S4-T9)"
```
Mutation: `sed -i.bak 's/^## Workflows$/## Workflow scripts/' tools/gates/README.md`; test `test_readme_documents_workflows` PASS / FAIL / PASS in `mutations/SD-S4-T9.txt`; `regedit.py set SD-S4-T9 pinned S4-T9 --pin-test tools/gates/tests/test_workflows.py::CommandsExistTest::test_readme_documents_workflows --pin-transcript docs/superpowers/state/strict-delivery/mutations/SD-S4-T9.txt`; commit `state: SD-S4-T9 pinned (mutation transcript)`.

CHECKPOINT b12: same sequence as CHECKPOINT b11 (`gate_state_docs --write` commit → `run_all.py` → final reviewer opus + final verifier Fable → `gh pr ready` → green → `gh pr merge --squash` → `postmerge.py --plan strict-delivery --block 12 --pr <n>` → handoff `block: 12`, `next_task: S5 Task 1` → ledger `CHECKPOINT b12` → `gate_checkpoint.py --plan strict-delivery --post` → status update + resume prompt → stop). S4 is complete; the next block is S5 b13.

Resume prompt:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 13 (sub-plan S5, docs/superpowers/plans/2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md, Task 1); first commit the state postmerge left pending on the new block branch. Use superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

---

## Self-review (writing-plans checklist, done 2026-10-17)

- **Spec coverage.** §7 rows: `gate_comments` (T1), `gate_tools_py` (T2; the "type check" item is not covered, see facts list), `gate_bench` (T3), `gate_state_docs` (T4), `gate_merge_ready` incl. tag rule §11.2 (T5, `--post`). §8: PreToolUse PR verbs + `git merge` (T5), git pre-push (T5), CI wiring (T1–T4), workflows (T7–T9). §3.4: post-merge sequence (T6, as amended by the umbrella). §10: state docs, bench rows, comments (T1, T3, T4). §13 must-fail fixtures: comment "(plan 06)" (T1), PR body "fix before merge" (T5).
- **Placeholder scan.** Runtime values (history shas, PR numbers) come from commands given in full (`regedit.py`, `gh`). The S2 review-package maker's name is the one unverifiable interface; Task 9 Step 1 resolves it against the tree.
- **Type consistency.** `gitinfo` helpers are defined in T1/T4 and used with those signatures in T5/T6. `gate_merge_ready.find_block_plan/tasks_in_block/block_rows/checks_of/OK_CONCLUSIONS` are used by `postmerge.py` with the same names. `postmerge` argv matches the umbrella CLI table everywhere (README, block.js, CHECKPOINTs). `gate_merge_ready` keeps the umbrella's `--pr`/`--post` and adds `--allow-draft`/`--pre-push`.
- **Review Focus.** All five lines have tests in their owning tasks: T1, T4 (gh failure), T5 (advisory red, body marker), T3 (hardware-only pin), T6 (stop at first fail, tag already present), T2 (expected-fail flips, uncovered script).
