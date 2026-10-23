# Strict Delivery S2 — Pipeline Gates and Checkpoints Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This is sub-plan S2 of the umbrella `docs/superpowers/plans/2026-10-17-strict-delivery.md`; the umbrella's "Shared interfaces" section is binding and is consumed here verbatim.

**Goal:** Install the four pipeline gates (`gate_dispatch`, `gate_review_package`, `gate_plan`, `gate_checkpoint`), the Agent branch of the PreToolUse hook, the Stop and SubagentStop hooks, the per-plan state layout and the checkpoint/resume/block-end checklists, so that from block b4 every dispatch, from b5 every review package and plan, and from b6 every block end is machine-checked.

**Architecture:** Each gate is one module under `tools/gates/` exposing `check(args) -> Result` and `main(argv)` over the S1 library (`tools/gates/lib/`), registered in `run_all.GATES` with a default argv that is meaningful in CI (`--config-check`, `--all`, `--in-flight`, `--structure`); the moment-of-truth modes (`--prompt`, `--dir`, `--plan`) are what hooks and the controller call. Hooks are thin: parse the event, call `run_all.run` (the Agent branch runs `dispatch`, `claims` and `decisions` through it; Task 2 ships it calling `gate_dispatch.check` through `lib.run_check` because `run_all.GATES` lacks `dispatch`, and Task 3 moves it onto `run_all.run`), exit 2 with the rendered lines. Tests drive `main()` (the production path) against throwaway git repositories built by one helper.

**Tech Stack:** Python 3.9+ (local `python3` is 3.9.6; every module starts with `from __future__ import annotations`), PyYAML 6 (6.0.3 installed locally; pinned in `tools/requirements.txt` by S1), `python3 -m unittest`, git ≥ 2.28 (`git init -b`; local 2.50.1), `gh` (faked in tests), Claude Code hooks.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` — §3.3–§3.5 (checkpoints), §5 (dispatch contract), §6 (pipeline), §7 (gates). Umbrella: `docs/superpowers/plans/2026-10-17-strict-delivery.md`.

## Global Constraints

- Every change, any size, goes through the pipeline (spec D20); this plan's own blocks are executed with the gates that exist at that point (spec §13 rollout).
- Block = PR, at most 3 tasks per block; a block ends in a clean tree (spec §3.4); one block per context; the session stops after each block for the owner's `/clear` (spec §3.5).
- Merge only on all-green (every CI job incl. advisory) under a per-plan grant entry `grant:<plan>` in `decisions.yml` (spec D22). The grant for this plan is decision `DEC-0001`.
- Tags: `strict-delivery-b4`, `strict-delivery-b5`, `strict-delivery-b6` on the squash commits.
- Model map (spec §5): controllers opus; implementers sonnet (haiku only for transcription of complete code); task reviewers opus; final branch reviewer opus; final adversarial verifier Fable; exception reviewer Fable (both opus while the DEC-0018 fallback is on). Never any other Fable use.
- Triggers (spec §3.1): controller 3 tasks / 250k tokens / 90 min; implementer 1 task / 150k; reviewer 1 review / 120k; any 429 → checkpoint at the task boundary.
- Zero warnings; zero gate failures at every commit once the gate exists; every Python script exits non-zero on any failure path; a missing input is a FAIL never "nothing to do" (spec §7).
- Pinned = production-path test + mutation transcript (spec D7). For gate scripts "production path" = the script's `main()` invoked on a fixture, never a helper called in isolation.
- No claim trusted (spec D3): every plan-text claim about the repo is verified by the implementer before use; a wrong claim is corrected in the plan and ledgered.
- Owner-only decisions (spec §5) stop the block with a decision entry; controller rulings max 5 per block, ledgered with cost-if-wrong.
- Hardware: nothing in this plan flashes or opens a serial port.
- Commit trailers: subagents use their own model's trailer; the squash commit carries the session's.
- Repo fact (verified 2026-10-17 with `gh api repos/superbrobenji/lap-timer/branches/main/protection`): `main` requires status checks (strict), linear history, and `enforce_admins: true` — no commit can be pushed to `main` directly. Every state commit (handoff, ledger, register transitions) therefore rides inside the block PR: the handoff is committed on the block branch before the merge, `gate_checkpoint --plan <slug>` checks it there (pre-merge) and `--post` checks the squash on `main` afterwards (Task 7). `merged` register states are written only by S4's `postmerge.py` (`by: postmerge`, S1 rule); until S4 lands, S2 rows stay `pinned` after their block merges.
- Review packages are git-ignored at `.superpowers/sdd/<plan>/review/<task>/` (umbrella; `.superpowers/` is in `.gitignore`, verified); mutation transcripts are tracked at `docs/superpowers/state/<plan>/mutations/<finding-id>.txt`, copied there by the controller after the task review's clean verdict (b4 ruling 3), together with the reviewer's report at `docs/superpowers/state/<plan>/reviews/S2-T<n>.md`.
- Plan grammar (DEC-0016, DEC-0017; `gate_plan` checks it): a Files item is `- Create|Modify|Test|Delete: ` then exactly one backticked path, then prose with no backtick (an item naming two files is two items); every task carries `**Siblings:**` (`none -- <reason>` with lower-case none and a non-empty reason, or every `siblings` entry of the register rows its Findings name, comma-separated) and `**Gates:**` (exactly `run_all` or names X whose `tools/gates/gate_X.py` exists or is created by this or an earlier task); a task whose Findings names ids has a step whose title holds the word `pin` (case-insensitive, `\bpin\b`: "Mutation run and pin", "Pin step" and "pin:" count; "Mapping", "Spinner" and "pinned" do not; B5-FIX-3) and whose body names a test named `test_<module>::test_<name>`.
- Block branches are named `<plan>/b<N>` (here `block/strict-delivery-b4`…`b6`), never `<plan>-b<N>`, so a branch never shares a name with its tag.

## Review Focus

1. **A dispatch with no `model` in `tool_input`.** The subagent would silently inherit the session model (possibly Fable); the Agent hook must block it even when the `## Model:` line is right. Pinned by `test_hooks.py::AgentDispatchHook::test_missing_tool_model_blocks` (Task 2).
2. **A strict block that itself contains `## ` headings.** Parsing by heading would cut it short and report a false mismatch; the gate compares by byte offset. Pinned by `test_gate_dispatch.py::GateDispatch::test_good_reviewer_with_inner_heading_passes` (Task 1).
3. **A handoff committed on the block branch, then squash-merged.** Its `head` is no longer an ancestor of `main`; the post-merge check must accept `main` = one commit on `base` whose tree differs from `head` only under `docs/superpowers/state/`, and reject a code change pushed after the handoff. Pinned by `test_gate_checkpoint.py::PostMerge::test_squash_merged_checkpoint_passes` and `::test_post_tree_mismatch_fails` (Task 7).
4. **A session waiting on a background subagent or an owner decision.** The Stop hook must bounce at most once (loop guard) and say how to stop. Pinned by `test_hook_stop.py::StopHook::test_stop_hook_active_passes_through` (Task 8).
5. **A `CHECKPOINT` line appended to the ledger but never committed.** The plan is still in flight and the Stop hook must still run the checkpoint gate. Pinned by `test_gate_plan.py::InFlight::test_uncommitted_checkpoint_line_is_in_flight` (Task 6).

---

## Consumed from S1 (verify before Task 1; do not redefine)

Run and read the output; every name below must exist. A mismatch is a plan-vs-code claim to correct and ledger (D3), not a reason to redefine the S1 interface.

```bash
grep -n "^def \|^class " tools/gates/lib/__init__.py tools/gates/lib/registers.py tools/gates/lib/gitinfo.py tools/gates/lib/config.py
grep -n "^GATES\|DEFAULT_ARGV\|^def gate_argv\|^def run\|^def main" tools/gates/run_all.py
grep -n "^def verified_at_head\|^def stale_reason" tools/gates/gate_claims.py
ls tools/gates/__init__.py tools/gates/tests/__init__.py tools/gates/tests/gitfixture.py
grep -n "tool_name\|^def " tools/gates/hooks/pre_tool_use.py
grep -n "unittest" .github/workflows/gates.yml
ls docs/superpowers/state/ ~/.claude/skills/strict-delivery/prompts/
```

Used names (S1 plan `2026-10-17-strict-delivery-S1-registers-core-gates.md`, Tasks 1–8): `tools.gates.lib.{Result, Line, GateError, CODE_ROOT, repo_root, gate_args, run_check, run_gate}` — every gate parses with `gate_args(prog)` (adds `--root <tree>`) and uses `root = a.root or repo_root()`; config is always `config.load(CODE_ROOT)`, never the tree under `--root`; `run_check` turns argparse `SystemExit` into `FAIL <name>.input`. `lib.registers.{STATE_DIR, load_findings, load_claims, load_decisions, finding_by_id, open_decisions, grant_for}` (registers are bare YAML lists; `merged` history entries are written only with `by: postmerge`, i.e. by S4's `postmerge.py`). `lib.gitinfo.{head_sha, branch, status_clean, worktrees, changed_files, staged_files}`. `gate_claims.{verified_at_head(root, claim, head) -> bool, stale_reason(root, claim, head, staged) -> str}`. `run_all.{GATES, gate_argv(name, args), run(names, args), main}` with `args = {"root": <tree>, "<gate>": [extra argv]}`; `gate_argv` always starts with `--root <tree>`. `hooks/pre_tool_use.py`: `CODE_ROOT`, `tree_for(event, command, options)`, `toplevel(directory)`, `gates_for(event)`, `main()` with the fail-closed `__main__` wrapper (the b3 DEC-0005 redesign has no `project_root`; b4 ruling 2(a)). Test helper `tools/gates/tests/gitfixture.py`: `REPO`, `STATE`, `git`, `write`, `new_repo`, `commit_all`, `remove` (and the package markers `tools/gates/__init__.py`, `tools/gates/tests/__init__.py`).

**One S1 extension this plan makes (Task 3):** a per-gate default argv for the S2 gates, used only when the caller passes nothing for that gate: `DEFAULT_ARGV` in `run_all.py`, applied in `gate_argv`. Each default is the gate's CI-meaningful mode (`dispatch --config-check`, `review_package --all`, `plan --in-flight`, `checkpoint --structure`).

---

## Block b4 — dispatch gate (Tasks 1–3; branch `block/strict-delivery-b4`; tag `strict-delivery-b4`)

### b4 preamble (controller, before Task 1)

- [ ] **Step 1: Branch from the tagged b3**

```bash
git fetch origin --tags && git checkout main && git pull --ff-only
test "$(git rev-parse strict-delivery-b3^{commit})" = "$(git rev-parse HEAD)" && echo b3-is-HEAD
git checkout -b block/strict-delivery-b4
```
Expected: `b3-is-HEAD`. Otherwise stop: S1 has not merged.

- [ ] **Step 2: Register rows for the nine S2 tasks (intake, spec §6.1)**

Every S2 task closes one maintenance row; from b5 the dispatch gate refuses an implementer without a `planned`/`in-progress` row.

```bash
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"
PLAN = "docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md"
SPEC = "docs/superpowers/specs/2026-10-17-strict-delivery-design.md"
TITLES = ["gate_dispatch", "PreToolUse Agent branch", "dispatch config check and run_all registration",
          "gate_review_package", "make_review_package", "gate_plan", "gate_checkpoint",
          "Stop and SubagentStop hooks", "per-plan state creator and checklists"]
sha = os.environ["SHA"]
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
if isinstance(doc, dict):
    key = next(k for k, v in doc.items() if v is None or isinstance(v, list))
    doc[key] = rows = doc[key] or []
else:
    doc = rows = doc or []
have = {r["id"] for r in rows}
for n, title in enumerate(TITLES, 1):
    fid = f"SD-S2-T{n}"
    if fid in have:
        continue
    rows.append({"id": fid, "source": PLAN, "severity": "INFO",
                 "class": f"strict-delivery rollout step 3: {title}", "siblings": [],
                 "cites": [{"file": SPEC, "lines": "189-207", "sha": sha}], "repro": None,
                 "owner": {"kind": "plan", "plan": "strict-delivery", "task": f"S2-T{n}"},
                 "state": "planned", "pin": None, "ruling": None, "evidence": [],
                 "history": [{"state": "open", "sha": sha, "by": "controller"},
                             {"state": "planned", "sha": sha, "by": "controller"}]})
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
python3 tools/gates/gate_register.py && git diff --stat
git add docs/superpowers/state/findings.yml
git commit -m "chore(state): register rows SD-S2-T1..T9 for strict-delivery S2"
```
Expected: `gate_register` prints only `PASS` lines; the diff touches `findings.yml` only.

- [ ] **Step 3: Ledger the start** — append `- b4 start <sha> rows SD-S2-T1..T9` to `docs/superpowers/state/strict-delivery/ledger.md` and commit with message `chore(state): b4 start`.

Note for the controller: after Task 2's commit the Agent hook is live in this checkout (`.claude/settings.json` already routes `Agent` to `pre_tool_use.py`, umbrella hook block). From Task 3 on, every dispatch prompt follows the umbrella's dispatch grammar.

### Task 1: gate_dispatch — grammar, roles, models, registers, strict block

**Files:**
- Create: `tools/gates/gate_dispatch.py`
- Create: `tools/gates/tests/repo_fixture.py`
- Create: `tools/gates/tests/fixtures/rows.yml`
- Create: `tools/gates/tests/fixtures/dispatch/prompts/implementer.md`
- Create: `tools/gates/tests/fixtures/dispatch/prompts/reviewer.md`
- Create: `tools/gates/tests/fixtures/dispatch/good_implementer.md`
- Create: `tools/gates/tests/fixtures/dispatch/good_reviewer.md`
- Test: `tools/gates/tests/test_gate_dispatch.py`

**Findings:** SD-S2-T1

**Siblings:** none -- the register row SD-S2-T1 lists no siblings

**Gates:** run_all, dispatch

**Interfaces:**
- Consumes: S1 `Result`, `GateError`, `CODE_ROOT`, `gate_args(prog)` (adds `--root`), `repo_root()`, `run_gate()`, `config.load(CODE_ROOT)` (config is never read from `--root`), `registers.load_findings/load_claims/load_decisions/finding_by_id`, `gitinfo.head_sha/staged_files`, `gate_claims.verified_at_head(root, claim, head) -> bool` and `gate_claims.stale_reason(root, claim, head, staged) -> str` (S1's claim-freshness rule: state verified and no cited path changed since `verified_at` or staged; never sha equality); test helper `gitfixture`.
- Produces: `gate_dispatch.check(args: list) -> Result`, `main(argv=None) -> int`; `parse(text) -> tuple[dict, int]`; `claim_fresh(root: str, claim: dict, head: str) -> str | None` (None = verified at `head`; used by Task 7); constants `ROLES`, `ROLE_PROMPT`, `DEFAULT_PROMPTS`. CLI: `[--json] --prompt P|- [--tool-model M] [--prompts-dir D]` (`--json` only as the first argument, as for every S1 gate). Line ids: `dispatch.{input,length,grammar,role,model.known,model.rank,model.fable,model.agree,brief,findings[.<id>],claims[.<id>],budget,report,decisions[.<id>],strict,task}`.
- Produces (tests): `repo_fixture.{REAL, FIX, STATE, DISPATCH_FILES, git, write, remove, commit, load_rows, write_registers, make_repo}` over S1's `gitfixture` (reused, not duplicated).

- [ ] **Step 1: Write the test helper and fixtures**

`tools/gates/tests/repo_fixture.py` (composes S1's `tools/gates/tests/gitfixture.py`; registers are bare YAML lists as in S1):
```python
"""S2 test repositories, built on S1's gitfixture. make_repo() commits the given files, then writes the
three registers from fixtures/rows.yml with every "@BASE@" value replaced by the sha of that commit, and
commits again. Config is never copied: gates read it from CODE_ROOT (S1). Tests point --root or
CLAUDE_PROJECT_DIR at the repo.
"""
from __future__ import annotations

import copy
import os
import re

import yaml

from tools.gates.tests import gitfixture as gf

REAL = gf.REPO
FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
STATE = "docs/superpowers/state"
DISPATCH_FILES = {
    "docs/cited.md": "cited\n",
    "docs/moved.md": "moved once\n",
    "docs/brief.md": "# Brief\nfixture task 1\n",
    "reports/.gitkeep": "",
    "docs/superpowers/plans/fixture-plan.md": "# Fixture plan\n",
}
git, write, remove = gf.git, gf.write, gf.remove
FALLBACK_LINE = re.compile(r"^fable_fallback:.*\n", re.M)


def config_text(fallback: str | None = None) -> str:
    """The real tools/gates/config.yml with its fable_fallback line removed if it has one (the owner's
    switch may be on or off, R1-1), then 'fable_fallback: <fallback>' appended (None: the key is absent,
    the DEC-0018 fallback is off)."""
    with open(os.path.join(REAL, "tools", "gates", "config.yml"), encoding="utf-8") as f:
        real = f.read()
    text, n = FALLBACK_LINE.subn("", real)
    if n > 1:
        raise AssertionError(f"tools/gates/config.yml holds {n} fable_fallback lines, expected 0 or 1")
    return text if fallback is None else text + f"fable_fallback: {fallback}\n"


def commit(root: str, message: str) -> str:
    return gf.commit_all(root, message)


def load_rows() -> dict:
    with open(os.path.join(FIX, "rows.yml"), encoding="utf-8") as f:
        return yaml.safe_load(f)


def _subst(obj, base: str):
    if isinstance(obj, dict):
        return {k: _subst(v, base) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_subst(v, base) for v in obj]
    return base if obj == "@BASE@" else obj


def write_registers(root: str, rows: dict, base: str) -> None:
    rows = _subst(copy.deepcopy(rows), base)
    for key in ("findings", "claims", "decisions"):
        write(root, f"{STATE}/{key}.yml", yaml.safe_dump(rows.get(key, []), sort_keys=False, allow_unicode=True))


def make_repo(files: dict, rows: dict, after: dict | None = None) -> str:
    root = gf.new_repo()
    for rel, text in files.items():
        write(root, rel, text)
    base = commit(root, "fixture: base files")
    write_registers(root, rows, base)
    commit(root, "fixture: registers")
    if after:
        for rel, text in after.items():
            write(root, rel, text)
        commit(root, "fixture: after")
    return root
```

`tools/gates/tests/fixtures/rows.yml` (one mapping of three lists; `write_registers` writes each list to its register file):
```yaml
# Register rows for the S2 gate tests. "@BASE@" becomes the sha of the first fixture commit.
findings:
  - id: SD-FX-1
    source: docs/cited.md
    severity: LOW
    class: fixture class one
    siblings: [sib-a]
    cites: [{file: docs/cited.md, lines: "1", sha: "@BASE@"}]
    repro: null
    owner: {kind: plan, plan: fx, task: "1"}
    state: planned
    pin: null
    ruling: null
    evidence: []
    history:
      - {state: open, sha: "@BASE@", by: fixture}
      - {state: planned, sha: "@BASE@", by: fixture}
  - id: SD-FX-2
    source: docs/cited.md
    severity: LOW
    class: fixture class two
    siblings: []
    cites: [{file: docs/cited.md, lines: "1", sha: "@BASE@"}]
    repro: null
    owner: {kind: plan, plan: fx, task: "2"}
    state: open
    pin: null
    ruling: null
    evidence: []
    history:
      - {state: open, sha: "@BASE@", by: fixture}
claims:
  - id: C-fx.ok
    doc: docs/cited.md
    anchor: L1
    text: docs/cited.md says cited
    direct: grep -n cited docs/cited.md
    contra: grep -c uncited docs/cited.md
    cites: [docs/cited.md]
    verified_at: "@BASE@"
    by: fixture
    state: verified
    finding: null
  - id: C-fx.stale
    doc: docs/cited.md
    anchor: L1
    text: docs/cited.md says cited
    direct: grep -n cited docs/cited.md
    contra: grep -c uncited docs/cited.md
    cites: [docs/cited.md]
    verified_at: "@BASE@"
    by: fixture
    state: stale
    finding: null
  - id: C-fx.moved
    doc: docs/moved.md
    anchor: L1
    text: docs/moved.md says moved once
    direct: grep -n "moved once" docs/moved.md
    contra: grep -c "moved twice" docs/moved.md
    cites: [docs/moved.md]
    verified_at: "@BASE@"
    by: fixture
    state: verified
    finding: null
decisions:
  - id: DEC-0001
    class: merge-grant
    question: "grant:fx"
    options: []
    asked_at: {sha: "@BASE@", by: fixture}
    answer: "yes"
    answered_at: {sha: "@BASE@", by: superbrobenji}
  - id: DEC-0002
    class: scope
    question: Does fixture task 3 enter the plan?
    options: [{label: enter, cost: one more task}, {label: defer, cost: none}]
    asked_at: {sha: "@BASE@", by: fixture}
    answer: null
    answered_at: null
```

`tools/gates/tests/fixtures/dispatch/prompts/implementer.md` (ends with one newline):
```
You are an implementer under strict delivery.
Verify every claim you make or read: one direct check and one contradiction check.
A bug from your own change leaves the task incomplete. Dispatch no subagents.
Write the full report to the report path; its first line is status: <STATUS>.
```

`tools/gates/tests/fixtures/dispatch/prompts/reviewer.md` (contains a `## ` heading on purpose — Review Focus 2):
```
You are a reviewer under strict delivery.
## Checks
Re-run every gate clean. Execute every claim in the report, the commits and the diff comments.
```

`tools/gates/tests/fixtures/dispatch/good_implementer.md` (the strict block is `prompts/implementer.md` byte for byte):
```
## Role: implementer
## Brief: docs/brief.md
## Findings: SD-FX-1
## Claims: C-fx.ok
## Budget: tokens=150000 minutes=60
## Model: sonnet
## Report: reports/t1.md
## Strict block
You are an implementer under strict delivery.
Verify every claim you make or read: one direct check and one contradiction check.
A bug from your own change leaves the task incomplete. Dispatch no subagents.
Write the full report to the report path; its first line is status: <STATUS>.
## Task
Implement fixture task 1 from docs/brief.md.
```

`tools/gates/tests/fixtures/dispatch/good_reviewer.md`:
```
## Role: task_reviewer
## Brief: docs/brief.md
## Findings: SD-FX-2
## Claims: none
## Budget: tokens=120000 minutes=30
## Model: opus
## Report: reports/r1.md
## Strict block
You are a reviewer under strict delivery.
## Checks
Re-run every gate clean. Execute every claim in the report, the commits and the diff comments.
## Task
Review fixture task 1.
```

- [ ] **Step 2: Write the failing tests**

`tools/gates/tests/test_gate_dispatch.py`:
```python
"""gate_dispatch through main() on fixture repos (strict-delivery S2 Tasks 1 and 3).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_dispatch -v
"""
import contextlib
import io
import json
import os
import shutil
import tempfile
import unittest
from unittest import mock

from tools.gates import gate_dispatch
from tools.gates.lib import config
from tools.gates.tests import repo_fixture as rf

DFIX = os.path.join(rf.FIX, "dispatch")
PROMPTS = os.path.join(DFIX, "prompts")


def fixture(name):
    with open(os.path.join(DFIX, name), encoding="utf-8", newline="") as f:
        return f.read()


GOOD = fixture("good_implementer.md")
REVIEWER = fixture("good_reviewer.md")


def gate(root, argv):
    buf = io.StringIO()
    with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf), \
            contextlib.redirect_stderr(io.StringIO()):    # argparse usage goes to stderr
        rc = gate_dispatch.main(argv)
    return rc, buf.getvalue()


def config_with_fallback(fallback=None):
    """The real config.yml with fable_fallback set to `fallback` (None: absent), parsed by config.load
    from a throwaway tree and injected at config.load (gates read config from CODE_ROOT, S1)."""
    d = tempfile.mkdtemp(prefix="gate-cfg-")
    try:
        rf.write(d, "tools/gates/config.yml", rf.config_text(fallback))
        conf = config.load(d)
    finally:
        shutil.rmtree(d, ignore_errors=True)
    return mock.patch("tools.gates.lib.config.load", return_value=conf)


class GateDispatch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = rf.make_repo(rf.DISPATCH_FILES, rf.load_rows(),
                                after={"docs/moved.md": "moved twice\n"})
        cls.conf = config.load(rf.REAL)
        # one prompt file per prompt name the umbrella lists, each ending in a newline
        cls.allp = tempfile.mkdtemp(prefix="gate-prompts-")
        for name in sorted(set(gate_dispatch.ROLE_PROMPT.values())):
            with open(os.path.join(cls.allp, name + ".md"), "w", encoding="utf-8", newline="") as f:
                f.write(f"You are the {name}.\n")

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)
        shutil.rmtree(cls.allp, ignore_errors=True)

    def role_prompt(self, role, *, brief="docs/brief.md", findings=None, budget="tokens=1000 minutes=10",
                    report="reports/x.md", prompts=None):
        """A well-formed prompt for `role` over the all-roles prompts dir (or `prompts`)."""
        if findings is None:
            findings = "SD-FX-1" if role in gate_dispatch.IMPLEMENTERS else "SD-FX-2"
        name = gate_dispatch.ROLE_PROMPT[role]
        with open(os.path.join(prompts or self.allp, name + ".md"), encoding="utf-8", newline="") as f:
            block = f.read()
        return (f"## Role: {role}\n## Brief: {brief}\n## Findings: {findings}\n## Claims: none\n"
                f"## Budget: {budget}\n## Model: {self.conf['models'][role]}\n## Report: {report}\n"
                f"## Strict block\n{block}## Task\nDo the fixture task.\n")

    def dispatch_role(self, text, prompts=None):
        return self.dispatch(text, prompts=prompts or self.allp)

    def dispatch(self, text, *extra, root=None, prompts=None):
        root = root or self.root
        path = os.path.join(root, "prompt.under-test.md")
        with open(path, "w", encoding="utf-8", newline="") as f:
            f.write(text)
        return gate(root, ["--prompt", path, "--prompts-dir", prompts or PROMPTS, *extra])

    def variant(self, old, new, base=GOOD):
        text = base.replace(old, new, 1)
        self.assertNotEqual(base, text, f"fixture lacks {old!r}")
        return text

    def assertFailLine(self, rc, out, line_id):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)

    def test_good_prompt_passes(self):
        rc, out = self.dispatch(GOOD)
        self.assertEqual(0, rc, out)
        self.assertNotIn("FAIL", out)

    def test_good_reviewer_with_inner_heading_passes(self):
        self.assertIn("\n## Checks\n", fixture("prompts/reviewer.md"))
        rc, out = self.dispatch(REVIEWER)
        self.assertEqual(0, rc, out)

    def test_wrong_model_fails(self):
        rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: haiku"))
        self.assertFailLine(rc, out, "dispatch.model.rank")

    def test_fable_on_implementer_fails(self):
        rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: fable"))
        self.assertFailLine(rc, out, "dispatch.model.fable")
        self.assertNotIn("FAIL dispatch.model.rank", out)

    def test_unknown_model_fails(self):
        rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: gpt"))
        self.assertFailLine(rc, out, "dispatch.model.known")

    def test_unknown_role_fails(self):
        rc, out = self.dispatch(self.variant("## Role: implementer", "## Role: helper"))
        self.assertFailLine(rc, out, "dispatch.role")

    def test_unregistered_finding_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: SD-FX-404"))
        self.assertFailLine(rc, out, "dispatch.findings.SD-FX-404")

    def test_implementer_on_open_finding_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: SD-FX-2"))
        self.assertFailLine(rc, out, "dispatch.findings.SD-FX-2")

    def test_implementer_on_a_pinned_or_merged_row_fails(self):
        rows = rf.load_rows()
        for fid, state in (("SD-FX-3", "pinned"), ("SD-FX-4", "merged"), ("SD-FX-5", "in-progress")):
            rows["findings"].append(dict(rows["findings"][0], id=fid, state=state))
        root = rf.make_repo(rf.DISPATCH_FILES, rows)
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        for fid, state in (("SD-FX-3", "pinned"), ("SD-FX-4", "merged")):
            rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", f"## Findings: {fid}"), root=root)
            self.assertFailLine(rc, out, f"dispatch.findings.{fid}")
            self.assertIn(f"state {state!r}; an implementer takes only planned or in-progress rows", out)
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: SD-FX-5"), root=root)
        self.assertEqual(0, rc, out)

    def test_implementer_without_findings_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1", "## Findings: none"))
        self.assertFailLine(rc, out, "dispatch.findings")

    def test_stale_claim_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.stale"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.stale")

    def test_unregistered_claim_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.404"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.404")
        self.assertIn("FAIL dispatch.claims.C-fx.404: not in docs/superpowers/state/claims.yml", out)

    def test_claim_whose_cited_file_changed_fails(self):
        rc, out = self.dispatch(self.variant("## Claims: C-fx.ok", "## Claims: C-fx.moved"))
        self.assertFailLine(rc, out, "dispatch.claims.C-fx.moved")
        self.assertIn("docs/moved.md", out)

    def test_open_decision_fails(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-0002.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_unknown_decision_fails(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-0404.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0404")

    def test_decision_answered_by_a_non_owner_fails(self):
        rows = rf.load_rows()
        rows["decisions"].append({
            "id": "DEC-0003", "class": "scope", "question": "Does fixture task 4 enter the plan?",
            "options": [{"label": "enter", "cost": "one more task"}, {"label": "defer", "cost": "none"}],
            "asked_at": {"sha": "@BASE@", "by": "fixture"}, "answer": "defer",
            "answered_at": {"sha": "@BASE@", "by": "fixture"}})
        root = rf.make_repo(rf.DISPATCH_FILES, rows)
        self.addCleanup(shutil.rmtree, root, ignore_errors=True)
        rc, out = self.dispatch(GOOD + "Scope follows DEC-0003.\n", root=root)
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0003")
        self.assertIn("unanswered", out)

    def test_decision_id_inside_a_word_fails(self):
        rc, out = self.dispatch(GOOD + "Scope follows_DEC-0002.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_a_five_digit_text_is_a_mention_of_its_first_four_digits(self):
        """DEC-0010 (umbrella pattern DEC-[0-9]{4}, no lookahead): DEC-00021 cites DEC-0002."""
        rc, out = self.dispatch(GOOD + "Scope follows DEC-00021.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_non_ascii_digits_never_form_an_id(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-\uff10\uff10\uff10\uff12 (fullwidth).\n")
        self.assertEqual(0, rc, out)
        self.assertNotIn("dispatch.decisions.DEC-", out.replace("PASS dispatch.decisions\n", ""))

    def test_answered_decision_passes(self):
        rc, out = self.dispatch(GOOD + "Merge under DEC-0001.\n")
        self.assertEqual(0, rc, out)

    def test_prompt_at_cap_passes_and_one_over_fails(self):
        cap = self.conf["dispatch"]["max_prompt_chars"]
        at_cap = GOOD + "x" * (cap - len(GOOD) - 1) + "\n"
        self.assertEqual(cap, len(at_cap))
        rc, out = self.dispatch(at_cap)
        self.assertEqual(0, rc, out)
        rc, out = self.dispatch(at_cap + "y")
        self.assertFailLine(rc, out, "dispatch.length")

    def test_strict_block_altered_by_one_byte_fails(self):
        block = fixture("prompts/implementer.md")
        i = GOOD.index(block) + len(block) // 2
        text = GOOD[:i] + ("X" if GOOD[i] != "X" else "Y") + GOOD[i + 1:]
        rc, out = self.dispatch(text)
        self.assertFailLine(rc, out, "dispatch.strict")

    def test_strict_block_must_be_followed_by_task(self):
        rc, out = self.dispatch(self.variant("## Task\n", "## Tasks\n"))
        self.assertFailLine(rc, out, "dispatch.strict")

    def test_empty_task_fails(self):
        rc, out = self.dispatch(self.variant("Implement fixture task 1 from docs/brief.md.\n", "\n"))
        self.assertFailLine(rc, out, "dispatch.task")

    def test_missing_brief_fails(self):
        rc, out = self.dispatch(self.variant("## Brief: docs/brief.md", "## Brief: docs/no-brief.md"))
        self.assertFailLine(rc, out, "dispatch.brief")

    def test_report_dir_missing_fails(self):
        rc, out = self.dispatch(self.variant("## Report: reports/t1.md", "## Report: nowhere/t1.md"))
        self.assertFailLine(rc, out, "dispatch.report")

    def test_section_out_of_order_fails(self):
        rc, out = self.dispatch(self.variant("## Findings: SD-FX-1\n## Claims: C-fx.ok\n",
                                             "## Claims: C-fx.ok\n## Findings: SD-FX-1\n"))
        self.assertFailLine(rc, out, "dispatch.grammar")

    def test_budget_over_role_cap_fails(self):
        rc, out = self.dispatch(self.variant("tokens=150000", "tokens=150001"))
        self.assertFailLine(rc, out, "dispatch.budget")

    def test_budget_with_trailing_text_fails(self):
        rc, out = self.dispatch(self.variant("## Budget: tokens=150000 minutes=60\n",
                                             "## Budget: tokens=150000 minutes=60 extra\n"))
        self.assertFailLine(rc, out, "dispatch.budget")
        self.assertIn("expected 'tokens=<n> minutes=<n>'", out)

    def test_fork_dispatch_fails(self):
        """Ruling 4 (B4-FIX-1): a fork runs on the session model with the session context."""
        for value in ("fork", "Fork", " fork "):
            with self.subTest(subagent_type=value):
                rc, out = self.dispatch(GOOD, "--tool-model", "sonnet", "--subagent-type", value)
                self.assertFailLine(rc, out, "dispatch.fork")
                self.assertIn("session model", out)
                self.assertIn("general-purpose", out)

    def test_fork_fails_even_when_the_prompt_is_not_in_the_grammar(self):
        rc, out = self.dispatch("not a dispatch prompt\n", "--subagent-type", "fork")
        self.assertFailLine(rc, out, "dispatch.fork")
        self.assertIn("FAIL dispatch.grammar", out)

    def test_other_subagent_types_keep_todays_behaviour(self):
        rc, base = self.dispatch(GOOD, "--tool-model", "sonnet")
        self.assertEqual(0, rc, base)
        self.assertNotIn("dispatch.fork", base)
        for value in ("general-purpose", "Explore", "claude", "forked", ""):
            with self.subTest(subagent_type=value):
                rc, out = self.dispatch(GOOD, "--tool-model", "sonnet", "--subagent-type", value)
                self.assertEqual(0, rc, out)
                self.assertEqual("PASS dispatch.fork\n" + base, out)

    def test_tool_model_must_agree(self):
        rc, out = self.dispatch(GOOD, "--tool-model", "sonnet")
        self.assertEqual(0, rc, out)
        rc, out = self.dispatch(GOOD, "--tool-model", "opus")
        self.assertFailLine(rc, out, "dispatch.model.agree")
        rc, out = self.dispatch(GOOD, "--tool-model", "")
        self.assertFailLine(rc, out, "dispatch.model.agree")

    def test_no_input_fails(self):
        rc, out = gate(self.root, [])
        self.assertFailLine(rc, out, "dispatch.input")

    def test_json_flag_first_prints_json(self):
        rc, out = self.dispatch(GOOD, "--tool-model", "sonnet")
        self.assertEqual(0, rc, out)
        path = os.path.join(self.root, "prompt.under-test.md")
        rc, out = gate(self.root, ["--json", "--prompt", path, "--prompts-dir", PROMPTS])
        self.assertEqual(0, rc, out)
        doc = json.loads(out)
        self.assertEqual("dispatch", doc["gate"])
        self.assertTrue(doc["ok"])

    def test_json_flag_after_other_arguments_fails(self):
        rc, out = self.dispatch(GOOD, "--json")
        self.assertFailLine(rc, out, "dispatch.input")

    def test_report_naming_a_directory_fails(self):
        for value in ("reports", "reports/"):
            rc, out = self.dispatch(self.variant("## Report: reports/t1.md", f"## Report: {value}"))
            self.assertFailLine(rc, out, "dispatch.report")
            self.assertIn("names a directory", out)

    def test_every_role_passes_with_its_prompt_file(self):
        self.assertEqual({"controller", "implementer", "reviewer", "verifier", "exception-reviewer",
                          "audit-reviewer", "audit-verifier", "investigator"},
                         set(gate_dispatch.ROLE_PROMPT.values()))
        self.assertEqual(set(gate_dispatch.ROLES), set(gate_dispatch.ROLE_PROMPT))
        for role in gate_dispatch.ROLES:
            rc, out = self.dispatch_role(self.role_prompt(role))
            self.assertEqual(0, rc, f"{role}: {out}")

    def test_audit_and_investigator_roles_may_name_no_findings_and_no_brief(self):
        for role in ("audit_reviewer", "audit_verifier", "investigator"):
            rc, out = self.dispatch_role(self.role_prompt(role, brief="none", findings="none"))
            self.assertEqual(0, rc, f"{role}: {out}")

    def test_controller_budget_caps(self):
        rc, out = self.dispatch_role(self.role_prompt("controller", budget="tokens=250000 minutes=90"))
        self.assertEqual(0, rc, out)
        for budget in ("tokens=250001 minutes=90", "tokens=250000 minutes=91"):
            rc, out = self.dispatch_role(self.role_prompt("controller", budget=budget))
            self.assertFailLine(rc, out, "dispatch.budget")

    def test_reviewer_token_cap(self):
        rc, out = self.dispatch_role(self.role_prompt("task_reviewer", budget="tokens=120000 minutes=30"))
        self.assertEqual(0, rc, out)
        rc, out = self.dispatch_role(self.role_prompt("task_reviewer", budget="tokens=120001 minutes=30"))
        self.assertFailLine(rc, out, "dispatch.budget")

    def test_zero_budget_fails(self):
        for budget in ("tokens=0 minutes=10", "tokens=1000 minutes=0"):
            rc, out = self.dispatch_role(self.role_prompt("implementer", budget=budget))
            self.assertFailLine(rc, out, "dispatch.budget")

    def test_prompt_file_without_final_newline_fails(self):
        bad = tempfile.mkdtemp(prefix="gate-prompts-")
        self.addCleanup(shutil.rmtree, bad, ignore_errors=True)
        with open(os.path.join(bad, "implementer.md"), "w", encoding="utf-8", newline="") as f:
            f.write("You are the implementer.")
        rc, out = self.dispatch_role(self.role_prompt("implementer"), prompts=bad)
        self.assertFailLine(rc, out, "dispatch.strict")
        self.assertIn("must end with a newline", out)

    def test_transcription_implementer_needs_a_planned_row(self):
        rc, out = self.dispatch_role(self.role_prompt("implementer_transcription", findings="SD-FX-2"))
        self.assertFailLine(rc, out, "dispatch.findings.SD-FX-2")

    def test_reviewer_needs_a_brief(self):
        rc, out = self.dispatch_role(self.role_prompt("task_reviewer", brief="none"))
        self.assertFailLine(rc, out, "dispatch.brief")

    def test_prompt_from_stdin_passes(self):
        with mock.patch("sys.stdin", io.StringIO(GOOD)):
            rc, out = gate(self.root, ["--prompt", "-", "--prompts-dir", PROMPTS])
        self.assertEqual(0, rc, out)

    # -- DEC-0018: the fable_fallback key (B5-FIX-2)
    def fable_role_on(self, role, model, fallback, tool_model=None):
        text = self.role_prompt(role).replace("## Model: fable\n", f"## Model: {model}\n", 1)
        self.assertIn(f"## Model: {model}\n", text)
        extra = [] if tool_model is None else ["--tool-model", tool_model]
        with config_with_fallback(fallback):
            return self.dispatch(text, *extra, prompts=self.allp)

    def test_fallback_on_lets_a_fable_role_run_on_opus(self):
        for role in ("final_verifier", "exception_reviewer"):
            with self.subTest(role):
                rc, out = self.fable_role_on(role, "opus", "opus", tool_model="opus")
                self.assertEqual(0, rc, out)
                lines = [l for l in out.splitlines() if l.startswith("PASS dispatch.model.fallback: ")]
                self.assertEqual(1, len(lines), out)
                self.assertIn("DEC-0018", lines[0])
                self.assertIn(f"{role} on opus", lines[0])
                self.assertNotIn("dispatch.model.rank", out)
                self.assertIn("PASS dispatch.model.agree", out)

    def test_fallback_absent_keeps_the_rank_failure(self):
        for role in ("final_verifier", "exception_reviewer"):
            with self.subTest(role):
                rc, out = self.fable_role_on(role, "opus", None)
                self.assertFailLine(rc, out, f"dispatch.model.rank: role {role} needs at least fable, got opus")
                self.assertNotIn("dispatch.model.fallback", out)

    def test_fable_stays_allowed_with_the_fallback_on(self):
        """A model that meets the role's rank passes dispatch.model.rank whatever fable_fallback holds: the fallback
        branch is taken only when the rank check would fail, so even an invalid value ('fable', 'gpt'; --config-check
        fails those) prints no dispatch.model.fallback line for a fable dispatch (R1-3)."""
        for fallback in ("opus", None, "fable", "gpt"):
            with self.subTest(fallback=fallback):
                rc, out = self.fable_role_on("final_verifier", "fable", fallback)
                self.assertEqual(0, rc, out)
                self.assertIn("PASS dispatch.model.rank\n", out)
                self.assertNotIn("dispatch.model.fallback", out)

    def test_a_model_below_the_fallback_still_fails_rank(self):
        for model in ("sonnet", "haiku"):
            with self.subTest(model):
                rc, out = self.fable_role_on("final_verifier", model, "opus")
                self.assertFailLine(rc, out, f"dispatch.model.rank: role final_verifier needs at least fable, got {model}")
                self.assertNotIn("dispatch.model.fallback", out)

    def test_an_invalid_fallback_admits_no_lower_model(self):
        for fallback in ("sonnet", "haiku"):
            with self.subTest(fallback):
                rc, out = self.fable_role_on("final_verifier", fallback, fallback)
                self.assertFailLine(rc, out, "dispatch.model.fallback: ")
                self.assertIn(f"fable_fallback {fallback!r} must be a model_rank model ranked at least opus and below fable", out)
                self.assertNotIn("PASS dispatch.model.fallback", out)

    def test_non_fable_roles_are_unchanged_by_the_fallback(self):
        with config_with_fallback("opus"):
            rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: fable"))
            self.assertFailLine(rc, out, "dispatch.model.fable")
            self.assertNotIn("dispatch.model.fallback", out)
            rc, out = self.dispatch(GOOD)
            self.assertEqual(0, rc, out)
            self.assertNotIn("dispatch.model.fallback", out)
            rc, out = self.dispatch(self.variant("## Model: sonnet", "## Model: haiku"))
            self.assertFailLine(rc, out, "dispatch.model.rank")
            for role in ("task_reviewer", "controller"):
                rc, out = self.dispatch_role(self.role_prompt(role))
                self.assertEqual(0, rc, f"{role}: {out}")
                self.assertIn("PASS dispatch.model.rank\n", out)
                self.assertNotIn("dispatch.model.fallback", out)
        with config_with_fallback("opus") as load:    # a fable minimum outside fable_roles (--config-check fails it)
            load.return_value["models"]["task_reviewer"] = "fable"
            rc, out = self.dispatch_role(self.role_prompt("task_reviewer"))    # ## Model: opus
            self.assertFailLine(rc, out, "dispatch.model.rank: role task_reviewer needs at least fable, got opus")
            self.assertNotIn("dispatch.model.fallback", out)

    def test_readme_describes_the_fallback(self):
        """Sibling 'README gate_dispatch section' (B5-FIX-2): the section and the table row name the key, its
        validation, the fallback PASS line and DEC-0018."""
        with open(os.path.join(rf.REAL, "tools", "gates", "README.md"), encoding="utf-8") as f:
            readme = f.read()
        section = readme[readme.index("## gate_dispatch (S2)\n"):readme.index("\n## make_review_package")]
        for word in ("`fable_fallback`", "PASS dispatch.model.fallback", "FAIL dispatch.config.fable_fallback",
                     "DEC-0018", "ranked at", "15 lines."):
            self.assertIn(word, section)
        row = [l for l in readme.splitlines() if l.startswith("| `gate_dispatch.py` |")]
        self.assertEqual(1, len(row))
        for word in ("`fable_fallback`", "DEC-0018", "PASS dispatch.model.fallback"):
            self.assertIn(word, row[0])

    def test_spec_and_config_name_the_fallback(self):
        """Sibling 'spec D18/§5 text' (B5-FIX-2): the spec and tools/gates/config.yml agree on the key."""
        with open(os.path.join(rf.REAL, "docs/superpowers/specs/2026-10-17-strict-delivery-design.md"),
                  encoding="utf-8") as f:
            spec = f.read().splitlines()
        d18 = [l for l in spec if l.startswith("| D18 |")]
        self.assertEqual(1, len(d18), "spec D18 row")
        for word in ("`fable_fallback`", "DEC-0018", "run on opus"):
            self.assertIn(word, d18[0])
        for row in ("| Final adversarial verifier |", "| Exception (P10) reviewer |"):
            hits = [l for l in spec if l.startswith(row)]
            self.assertEqual(1, len(hits), row)
            self.assertIn("Fable (opus while the DEC-0018 fallback is on)", hits[0])
        with open(os.path.join(rf.REAL, "tools", "gates", "config.yml"), encoding="utf-8") as f:
            text = f.read()
        self.assertRegex(text, r"(?m)^# .*\bDEC-0018\b.*$")
        self.assertRegex(text, r"(?m)^# .*\bfable_fallback\b.*$")
        conf = config.load(rf.REAL)
        self.assertIn(conf.get("fable_fallback", "opus"), ("opus",), "the spec names opus as the fallback")


class ConfigCheck(unittest.TestCase):
    def with_config(self, edit):
        """Config is read from CODE_ROOT (S1), so a bad config is injected at config.load."""
        conf = config.load(rf.REAL)
        edit(conf)
        return mock.patch("tools.gates.lib.config.load", return_value=conf)

    def test_config_check_passes_on_repo_config(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.implementer", out)

    def test_config_check_fails_when_implementer_minimum_is_fable(self):
        with self.with_config(lambda c: c["models"].__setitem__("implementer", "fable")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.implementer", out)

    def test_config_check_fails_on_an_unknown_role(self):
        with self.with_config(lambda c: c["models"].__setitem__("helper", "opus")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.models", out)

    def test_config_check_fails_on_a_non_positive_cap(self):
        with self.with_config(lambda c: c["dispatch"].__setitem__("max_prompt_chars", 0)):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.max_prompt_chars", out)

    def test_config_check_fails_on_a_cap_that_is_not_an_integer(self):
        for bad in (True, 1.5, "12000"):
            with self.subTest(cap=bad), \
                    self.with_config(lambda c, b=bad: c["dispatch"].__setitem__("max_prompt_chars", b)):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.max_prompt_chars", out)

    def test_config_check_fails_on_a_minimum_that_is_not_in_model_rank(self):
        with self.with_config(lambda c: c["models"].__setitem__("implementer", "gpt")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.implementer: minimum model 'gpt' not in model_rank", out)

    def test_config_check_fails_on_a_role_with_no_minimum(self):
        with self.with_config(lambda c: c["models"].pop("investigator")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.investigator: minimum model None not in model_rank", out)

    def test_config_check_fails_on_an_unknown_fable_role(self):
        with self.with_config(lambda c: c["fable_roles"].append("helper")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.fable_roles: unknown roles: helper", out)

    def test_config_check_fails_when_fable_roles_differ_from_the_fable_minimums(self):
        """DEC-0012 = require-equal: fable_roles is exactly the roles whose minimum model is fable."""
        for name, edit in (("extra", lambda c: c["fable_roles"].append("implementer")),
                           ("missing", lambda c: c["fable_roles"].remove("exception_reviewer"))):
            with self.subTest(name), self.with_config(edit):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.fable_roles: ", out)
                self.assertIn("fable minimums ['exception_reviewer', 'final_verifier']", out)

    def test_config_check_names_both_fable_sets(self):
        with self.with_config(lambda c: c["fable_roles"].append("implementer")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_roles: fable_roles ['exception_reviewer', 'final_verifier',"
                      " 'implementer'] != the roles whose minimum model is fable"
                      " (fable minimums ['exception_reviewer', 'final_verifier'])", out)

    def test_config_check_passes_model_rank_on_repo_config(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.model_rank\n", out)
        self.assertIn("PASS dispatch.config.fable_roles\n", out)

    def test_config_check_fails_when_model_rank_is_not_strictly_ordered(self):
        bad = ({"haiku": 1, "sonnet": 2, "opus": 5, "fable": 4}, {"haiku": 1, "sonnet": 2, "opus": 3, "fable": 3},
               {"haiku": 2, "sonnet": 1, "opus": 3, "fable": 4}, {"haiku": 1, "sonnet": 2, "opus": 3},
               {"haiku": 1, "sonnet": 2, "opus": 3, "fable": "4"}, {"haiku": 1, "sonnet": 2, "opus": 3, "fable": True},
               {"haiku": True, "sonnet": 2, "opus": 3, "fable": 4})
        for rank in bad:
            with self.subTest(rank=rank), self.with_config(lambda c, r=rank: c.__setitem__("model_rank", dict(r))):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.model_rank: must order haiku < sonnet < opus < fable", out)

    def test_config_check_fails_when_a_role_has_no_prompt_file(self):
        patched = {k: v for k, v in gate_dispatch.ROLE_PROMPT.items() if k != "investigator"}
        with mock.patch.object(gate_dispatch, "ROLE_PROMPT", patched):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.investigator: no prompt file mapped for the role", out)

    def test_neither_prompt_nor_config_check_is_a_bad_argument(self):
        rc, out = gate(rf.REAL, [])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.input: bad arguments", out)

    def test_prompt_and_config_check_are_exclusive(self):
        rc, out = gate(rf.REAL, ["--config-check", "--prompt", "x.md"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.input", out)

    # -- DEC-0018: the fable_fallback key (B5-FIX-2)
    def test_config_check_passes_the_repo_fallback(self):
        """The real config passes in whichever state the owner's switch is in (R1-1): set, its PASS line names
        the value; absent, the off line."""
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        conf = config.load(rf.REAL)
        if "fable_fallback" in conf:
            v = conf["fable_fallback"]
            want = f"PASS dispatch.config.fable_fallback: {v}: the fable_roles run on {v} while Fable is unavailable (DEC-0018)"
        else:
            want = "PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)"
        self.assertIn(want + "\n", out + "\n")
        self.assertEqual(15, len(out.splitlines()), out)

    def test_config_text_sets_the_key_whether_or_not_the_repo_has_it(self):
        """R1-1: the fixtures set or remove the key themselves, so the suite does not depend on the repo's switch.
        A copy of the real config with the key cleared (and one with it set) gives the same fixture configs."""
        with open(os.path.join(rf.REAL, "tools", "gates", "config.yml"), encoding="utf-8") as f:
            real = f.read()
        bare = rf.FALLBACK_LINE.sub("", real)
        self.assertNotIn("\nfable_fallback:", "\n" + bare)
        for state, text in (("cleared", bare), ("set", bare + "fable_fallback: opus\n")):
            d = tempfile.mkdtemp(prefix="gate-cfgsrc-")
            self.addCleanup(shutil.rmtree, d, True)
            rf.write(d, "tools/gates/config.yml", text)
            with self.subTest(state), mock.patch.object(rf, "REAL", d):
                self.assertEqual(bare, rf.config_text(None))
                self.assertEqual(bare + "fable_fallback: sonnet\n", rf.config_text("sonnet"))
                with config_with_fallback(None) as load:
                    self.assertNotIn("fable_fallback", load.return_value)
                    rc, out = gate(rf.REAL, ["--config-check"])
                self.assertIn("PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)", out)
                with config_with_fallback("opus") as load:
                    self.assertEqual("opus", load.return_value["fable_fallback"])
        d = tempfile.mkdtemp(prefix="gate-cfgsrc-")
        self.addCleanup(shutil.rmtree, d, True)
        rf.write(d, "tools/gates/config.yml", bare + "fable_fallback: opus\nfable_fallback: opus\n")
        with mock.patch.object(rf, "REAL", d), self.assertRaisesRegex(AssertionError, "holds 2 fable_fallback lines"):
            rf.config_text(None)

    def test_config_check_passes_an_absent_fallback(self):
        with config_with_fallback(None) as load:
            self.assertNotIn("fable_fallback", load.return_value)
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)", out)
        self.assertEqual(15, len(out.splitlines()), out)

    def test_config_check_fails_an_invalid_fallback(self):
        for value in ("sonnet", "haiku", "fable", "gpt", "null", "[opus]", "3", "Opus"):
            with self.subTest(value), config_with_fallback(value):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback ", out)
                self.assertIn("must be a model_rank model ranked at least opus and below fable", out)
                self.assertNotIn("PASS dispatch.config.fable_fallback", out)

    def test_config_check_ranks_the_fallback_by_model_rank(self):
        """A model_rank that ranks sonnet with opus admits sonnet; one that ranks opus with fable admits nothing."""
        with config_with_fallback("sonnet") as load:
            load.return_value["model_rank"] = {"haiku": 1, "sonnet": 3, "opus": 3, "fable": 4}
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("PASS dispatch.config.fable_fallback: sonnet: ", out)
        with config_with_fallback("opus") as load:
            load.return_value["model_rank"] = {"haiku": 1, "sonnet": 2, "opus": 4, "fable": 4}
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback 'opus' ", out)
        with config_with_fallback("opus") as load:    # no fable rank: fail closed, no KeyError
            load.return_value["model_rank"].pop("fable")
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback 'opus' ", out)

    def test_run_all_runs_dispatch_in_config_check_mode(self):
        from tools.gates import run_all
        self.assertIn("dispatch", run_all.GATES)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": rf.REAL}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "dispatch"])
        self.assertEqual(0, rc, buf.getvalue())
        self.assertIn("dispatch.config.implementer", buf.getvalue())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_dispatch -v`
Expected: ERROR `ImportError: cannot import name 'gate_dispatch'` (or `ModuleNotFoundError`).

- [ ] **Step 4: Implement `tools/gates/gate_dispatch.py`**

```python
#!/usr/bin/env python3
"""gate_dispatch -- a subagent dispatch prompt obeys the strict-delivery dispatch grammar.

Grammar and rules: docs/superpowers/plans/2026-10-17-strict-delivery.md, "Dispatch prompt grammar".
Usage: gate_dispatch.py [--json] --prompt P|- [--tool-model M] [--subagent-type T] [--prompts-dir D]
       | [--json] --config-check
(--json is read by run_gate and only as the first argument, like every S1 gate.)
"""
from __future__ import annotations

import argparse
import os
import re
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import CODE_ROOT, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as gconfig  # noqa: E402
from tools.gates.gate_claims import stale_reason, verified_at_head  # noqa: E402
from tools.gates.gate_decisions import REF as DEC_RE  # noqa: E402    # one scan pattern for both gates
from tools.gates.lib import gitinfo, registers  # noqa: E402

ROLES = ["controller", "implementer", "implementer_transcription", "task_reviewer", "final_reviewer",
         "final_verifier", "exception_reviewer", "audit_reviewer", "audit_verifier", "investigator"]
# The umbrella names ten roles but eight prompt files; this map is the only place they meet.
ROLE_PROMPT = {"controller": "controller", "implementer": "implementer",
               "implementer_transcription": "implementer", "task_reviewer": "reviewer",
               "final_reviewer": "reviewer", "final_verifier": "verifier",
               "exception_reviewer": "exception-reviewer", "audit_reviewer": "audit-reviewer",
               "audit_verifier": "audit-verifier", "investigator": "investigator"}
IMPLEMENTERS = {"implementer", "implementer_transcription"}
BRIEF_ROLES = IMPLEMENTERS | {"task_reviewer", "final_reviewer", "final_verifier", "exception_reviewer"}
FINDINGS_NONE_ROLES = {"investigator", "audit_reviewer", "audit_verifier"}
HEADERS = ["Role", "Brief", "Findings", "Claims", "Budget", "Model", "Report"]
STRICT, TASK = "## Strict block\n", "## Task\n"
BUDGET_RE = re.compile(r"^tokens=(\d+) minutes=(\d+)$")
RANK_ORDER = ["haiku", "sonnet", "opus", "fable"]    # DEC-0012: model_rank orders these strictly
DEFAULT_PROMPTS = os.path.expanduser("~/.claude/skills/strict-delivery/prompts")


class Grammar(Exception):
    """The prompt's sections are missing or out of order."""


def parse(text: str) -> tuple[dict, int]:
    """(header fields, offset of the first character after '## Strict block\\n')."""
    fields, pos = {}, 0
    for key in HEADERS:
        while text.startswith("\n", pos):
            pos += 1
        end = text.find("\n", pos)
        line = text[pos:end] if end >= 0 else text[pos:]
        prefix = f"## {key}: "
        if end < 0 or not line.startswith(prefix) or not line[len(prefix):].strip():
            raise Grammar(f"expected '{prefix}<value>' as the next section, got {line[:60]!r}")
        fields[key] = line[len(prefix):].strip()
        pos = end + 1
    while text.startswith("\n", pos):
        pos += 1
    if not text.startswith(STRICT, pos):
        raise Grammar(f"expected '## Strict block' after '## Report:', got {text[pos:pos + 60]!r}")
    return fields, pos + len(STRICT)


def id_list(value: str) -> list[str]:
    if value.strip().lower() == "none":
        return []
    return [v.strip() for v in value.split(",")]


def resolve(root: str, path: str) -> str:
    return path if os.path.isabs(path) else os.path.join(root, path)


def read_prompt(path: str) -> str:
    """'--prompt -' reads the prompt from stdin (umbrella "CLI flags and conventions")."""
    if path == "-":
        return sys.stdin.read()
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def claim_fresh(root: str, claim: dict, head: str) -> str | None:
    """None when the row is verified at head -- S1's rule (gate_claims.verified_at_head): state verified
    and no cited path changed between verified_at and head or in the index; otherwise the reason."""
    if claim.get("state") != "verified":
        return f"state is {claim.get('state')!r}, not 'verified'"
    if verified_at_head(root, claim, head):
        return None
    return stale_reason(root, claim, head, gitinfo.staged_files(root)) or "row fails the claims schema"


def fallback_error(conf) -> str | None:
    """None when config's optional fable_fallback (DEC-0018) is a model_rank model ranked at least opus and
    strictly below fable; otherwise the reason. Absent is never an error (the fallback is off)."""
    if "fable_fallback" not in conf:
        return None
    value, rank = conf["fable_fallback"], conf["model_rank"]
    ok = isinstance(value, str) and value in rank and all(isinstance(rank.get(m), int) for m in ("opus", "fable")) \
        and rank["opus"] <= rank[value] < rank["fable"]
    return None if ok else f"fable_fallback {value!r} must be a model_rank model ranked at least opus and below fable"


def _model(conf, role, model, tool_model, res):
    rank, need, fable = conf["model_rank"], conf["models"][role], conf["fable_roles"]
    if model not in rank:
        res.fail("dispatch.model.known", f"model {model!r} not in model_rank {sorted(rank)}")
        return
    res.passed("dispatch.model.known")
    if rank[model] < rank[need] and role in fable and "fable_fallback" in conf and model == conf["fable_fallback"]:
        err = fallback_error(conf)    # DEC-0018: the owner-switched fallback replaces the rank check
        if err:
            res.fail("dispatch.model.fallback", err)
        else:
            res.passed("dispatch.model.fallback", f"{role} on {model}: config fable_fallback is on while Fable is "
                       "unavailable (DEC-0018)")
    elif rank[model] < rank[need]:
        res.fail("dispatch.model.rank", f"role {role} needs at least {need}, got {model}")
    else:
        res.passed("dispatch.model.rank")
    if model == "fable" and role not in conf["fable_roles"]:
        res.fail("dispatch.model.fable", f"fable is reserved for {', '.join(fable)}; role is {role}")
    else:
        res.passed("dispatch.model.fable")
    if tool_model is None:
        return
    if not tool_model:
        res.fail("dispatch.model.agree", "tool_input.model is not set; the subagent would inherit the session model")
    elif tool_model != model:
        res.fail("dispatch.model.agree", f"tool_input.model {tool_model!r} != '## Model: {model}'")
    else:
        res.passed("dispatch.model.agree")


def _fork(subagent_type, res):
    """Ruling 4 (B4-FIX-1): the Agent tool runs a fork on the session model with the session's context
    and ignores tool_input.model, so a fork dispatch is refused whatever its prompt says."""
    if subagent_type is None:
        return
    if subagent_type.strip().lower() == "fork":
        res.fail("dispatch.fork", "a fork runs on the session model with the session context; "
                 "dispatch a general-purpose agent with a brief")
    else:
        res.passed("dispatch.fork")


def _brief(root, role, value, res):
    if value.lower() == "none" and role not in BRIEF_ROLES:
        res.passed("dispatch.brief")
    elif value.lower() != "none" and os.path.isfile(resolve(root, value)):
        res.passed("dispatch.brief")
    else:
        res.fail("dispatch.brief", f"role {role} needs an existing brief file; got {value!r}")


def _findings(root, role, value, res):
    ids = id_list(value)
    if not ids:
        if role in FINDINGS_NONE_ROLES:
            res.passed("dispatch.findings")
        else:
            res.fail("dispatch.findings", f"role {role} must name the register ids it works on")
        return
    findings = registers.load_findings(root)
    for fid in ids:
        f = registers.finding_by_id(findings, fid)
        if f is None:
            res.fail(f"dispatch.findings.{fid}", "not in docs/superpowers/state/findings.yml")
        elif role in IMPLEMENTERS and f["state"] not in ("planned", "in-progress"):
            res.fail(f"dispatch.findings.{fid}",
                     f"state {f['state']!r}; an implementer takes only planned or in-progress rows")
        else:
            res.passed(f"dispatch.findings.{fid}")


def _claims(root, value, res):
    ids = id_list(value)
    if not ids:
        res.passed("dispatch.claims")
        return
    by_id = {c["id"]: c for c in registers.load_claims(root)}
    head = gitinfo.head_sha(root)
    for cid in ids:
        c = by_id.get(cid)
        reason = "not in docs/superpowers/state/claims.yml" if c is None else claim_fresh(root, c, head)
        if reason:
            res.fail(f"dispatch.claims.{cid}", reason)
        else:
            res.passed(f"dispatch.claims.{cid}")


def _budget(conf, role, value, res):
    m = BUDGET_RE.match(value)
    if not m:
        res.fail("dispatch.budget", f"expected 'tokens=<n> minutes=<n>', got {value!r}")
        return
    tokens, minutes = int(m.group(1)), int(m.group(2))
    t = conf["triggers"]
    if role == "controller":
        cap_t, cap_m = t["controller"]["tokens"], t["controller"]["minutes"]
    elif role in IMPLEMENTERS:
        cap_t, cap_m = t["implementer"]["tokens"], None
    else:
        cap_t, cap_m = t["reviewer"]["tokens"], None
    if tokens <= 0 or minutes <= 0:
        res.fail("dispatch.budget", "tokens and minutes must be positive")
    elif tokens > cap_t or (cap_m is not None and minutes > cap_m):
        res.fail("dispatch.budget", f"budget {value!r} exceeds the {role} trigger (tokens {cap_t}"
                 + (f", minutes {cap_m})" if cap_m is not None else ")"))
    else:
        res.passed("dispatch.budget")


def _report(root, value, res):
    target = resolve(root, value)
    folder = os.path.dirname(target)
    if value.endswith("/") or os.path.isdir(target):
        res.fail("dispatch.report", f"report path {value!r} names a directory; it must name a file")
    elif os.path.isdir(folder):
        res.passed("dispatch.report")
    else:
        res.fail("dispatch.report", f"report directory {os.path.dirname(value) or '.'!r} does not exist")


def _decisions(root, text, res):
    ids = sorted(set(DEC_RE.findall(text)))
    if not ids:
        res.passed("dispatch.decisions")
        return
    decisions = registers.load_decisions(root)
    known = {d.get("id") for d in decisions}
    unanswered = {d.get("id") for d in registers.open_decisions(decisions, owner=registers.owner_handle())}
    for did in ids:
        if did not in known:
            res.fail(f"dispatch.decisions.{did}", "not in docs/superpowers/state/decisions.yml")
        elif did in unanswered:
            res.fail(f"dispatch.decisions.{did}", "unanswered owner decision; the path halts until the owner answers")
        else:
            res.passed(f"dispatch.decisions.{did}")


def _strict(text, start, role, prompts_dir, res):
    path = os.path.join(prompts_dir, ROLE_PROMPT[role] + ".md")
    if not os.path.isfile(path):
        res.fail("dispatch.strict", f"prompt block file {path} not found")
        return
    with open(path, encoding="utf-8", newline="") as f:
        block = f.read()
    if not block.endswith("\n"):
        res.fail("dispatch.strict", f"{path} must end with a newline")
        return
    got = text[start:start + len(block)]
    if got != block:
        i = next((k for k, (x, y) in enumerate(zip(got, block)) if x != y), min(len(got), len(block)))
        res.fail("dispatch.strict", f"strict block differs from {path} at character {i}")
        return
    rest = text[start + len(block):]
    if not rest.startswith(TASK):
        res.fail("dispatch.strict", "the strict block must be followed directly by '## Task'")
        return
    res.passed("dispatch.strict")
    if rest[len(TASK):].strip():
        res.passed("dispatch.task")
    else:
        res.fail("dispatch.task", "the '## Task' section is empty")


def check_prompt(root, conf, text, tool_model, prompts_dir, res, subagent_type=None) -> None:
    _fork(subagent_type, res)
    cap = conf["dispatch"]["max_prompt_chars"]
    if len(text) > cap:
        res.fail("dispatch.length", f"{len(text)} chars > dispatch.max_prompt_chars {cap}")
    else:
        res.passed("dispatch.length")
    try:
        fields, start = parse(text)
    except Grammar as e:
        res.fail("dispatch.grammar", str(e))
        return
    res.passed("dispatch.grammar")
    role = fields["Role"]
    if role not in ROLES or role not in conf["models"]:
        res.fail("dispatch.role", f"unknown role {role!r}; roles: {', '.join(ROLES)}")
        return
    res.passed("dispatch.role")
    _model(conf, role, fields["Model"], tool_model, res)
    _brief(root, role, fields["Brief"], res)
    _findings(root, role, fields["Findings"], res)
    _claims(root, fields["Claims"], res)
    _budget(conf, role, fields["Budget"], res)
    _report(root, fields["Report"], res)
    _decisions(root, text, res)
    _strict(text, start, role, prompts_dir, res)


def check_config(conf: dict, res: Result) -> None:
    """CI mode: the model map, fable_roles, model_rank, the prompt cap and the optional fable_fallback agree with
    the dispatch grammar (DEC-0012: fable_roles equals the fable-minimum roles; model_rank orders
    haiku < sonnet < opus < fable; DEC-0018: fable_fallback, when present, ranks at least opus and below fable)."""
    models, rank, fable = conf["models"], conf["model_rank"], set(conf["fable_roles"])
    for role in ROLES:
        need = models.get(role)
        if need not in rank:
            res.fail(f"dispatch.config.{role}", f"minimum model {need!r} not in model_rank")
        elif need == "fable" and role not in fable:
            res.fail(f"dispatch.config.{role}", "minimum model fable but the role is not in fable_roles")
        elif ROLE_PROMPT.get(role) is None:
            res.fail(f"dispatch.config.{role}", "no prompt file mapped for the role")
        else:
            res.passed(f"dispatch.config.{role}")
    extra = sorted(set(models) - set(ROLES))
    if extra:
        res.fail("dispatch.config.models", f"roles outside the dispatch grammar: {', '.join(extra)}")
    else:
        res.passed("dispatch.config.models")
    stray = sorted(fable - set(ROLES))
    minimums = sorted(r for r in ROLES if models.get(r) == "fable")
    if stray:
        res.fail("dispatch.config.fable_roles", f"unknown roles: {', '.join(stray)}")
    elif sorted(fable) != minimums:    # DEC-0012 = require-equal
        res.fail("dispatch.config.fable_roles", f"fable_roles {sorted(fable)} != the roles whose minimum model "
                 f"is fable (fable minimums {minimums})")
    else:
        res.passed("dispatch.config.fable_roles")
    values = [rank.get(m) for m in RANK_ORDER]
    if all(isinstance(v, int) and not isinstance(v, bool) for v in values) \
            and all(a < b for a, b in zip(values, values[1:])):
        res.passed("dispatch.config.model_rank")
    else:
        res.fail("dispatch.config.model_rank", f"must order {' < '.join(RANK_ORDER)} strictly with integer "
                 f"ranks; got {', '.join(f'{m}={rank.get(m)!r}' for m in RANK_ORDER)}")
    cap = conf["dispatch"]["max_prompt_chars"]
    if isinstance(cap, int) and not isinstance(cap, bool) and cap > 0:
        res.passed("dispatch.config.max_prompt_chars")
    else:
        res.fail("dispatch.config.max_prompt_chars", f"must be a positive integer, got {cap!r}")
    err = fallback_error(conf)
    if err:
        res.fail("dispatch.config.fable_fallback", err)
    elif "fable_fallback" in conf:
        res.passed("dispatch.config.fable_fallback", f"{conf['fable_fallback']}: the fable_roles run on "
                   f"{conf['fable_fallback']} while Fable is unavailable (DEC-0018)")
    else:
        res.passed("dispatch.config.fable_fallback", "absent: the fallback is off (DEC-0018)")


def check(args: list) -> Result:
    p = gate_args("gate_dispatch")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--prompt")
    mode.add_argument("--config-check", action="store_true")
    p.add_argument("--tool-model")
    p.add_argument("--subagent-type")
    p.add_argument("--prompts-dir", default=DEFAULT_PROMPTS)
    a = p.parse_args(args)
    root = a.root or repo_root()
    conf = gconfig.load(CODE_ROOT)
    res = Result("dispatch")
    if a.config_check:
        check_config(conf, res)
        return res
    check_prompt(root, conf, read_prompt(a.prompt), a.tool_model, a.prompts_dir, res, a.subagent_type)
    return res


def main(argv=None):
    return run_gate("dispatch", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_gate_dispatch -v`
Expected: `OK` (38 tests at Task 1's commit; the Step 2 and Step 4 blocks show the tip files after Task 3 and the b4 fix wave B4-FIX-1, where the module has 62 tests; the b5 fix wave B5-FIX-2 brings it to 75). Then `python3 tools/gates/run_all.py` → only `PASS` lines (gate_dispatch is not yet in `GATES`; Task 3 adds it).

- [ ] **Step 6: Commit**

```bash
git add tools/gates/gate_dispatch.py tools/gates/tests/repo_fixture.py tools/gates/tests/fixtures/rows.yml \
        tools/gates/tests/fixtures/dispatch tools/gates/tests/test_gate_dispatch.py
git commit -m "feat(gates): gate_dispatch -- dispatch grammar, model map, register ids, fresh claims, strict block byte-equal"
```

- [ ] **Step 7: Mutation run and pin**

```bash
T=tools.gates.tests.test_gate_dispatch.GateDispatch.test_fable_on_implementer_fails
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/if model == "fable" and role not in conf\["fable_roles"\]:/if False:/' tools/gates/gate_dispatch.py
git diff --stat                                                            # expect 1 file, 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/gate_dispatch.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T1/mutation-SD-S2-T1.txt` (the package copy) with exactly the observed verdicts:
```
finding: SD-S2-T1
test: test_fable_on_implementer_fails
step 1 (fix present): python3 -m unittest tools.gates.tests.test_gate_dispatch.GateDispatch.test_fable_on_implementer_fails -> PASS
step 2 (fix reverted: perl replaces the fable_roles condition in tools/gates/gate_dispatch.py with False): python3 -m unittest tools.gates.tests.test_gate_dispatch.GateDispatch.test_fable_on_implementer_fails -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_gate_dispatch.GateDispatch.test_fable_on_implementer_fails -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T1.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T1.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T1/mutation-SD-S2-T1.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T1.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T1.md
echo "- S2-T1 DONE <task head sha> gate_dispatch; review docs/superpowers/state/strict-delivery/reviews/S2-T1.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T1"
PIN = {"test": "test_gate_dispatch::test_fable_on_implementer_fails",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T1.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T1.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T1.txt docs/superpowers/state/strict-delivery/reviews/S2-T1.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T1 pinned; ledger S2-T1"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 8: Ledger (controller, after the task review is ✅)** — the line `- S2-T1 DONE <task head sha> gate_dispatch; review docs/superpowers/state/strict-delivery/reviews/S2-T1.md` goes to `docs/superpowers/state/strict-delivery/ledger.md` in the pin commit above (`chore(state): SD-S2-T1 pinned; ledger S2-T1`); no separate ledger commit.

### Task 2: PreToolUse Agent branch

**Files:**
- Modify: `tools/gates/hooks/pre_tool_use.py` (S1 file: add import tempfile, handle_agent, route tool_name == "Agent" to it, rewrite the module docstring's tool sentence)
- Modify: `tools/gates/README.md` (S1 file: Hooks section, the Agent branch)
- Test: `tools/gates/tests/test_hooks.py` (S1 file: repo_fixture import, hook() takes extra environment variables, append class AgentDispatchHook, update the Agent case of PreToolUse.test_ungated_commands_and_tools_pass_even_on_a_failing_register)

**Findings:** SD-S2-T2

**Siblings:** none -- the register row SD-S2-T2 lists no siblings

**Gates:** run_all, dispatch

**Interfaces:**
- Consumes: `gate_dispatch.check` (Task 1); S1 `run_all.run(names, {"root": ..., "<gate>": [argv]}) -> Result` for `claims` and `decisions`, `lib.run_check`, `pre_tool_use.{CODE_ROOT, Blocked, toplevel, main}`; `repo_fixture` (Task 1, incl. `docs/superpowers/plans/fixture-plan.md` in `DISPATCH_FILES`, which lets run_all's decisions argv resolve).
- Produces: `handle_agent(event: dict) -> int` in `pre_tool_use.py` (0 = allow, 2 = block, lines on stderr). Test-only environment override `SD_PROMPTS_DIR` → `--prompts-dir`. The gated tree is `toplevel(event cwd)`: missing/empty cwd → `Blocked("hook.input")`, non-repository cwd → `FAIL hook.target` (S1's `project_root` does not exist). `dispatch` runs directly via `run_check("dispatch", gate_dispatch.check, ...)`, not through `run_all.run`, because `run_all.GATES` gains `dispatch` only in Task 3.

- [ ] **Step 1: Write the failing tests** — in `tools/gates/tests/test_hooks.py`: add `from tools.gates.tests import repo_fixture as rf` after the `gitfixture` import; change `hook()` to `def hook(script, event, hooks_dir=HOOKS, **extra_env):` with `env = isolated_env(**extra_env)`; in `PreToolUse.test_ungated_commands_and_tools_pass_even_on_a_failing_register` replace the Agent assertion `self.assertEqual((0, "", ""), (p.returncode, p.stdout, p.stderr))` by
```python
        p = hook("pre_tool_use.py", agent)    # the Agent branch runs dispatch, claims, decisions -- never register
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL dispatch.grammar", p.stderr)
        self.assertNotIn("register.", p.stderr)
```
and append above the `if __name__` line:

```python
DISPATCH_FIX = os.path.join(rf.FIX, "dispatch")


def agent_event(cwd, prompt, model="sonnet", subagent_type="general-purpose"):
    tool_input = {"prompt": prompt, "description": "fixture"}
    if subagent_type is not None:
        tool_input["subagent_type"] = subagent_type
    if model is not None:
        tool_input["model"] = model
    event = {"session_id": "fx", "transcript_path": "/dev/null", "permission_mode": "default",
             "hook_event_name": "PreToolUse", "tool_name": "Agent", "tool_input": tool_input, "tool_use_id": "fx-1"}
    if cwd is not None:
        event["cwd"] = cwd
    return event


class AgentDispatchHook(unittest.TestCase):
    """PreToolUse Agent branch (S2 Task 2), through the hook as a subprocess."""

    @classmethod
    def setUpClass(cls):
        rows = rf.load_rows()
        rows["decisions"] = [d for d in rows["decisions"] if d["id"] == "DEC-0001"]
        clean = dict(rows, claims=[c for c in rows["claims"] if c["id"] == "C-fx.ok"])
        stale = dict(rows, claims=[c for c in rows["claims"] if c["id"] in ("C-fx.ok", "C-fx.stale")])
        cls.root = rf.make_repo(rf.DISPATCH_FILES, clean)
        cls.stale_root = rf.make_repo(rf.DISPATCH_FILES, stale)
        cls.dup_root = rf.make_repo(rf.DISPATCH_FILES, dict(clean, claims=2 * [c for c in rows["claims"] if c["id"] == "C-fx.ok"]))
        cls.open_root = rf.make_repo(rf.DISPATCH_FILES, dict(rf.load_rows(), claims=[c for c in rows["claims"] if c["id"] == "C-fx.ok"]))
        with open(os.path.join(DISPATCH_FIX, "good_implementer.md"), encoding="utf-8", newline="") as f:
            cls.good = f.read()

    @classmethod
    def tearDownClass(cls):
        gf.remove(cls.root)
        gf.remove(cls.stale_root)
        gf.remove(cls.dup_root)
        gf.remove(cls.open_root)

    def run_agent(self, root, prompt, model="sonnet", cwd=True, subagent_type="general-purpose"):
        event = agent_event(root if cwd is True else cwd, prompt, model, subagent_type)
        return hook("pre_tool_use.py", event, SD_PROMPTS_DIR=os.path.join(DISPATCH_FIX, "prompts"))

    def test_good_dispatch_passes(self):
        p = self.run_agent(self.root, self.good)
        self.assertEqual((0, ""), (p.returncode, p.stderr))

    def test_agent_dispatch_blocked_on_stale_claim(self):
        prompt = self.good.replace("## Claims: C-fx.ok", "## Claims: C-fx.stale")
        p = self.run_agent(self.stale_root, prompt)
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("dispatch.claims.C-fx.stale", p.stderr)

    def test_claims_gate_runs_on_the_tree(self):
        p = self.run_agent(self.dup_root, self.good)
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL claims.duplicate.C-fx.ok", p.stderr)

    def test_prompt_is_scanned_for_open_decisions(self):
        p = self.run_agent(self.open_root, self.good + "\nsee DEC-0002\n")
        self.assertEqual(2, p.returncode, p.stderr)
        lines = [l for l in p.stderr.splitlines() if l.startswith("FAIL decisions.open-ref.DEC-0002")]
        self.assertEqual(1, len(lines), p.stderr)
        self.assertTrue(lines[0].startswith("FAIL decisions.open-ref.DEC-0002: <tool_input.prompt>:"), lines[0])
        self.assertNotIn("sd-dispatch-", p.stderr)

    def test_tool_model_disagreeing_with_model_line_blocks(self):
        p = self.run_agent(self.root, self.good, model="opus")
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("model.agree", p.stderr)

    def test_missing_tool_model_blocks(self):
        p = self.run_agent(self.root, self.good, model=None)
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("model.agree", p.stderr)

    def test_fork_dispatch_blocks(self):
        """Ruling 4 (B4-FIX-1): a fork ignores tool_input.model and inherits the session context."""
        p = self.run_agent(self.root, self.good, subagent_type="fork")
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL dispatch.fork: ", p.stderr)
        self.assertIn("session model", p.stderr)

    def test_other_or_missing_subagent_types_pass(self):
        for value in ("general-purpose", "Explore", None):
            p = self.run_agent(self.root, self.good, subagent_type=value)
            self.assertEqual((0, ""), (p.returncode, p.stderr), value)

    def test_a_non_string_subagent_type_fails_closed(self):
        p = self.run_agent(self.root, self.good, subagent_type=["fork"])
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL dispatch.fork: ", p.stderr)

    def run_final_verifier(self, fallback, model="opus"):
        """A final_verifier dispatch on `model` through a copy of tools/gates whose config.yml sets
        fable_fallback to `fallback` (None: absent); the hook reads config from its own checkout."""
        code = tempfile.mkdtemp(prefix="gate-code-")
        self.addCleanup(gf.remove, code)
        copy_gates(code)
        gf.write(code, "tools/gates/config.yml", rf.config_text(fallback))
        prompts = tempfile.mkdtemp(prefix="gate-prompts-")
        self.addCleanup(gf.remove, prompts)
        shutil.copyfile(os.path.join(DISPATCH_FIX, "prompts", "implementer.md"), os.path.join(prompts, "verifier.md"))
        prompt = self.good
        for old, new in (("## Role: implementer\n", "## Role: final_verifier\n"),
                         ("## Budget: tokens=150000 minutes=60\n", "## Budget: tokens=100000 minutes=60\n"),
                         ("## Model: sonnet\n", f"## Model: {model}\n")):
            self.assertIn(old, prompt)
            prompt = prompt.replace(old, new, 1)
        event = agent_event(self.root, prompt, model)
        return hook("pre_tool_use.py", event, hooks_dir=os.path.join(code, "tools", "gates", "hooks"),
                    SD_PROMPTS_DIR=prompts)

    def test_final_verifier_on_opus_passes_while_the_fallback_is_on(self):
        """DEC-0018 (B5-FIX-2): fable_fallback: opus lets the live hook admit an opus final_verifier."""
        p = self.run_final_verifier("opus")
        self.assertEqual((0, ""), (p.returncode, p.stderr))
        p = self.run_final_verifier("opus", model="fable")
        self.assertEqual((0, ""), (p.returncode, p.stderr))

    def test_final_verifier_on_opus_blocks_while_the_fallback_is_off(self):
        p = self.run_final_verifier(None)
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL dispatch.model.rank: role final_verifier needs at least fable, got opus", p.stderr)
        p = self.run_final_verifier("opus", model="sonnet")
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL dispatch.model.rank: role final_verifier needs at least fable, got sonnet", p.stderr)

    def test_empty_prompt_blocks(self):
        p = self.run_agent(self.root, "   ")
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("dispatch.input", p.stderr)

    def test_fable_implementer_blocks_even_when_both_lines_agree(self):
        prompt = self.good.replace("## Model: sonnet", "## Model: fable")
        p = self.run_agent(self.root, prompt, model="fable")
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("model.fable", p.stderr)

    def test_missing_or_empty_cwd_fails_closed(self):
        for cwd in (None, ""):
            p = self.run_agent(self.root, self.good, cwd=cwd)
            self.assertEqual(2, p.returncode, (cwd, p.stderr))
            self.assertTrue(p.stderr.startswith("FAIL hook.input: "), (cwd, p.stderr))

    def test_cwd_outside_a_repository_is_hook_target(self):
        outside = tempfile.mkdtemp(prefix="gate-test-outside-")
        self.addCleanup(gf.remove, outside)
        p = self.run_agent(self.root, self.good, cwd=outside)
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertTrue(p.stderr.startswith("FAIL hook.target: %s: " % outside), p.stderr)
```

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_hooks.AgentDispatchHook -v`
Expected: `test_good_dispatch_passes` passes (S1 lets `Agent` through); the other nine new tests and the updated S1 case FAIL (`AssertionError: 2 != 0`, or `startswith` for the cwd cases). The two DEC-0018 tests the fix wave added (B5-FIX-2) split the same way: `test_final_verifier_on_opus_passes_while_the_fallback_is_on` passes, `test_final_verifier_on_opus_blocks_while_the_fallback_is_off` FAILs (`AssertionError: 2 != 0`).

- [ ] **Step 3: Implement** — in `tools/gates/hooks/pre_tool_use.py` add `import tempfile` after `import sys`, add the module constant `PROMPT_LABEL = "<tool_input.prompt>"` above `class Blocked` (a blank line before it), rewrite the module docstring's tool sentence (Bash and Agent branches), add this function above `main()`:

```python
def handle_agent(event: dict) -> int:
    """PreToolUse Agent: the dispatch prompt must pass gate_dispatch (which also checks the claims it
    names), gate_claims and gate_decisions in the tree of the event's cwd (all three through
    run_all.run; the dispatch argv given here replaces run_all.DEFAULT_ARGV's --config-check).
    tool_input.model must be set and equal the prompt's '## Model:' line (umbrella dispatch
    grammar); tool_input.subagent_type, when present, goes to gate_dispatch --subagent-type, which
    refuses a fork (ruling 4). 0 = allow, 2 = block."""
    cwd = event.get("cwd")
    if not isinstance(cwd, str) or not cwd:
        raise Blocked("hook.input", "the event has no cwd")
    root = toplevel(cwd)
    tool_input = event.get("tool_input")
    tool_input = tool_input if isinstance(tool_input, dict) else {}
    prompt = tool_input.get("prompt")
    if not isinstance(prompt, str) or not prompt.strip():
        print("FAIL dispatch.input: Agent tool_input.prompt is missing or empty", file=sys.stderr)
        return 2
    model = tool_input.get("model")
    subagent_type = tool_input.get("subagent_type")
    if subagent_type is not None and not isinstance(subagent_type, str):
        print(f"FAIL dispatch.fork: tool_input.subagent_type {subagent_type!r} is not a string", file=sys.stderr)
        return 2
    sys.path.insert(0, CODE_ROOT)
    from tools.gates import run_all   # lazy, as in main()
    fd, path = tempfile.mkstemp(prefix="sd-dispatch-", suffix=".md")
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as f:
            f.write(prompt)
        dispatch = ["--prompt", path, "--tool-model", model if isinstance(model, str) else ""]
        if subagent_type is not None:
            dispatch.append(f"--subagent-type={subagent_type}")    # "=": a value may start with "-"
        prompts = os.environ.get("SD_PROMPTS_DIR")    # test override; unset in real sessions
        if prompts:
            dispatch += ["--prompts-dir", prompts]
        res = run_all.run(["dispatch", "claims", "decisions"],
                          {"root": root, "dispatch": dispatch, "decisions": ["--scan", path]})
    finally:
        os.unlink(path)
    if res.ok():
        return 0
    # the temporary file is gone when the reader sees a reason: name the prompt, not the path
    forms = {path, os.path.realpath(path), os.path.relpath(path, root), os.path.relpath(os.path.realpath(path), root)}
    for line in res.lines:
        for form in sorted(forms, key=len, reverse=True):
            line.reason = line.reason.replace(form, PROMPT_LABEL)
    print(res.render(False), file=sys.stderr)
    return 2
```
and in `main()`, directly after the `if not isinstance(event, dict): raise ...` check and before `command = bash_command(event)`, insert:
```python
    if event.get("tool_name") == "Agent":
        return handle_agent(event)
```
In `tools/gates/README.md` replace the sentence "the `Agent` branch (the dispatch gate) arrives with S2." by the description of the shipped Agent branch (see the file). The S1 fail-closed wrapper (`except Exception` → exit 2) already surrounds the dispatch.

- [ ] **Step 4: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_hooks -v`
Expected: `OK` (60 S1 hook tests, one updated, and the ten new ones: 70; the block above is the tip form, which also holds the three tests of the b4 fix wave B4-FIX-1 and the two of the b5 fix wave B5-FIX-2: 75). Then `python3 tools/gates/run_all.py` → only `PASS`.

- [ ] **Step 5: Commit**

```bash
git add tools/gates/hooks/pre_tool_use.py tools/gates/tests/test_hooks.py tools/gates/README.md
git commit -m "feat(hooks): PreToolUse Agent branch -- every dispatch through gate_dispatch, claims, decisions; tool model must equal the Model line"
```

- [ ] **Step 6: Mutation run and pin**

```bash
T=tools.gates.tests.test_hooks.AgentDispatchHook.test_missing_tool_model_blocks
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/argv = \["--prompt", path, "--tool-model", model if isinstance\(model, str\) else ""\]/argv = ["--prompt", path]/' tools/gates/hooks/pre_tool_use.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/hooks/pre_tool_use.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T2/mutation-SD-S2-T2.txt` (the package copy):
```
finding: SD-S2-T2
test: test_missing_tool_model_blocks
step 1 (fix present): python3 -m unittest tools.gates.tests.test_hooks.AgentDispatchHook.test_missing_tool_model_blocks -> PASS
step 2 (fix reverted: perl drops the --tool-model argument in tools/gates/hooks/pre_tool_use.py): python3 -m unittest tools.gates.tests.test_hooks.AgentDispatchHook.test_missing_tool_model_blocks -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_hooks.AgentDispatchHook.test_missing_tool_model_blocks -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T2.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T2.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T2/mutation-SD-S2-T2.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T2.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T2.md
echo "- S2-T2 DONE <task head sha> Agent hook branch; review docs/superpowers/state/strict-delivery/reviews/S2-T2.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T2"
PIN = {"test": "test_hooks::test_missing_tool_model_blocks",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T2.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T2.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T2.txt docs/superpowers/state/strict-delivery/reviews/S2-T2.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T2 pinned; ledger S2-T2"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 7: Ledger (controller, after the task review is ✅)** — the line `- S2-T2 DONE <task head sha> Agent hook branch; review docs/superpowers/state/strict-delivery/reviews/S2-T2.md` goes to `docs/superpowers/state/strict-delivery/ledger.md` in the pin commit above (`chore(state): SD-S2-T2 pinned; ledger S2-T2`); no separate ledger commit. From here every dispatch in this checkout is gated.

### Task 3: dispatch config check, run_all registration, README, DEC-0010 pattern, hook on run_all

**Files:**
- Modify: `tools/gates/gate_dispatch.py` (check() parser; add check_config; docstring usage)
- Modify: `tools/gates/gate_decisions.py` (b2 file: REF, owner ruling DEC-0010)
- Modify: `tools/gates/run_all.py` (S1 file: GATES, DEFAULT_ARGV, gate_argv)
- Modify: `tools/gates/hooks/pre_tool_use.py` (handle_agent runs dispatch through run_all.run)
- Modify: `tools/gates/README.md` (S1 file: table row + the gate_dispatch section)
- Modify: `.github/workflows/gates.yml` (no change: the job uses unittest discover, verified with grep -n unittest .github/workflows/gates.yml)
- Test: `tools/gates/tests/test_gate_dispatch.py` (append class ConfigCheck; two DEC-0010 tests in GateDispatch)
- Test: `tools/gates/tests/test_gate_decisions.py` (the longer-id test inverted, two siblings)
- Test: `tools/gates/tests/test_run_all.py` (GATES pin and the unknown-gate line)

**Findings:** SD-S2-T3

**Siblings:** none -- the register row SD-S2-T3 lists no siblings

**Gates:** run_all, dispatch

**Interfaces:**
- Consumes: Task 1 `gate_dispatch`; Task 2 `handle_agent`; S1 `run_all`.
- Produces: CLI mode `gate_dispatch.py --config-check` (line ids `dispatch.config.<role>`, `dispatch.config.models`, `dispatch.config.fable_roles`, `dispatch.config.max_prompt_chars`, and from the b4 fix wave (DEC-0012 = require-equal) `dispatch.config.model_rank`, with `dispatch.config.fable_roles` also requiring equality with the fable-minimum roles, and from the b5 fix wave (DEC-0018, B5-FIX-2) `dispatch.config.fable_fallback`; exclusive with `--prompt`); `run_all.GATES == ["register", "claims", "decisions", "dispatch"]`; `run_all.DEFAULT_ARGV == {"dispatch": ["--config-check"]}`; `gate_decisions.REF == re.compile(r"DEC-[0-9]{4}")` (owner decision DEC-0010, the umbrella pattern: four ASCII digits, no lookahead, so `DEC-00021` cites `DEC-0002`; `gate_dispatch` consumes it as `DEC_RE`); `handle_agent` runs `["dispatch", "claims", "decisions"]` through `run_all.run` (owner decisions DEC-0009 keep `BRIEF_ROLES`, DEC-0011 follow the spec: no code change).
- Item A (Task 1 review F2): `check()` has no `--json` option (`run_gate` reads `--json` only as argv[0]; `test_json_flag_after_other_arguments_fails` pins it), so the docstring usage reads `Usage: gate_dispatch.py [--json] --prompt P|- [--tool-model M] [--prompts-dir D] | [--json] --config-check` (B4-FIX-1 later adds `[--subagent-type T]`).

- [ ] **Step 1: Write the failing tests**
  1. Append to `tools/gates/tests/test_gate_dispatch.py` above the `if __name__` line:

```python
class ConfigCheck(unittest.TestCase):
    def with_config(self, edit):
        """Config is read from CODE_ROOT (S1), so a bad config is injected at config.load."""
        conf = config.load(rf.REAL)
        edit(conf)
        return mock.patch("tools.gates.lib.config.load", return_value=conf)

    def test_config_check_passes_on_repo_config(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.implementer", out)

    def test_config_check_fails_when_implementer_minimum_is_fable(self):
        with self.with_config(lambda c: c["models"].__setitem__("implementer", "fable")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.implementer", out)

    def test_config_check_fails_on_an_unknown_role(self):
        with self.with_config(lambda c: c["models"].__setitem__("helper", "opus")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.models", out)

    def test_config_check_fails_on_a_non_positive_cap(self):
        with self.with_config(lambda c: c["dispatch"].__setitem__("max_prompt_chars", 0)):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.max_prompt_chars", out)

    def test_config_check_fails_on_a_cap_that_is_not_an_integer(self):
        for bad in (True, 1.5, "12000"):
            with self.subTest(cap=bad), \
                    self.with_config(lambda c, b=bad: c["dispatch"].__setitem__("max_prompt_chars", b)):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.max_prompt_chars", out)

    def test_config_check_fails_on_a_minimum_that_is_not_in_model_rank(self):
        with self.with_config(lambda c: c["models"].__setitem__("implementer", "gpt")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.implementer: minimum model 'gpt' not in model_rank", out)

    def test_config_check_fails_on_a_role_with_no_minimum(self):
        with self.with_config(lambda c: c["models"].pop("investigator")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.investigator: minimum model None not in model_rank", out)

    def test_config_check_fails_on_an_unknown_fable_role(self):
        with self.with_config(lambda c: c["fable_roles"].append("helper")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.fable_roles: unknown roles: helper", out)

    def test_config_check_fails_when_fable_roles_differ_from_the_fable_minimums(self):
        """DEC-0012 = require-equal: fable_roles is exactly the roles whose minimum model is fable."""
        for name, edit in (("extra", lambda c: c["fable_roles"].append("implementer")),
                           ("missing", lambda c: c["fable_roles"].remove("exception_reviewer"))):
            with self.subTest(name), self.with_config(edit):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.fable_roles: ", out)
                self.assertIn("fable minimums ['exception_reviewer', 'final_verifier']", out)

    def test_config_check_names_both_fable_sets(self):
        with self.with_config(lambda c: c["fable_roles"].append("implementer")):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_roles: fable_roles ['exception_reviewer', 'final_verifier',"
                      " 'implementer'] != the roles whose minimum model is fable"
                      " (fable minimums ['exception_reviewer', 'final_verifier'])", out)

    def test_config_check_passes_model_rank_on_repo_config(self):
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.model_rank\n", out)
        self.assertIn("PASS dispatch.config.fable_roles\n", out)

    def test_config_check_fails_when_model_rank_is_not_strictly_ordered(self):
        bad = ({"haiku": 1, "sonnet": 2, "opus": 5, "fable": 4}, {"haiku": 1, "sonnet": 2, "opus": 3, "fable": 3},
               {"haiku": 2, "sonnet": 1, "opus": 3, "fable": 4}, {"haiku": 1, "sonnet": 2, "opus": 3},
               {"haiku": 1, "sonnet": 2, "opus": 3, "fable": "4"}, {"haiku": 1, "sonnet": 2, "opus": 3, "fable": True},
               {"haiku": True, "sonnet": 2, "opus": 3, "fable": 4})
        for rank in bad:
            with self.subTest(rank=rank), self.with_config(lambda c, r=rank: c.__setitem__("model_rank", dict(r))):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.model_rank: must order haiku < sonnet < opus < fable", out)

    def test_config_check_fails_when_a_role_has_no_prompt_file(self):
        patched = {k: v for k, v in gate_dispatch.ROLE_PROMPT.items() if k != "investigator"}
        with mock.patch.object(gate_dispatch, "ROLE_PROMPT", patched):
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.config.investigator: no prompt file mapped for the role", out)

    def test_neither_prompt_nor_config_check_is_a_bad_argument(self):
        rc, out = gate(rf.REAL, [])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.input: bad arguments", out)

    def test_prompt_and_config_check_are_exclusive(self):
        rc, out = gate(rf.REAL, ["--config-check", "--prompt", "x.md"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL dispatch.input", out)

    # -- DEC-0018: the fable_fallback key (B5-FIX-2)
    def test_config_check_passes_the_repo_fallback(self):
        """The real config passes in whichever state the owner's switch is in (R1-1): set, its PASS line names
        the value; absent, the off line."""
        rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        conf = config.load(rf.REAL)
        if "fable_fallback" in conf:
            v = conf["fable_fallback"]
            want = f"PASS dispatch.config.fable_fallback: {v}: the fable_roles run on {v} while Fable is unavailable (DEC-0018)"
        else:
            want = "PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)"
        self.assertIn(want + "\n", out + "\n")
        self.assertEqual(15, len(out.splitlines()), out)

    def test_config_text_sets_the_key_whether_or_not_the_repo_has_it(self):
        """R1-1: the fixtures set or remove the key themselves, so the suite does not depend on the repo's switch.
        A copy of the real config with the key cleared (and one with it set) gives the same fixture configs."""
        with open(os.path.join(rf.REAL, "tools", "gates", "config.yml"), encoding="utf-8") as f:
            real = f.read()
        bare = rf.FALLBACK_LINE.sub("", real)
        self.assertNotIn("\nfable_fallback:", "\n" + bare)
        for state, text in (("cleared", bare), ("set", bare + "fable_fallback: opus\n")):
            d = tempfile.mkdtemp(prefix="gate-cfgsrc-")
            self.addCleanup(shutil.rmtree, d, True)
            rf.write(d, "tools/gates/config.yml", text)
            with self.subTest(state), mock.patch.object(rf, "REAL", d):
                self.assertEqual(bare, rf.config_text(None))
                self.assertEqual(bare + "fable_fallback: sonnet\n", rf.config_text("sonnet"))
                with config_with_fallback(None) as load:
                    self.assertNotIn("fable_fallback", load.return_value)
                    rc, out = gate(rf.REAL, ["--config-check"])
                self.assertIn("PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)", out)
                with config_with_fallback("opus") as load:
                    self.assertEqual("opus", load.return_value["fable_fallback"])
        d = tempfile.mkdtemp(prefix="gate-cfgsrc-")
        self.addCleanup(shutil.rmtree, d, True)
        rf.write(d, "tools/gates/config.yml", bare + "fable_fallback: opus\nfable_fallback: opus\n")
        with mock.patch.object(rf, "REAL", d), self.assertRaisesRegex(AssertionError, "holds 2 fable_fallback lines"):
            rf.config_text(None)

    def test_config_check_passes_an_absent_fallback(self):
        with config_with_fallback(None) as load:
            self.assertNotIn("fable_fallback", load.return_value)
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS dispatch.config.fable_fallback: absent: the fallback is off (DEC-0018)", out)
        self.assertEqual(15, len(out.splitlines()), out)

    def test_config_check_fails_an_invalid_fallback(self):
        for value in ("sonnet", "haiku", "fable", "gpt", "null", "[opus]", "3", "Opus"):
            with self.subTest(value), config_with_fallback(value):
                rc, out = gate(rf.REAL, ["--config-check"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback ", out)
                self.assertIn("must be a model_rank model ranked at least opus and below fable", out)
                self.assertNotIn("PASS dispatch.config.fable_fallback", out)

    def test_config_check_ranks_the_fallback_by_model_rank(self):
        """A model_rank that ranks sonnet with opus admits sonnet; one that ranks opus with fable admits nothing."""
        with config_with_fallback("sonnet") as load:
            load.return_value["model_rank"] = {"haiku": 1, "sonnet": 3, "opus": 3, "fable": 4}
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("PASS dispatch.config.fable_fallback: sonnet: ", out)
        with config_with_fallback("opus") as load:
            load.return_value["model_rank"] = {"haiku": 1, "sonnet": 2, "opus": 4, "fable": 4}
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback 'opus' ", out)
        with config_with_fallback("opus") as load:    # no fable rank: fail closed, no KeyError
            load.return_value["model_rank"].pop("fable")
            rc, out = gate(rf.REAL, ["--config-check"])
        self.assertIn("FAIL dispatch.config.fable_fallback: fable_fallback 'opus' ", out)

    def test_run_all_runs_dispatch_in_config_check_mode(self):
        from tools.gates import run_all
        self.assertIn("dispatch", run_all.GATES)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": rf.REAL}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "dispatch"])
        self.assertEqual(0, rc, buf.getvalue())
        self.assertIn("dispatch.config.implementer", buf.getvalue())
```
  2. In `GateDispatch` (same file), insert directly above `test_answered_decision_passes`:

```python
    def test_a_five_digit_text_is_a_mention_of_its_first_four_digits(self):
        """DEC-0010 (umbrella pattern DEC-[0-9]{4}, no lookahead): DEC-00021 cites DEC-0002."""
        rc, out = self.dispatch(GOOD + "Scope follows DEC-00021.\n")
        self.assertFailLine(rc, out, "dispatch.decisions.DEC-0002")

    def test_non_ascii_digits_never_form_an_id(self):
        rc, out = self.dispatch(GOOD + "Scope follows DEC-\uff10\uff10\uff10\uff12 (fullwidth).\n")
        self.assertEqual(0, rc, out)
        self.assertNotIn("dispatch.decisions.DEC-", out.replace("PASS dispatch.decisions\n", ""))
```
  3. In `tools/gates/tests/test_gate_decisions.py` replace `test_a_longer_id_is_not_a_reference_to_a_shorter_one` (it pinned the lookahead behaviour DEC-0010 reverses) by:

```python
    def test_a_five_digit_text_is_a_mention_of_its_first_four_digits(self):
        """DEC-0010 (umbrella pattern DEC-[0-9]{4}, no lookahead): DEC-90012 cites DEC-9001."""
        gf.write(self.root, "docs/plan.md", "See DEC-90012 for the long form.\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/plan.md:1 references unanswered decision DEC-9001", out)
        self.assertEqual(1, rc)

    def test_a_non_ascii_digit_after_four_ascii_digits_is_still_a_mention(self):
        gf.write(self.root, "docs/plan.md", "See DEC-9001\u0663 (Arabic-Indic three).\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.open-ref.DEC-9001", out)
        self.assertEqual(1, rc)

    def test_non_ascii_digits_never_form_an_id(self):
        """The scan pattern itself is the observable: an id formed from non-ASCII digits can never equal
        an open (ASCII) id, so the gate's verdict cannot show it; REF must not match such text at all."""
        for text in ("DEC-\uff19\uff10\uff10\uff11", "DEC-\u0669\u0660\u0660\u0661"):
            self.assertEqual([], gate_decisions.REF.findall(f"Non-ASCII {text} is not an id."), text)
        gf.write(self.root, "docs/plan.md", "Fullwidth DEC-\uff19\uff10\uff10\uff11 is not an id.\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertEqual(PASS_ALL, out)
        self.assertEqual(0, rc)
```
  4. In `tools/gates/tests/test_run_all.py`: `test_gates_at_this_block` asserts `["register", "claims", "decisions", "dispatch"]`, and `test_cli_unknown_gate_is_rc_1` expects `(installed: register, claims, decisions, dispatch)`.

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_dispatch tools.gates.tests.test_gate_decisions tools.gates.tests.test_run_all`
Expected: FAIL — `--config-check` is an unknown argument (`FAIL dispatch.input`), `dispatch` not in `GATES`, `DEC-00021` and `DEC-9001` + U+0663 not read as mentions, fullwidth digits read as an unknown id (`FAIL dispatch.decisions.DEC-０００２`).

- [ ] **Step 3: Implement the mode** — in `tools/gates/gate_dispatch.py`, add above `check()`:

```python
def check_config(conf: dict, res: Result) -> None:
    """CI mode: the model map, fable_roles, model_rank, the prompt cap and the optional fable_fallback agree with
    the dispatch grammar (DEC-0012: fable_roles equals the fable-minimum roles; model_rank orders
    haiku < sonnet < opus < fable; DEC-0018: fable_fallback, when present, ranks at least opus and below fable)."""
    models, rank, fable = conf["models"], conf["model_rank"], set(conf["fable_roles"])
    for role in ROLES:
        need = models.get(role)
        if need not in rank:
            res.fail(f"dispatch.config.{role}", f"minimum model {need!r} not in model_rank")
        elif need == "fable" and role not in fable:
            res.fail(f"dispatch.config.{role}", "minimum model fable but the role is not in fable_roles")
        elif ROLE_PROMPT.get(role) is None:
            res.fail(f"dispatch.config.{role}", "no prompt file mapped for the role")
        else:
            res.passed(f"dispatch.config.{role}")
    extra = sorted(set(models) - set(ROLES))
    if extra:
        res.fail("dispatch.config.models", f"roles outside the dispatch grammar: {', '.join(extra)}")
    else:
        res.passed("dispatch.config.models")
    stray = sorted(fable - set(ROLES))
    minimums = sorted(r for r in ROLES if models.get(r) == "fable")
    if stray:
        res.fail("dispatch.config.fable_roles", f"unknown roles: {', '.join(stray)}")
    elif sorted(fable) != minimums:    # DEC-0012 = require-equal
        res.fail("dispatch.config.fable_roles", f"fable_roles {sorted(fable)} != the roles whose minimum model "
                 f"is fable (fable minimums {minimums})")
    else:
        res.passed("dispatch.config.fable_roles")
    values = [rank.get(m) for m in RANK_ORDER]
    if all(isinstance(v, int) and not isinstance(v, bool) for v in values) \
            and all(a < b for a, b in zip(values, values[1:])):
        res.passed("dispatch.config.model_rank")
    else:
        res.fail("dispatch.config.model_rank", f"must order {' < '.join(RANK_ORDER)} strictly with integer "
                 f"ranks; got {', '.join(f'{m}={rank.get(m)!r}' for m in RANK_ORDER)}")
    cap = conf["dispatch"]["max_prompt_chars"]
    if isinstance(cap, int) and not isinstance(cap, bool) and cap > 0:
        res.passed("dispatch.config.max_prompt_chars")
    else:
        res.fail("dispatch.config.max_prompt_chars", f"must be a positive integer, got {cap!r}")
    err = fallback_error(conf)
    if err:
        res.fail("dispatch.config.fable_fallback", err)
    elif "fable_fallback" in conf:
        res.passed("dispatch.config.fable_fallback", f"{conf['fable_fallback']}: the fable_roles run on "
                   f"{conf['fable_fallback']} while Fable is unavailable (DEC-0018)")
    else:
        res.passed("dispatch.config.fable_fallback", "absent: the fallback is off (DEC-0018)")
```
and replace the body of `check()` with:
```python
def check(args: list) -> Result:
    p = gate_args("gate_dispatch")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--prompt")
    mode.add_argument("--config-check", action="store_true")
    p.add_argument("--tool-model")
    p.add_argument("--subagent-type")
    p.add_argument("--prompts-dir", default=DEFAULT_PROMPTS)
    a = p.parse_args(args)
    root = a.root or repo_root()
    conf = gconfig.load(CODE_ROOT)
    res = Result("dispatch")
    if a.config_check:
        check_config(conf, res)
        return res
    check_prompt(root, conf, read_prompt(a.prompt), a.tool_model, a.prompts_dir, res, a.subagent_type)
    return res
```
Also change the module docstring's usage line to:
`Usage: gate_dispatch.py [--json] --prompt P|- [--tool-model M] [--prompts-dir D] | [--json] --config-check`
(the b4 fix wave B4-FIX-1 later adds `[--subagent-type T]` after `[--tool-model M]` and wraps the line; the Task 1 Step 4 block shows that tip form).

- [ ] **Step 4: Register in run_all** — in `tools/gates/run_all.py` set (Tasks 4 and 6 extend the list; at the tip it names six gates):
```python
GATES = ["register", "claims", "decisions", "dispatch"]
# argv an S2+ gate gets when run() is handed nothing for it: the gate's CI-meaningful mode
DEFAULT_ARGV = {"dispatch": ["--config-check"]}
```

- [ ] **Step 5: Apply it in `gate_argv`** — replace S1's last line of `gate_argv`, `return argv + list(args.get(name, []))`, with:
```python
    return argv + (list(args[name]) if name in args else list(DEFAULT_ARGV.get(name, [])))
```
S1's three gates are not in `DEFAULT_ARGV`, so their argv is unchanged. Also reword the `gate_argv` docstring to:
```python
    """argv for one gate: --root, the gate's defaults, then args[name] (a list) verbatim, or
    DEFAULT_ARGV[name] when args has no entry for the gate (an entry replaces the default)."""
```

- [ ] **Step 5b: The DEC-0010 pattern** — in `tools/gates/gate_decisions.py` `REF` becomes `re.compile(r"DEC-[0-9]{4}")` (four ASCII digits, no lookahead; `gate_dispatch` imports it as `DEC_RE`): the Task 1/2 blocks above show the tip state, which includes this task's changes.

- [ ] **Step 5c: The Agent hook runs dispatch through run_all** — `handle_agent` hands `dispatch` its prompt argv through `run_all.run` (replacing `DEFAULT_ARGV`'s `--config-check`): the Task 1/2 blocks above show the tip state, which includes this task's changes.

- [ ] **Step 6: README** — in `tools/gates/README.md` add this row below the `gate_decisions.py` row of the "Installed gates" table:
```markdown
| `gate_dispatch.py` | `--config-check` (run_all's default): the model map, `fable_roles` (equal to the fable-minimum roles), `model_rank` (haiku < sonnet < opus < fable), the prompt cap and the optional `fable_fallback` (DEC-0018: absent = off, else a model ranked at least opus and below fable) agree with the dispatch grammar; `--prompt`: one dispatch prompt, where a `fable_roles` role on the `fable_fallback` model is `PASS dispatch.model.fallback` (see the gate_dispatch section) |
```
and append:
```markdown
## gate_dispatch (S2)

`python3 tools/gates/gate_dispatch.py --prompt P|- [--tool-model M] [--subagent-type T] [--prompts-dir D]` checks one dispatch
prompt against the umbrella's dispatch grammar: sections Role, Brief, Findings, Claims, Budget, Model, Report,
Strict block, Task in that order; model rank >= the role minimum, except that a `fable_roles` role on the model
`fable_fallback` names, while that key is set, is `PASS dispatch.model.fallback` (naming DEC-0018) in place of the
rank line (an invalid key is `FAIL dispatch.model.fallback`; a model below it still fails `dispatch.model.rank`);
fable only for `fable_roles`; a brief file
exists for the implementer, implementer_transcription, task_reviewer, final_reviewer, final_verifier and
exception_reviewer roles (`BRIEF_ROLES`; the other roles may say `none`); implementer findings
`planned`/`in-progress`; claim rows `verified` with no cited file changed since `verified_at`; report
directory exists; no unanswered `DEC-nnnn` (pattern `DEC-[0-9]{4}`, no lookahead: `DEC-00021` cites `DEC-0002`)
anywhere; length <= `dispatch.max_prompt_chars`; the strict block equals
`~/.claude/skills/strict-delivery/prompts/<file>.md` byte for byte (role -> file map: `ROLE_PROMPT`). The
PreToolUse hook runs it, with `claims` and `decisions` (never `register`), on every Agent call through
`run_all.run`, with `--tool-model <tool_input.model>` and `--subagent-type <tool_input.subagent_type>` (when
present; `fork` is `FAIL dispatch.fork`, the first line, emitted even when the grammar fails). `--config-check`
(run_all's default for `dispatch`, `run_all.DEFAULT_ARGV`) checks the model map and the cap, and, under
DEC-0012 (require-equal), that `fable_roles` equals the set of roles whose minimum model is `fable`
(`FAIL dispatch.config.fable_roles` names both sets) and that `model_rank` orders haiku < sonnet < opus < fable
strictly with integer ranks (`dispatch.config.model_rank`), and, under DEC-0018 (the owner's opus fallback for
the Fable roles while Fable is unavailable), the optional `fable_fallback` key of `tools/gates/config.yml`: absent
means off (`PASS dispatch.config.fable_fallback: absent: ...`); present, it must be a `model_rank` model ranked at
least opus and strictly below fable, else `FAIL dispatch.config.fable_fallback`; the session sets and clears it
only on the owner's word: 15 lines.
```

- [ ] **Step 7: CI** — run `grep -n "unittest" .github/workflows/gates.yml`. The job uses `discover` (lines `python3.9 -m unittest discover -v -s tools/gates/tests -t .` and the 3.12 twin): nothing to change.

- [ ] **Step 8: Run to verify they pass**

Run: `python3 -m unittest discover -s tools/gates/tests -t .` → `OK` (348 tests: 332 + 12 `ConfigCheck` + 2 `GateDispatch` DEC-0010 + 2 net `GateDecisions`); `python3 tools/gates/run_all.py` → only `PASS` (28 lines: the 15 of b3 + 13 `dispatch.config.*`). After the b4 fix wave (B4-FIX-1): 361 tests, and 29 `PASS` lines (+ `dispatch.config.model_rank`).

- [ ] **Step 9: Commit**

```bash
git add tools/gates/gate_dispatch.py tools/gates/gate_decisions.py tools/gates/run_all.py tools/gates/hooks/pre_tool_use.py tools/gates/README.md tools/gates/tests/test_gate_dispatch.py tools/gates/tests/test_gate_decisions.py tools/gates/tests/test_run_all.py docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md
git commit -m "feat(gates): gate_dispatch --config-check; dispatch joins run_all; DEC-0010 pattern"
```

- [ ] **Step 10: Mutation run and pin**

```bash
T=tools.gates.tests.test_gate_dispatch.ConfigCheck.test_config_check_fails_when_implementer_minimum_is_fable
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/elif need == "fable" and role not in fable:/elif False:/' tools/gates/gate_dispatch.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/gate_dispatch.py
python3 -m unittest $T                                                     # expect OK
```
Then the DEC-0010 pins (owner decision; the old lookahead form is the reverted fix):
```bash
B1=tools.gates.tests.test_gate_dispatch.GateDispatch.test_a_five_digit_text_is_a_mention_of_its_first_four_digits
B2=tools.gates.tests.test_gate_dispatch.GateDispatch.test_non_ascii_digits_never_form_an_id
B3=tools.gates.tests.test_gate_decisions.GateDecisions.test_a_five_digit_text_is_a_mention_of_its_first_four_digits
B4=tools.gates.tests.test_gate_decisions.GateDecisions.test_a_non_ascii_digit_after_four_ascii_digits_is_still_a_mention
python3 -m unittest $B1 $B2 $B3 $B4                                        # expect OK (4 tests)
perl -pi -e 's/^REF = re\.compile\(r"DEC-\[0-9\]\{4\}"\)/REF = re.compile(r"DEC-\\d{4}(?!\\d)")/' tools/gates/gate_decisions.py
git diff --stat                                                            # expect 1 line changed
for t in $B1 $B2 $B3 $B4; do python3 -m unittest $t; done                  # expect FAILED four times
git checkout -- tools/gates/gate_decisions.py
python3 -m unittest $B1 $B2 $B3 $B4                                        # expect OK
```
Fix round 1 adds one mutant per new test (reverting the one-line guard it pins, listed in steps 7-16 below). Write `.superpowers/sdd/strict-delivery/review/S2-T3/mutation-SD-S2-T3.txt` (the package copy):
```
finding: SD-S2-T3
test: test_config_check_fails_when_implementer_minimum_is_fable
step 1 (fix present): python3 -m unittest tools.gates.tests.test_gate_dispatch.ConfigCheck.test_config_check_fails_when_implementer_minimum_is_fable -> PASS
step 2 (fix reverted: perl replaces the fable-minimum condition in check_config with False): python3 -m unittest tools.gates.tests.test_gate_dispatch.ConfigCheck.test_config_check_fails_when_implementer_minimum_is_fable -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_gate_dispatch.ConfigCheck.test_config_check_fails_when_implementer_minimum_is_fable -> PASS
step 4 (DEC-0010 pins present): python3 -m unittest tools.gates.tests.test_gate_dispatch.GateDispatch.test_a_five_digit_text_is_a_mention_of_its_first_four_digits tools.gates.tests.test_gate_dispatch.GateDispatch.test_non_ascii_digits_never_form_an_id tools.gates.tests.test_gate_decisions.GateDecisions.test_a_five_digit_text_is_a_mention_of_its_first_four_digits tools.gates.tests.test_gate_decisions.GateDecisions.test_a_non_ascii_digit_after_four_ascii_digits_is_still_a_mention -> PASS (4 tests)
step 5 (REF reverted to the old lookahead form r"DEC-\d{4}(?!\d)" in tools/gates/gate_decisions.py, 1 line changed): the same four tests -> FAIL each (run one by one: test_gate_dispatch five-digit FAIL, test_gate_decisions five-digit FAIL, test_gate_decisions non-ascii-after-four FAIL, test_gate_dispatch non-ascii-never-form-an-id FAIL)
step 6 (REF restored: git checkout -- tools/gates/gate_decisions.py): the same four tests -> PASS (4 tests)
fix round 1 and 2 (finding ids are review 1's; each mutant is one changed line in gate_dispatch.py, run over test_gate_dispatch + test_run_all, then restored by writing the original back; counts are the observed "FAILED (failures=N)" and name the failing tests):
step 7 (new tests present: tools.gates.tests.test_gate_dispatch.ConfigCheck.test_config_check_fails_on_a_cap_that_is_not_an_integer (subTests True, 1.5, "12000"), ...fails_on_a_minimum_that_is_not_in_model_rank, ...fails_on_a_role_with_no_minimum, ...fails_on_an_unknown_fable_role, ...fails_when_a_role_has_no_prompt_file, ...test_neither_prompt_nor_config_check_is_a_bad_argument) -> PASS (suite 348 tests)
step 8 (F1: bool exclusion removed, isinstance(cap, int) and cap > 0) -> FAIL, 1 failure (subTest cap=True of test_config_check_fails_on_a_cap_that_is_not_an_integer)
step 9 (F2: 'if need not in rank:' -> 'if False:') -> FAIL, 2 failures (test_config_check_fails_on_a_minimum_that_is_not_in_model_rank, test_config_check_fails_on_a_role_with_no_minimum)
step 10 (F6: ROLE_PROMPT branch -> 'elif False:') -> FAIL, 1 failure (test_config_check_fails_when_a_role_has_no_prompt_file)
step 11 (F3: stray fable_roles -> 'stray = []') -> FAIL, 1 failure (test_config_check_fails_on_an_unknown_fable_role)
step 12 (F4: non-int cap accepted) -> 'if cap > 0:' FAIL, 3 failures (subTests cap=True, 1.5, "12000" of test_config_check_fails_on_a_cap_that_is_not_an_integer); 'if not isinstance(cap, str) and cap > 0:' FAIL, 2 failures (subTests cap=True, 1.5 of the same test)
step 13 (review 1 mutant M6, no finding: cap '>= 0') -> FAIL, 1 failure (test_config_check_fails_on_a_non_positive_cap)
step 14 (F5: mode group required=False) -> FAIL, 1 failure (test_neither_prompt_nor_config_check_is_a_bad_argument)
step 15 (review 1 mutants M2, M4, M8, M9, M11, still killed): M2 fable condition dead FAIL, 1 failure (test_config_check_fails_when_implementer_minimum_is_fable); M4 extra roles ignored FAIL, 1 failure (test_config_check_fails_on_an_unknown_role); M8 cap always passes FAIL, 4 failures (3 subTests of test_config_check_fails_on_a_cap_that_is_not_an_integer + test_config_check_fails_on_a_non_positive_cap); M9 modes not exclusive FAIL, 2 failures (test_neither_prompt_nor_config_check_is_a_bad_argument, test_prompt_and_config_check_are_exclusive); M11 --config-check ignored FAIL, 15 failures (3 of them subTests of test_config_check_fails_on_a_cap_that_is_not_an_integer)
step 16 (all restored): both modules -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T3.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T3.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T3/mutation-SD-S2-T3.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T3.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T3.md
echo "- S2-T3 DONE <task head sha> dispatch config check + run_all; review docs/superpowers/state/strict-delivery/reviews/S2-T3.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T3"
PIN = {"test": "test_gate_dispatch::test_config_check_fails_when_implementer_minimum_is_fable",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T3.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T3.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T3.txt docs/superpowers/state/strict-delivery/reviews/S2-T3.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T3 pinned; ledger S2-T3"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 11: Ledger (controller, after the task review is ✅)** — the line `- S2-T3 DONE <task head sha> dispatch config check + run_all; review docs/superpowers/state/strict-delivery/reviews/S2-T3.md` goes to `docs/superpowers/state/strict-delivery/ledger.md` in the pin commit above (`chore(state): SD-S2-T3 pinned; ledger S2-T3`); no separate ledger commit.

### b4 checkpoint

`gate_checkpoint` does not exist yet: the controller runs the umbrella's checkpoint protocol by hand, in this order (state commits ride inside the PR — repo fact in Global Constraints).

- [ ] **Step 1: CHECKPOINT b4** — run, in order, stopping on any red line:
  1. `python3 tools/gates/run_all.py` → every line `PASS`; the gate-test command from `.github/workflows/gates.yml` → `OK`.
  2. Dispatch the final branch reviewer (opus) and the final adversarial verifier (Fable) with the three task diffs (superpowers `review-package` output per task) and the three mutation transcripts; both reports clean. Every dispatch now uses the grammar (the hook is live).
  3. Write `docs/superpowers/state/strict-delivery/handoff.md` with the umbrella front matter: `plan: docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md`, `spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md`, `branch: block/strict-delivery-b4`, `head: "<git rev-parse HEAD>"` (before the state commit), `base: "<git merge-base HEAD origin/main>"`, `ledger: docs/superpowers/state/strict-delivery/ledger.md`, `resume_line` = the ledger line number of `- b4 start`, `block: 4`, `next_task: T4`, `findings_in_flight: [{SD-S2-T1: pinned}, {SD-S2-T2: pinned}, {SD-S2-T3: pinned}]`, `claims_verified_at_head: []`, `live_subagents: []`, `open_decisions` = every unanswered id in `decisions.yml`, `rulings` = this block's ruling lines, `grant: DEC-0001`, `parked_branches` = every local branch but main with reason and owner, `env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}`, `traps` = one line each.
  4. Append `CHECKPOINT b4 <head sha>` to the ledger; `git add docs/superpowers/state/strict-delivery && git commit -m "chore(state): checkpoint b4" && git push -u origin block/strict-delivery-b4`.
  5. `gh pr create --fill` (body lists tasks, gate output, mutation transcripts); `gh pr ready`; `gh pr checks --watch` → every check passes, advisory included.
  6. Merge-readiness by hand (`checklists/block-end.md` step 6 text, below in Task 9, applies verbatim): PR body free of every `merge_markers` entry in `tools/gates/config.yml`; rows pinned; grant `DEC-0001` answered `yes` by the owner.
  7. `gh pr merge --squash --delete-branch`; `git checkout main && git pull --ff-only`; `git tag strict-delivery-b4 && git push origin strict-delivery-b4`; `git branch -D block/strict-delivery-b4` if it remains; `git worktree list` → remove any worktree the block created. Check by hand what Task 7 will automate: `git rev-parse main^` equals the handoff `base`, and `git diff --name-only <handoff head> main` lists only `docs/superpowers/state/` paths.
  8. Post the status update (spec §3.3 item 1) and this resume prompt, then stop:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 5 (sub-plan S2, task 4) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

## Block b5 — review package and plan gates (Tasks 4–6; branch `block/strict-delivery-b5`; tag `strict-delivery-b5`)

### b5 preamble (controller, before Task 4)

- [ ] **Step 1: Branch** — `git checkout main && git pull --ff-only && git checkout -b block/strict-delivery-b5`; append `- b5 start <sha>` to the ledger and commit `chore(state): b5 start`.
- [ ] **Step 2: Rows** — `SD-S2-T1..T3` stay `pinned`: `merged` is written only by S4's `postmerge.py` (`by: postmerge`). Run `python3 tools/gates/gate_register.py` → `PASS`.
- [ ] **Step 3: Review packages** — Tasks 4 and 5 use superpowers' `review-package` diff plus the mutation transcript; from Task 6 the controller runs `python3 tools/gates/make_review_package.py strict-delivery S2-T<n> <base> <head>` and `python3 tools/gates/gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-T<n>` (git-ignored; the dir is the row's `owner.task`) before every reviewer dispatch, after the implementer has written `mutation-<id>.txt`, `siblings.md` and `claims.md` into that directory.

### Task 4: gate_review_package

**Files:**
- Create: `tools/gates/gate_review_package.py`
- Modify: `tools/gates/run_all.py` (GATES, DEFAULT_ARGV)
- Modify: `tools/gates/tests/test_run_all.py` (test_gates_at_this_block and test_cli_unknown_gate_is_rc_1 pin the gate list)
- Modify: `tools/gates/README.md` (one gate table row)
- Test: `tools/gates/tests/test_gate_review_package.py`

**Findings:** SD-S2-T4

**Siblings:** none -- the register row SD-S2-T4 lists no siblings

**Gates:** run_all, review_package

**Interfaces:**
- Consumes: S1 lib; `repo_fixture` (Task 1).
- Produces: `gate_review_package.check(args) -> Result`, `main(argv=None)`; `REQUIRED` (list of file names); `table_rows(text) -> list[list[str]] | None`; `mutation_errors(text, finding_id, pin_test=None) -> list[str]`. `task_findings(findings, pkg) -> list[str]`. CLI (umbrella table): `--dir .superpowers/sdd/<plan>/review/<task>` (git-ignored) | `--all` (every package under `.superpowers/sdd/*/review/*`; none is a PASS — CI has none); the task's ids are the register rows with owner `{kind: plan, plan: <plan>, task: <task>}` plus every `mutation-<id>.txt` present, so the review dir is named by the row's `owner.task` (here `S2-T<n>`). Line ids add `review_package.findings` (no owned row). Line ids: `<pfx>.file.<name>`, `<pfx>.commits`, `<pfx>.diff`, `<pfx>.gates`, `<pfx>.finding.<id>`, `<pfx>.mutation[.<id>]`, `<pfx>.siblings`, `<pfx>.claims`, with `<pfx>` = `review_package` (single) or `review_package.<plan>.<task>` (`--all`); `review_package.all` when no package exists; `<pfx>.input` for a non-directory entry under `review/` (`--all`, dot-entries included) and `review_package.<plan>.input` for a `review` path that is not a directory; `--dir` outside `<root>/.superpowers/sdd/<plan>/review/<task>` is `review_package.input`.

- [ ] **Step 1: Write the failing tests** — `tools/gates/tests/test_gate_review_package.py`:

```python
"""gate_review_package through main() (strict-delivery S2 Task 4).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_review_package -v
"""
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools.gates import gate_review_package as grp
from tools.gates.tests import repo_fixture as rf

PKG = ".superpowers/sdd/fx/review/1"
MUT = ("finding: SD-FX-1\n"
       "test: test_fx\n"
       "step 1 (fix present): python3 -m unittest t.T.test_fx -> PASS\n"
       "step 2 (fix reverted: git stash push src/fx.py): python3 -m unittest t.T.test_fx -> FAIL\n"
       "step 3 (fix restored): python3 -m unittest t.T.test_fx -> PASS\n")
GOOD = {
    "commits.txt": "abc1234 feat: fixture\n",
    "diff.patch": "diff --git a/src/fx.py b/src/fx.py\n--- a/src/fx.py\n+++ b/src/fx.py\n@@ -1 +1 @@\n+fixed\n",
    "gates.txt": "PASS register.schema\n",
    "siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-a | test_sib_a |\n",
    "claims.md": ("| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
                  "| fx fixed | python3 -m unittest t | git stash; python3 -m unittest t | OK / FAILED | PASS |\n"),
    "mutation-SD-FX-1.txt": MUT,
}


def gate(root, argv):
    buf = io.StringIO()
    with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf):
        rc = grp.main(argv)
    return rc, buf.getvalue()


class GateReviewPackage(unittest.TestCase):
    def setUp(self):
        self.root = rf.make_repo(rf.DISPATCH_FILES, rf.load_rows())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.put(GOOD)

    def put(self, files):
        for name, text in files.items():
            rf.write(self.root, f"{PKG}/{name}", text)

    def check(self):
        return gate(self.root, ["--dir", PKG])

    def assertFailLine(self, rc, out, line_id, reason=""):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)
        line = next(l for l in out.splitlines() if l.startswith(f"FAIL {line_id}"))
        self.assertIn(reason, line)

    def with_rows(self, edit):
        rows = rf.load_rows()
        edit(rows)
        self.root = rf.make_repo(rf.DISPATCH_FILES, rows)
        self.addCleanup(shutil.rmtree, self.root, True)
        self.put(GOOD)

    def test_good_package_passes(self):
        rc, out = self.check()
        self.assertEqual(0, rc, out)
        self.assertNotIn("FAIL", out)
        for lid in ("file.commits.txt", "file.diff.patch", "file.gates.txt", "file.siblings.md", "file.claims.md",
                    "commits", "diff", "gates", "mutation.SD-FX-1", "siblings", "claims"):
            self.assertIn(f"PASS review_package.{lid}\n", out + "\n")

    def test_each_missing_file_fails(self):
        expect = {n: f"review_package.file.{n}" for n in grp.REQUIRED}
        expect["mutation-SD-FX-1.txt"] = "review_package.mutation.SD-FX-1"
        for name, line_id in expect.items():
            with self.subTest(name=name):
                os.remove(os.path.join(self.root, PKG, name))
                rc, out = self.check()
                self.assertFailLine(rc, out, line_id)
                self.put({name: GOOD[name]})

    def test_mutation_step_two_must_fail(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("-> FAIL", "-> PASS")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_mutation_steps_must_run_one_command(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("restored): python3 -m unittest t.T.test_fx",
                                                      "restored): python3 -m unittest t.T.test_other")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_mutation_command_must_name_the_test(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("test: test_fx", "test: test_elsewhere")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_mutation_for_another_finding_fails(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("finding: SD-FX-1", "finding: SD-FX-2")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1")

    def test_fewer_sibling_rows_than_register_fails(self):
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n"})
        self.assertFailLine(*self.check(), "review_package.siblings")

    def test_sibling_named_in_register_must_appear(self):
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-z | test_z |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings")

    def test_claims_fail_row_fails(self):
        self.put({"claims.md": GOOD["claims.md"].replace("| PASS |", "| FAIL |")})
        self.assertFailLine(*self.check(), "review_package.claims")

    def test_claims_without_rows_fails(self):
        self.put({"claims.md": "| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"})
        self.assertFailLine(*self.check(), "review_package.claims")

    def test_red_gates_output_fails(self):
        self.put({"gates.txt": "FAIL register.schema: broken\n"})
        self.assertFailLine(*self.check(), "review_package.gates")

    def test_diff_must_be_a_git_diff(self):
        self.put({"diff.patch": "not a diff\n"})
        self.assertFailLine(*self.check(), "review_package.diff")

    def test_unregistered_finding_fails(self):
        self.put({"mutation-SD-FX-404.txt": MUT.replace("SD-FX-1", "SD-FX-404")})
        self.assertFailLine(*self.check(), "review_package.finding.SD-FX-404")

    def test_task_with_no_owned_rows_fails(self):
        other = ".superpowers/sdd/fx/review/9"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        self.assertFailLine(*gate(self.root, ["--dir", other]), "review_package.findings")

    def test_all_mode_checks_every_package(self):
        rc, out = gate(self.root, ["--all"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.fx.1.claims", out)
        os.remove(os.path.join(self.root, PKG, "mutation-SD-FX-1.txt"))
        rc, out = gate(self.root, ["--all"])
        self.assertFailLine(rc, out, "review_package.fx.1.mutation")

    def test_all_mode_with_no_packages_passes(self):
        shutil.rmtree(os.path.join(self.root, ".superpowers/sdd/fx"))
        rc, out = gate(self.root, ["--all"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.all", out)

    def test_no_mode_fails(self):
        self.assertFailLine(*gate(self.root, []), "review_package.input")

    # --- fix round 1 (F1-F9 of task-4-review1.md); every test goes through main() ---
    def test_each_empty_file_fails(self):
        for name in grp.REQUIRED:
            with self.subTest(name=name):
                self.put({name: ""})
                self.assertFailLine(*self.check(), f"review_package.file.{name}", "missing or empty")
                self.put({name: GOOD[name]})

    def test_gates_txt_must_hold_gate_output(self):
        for text in (" \n", "hello\n", "Traceback (most recent call last):\nerror\n", "fail register.schema\n",
                     "PASS a\n  FAIL b: x\n", "PASS\n"):
            with self.subTest(text=text):
                self.put({"gates.txt": text})
                self.assertFailLine(*self.check(), "review_package.gates", "gates.txt needs gate output")

    def test_gates_txt_blank_lines_and_crlf_pass(self):
        self.put({"gates.txt": "PASS a.b\r\n\nPASS c: note\r\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_claims_empty_cells_fail(self):
        head = "| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
        for row in ("| x |  | c | o | PASS |\n", "| x | d |  | o | PASS |\n", "|  | d | c | o | PASS |\n",
                    "| x | d | c |  | PASS |\n"):
            with self.subTest(row=row):
                self.put({"claims.md": head + row})
                self.assertFailLine(*self.check(), "review_package.claims", "first four cells filled")

    def test_claims_malformed_rows_fail(self):
        head = "| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
        for row in ("| x | d | c | PASS |\n", "| x | d | c | o | pass |\n", "| x | d | c | o | PASS | z |\n"):
            with self.subTest(row=row):
                self.put({"claims.md": head + row})
                self.assertFailLine(*self.check(), "review_package.claims", "not 'claim |")

    def test_claims_and_siblings_must_be_tables(self):
        self.put({"claims.md": "just prose | with a bar\n"})
        self.assertFailLine(*self.check(), "review_package.claims", "not a Markdown table")
        self.put({"claims.md": GOOD["claims.md"], "siblings.md": "| sibling | test or finding |\n| x | y |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "not a Markdown table")

    def test_only_the_first_table_counts(self):
        self.put({"claims.md": GOOD["claims.md"] + "\n| a | b |\n|---|---|\n| x | y |\n",
                  "siblings.md": GOOD["siblings.md"] + "\n| a | b |\n|---|---|\n| x | y |\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_must_be_exactly_five_lines(self):
        self.put({"mutation-SD-FX-1.txt": "\n".join(MUT.splitlines()[:4]) + "\n"})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "exactly five lines")
        self.put({"mutation-SD-FX-1.txt": MUT + "step 4 (again): python3 -m unittest t.T.test_fx -> FAIL\n"})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "exactly five lines")
        self.put({"mutation-SD-FX-1.txt": "\n" + MUT.replace("\n", "\n\n")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_test_line_is_required(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("test: test_fx", "test: ")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "line 2 must be")

    def test_mutation_step_one_and_three_must_pass(self):
        for n, tail in ((1, "(fix present): python3 -m unittest t.T.test_fx -> PASS"),
                        (3, "(fix restored): python3 -m unittest t.T.test_fx -> PASS")):
            with self.subTest(step=n):
                self.put({"mutation-SD-FX-1.txt": MUT.replace(tail, tail[:-4] + "WARN")})
                self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", f"step {n} malformed")

    def test_mutation_step_two_names_how(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("(fix reverted: git stash push src/fx.py)", "(fix reverted:  )")})
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "names no way")

    def test_mutation_test_name_is_a_whole_component(self):
        for cmd in ("t.T.test_fx_other", "t.T.xtest_fx", "t.test_fx.T"):
            with self.subTest(cmd=cmd):
                self.put({"mutation-SD-FX-1.txt": MUT.replace("t.T.test_fx", cmd)})
                self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "does not name the test")
        self.put({"mutation-SD-FX-1.txt": MUT.replace("t.T.test_fx", "tests/t.py::T::test_fx")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_test_must_equal_the_pinned_test(self):
        def pin(rows, test):
            rows["findings"][0]["pin"] = {"test": test, "transcript": "docs/x.txt"}
        self.with_rows(lambda r: pin(r, "test_other::test_elsewhere"))
        self.assertFailLine(*self.check(), "review_package.mutation.SD-FX-1", "pinned test")
        self.with_rows(lambda r: pin(r, "test_mod::test_fx"))
        rc, out = self.check()
        self.assertEqual(0, rc, out)
        self.with_rows(lambda r: pin(r, "test_fx"))
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_siblings_match_the_exact_first_cell(self):
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-abc | test_x |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "not listed: sib-a")

    def test_sibling_second_cell_is_a_test_or_a_registered_id(self):
        head = "| sibling | test or finding |\n|---|---|\n"
        for cell in ("-", "FAIL", "not a test", "SD-FX-404", ""):
            with self.subTest(cell=cell):
                self.put({"siblings.md": head + f"| sib-a | {cell} |\n"})
                self.assertFailLine(*self.check(), "review_package.siblings", "are not 'sibling |")
        for cell in ("SD-FX-2", "t.T.test_sib", "tests/t.py::T::test_sib", "test_sib"):
            with self.subTest(cell=cell):
                self.put({"siblings.md": head + f"| sib-a | {cell} |\n"})
                rc, out = self.check()
                self.assertEqual(0, rc, out)

    def test_sibling_rows_need_two_cells_and_a_name(self):
        head = "| sibling | test or finding |\n|---|---|\n"
        for row in ("| sib-a |\n", "|  | test_x |\n", "| sib-a | test_x | extra |\n"):
            with self.subTest(row=row):
                self.put({"siblings.md": head + "| sib-a | test_a |\n" + row})
                self.assertFailLine(*self.check(), "review_package.siblings", "are not 'sibling |")

    def test_commits_txt_holds_sha_subject_lines(self):
        for text in ("\n", "not a commit\n", "abc1234 ok\nnot a commit\n", "abc123 short\n", "abc1234\n"):
            with self.subTest(text=text):
                self.put({"commits.txt": text})
                self.assertFailLine(*self.check(), "review_package.commits", "<sha> <subject>")
        self.put({"commits.txt": "abc1234 one\n\n" + "0" * 40 + " two\r\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_diff_needs_a_header_and_a_body(self):
        self.put({"diff.patch": "diff --git a/x b/x\n"})
        self.assertFailLine(*self.check(), "review_package.diff", "no hunk or file line")
        self.put({"diff.patch": "diff --git x\n--- a/x\n"})
        self.assertFailLine(*self.check(), "review_package.diff", "does not start with")
        self.put({"diff.patch": "diff --git a/x b/x\nBinary files a/x and b/x differ\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_dir_must_be_the_package_layout(self):
        other = ".superpowers/sdd/fx/notreview/1"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        for d in (other, "/", "..", ".", ".superpowers/sdd/fx", f"{PKG}/x"):
            with self.subTest(dir=d):
                rc, out = gate(self.root, ["--dir", d])
                self.assertFailLine(rc, out, "review_package.input", "is not <root>/.superpowers/sdd/<plan>/review/<task>")
                self.assertNotIn("index", out)

    def test_dir_must_exist(self):
        self.assertFailLine(*gate(self.root, ["--dir", ".superpowers/sdd/fx/review/77"]),
                            "review_package.input", "no such directory")

    def test_dir_accepts_an_absolute_path(self):
        rc, out = gate(self.root, ["--dir", os.path.join(self.root, PKG)])
        self.assertEqual(0, rc, out)

    def test_dir_and_all_together_or_neither_fail(self):
        self.assertFailLine(*gate(self.root, ["--dir", PKG, "--all"]), "review_package.input", "exactly one")
        self.assertFailLine(*gate(self.root, ["--dir", ""]), "review_package.input", "exactly one")

    def test_missing_register_is_input_fail(self):
        os.remove(os.path.join(self.root, rf.STATE, "findings.yml"))
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.input")
        self.assertFailLine(*self.check(), "review_package.input")

    def test_package_under_another_plan_does_not_own_the_row(self):
        other = ".superpowers/sdd/other/review/1"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        self.assertFailLine(*gate(self.root, ["--dir", other]), "review_package.findings", "plan: other, task: 1")

    def test_all_mode_package_with_no_owner_and_no_transcript_fails(self):
        other = ".superpowers/sdd/fx/review/5"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx.5.mutation", "no register row owns")

    def test_all_mode_non_directory_entry_fails(self):
        rf.write(self.root, ".superpowers/sdd/fx/review/2", "a stray file\n")
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx.2.input", "is not a directory")

    def test_all_mode_lone_stray_file_fails_too(self):
        shutil.rmtree(os.path.join(self.root, ".superpowers/sdd/fx"))
        rf.write(self.root, ".superpowers/sdd/fx/review/2", "a stray file\n")
        rc, out = gate(self.root, ["--all"])
        self.assertFailLine(rc, out, "review_package.fx.2.input")
        self.assertNotIn("review_package.all", out)

    def test_blanks_indentation_and_prose_around_tables(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("\n", "\n \t\n"),
                  "gates.txt": " \nPASS a.b\n \n",
                  "commits.txt": "  \nabc1234 one\n",
                  "claims.md": "Claims |\n\n  " + GOOD["claims.md"].replace("\n", "\n  ").rstrip(" ") + "after the table |\n",
                  "siblings.md": "Siblings |\n\n" + GOOD["siblings.md"] + "closing prose |\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_mutation_test_value_is_stripped(self):
        self.put({"mutation-SD-FX-1.txt": MUT.replace("test: test_fx", "test:   test_fx")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_script_runs_as_a_program(self):
        env = dict(os.environ, CLAUDE_PROJECT_DIR=self.root)
        p = subprocess.run([sys.executable, grp.__file__, "--all"], capture_output=True, text=True, env=env, check=False)
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        self.assertIn("PASS review_package.fx.1.claims", p.stdout)

    # --- fix round 2 (N1-N5 of task-4-review2.md) ---
    def test_dir_with_a_wrong_prefix_is_not_the_layout(self):
        other = "a/b/fx/review/1"
        for name in grp.REQUIRED:
            rf.write(self.root, f"{other}/{name}", GOOD[name])
        rf.write(self.root, f"{other}/mutation-SD-FX-1.txt", MUT)
        self.assertFailLine(*gate(self.root, ["--dir", other]), "review_package.input", "is not <root>/")

    def test_dir_by_realpath_under_a_symlinked_root_passes(self):
        link = os.path.join(tempfile.mkdtemp(), "link")
        self.addCleanup(shutil.rmtree, os.path.dirname(link), True)
        os.symlink(self.root, link)
        for d in (os.path.join(os.path.realpath(self.root), PKG), os.path.join(link, PKG), PKG):
            with self.subTest(dir=d):
                rc, out = gate(link, ["--dir", d])
                self.assertEqual(0, rc, out)
        rc, out = gate(link, ["--all"])
        self.assertEqual(0, rc, out)

    def test_siblings_rows_at_least_the_registers_list_length(self):
        def dup(rows):
            rows["findings"][0]["siblings"] = ["sib-a", "sib-a"]
        self.with_rows(dup)
        self.assertFailLine(*self.check(), "review_package.siblings", "1 row(s) for a register siblings list of 2")
        row = "| sib-a | test_sib_a |\n"
        self.put({"siblings.md": GOOD["siblings.md"] + row})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_all_mode_sweeps_hidden_entries(self):
        hidden = ".superpowers/sdd/fx/review/.hidden"
        rf.write(self.root, f"{hidden}/x", "stray\n")
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx..hidden.mutation")
        shutil.rmtree(os.path.join(self.root, hidden))
        rf.write(self.root, ".superpowers/sdd/fx/review/.DS_Store", "x\n")
        self.assertFailLine(*gate(self.root, ["--all"]), "review_package.fx..DS_Store.input", "is not a directory")

    def test_all_mode_review_that_is_a_file_fails(self):
        shutil.rmtree(os.path.join(self.root, ".superpowers/sdd/fx"))
        rf.write(self.root, ".superpowers/sdd/zz/review", "a file\n")
        rc, out = gate(self.root, ["--all"])
        self.assertFailLine(rc, out, "review_package.zz.input", "is not a directory")
        self.assertNotIn("review_package.all", out)

    def test_register_sibling_with_outer_whitespace_or_a_pipe_is_listable(self):
        for sibling, cell in (("sib-a ", "sib-a"), (" sib-a", "sib-a"), ("a|b", "a\\|b")):
            with self.subTest(sibling=sibling):
                self.with_rows(lambda rows: rows["findings"][0].update(siblings=[sibling]))
                self.put({"siblings.md": f"| sibling | test or finding |\n|---|---|\n| {cell} | test_x |\n"})
                rc, out = self.check()
                self.assertEqual(0, rc, out)

    def test_unescaped_pipe_splits_a_cell(self):
        self.with_rows(lambda rows: rows["findings"][0].update(siblings=["a|b"]))
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| a|b | test_x |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "are not 'sibling |")

    def test_escaped_pipe_in_a_claims_cell(self):
        self.put({"claims.md": GOOD["claims.md"].replace("python3 -m unittest t |", "python3 -m unittest t \\| tail |")})
        rc, out = self.check()
        self.assertEqual(0, rc, out)

    def test_table_rows_without_spaces_or_a_trailing_pipe(self):
        head = "| sibling | test or finding |\n|---|---|\n"
        for row in ("|sib-a|test_x|\n", "| sib-a | test_x\n", "|sib-a|test_x\n"):
            with self.subTest(row=row):
                self.put({"siblings.md": head + row})
                rc, out = self.check()
                self.assertEqual(0, rc, out)

    def test_all_mode_skips_a_plan_without_a_review_dir(self):
        rf.write(self.root, ".superpowers/sdd/noreview/notes.md", "notes\n")
        rc, out = gate(self.root, ["--all"])
        self.assertEqual(0, rc, out)
        self.assertNotIn("noreview", out)

    # --- fix round 3 (R2-R4 of task-4-review3.md) ---
    def both_modes(self, dir_arg=PKG):
        for argv in (["--dir", dir_arg], ["--all"]):
            rc, out = gate(self.root, argv)
            self.assertEqual(0, rc, (argv, out))

    def external(self, rel):
        """Move <root>/rel to a fresh external directory and leave a symlink in its place."""
        ext = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, ext, True)
        src = os.path.join(self.root, rel)
        dst = os.path.join(ext, os.path.basename(rel))
        shutil.move(src, dst)
        os.symlink(dst, src)

    def test_symlinked_dot_superpowers_gives_the_same_verdict_in_both_modes(self):
        self.external(".superpowers")
        self.both_modes()

    def test_symlinked_review_dir_gives_the_same_verdict_in_both_modes(self):
        self.external(".superpowers/sdd/fx/review")
        self.both_modes()

    def test_symlinked_task_dir_is_followed_and_keeps_its_own_name(self):
        # review/1 -> review/2: the link's name (task 1, row SD-FX-1) owns the package; the target's name
        # (task 2) would own SD-FX-2, which has no transcript here
        review = os.path.join(self.root, ".superpowers/sdd/fx/review")
        os.rename(os.path.join(review, "1"), os.path.join(review, "2"))
        os.symlink("2", os.path.join(review, "1"))
        rc, out = gate(self.root, ["--dir", PKG])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.mutation.SD-FX-1", out)
        self.assertNotIn("SD-FX-2", out)

    def test_task_with_two_findings_needs_the_longest_siblings_list_only(self):
        def two(rows):
            rows["findings"][1]["owner"]["task"] = "1"
            rows["findings"][1]["siblings"] = ["sib-a", "sib-b"]
            rows["findings"][1]["state"] = "planned"
        self.with_rows(two)
        self.put({"mutation-SD-FX-2.txt": MUT.replace("SD-FX-1", "SD-FX-2"),
                  "siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-a | test_a |\n| sib-b | test_b |\n"})
        rc, out = self.check()
        self.assertEqual(0, rc, out)
        self.put({"siblings.md": "| sibling | test or finding |\n|---|---|\n| sib-a | test_a |\n"})
        self.assertFailLine(*self.check(), "review_package.siblings", "1 row(s) for a register siblings list of 2")

    def test_registered_in_run_all(self):
        from tools.gates import run_all
        self.assertIn("review_package", run_all.GATES)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": self.root}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "review_package"])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_review_package -v`
Expected: ERROR `cannot import name 'gate_review_package'`.

- [ ] **Step 3: Implement** — `tools/gates/gate_review_package.py`:

```python
#!/usr/bin/env python3
"""gate_review_package -- a task's review package is complete before a reviewer sees it.

Layout and mutation transcript format: docs/superpowers/plans/2026-10-17-strict-delivery.md, "Review package".
Extends superpowers' subagent-driven-development review-package (commits + diff) with gates.txt, one
mutation-<finding-id>.txt per finding the task closes, siblings.md and claims.md.
Usage: gate_review_package.py [--json] --dir .superpowers/sdd/<plan>/review/<task> | [--json] --all
The task's ids: register rows owned by {kind: plan, plan: <plan>, task: <task>} plus mutation-<id>.txt files.
"""
from __future__ import annotations

import os
import re
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

SDD = ".superpowers/sdd"          # git-ignored; packages live at .superpowers/sdd/<plan>/review/<task>/
REQUIRED = ["commits.txt", "diff.patch", "gates.txt", "siblings.md", "claims.md"]
STEP1 = re.compile(r"^step 1 \(fix present\): (?P<cmd>.+) -> PASS$")
STEP2 = re.compile(r"^step 2 \(fix reverted: (?P<how>.+?)\): (?P<cmd>.+) -> FAIL$")
STEP3 = re.compile(r"^step 3 \(fix restored\): (?P<cmd>.+) -> PASS$")
SEPARATOR = re.compile(r"^\|(\s*:?-{3,}:?\s*\|)+$")
COMMIT_LINE = re.compile(r"^[0-9a-f]{7,40} \S")
GATE_LINE = re.compile(r"^(PASS|FAIL) \S+")
DIFF_BODY = re.compile(r"^(@@ |--- |\+\+\+ |index |new file mode|deleted file mode|old mode|new mode"
                       r"|similarity index|rename |copy |Binary files )")
TEST_NAME = re.compile(r"^(?:[\w.\-/]+::)*(?:\w+\.)*test_\w+$")


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def _lines(text: str) -> list[str]:
    return [l.rstrip() for l in text.splitlines() if l.strip()]


def table_rows(text: str) -> list[list[str]] | None:
    """Data rows of the first Markdown table: the first '|' line is the header, the next line must be the
    separator, and the table ends at the first line that does not start with '|'. Cells split on an
    unescaped '|' only; '\\|' inside a cell is a literal '|'. None when there is no header + separator."""
    lines = [l.strip() for l in text.splitlines()]
    start = next((i for i, l in enumerate(lines) if l.startswith("|")), None)
    if start is None or start + 1 >= len(lines) or not SEPARATOR.match(lines[start + 1]):
        return None
    rows = []
    for l in lines[start + 2:]:
        if not l.startswith("|"):
            break
        rows.append(_cells(l))
    return rows


def _cells(line: str) -> list[str]:
    inner = line[1:]
    if inner.endswith("|") and not inner.endswith("\\|"):
        inner = inner[:-1]
    return [c.replace("\\|", "|").strip() for c in re.split(r"(?<!\\)\|", inner)]


def names_test(cmd: str, test: str) -> bool:
    """True when test is a whole component of cmd: a token's last dotted component or its part after '::'."""
    return any(tok.split("::")[-1].split(".")[-1] == test for tok in cmd.split())


def mutation_errors(text: str, fid: str, pin_test: str | None = None) -> list[str]:
    lines = _lines(text)
    if len(lines) != 5:
        return [f"want exactly five lines (finding, test, step 1, step 2, step 3), got {len(lines)}"]
    errs = []
    if lines[0] != f"finding: {fid}":
        errs.append(f"line 1 must be 'finding: {fid}'")
    test = lines[1][len("test: "):].strip() if lines[1].startswith("test: ") else ""
    if not test:
        errs.append("line 2 must be 'test: <test name>'")
    steps = [STEP1.match(lines[2]), STEP2.match(lines[3]), STEP3.match(lines[4])]
    for n, m in enumerate(steps, 1):
        if not m:
            errs.append(f"step {n} malformed or wrong verdict (want PASS/FAIL/PASS): {lines[n + 1][:80]!r}")
    if steps[1] and not steps[1].group("how").strip():
        errs.append("step 2 names no way the fix was reverted")
    if all(steps):
        cmds = {m.group("cmd") for m in steps}
        if len(cmds) != 1:
            errs.append("steps 1-3 must run the same command")
        elif test and not names_test(steps[0].group("cmd"), test):
            errs.append(f"the command does not name the test {test!r}")
    if pin_test is not None and test and test != pin_test.split("::")[-1]:
        errs.append(f"test {test!r} is not the register's pinned test {pin_test!r}")
    return errs


def _verdict(res, lid, ok, reason):
    if ok:
        res.passed(lid)
    else:
        res.fail(lid, reason)


def _commits(pkg, res, pfx):
    path = os.path.join(pkg, "commits.txt")
    if os.path.isfile(path):
        lines = _lines(_read(path))
        bad = [l for l in lines if not COMMIT_LINE.match(l)]
        _verdict(res, f"{pfx}.commits", lines and not bad,
                 f"commits.txt needs one '<sha> <subject>' line per commit; "
                 f"{len(bad)} bad of {len(lines)}: {bad[0][:60] if bad else '-'!r}")


def diff_error(text: str) -> str:
    if not text.startswith("diff --git a/"):
        return "diff.patch does not start with 'diff --git a/'"
    if not any(DIFF_BODY.match(l) for l in text.splitlines()[1:]):
        return "diff.patch has no hunk or file line after 'diff --git'"
    return ""


def _gates(pkg, res, pfx):
    path = os.path.join(pkg, "gates.txt")
    if os.path.isfile(path):
        lines = _lines(_read(path))
        bad = [l for l in lines if not GATE_LINE.match(l)]
        red = [l for l in lines if l.startswith("FAIL ")]
        _verdict(res, f"{pfx}.gates", lines and not bad and not red,
                 f"gates.txt needs gate output ('PASS|FAIL <id>' lines, no FAIL): {len(lines)} line(s), "
                 f"{len(bad)} not gate lines, {len(red)} FAIL; first: {(bad or red or [''])[0][:60]!r}")


def _sibling_ok(cell: str, findings: list) -> bool:
    return bool(TEST_NAME.match(cell)) or registers.finding_by_id(findings, cell) is not None


def _siblings(pkg, needed, longest, findings, res, pfx):
    path = os.path.join(pkg, "siblings.md")
    if not os.path.isfile(path):
        return
    rows = table_rows(_read(path))
    if rows is None:
        res.fail(f"{pfx}.siblings", "siblings.md is not a Markdown table (header row + separator)")
        return
    bad = [r for r in rows if len(r) != 2 or not r[0] or not _sibling_ok(r[1], findings)]
    missing = [s for s in needed if not any(s == r[0] for r in rows)]
    if len(rows) < longest:
        res.fail(f"{pfx}.siblings", f"{len(rows)} row(s) for a register siblings list of {longest}")
    elif bad:
        res.fail(f"{pfx}.siblings", f"{len(bad)} row(s) are not 'sibling | test name or registered finding id'")
    elif missing:
        res.fail(f"{pfx}.siblings", f"register sibling(s) not listed: {', '.join(missing)}")
    else:
        res.passed(f"{pfx}.siblings")


def _claims(pkg, res, pfx):
    path = os.path.join(pkg, "claims.md")
    if not os.path.isfile(path):
        return
    rows = table_rows(_read(path))
    if rows is None:
        res.fail(f"{pfx}.claims", "claims.md is not a Markdown table (header row + separator)")
        return
    malformed = [r for r in rows if len(r) != 5 or not all(r[:4]) or r[4] not in ("PASS", "FAIL")]
    failed = [r[0] for r in rows if len(r) == 5 and r[4] == "FAIL"]
    if not rows:
        res.fail(f"{pfx}.claims", "no executed claims; a claim with no executable check is a finding")
    elif malformed:
        res.fail(f"{pfx}.claims", f"{len(malformed)} row(s) not 'claim | direct | contradiction | output | PASS/FAIL' "
                 "with the first four cells filled")
    elif failed:
        res.fail(f"{pfx}.claims", f"FAIL rows: {'; '.join(failed)}")
    else:
        res.passed(f"{pfx}.claims")


def task_findings(findings: list, pkg: str) -> list[str]:
    """Ids the package's task closes: register rows with owner {kind: plan, plan: <plan>, task: <task>}
    (the directory names in .superpowers/sdd/<plan>/review/<task>) plus every mutation-<id>.txt present."""
    parts = os.path.normpath(pkg).split(os.sep)
    plan, task = parts[-3], parts[-1]
    owned = {f["id"] for f in findings
             if (f.get("owner") or {}).get("kind") == "plan"
             and str(f["owner"].get("plan")) == plan and str(f["owner"].get("task")) == task}
    files = {n[len("mutation-"):-len(".txt")] for n in os.listdir(pkg)
             if n.startswith("mutation-") and n.endswith(".txt")}
    return sorted(owned | files)


def _pin_test(row: dict):
    pin = row.get("pin")
    test = pin.get("test") if isinstance(pin, dict) else None
    return test if isinstance(test, str) else None


def _mutations(pkg, ids, findings, res, pfx):
    """(register siblings still to list, longest register siblings list among the task's findings)."""
    needed, longest = [], 0
    for fid in ids:
        f = registers.finding_by_id(findings, fid)
        if f is None:
            res.fail(f"{pfx}.finding.{fid}", "not in docs/superpowers/state/findings.yml")
            continue
        listed = [str(s).strip() for s in (f.get("siblings") or [])]
        longest = max(longest, len(listed))
        needed.extend(s for s in listed if s not in needed)
        path = os.path.join(pkg, f"mutation-{fid}.txt")
        if not os.path.isfile(path):
            res.fail(f"{pfx}.mutation.{fid}", f"mutation-{fid}.txt missing")
            continue
        errs = mutation_errors(_read(path), fid, _pin_test(f))
        _verdict(res, f"{pfx}.mutation.{fid}", not errs, "; ".join(errs))
    return needed, longest


def check_package(root: str, pkg: str, ids: list, findings: list, res: Result, pfx: str) -> None:
    for name in REQUIRED:
        path = os.path.join(pkg, name)
        _verdict(res, f"{pfx}.file.{name}", os.path.isfile(path) and os.path.getsize(path) > 0,
                 f"{os.path.relpath(path, root)} missing or empty")
    _commits(pkg, res, pfx)
    diff = os.path.join(pkg, "diff.patch")
    if os.path.isfile(diff):
        err = diff_error(_read(diff))
        _verdict(res, f"{pfx}.diff", not err, err)
    _gates(pkg, res, pfx)
    needed, longest = _mutations(pkg, ids, findings, res, pfx)
    _siblings(pkg, needed, longest, findings, res, pfx)
    _claims(pkg, res, pfx)


def _layout(roots: tuple, wanted: str) -> tuple[str, str]:
    """(plan, task) of <root>/.superpowers/sdd/<plan>/review/<task> for the root as given or its realpath;
    lexical only (no component below the root is resolved, so --dir agrees with --all); else GateError."""
    for root in roots:
        parts = os.path.relpath(wanted, root).split(os.sep)
        if len(parts) == 5 and parts[:2] == SDD.split("/") and parts[3] == "review":
            return parts[2], parts[4]
    raise GateError(f"--dir {wanted} is not <root>/{SDD}/<plan>/review/<task>")


def _check_all(root: str, findings: list, res: Result) -> None:
    """Every entry under <root>/.superpowers/sdd/<plan>/review/, dot-entries included."""
    sdd = os.path.join(root, SDD)
    seen = False
    for plan in sorted(os.listdir(sdd)) if os.path.isdir(sdd) else []:
        review = os.path.join(sdd, plan, "review")
        if not os.path.lexists(review):
            continue
        seen = True
        if not os.path.isdir(review):
            res.fail(f"review_package.{plan}.input", f"{os.path.relpath(review, root)} is not a directory")
            continue
        for task in sorted(os.listdir(review)):
            _check_entry(root, os.path.join(review, task), f"review_package.{plan}.{task}", findings, res)
    if not seen:
        res.passed("review_package.all")


def _check_entry(root: str, pkg: str, pfx: str, findings: list, res: Result) -> None:
    if not os.path.isdir(pkg):
        res.fail(f"{pfx}.input", f"{os.path.relpath(pkg, root)} is not a directory")
        return
    ids = task_findings(findings, pkg)
    if not ids:
        res.fail(f"{pfx}.mutation", "no register row owns this task and no mutation-<finding-id>.txt exists")
    check_package(root, pkg, ids, findings, res, pfx)


def check(args: list) -> Result:
    p = gate_args("gate_review_package")
    p.add_argument("--dir")
    p.add_argument("--all", action="store_true")
    a = p.parse_args(args)
    if a.all == bool(a.dir):
        raise GateError("give exactly one of --all or --dir .superpowers/sdd/<plan>/review/<task>")
    given = os.path.abspath(a.root or repo_root())
    root = os.path.realpath(given)
    findings = registers.load_findings(root)
    res = Result("review_package")
    if a.all:
        _check_all(root, findings, res)
        return res
    wanted = os.path.normpath(os.path.join(given, a.dir))    # an absolute --dir wins in join
    plan, task = _layout((given, root), wanted)
    pkg = os.path.join(root, SDD, plan, "review", task)    # symlinks below the root are followed, as in --all
    if not os.path.isdir(pkg):
        raise GateError(f"--dir {a.dir}: no such directory")
    ids = task_findings(findings, pkg)
    if not ids:
        res.fail("review_package.findings", f"no register row has owner {{kind: plan, plan: {plan}, task: {task}}}")
    check_package(root, pkg, ids, findings, res, "review_package")
    return res


def main(argv=None):
    return run_gate("review_package", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Register in run_all** — `GATES = ["register", "claims", "decisions", "dispatch", "review_package"]`; add `"review_package": ["--all"]` to `DEFAULT_ARGV`. Update the two pins in `tools/gates/tests/test_run_all.py`: `test_gates_at_this_block` expects the five-gate list; `test_cli_unknown_gate_is_rc_1` expects `(installed: register, claims, decisions, dispatch, review_package)`. Add the gate's row to the table in `tools/gates/README.md`.

- [ ] **Step 5: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_gate_review_package -v` → `OK` (62 tests); `python3 tools/gates/run_all.py` → only `PASS` (30 lines at this block, with no package present); the full suite 423 tests.

- [ ] **Step 6: Commit**

```bash
git add tools/gates/gate_review_package.py tools/gates/run_all.py tools/gates/tests/test_gate_review_package.py tools/gates/tests/test_run_all.py tools/gates/README.md
git commit -m "feat(gates): gate_review_package -- required files, PASS/FAIL/PASS mutation transcript, sibling and claims tables"
```

- [ ] **Step 7: Mutation run and pin**

```bash
T=tools.gates.tests.test_gate_review_package.GateReviewPackage.test_claims_fail_row_fails
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/failed = \[r\[0\] for r in rows if len\(r\) == 5 and r\[4\] == "FAIL"\]/failed = []/' tools/gates/gate_review_package.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/gate_review_package.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T4/mutation-SD-S2-T4.txt` (the package copy):
```
finding: SD-S2-T4
test: test_claims_fail_row_fails
step 1 (fix present): python3 -m unittest tools.gates.tests.test_gate_review_package.GateReviewPackage.test_claims_fail_row_fails -> PASS
step 2 (fix reverted: perl empties the FAIL-row list in tools/gates/gate_review_package.py): python3 -m unittest tools.gates.tests.test_gate_review_package.GateReviewPackage.test_claims_fail_row_fails -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_gate_review_package.GateReviewPackage.test_claims_fail_row_fails -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T4.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T4.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T4/mutation-SD-S2-T4.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T4.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T4.md
echo "- S2-T4 DONE <task head sha> gate_review_package; review docs/superpowers/state/strict-delivery/reviews/S2-T4.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T4"
PIN = {"test": "test_gate_review_package::test_claims_fail_row_fails",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T4.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T4.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T4.txt docs/superpowers/state/strict-delivery/reviews/S2-T4.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T4 pinned; ledger S2-T4"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 8: Ledger (controller, after the task review is ✅)** — the line `- S2-T4 DONE <task head sha> gate_review_package; review docs/superpowers/state/strict-delivery/reviews/S2-T4.md` goes to `docs/superpowers/state/strict-delivery/ledger.md` in the pin commit above (`chore(state): SD-S2-T4 pinned; ledger S2-T4`); no separate ledger commit.

### Task 5: make_review_package helper

**Files:**
- Create: `tools/gates/make_review_package.py`
- Test: `tools/gates/tests/test_make_review_package.py`

**Findings:** SD-S2-T5

**Siblings:** none -- the register row SD-S2-T5 lists no siblings

**Gates:** run_all, review_package

**Interfaces:**
- Consumes: `run_all.GATES`, `run_all.run` (S1/Task 4); `gitinfo.head_sha`; `repo_fixture`.
- Produces: CLI `python3 tools/gates/make_review_package.py <plan> <task> <base> <head> [--only a,b]` → writes `.superpowers/sdd/<plan>/review/<task>/{commits.txt,diff.patch,gates.txt}` (git-ignored; `docs/superpowers/state/<plan>/` must exist); never touches `mutation-*.txt`, `siblings.md`, `claims.md`; exit 0 only when the range is valid and every gate in `gates.txt` passed. `main(argv=None) -> int`; `EXCLUDE = ("review_package", "merge_ready")`; `SDD = ".superpowers/sdd"`.

- [ ] **Step 1: Write the failing tests** — `tools/gates/tests/test_make_review_package.py`:

```python
"""make_review_package.main() on a fixture repo (strict-delivery S2 Task 5).
Run from the repo root: python3 -m unittest tools.gates.tests.test_make_review_package -v
"""
import contextlib
import io
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.gates import gate_review_package as grp
from tools.gates import make_review_package as mrp
from tools.gates.tests import repo_fixture as rf

REVIEW = ".superpowers/sdd/fx/review"
MUTATION = ("finding: SD-FX-1\n"
            "test: test_fx\n"
            "step 1 (fix present): python3 -m unittest t.T.test_fx -> PASS\n"
            "step 2 (fix reverted: git stash push src/fx.py): python3 -m unittest t.T.test_fx -> FAIL\n"
            "step 3 (fix restored): python3 -m unittest t.T.test_fx -> PASS\n")
SIBLINGS = "| sibling | test or finding |\n|---|---|\n| sib-a | test_sib_a |\n"
CLAIMS = ("| claim | direct | contradiction | output | verdict |\n|---|---|---|---|---|\n"
          "| fx fixed | grep -n fixed src/fx.py | grep -c unfixed src/fx.py | 1:fixed = True / 0 | PASS |\n")


def read(root, rel):
    with open(os.path.join(root, rel), encoding="utf-8") as f:
        return f.read()


class MakeReviewPackage(unittest.TestCase):
    def setUp(self):
        files = dict(rf.DISPATCH_FILES)
        files["docs/superpowers/state/fx/mutations/.gitkeep"] = ""
        files["docs/reviews/.gitkeep"] = ""    # gate_register needs the report directory
        files[".gitignore"] = ".superpowers/\n"    # as in the real repo: the package dir is not "dirty"
        self.root = rf.make_repo(files, rf.load_rows())
        self.addCleanup(shutil.rmtree, self.root, True)
        self.base = rf.git(self.root, "rev-parse", "HEAD")
        rf.write(self.root, "src/fx.py", "fixed = True\n")
        self.head = rf.commit(self.root, "feat: fixture fix")

    def run_main(self, main, *argv):
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": self.root}), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = main(list(argv))
        return rc, out.getvalue() + err.getvalue()

    def make(self, *argv):
        return self.run_main(mrp.main, *argv)

    def test_writes_commits_diff_and_gates(self):
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        commits = read(self.root, f"{REVIEW}/1/commits.txt")
        self.assertEqual(1, len(commits.splitlines()), commits)
        self.assertIn("feat: fixture fix", commits)
        diff = read(self.root, f"{REVIEW}/1/diff.patch")
        self.assertTrue(diff.startswith("diff --git"), diff[:80])
        self.assertIn("src/fx.py", diff)
        gates = read(self.root, f"{REVIEW}/1/gates.txt")
        self.assertIn("register", gates)
        self.assertNotIn("FAIL", gates)

    def test_keeps_the_implementer_files(self):
        mine = "| sibling | test or finding |\n|---|---|\n"
        rf.write(self.root, f"{REVIEW}/1/siblings.md", mine)
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertEqual(mine, read(self.root, f"{REVIEW}/1/siblings.md"))
        self.assertIn("claims.md", out)

    def test_empty_range_fails(self):
        rc, out = self.make("fx", "1", self.head, self.head)
        self.assertEqual(1, rc, out)
        self.assertIn("empty commit range", out)

    def test_reversed_range_fails(self):
        rc, out = self.make("fx", "1", self.head, self.base)
        self.assertEqual(1, rc, out)
        self.assertIn("not a descendant", out)

    def test_head_must_be_checked_out(self):
        first = rf.git(self.root, "rev-list", "--max-parents=0", "HEAD")
        rf.git(self.root, "checkout", "-q", self.base)
        rc, out = self.make("fx", "1", first, self.head)
        self.assertEqual(1, rc, out)
        self.assertIn("check out", out)

    def test_missing_plan_state_dir_fails(self):
        rc, out = self.make("nope", "1", self.base, self.head)
        self.assertEqual(1, rc, out)
        self.assertIn("new_plan_state", out)

    def test_red_gate_fails_but_still_writes_gates_txt(self):
        rf.write(self.root, "docs/superpowers/state/findings.yml", "not: [valid\n")
        rf.commit(self.root, "break the register")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL", read(self.root, f"{REVIEW}/1/gates.txt"))

    def test_decorated_log_config_does_not_reach_commits_txt(self):
        rf.git(self.root, "config", "log.decorate", "short")
        rf.git(self.root, "tag", "task-tip", self.head)
        self.assertIn("tag: task-tip", rf.git(self.root, "log", "--oneline", "-1"))    # the config does decorate
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        commits = read(self.root, f"{REVIEW}/1/commits.txt")
        self.assertRegex(commits, r"^[0-9a-f]{7,40} feat: fixture fix\n$")
        self.assertNotIn("tag:", commits)

    def test_diff_prefix_config_does_not_reach_diff_patch(self):
        rf.git(self.root, "config", "diff.noprefix", "true")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/src/fx.py b/src/fx.py"))

    def test_empty_diff_fails(self):
        rf.git(self.root, "commit", "-q", "--allow-empty", "-m", "no change")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rc, out = self.make("fx", "1", head + "~1", head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("empty diff", out)

    def test_task_name_cannot_leave_the_package_dir(self):
        rc, out = self.make("fx", "../x", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("not a directory name", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/fx/x")))

    def test_dot_names_are_refused(self):
        for plan, task in (("fx", ".."), ("fx", "."), ("..", "1"), (".", "1"), ("fx", ".hidden")):
            with self.subTest(plan=plan, task=task):
                rc, out = self.make(plan, task, self.base, self.head, "--only", "register")
                self.assertEqual(1, rc, out)
                self.assertIn("not a directory name", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/fx/commits.txt")))
        self.assertFalse(os.path.exists(os.path.join(self.root, ".superpowers/sdd/review")))

    def test_dirty_tree_fails_before_writing(self):
        rf.write(self.root, "docs/superpowers/state/findings.yml", "not: [valid\n")
        rf.commit(self.root, "break the register")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rf.git(self.root, "checkout", "-q", self.base, "--", "docs/superpowers/state/findings.yml")    # fix, uncommitted
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("working tree is not clean", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def test_untracked_file_fails_before_writing(self):
        rf.write(self.root, "stray.txt", "x\n")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("working tree is not clean", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def test_color_config_does_not_reach_the_package(self):
        rf.git(self.root, "config", "color.ui", "always")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertNotIn("\x1b", read(self.root, f"{REVIEW}/1/commits.txt"))
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/"))

    def test_textconv_driver_does_not_reach_diff_patch(self):
        rf.write(self.root, ".gitattributes", "*.x diff=up\n")
        rf.write(self.root, "data.x", "hi\n")
        head = rf.commit(self.root, "add data.x")
        rf.git(self.root, "config", "diff.up.textconv", "false")    # a converter that fails
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertIn("+hi\n", read(self.root, f"{REVIEW}/1/diff.patch"))

    def test_external_diff_driver_does_not_reach_diff_patch(self):
        script = os.path.join(self.root, "..", os.path.basename(self.root) + "-extdiff.sh")
        rf.write(os.path.dirname(script), os.path.basename(script), "#!/bin/sh\necho EXTERNAL $1\n")
        os.chmod(script, 0o755)
        self.addCleanup(os.remove, script)
        rf.git(self.root, "config", "diff.external", script)
        self.assertIn("EXTERNAL", rf.git(self.root, "diff", f"{self.base}..{self.head}"))    # the config does replace the diff
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(0, rc, out)
        self.assertTrue(read(self.root, f"{REVIEW}/1/diff.patch").startswith("diff --git a/"))

    def test_src_dst_prefix_config_gives_a_diff_the_gate_accepts(self):
        rf.git(self.root, "config", "diff.srcPrefix", "SRC/")
        rf.git(self.root, "config", "diff.dstPrefix", "DST/")
        rc, out = self.make("fx", "1", self.base, self.head)
        self.assertEqual(0, rc, out)
        for name, text in (("mutation-SD-FX-1.txt", MUTATION), ("siblings.md", SIBLINGS), ("claims.md", CLAIMS)):
            rf.write(self.root, f"{REVIEW}/1/{name}", text)
        rc, out = self.run_main(grp.main, "--dir", f"{REVIEW}/1")
        self.assertEqual(0, rc, out)
        self.assertIn("PASS review_package.diff", out)
        self.assertEqual("diff --git a/src/fx.py b/src/fx.py", read(self.root, f"{REVIEW}/1/diff.patch").splitlines()[0])

    def test_assume_unchanged_tracked_file_is_not_clean(self):
        self.hide_a_fix("--assume-unchanged")
        rc, out = self.make("fx", "1", self.base, self.broken, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("hidden from git status", out)
        self.assertIn("findings.yml", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def test_skip_worktree_tracked_file_is_not_clean(self):
        self.hide_a_fix("--skip-worktree")
        rc, out = self.make("fx", "1", self.base, self.broken, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("hidden from git status", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, REVIEW)))

    def hide_a_fix(self, flag):
        """HEAD holds a broken register; the good one is back in the worktree, hidden from git status."""
        rf.write(self.root, "docs/superpowers/state/findings.yml", "not: [valid\n")
        rf.commit(self.root, "break the register")
        self.broken = rf.git(self.root, "rev-parse", "HEAD")
        rf.git(self.root, "update-index", flag, "docs/superpowers/state/findings.yml")
        rf.write(self.root, "docs/superpowers/state/findings.yml",
                 rf.git(self.root, "show", f"{self.base}:docs/superpowers/state/findings.yml") + "\n")
        self.assertEqual("", rf.git(self.root, "status", "--porcelain"))    # git status does not see it

    def test_diff_has_ten_lines_of_context(self):
        rf.write(self.root, "ctx.txt", "".join(f"line{n:02d}\n" for n in range(1, 31)))
        mid = rf.commit(self.root, "add ctx.txt")
        rf.write(self.root, "ctx.txt", "".join(f"line{n:02d}\n" if n != 15 else "changed\n" for n in range(1, 31)))
        head = rf.commit(self.root, "change line 15")
        rc, out = self.make("fx", "1", mid, head, "--only", "register")
        self.assertEqual(0, rc, out)
        diff = read(self.root, f"{REVIEW}/1/diff.patch")
        self.assertIn(" line05", diff.splitlines())
        self.assertNotIn(" line04", diff.splitlines())

    def test_show_signature_config_does_not_reach_commits_txt(self):
        if shutil.which("ssh-keygen") is None:
            self.skipTest("ssh-keygen is needed to sign a fixture commit")
        keydir = tempfile.mkdtemp(prefix="mrp-key-")
        self.addCleanup(shutil.rmtree, keydir, True)
        key = os.path.join(keydir, "key")
        subprocess.run(["ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", key], check=True)
        rf.git(self.root, "config", "gpg.format", "ssh")
        rf.git(self.root, "config", "user.signingkey", key + ".pub")
        rf.write(self.root, "src/signed.py", "signed = True\n")
        rf.git(self.root, "add", "-A")
        rf.git(self.root, "commit", "-q", "-S", "-m", "feat: signed")
        head = rf.git(self.root, "rev-parse", "HEAD")
        rf.git(self.root, "config", "log.showSignature", "true")
        self.assertGreater(len(rf.git(self.root, "log", "--oneline", "-1").splitlines()), 1)    # the config does add lines
        rc, out = self.make("fx", "1", self.base, head, "--only", "register")
        self.assertEqual(0, rc, out)
        commits = read(self.root, f"{REVIEW}/1/commits.txt")
        self.assertEqual(2, len(commits.splitlines()), commits)

    def test_annotated_tag_head_is_accepted(self):
        rf.git(self.root, "tag", "-a", "-m", "task tip", "task-tip", self.head)
        rc, out = self.make("fx", "1", self.base, "task-tip", "--only", "register")
        self.assertEqual(0, rc, out)

    def test_unknown_revisions_name_the_argument(self):
        for label, argv in (("base", ("nosuchrev", self.head)), ("head", (self.base, "nosuchrev"))):
            with self.subTest(label=label):
                rc, out = self.make("fx", "1", *argv, "--only", "register")
                self.assertEqual(1, rc, out)
                self.assertIn(f"unknown revision for {label}: nosuchrev", out)

    def test_not_a_git_repository_fails(self):
        plain = tempfile.mkdtemp(prefix="mrp-plain-")
        self.addCleanup(shutil.rmtree, plain, True)
        os.makedirs(os.path.join(plain, "docs/superpowers/state/fx"))
        out, err = io.StringIO(), io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": plain}), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = mrp.main(["fx", "1", "a", "b"])
        self.assertEqual(1, rc, err.getvalue())
        self.assertIn("not a git repository", err.getvalue())

    def test_package_path_that_is_a_file_fails(self):
        rf.write(self.root, f"{REVIEW}/1", "in the way\n")
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("cannot write", out)

    def test_gates_txt_that_is_a_directory_fails(self):
        os.makedirs(os.path.join(self.root, REVIEW, "1", "gates.txt"))
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register")
        self.assertEqual(1, rc, out)
        self.assertIn("cannot write gates.txt", out)

    def test_only_drops_empty_entries(self):
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "register, ,")
        self.assertEqual(0, rc, out)
        gates = read(self.root, f"{REVIEW}/1/gates.txt")
        self.assertTrue(gates and all(l.startswith("PASS register.") for l in gates.splitlines()), gates)

    def test_only_with_no_gate_name_fails_like_run_all(self):
        rc, out = self.make("fx", "1", self.base, self.head, "--only", "")
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL run_all.empty", read(self.root, f"{REVIEW}/1/gates.txt"))

    def test_package_passes_gate_review_package(self):
        rc, out = self.make("fx", "1", self.base, self.head)
        self.assertEqual(0, rc, out)
        self.assertRegex(read(self.root, f"{REVIEW}/1/gates.txt"), r"(?m)^PASS register\.")
        for name, text in (("mutation-SD-FX-1.txt", MUTATION), ("siblings.md", SIBLINGS), ("claims.md", CLAIMS)):
            rf.write(self.root, f"{REVIEW}/1/{name}", text)
        rc, out = self.run_main(grp.main, "--dir", f"{REVIEW}/1")
        self.assertEqual(0, rc, out)
        lines = [l for l in out.splitlines() if l.strip()]
        self.assertTrue(lines and all(l.startswith("PASS ") for l in lines), out)
        self.assertTrue(any(re.match(r"PASS review_package\.gates", l) for l in lines), out)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_make_review_package -v`
Expected: ERROR `cannot import name 'make_review_package'`.

- [ ] **Step 3: Implement** — `tools/gates/make_review_package.py`:

```python
#!/usr/bin/env python3
"""make_review_package -- build .superpowers/sdd/<plan>/review/<task>/ (git-ignored) for one task's BASE..HEAD.

Writes commits.txt (git log --oneline --no-decorate), diff.patch (git diff -U10) and gates.txt
(run_all at HEAD minus the gates that judge the package or the PR). The implementer adds
mutation-<finding-id>.txt, siblings.md and claims.md; this script never touches those.
The output is what gate_review_package accepts: commit lines '<sha> <subject>', a diff starting
'diff --git a/', gate lines 'PASS|FAIL <id>'. git is run with the options that fix those forms
whatever the user's git config says (decorations, diff prefixes, external diff, colour).
Same range guards as superpowers' subagent-driven-development scripts/review-package; a dirty tree is
refused, because gates.txt must describe HEAD, not the working tree.
Usage: make_review_package.py <plan> <task> <base> <head> [--only gate,gate]
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates import run_all  # noqa: E402
from tools.gates.lib import GateError, gitinfo, repo_root  # noqa: E402

STATE = "docs/superpowers/state"
SDD = ".superpowers/sdd"
EXCLUDE = ("review_package", "merge_ready")
IMPLEMENTER_FILES = ("siblings.md", "claims.md")
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
# options that pin the forms gate_review_package reads, against the caller's git config
PLAIN = ("-c", "color.ui=never", "-c", "log.showSignature=false")


class Refused(Exception):
    pass


def _git(root: str, *args: str) -> bytes:
    r = subprocess.run(["git", "--no-pager", *args], cwd=root, capture_output=True, check=False,
                       env={**os.environ, "LC_ALL": "C"})
    if r.returncode != 0:
        err = r.stderr.decode("utf-8", "replace").strip()
        raise Refused(f"git {' '.join(args)}: {err or 'exit ' + str(r.returncode)}")
    return r.stdout


def _sha(root: str, rev: str, label: str) -> str:
    try:
        return _git(root, "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}").decode().strip()
    except Refused:
        raise Refused(f"unknown revision for {label}: {rev}") from None


def _range(root: str, base: str, head: str) -> tuple[str, str, int]:
    b, h = _sha(root, base, "base"), _sha(root, head, "head")
    anc = subprocess.run(["git", "merge-base", "--is-ancestor", b, h], cwd=root, capture_output=True, check=False)
    if anc.returncode != 0:
        raise Refused(f"HEAD is not a descendant of BASE: {base}..{head}")
    n = int(_git(root, "rev-list", "--count", f"{b}..{h}").decode().strip())
    if n == 0:
        raise Refused(f"empty commit range: {base}..{head}")
    if h != gitinfo.head_sha(root):
        raise Refused(f"check out {head} first: gates.txt must describe the commit under review")
    return b, h, n


def _write(path: str, data) -> None:
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def _package(root: str, a) -> tuple[str, int]:
    for label, value in (("plan", a.plan), ("task", a.task)):
        if not NAME.match(value):
            raise Refused(f"{label} {value!r} is not a directory name ({NAME.pattern})")
    if not os.path.isdir(os.path.join(root, STATE, a.plan)):
        raise Refused(f"{STATE}/{a.plan}/ missing: run tools/gates/new_plan_state.py {a.plan} first")
    if not gitinfo.status_clean(root):
        raise Refused("working tree is not clean: gates.txt must describe the commit under review; "
                      "commit or stash first (the ignored .superpowers/ does not count)")
    hidden = [l[2:] for l in _git(root, "ls-files", "-v").decode("utf-8", "replace").splitlines()
              if l[:1].islower() or l[:1] == "S"]    # assume-unchanged / skip-worktree: status cannot see them
    if hidden:
        raise Refused(f"tracked file(s) hidden from git status (assume-unchanged or skip-worktree): "
                      f"{', '.join(hidden[:5])}; clear the flag so gates.txt describes HEAD")
    b, h, n = _range(root, a.base, a.head)
    diff = _git(root, *PLAIN, "diff", "--no-ext-diff", "--no-textconv", "--src-prefix=a/", "--dst-prefix=b/", "-U10", f"{b}..{h}")
    if not diff.strip():
        raise Refused(f"empty diff for {a.base}..{a.head}: the commits change no file")
    pkg = os.path.join(root, SDD, a.plan, "review", a.task)
    try:
        os.makedirs(pkg, exist_ok=True)
        _write(os.path.join(pkg, "commits.txt"), _git(root, *PLAIN, "log", "--oneline", "--no-decorate", f"{b}..{h}"))
        _write(os.path.join(pkg, "diff.patch"), diff)
    except OSError as e:
        raise Refused(f"cannot write {os.path.relpath(pkg, root)}: {e.strerror or e}") from None
    return pkg, n


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="make_review_package")
    p.add_argument("plan")
    p.add_argument("task")
    p.add_argument("base")
    p.add_argument("head")
    p.add_argument("--only", help="comma-separated gates for gates.txt (default: run_all.GATES minus "
                                  + ", ".join(EXCLUDE) + ")")
    a = p.parse_args(argv)
    try:
        root = repo_root()
        pkg, n = _package(root, a)
    except (Refused, GateError) as e:
        print(f"make_review_package: {e}", file=sys.stderr)
        return 1
    names = ([x.strip() for x in a.only.split(",") if x.strip()] if a.only is not None
             else [g for g in run_all.GATES if g not in EXCLUDE])    # as run_all --only
    res = run_all.run(names, {"root": root})
    try:
        _write(os.path.join(pkg, "gates.txt"), res.render(False) + "\n")
        pending = [f for f in IMPLEMENTER_FILES if not os.path.isfile(os.path.join(pkg, f))]
        if not any(f.startswith("mutation-") for f in os.listdir(pkg)):
            pending.append("mutation-<finding-id>.txt")
    except OSError as e:
        print(f"make_review_package: cannot write gates.txt: {e.strerror or e}", file=sys.stderr)
        return 1
    print(f"wrote {os.path.relpath(pkg, root)}: {n} commit(s); gates {'PASS' if res.ok() else 'FAIL'}")
    if pending:
        print(f"the implementer still adds: {', '.join(pending)}")
    return 0 if res.ok() else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_make_review_package -v` → `OK` (30 tests); `python3 tools/gates/run_all.py` → only `PASS`.

- [ ] **Step 5: Commit**

```bash
git add tools/gates/make_review_package.py tools/gates/tests/test_make_review_package.py
git commit -m "feat(gates): make_review_package -- commits, diff and gates.txt from BASE..HEAD into the ignored package dir"
```

- [ ] **Step 6: Mutation run and pin**

```bash
T=tools.gates.tests.test_make_review_package.MakeReviewPackage.test_empty_range_fails
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/    if n == 0:/    if False:/' tools/gates/make_review_package.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/make_review_package.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T5/mutation-SD-S2-T5.txt` (the package copy):
```
finding: SD-S2-T5
test: test_empty_range_fails
step 1 (fix present): python3 -m unittest tools.gates.tests.test_make_review_package.MakeReviewPackage.test_empty_range_fails -> PASS
step 2 (fix reverted: perl -pi -e 's/    if n == 0:/    if False:/' tools/gates/make_review_package.py replaces the empty-range guard with False): python3 -m unittest tools.gates.tests.test_make_review_package.MakeReviewPackage.test_empty_range_fails -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_make_review_package.MakeReviewPackage.test_empty_range_fails -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T5.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T5.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T5/mutation-SD-S2-T5.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T5.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T5.md
echo "- S2-T5 DONE <task head sha> make_review_package; review docs/superpowers/state/strict-delivery/reviews/S2-T5.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T5"
PIN = {"test": "test_make_review_package::test_empty_range_fails",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T5.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T5.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T5.txt docs/superpowers/state/strict-delivery/reviews/S2-T5.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T5 pinned; ledger S2-T5"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 7: Ledger (controller, after the task review is ✅)** — the line `- S2-T5 DONE <task head sha> make_review_package; review docs/superpowers/state/strict-delivery/reviews/S2-T5.md` goes to `docs/superpowers/state/strict-delivery/ledger.md` in the pin commit above (`chore(state): SD-S2-T5 pinned; ledger S2-T5`); no separate ledger commit.

### Task 6: gate_plan and the ledger contract

**Files:**
- Create: `tools/gates/gate_plan.py`
- Create: `tools/gates/tests/fixtures/plan/good.md`
- Modify: `tools/gates/run_all.py` (GATES, DEFAULT_ARGV)
- Modify: `tools/gates/README.md` (one gate table row; append the make_review_package/gate_plan section)
- Modify: `docs/superpowers/state/strict-delivery/ledger.md` (the line 1 plan: header replaces the "# strict-delivery ledger" title; one appended ledger line)
- Create: `docs/superpowers/state/strict-delivery/mutations/.gitkeep`
- Modify: `tools/gates/tests/test_run_all.py` (test_gates_at_this_block and test_cli_unknown_gate_is_rc_1 pin the gate list)
- Test: `tools/gates/tests/test_gate_plan.py`

**Findings:** SD-S2-T6

**Siblings:** none -- the register row SD-S2-T6 lists no siblings

**Gates:** run_all, review_package, plan

**Interfaces:**
- Consumes: S1 lib; `repo_fixture`.
- Produces: `gate_plan.check(args) -> Result`, `main(argv=None)`; `parse_plan(text) -> Plan` with `Plan.tasks: list[Task]`, `Task.{num: str, line: int, block: int, files: list[str], findings: list[str] | None, findings_error: str, parallel: bool, files_errors: list[str], mode_error: str, mode_seen: bool, creates: list[str], siblings: list[str] | None, siblings_value: str, siblings_error: str, gates: list[str] | None, gates_error: str, steps: list[Step]}`, `Step.{title: str, body: list[str]}`, `Plan.{headers, tasks, placeholders, block_errors, fence_error}`; `plans_in_flight(root) -> list[tuple[str, str]]` (state-dir name, plan path); constants `STATE`, `LEDGER_PLAN`, `CHECKPOINT_LINE`. CLI: `--plan P [--source FILE | --source register:<query>]` | `--in-flight`. Query grammar: `key=pattern[,pattern][&key=pattern…]`, keys `plan` (owner.plan), `task` (owner.task), `state`, `severity`, fnmatch patterns.
- **Plan grammar checked (DEC-0016, DEC-0017):** a Files item is `- Create|Modify|Test|Delete: ` then exactly one backticked path, then prose with no backtick (else `FAIL plan.files.N`); per task `**Siblings:**` lists every register sibling of its Findings ids or says `none -- <reason>` (lower-case none, a non-empty reason after ` -- `; the empty list) (`FAIL plan.siblings.N`), `**Gates:**` names exactly `run_all` or gates whose `tools/gates/gate_X.py` exists at the root or is a Create item of this or an earlier task (`FAIL plan.gates.N`), and a task whose Findings names ids has a step whose title holds the word `pin` (case-insensitive, `\bpin\b`: "Mutation run and pin", "Pin step" and "pin:" count; "Mapping", "Spinner" and "pinned" do not; B5-FIX-3) and whose body names a test named `test_<module>::test_<name>` (`FAIL plan.pin.N`). Per-task line ids have one form (B5-FIX-3): `plan.files.N`, `plan.mode.N`, `plan.findings.N`, `plan.siblings.N`, `plan.gates.N`, `plan.pin.N` (`plan.<slug>.<check>.N` under `--in-flight`).
- **Ledger contract (defined here, used by Tasks 7–9):** `docs/superpowers/state/<plan>/ledger.md` line 1 is `plan: <path of the plan file whose tasks are executing>`; a line `CHECKPOINT b<N> …` marks a block boundary; a plan is **in flight** when the last non-empty ledger line is not a `CHECKPOINT` line in the working tree **or** in the `HEAD` commit (or the ledger is not committed).

- [ ] **Step 1: Fixture plan** — `tools/gates/tests/fixtures/plan/good.md`:

````markdown
# Fixture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** Exercise gate_plan.

**Architecture:** Two blocks; one parallel pair in the first.

**Tech Stack:** Python 3.9.

**Spec:** `docs/cited.md`

## Global Constraints

- The fixture has no constraints beyond itself.

## Review Focus

1. Overlap between parallel tasks.

---

## Block b1 — first

### Task 1: alpha

**Mode:** PARALLEL

**Files:**
- Create: `src/alpha.py`
- Test: `tests/test_alpha.py`

**Findings:** SD-FX-1

**Siblings:** sib-a

**Gates:** run_all

- [ ] **Step 1: Write the failing test**

- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.

### Task 2: beta

**Mode:** PARALLEL

**Files:**
- Create: `src/beta.py`
- Modify: `src/shared.py:10-20`
- Create: `tools/gates/gate_beta.py`

**Findings:** SD-FX-2

**Siblings:** none -- the register row lists no siblings

**Gates:** run_all, beta

- [ ] **Step 1: Pin**

```bash
python3 -m unittest tools.gates.tests.test_beta  # pin test_beta::test_beta_fails
```

- [ ] **Step 2: CHECKPOINT b1**

## Block b2 — second

### Task 3: gamma

**Files:**
- Modify: `src/shared.py`

**Findings:** none — fixture maintenance with no register row

**Siblings:** none -- no register row

**Gates:** beta

- [ ] **Step 1: CHECKPOINT b2**
````

- [ ] **Step 2: Write the failing tests** — `tools/gates/tests/test_gate_plan.py`:

```python
"""gate_plan through main() (strict-delivery S2 Task 6).
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_plan -v
"""
import contextlib
import copy
import io
import os
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock

from tools.gates import gate_plan
from tools.gates.tests import repo_fixture as rf

with open(os.path.join(rf.FIX, "plan", "good.md"), encoding="utf-8") as _f:
    GOOD = _f.read()
FENCE = "`" * 3
PH = "T" + "BD"   # a writing-plans placeholder, assembled so this file never contains one
LEDGER = "plan: docs/superpowers/plans/fx.md\n# Ledger: fx\n- T1 DONE\n"
NO_TASK_9 = r"(?m)^(PASS|FAIL) plan\.[a-z_-]+\.9\b"    # any per-task line of a Task 9 (B5-FIX-3: one id form)
PER_TASK = ("files", "findings", "siblings", "gates", "pin")


def gate(root, argv):
    buf = io.StringIO()
    with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf):
        rc = gate_plan.main(argv)
    return rc, buf.getvalue()


def plan_repo(ledger=None):
    files = dict(rf.DISPATCH_FILES)
    files["docs/superpowers/plans/fx.md"] = GOOD
    if ledger is not None:
        files["docs/superpowers/state/fx/ledger.md"] = ledger
    return rf.make_repo(files, rf.load_rows())


class GatePlan(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = plan_repo()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def check(self, text, *extra):
        path = os.path.join(self.root, "plan.under-test.md")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return gate(self.root, ["--plan", path, *extra])

    def variant(self, old, new, base=GOOD):
        text = base.replace(old, new, 1)
        self.assertNotEqual(base, text, f"fixture lacks {old!r}")
        return text

    def assertFailLine(self, rc, out, line_id):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)

    def test_good_plan_passes_with_register_source(self):
        rc, out = self.check(GOOD, "--source", "register:plan=fx")
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertIn("PASS plan.overlap.1-2", out)
        self.assertNotIn("plan.overlap.2-3", out)

    def test_report_source_counts_ids_in_the_text(self):
        rf.write(self.root, "docs/report.md", "Findings SD-FX-1 and SD-FX-2 and UTF-8.\n")
        rc, out = self.check(GOOD, "--source", "docs/report.md")
        self.assertEqual(0, rc, out)
        self.assertNotIn("UTF-8", out)

    def test_missing_header_fails(self):
        self.assertFailLine(*self.check(self.variant("**Tech Stack:** Python 3.9.\n", "")), "plan.header.tech-stack")

    def test_spec_path_must_exist(self):
        self.assertFailLine(*self.check(self.variant("`docs/cited.md`", "`docs/none.md`")), "plan.header.spec-path")

    def test_task_without_findings_line_fails(self):
        text = self.variant("**Findings:** none — fixture maintenance with no register row\n", "")
        self.assertFailLine(*self.check(text), "plan.findings.3")

    def test_findings_line_with_a_non_id_fails(self):
        self.assertFailLine(*self.check(self.variant("**Findings:** SD-FX-2", "**Findings:** see audit")),
                            "plan.findings.2")

    def test_task_without_files_fails(self):
        self.assertFailLine(*self.check(self.variant("- Modify: `src/shared.py`\n", "")), "plan.files.3")

    def test_parallel_tasks_sharing_a_file_fail(self):
        text = self.variant("- Create: `src/beta.py`", "- Create: `src/alpha.py`")
        self.assertFailLine(*self.check(text), "plan.overlap.1-2")

    def test_line_ranges_do_not_hide_an_overlap(self):
        text = self.variant("- Test: `tests/test_alpha.py`\n",
                            "- Test: `tests/test_alpha.py`\n- Modify: `src/shared.py:1-5`\n")
        rc, out = self.check(text)
        self.assertFailLine(rc, out, "plan.overlap.1-2")
        self.assertIn("src/shared.py", out)

    def test_block_over_the_cap_fails(self):
        extra = "".join(f"### Task {n}: extra\n\n**Files:**\n- Create: `src/x{n}.py`\n\n"
                        f"**Findings:** none — fixture filler\n\n" for n in (4, 5, 6))
        text = self.variant("- [ ] **Step 1: CHECKPOINT b2**", extra + "- [ ] **Step 1: CHECKPOINT b2**")
        self.assertFailLine(*self.check(text), "plan.block.2")

    def test_checkpoint_markers_split_blocks_without_headings(self):
        rc, out = self.check(self.variant("## Block b2 — second\n", ""))
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.block.2", out)

    def test_placeholder_fails(self):
        rc, out = self.check(GOOD + f"\nStep detail: {PH}\n")
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL plan.placeholder.L", out)

    def test_placeholder_inside_code_fence_fails(self):
        rc, out = self.check(GOOD + f"\n{FENCE}python\nx = 1  # {PH}\n{FENCE}\n")
        self.assertIn("FAIL plan.placeholder.L", out)

    def test_task_heading_inside_code_fence_is_ignored(self):
        rc, out = self.check(GOOD + f"\n{FENCE}\n### Task 9: not a task\n{FENCE}\n")
        self.assertEqual(0, rc, out)
        self.assertNotRegex(out, NO_TASK_9)

    def test_id_in_two_tasks_fails(self):
        text = self.variant("**Findings:** none — fixture maintenance with no register row", "**Findings:** SD-FX-1")
        self.assertFailLine(*self.check(text), "plan.id.SD-FX-1")

    def test_unregistered_id_fails(self):
        self.assertFailLine(*self.check(self.variant("**Findings:** SD-FX-2", "**Findings:** SD-FX-404")),
                            "plan.id.SD-FX-404")

    def test_source_id_missing_from_plan_fails(self):
        text = self.variant("**Findings:** SD-FX-2", "**Findings:** none — moved out of the plan")
        self.assertFailLine(*self.check(text, "--source", "register:plan=fx"), "plan.coverage.SD-FX-2")

    def test_source_with_no_ids_fails(self):
        self.assertFailLine(*self.check(GOOD, "--source", "register:plan=nothing"), "plan.source")

    def test_bad_register_query_fails(self):
        self.assertFailLine(*self.check(GOOD, "--source", "register:owner=fx"), "plan.input")

    def test_no_mode_fails(self):
        self.assertFailLine(*gate(self.root, []), "plan.input")


class InFlight(unittest.TestCase):
    def repo(self, ledger):
        root = plan_repo(ledger)
        self.addCleanup(shutil.rmtree, root, True)
        return root

    def test_in_flight_plan_is_checked(self):
        root = self.repo(LEDGER)
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.fx.header.goal", out)
        for n in ("1", "2", "3"):
            for check in PER_TASK:
                self.assertIn(f"PASS plan.fx.{check}.{n}\n", out + "\n")
        self.assertIn("PASS plan.fx.mode.1\n", out)
        self.assertNotIn(".task.", out)

    def test_closed_ledger_is_not_in_flight(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n")
        self.assertEqual([], gate_plan.plans_in_flight(root))
        rc, out = gate(root, ["--in-flight"])
        self.assertIn("PASS plan.in_flight", out)

    def test_uncommitted_checkpoint_line_is_in_flight(self):
        root = self.repo(LEDGER)
        with open(os.path.join(root, "docs/superpowers/state/fx/ledger.md"), "a", encoding="utf-8") as f:
            f.write("CHECKPOINT b1 abc1234\n")
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(root))
        rf.commit(root, "chore(state): checkpoint b1")
        self.assertEqual([], gate_plan.plans_in_flight(root))

    def test_ledger_without_plan_line_fails(self):
        root = self.repo("# Ledger: fx\n")
        self.assertIn("FAIL plan.input", gate(root, ["--in-flight"])[1])

    def test_no_state_dirs_pass(self):
        root = plan_repo()
        self.addCleanup(shutil.rmtree, root, True)
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.in_flight", out)

    def test_registered_in_run_all(self):
        from tools.gates import run_all
        self.assertIn("plan", run_all.GATES)
        root = self.repo(LEDGER)
        buf = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": root}), contextlib.redirect_stdout(buf):
            rc = run_all.main(["--only", "plan"])
        self.assertEqual(0, rc, buf.getvalue())


def words(*parts):
    """A phrase assembled from pieces, so this file never contains a writing-plans placeholder itself."""
    return "".join(parts)


PLACEHOLDER_TOKENS = (words("T", "BD"), words("TO", "DO"))
PHRASES = [words("T", "BD"), words("TO", "DO"), words("implement lat", "er"), words("fill in detai", "ls"),
           words("add appropriate error handl", "ing"), words("add valid", "ation"), words("handle edge cas", "es"),
           words("write tests for the abo", "ve"), words("similar to tas", "k 3")]


class Branches(unittest.TestCase):
    """One production-path test per load-bearing branch of gate_plan (review R6-6)."""

    @classmethod
    def setUpClass(cls):
        cls.root = plan_repo()

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.root, ignore_errors=True)

    def check(self, text, *extra):
        path = os.path.join(self.root, "plan.under-test.md")
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return gate(self.root, ["--plan", path, *extra])

    def variant(self, old, new, base=GOOD):
        text = base.replace(old, new, 1)
        self.assertNotEqual(base, text, f"fixture lacks {old!r}")
        return text

    def fail_line(self, text, line_id, message="", *extra):
        rc, out = self.check(text, *extra)
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)
        self.assertIn(message, out)
        return out

    def ok(self, text, *extra):
        rc, out = self.check(text, *extra)
        self.assertEqual(0, rc, out)
        return out

    # -- PARALLEL overlap and the Mode line
    def test_parallel_tasks_in_different_blocks_may_share_a_file(self):
        out = self.ok(self.variant("### Task 3: gamma\n\n", "### Task 3: gamma\n\n**Mode:** PARALLEL\n\n"))
        self.assertNotIn("overlap.2-3", out)
        self.assertNotIn("overlap.1-3", out)

    def test_parallel_and_serial_pair_may_share_a_file(self):
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                            "**Mode:** SERIAL\n\n**Files:**\n- Create: `src/alpha.py`")
        out = self.ok(text)
        self.assertIn("PASS plan.mode.2", out)
        self.assertIn("PASS plan.overlap\n", out + "\n")

    def test_mode_with_a_suffix_fails(self):
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                            "**Mode:** PARALLEL (with Task 1)\n\n**Files:**\n- Create: `src/alpha.py`")
        self.fail_line(text, "plan.mode.2", "exactly PARALLEL or SERIAL")

    def test_lowercase_mode_fails(self):
        self.fail_line(self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                               "**Mode:** parallel\n\n**Files:**\n- Create: `src/beta.py`"),
                  "plan.mode.2", "'parallel'")

    def test_overlap_names_the_shared_file(self):
        out = self.fail_line(self.variant("- Create: `src/beta.py`", "- Create: `src/alpha.py`"), "plan.overlap.1-2",
                        "PARALLEL tasks share src/alpha.py")
        self.assertNotIn("tests/test_alpha.py", out)

    def test_unnormalised_paths_do_not_hide_an_overlap(self):
        for spelled in ("./src/alpha.py", "src/../src/alpha.py", "src/alpha.py ", "src//alpha.py", "src/alpha.py:3-4"):
            with self.subTest(spelled):
                text = self.variant("- Create: `src/beta.py`", f"- Create: `{spelled}`")
                self.fail_line(text, "plan.overlap.1-2", "src/alpha.py")

    # -- the Files list
    def test_prose_line_inside_files_list_fails_and_is_quoted(self):
        text = self.variant("- Create: `src/beta.py`\n", "- Create: `src/beta.py`\n- Not modified: `x.yml` (a note)\n")
        self.fail_line(text, "plan.files.2", "- Not modified: `x.yml` (a note)")

    def test_blank_line_inside_files_list_keeps_collecting(self):
        text = self.variant("- Create: `src/beta.py`\n", "- Create: `src/beta.py`\n\n- Modify: `src/alpha.py`\n")
        self.fail_line(text, "plan.overlap.1-2", "src/alpha.py")

    def test_blank_line_before_the_first_files_item_is_skipped(self):
        out = self.ok(self.variant("**Files:**\n- Modify: `src/shared.py`\n", "**Files:**\n\n- Modify: `src/shared.py`\n"))
        self.assertIn("PASS plan.files.3", out)

    def test_files_list_ends_at_a_fence_a_step_or_a_field(self):
        for tail in (f"{FENCE}\nx\n{FENCE}\nprose line\n", "- [ ] **Step 1: go**\nprose line\n", "**Mode:** SERIAL\nprose\n",
                     "## Notes\nprose line\n"):
            with self.subTest(tail):
                text = self.variant("**Files:**\n- Modify: `src/shared.py`\n", f"**Files:**\n- Modify: `src/shared.py`\n{tail}")
                self.ok(text)

    def test_stray_files_after_a_block_heading_do_not_join_the_previous_task(self):
        text = self.variant("## Block b2 — second\n", "## Block b2 — second\n\n**Files:**\n- Create: `src/alpha.py`\n")
        self.ok(text)

    # -- placeholders
    def test_every_placeholder_phrase_fails(self):
        for phrase in PHRASES:
            # the two tokens are exact-case; the phrases are case-insensitive
            spellings = [phrase] if phrase in PLACEHOLDER_TOKENS else [phrase, phrase.upper(), phrase.title()]
            for spelled in spellings:
                with self.subTest(spelled):
                    rc, out = self.check(GOOD + f"\nStep detail: {spelled}\n")
                    self.assertEqual(1, rc, out)
                    self.assertIn("FAIL plan.placeholder.L", out)

    def test_placeholder_line_number_and_text_are_reported(self):
        rc, out = self.check(GOOD + f"\nStep detail: {PHRASES[0]}\n")
        self.assertRegex(out, r"FAIL plan\.placeholder\.L\d+: Step detail: ")

    # -- Findings forms
    def test_backtick_ids_are_accepted(self):
        out = self.ok(self.variant("**Findings:** SD-FX-2", "**Findings:** `SD-FX-2`"), "--source", "register:plan=fx")
        self.assertIn("PASS plan.coverage.SD-FX-2", out)

    def test_none_forms(self):
        for form in ("none - ascii hyphen", "none -- double hyphen", "none — dash"):
            with self.subTest(form):
                out = self.ok(self.variant("none — fixture maintenance with no register row", form))
                self.assertIn("PASS plan.findings.3", out)
        self.fail_line(self.variant("none — fixture maintenance with no register row", "none"), "plan.findings.3",
                  "not register ids")

    def test_non_id_in_a_list_is_named(self):
        out = self.fail_line(self.variant("**Findings:** SD-FX-2", "**Findings:** SD-FX-2, see audit"),
                        "plan.findings.2", "'see audit'")
        self.assertNotIn("plan.id.SD-FX-2", out)    # nothing is routed from a bad line
        self.assertNotIn("plan.id.see", out)

    def test_source_id_in_two_tasks_fails_coverage_with_both_numbers(self):
        text = self.variant("**Findings:** none — fixture maintenance with no register row", "**Findings:** SD-FX-1")
        rc, out = self.check(text, "--source", "register:plan=fx")
        self.assertIn("FAIL plan.coverage.SD-FX-1: in tasks 1, 3", out)

    # -- the register query
    def test_query_grammar_errors_are_messages_not_crashes(self):
        for q, msg in (("plan=fx&&task=1", "bad register query part ''"), ("owner=fx", "bad register query part 'owner=fx'"),
                       ("plan", "bad register query part 'plan'"), ("plan=", "bad register query part 'plan='"),
                       ("plan=fx&task=1&plan=fy", "duplicate register query key 'plan'"),
                       ("task=1,", "empty pattern in register query part 'task=1,'"),
                       ("task=,1", "empty pattern")):
            with self.subTest(q):
                self.fail_line(GOOD, "plan.input", msg, "--source", f"register:{q}")

    def test_query_keys_are_anded_and_patterns_are_ored(self):
        rc, out = self.check(GOOD, "--source", "register:plan=fx&task=2")
        self.assertIn("PASS plan.coverage.SD-FX-2", out)
        self.assertNotIn("coverage.SD-FX-1", out)
        rc, out = self.check(GOOD, "--source", "register:plan=fx,fz&task=1,2")
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertIn("PASS plan.coverage.SD-FX-2", out)
        self.fail_line(GOOD, "plan.source", "yields no register ids", "--source", "register:plan=fx&task=9")

    def test_query_state_and_severity_keys(self):
        rc, out = self.check(GOOD, "--source", "register:state=planned&severity=LOW")
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertNotIn("coverage.SD-FX-2", out)

    # -- report sources
    def test_missing_source_file_fails_with_its_name(self):
        self.fail_line(GOOD, "plan.input", "--source docs/none.md: no such file", "--source", "docs/none.md")

    def test_report_source_matches_rows_by_their_source_field(self):
        rc, out = self.check(GOOD, "--source", "docs/cited.md")
        self.assertIn("PASS plan.coverage.SD-FX-1", out)
        self.assertIn("PASS plan.coverage.SD-FX-2", out)

    def test_report_source_path_forms_and_row_source_suffixes(self):
        rows = rf.load_rows()
        extra = []
        for rid, src, task in (("SD-FX-3", "docs/cited.md §2", "3"), ("SD-FX-4", "docs/cited.md#sec", "4"),
                               ("SD-FX-5", "docs/cited.md:7", "5"), ("SD-FX-6", "docs/cited.mdx", "6"),
                               ("SD-FX-8", "./docs/cited.md", "8"), ("SD-FX-10", "  docs/cited.md", "10"), ("SD-FX-9", "docs/./cited.md#sec", "9"),
                               ("SD-FX-7", "xdocs/cited.md", "7")):
            row = copy.deepcopy(rows["findings"][0])
            row.update(id=rid, source=src)
            row["owner"] = {"kind": "plan", "plan": "other", "task": task}
            extra.append(row)
        rows["findings"] = rows["findings"] + extra
        files = dict(rf.DISPATCH_FILES)
        files["docs/superpowers/plans/fx.md"] = GOOD
        root = rf.make_repo(files, rows)
        self.addCleanup(shutil.rmtree, root, True)
        plan = os.path.join(root, "plan.under-test.md")
        with open(plan, "w", encoding="utf-8") as f:
            f.write(GOOD)
        for spelled in ("docs/cited.md", "./docs/cited.md", os.path.join(root, "docs", "cited.md")):
            with self.subTest(spelled):
                rc, out = gate(root, ["--plan", plan, "--source", spelled])
                for rid in ("SD-FX-3", "SD-FX-4", "SD-FX-5", "SD-FX-8", "SD-FX-9", "SD-FX-10"):
                    self.assertIn(f"FAIL plan.coverage.{rid}: in no task", out)
                for rid in ("SD-FX-6", "SD-FX-7"):
                    self.assertNotIn(rid, out)

    def test_source_given_through_a_symlinked_root(self):
        link = tempfile.mkdtemp(prefix="gate-link-")
        self.addCleanup(shutil.rmtree, link, True)
        os.symlink(self.root, os.path.join(link, "tree"))
        rc, out = self.check(GOOD, "--source", os.path.join(link, "tree", "docs", "cited.md"))
        self.assertIn("PASS plan.coverage.SD-FX-1", out)

    # -- code fences (CommonMark)
    def test_fences_hide_task_headings(self):
        inner = "### Task 9: not a task\n"
        for name, text in (
                ("tilde", f"~~~\n{inner}~~~\n"),
                ("four backticks around three", f"{FENCE}`\n{FENCE}python\n{inner}{FENCE}\n{inner}{FENCE}`\n"),
                ("indented", f"paragraph\n\n  {FENCE}\n{inner}  {FENCE}\n"),
                ("info string does not close", f"{FENCE}\n{FENCE}python\n{inner}{FENCE}\n"),
                ("other character does not close", f"{FENCE}\n~~~\n{inner}{FENCE}\n"),
                ("shorter run does not close", f"{FENCE}`\n{FENCE}\n{inner}{FENCE}`\n"),
                ("longer tilde run closes", f"~~~\n{inner}~~~~\n")):
            with self.subTest(name):
                out = self.ok(GOOD + "\n" + text)
                self.assertNotRegex(out, NO_TASK_9)

    def test_text_after_a_closed_fence_is_parsed_again(self):
        for name, text in (("info string closes nothing", f"{FENCE}\nx\n{FENCE}python\n### Task 9: swallowed\n"),
                           ("indent of four is no fence", f"text\n\n    {FENCE}\n### Task 9: real\n"),
                           ("inline code is no fence", f"{FENCE}inline{FENCE}\n### Task 9: real\n"),
                           ("tilde closed by backticks", f"~~~\nx\n{FENCE}\n### Task 9: swallowed\n")):
            with self.subTest(name):
                rc, out = self.check(GOOD + "\n" + text)
                self.assertEqual("real" in text, "plan.files.9" in out, out)

    def test_list_item_fences(self):
        item = "- an item\n"
        rc, out = self.check(GOOD + f"\n{item}  {FENCE}\n### Task 9: left of the item ends it\n  {FENCE}\n")
        self.assertIn("FAIL plan.files.9", out)
        rc, out = self.check(GOOD + f"\ntext\n\n  {FENCE}\n### Task 9: inside a plain fence\n  {FENCE}\n")
        self.assertNotRegex(out, NO_TASK_9)
        rc, out = self.check(GOOD + f"\n{item}    {FENCE}\n    code\n    {FENCE}\n### Task 9: after\n")
        self.assertIn("FAIL plan.files.9", out)

    def test_a_files_item_holds_exactly_one_backticked_path(self):
        for item, msg in (("- Create: `src/beta.py`, `src/alpha.py`", "a second backticked span"),
                          ("- Create: `src/beta.py` (helper), `src/alpha.py`", "a second backticked span"),
                          ("- Create: `src/beta.py` (`GATES`, `DEFAULT_ARGV`)", "a second backticked span"),
                          ("- Create: `src/beta.py` (and `CLAUDE.md`)", "a second backticked span"),
                          ("- Create: `src/beta.py`, `src/alpha.py", "unclosed backtick"),
                          ("- Create: `src/beta.py` (note `x", "unclosed backtick"),
                          ("- Create: src/beta.py", "no backticked path"),
                          ("- Create:", "no backticked path"),
                          ("- Create: the helper `src/beta.py`", "must start with the backticked path")):
            with self.subTest(item):
                self.fail_line(self.variant("- Create: `src/beta.py`", item), "plan.files.2", msg)

    def test_prose_after_the_path_is_a_comment(self):
        out = self.ok(self.variant("- Create: `src/beta.py`", "- Create: `src/beta.py` (GATES, DEFAULT_ARGV; a (nested) note)"))
        self.assertIn("PASS plan.files.2", out)
        self.fail_line(self.variant("- Create: `src/beta.py`", "- Create: `src/alpha.py` (moved)"), "plan.overlap.1-2", "src/alpha.py")

    def test_a_dot_path_is_not_a_files_item(self):
        for spelled in (".", "./", "a/.."):
            with self.subTest(spelled):
                self.fail_line(self.variant("- Create: `src/beta.py`", f"- Create: `{spelled}`"), "plan.files.2")

    def test_block_heading_forms(self):
        for heading in ("##  Block b7 — second", "##\tBlock b7 — second", "## Block 7 — second", "##  Block  7 — second"):
            with self.subTest(heading):
                out = self.ok(self.variant("## Block b2 — second", heading))
                self.assertIn("PASS plan.block.7", out)
                self.assertNotIn("plan.block.2", out)
        for heading in ("### Block b7 — second", "##Block b7 — second", " ## Block b7 — second"):
            with self.subTest(heading):
                out = self.ok(self.variant("## Block b2 — second", heading))
                self.assertNotIn("plan.block.7", out)

    def test_a_second_mode_line_fails(self):
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/alpha.py`",
                            "**Mode:** PARALLEL\n**Mode:** SERIAL\n\n**Files:**\n- Create: `src/alpha.py`")
        self.fail_line(text, "plan.mode.1", "a second **Mode:** line")

    def test_only_level_two_headings_are_blocks(self):
        for level in ("###", "####", "#"):
            with self.subTest(level):
                out = self.ok(self.variant("### Task 2: beta", f"{level} Block b9 notes\n\n### Task 2: beta"))
                self.assertIn("PASS plan.block.order", out)

    def test_an_unclosed_fence_fails_and_names_its_line(self):
        text = self.variant("## Block b2 — second", f"{FENCE}\n## Block b2 — second").replace(
            "**Findings:** none — fixture maintenance with no register row", "**Findings:** SD-FX-1")
        line = text.split("\n").index("## Block b2 — second")    # the fence sits on the line above it (1-based)
        self.fail_line(text, "plan.fence", f"line {line}: code fence never closed")
        self.assertIn("PASS plan.fence", self.ok(GOOD))

    def test_first_matching_header_line_wins(self):
        out = self.ok(GOOD + "\n**Spec:** `docs/none.md`\n")
        self.assertIn("PASS plan.header.spec-path", out)

    # -- block order and plan-level failures
    def test_tasks_before_the_first_block_heading_fail(self):
        self.fail_line(self.variant("## Block b1 — first\n", ""), "plan.block.order", "before the first 'Block' heading")

    def test_block_numbers_must_increase(self):
        self.fail_line(self.variant("## Block b2 — second", "## Block b1 — second"), "plan.block.order",
                  "'Block b1' does not follow block 1")
        self.fail_line(self.variant("## Block b2 — second", "## Block b2 — second\n\n## Block b2 — again"), "plan.block.order",
                  "'Block b2' does not follow block 2")

    def test_a_heading_cannot_reuse_a_checkpoint_split_block(self):
        text = self.variant("## Block b2 — second\n", "") + "\n## Block b2 — again\n"
        self.fail_line(text, "plan.block.order", "'Block b2' does not follow block 2")

    def test_missing_plan_file_fails(self):
        rc, out = gate(self.root, ["--plan", "docs/none-plan.md"])
        self.assertIn("FAIL plan.file: docs/none-plan.md: no such plan file", out)

    def test_plan_without_tasks_fails(self):
        self.fail_line(GOOD.split("---")[0], "plan.tasks", "no '### Task N' headings")

    def test_plan_and_in_flight_together_fail(self):
        self.fail_line(GOOD, "plan.input", "exactly one of", "--in-flight")

    def test_source_with_in_flight_fails(self):
        rc, out = gate(self.root, ["--in-flight", "--source", "docs/cited.md"])
        self.assertIn("FAIL plan.input: --source goes with --plan", out)


    # -- Siblings, Gates and the pin step (DEC-0016)
    def test_siblings_must_list_every_register_sibling(self):
        out = self.fail_line(self.variant("**Siblings:** sib-a", "**Siblings:** none -- forgot"), "plan.siblings.1", "missing sib-a")
        self.assertIn("PASS plan.siblings.2", out)
        for value in ("sib-ab", "xsib-a", "sib-a-x, y"):
            with self.subTest(value):
                self.fail_line(self.variant("**Siblings:** sib-a", f"**Siblings:** {value}"), "plan.siblings.1", "missing sib-a")
        out = self.ok(self.variant("**Siblings:** sib-a", "**Siblings:** other, sib-a"))
        self.assertIn("PASS plan.siblings.1", out)

    def test_a_sibling_holding_a_comma_matches_verbatim(self):
        rows = rf.load_rows()
        rows["findings"][0]["siblings"] = ["sib-a, part two", "sib-b"]
        files = dict(rf.DISPATCH_FILES)
        root = rf.make_repo(files, rows)
        self.addCleanup(shutil.rmtree, root, True)
        plan = os.path.join(root, "plan.under-test.md")
        for value, rc_want in (("sib-a, part two, sib-b", 0), ("sib-b, sib-a, part two", 0), ("sib-a, sib-b", 1)):
            with self.subTest(value):
                rf.write(root, "plan.under-test.md", self.variant("**Siblings:** sib-a", f"**Siblings:** {value}"))
                rc, out = gate(root, ["--plan", plan])
                self.assertEqual(rc_want, rc, out)

    def test_siblings_line_forms(self):
        self.fail_line(self.variant("**Siblings:** none -- no register row\n", ""), "plan.siblings.3", "no **Siblings:** line")
        for bare in ("none", "None", "NONE"):
            with self.subTest(bare):
                self.fail_line(self.variant("**Siblings:** none -- no register row", f"**Siblings:** {bare}"), "plan.siblings.3",
                               "'none -- <reason>'")
        self.fail_line(self.variant("**Siblings:** sib-a", "**Siblings:** sib-a,, x"), "plan.siblings.1", "empty entry")
        out = self.ok(self.variant("**Siblings:** none -- no register row", "**Siblings:** none -- a reason,, with commas"))
        self.assertIn("PASS plan.siblings.3", out)
        self.fail_line(self.variant("**Siblings:** sib-a", "**Siblings:** sib-a\n**Siblings:** sib-a"), "plan.siblings.1",
                  "a second **Siblings:** line")

    def test_a_none_siblings_line_needs_a_reason_after_a_double_hyphen(self):
        for value in ("none --", "none -- ", "none -", "none - a reason", "None -- r", "NONE -- r", "none — a reason",
                      "none --reason", "none  -- r", "none r"):
            with self.subTest(value):
                self.fail_line(self.variant("**Siblings:** none -- no register row", f"**Siblings:** {value}"),
                               "plan.siblings.3", "'none -- <reason>'")
        out = self.ok(self.variant("**Siblings:** none -- no register row", "**Siblings:** none -- r"))
        self.assertIn("PASS plan.siblings.3", out)
        for value in ("noneish, sib-a", "none_x, sib-a"):
            with self.subTest(value):
                self.assertIn("PASS plan.siblings.1", self.ok(self.variant("**Siblings:** sib-a", f"**Siblings:** {value}")))

    def test_a_none_siblings_line_is_the_empty_list(self):
        for value in ("none -- reason, sib-a", "none -- sib-a", "none -- reason,sib-a"):
            with self.subTest(value):
                self.fail_line(self.variant("**Siblings:** sib-a", f"**Siblings:** {value}"), "plan.siblings.1", "missing sib-a")

    def test_siblings_of_an_unregistered_id_fail_once_on_the_id(self):
        out = self.fail_line(self.variant("**Findings:** SD-FX-2", "**Findings:** SD-FX-404"), "plan.id.SD-FX-404")
        self.assertIn("PASS plan.siblings.2", out)

    def test_gates_names_must_exist(self):
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:** run_all, nosuch\n"), "plan.gates.1", "nosuch")
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:** run_all, beta\n"), "plan.gates.1", "beta")
        self.fail_line(self.variant("**Gates:** run_all\n", ""), "plan.gates.1", "no **Gates:** line")
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:**\n"), "plan.gates.1", "empty")
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:** run_all, ../x\n"), "plan.gates.1", "'../x'")
        for name in ("run_allx", "run_all_x", "Run_all", "run_al"):
            with self.subTest(name):
                self.fail_line(self.variant("**Gates:** run_all\n", f"**Gates:** {name}\n"), "plan.gates.1", repr(name))
        rf.write(self.root, "tools/gates/gate_sub/x.py", "")
        self.addCleanup(shutil.rmtree, os.path.join(self.root, "tools/gates/gate_sub"), True)
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:** sub/x\n"), "plan.gates.1", "'sub/x'")
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:** run_all\n**Gates:** run_all\n"), "plan.gates.1",
                  "a second **Gates:** line")
        out = self.ok(GOOD)
        for n in ("1", "2", "3"):
            self.assertIn(f"PASS plan.gates.{n}", out)

    def test_a_gate_at_the_plan_root_counts(self):
        rf.write(self.root, "tools/gates/gate_rooted.py", "")
        self.addCleanup(os.remove, os.path.join(self.root, "tools/gates/gate_rooted.py"))
        self.ok(self.variant("**Gates:** run_all\n", "**Gates:** rooted\n"))

    def test_a_gate_created_in_a_test_or_modify_item_does_not_count(self):
        text = self.variant("- Test: `tests/test_alpha.py`\n", "- Test: `tests/test_alpha.py`\n- Modify: `tools/gates/gate_mod.py`\n")
        self.fail_line(self.variant("**Gates:** run_all\n", "**Gates:** mod\n", base=text), "plan.gates.1", "mod")

    def test_ids_need_a_pin_step_naming_a_pin_test(self):
        self.fail_line(self.variant("- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.\n", ""),
                  "plan.pin.1", "no '- [ ] **Step N: ... pin ...**' step")
        self.fail_line(self.variant("**Step 2: Mutation run and pin**", "**Step 2: Mutation run**"), "plan.pin.1",
                  "no '- [ ] **Step N: ... pin ...**' step")
        self.fail_line(self.variant("**Step 2: Mutation run and pin**", "**Step 2: Mutation run and p-i-n**"), "plan.pin.1",
                  "no '- [ ] **Step N: ... pin ...**' step")
        self.fail_line(self.variant("`test_alpha::test_alpha_fails`", "`test_alpha_fails`"), "plan.pin.1", "a test named test_<module>::test_<name>")
        self.fail_line(self.variant("`test_alpha::test_alpha_fails`", "`test_alpha::alpha_fails`"), "plan.pin.1", "a test named test_<module>::test_<name>")
        self.fail_line(self.variant("pin test_beta::test_beta_fails", "pin test_beta"), "plan.pin.2", "a test named test_<module>::test_<name>")
        self.fail_line(self.variant("`test_alpha::test_alpha_fails`", "`xtest_alpha::test_alpha_fails`"), "plan.pin.1",
                       "a test named test_<module>::test_<name>")
        self.fail_line(self.variant("`test_alpha::test_alpha_fails`", "`test_alpha::test_`"), "plan.pin.1", "a test named test_<module>::test_<name>")

    def test_pin_step_forms_that_pass(self):
        out = self.ok(GOOD)
        for n in ("1", "2", "3"):
            self.assertIn(f"PASS plan.pin.{n}", out)
        out = self.ok(self.variant("**Step 2: Mutation run and pin**", "**Step 2: PIN the row**"))
        self.assertIn("PASS plan.pin.1", out)
        for title in ("Pin step", "pin: the row", "re-Pin", "Mutation run, pin"):
            with self.subTest(title):
                out = self.ok(self.variant("**Step 2: Mutation run and pin**", f"**Step 2: {title}**"))
                self.assertIn("PASS plan.pin.1", out)

    def test_every_per_task_check_has_the_one_id_form(self):
        """B5-FIX-3 (b): plan.<check>.<N> for every per-task check, Findings and Mode included."""
        out = self.ok(GOOD)
        for n in ("1", "2", "3"):
            for check in PER_TASK:
                self.assertEqual(1, out.count(f"PASS plan.{check}.{n}\n"), f"{check}.{n}: {out}")
        for n in ("1", "2"):
            self.assertEqual(1, out.count(f"PASS plan.mode.{n}\n"), out)
        self.assertNotIn("plan.mode.3", out)    # Task 3 has no Mode line
        self.assertNotIn(".task.", out)
        text = self.variant("**Findings:** SD-FX-2", "**Findings:** see audit")
        text = self.variant("**Mode:** PARALLEL\n\n**Files:**\n- Create: `src/beta.py`",
                            "**Mode:** parallel\n\n**Files:**\n- Create: `src/beta.py`", base=text)
        out = self.fail_line(text, "plan.findings.2: ")
        self.assertIn("FAIL plan.mode.2: ", out)
        self.assertNotIn(".task.", out)

    def test_readme_lists_the_id_forms_and_the_pin_word(self):
        """Sibling 'README gate_plan section' (B5-FIX-3)."""
        with open(os.path.join(rf.REAL, "tools", "gates", "README.md"), encoding="utf-8") as f:
            readme = f.read()
        section = readme[readme.index("## make_review_package, gate_plan (S2)\n"):]
        for word in ("`FAIL plan.mode.N`", "`FAIL plan.findings.N`", "`plan.<check>.<N>`", "`plan.<slug>.<check>.<N>`",
                     "holds the word `pin`", "`Mapping`, `Spinner` and `pinned` do not"):
            self.assertIn(word, section)
        self.assertNotIn("plan.task.", readme)
        self.assertNotIn("title contains `pin`", readme)

    def test_s2_plan_task_6_lists_the_id_forms(self):
        """Sibling 'S2 plan Task 6 text' (B5-FIX-3): Task 6's grammar line states the pin word and the id forms."""
        with open(os.path.join(rf.REAL, "docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md"),
                  encoding="utf-8") as f:
            plan = f.read()
        task6 = plan[plan.index("### Task 6: gate_plan and the ledger contract\n"):plan.index("- [ ] **Step 1: Fixture plan**")]
        for word in ("holds the word `pin`", "`plan.files.N`, `plan.mode.N`, `plan.findings.N`, `plan.siblings.N`, "
                     "`plan.gates.N`, `plan.pin.N`", "`plan.<slug>.<check>.N`"):
            self.assertIn(word, task6)
        self.assertNotIn("title contains `pin`", task6)

    def test_s2_plan_global_constraints_state_the_pin_word(self):
        """R1-2: the S2 plan's Global Constraints grammar line states the same pin rule as Task 6 and PIN_TITLE."""
        with open(os.path.join(rf.REAL, "docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md"),
                  encoding="utf-8") as f:
            lines = [l for l in f.read().splitlines()
                     if l.startswith("- Plan grammar (DEC-0016, DEC-0017; `gate_plan` checks it): ")]
        self.assertEqual(1, len(lines))
        self.assertIn("a step whose title holds the word `pin` (case-insensitive, `\\bpin\\b`", lines[0])
        self.assertIn("B5-FIX-3", lines[0])
        self.assertNotIn("title contains `pin`", lines[0])

    def test_pin_is_a_whole_word_in_the_title(self):
        """B5-FIX-3 (a): 'pin' inside another word names no pin step, so the task has none."""
        for title in ("Mapping table", "Spinner", "PINNED row", "Mutation run and pinning", "pins", "spin_up", "pin2", "Tailspin"):
            with self.subTest(title):
                self.fail_line(self.variant("**Step 2: Mutation run and pin**", f"**Step 2: {title}**"),
                               "plan.pin.1", "no '- [ ] **Step N: ... pin ...**' step")

    def test_the_pin_test_must_be_in_the_pin_step_body(self):
        text = self.variant("- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.\n",
                            "- [ ] **Step 2: Mutation run and pin**\n\n- [ ] **Step 3: Other** — `test_alpha::test_alpha_fails`\n")
        self.fail_line(text, "plan.pin.1", "a test named test_<module>::test_<name>")
        text = self.variant("- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.\n",
                            "- [ ] **Step 2: Mutation run and pin**\n\n## Notes\n`test_alpha::test_alpha_fails`\n")
        self.fail_line(text, "plan.pin.1", "a test named test_<module>::test_<name>")
        text = self.variant("- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.\n",
                            "- [ ] **Step 2: Mutation run and pin**\n\nA later body line names `test_alpha::test_alpha_fails`.\n")
        self.assertIn("PASS plan.pin.1", self.ok(text))

    def test_steps_outside_a_task_and_unclosed_step_titles(self):
        out = self.ok(self.variant("## Block b2 — second\n", "## Block b2 — second\n\n- [ ] **Step 1: Branch** — no task yet\n"))
        self.assertIn("PASS plan.pin.3", out)
        self.fail_line(self.variant("- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.\n",
                                    "- [ ] **Step 2: Mutation run and pin `test_alpha::test_alpha_fails`\n"),
                       "plan.pin.1", "a test named test_<module>::test_<name>")

    def test_a_task_or_block_heading_ends_the_pin_step_body(self):
        text = self.variant("the pin test is `test_alpha::test_alpha_fails`.", "no test here.")
        text = self.variant("### Task 2: beta\n", "### Task 2: beta\n\n`test_alpha::test_alpha_fails`\n", base=text)
        self.fail_line(text, "plan.pin.1", "a test named test_<module>::test_<name>")
        text = self.variant("pin test_beta::test_beta_fails", "pin", base=GOOD)
        text = self.variant("- [ ] **Step 2: CHECKPOINT b1**\n\n## Block b2 — second\n",
                            "## Block b2 — second\n\n`test_beta::test_beta_fails`\n", base=text)
        self.fail_line(text, "plan.pin.2", "a test named test_<module>::test_<name>")

    def test_a_pin_step_of_another_task_does_not_count(self):
        text = self.variant("- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.\n", "")
        text = self.variant("- [ ] **Step 1: CHECKPOINT b2**", "- [ ] **Step 1: pin** `test_alpha::test_alpha_fails`\n\n"
                            "- [ ] **Step 2: CHECKPOINT b2**", base=text)
        self.fail_line(text, "plan.pin.1", "no '- [ ] **Step N")


class Ledgers(unittest.TestCase):
    def repo(self, ledger):
        root = plan_repo(ledger)
        self.addCleanup(shutil.rmtree, root, True)
        return root

    def append(self, root, text):
        with open(os.path.join(root, "docs/superpowers/state/fx/ledger.md"), "a", encoding="utf-8") as f:
            f.write(text)

    def test_line_one_rule_names_the_ledger(self):
        for first in ("# Ledger: fx", "", "plan:docs/superpowers/plans/fx.md", "plan:  x y"):
            with self.subTest(first):
                root = self.repo(first + "\n- T1 DONE\n")
                rc, out = gate(root, ["--in-flight"])
                self.assertEqual(1, rc, out)
                self.assertIn("FAIL plan.input: docs/superpowers/state/fx/ledger.md: line 1 must be 'plan:", out)

    def test_head_closed_but_working_tree_appended_is_in_flight(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n")
        self.assertEqual([], gate_plan.plans_in_flight(root))
        self.append(root, "- T2 DONE\n")
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(root))

    def test_working_tree_closed_but_head_open_is_in_flight(self):
        root = self.repo(LEDGER)
        self.append(root, "CHECKPOINT b1 abc1234\n")
        self.assertEqual(1, len(gate_plan.plans_in_flight(root)))

    def test_only_the_last_line_closes_a_ledger(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n- T2 DONE\n")
        self.assertEqual(1, len(gate_plan.plans_in_flight(root)))
        rc, out = gate(root, ["--in-flight"])
        self.assertIn("PASS plan.fx.header.goal", out)

    def test_trailing_blank_lines_do_not_hide_the_checkpoint(self):
        root = self.repo(LEDGER + "CHECKPOINT b1 abc1234\n\n  \n")
        self.assertEqual([], gate_plan.plans_in_flight(root))

    def test_a_repository_without_a_commit_is_in_flight(self):
        root = tempfile.mkdtemp(prefix="gate-test-")
        self.addCleanup(shutil.rmtree, root, True)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        rf.write_registers(root, rf.load_rows(), "0" * 40)
        rf.write(root, "docs/superpowers/plans/fx.md", GOOD)
        rf.write(root, "docs/cited.md", "cited\n")
        rf.write(root, "docs/superpowers/state/fx/ledger.md", LEDGER + "CHECKPOINT b1 abc1234\n")
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS plan.fx.header.goal", out)

    def test_two_ledgers_are_each_judged(self):
        root = self.repo(LEDGER)
        rf.write(root, "docs/superpowers/state/gx/ledger.md", LEDGER + "CHECKPOINT b1 abc1234\n")
        rf.commit(root, "second ledger")
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(root))

    def test_missing_state_directory_fails(self):
        root = rf.make_repo(dict(rf.DISPATCH_FILES), rf.load_rows())
        self.addCleanup(shutil.rmtree, root, True)
        shutil.rmtree(os.path.join(root, "docs/superpowers/state"), ignore_errors=True)
        rc, out = gate(root, ["--in-flight"])
        self.assertEqual(1, rc, out)
        self.assertIn("FAIL plan.input", out)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_plan -v`
Expected: ERROR `cannot import name 'gate_plan'`.

- [ ] **Step 4: Implement** — `tools/gates/gate_plan.py`:

```python
#!/usr/bin/env python3
"""gate_plan -- an implementation plan is executable under strict delivery (spec §6 step 3).

Checks: writing-plans header blocks; '**Files:**' and '**Findings:**' per task; every routed id registered
and in exactly one task; with --source, every source id in exactly one task; file overlap between PARALLEL
tasks of one block; tasks per block <= config block.max_tasks (blocks: '## Block bN' headings or CHECKPOINT
steps; heading numbers increase; '## Block N' or '## Block bN', level 2); the writing-plans "No Placeholders" list over the whole file, code included.
A Files item ('- Create|Modify|Test|Delete: `path` prose') starts with exactly one backticked span, the path;
the prose after it holds no backtick (DEC-0017). Per task also (DEC-0016): '**Siblings:**' lists every sibling
of the register rows its Findings name; '**Gates:**' names run_all or gates X whose tools/gates/gate_X.py is at
the root or a Create item of this or an earlier task; Findings that name ids need a pin step (a step title that
holds the word 'pin', case-insensitive, whose body names a test test_<module>::test_<name>). Per-task line ids
are all <prefix>.<check>.<N> for the checks files, mode, findings, siblings, gates and pin (B5-FIX-3).
Also owns the per-plan ledger contract the hooks use: plans_in_flight(root).
Usage: gate_plan.py --plan P [--source FILE | --source register:key=pat[,pat][&key=pat]] [--json]
       gate_plan.py --in-flight [--json]
"""
from __future__ import annotations

import argparse
import fnmatch
import glob
import os
import posixpath
import re
import subprocess
import sys
from dataclasses import dataclass, field

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import CODE_ROOT, GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as gconfig  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

STATE = "docs/superpowers/state"
HEADERS = {
    "agentic": re.compile(r"^> \*\*For agentic workers:\*\*"),
    "goal": re.compile(r"^\*\*Goal:\*\* \S"),
    "architecture": re.compile(r"^\*\*Architecture:\*\* \S"),
    "tech-stack": re.compile(r"^\*\*Tech Stack:\*\* \S"),
    "spec": re.compile(r"^\*\*Spec:\*\* \S"),
    "global-constraints": re.compile(r"^## Global Constraints\s*$"),
    "review-focus": re.compile(r"^## Review Focus\s*$"),
}
BLOCK_RE = re.compile(r"^##[ \t]+Block[ \t]+b?(\d+)\b")
FENCE_RE = re.compile(r"^( *)(`{3,}|~{3,})(.*)$")
LIST_ITEM = re.compile(r"^( *)([-*+]|\d{1,9}[.)])( +)\S")
FIELD_RE = re.compile(r"^\*\*[^*\n]+:\*\*")
STEP_RE = re.compile(r"^- \[[ xX]\] \*\*Step\b")
TASK_RE = re.compile(r"^### Task (\d+[A-Za-z]?)\b")
CHECKPOINT_STEP = re.compile(r"^- \[[ xX]\] \*\*Step \d+: CHECKPOINT\b")
FILE_RE = re.compile(r"^- (Create|Modify|Test|Delete):(.*)$")
FILE_PATH = re.compile(r"^`([^`]+)`")
FINDINGS_RE = re.compile(r"^\*\*Findings:\*\*\s*(.*)$")
MODE_RE = re.compile(r"^\*\*Mode:\*\*\s*(.*?)\s*$")
SIBLINGS_RE = re.compile(r"^\*\*Siblings:\*\*\s*(.*?)\s*$")
GATES_RE = re.compile(r"^\*\*Gates:\*\*\s*(.*?)\s*$")
GATE_NAME = re.compile(r"^[a-z0-9_]+$")
STEP_TITLE = re.compile(r"^- \[[ xX]\] \*\*(.*?)\*\*(.*)$")
PIN_TITLE = re.compile(r"\bpin\b", re.I)    # B5-FIX-3: the word, not a substring ("Mapping")
PIN_NAME = re.compile(r"(?<![A-Za-z0-9_])test_[A-Za-z0-9_]+::test_[A-Za-z0-9_]+")
ID_RE = re.compile(r"^[A-Z0-9]+(-[A-Za-z0-9]+)+$")
NONE_RE = re.compile(r"^none\s+(?:—|--|-)\s+\S")
SIBLINGS_NONE = re.compile(r"^none -- \S")
SIBLINGS_NONE_WORD = re.compile(r"^none(?![A-Za-z0-9_])", re.I)
SPEC_PATH = re.compile(r"`([^`]+)`")
TOKEN_RE = re.compile(r"\b[A-Z0-9]+(?:-[A-Za-z0-9]+)+\b")
# writing-plans "No Placeholders"; each phrase is split so this file never matches itself
PLACEHOLDERS = [re.compile(r"\bT" r"BD\b"), re.compile(r"\bTO" r"DO\b")] + [re.compile(p, re.I) for p in (
    r"implement lat" r"er", r"fill in detai" r"ls", r"add appropriate error handl" r"ing",
    r"add valid" r"ation", r"handle edge cas" r"es", r"write tests for the abo" r"ve",
    r"similar to tas" r"k \d+")]
LEDGER_PLAN = re.compile(r"^plan: (\S+)\s*$")
CHECKPOINT_LINE = re.compile(r"^CHECKPOINT b(\d+)\b")
QUERY_KEYS = {
    "plan": lambda f: (f.get("owner") or {}).get("plan"),
    "task": lambda f: (f.get("owner") or {}).get("task"),
    "state": lambda f: f.get("state"),
    "severity": lambda f: f.get("severity"),
}



@dataclass
class Task:
    num: str
    line: int
    block: int
    files: list = field(default_factory=list)
    findings: list | None = None
    findings_error: str = ""
    parallel: bool = False
    files_errors: list = field(default_factory=list)
    mode_error: str = ""
    mode_seen: bool = False
    creates: list = field(default_factory=list)
    siblings: list | None = None
    siblings_value: str = ""
    siblings_error: str = ""
    gates: list | None = None
    gates_error: str = ""
    steps: list = field(default_factory=list)


@dataclass
class Step:
    title: str
    body: list


@dataclass
class Plan:
    headers: dict
    tasks: list
    placeholders: list
    block_errors: list = field(default_factory=list)
    fence_error: str = ""


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def _resolve(root: str, path: str) -> str:
    return path if os.path.isabs(path) else os.path.join(root, path)


def _findings_value(task: Task, value: str) -> None:
    value = value.strip()
    if NONE_RE.match(value):
        task.findings = []
        return
    ids = [v.strip().strip("`") for v in value.split(",")]
    bad = [i for i in ids if not ID_RE.match(i)]
    task.findings = [] if bad else ids
    if bad:
        task.findings_error = f"not register ids: {', '.join(repr(b) for b in bad)} (or write 'none — <reason>')"


def _fence_open(line: str, item_col):
    """(char, run length, base column) when `line` opens a code fence, else None. CommonMark: up to 3 spaces
    of indent, relative to the content column of the list item the line continues; a backtick fence's info
    string holds no backtick."""
    m = FENCE_RE.match(line)
    if not m:
        return None
    indent, run, info = len(m.group(1)), m.group(2), m.group(3)
    if run[0] == "`" and "`" in info:
        return None
    base = item_col if item_col is not None and indent >= item_col else 0
    return (run[0], len(run), base) if indent - base <= 3 else None


def _fence_close(line: str, fence: tuple) -> bool:
    """Same character, a run at least as long as the opener's, no info string, indent <= 3 past the base."""
    m = FENCE_RE.match(line)
    char, size, base = fence
    return bool(m) and not m.group(3).strip() and m.group(2)[0] == char and len(m.group(2)) >= size \
        and len(m.group(1)) <= base + 3


def _file_item(task: Task, line: str) -> bool:
    """A Files item: '- Create|Modify|Test|Delete: ' then exactly one backticked span, the path (a ':' suffix
    is a line range), then prose holding no backtick. Any other shape is a files error (fail closed)."""
    fm = FILE_RE.match(line)
    if not fm:
        return False
    rest = fm.group(2).strip()
    ticks = rest.count("`")
    m = FILE_PATH.match(rest)
    path = posixpath.normpath(m.group(1).split(":")[0].strip()) if m else ""
    if ticks == 0:
        task.files_errors.append(f"no backticked path: {line.strip()[:80]!r}")
    elif ticks % 2:
        task.files_errors.append(f"unclosed backtick: {line.strip()[:80]!r}")
    elif ticks > 2:
        task.files_errors.append(f"a second backticked span (one path per item; symbols go in plain prose): {line.strip()[:80]!r}")
    elif not m:
        task.files_errors.append(f"the item must start with the backticked path: {line.strip()[:80]!r}")
    elif path in ("", "."):
        task.files_errors.append(f"not a path: {line.strip()[:80]!r}")
    else:
        task.files.append(path)
        if fm.group(1) == "Create":
            task.creates.append(path)
    return True


def _siblings_value(task: Task, value: str) -> None:
    if task.siblings is not None:
        task.siblings_error = "a second **Siblings:** line in one task"
        return
    task.siblings_value = value
    if SIBLINGS_NONE.match(value):
        task.siblings = []
        return
    task.siblings = [v.strip() for v in value.split(",")]
    if SIBLINGS_NONE_WORD.match(value):
        task.siblings_error = f"write 'none -- <reason>' (lower-case none, a reason after ' -- ') or a comma-separated list: {value!r}"
    elif "" in task.siblings:
        task.siblings_error = f"empty entry in **Siblings:** {value!r}"


def _gates_value(task: Task, value: str) -> None:
    if task.gates is not None:
        task.gates_error = "a second **Gates:** line in one task"
        return
    task.gates = [v.strip() for v in value.split(",")]
    if "" in task.gates:
        task.gates_error = f"empty gate name in **Gates:** {value!r}"


def _listed(sibling: str, value: str) -> bool:
    """`sibling` is one whole comma-separated entry of `value`, verbatim (an entry may itself hold a comma)."""
    return re.search(r"(?:^|,)\s*" + re.escape(sibling) + r"\s*(?:,|$)", value) is not None


def parse_plan(text: str) -> Plan:
    headers, tasks, placeholders, block_errors = {}, [], [], []
    fence, fence_line, item_col, block, closed, cur, in_files = None, 0, None, 1, False, None, False
    used, first_head, step = 0, None, None
    for n, line in enumerate(text.splitlines(), 1):
        if any(rx.search(line) for rx in PLACEHOLDERS):
            placeholders.append((n, line.strip()[:80]))
        if fence is not None:
            if _fence_close(line, fence):
                fence = None
                continue
            if not (fence[2] and line.strip() and len(line) - len(line.lstrip(" ")) < fence[2]):
                if step is not None:
                    step.body.append(line)    # code inside a step is part of its body
                continue
            fence, item_col = None, None    # a line left of the list item's content ends the item and its fence
        opened = _fence_open(line, item_col)
        if opened:
            fence, in_files, fence_line = opened, False, n
            continue
        if line.strip():
            lm = LIST_ITEM.match(line)
            if lm:
                item_col = len(lm.group(1)) + len(lm.group(2)) + len(lm.group(3))
            elif item_col is not None and len(line) - len(line.lstrip(" ")) < item_col:
                item_col = None
        m = BLOCK_RE.match(line)
        if m:
            num = int(m.group(1))
            if num <= used:
                block_errors.append(f"line {n}: 'Block b{num}' does not follow block {used}")
            used, first_head = max(used, num), first_head or n
            block, closed, cur, in_files, step = num, False, None, False, None
            continue
        m = TASK_RE.match(line)
        if m:
            if closed:
                block, closed = block + 1, False
            used = max(used, block)
            cur, in_files, step = Task(m.group(1), n, block), False, None
            tasks.append(cur)
            continue
        if STEP_RE.match(line):
            sm = STEP_TITLE.match(line)
            step = Step(sm.group(1), [sm.group(2)]) if sm else Step(line, [])
            if cur is not None:
                cur.steps.append(step)
        elif line.startswith("#"):
            step = None
        elif step is not None:
            step.body.append(line)
        if CHECKPOINT_STEP.match(line):
            closed, in_files = True, False
            continue
        for key, rx in HEADERS.items():
            if key not in headers and rx.match(line):
                headers[key] = line
        if cur is None:
            continue
        if in_files:
            if not line.strip():
                continue
            if _file_item(cur, line):
                continue
            if not (line.startswith("#") or FIELD_RE.match(line) or STEP_RE.match(line)):
                cur.files_errors.append(f"not a '- Create|Modify|Test|Delete: `path`' item inside **Files:**: {line.strip()[:80]!r}")
                continue
            in_files = False
        if line.startswith("**Files:**"):
            in_files = True
            continue
        m = FINDINGS_RE.match(line)
        if m:
            _findings_value(cur, m.group(1))
            continue
        m = SIBLINGS_RE.match(line)
        if m:
            _siblings_value(cur, m.group(1))
            continue
        m = GATES_RE.match(line)
        if m:
            _gates_value(cur, m.group(1))
            continue
        m = MODE_RE.match(line)
        if m:
            if cur.mode_seen:
                cur.mode_error = "a second **Mode:** line in one task"
            cur.mode_seen = True
            cur.parallel = m.group(1) == "PARALLEL"
            if m.group(1) not in ("PARALLEL", "SERIAL"):
                cur.mode_error = f"**Mode:** must be exactly PARALLEL or SERIAL, found {m.group(1)!r}"
    if first_head is not None:
        for t in tasks:
            if t.line < first_head:
                block_errors.append(f"line {t.line}: Task {t.num} comes before the first 'Block' heading")
    err = f"line {fence_line}: code fence never closed" if fence is not None else ""
    return Plan(headers, tasks, placeholders, block_errors, err)


def parse_query(query: str) -> dict:
    out = {}
    for part in query.split("&"):
        key, sep, value = part.partition("=")
        if key not in QUERY_KEYS or not value:
            raise GateError(f"bad register query part {part!r}: use key=pattern[,pattern] with key in {sorted(QUERY_KEYS)}")
        if key in out:
            raise GateError(f"duplicate register query key {key!r}: one key=pattern[,pattern] per key")
        if "" in value.split(","):
            raise GateError(f"empty pattern in register query part {part!r}")
        out[key] = value.split(",")
    return out


def source_ids(root: str, source: str, findings: list) -> list[str]:
    if source.startswith("register:"):
        q = parse_query(source[len("register:"):])
        return sorted(f["id"] for f in findings
                      if all(any(fnmatch.fnmatchcase(str(QUERY_KEYS[k](f)), p) for p in pats) for k, pats in q.items()))
    path = _resolve(root, source)
    if not os.path.isfile(path):
        raise GateError(f"--source {source}: no such file")
    known = {f["id"] for f in findings}
    in_text = {t for t in TOKEN_RE.findall(_read(path)) if t in known}
    rel = posixpath.normpath(os.path.relpath(os.path.realpath(path), os.path.realpath(root)).replace(os.sep, "/"))
    by_source = {f["id"] for f in findings if _names(str(f.get("source") or ""), rel)}
    return sorted(in_text | by_source)


def _names(source: str, path: str) -> bool:
    """A register row's `source` names the repo-relative `path`: its first word (up to ' ', '#' or ':'),
    normalised like the argument, equals it."""
    head = re.split(r"[ #:]", source.strip(), maxsplit=1)[0]
    return posixpath.normpath(head) == path


def _verdict(res, lid, ok, reason):
    if ok:
        res.passed(lid)
    else:
        res.fail(lid, reason)


def _fields(res, root: str, pfx: str, t: Task, rows: dict, created: set) -> None:
    """Siblings, Gates and the pin step of one task (DEC-0016). An id with no register row adds no siblings:
    plan.id.<id> already fails it."""
    lid = f"{pfx}.siblings.{t.num}"
    if t.siblings is None:
        res.fail(lid, "no **Siblings:** line ('none -- <reason>' or the register rows' siblings, comma-separated)")
    elif t.siblings_error:
        res.fail(lid, t.siblings_error)
    else:
        need = [s for fid in t.findings or [] for s in (rows.get(fid) or {}).get("siblings") or []]
        missing = [s for s in need if not (t.siblings and _listed(s, t.siblings_value))]
        _verdict(res, lid, not missing, f"missing {', '.join(missing)} (register siblings of {', '.join(t.findings or [])})")
    lid = f"{pfx}.gates.{t.num}"
    if t.gates is None:
        res.fail(lid, "no **Gates:** line (run_all or gate names, comma-separated)")
    elif t.gates_error:
        res.fail(lid, t.gates_error)
    else:
        bad = [g for g in t.gates if g != "run_all" and not (
            GATE_NAME.match(g) and (os.path.isfile(os.path.join(root, "tools", "gates", f"gate_{g}.py"))
                                    or f"tools/gates/gate_{g}.py" in created))]
        _verdict(res, lid, not bad, f"unknown gate {', '.join(repr(g) for g in bad)}: no tools/gates/gate_<name>.py "
                                    "at the root or in a Create item of this or an earlier task")
    lid = f"{pfx}.pin.{t.num}"
    pins = [s for s in t.steps if PIN_TITLE.search(s.title)]
    if not t.findings:
        res.passed(lid)
    elif not pins:
        res.fail(lid, "no '- [ ] **Step N: ... pin ...**' step (the Findings line names ids)")
    else:
        named = any(PIN_NAME.search(line) for s in pins for line in s.body)
        _verdict(res, lid, named, "the pin step body does not name a test named test_<module>::test_<name>")


def check_plan(root: str, conf: dict, findings: list, plan_path: str, source: str | None,
               res: Result, pfx: str) -> None:
    full = _resolve(root, plan_path)
    if not os.path.isfile(full):
        res.fail(f"{pfx}.file", f"{plan_path}: no such plan file")
        return
    plan = parse_plan(_read(full))
    for key, rx in HEADERS.items():
        _verdict(res, f"{pfx}.header.{key}", key in plan.headers, f"no header line matching {rx.pattern!r}")
    if "spec" in plan.headers:
        m = SPEC_PATH.search(plan.headers["spec"])
        _verdict(res, f"{pfx}.header.spec-path", bool(m) and os.path.isfile(_resolve(root, m.group(1))),
                 "the **Spec:** line must name an existing file in backticks")
    if not plan.tasks:
        res.fail(f"{pfx}.tasks", "no '### Task N' headings outside code fences")
    routed = {}
    for t in plan.tasks:
        if t.files_errors:
            res.fail(f"{pfx}.files.{t.num}", t.files_errors[0])
        else:
            _verdict(res, f"{pfx}.files.{t.num}", bool(t.files), "no '- Create|Modify|Test|Delete: `path`' lines under **Files:**")
        if t.mode_seen:
            _verdict(res, f"{pfx}.mode.{t.num}", not t.mode_error, t.mode_error)
        if t.findings is None:
            res.fail(f"{pfx}.findings.{t.num}", "no **Findings:** line (register ids, or 'none — <reason>')")
        else:
            _verdict(res, f"{pfx}.findings.{t.num}", not t.findings_error, t.findings_error)
        for fid in t.findings or []:
            routed.setdefault(fid, []).append(t.num)
    rows = {f["id"]: f for f in findings}
    created = set()
    for t in plan.tasks:
        created.update(t.creates)
        _fields(res, root, pfx, t, rows, created)
    known = set(rows)
    for fid, nums in sorted(routed.items()):
        if fid not in known:
            res.fail(f"{pfx}.id.{fid}", "not in docs/superpowers/state/findings.yml")
        else:
            _verdict(res, f"{pfx}.id.{fid}", len(nums) == 1,
                     f"routed to tasks {', '.join(nums)}; each id belongs to exactly one task")
    _verdict(res, f"{pfx}.fence", not plan.fence_error, plan.fence_error)
    for err in plan.block_errors:
        res.fail(f"{pfx}.block.order", err)
    if not plan.block_errors:
        res.passed(f"{pfx}.block.order")
    cap = conf["block"]["max_tasks"]
    by_block = {}
    for t in plan.tasks:
        by_block.setdefault(t.block, []).append(t.num)
    for b, nums in sorted(by_block.items()):
        _verdict(res, f"{pfx}.block.{b}", len(nums) <= cap,
                 f"{len(nums)} tasks ({', '.join(nums)}) > block.max_tasks {cap}")
    pairs = 0
    for i, a in enumerate(plan.tasks):
        for b in plan.tasks[i + 1:]:
            if not (a.parallel and b.parallel and a.block == b.block):
                continue
            pairs += 1
            shared = sorted(set(a.files) & set(b.files))
            _verdict(res, f"{pfx}.overlap.{a.num}-{b.num}", not shared, f"PARALLEL tasks share {', '.join(shared)}")
    if not pairs:
        res.passed(f"{pfx}.overlap")
    for n, text in plan.placeholders:
        res.fail(f"{pfx}.placeholder.L{n}", text)
    if not plan.placeholders:
        res.passed(f"{pfx}.placeholders")
    if source is None:
        return
    ids = source_ids(root, source, findings)
    if not ids:
        res.fail(f"{pfx}.source", f"{source} yields no register ids")
    for fid in ids:
        nums = routed.get(fid, [])
        _verdict(res, f"{pfx}.coverage.{fid}", len(nums) == 1,
                 "in no task" if not nums else f"in tasks {', '.join(nums)}")


def _closed(text: str) -> bool:
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    return bool(lines) and bool(CHECKPOINT_LINE.match(lines[-1]))


def _committed(root: str, rel: str) -> str:
    """The ledger at HEAD; empty when there is no HEAD or the file is not in it."""
    r = subprocess.run(["git", "show", f"HEAD:{rel}"], cwd=root, capture_output=True, text=True, check=False)
    return r.stdout if r.returncode == 0 else ""


def plans_in_flight(root: str) -> list[tuple[str, str]]:
    """(state-dir name, plan path) for every ledger whose last line is not a committed CHECKPOINT line."""
    state = os.path.join(root, STATE)
    if not os.path.isdir(state):
        raise GateError(f"{STATE}/ missing")
    out = []
    for ledger in sorted(glob.glob(os.path.join(state, "*", "ledger.md"))):
        name = os.path.basename(os.path.dirname(ledger))
        rel = os.path.relpath(ledger, root).replace(os.sep, "/")
        text = _read(ledger)
        first = text.splitlines()[0] if text else ""
        m = LEDGER_PLAN.match(first)
        if not m:
            raise GateError(f"{rel}: line 1 must be 'plan: <plan file>'")
        if not (_closed(text) and _closed(_committed(root, rel))):
            out.append((name, m.group(1)))
    return out


def check(args: list) -> Result:
    p = gate_args("gate_plan")
    p.add_argument("--plan")
    p.add_argument("--source")
    p.add_argument("--in-flight", action="store_true")
    p.add_argument("--json", action="store_true")
    a = p.parse_args(args)
    if bool(a.plan) == a.in_flight:
        raise GateError("give exactly one of --plan P or --in-flight")
    if a.in_flight and a.source:
        raise GateError("--source goes with --plan")
    root = a.root or repo_root()
    conf = gconfig.load(CODE_ROOT)
    findings = registers.load_findings(root)
    res = Result("plan")
    if a.plan:
        check_plan(root, conf, findings, a.plan, a.source, res, "plan")
        return res
    pairs = plans_in_flight(root)
    if not pairs:
        res.passed("plan.in_flight")
    for name, path in pairs:
        check_plan(root, conf, findings, path, None, res, f"plan.{name}")
    return res


def main(argv=None):
    return run_gate("plan", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Register in run_all** — `GATES = ["register", "claims", "decisions", "dispatch", "review_package", "plan"]` (session ruling 2b of b5: `plan` last; the umbrella order with `plan` in the middle is superseded); add `"plan": ["--in-flight"]` to `DEFAULT_ARGV`. Update the two pins in `tools/gates/tests/test_run_all.py`: `test_gates_at_this_block` expects the six-gate list; `test_cli_unknown_gate_is_rc_1` expects `(installed: register, claims, decisions, dispatch, review_package, plan)`.

- [ ] **Step 6: Give the live strict-delivery ledger the contract header** (from this commit, `run_all` checks in-flight plans; line 1 `# strict-delivery ledger` becomes the `plan:` line, every other line stays):

```bash
python3 - <<'EOF'
import os
d = "docs/superpowers/state/strict-delivery"
p = f"{d}/ledger.md"
plan = "plan: docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md\n"
note = "- ledger header added (line numbers shift by one; next handoff resume_line counts it)\n"
text = open(p, encoding="utf-8").read()
first, _, rest = text.partition("\n")
assert first == "# strict-delivery ledger" or first.startswith("plan: "), first
text = plan + rest
if note not in text:
    text += note
os.makedirs(f"{d}/mutations", exist_ok=True)
open(p, "w", encoding="utf-8").write(text)
open(f"{d}/mutations/.gitkeep", "a").close()
os.makedirs(".superpowers/sdd/strict-delivery/review", exist_ok=True)   # git-ignored package root
EOF
head -1 docs/superpowers/state/strict-delivery/ledger.md
```
Expected: the `plan:` line.

- [ ] **Step 7: README + CI** — in `tools/gates/README.md` add this row after the `gate_review_package.py` row of the "Installed gates" table, then append the section below:
```markdown
| `gate_plan.py` | `--plan P [--source report-or-register:query]`: the writing-plans header blocks, `**Files:**` (one backticked path per item), `**Findings:**`, `**Siblings:**`, `**Gates:**` and a pin step per task, every routed id registered and in exactly one task, source-id coverage, PARALLEL file overlap, tasks per block <= `block.max_tasks`, no placeholder text; `--in-flight` (run_all's default): the same checks, without a source, on the plan named on line 1 of every ledger that is in flight |
```
```markdown
## make_review_package, gate_plan (S2)

`python3 tools/gates/make_review_package.py <plan> <task> <base> <head> [--only gate,gate]` writes `commits.txt`,
`diff.patch` and `gates.txt` (`run_all` at HEAD minus `review_package` and `merge_ready`) into the git-ignored
`.superpowers/sdd/<plan>/review/<task>/`, in the forms `gate_review_package` reads whatever the caller's git
config says; it refuses a dirty tree, because `gates.txt` must describe HEAD. The implementer adds
`mutation-<id>.txt`, `siblings.md` and `claims.md`; `gate_review_package.py --dir <that directory>` must pass
before a reviewer is dispatched.

`gate_plan.py --plan P --source <report | register:key=pat[,pat][&key=pat]>` prints the header, id-coverage, overlap,
block-size and placeholder lines at plan approval. Register keys: `plan` (owner.plan), `task` (owner.task), `state`,
`severity`; patterns are fnmatch; a repeated key or an empty pattern is `FAIL plan.input`. A report `--source` file is
normalised to a repo-relative path and matches a register row whose `source`, up to its first ` `, `#` or `:`, normalises to that path. The S2 plan's approval check is
`--source 'register:plan=strict-delivery&task=S2-T*'` (`task=S2-*` also matches `B4-FIX-1`, whose owner.task is
`S2-b4-fixwave`). Blocks are `## Block bN` headings, else the tasks up to a `CHECKPOINT` step; a heading number that does
not increase, or a task above the first heading, is `FAIL plan.block.order`. A `**Files:**` list runs to the next
`**Field:**` line, heading, step line or fence; blank lines inside it are skipped and any other non-item line is
`FAIL plan.files.N`. File paths are compared after `posixpath.normpath`; a `**Mode:**` line other than exactly
`PARALLEL` or `SERIAL`, or a second one in a task, is `FAIL plan.mode.N`; a missing `**Findings:**` line or one that
is neither register ids nor `none — <reason>` is `FAIL plan.findings.N`. A Files item (DEC-0017) is
`- Create|Modify|Test|Delete: ` then exactly one backticked span, the path (a `:` suffix is a line range), then prose
holding no backtick: an item with no backticked path, an unclosed backtick, a second backticked span or text before
the path is `FAIL plan.files.N` (an item naming two files is two items; symbols go in plain prose). Per task
(DEC-0016): `**Siblings:** none -- <reason>` (lower-case `none`, a literal ` -- `, a non-empty reason; it is the empty
list; any other value starting with `none`, in any case, not followed by a letter, digit or `_`, fails) or a comma-separated list holding, as whole
entries, every `siblings` entry of the register rows the task's Findings name (an id with no row adds nothing:
`plan.id.<id>` fails it), else `FAIL plan.siblings.N`; `**Gates:**` names, comma-separated, each exactly `run_all` or an X for which `tools/gates/gate_X.py`
is at the root or a `Create` item of this or an earlier task, else `FAIL plan.gates.N`; a task whose Findings names
ids has a step `- [ ] **Step N: ...**` whose title holds the word `pin` (case-insensitive, `\bpin\b`: `Pin step` and
`pin:` count, `Mapping`, `Spinner` and `pinned` do not) and whose body (up to the next
step or heading, code included) names a test named `test_<module>::test_<name>`,
else `FAIL plan.pin.N`; a missing or second Siblings or Gates line fails the same line. Code fences follow CommonMark (backtick or tilde, up to 3 spaces of
indent relative to the list item, closed by the same character, a run at least as long and no info string); a fence still open at the end of the file is `FAIL plan.fence`. Per-task line ids have one form (B5-FIX-3),
`plan.<check>.<N>` (`plan.<slug>.<check>.<N>` under `--in-flight`), for the checks `files`, `mode`, `findings`,
`siblings`, `gates` and `pin`. A block heading is `## Block N` or `## Block bN` (level 2; spaces or tabs after `##` and before the number).

Ledger contract: `docs/superpowers/state/<plan>/ledger.md` line 1 is `plan: <plan file>`; a line `CHECKPOINT b<N> ...`
ends a block. A plan is in flight unless the last non-empty line of the ledger is a `CHECKPOINT` line both in the
working tree and in `HEAD` (an uncommitted ledger is in flight); `--in-flight` checks every plan in flight, and a
ledger without the `plan:` line is `FAIL plan.input`.
```
CI: `grep -n unittest .github/workflows/gates.yml` prints lines 30 and 32, both `unittest discover -v -s tools/gates/tests -t .`: the new test modules are found without a list, so `.github/workflows/gates.yml` is not edited.

- [ ] **Step 8: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_gate_plan -v` → `OK` (91 tests; 95 after the b5 fix wave B5-FIX-2/B5-FIX-3, whose suite is 562 on both interpreters and whose `run_all` adds one `dispatch.config.fable_fallback` line: 132 before the S2-b5-fixwave package exists; 96 and a suite of 564 after its review round 1); the full suite 544 tests on both interpreters (`PYTHONDONTWRITEBYTECODE=1 /usr/bin/python3 -W error -m unittest discover -s tools/gates/tests -t .` and the same with `tools/.venv/bin/python3`). Then `python3 tools/gates/run_all.py` → only `PASS`: 120 lines (51 before this task + 69 `plan.strict-delivery.*` lines for this very plan file), 131 once the S2-T6 package exists. Then the approval check: `python3 tools/gates/gate_plan.py --plan docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md --source 'register:plan=strict-delivery&task=S2-T*'` → only `PASS` (each SD-S2-Tn in exactly one task; `task=S2-*` would also match `B4-FIX-1`, owner.task `S2-b4-fixwave`, and print `FAIL plan.coverage.B4-FIX-1: in no task`).

- [ ] **Step 9: Commit**

```bash
git add tools/gates/gate_plan.py tools/gates/tests/fixtures/plan/good.md tools/gates/tests/test_gate_plan.py \
        tools/gates/run_all.py tools/gates/tests/test_run_all.py tools/gates/README.md \
        docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md docs/superpowers/state/strict-delivery
git commit -m "feat(gates): gate_plan -- headers, findings per task, id coverage, PARALLEL overlap, block cap, placeholders; ledger contract"
```

- [ ] **Step 10: Mutation run and pin**

```bash
export PYTHONDONTWRITEBYTECODE=1
T=tools.gates.tests.test_gate_plan.GatePlan.test_parallel_tasks_sharing_a_file_fail
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/shared = sorted\(set\(a\.files\) & set\(b\.files\)\)/shared = []/' tools/gates/gate_plan.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/gate_plan.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T6/mutation-SD-S2-T6.txt` (the package copy):
```
finding: SD-S2-T6
test: test_parallel_tasks_sharing_a_file_fail
step 1 (fix present): python3 -m unittest tools.gates.tests.test_gate_plan.GatePlan.test_parallel_tasks_sharing_a_file_fail -> PASS
step 2 (fix reverted: perl -pi -e 's/shared = sorted\(set\(a\.files\) & set\(b\.files\)\)/shared = []/' tools/gates/gate_plan.py empties the shared-file set): python3 -m unittest tools.gates.tests.test_gate_plan.GatePlan.test_parallel_tasks_sharing_a_file_fail -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_gate_plan.GatePlan.test_parallel_tasks_sharing_a_file_fail -> PASS
```
Write `siblings.md` (header `| sibling | test or finding |` and its separator row; the register row's list, header-only when it is empty) and `claims.md` (`| claim | direct | contradiction | output | verdict |`, every row PASS) beside it. On a clean tree build and gate the package:
```bash
python3 tools/gates/make_review_package.py strict-delivery S2-T6 <base> <head>
python3 tools/gates/gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-T6   # only PASS
python3 tools/gates/run_all.py                                                                 # only PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T6.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T6.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T6/mutation-SD-S2-T6.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T6.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T6.md
echo "- S2-T6 DONE <task head sha> gate_plan; review docs/superpowers/state/strict-delivery/reviews/S2-T6.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T6"
PIN = {"test": "test_gate_plan::test_parallel_tasks_sharing_a_file_fail",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T6.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T6.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T6.txt docs/superpowers/state/strict-delivery/reviews/S2-T6.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T6 pinned; ledger S2-T6"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 11: Ledger (controller, after the task review is ✅)** — the line `- S2-T6 DONE <task head sha> gate_plan; review docs/superpowers/state/strict-delivery/reviews/S2-T6.md` goes to `docs/superpowers/state/strict-delivery/ledger.md` in the pin commit above (`chore(state): SD-S2-T6 pinned; ledger S2-T6`); no separate ledger commit.

### b5 checkpoint

- [ ] **Step 1: CHECKPOINT b5** — the b4 checkpoint sequence, with these values: `python3 tools/gates/gate_review_package.py --all` → only `PASS` (packages stay git-ignored); handoff `branch: block/strict-delivery-b5`, `head` = `git rev-parse HEAD` before the state commit, `base` = `git merge-base HEAD origin/main`, `block: 5`, `next_task: "7"`, `findings_in_flight: [{SD-S2-T4: pinned}, {SD-S2-T5: pinned}, {SD-S2-T6: pinned}]`, `resume_line` = the line of `- b5 start`; ledger `CHECKPOINT b5 <head sha>`; commit `chore(state): checkpoint b5`; push `block/strict-delivery-b5`; PR; all checks green; merge-readiness by hand; push the annotated tag `strict-delivery-b5-tip` at the branch tip before the merge deletes the branch (DEC-0015); squash-merge; tag `strict-delivery-b5`; clean worktrees; status update; this resume prompt; stop:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 6 (sub-plan S2, task 7) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

## Block b6 — checkpoint gate, stop hooks, state layout (Tasks 7–9; branch `block/strict-delivery-b6`; tag `strict-delivery-b6`)

### b6 preamble (controller, before Task 7)

- [ ] **Step 1: Branch** — `git checkout main && git pull --ff-only && git checkout -b block/strict-delivery-b6`; append `- b6 start <sha>` to the ledger; `python3 tools/gates/gate_register.py` → `PASS` (rows `SD-S2-T1..T6` stay `pinned` until S4's `postmerge.py`); commit `chore(state): b6 start`.

### Task 7: gate_checkpoint — pre-merge and post-merge

**Files:**
- Create: `tools/gates/gate_checkpoint.py`
- Modify: `tools/gates/tests/repo_fixture.py` (append fake_gh)
- Modify: `tools/gates/run_all.py` (GATES, DEFAULT_ARGV)
- Test: `tools/gates/tests/test_gate_checkpoint.py`

**Findings:** SD-S2-T7

**Siblings:** none -- the register row SD-S2-T7 lists no siblings

**Gates:** run_all, checkpoint

**Interfaces:**
- Consumes: `gate_dispatch.claim_fresh` (Task 1); `gate_plan.{STATE, CHECKPOINT_LINE, LEDGER_PLAN, parse_plan}` (Task 6); S1 `gate_args`, `CODE_ROOT`, `registers.{load_*, finding_by_id, open_decisions, grant_for}`, `gitinfo.{head_sha, branch, status_clean, worktrees}`.
- Produces: `gate_checkpoint.check(args) -> Result`, `main(argv=None)`; `front_matter(path) -> dict`; `KEYS` (umbrella handoff keys); `open_prs(root) -> list[dict]`. CLI (umbrella table): `--plan <slug>` (pre-merge), `--plan <slug> --post` (post-merge), `--structure` (CI-safe; run_all's default).
- **The two moments (umbrella "Handoff"; `main` is protected, so the handoff is committed on the block branch before the merge):**
  - *pre-merge* (`--plan`): checkout on `handoff.branch` (a block branch, never `main`); `handoff.head` is an ancestor of `HEAD` and `git diff --name-only head HEAD` lists only paths under `docs/superpowers/state/` (the handoff commit; a commit cannot name its own sha, so `head == HEAD` is impossible once the handoff is committed); `base` is an ancestor of `head`. The block's own PR is open by design and worktrees are removed after the merge, so neither is checked here.
  - *post-merge* (`--plan --post`): checkout on `main`; `git rev-parse main` has exactly one parent and it is `handoff.base`; `main`'s tree differs from `handoff.head`'s only under `docs/superpowers/state/` (the squashed handoff commit); `git worktree list` shows the main checkout alone; local branches are `main` plus `parked_branches`; no open PR whose head is `<slug>/…` or `handoff.branch`.
  - *both*: front matter keys, paths, shas; finding states equal the register; claims fresh at HEAD (`claim_fresh`); `open_decisions` equals the register's open set; rulings ≤ `rulings.max_per_block`; `grant` is the answered `grant:<slug>`; `git status --porcelain` empty; `live_subagents` empty; a `CHECKPOINT b<block>` ledger line after `resume_line`; `next_task` is `none` exactly when `block` is the plan's last.
  - *structure*: every state dir's layout (ledger line 1 `plan: <existing file>`, `mutations/`) and, where a handoff exists, keys, paths, shas, ids, rulings cap and grant — none of the moment checks.
- Line ids (prefix `checkpoint`; structure mode `checkpoint.<slug>`): `handoff`, `keys`, `path.{plan,spec,ledger}`, `head.commit`, `base.commit`, `numbers`, `rulings`, `env`, `parked`, `finding.<id>`, `claim.<id>`, `decision.<id>`, `open_decisions`, `grant`, `branch`, `head`, `base`, `post.parent`, `post.tree`, `worktrees`, `branches`, `prs`, `status`, `subagents`, `ledger`, `next_task`, `moment`, `layout`.

- [ ] **Step 1: Test helper** — append to `tools/gates/tests/repo_fixture.py`:

```python
def fake_gh(dirpath: str) -> str:
    """Directory holding a `gh` that prints $FAKE_GH_JSON (default []), for gates that list PRs."""
    os.makedirs(dirpath, exist_ok=True)
    path = os.path.join(dirpath, "gh")
    with open(path, "w", encoding="utf-8") as f:
        f.write('#!/bin/sh\nprintf \'%s\\n\' "${FAKE_GH_JSON:-[]}"\n')
    os.chmod(path, 0o755)
    return dirpath
```

- [ ] **Step 2: Write the failing tests** — `tools/gates/tests/test_gate_checkpoint.py`:

```python
"""gate_checkpoint through main() (strict-delivery S2 Task 7): pre-merge on the block branch, post-merge
after a simulated squash merge, and the CI-safe structure mode.
Run from the repo root: python3 -m unittest tools.gates.tests.test_gate_checkpoint -v
"""
import contextlib
import io
import json
import os
import tempfile
import unittest
from unittest import mock

import yaml

from tools.gates import gate_checkpoint
from tools.gates.lib import config
from tools.gates.tests import repo_fixture as rf

with open(os.path.join(rf.FIX, "plan", "good.md"), encoding="utf-8") as _f:
    PLAN = _f.read()
STATE = "docs/superpowers/state/fx"
BIN = rf.fake_gh(tempfile.mkdtemp(prefix="gates-gh-"))
BLOCK_BRANCH = "fx/b1"


def cp_repo(checkpoint=True, drop=(), **over):
    """Plan fx, block 1 of 2, at its pre-merge checkpoint: one task commit on fx/b1, then the handoff and
    the CHECKPOINT line committed on fx/b1 (state-only). Returns the repo, checked out on fx/b1."""
    files = dict(rf.DISPATCH_FILES)
    files.update({"docs/superpowers/plans/fx.md": PLAN, "docs/superpowers/specs/fx.md": "# fx spec\n",
                  f"{STATE}/mutations/.gitkeep": "",
                  f"{STATE}/ledger.md": "plan: docs/superpowers/plans/fx.md\n# Ledger: fx\n- T1 DONE\n"})
    root = rf.make_repo(files, rf.load_rows())
    base = rf.git(root, "rev-parse", "HEAD")
    rf.git(root, "checkout", "-q", "-b", BLOCK_BRANCH)
    rf.write(root, "src/fx.py", "fixed = True\n")
    head = rf.commit(root, "feat: fixture task 1")
    fm = {"plan": "docs/superpowers/plans/fx.md", "spec": "docs/superpowers/specs/fx.md", "branch": BLOCK_BRANCH,
          "head": head, "base": base, "ledger": f"{STATE}/ledger.md", "resume_line": 2, "block": 1,
          "next_task": "3", "findings_in_flight": [{"SD-FX-1": "planned"}], "claims_verified_at_head": ["C-fx.ok"],
          "live_subagents": [], "open_decisions": ["DEC-0002"], "rulings": [], "grant": "DEC-0001",
          "parked_branches": [],
          "env": {"idf": "source tools/idf-env.sh", "flash": "owner says ready", "ota_window_s": 35},
          "traps": []}
    fm.update(over)
    for key in drop:
        fm.pop(key)
    rf.write(root, f"{STATE}/handoff.md", "---\n" + yaml.safe_dump(fm, sort_keys=False) + "---\n\nfixture\n")
    if checkpoint:
        with open(os.path.join(root, STATE, "ledger.md"), "a", encoding="utf-8") as f:
            f.write(f"CHECKPOINT b{fm.get('block', 1)} {head}\n")
    rf.commit(root, "chore(state): checkpoint")
    return root


def squash(root, parent=None):
    """Simulate GitHub's squash merge of fx/b1 onto main, then delete the block branch."""
    tip = rf.git(root, "rev-parse", "HEAD")
    base = parent or rf.git(root, "rev-parse", "main")
    merged = rf.git(root, "commit-tree", f"{tip}^{{tree}}", "-p", base, "-m", "fx block 1 (#1)")
    rf.git(root, "checkout", "-q", "main")
    rf.git(root, "reset", "-q", "--hard", merged)
    rf.git(root, "branch", "-D", BLOCK_BRANCH)


def gate(root, argv, gh_json="[]"):
    env = {"PATH": BIN + os.pathsep + os.environ["PATH"], "FAKE_GH_JSON": gh_json}
    buf = io.StringIO()
    with mock.patch.dict(os.environ, env), contextlib.redirect_stdout(buf):
        rc = gate_checkpoint.main(["--root", root, *argv])
    return rc, buf.getvalue()


class Base(unittest.TestCase):
    def repo(self, **kw):
        root = cp_repo(**kw)
        self.addCleanup(rf.remove, root)
        return root

    def assertFailLine(self, rc, out, line_id):
        self.assertEqual(1, rc, out)
        self.assertIn(f"FAIL {line_id}", out)


class PreMerge(Base):
    def pre(self, root, gh_json="[]"):
        return gate(root, ["--plan", "fx"], gh_json)

    def test_clean_pre_merge_checkpoint_passes(self):
        rc, out = self.pre(self.repo())
        self.assertEqual(0, rc, out)
        self.assertNotIn("FAIL", out)

    def test_the_blocks_own_open_pr_is_allowed_before_merge(self):
        rc, out = self.pre(self.repo(), json.dumps([{"number": 7, "headRefName": BLOCK_BRANCH}]))
        self.assertEqual(0, rc, out)

    def test_state_only_commits_after_head_pass(self):
        root = self.repo()
        with open(os.path.join(root, STATE, "ledger.md"), "a", encoding="utf-8") as f:
            f.write("- note after the checkpoint\n")
        rf.commit(root, "chore(state): note")
        rc, out = self.pre(root)
        self.assertEqual(0, rc, out)

    def test_sha_mismatch_fails(self):
        root = self.repo()
        rf.write(root, "src/late.py", "late = True\n")
        rf.commit(root, "feat: code after the checkpoint")
        rc, out = self.pre(root)
        self.assertFailLine(rc, out, "checkpoint.head")
        self.assertIn("src/late.py", out)

    def test_head_not_an_ancestor_fails(self):
        root = self.repo()
        rf.git(root, "reset", "-q", "--soft", "HEAD~2")
        rf.commit(root, "the block rewritten as one commit")
        rc, out = self.pre(root)
        self.assertFailLine(rc, out, "checkpoint.head")

    def test_checkout_on_another_branch_fails_pre_merge(self):
        root = self.repo()
        rf.git(root, "checkout", "-q", "-b", "elsewhere")
        self.assertFailLine(*self.pre(root), "checkpoint.branch")

    def test_handoff_naming_main_fails_pre_merge(self):
        self.assertFailLine(*self.pre(self.repo(branch="main")), "checkpoint.branch")

    def test_dirty_status_fails(self):
        root = self.repo()
        rf.write(root, "stray.txt", "x\n")
        self.assertFailLine(*self.pre(root), "checkpoint.status")

    def test_live_subagent_fails(self):
        self.assertFailLine(*self.pre(self.repo(live_subagents=["agent-7"])), "checkpoint.subagents")

    def test_finding_state_mismatch_fails(self):
        self.assertFailLine(*self.pre(self.repo(findings_in_flight=[{"SD-FX-1": "pinned"}])),
                            "checkpoint.finding.SD-FX-1")

    def test_claim_not_verified_at_head_fails(self):
        self.assertFailLine(*self.pre(self.repo(claims_verified_at_head=["C-fx.stale"])), "checkpoint.claim.C-fx.stale")

    def test_open_decisions_must_match_register(self):
        self.assertFailLine(*self.pre(self.repo(open_decisions=[])), "checkpoint.open_decisions")

    def test_rulings_over_cap_fail(self):
        cap = config.load(rf.REAL)["rulings"]["max_per_block"]
        root = self.repo(rulings=[f"Ruling {i}: fixture — why — cost — §3" for i in range(cap + 1)])
        self.assertFailLine(*self.pre(root), "checkpoint.rulings")

    def test_missing_checkpoint_line_fails(self):
        self.assertFailLine(*self.pre(self.repo(checkpoint=False)), "checkpoint.ledger")

    def test_next_task_must_be_none_at_last_block(self):
        self.assertFailLine(*self.pre(self.repo(block=2, next_task="4")), "checkpoint.next_task")
        rc, out = self.pre(self.repo(block=2, next_task="none"))
        self.assertEqual(0, rc, out)

    def test_next_task_none_before_last_block_fails(self):
        self.assertFailLine(*self.pre(self.repo(next_task="none")), "checkpoint.next_task")

    def test_wrong_grant_fails(self):
        self.assertFailLine(*self.pre(self.repo(grant="DEC-0002")), "checkpoint.grant")

    def test_missing_key_fails(self):
        self.assertFailLine(*self.pre(self.repo(drop=("traps",))), "checkpoint.keys")

    def test_missing_handoff_fails(self):
        root = self.repo()
        os.remove(os.path.join(root, STATE, "handoff.md"))
        rf.commit(root, "chore(state): drop handoff")
        self.assertFailLine(*self.pre(root), "checkpoint.handoff")


class PostMerge(Base):
    def merged(self, **kw):
        root = self.repo(**kw)
        squash(root)
        return root

    def post(self, root, gh_json="[]"):
        return gate(root, ["--plan", "fx", "--post"], gh_json)

    def test_squash_merged_checkpoint_passes(self):
        rc, out = self.post(self.merged())
        self.assertEqual(0, rc, out)
        self.assertIn("PASS checkpoint.post.tree", out)

    def test_post_tree_mismatch_fails(self):
        root = self.repo()
        rf.write(root, "src/late.py", "late = True\n")
        rf.commit(root, "feat: pushed after the handoff")
        squash(root)
        rc, out = self.post(root)
        self.assertFailLine(rc, out, "checkpoint.post.tree")
        self.assertIn("src/late.py", out)

    def test_post_parent_mismatch_fails(self):
        root = self.repo()
        squash(root, parent=rf.git(root, "rev-list", "--max-parents=0", "HEAD"))
        self.assertFailLine(*self.post(root), "checkpoint.post.parent")

    def test_post_merge_must_run_on_main(self):
        root = self.merged()
        rf.git(root, "checkout", "-q", "-b", "elsewhere")
        self.assertFailLine(*self.post(root), "checkpoint.branch")

    def test_post_extra_worktree_fails(self):
        root = self.merged()
        wt = tempfile.mkdtemp(prefix="gates-wt-")
        os.rmdir(wt)
        rf.git(root, "worktree", "add", "-q", "--detach", wt)
        self.addCleanup(rf.remove, wt)
        self.assertFailLine(*self.post(root), "checkpoint.worktrees")

    def test_post_unlisted_branch_fails_and_parked_branch_passes(self):
        root = self.merged()
        rf.git(root, "branch", "fx-parked")
        self.assertFailLine(*self.post(root), "checkpoint.branches")
        root = self.merged(parked_branches=[{"name": "fx-parked", "reason": "fixture", "owner": "issue #1"}])
        rf.git(root, "branch", "fx-parked")
        rc, out = self.post(root)
        self.assertEqual(0, rc, out)

    def test_post_open_pr_from_plan_fails(self):
        root = self.merged()
        self.assertFailLine(*self.post(root, json.dumps([{"number": 9, "headRefName": "fx/b2"}])), "checkpoint.prs")
        rc, out = self.post(root, json.dumps([{"number": 8, "headRefName": "other-work"}]))
        self.assertEqual(0, rc, out)

    def test_post_dirty_status_fails(self):
        root = self.merged()
        rf.write(root, "stray.txt", "x\n")
        self.assertFailLine(*self.post(root), "checkpoint.status")


class Structure(Base):
    def test_structure_mode_passes_and_ignores_moment_checks(self):
        root = self.repo()
        rf.write(root, "stray.txt", "x\n")
        rc, out = gate(root, ["--structure"])
        self.assertEqual(0, rc, out)
        self.assertIn("PASS checkpoint.fx.layout", out)

    def test_structure_mode_fails_on_a_broken_handoff(self):
        self.assertFailLine(*gate(self.repo(drop=("traps",)), ["--structure"]), "checkpoint.fx.keys")

    def test_no_mode_fails(self):
        self.assertFailLine(*gate(self.repo(), []), "checkpoint.input")

    def test_post_without_plan_fails(self):
        self.assertFailLine(*gate(self.repo(), ["--structure", "--post"]), "checkpoint.input")

    def test_registered_in_run_all(self):
        from tools.gates import run_all
        self.assertIn("checkpoint", run_all.GATES)
        root = self.repo()
        res = run_all.run(["checkpoint"], {"root": root})
        self.assertTrue(res.ok(), res.render(False))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_checkpoint -v`
Expected: ERROR `cannot import name 'gate_checkpoint'`.

- [ ] **Step 4: Implement** — `tools/gates/gate_checkpoint.py`:

```python
#!/usr/bin/env python3
"""gate_checkpoint -- a plan's handoff agrees with git, GitHub and the registers at a block boundary.

Handoff format and failure rules: docs/superpowers/plans/2026-10-17-strict-delivery.md, "Handoff".
main is protected, so the handoff is committed on the block branch before the merge. Two moments:
  pre-merge (--plan):         on handoff.branch; HEAD = handoff.head plus state-only commits.
  post-merge (--plan --post): on main; main's only parent is handoff.base; main's tree differs from
                              handoff.head's only under docs/superpowers/state/; one worktree; local
                              branches main + parked; no open PR from <slug>/... or handoff.branch.
Both: clean status, no live subagents, finding states, fresh claims, open decisions, rulings cap, grant,
the ledger's CHECKPOINT b<block> line after resume_line, next_task.
Usage: gate_checkpoint.py --plan SLUG [--post] [--root R] [--json]
       gate_checkpoint.py --structure [--root R] [--json]     CI-safe subset over every state dir
"""
from __future__ import annotations

import glob
import json
import os
import subprocess
import sys

import yaml

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.gate_dispatch import claim_fresh  # noqa: E402
from tools.gates.gate_plan import CHECKPOINT_LINE, LEDGER_PLAN, STATE, parse_plan  # noqa: E402
from tools.gates.lib import CODE_ROOT, GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as gconfig  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

KEYS = ["plan", "spec", "branch", "head", "base", "ledger", "resume_line", "block", "next_task",
        "findings_in_flight", "claims_verified_at_head", "live_subagents", "open_decisions", "rulings",
        "grant", "parked_branches", "env", "traps"]
ENV_KEYS = ["idf", "flash", "ota_window_s"]


def _read(path: str) -> str:
    with open(path, encoding="utf-8") as f:
        return f.read()


def _git(root: str, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, check=False)


def _is_commit(root: str, sha: str) -> bool:
    return bool(sha) and _git(root, "cat-file", "-e", f"{sha}^{{commit}}").returncode == 0


def _ancestor(root: str, a: str, b: str) -> bool:
    return _git(root, "merge-base", "--is-ancestor", a, b).returncode == 0


def _outside_state(root: str, a: str, b: str) -> list[str]:
    r = _git(root, "diff", "--name-only", a, b)
    if r.returncode != 0:
        raise GateError(f"git diff {a[:12]} {b[:12]}: {r.stderr.strip()}")
    return [p for p in r.stdout.splitlines() if p and not p.startswith(STATE + "/")]


def _verdict(res, lid, ok, reason):
    if ok:
        res.passed(lid)
    else:
        res.fail(lid, reason)


def front_matter(path: str) -> dict:
    text = _read(path)
    if not text.startswith("---\n"):
        raise GateError(f"{path}: first line must be '---' (YAML front matter)")
    end = text.find("\n---\n", 3)
    if end < 0:
        raise GateError(f"{path}: front matter is not closed by a '---' line")
    doc = yaml.safe_load(text[4:end + 1])
    if not isinstance(doc, dict):
        raise GateError(f"{path}: front matter is not a mapping")
    return doc


def _pairs(items) -> list[tuple[str, str]]:
    out = []
    for it in items or []:
        if isinstance(it, dict):
            out.extend((str(k), str(v)) for k, v in it.items())
        else:
            key, _, value = str(it).partition(":")
            out.append((key.strip(), value.strip()))
    return out


def open_prs(root: str) -> list[dict]:
    r = subprocess.run(["gh", "pr", "list", "--state", "open", "--json", "number,headRefName", "--limit", "200"],
                       cwd=root, capture_output=True, text=True, check=False)
    if r.returncode != 0:
        raise GateError(f"gh pr list: {r.stderr.strip()}")
    return json.loads(r.stdout or "[]")


def _local_branches(root: str) -> list[str]:
    r = _git(root, "for-each-ref", "--format=%(refname:short)", "refs/heads")
    if r.returncode != 0:
        raise GateError(f"git for-each-ref: {r.stderr.strip()}")
    return [b for b in r.stdout.split() if b]


def _shape(root, conf, fm, res, pfx) -> bool:
    for key in ("plan", "spec", "ledger"):
        ok = isinstance(fm[key], str) and os.path.isfile(os.path.join(root, fm[key]))
        _verdict(res, f"{pfx}.path.{key}", ok, f"{fm[key]!r} is not an existing file")
    for key in ("head", "base"):
        _verdict(res, f"{pfx}.{key}.commit", _is_commit(root, str(fm[key])),
                 f"{fm[key]!r} is not a commit in this repository")
    ints = all(isinstance(fm[k], int) and not isinstance(fm[k], bool) and fm[k] >= 1 for k in ("resume_line", "block"))
    _verdict(res, f"{pfx}.numbers", ints, "resume_line and block must be integers >= 1")
    cap = conf["rulings"]["max_per_block"]
    rulings = fm["rulings"]
    _verdict(res, f"{pfx}.rulings", isinstance(rulings, list) and len(rulings) <= cap,
             f"rulings must be a list of at most {cap} (rulings.max_per_block); got {len(rulings or [])}")
    env = fm["env"]
    _verdict(res, f"{pfx}.env", isinstance(env, dict) and all(k in env for k in ENV_KEYS),
             f"env must carry {', '.join(ENV_KEYS)}")
    parked = fm["parked_branches"]
    ok = isinstance(parked, list) and all(isinstance(p, dict) and all(str(p.get(k) or "").strip()
                                          for k in ("name", "reason", "owner")) for p in parked)
    _verdict(res, f"{pfx}.parked", ok, "parked_branches entries need name, reason and owner")
    return ints


def _ids(root, name, fm, res, pfx, moment):
    findings, claims = registers.load_findings(root), registers.load_claims(root)
    decisions = registers.load_decisions(root)
    for fid, state in _pairs(fm["findings_in_flight"]):
        f = registers.finding_by_id(findings, fid)
        if f is None:
            res.fail(f"{pfx}.finding.{fid}", "not in findings.yml")
        else:
            _verdict(res, f"{pfx}.finding.{fid}", not moment or f["state"] == state,
                     f"handoff says {state!r}, register says {f['state']!r}")
    by_claim = {c["id"]: c for c in claims}
    head = gitinfo.head_sha(root) if moment else ""
    for cid in [str(c) for c in fm["claims_verified_at_head"] or []]:
        c = by_claim.get(cid)
        reason = "not in claims.yml" if c is None else (claim_fresh(root, c, head) if moment else None)
        _verdict(res, f"{pfx}.claim.{cid}", not reason, reason or "")
    by_dec = {d["id"]: d for d in decisions}
    listed = sorted(str(d) for d in fm["open_decisions"] or [])
    for did in listed:
        _verdict(res, f"{pfx}.decision.{did}", did in by_dec, "not in decisions.yml")
    if moment:
        actual = sorted(d["id"] for d in registers.open_decisions(decisions))
        _verdict(res, f"{pfx}.open_decisions", listed == actual,
                 f"handoff lists {listed}, decisions.yml has open {actual}")
    grant = registers.grant_for(decisions, name)
    _verdict(res, f"{pfx}.grant", grant is not None and grant["id"] == str(fm["grant"]),
             f"grant {fm['grant']!r} is not the answered merge-grant 'grant:{name}' in decisions.yml")


def _ledger(root, fm, res, pfx):
    lines = _read(os.path.join(root, fm["ledger"])).splitlines()
    hits = [l for l in lines[fm["resume_line"]:] if CHECKPOINT_LINE.match(l.strip())
            and int(CHECKPOINT_LINE.match(l.strip()).group(1)) == fm["block"]]
    _verdict(res, f"{pfx}.ledger", bool(hits), f"no 'CHECKPOINT b{fm['block']}' line after ledger line {fm['resume_line']}")


def _next_task(root, fm, res, pfx):
    blocks = sorted({t.block for t in parse_plan(_read(os.path.join(root, fm["plan"]))).tasks})
    nxt, block = str(fm["next_task"]).strip(), fm["block"]
    if not blocks or block not in blocks:
        res.fail(f"{pfx}.next_task", f"block {block} is not a block of {fm['plan']} (blocks {blocks})")
    elif block == blocks[-1]:
        _verdict(res, f"{pfx}.next_task", nxt == "none", f"block {block} is the plan's last; next_task must be 'none'")
    else:
        _verdict(res, f"{pfx}.next_task", nxt != "none", f"block {block} is not the last ({blocks[-1]}); name the next task")


def _pre(root, fm, res, pfx):
    cur, head, base, on = gitinfo.head_sha(root), str(fm["head"]), str(fm["base"]), gitinfo.branch(root)
    _verdict(res, f"{pfx}.branch", str(fm["branch"]) == on and on != "main",
             f"pre-merge runs on the block branch the handoff names ({fm['branch']!r}); checkout is on {on!r}")
    if _is_commit(root, head):
        moved = _outside_state(root, head, cur)
        anc = _ancestor(root, head, cur)
        _verdict(res, f"{pfx}.head", anc and not moved,
                 f"HEAD {cur[:12]} is not handoff head {head[:12]} plus state-only commits: "
                 + (f"files outside {STATE}/ differ: {', '.join(moved[:5])}" if moved else "head is not an ancestor of HEAD"))
        if _is_commit(root, base):
            _verdict(res, f"{pfx}.base", _ancestor(root, base, head), f"base {base[:12]} is not an ancestor of head {head[:12]}")


def _post(root, name, fm, res, pfx):
    head, base, on = str(fm["head"]), str(fm["base"]), gitinfo.branch(root)
    _verdict(res, f"{pfx}.branch", on == "main", f"post-merge runs on main; checkout is on {on!r}")
    r = _git(root, "rev-list", "--parents", "-n", "1", "main")
    if r.returncode != 0:
        raise GateError(f"git rev-list main: {r.stderr.strip()}")
    main, *parents = r.stdout.split()
    _verdict(res, f"{pfx}.post.parent", parents == [base],
             f"main {main[:12]} has parent(s) {[p[:12] for p in parents]}; the squash merge must sit on base {base[:12]}")
    if _is_commit(root, head):
        moved = _outside_state(root, head, main)
        _verdict(res, f"{pfx}.post.tree", not moved,
                 f"main's tree differs from handoff head {head[:12]} outside {STATE}/: {', '.join(moved[:5])}")
    wts = gitinfo.worktrees(root)
    _verdict(res, f"{pfx}.worktrees", len(wts) == 1, f"worktrees besides the main checkout: {', '.join(wts[1:])}")
    parked = {str(p.get("name")) for p in fm["parked_branches"] or [] if isinstance(p, dict)}
    extra = sorted(set(_local_branches(root)) - {"main"} - parked)
    _verdict(res, f"{pfx}.branches", not extra, f"local branches neither main nor parked in the handoff: {', '.join(extra)}")

    def ours(pr: dict) -> bool:
        ref = str(pr.get("headRefName", ""))
        return ref.startswith(f"{name}/") or ref == str(fm["branch"])
    prs = [p for p in open_prs(root) if ours(p)]
    _verdict(res, f"{pfx}.prs", not prs, f"open PRs from this plan: {', '.join('#' + str(p.get('number')) for p in prs)}")


def check_handoff(root: str, conf: dict, name: str, res: Result, pfx: str, mode: str) -> None:
    """mode: 'structure', 'pre' or 'post'."""
    path = os.path.join(root, STATE, name, "handoff.md")
    if not os.path.isfile(path):
        res.fail(f"{pfx}.handoff", f"{STATE}/{name}/handoff.md missing")
        return
    fm = front_matter(path)
    missing = [k for k in KEYS if k not in fm]
    if missing:
        res.fail(f"{pfx}.keys", f"handoff front matter lacks: {', '.join(missing)}")
        return
    res.passed(f"{pfx}.keys")
    ints = _shape(root, conf, fm, res, pfx)
    moment = mode != "structure"
    _ids(root, name, fm, res, pfx, moment)
    if not moment:
        return
    if not (ints and all(isinstance(fm[k], str) and os.path.isfile(os.path.join(root, fm[k])) for k in ("plan", "ledger"))):
        res.fail(f"{pfx}.moment", "plan, ledger, resume_line or block unusable; moment checks not run")
        return
    if mode == "pre":
        _pre(root, fm, res, pfx)
    else:
        _post(root, name, fm, res, pfx)
    _verdict(res, f"{pfx}.status", gitinfo.status_clean(root), "git status --porcelain is not empty")
    live = fm["live_subagents"] or []
    _verdict(res, f"{pfx}.subagents", not live, f"live subagents at a checkpoint: {live}")
    _ledger(root, fm, res, pfx)
    _next_task(root, fm, res, pfx)


def _layout(root, name, res, pfx):
    d = os.path.join(root, STATE, name)
    first = _read(os.path.join(d, "ledger.md")).splitlines()[:1]
    m = LEDGER_PLAN.match(first[0]) if first else None
    ok = bool(m) and os.path.isfile(os.path.join(root, m.group(1))) and os.path.isdir(os.path.join(d, "mutations"))
    _verdict(res, f"{pfx}.layout", ok, "need ledger.md line 1 'plan: <existing file>' and mutations/")


def check(args: list) -> Result:
    p = gate_args("gate_checkpoint")
    p.add_argument("--plan")
    p.add_argument("--post", action="store_true")
    p.add_argument("--structure", action="store_true")
    a = p.parse_args(args)
    if bool(a.plan) == a.structure:
        raise GateError("give exactly one of --plan SLUG [--post] or --structure")
    if a.post and not a.plan:
        raise GateError("--post goes with --plan SLUG")
    root = a.root or repo_root()
    conf = gconfig.load(CODE_ROOT)
    res = Result("checkpoint")
    if a.plan:
        if not os.path.isdir(os.path.join(root, STATE, a.plan)):
            raise GateError(f"{STATE}/{a.plan}/ missing")
        check_handoff(root, conf, a.plan, res, "checkpoint", "post" if a.post else "pre")
        return res
    if not os.path.isdir(os.path.join(root, STATE)):
        raise GateError(f"{STATE}/ missing")
    names = sorted(os.path.basename(os.path.dirname(p)) for p in glob.glob(os.path.join(root, STATE, "*", "ledger.md")))
    if not names:
        res.passed("checkpoint.structure")
    for name in names:
        pfx = f"checkpoint.{name}"
        _layout(root, name, res, pfx)
        if os.path.isfile(os.path.join(root, STATE, name, "handoff.md")):
            check_handoff(root, conf, name, res, pfx, "structure")
    return res


def main(argv=None):
    return run_gate("checkpoint", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Register in run_all** — `GATES = ["register", "claims", "decisions", "plan", "dispatch", "review_package", "checkpoint"]`; add `"checkpoint": ["--structure"]` to `DEFAULT_ARGV`.

- [ ] **Step 6: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_gate_checkpoint -v` → `OK` (32 tests); `python3 tools/gates/run_all.py` → only `PASS` (structure mode over `docs/superpowers/state/strict-delivery/`).

- [ ] **Step 7: Commit**

```bash
git add tools/gates/gate_checkpoint.py tools/gates/tests/repo_fixture.py tools/gates/tests/test_gate_checkpoint.py tools/gates/run_all.py
git commit -m "feat(gates): gate_checkpoint -- pre-merge (block branch) and post-merge (--post, squash on base) handoff checks; --structure for CI"
```

- [ ] **Step 8: Mutation run and pin**

```bash
T=tools.gates.tests.test_gate_checkpoint.PostMerge.test_post_tree_mismatch_fails
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/_verdict\(res, f"\{pfx\}\.post\.tree", not moved,/_verdict(res, f"{pfx}.post.tree", True,/' tools/gates/gate_checkpoint.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/gate_checkpoint.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T7/mutation-SD-S2-T7.txt` (the package copy):
```
finding: SD-S2-T7
test: test_post_tree_mismatch_fails
step 1 (fix present): python3 -m unittest tools.gates.tests.test_gate_checkpoint.PostMerge.test_post_tree_mismatch_fails -> PASS
step 2 (fix reverted: perl makes the post-merge tree verdict always pass in tools/gates/gate_checkpoint.py): python3 -m unittest tools.gates.tests.test_gate_checkpoint.PostMerge.test_post_tree_mismatch_fails -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_gate_checkpoint.PostMerge.test_post_tree_mismatch_fails -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T7.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T7.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T7/mutation-SD-S2-T7.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T7.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T7.md
echo "- S2-T7 DONE <task head sha> gate_checkpoint; review docs/superpowers/state/strict-delivery/reviews/S2-T7.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T7"
PIN = {"test": "test_gate_checkpoint::test_post_tree_mismatch_fails",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T7.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T7.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T7.txt docs/superpowers/state/strict-delivery/reviews/S2-T7.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T7 pinned; ledger S2-T7"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 9: Review package and ledger (controller)** — before the reviewer: `python3 tools/gates/make_review_package.py strict-delivery S2-T7 <task base> <task head>`; the implementer's `mutation-SD-S2-T7.txt`, `siblings.md` (header + separator only: the row has no siblings) and `claims.md` in `.superpowers/sdd/strict-delivery/review/S2-T7/`; `python3 tools/gates/gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-T7` → `PASS`; reviewer; after ✅ the controller runs Step 8's pin, whose commit carries the ledger line `- S2-T7 DONE <task head sha> gate_checkpoint; review docs/superpowers/state/strict-delivery/reviews/S2-T7.md`.

### Task 8: Stop and SubagentStop hooks

**Files:**
- Create: `tools/gates/hooks/stop.py`
- Create: `tools/gates/hooks/subagent_stop.py`
- Modify: `.claude/settings.json` (add Stop and SubagentStop)
- Test: `tools/gates/tests/test_hook_stop.py`

**Findings:** SD-S2-T8

**Siblings:** none -- the register row SD-S2-T8 lists no siblings

**Gates:** run_all, checkpoint

**Interfaces:**
- Consumes: `gate_plan.plans_in_flight` (Task 6); `run_all.run(["checkpoint"], {"root": root, "checkpoint": ["--plan", name] (+ ["--post"] on main)})` (Task 7); `gitinfo.branch`; `registers.open_decisions`; test helpers `test_gate_checkpoint.{BIN, STATE, cp_repo, squash}`.
- Produces: `stop.py` — exit 0 when `stop_hook_active`, or when no plan is in flight and the checkout is on no `<plan>/b<N>` branch; otherwise runs gate_checkpoint for the in-flight plan with the newest `handoff.md` (falling back to `ledger.md` mtime) — `--post` when the checkout is on `main`, pre-merge otherwise — adds `checkpoint.subagents.live` when `background_tasks` is non-empty, exit 2 on any FAIL; the message lists the open decision ids and the line `waiting for the owner: DEC-…` the session ends with (umbrella convention), or a stop-again hint when none is open. `subagent_stop.py` — exit 0 when `stop_hook_active`; else the first user message of `agent_transcript_path` must carry `## Report: <path>`, the file must exist (relative paths resolve against the event `cwd`'s git top level, then `repo_root()`), and its first line must match `^status: (DONE_WITH_FINDINGS|DONE|NEEDS_DECISION|BLOCKED|DENIED)\b`; else exit 2 with "write your report file first".

- [ ] **Step 1: Write the failing tests** — `tools/gates/tests/test_hook_stop.py`:

```python
"""Stop and SubagentStop hooks run as Claude Code runs them: a subprocess with one JSON event on stdin
(strict-delivery S2 Task 8). Run from the repo root: python3 -m unittest tools.gates.tests.test_hook_stop -v
"""
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

from tools.gates.tests import repo_fixture as rf
from tools.gates.tests.test_gate_checkpoint import BIN, STATE, cp_repo, squash

HOOKS = os.path.join(rf.REAL, "tools", "gates", "hooks")
STATUSES = ["DONE", "DONE_WITH_FINDINGS", "NEEDS_DECISION", "BLOCKED", "DENIED"]


def hook(name, event, root):
    env = dict(os.environ, CLAUDE_PROJECT_DIR=root, PATH=BIN + os.pathsep + os.environ["PATH"], FAKE_GH_JSON="[]")
    data = event if isinstance(event, str) else json.dumps(event)
    return subprocess.run([sys.executable, os.path.join(HOOKS, name)], input=data, capture_output=True,
                          text=True, cwd=root, env=env, check=False)


def stop_event(root, **kw):
    ev = {"session_id": "fx", "transcript_path": "/dev/null", "cwd": root, "permission_mode": "default",
          "hook_event_name": "Stop", "stop_hook_active": False, "last_assistant_message": "done",
          "background_tasks": []}
    ev.update(kw)
    return ev


class StopHook(unittest.TestCase):
    def repo(self, merged=False):
        root = cp_repo()             # on fx/b1 at its pre-merge checkpoint (handoff committed)
        if merged:
            squash(root)             # on main after the squash merge; ledger ends in CHECKPOINT b1
        self.addCleanup(rf.remove, root)
        return root

    def broken(self):
        root = self.repo()
        rf.write(root, "src/late.py", "late = True\n")
        rf.commit(root, "feat: work after the handoff")
        return root

    def test_no_plan_in_flight_passes(self):
        root = self.repo(merged=True)
        r = hook("stop.py", stop_event(root), root)
        self.assertEqual(0, r.returncode, r.stderr)

    def test_block_branch_with_a_consistent_handoff_passes(self):
        root = self.repo()
        r = hook("stop.py", stop_event(root), root)
        self.assertEqual(0, r.returncode, r.stderr)

    def test_in_flight_plan_with_an_inconsistent_tree_blocks(self):
        root = self.broken()
        r = hook("stop.py", stop_event(root), root)
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("checkpoint.head", r.stderr)
        self.assertIn("stop_hook_active", r.stderr)

    def test_stop_hook_active_passes_through(self):
        root = self.broken()
        r = hook("stop.py", stop_event(root, stop_hook_active=True), root)
        self.assertEqual(0, r.returncode, r.stderr)

    def test_waiting_for_the_owner_names_the_open_decisions(self):
        root = self.broken()
        r = hook("stop.py", stop_event(root), root)
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("waiting for the owner: DEC-0002", r.stderr)

    def test_ledger_in_flight_on_main_runs_the_post_merge_check(self):
        root = self.repo(merged=True)
        with open(os.path.join(root, STATE, "ledger.md"), "a", encoding="utf-8") as f:
            f.write("- b2 start\n")
        rf.commit(root, "chore(state): b2 start")
        r = hook("stop.py", stop_event(root), root)
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("checkpoint.post.parent", r.stderr)

    def test_background_tasks_block_while_in_flight(self):
        root = self.repo()
        r = hook("stop.py", stop_event(root, background_tasks=[{"id": "a1"}]), root)
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("subagents.live", r.stderr)

    def test_background_tasks_ignored_when_no_plan_in_flight(self):
        root = self.repo(merged=True)
        r = hook("stop.py", stop_event(root, background_tasks=[{"id": "a1"}]), root)
        self.assertEqual(0, r.returncode, r.stderr)

    def test_garbage_stdin_blocks(self):
        root = self.repo()
        r = hook("stop.py", "not json", root)
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("FAIL hook.", r.stderr)


class SubagentStopHook(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="gates-sub-")
        self.addCleanup(shutil.rmtree, self.dir, True)

    def transcript(self, prompt):
        path = os.path.join(self.dir, "agent.jsonl")
        lines = [{"type": "system", "content": "init"},
                 {"type": "user", "message": {"role": "user", "content": [{"type": "text", "text": prompt}]}},
                 {"type": "assistant", "message": {"role": "assistant", "content": "ok"}}]
        rf.write(self.dir, "agent.jsonl", "".join(json.dumps(l) + "\n" for l in lines))
        return path

    def report(self, text, name="r1.md"):
        rf.write(self.dir, name, text)
        return os.path.join(self.dir, name)

    def event(self, transcript, **kw):
        ev = {"session_id": "fx", "transcript_path": "/dev/null", "cwd": self.dir, "permission_mode": "default",
              "hook_event_name": "SubagentStop", "stop_hook_active": False, "agent_id": "a1",
              "agent_type": "general-purpose", "agent_transcript_path": transcript,
              "last_assistant_message": "status: DONE"}
        ev.update(kw)
        return ev

    def run_hook(self, ev, root=None):
        return hook("subagent_stop.py", ev, root or self.dir)

    def test_each_status_passes(self):
        for status in STATUSES:
            with self.subTest(status=status):
                rp = self.report(f"status: {status}\nbody\n")
                r = self.run_hook(self.event(self.transcript(f"## Role: implementer\n## Report: {rp}\n")))
                self.assertEqual(0, r.returncode, r.stderr)

    def test_subagent_report_missing_blocks(self):
        tp = self.transcript(f"## Report: {os.path.join(self.dir, 'absent.md')}\n")
        r = self.run_hook(self.event(tp))
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("write your report file first", r.stderr)

    def test_subagent_bad_status_blocks(self):
        rp = self.report("status: FINISHED\n")
        r = self.run_hook(self.event(self.transcript(f"## Report: {rp}\n")))
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("subagent.report.status", r.stderr)

    def test_status_must_be_the_first_line(self):
        rp = self.report("notes\nstatus: DONE\n")
        r = self.run_hook(self.event(self.transcript(f"## Report: {rp}\n")))
        self.assertEqual(2, r.returncode, r.stderr)

    def test_prompt_without_report_line_blocks(self):
        r = self.run_hook(self.event(self.transcript("## Role: implementer\n")))
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("subagent.report", r.stderr)

    def test_missing_transcript_blocks(self):
        r = self.run_hook(self.event(os.path.join(self.dir, "none.jsonl")))
        self.assertEqual(2, r.returncode, r.stderr)
        self.assertIn("subagent.transcript", r.stderr)

    def test_stop_hook_active_passes_through(self):
        tp = self.transcript(f"## Report: {os.path.join(self.dir, 'absent.md')}\n")
        r = self.run_hook(self.event(tp, stop_hook_active=True))
        self.assertEqual(0, r.returncode, r.stderr)

    def test_relative_report_path_resolves_against_the_repo(self):
        root = rf.make_repo(rf.DISPATCH_FILES, rf.load_rows())
        self.addCleanup(rf.remove, root)
        rf.write(root, "reports/r2.md", "status: BLOCKED\nwaiting\n")
        r = self.run_hook(self.event(self.transcript("## Report: reports/r2.md\n"), cwd=root), root)
        self.assertEqual(0, r.returncode, r.stderr)


class Settings(unittest.TestCase):
    def test_stop_and_subagent_stop_hooks_are_registered(self):
        with open(os.path.join(rf.REAL, ".claude", "settings.json"), encoding="utf-8") as f:
            hooks = json.load(f)["hooks"]
        stop = hooks["Stop"][0]["hooks"][0]
        sub = hooks["SubagentStop"][0]["hooks"][0]
        self.assertEqual('python3 "${CLAUDE_PROJECT_DIR}/tools/gates/hooks/stop.py"', stop["command"])
        self.assertEqual(300, stop["timeout"])
        self.assertEqual('python3 "${CLAUDE_PROJECT_DIR}/tools/gates/hooks/subagent_stop.py"', sub["command"])
        self.assertEqual(60, sub["timeout"])
        self.assertIn("Agent", [e.get("matcher") for e in hooks["PreToolUse"]])


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_hook_stop -v`
Expected: FAIL/ERROR — `stop.py` and `subagent_stop.py` do not exist (`can't open file`, rc 2 where 0 is expected), `KeyError: 'Stop'`.

- [ ] **Step 3: Implement** — `tools/gates/hooks/stop.py`:

```python
#!/usr/bin/env python3
"""Claude Code Stop hook (umbrella "Hook protocol"; spec §3.4, §3.5).

While a strict-delivery plan is in flight the session may stop only when gate_checkpoint passes for it.
In flight = gate_plan.plans_in_flight() (ledger without a committed trailing CHECKPOINT line), or the
checkout is on one of the plan's block branches '<plan>/b<N>'. On main the post-merge check runs
(gate_checkpoint --plan <slug> --post), elsewhere the pre-merge one. When it blocks it names the open owner
decisions, so a session waiting for the owner stops on its second try. Loop guard: stop_hook_active -> exit 0.
"""
from __future__ import annotations

import json
import os
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

STATE = "docs/superpowers/state"
HINT = ("If you are only waiting for a background subagent or an owner decision, end the turn again: "
        "this hook lets the second stop through (stop_hook_active).")


def _newest(root: str, names: list) -> str:
    def stamp(name: str) -> float:
        for f in ("handoff.md", "ledger.md"):
            p = os.path.join(root, STATE, name, f)
            if os.path.isfile(p):
                return os.path.getmtime(p)
        return 0.0
    return max(names, key=stamp)


def run(event: dict) -> int:
    if event.get("stop_hook_active"):
        return 0
    from tools.gates import run_all
    from tools.gates.gate_plan import plans_in_flight
    from tools.gates.lib import gitinfo, registers, repo_root
    root = repo_root()
    names = {n for n, _ in plans_in_flight(root)}
    on = gitinfo.branch(root)
    state = os.path.join(root, STATE)
    names |= {n for n in os.listdir(state)
              if on.startswith(f"{n}/") and os.path.isfile(os.path.join(state, n, "ledger.md"))}
    if not names:
        return 0
    name = _newest(root, sorted(names))
    argv = ["--plan", name] + (["--post"] if on == "main" else [])   # post-merge on main, else pre-merge
    res = run_all.run(["checkpoint"], {"root": root, "checkpoint": argv})
    live = event.get("background_tasks") or []
    if live:
        count = len(live) if isinstance(live, (list, dict)) else 1
        res.fail("checkpoint.subagents.live", f"{count} background task(s) still running")
    if res.ok():
        return 0
    print(res.render(False), file=sys.stderr)
    waiting = [d["id"] for d in registers.open_decisions(registers.load_decisions(root))]
    if waiting:   # umbrella "CLI flags and conventions": waiting for the owner mid-block
        ids = ", ".join(waiting)
        print(f"plan {name} is in flight with open owner decisions {ids}. If you are waiting for the owner, "
              f"make your last message 'waiting for the owner: {ids}' and end the turn again: the second stop "
              "is let through (stop_hook_active).", file=sys.stderr)
    else:
        print(f"plan {name} is in flight. {HINT}", file=sys.stderr)
    return 2

def main() -> int:
    try:
        event = json.loads(sys.stdin.read() or "null")
        if not isinstance(event, dict):
            print("FAIL hook.input: the Stop hook expects one JSON object on stdin", file=sys.stderr)
            return 2
        return run(event)
    except Exception as exc:  # hooks fail closed (umbrella hook protocol)
        print(f"FAIL hook.internal: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
```

`tools/gates/hooks/subagent_stop.py`:
```python
#!/usr/bin/env python3
"""Claude Code SubagentStop hook (umbrella "Hook protocol"; spec §5 "Reports").

The subagent's report file -- path from the '## Report:' line of its dispatch prompt, which is the first
user message of agent_transcript_path -- must exist and start with 'status: <STATUS>'. Loop guard:
stop_hook_active -> exit 0.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

STATUS_RE = re.compile(r"^status: (DONE_WITH_FINDINGS|DONE|NEEDS_DECISION|BLOCKED|DENIED)\b")
REPORT_RE = re.compile(r"^## Report: (\S.*?)\s*$", re.M)


def first_user_text(path: str) -> str | None:
    with open(path, encoding="utf-8") as f:
        for line in f:
            try:
                obj = json.loads(line)
            except ValueError:
                continue
            if not isinstance(obj, dict) or obj.get("type") != "user":
                continue
            content = (obj.get("message") or {}).get("content")
            if isinstance(content, str):
                return content
            if isinstance(content, list):
                return "\n".join(b.get("text", "") for b in content if isinstance(b, dict) and b.get("type") == "text")
    return None


def _candidates(path: str, cwd: str | None) -> list[str]:
    if os.path.isabs(path):
        return [path]
    out = []
    if cwd and os.path.isdir(cwd):
        r = subprocess.run(["git", "-C", cwd, "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=False)
        if r.returncode == 0:
            out.append(os.path.join(r.stdout.strip(), path))
    from tools.gates.lib import repo_root
    out.append(os.path.join(repo_root(), path))
    return out


def run(event: dict) -> int:
    if event.get("stop_hook_active"):
        return 0
    transcript = event.get("agent_transcript_path")
    if not transcript or not os.path.isfile(transcript):
        print(f"FAIL subagent.transcript: agent_transcript_path missing or unreadable: {transcript!r}", file=sys.stderr)
        return 2
    text = first_user_text(transcript)
    m = REPORT_RE.search(text or "")
    if not m:
        print("FAIL subagent.report: the dispatch prompt has no '## Report: <path>' line", file=sys.stderr)
        return 2
    found = [p for p in _candidates(m.group(1), event.get("cwd")) if os.path.isfile(p)]
    if not found:
        print(f"FAIL subagent.report.missing: write your report file first: {m.group(1)}", file=sys.stderr)
        return 2
    with open(found[0], encoding="utf-8") as f:
        first = f.readline()
    if not STATUS_RE.match(first):
        print("FAIL subagent.report.status: write your report file first: line 1 must be 'status: "
              "DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED'", file=sys.stderr)
        return 2
    return 0


def main() -> int:
    try:
        event = json.loads(sys.stdin.read() or "null")
        if not isinstance(event, dict):
            print("FAIL hook.input: the SubagentStop hook expects one JSON object on stdin", file=sys.stderr)
            return 2
        return run(event)
    except Exception as exc:  # hooks fail closed (umbrella hook protocol)
        print(f"FAIL hook.internal: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Register the hooks** (exact umbrella entries; other keys untouched):

```bash
python3 - <<'EOF'
import json
p = ".claude/settings.json"
s = json.load(open(p, encoding="utf-8"))
cmd = 'python3 "${CLAUDE_PROJECT_DIR}/tools/gates/hooks/%s"'
s["hooks"]["Stop"] = [{"hooks": [{"type": "command", "command": cmd % "stop.py", "timeout": 300}]}]
s["hooks"]["SubagentStop"] = [{"hooks": [{"type": "command", "command": cmd % "subagent_stop.py", "timeout": 60}]}]
open(p, "w", encoding="utf-8").write(json.dumps(s, indent=2) + "\n")
EOF
git diff .claude/settings.json
```
Expected: the diff adds the two keys (and may re-indent S1's JSON; no value changes).

- [ ] **Step 5: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_hook_stop -v` → `OK` (18 tests); `python3 tools/gates/run_all.py` → only `PASS`. The controller's own session now meets the Stop hook at turn ends while b6 is in flight (once per stop; the second stop passes); Claude Code may require a hooks review (`/hooks`) before the edited settings take effect.

- [ ] **Step 6: Commit**

```bash
git add tools/gates/hooks/stop.py tools/gates/hooks/subagent_stop.py .claude/settings.json tools/gates/tests/test_hook_stop.py
git commit -m "feat(hooks): Stop runs gate_checkpoint while a plan is in flight; SubagentStop enforces the report contract"
```

- [ ] **Step 7: Mutation run and pin**

```bash
T=tools.gates.tests.test_hook_stop.SubagentStopHook.test_subagent_bad_status_blocks
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/if not STATUS_RE\.match\(first\):/if False:/' tools/gates/hooks/subagent_stop.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/hooks/subagent_stop.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T8/mutation-SD-S2-T8.txt` (the package copy):
```
finding: SD-S2-T8
test: test_subagent_bad_status_blocks
step 1 (fix present): python3 -m unittest tools.gates.tests.test_hook_stop.SubagentStopHook.test_subagent_bad_status_blocks -> PASS
step 2 (fix reverted: perl replaces the status check in tools/gates/hooks/subagent_stop.py with False): python3 -m unittest tools.gates.tests.test_hook_stop.SubagentStopHook.test_subagent_bad_status_blocks -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_hook_stop.SubagentStopHook.test_subagent_bad_status_blocks -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T8.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T8.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T8/mutation-SD-S2-T8.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T8.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T8.md
echo "- S2-T8 DONE <task head sha> Stop + SubagentStop hooks; review docs/superpowers/state/strict-delivery/reviews/S2-T8.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T8"
PIN = {"test": "test_hook_stop::test_subagent_bad_status_blocks",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T8.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T8.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T8.txt docs/superpowers/state/strict-delivery/reviews/S2-T8.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T8 pinned; ledger S2-T8"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 8: Review package and ledger (controller)** — `make_review_package.py strict-delivery S2-T8 <task base> <task head>`, implementer files, `gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-T8` → `PASS`, reviewer; after ✅ the controller runs the pin step above, whose commit carries the ledger line `- S2-T8 DONE <task head sha> Stop + SubagentStop hooks; review docs/superpowers/state/strict-delivery/reviews/S2-T8.md`.

### Task 9: per-plan state creator and the checklists

**Files:**
- Create: `tools/gates/new_plan_state.py`
- Create: `~/.claude/skills/strict-delivery/checklists/checkpoint.md` (outside the repo; not committed)
- Create: `~/.claude/skills/strict-delivery/checklists/resume.md` (outside the repo; not committed)
- Create: `~/.claude/skills/strict-delivery/checklists/block-end.md` (outside the repo; not committed)
- Modify: `tools/gates/README.md` (append checkpoint/hooks/state section)
- Modify: `.github/workflows/gates.yml` (only if it lists test modules)
- Test: `tools/gates/tests/test_new_plan_state.py`

**Findings:** SD-S2-T9

**Siblings:** none -- the register row SD-S2-T9 lists no siblings

**Gates:** run_all, checkpoint

**Interfaces:**
- Consumes: ledger contract and `plans_in_flight` (Task 6); `gate_checkpoint --structure` (Task 7).
- Produces: CLI `python3 tools/gates/new_plan_state.py <plan> [--plan-file docs/superpowers/plans/<file>.md]` creating the tracked `docs/superpowers/state/<plan>/{ledger.md, mutations/.gitkeep}` and the git-ignored package root `.superpowers/sdd/<plan>/review/`; `ledger.md` line 1 `plan: <plan file>`; refuses an existing dir, a missing plan file, or a name not matching `^[a-z0-9][a-z0-9-]*$` (exit 1). `handoff.md` is written at the first checkpoint, never by this script. `main(argv=None) -> int`.

- [ ] **Step 1: Write the failing tests** — `tools/gates/tests/test_new_plan_state.py`:

```python
"""new_plan_state.main() on a fixture repo (strict-delivery S2 Task 9).
Run from the repo root: python3 -m unittest tools.gates.tests.test_new_plan_state -v
"""
import contextlib
import io
import os
import shutil
import unittest
from unittest import mock

from tools.gates import gate_checkpoint, gate_plan, new_plan_state as nps
from tools.gates.tests import repo_fixture as rf

STATE = "docs/superpowers/state/fx"


class NewPlanState(unittest.TestCase):
    def setUp(self):
        files = dict(rf.DISPATCH_FILES)
        files["docs/superpowers/plans/fx.md"] = "# fx\n"
        self.root = rf.make_repo(files, rf.load_rows())
        self.addCleanup(shutil.rmtree, self.root, True)

    def run_main(self, module, *argv):
        out = io.StringIO()
        with mock.patch.dict(os.environ, {"CLAUDE_PROJECT_DIR": self.root}), \
                contextlib.redirect_stdout(out), contextlib.redirect_stderr(out):
            rc = module.main(list(argv))
        return rc, out.getvalue()

    def test_creates_ledger_mutations_and_review(self):
        rc, out = self.run_main(nps, "fx")
        self.assertEqual(0, rc, out)
        for rel in ("ledger.md", "mutations/.gitkeep"):
            self.assertTrue(os.path.isfile(os.path.join(self.root, STATE, rel)), rel)
        self.assertTrue(os.path.isdir(os.path.join(self.root, ".superpowers/sdd/fx/review")))
        self.assertFalse(os.path.exists(os.path.join(self.root, STATE, "handoff.md")))

    def test_ledger_starts_with_plan_line(self):
        rc, out = self.run_main(nps, "fx", "--plan-file", "docs/superpowers/plans/fx.md")
        self.assertEqual(0, rc, out)
        with open(os.path.join(self.root, STATE, "ledger.md"), encoding="utf-8") as f:
            self.assertEqual("plan: docs/superpowers/plans/fx.md", f.readline().rstrip("\n"))
        self.assertEqual([("fx", "docs/superpowers/plans/fx.md")], gate_plan.plans_in_flight(self.root))

    def test_refuses_existing_state_dir(self):
        self.assertEqual(0, self.run_main(nps, "fx")[0])
        rc, out = self.run_main(nps, "fx")
        self.assertEqual(1, rc, out)
        self.assertIn("already exists", out)

    def test_refuses_missing_plan_file(self):
        rc, out = self.run_main(nps, "nope")
        self.assertEqual(1, rc, out)
        self.assertIn("no such plan file", out)

    def test_refuses_bad_name(self):
        rc, out = self.run_main(nps, "Bad_Name", "--plan-file", "docs/superpowers/plans/fx.md")
        self.assertEqual(1, rc, out)

    def test_new_state_passes_structure_mode(self):
        self.assertEqual(0, self.run_main(nps, "fx")[0])
        rf.commit(self.root, "chore(state): new plan fx")
        rc, out = self.run_main(gate_checkpoint, "--structure")
        self.assertEqual(0, rc, out)
        self.assertIn("PASS checkpoint.fx.layout", out)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_new_plan_state -v`
Expected: ERROR `cannot import name 'new_plan_state'`.

- [ ] **Step 3: Implement** — `tools/gates/new_plan_state.py`:

```python
#!/usr/bin/env python3
"""new_plan_state -- create docs/superpowers/state/<plan>/ (ledger.md, mutations/) and the git-ignored
.superpowers/sdd/<plan>/review/ for a plan
about to execute. The ledger's line 1 names the plan file (the contract in tools/gates/gate_plan.py);
handoff.md is written at the first checkpoint, never here.
Usage: new_plan_state.py <plan> [--plan-file docs/superpowers/plans/<file>.md]
"""
from __future__ import annotations

import argparse
import os
import re
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import gitinfo, repo_root  # noqa: E402

STATE = "docs/superpowers/state"
NAME_RE = re.compile(r"^[a-z0-9][a-z0-9-]*$")


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="new_plan_state")
    p.add_argument("plan", help="state directory name; block branches are <plan>/b<N>, tags <plan>-b<N>")
    p.add_argument("--plan-file", help="default: docs/superpowers/plans/<plan>.md")
    a = p.parse_args(argv)
    if not NAME_RE.match(a.plan):
        print(f"new_plan_state: plan name {a.plan!r} must match {NAME_RE.pattern}", file=sys.stderr)
        return 1
    root = repo_root()
    plan_file = a.plan_file or f"docs/superpowers/plans/{a.plan}.md"
    if not os.path.isfile(os.path.join(root, plan_file)):
        print(f"new_plan_state: no such plan file: {plan_file}", file=sys.stderr)
        return 1
    d = os.path.join(root, STATE, a.plan)
    if os.path.exists(d):
        print(f"new_plan_state: {STATE}/{a.plan} already exists; a plan's state is created once", file=sys.stderr)
        return 1
    os.makedirs(os.path.join(d, "mutations"))
    open(os.path.join(d, "mutations", ".gitkeep"), "w", encoding="utf-8").close()
    os.makedirs(os.path.join(root, ".superpowers", "sdd", a.plan, "review"), exist_ok=True)  # git-ignored
    with open(os.path.join(d, "ledger.md"), "w", encoding="utf-8") as f:
        f.write(f"plan: {plan_file}\n# Ledger: {a.plan}\n\n"
                f"- created by tools/gates/new_plan_state.py at {gitinfo.head_sha(root)[:12]}\n")
    print(f"created {STATE}/{a.plan}/ (ledger.md, mutations/) and .superpowers/sdd/{a.plan}/review/ "
          "(plan in flight until 'CHECKPOINT b<N>')")
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_new_plan_state -v` → `OK` (6 tests); `python3 tools/gates/run_all.py` → only `PASS`.

- [ ] **Step 5: Checklists** (skill files, outside the repo: `mkdir -p ~/.claude/skills/strict-delivery/checklists`).

`~/.claude/skills/strict-delivery/checklists/block-end.md`:
```markdown
# Block end (spec §3.4) — in order; any red line stops the sequence

Variables: PLAN = state dir name (`docs/superpowers/state/PLAN/`); N = block number; BR = `PLAN/bN`.
main is protected (strict checks, enforce_admins): never commit to main; every state commit rides in the PR.

1. Boundary. Every task of block N is `DONE` in the ledger with a ✅ review and its row `pinned` (the
   reviewer copied the accepted transcript to `docs/superpowers/state/PLAN/mutations/<id>.txt`). A trigger
   that fired mid-task means: finish that task's review loop, or revert to the last boundary
   (`git reset --hard <last task head>`, row back to `planned`, ledger `- Tn reverted to boundary`).
2. Up to date. `git fetch origin && git merge --ff-only origin/main` is a no-op, or merge main in and re-run
   step 3 (branch protection is strict: the PR must contain main's tip).
3. Gates. `python3 tools/gates/run_all.py` → every line PASS; `python3 -m unittest discover -s tools/gates/tests -t .` → OK.
4. Packages (git-ignored, never committed): `python3 tools/gates/gate_review_package.py --all` → every line PASS.
5. Final reviews. Dispatch role `final_reviewer` (opus) and role `final_verifier` (fable) with the dispatch
   grammar; both re-run every gate and every mutation; both report files start `status: DONE`. Any finding →
   fix loop (spec §6.7), then back to step 3.
6. Rulings. At most 5 `Ruling:` lines in the ledger since `- bN start`.
7. Merge readiness. If `python3 -c "from tools.gates import run_all; print('merge_ready' in run_all.GATES)"`
   prints True: `python3 tools/gates/gate_merge_ready.py --pr <number>` → PASS. Otherwise by hand, ledgering each:
   a. PR body free of every `merge_markers` entry:
      `python3 -c "import re,subprocess,yaml; m=yaml.safe_load(open('tools/gates/config.yml'))['merge_markers']; b=subprocess.run(['gh','pr','view','--json','body','-q','.body'],capture_output=True,text=True,check=True).stdout; h=[x for x in m if re.search(re.escape(x),b,re.I)]; print(h or 'markers: none'); raise SystemExit(1 if h else 0)"`
   b. `python3 tools/gates/gate_register.py`, `gate_claims.py`, `gate_decisions.py` → PASS; every row the
      ledger names for block N is `pinned`, `closed`, or `deferred` with a `ruling`.
   c. The grant `grant:PLAN` is answered `yes` by the owner in `decisions.yml`.
8. Handoff on BR: `checkpoint.md` steps 1–4, ending with `python3 tools/gates/gate_checkpoint.py --plan PLAN`
   (pre-merge) → every line PASS.
9. PR. `gh pr ready`; `gh pr checks --watch`; then `gh pr checks` shows every check `pass`, advisory included.
10. Merge. `gh pr merge --squash --delete-branch`.
11. Post-merge. `git checkout main && git pull --ff-only`; `git tag PLAN-bN && git push origin PLAN-bN`;
    `git branch -D BR` if it still exists; `git worktree list` → for every tree the block created
    `git worktree remove <path>` (trees with submodules: `rm -rf <path> && git worktree prune`); archive the
    scratch workspace: `mkdir -p .superpowers/archive/PLAN-bN && mv .superpowers/sdd/PLAN .superpowers/archive/PLAN-bN/`.
12. Verify. `python3 tools/gates/gate_checkpoint.py --plan PLAN --post` → every line PASS on main. A FAIL is
    reported to the owner and fixed in the next block's first commit; it is never waved through.
13. `checkpoint.md` steps 5–7: status update, resume prompt, stop (spec §3.5).
```

`~/.claude/skills/strict-delivery/checklists/checkpoint.md`:
```markdown
# Checkpoint outputs (spec §3.3) — called from block-end.md steps 8 and 13

1. Facts on BR: `git rev-parse HEAD` (HEAD), `git merge-base HEAD origin/main` (BASE), `git status --porcelain`,
   `python3 tools/gates/run_all.py` (summary for the status update).
2. Handoff. Write `docs/superpowers/state/PLAN/handoff.md`: YAML front matter with exactly the umbrella keys —
   plan (the plan file whose tasks ran), spec, `branch: BR` (the block branch; the post-merge check maps it to
   main), `head: "<HEAD from step 1>"` (quoted), `base: "<BASE from step 1>"`,
   `ledger: docs/superpowers/state/PLAN/ledger.md`, `resume_line` (ledger line of `- bN start`), `block: N`,
   `next_task` (`none` after the plan's last block), `findings_in_flight` (`[{ID: state}, …]` exactly as
   `findings.yml` says), `claims_verified_at_head` (ids whose rows are verified with no cited file changed
   since), `live_subagents: []`, `open_decisions` (every unanswered id in `decisions.yml`), `rulings` (this
   block's ledger `Ruling:` lines), `grant` (the `grant:PLAN` decision id), `parked_branches` (every local
   branch but main and BR: name, reason, owner), `env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}`, `traps`.
   Body: the status update text.
3. Ledger. Append `CHECKPOINT bN <HEAD from step 1>`.
4. Commit and push on BR (state files only): `git add docs/superpowers/state/PLAN && git commit -m "chore(state): checkpoint bN" && git push`;
   then `python3 tools/gates/gate_checkpoint.py --plan PLAN` → PASS (pre-merge).
5. Status update to the owner: done since the last checkpoint (task ids, commits, gate results at HEAD);
   remaining in this plan; remaining on the roadmap after it; open decisions awaiting the owner; rulings
   made in this block; parked items.
6. Resume prompt, verbatim:
   `Invoke the strict-delivery skill. Read docs/superpowers/state/PLAN/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the PLAN plan at block <N+1> (sub-plan <S>, task <T>) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.`
7. Stop. Do not start the next block in this context. If an owner decision is open, the last message is
   `waiting for the owner: DEC-…`; the Stop hook blocks once listing the ids and lets the second stop through.
```

`~/.claude/skills/strict-delivery/checklists/resume.md`:
```markdown
# Resume after /clear (spec §3.5) — skill first, handoff second, gates third, then dispatch

0. If this context already ran a block, refuse: ask the owner to /clear and paste the resume prompt.
1. Invoke the strict-delivery skill (Skill tool) before any other action.
2. Read `docs/superpowers/state/PLAN/handoff.md` (plan, block, next_task, open decisions, traps).
3. `git checkout main && git pull --ff-only`; `python3 tools/gates/gate_checkpoint.py --plan PLAN --post` → PASS
   (only while main is still the squash of that block; once later work has merged, read the handoff only).
   A FAIL means the last checkpoint did not finish: report the lines to the owner and stop.
4. `python3 tools/gates/run_all.py` → every line PASS; stop on any FAIL.
5. Decisions: `python3 tools/gates/gate_decisions.py`; for each id in `open_decisions` read its answer in
   `decisions.yml`; one still unanswered that blocks `next_task` → stop and say `waiting for the owner: DEC-…`.
6. Branch: `git checkout -b PLAN/b<N+1>`; ledger `- b<N+1> start <sha>`; commit `chore(state): b<N+1> start`
   (the plan is in flight). Rows of earlier blocks stay `pinned` until `postmerge.py` (S4) sets `merged`.
7. Dispatch `next_task` with the dispatch grammar; models per `tools/gates/config.yml`; budget per
   `triggers`. The PreToolUse hook refuses anything else.
```

Verify: `ls ~/.claude/skills/strict-delivery/checklists/` → three files; `grep -c "gate_checkpoint" ~/.claude/skills/strict-delivery/checklists/*.md` → each ≥ 1.

- [ ] **Step 6: README + CI** — append to `tools/gates/README.md`:
```markdown
## gate_checkpoint, Stop/SubagentStop hooks, per-plan state (S2)

`python3 tools/gates/new_plan_state.py <plan> [--plan-file P]` creates `docs/superpowers/state/<plan>/`
(`ledger.md` with line 1 `plan: P`, `mutations/`) and the git-ignored `.superpowers/sdd/<plan>/review/`.
`gate_checkpoint.py --plan <plan>` (pre-merge, on the block branch) checks HEAD = handoff head plus
state-only commits, branch, clean status, live subagents, finding states, fresh claims, open decisions,
rulings cap, grant, the ledger's `CHECKPOINT b<N>` line and `next_task`; `--post` (on main after the squash)
checks instead that main's only parent is the handoff base and its tree differs from the handoff head only
under `docs/superpowers/state/`, plus worktrees (main only), local branches (main + parked) and open PRs from
`<plan>/…`; `--structure` (run_all's default) is the CI-safe subset. The Stop hook runs the check (`--post`
on main) whenever a plan is in flight (ledger not ending in a committed `CHECKPOINT` line, or the checkout
is on `<plan>/b<N>`) and names open owner decisions when it blocks; the SubagentStop hook requires
the report file named by the dispatch's `## Report:` line, starting `status: <STATUS>`. Checklists:
`~/.claude/skills/strict-delivery/checklists/{block-end,checkpoint,resume}.md`.
```
Run `grep -n "unittest" .github/workflows/gates.yml`; if modules are listed, append `tools.gates.tests.test_gate_checkpoint tools.gates.tests.test_hook_stop tools.gates.tests.test_new_plan_state`.

- [ ] **Step 7: Commit**

```bash
git add tools/gates/new_plan_state.py tools/gates/tests/test_new_plan_state.py tools/gates/README.md .github/workflows/gates.yml
git commit -m "feat(gates): new_plan_state -- per-plan ledger, mutations/, review/; README for the S2 gates and hooks"
```

- [ ] **Step 8: Mutation run and pin**

```bash
T=tools.gates.tests.test_new_plan_state.NewPlanState.test_ledger_starts_with_plan_line
python3 -m unittest $T                                                     # expect OK
perl -pi -e 's/f\.write\(f"plan: \{plan_file\}\\n# Ledger/f.write(f"# plan {plan_file}\\n# Ledger/' tools/gates/new_plan_state.py
git diff --stat                                                            # expect 1 line changed
python3 -m unittest $T                                                     # expect FAILED
git checkout -- tools/gates/new_plan_state.py
python3 -m unittest $T                                                     # expect OK
```
Write `.superpowers/sdd/strict-delivery/review/S2-T9/mutation-SD-S2-T9.txt` (the package copy):
```
finding: SD-S2-T9
test: test_ledger_starts_with_plan_line
step 1 (fix present): python3 -m unittest tools.gates.tests.test_new_plan_state.NewPlanState.test_ledger_starts_with_plan_line -> PASS
step 2 (fix reverted: perl turns the ledger's 'plan:' line into a comment in tools/gates/new_plan_state.py): python3 -m unittest tools.gates.tests.test_new_plan_state.NewPlanState.test_ledger_starts_with_plan_line -> FAIL
step 3 (fix restored): python3 -m unittest tools.gates.tests.test_new_plan_state.NewPlanState.test_ledger_starts_with_plan_line -> PASS
```
After the task review's verdict is spec ✅, quality ✅, claims-executed ✅, the controller (never the reviewer, never before the review; b4 ruling 3) pins the row: it copies that file to the tracked `docs/superpowers/state/strict-delivery/mutations/SD-S2-T9.txt` (what `pin.transcript` names) and the reviewer's report, verbatim, to `docs/superpowers/state/strict-delivery/reviews/S2-T9.md` (what `evidence` names), appends the ledger line, sets `pin.test` in `registers.PIN_TEST` form and both history entries `by: controller`, and commits the four files together:
```bash
mkdir -p docs/superpowers/state/strict-delivery/mutations docs/superpowers/state/strict-delivery/reviews
cp .superpowers/sdd/strict-delivery/review/S2-T9/mutation-SD-S2-T9.txt docs/superpowers/state/strict-delivery/mutations/SD-S2-T9.txt
cp <the reviewer's report file> docs/superpowers/state/strict-delivery/reviews/S2-T9.md
echo "- S2-T9 DONE <task head sha> new_plan_state + checklists; review docs/superpowers/state/strict-delivery/reviews/S2-T9.md" >> docs/superpowers/state/strict-delivery/ledger.md
SHA=$(git rev-parse HEAD) python3 - <<'EOF'
import os, yaml
P = "docs/superpowers/state/findings.yml"; FID = "SD-S2-T9"
PIN = {"test": "test_new_plan_state::test_ledger_starts_with_plan_line",
       "transcript": "docs/superpowers/state/strict-delivery/mutations/SD-S2-T9.txt"}
text = open(P, encoding="utf-8").read()
header = "".join(l for l in text.splitlines(True) if l.startswith("#"))
doc = yaml.safe_load(text)
rows = doc if isinstance(doc, list) else next(v for v in doc.values() if isinstance(v, list))
row = next(r for r in rows if r["id"] == FID)
assert row["state"] == "planned", row["state"]
row["state"], row["pin"] = "pinned", PIN
row["evidence"] = ["docs/superpowers/state/strict-delivery/reviews/S2-T9.md"]
row["history"] += [{"state": "in-progress", "sha": os.environ["SHA"], "by": "controller"},
                   {"state": "pinned", "sha": os.environ["SHA"], "by": "controller"}]
open(P, "w", encoding="utf-8").write(header + yaml.safe_dump(doc, sort_keys=False, allow_unicode=True))
EOF
git add docs/superpowers/state/findings.yml docs/superpowers/state/strict-delivery/mutations/SD-S2-T9.txt docs/superpowers/state/strict-delivery/reviews/S2-T9.md docs/superpowers/state/strict-delivery/ledger.md
python3 tools/gates/gate_register.py                                       # evidence must be tracked: stage first
git commit -m "chore(state): SD-S2-T9 pinned; ledger S2-T9"
```
Expected: `gate_register` all `PASS` (on the staged files); then both suites and `python3 tools/gates/run_all.py` (all `PASS`) before the commit.

- [ ] **Step 9: Review package and ledger (controller)** — `make_review_package.py strict-delivery S2-T9 <task base> <task head>`, implementer files, `gate_review_package.py --dir .superpowers/sdd/strict-delivery/review/S2-T9` → `PASS`, reviewer; after ✅ the controller runs the pin step above, whose commit carries the ledger line `- S2-T9 DONE <task head sha> new_plan_state + checklists; review docs/superpowers/state/strict-delivery/reviews/S2-T9.md`.

### b6 checkpoint

From this block on `gate_checkpoint` runs the checkpoint itself: the Stop hook calls `gate_checkpoint --plan strict-delivery` (pre-merge on `block/strict-delivery-b6`, `--post` on main) whenever the plan is in flight, and `checklists/block-end.md` is the sequence.

- [ ] **Step 1: CHECKPOINT b6** — run `~/.claude/skills/strict-delivery/checklists/block-end.md` steps 1–13 with `PLAN=strict-delivery`, `N=6`, `BR=block/strict-delivery-b6`. Handoff values: `block: 6`, `next_task: none` (b6 is this sub-plan's last block; the resume prompt names S3), `findings_in_flight: [{SD-S2-T7: pinned}, {SD-S2-T8: pinned}, {SD-S2-T9: pinned}]`. Before step 12, two repo facts will fail `gate_checkpoint --plan strict-delivery --post` unless the owner acts; each is an owner decision, so write it to `decisions.yml` and stop rather than ruling:
  1. `git worktree list` shows `/Users/benji/projects/personal/lap-timer-worktrees/s52` (branch `s5.2-conn-ble`, parked per sub-project C) → `checkpoint.worktrees` FAIL. Options: remove the worktree and keep the branch listed in `parked_branches` (cost: re-create the worktree when C starts), or amend the gate to accept worktrees of parked branches (cost: spec §3.4 "main alone" weakened).
  2. Local branch `pre-bench-wrap-up` (and `s5.2-conn-ble`) must be listed in `parked_branches` with reason and owner, or deleted → `checkpoint.branches`.
  After step 12 passes, the resume prompt is:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 7 (sub-plan S3, task 1) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```
