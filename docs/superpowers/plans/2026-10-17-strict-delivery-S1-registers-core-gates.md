# Strict Delivery S1 — Registers and Core Gates Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. From block b3 on, also invoke the `strict-delivery` skill first.

**Goal:** Install the three registers, the gate library, `gate_register`, `gate_claims`, `gate_decisions`, `run_all`, the required CI job `gates`, the Claude Code and git hooks wired to them, `CLAUDE.md`, and the `strict-delivery` skill skeleton with its eight prompt blocks: rollout steps 1–2 of the spec, blocks `strict-delivery-b1..b3` of the umbrella plan.

**Architecture:** YAML registers under `docs/superpowers/state/` are parsed by `tools/gates/lib/` (a strict loader that refuses duplicate keys, schema validators, lifecycle rules, git/gh helpers). Each gate is a script with `check(args) -> Result` and `main(argv)`; `run_all.py` aggregates. The same scripts run from the CI job `gates`, from the Claude Code `PreToolUse` hook on `git commit|push`, and from the git `pre-commit` shim. Every failure mode is a `FAIL` line; nothing is skipped.

**Tech Stack:** Python 3.9+ (local `/usr/bin/python3` is 3.9.6; CI uses 3.12 through `actions/setup-python@v5`), PyYAML 6.0.2, `python3 -m unittest`, POSIX sh / bash, GitHub Actions, Claude Code hooks.

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (APPROVED 2026-10-17). **Umbrella:** `docs/superpowers/plans/2026-10-17-strict-delivery.md`. It locks the file structure, the shared interfaces and the block boundaries. Signatures, field names and the "CLI flags and conventions" table are copied from it; every place this plan adds to them or reads them differently is listed under "Interface notes", so the owner can see each one.

## Global Constraints

- Every umbrella Global Constraint applies verbatim. The ones that bind S1 most:
- Block = PR, at most 3 tasks; branch `block/strict-delivery-b<N>`; tag `strict-delivery-b<N>` on the squash commit (spec §11.2).
- Merge only on every CI job green, advisory included, under `grant:strict-delivery` = `DEC-0001` answered `"yes"` by the owner. The owner answers DEC-0001 in chat at b1 setup Step 0.1, before the docs PR merges. Task 1 records the answer.
- Model map: implementers sonnet (haiku only to transcribe the complete code in this plan); task reviewers, final reviewer and controller opus; final verifier Fable (umbrella Owner ruling 9).
- Zero warnings, zero failing tests, zero gate failures at every commit once the gate exists. A missing input is a FAIL (spec §7).
- "Production path" for a gate = its `main()` run on a fixture tree, or the hook script run as a subprocess with JSON on stdin; never a helper called on its own (umbrella Global Constraints).
- Python files start with `from __future__ import annotations` (Python 3.9 cannot evaluate `dict | None` at runtime) and use only the stdlib plus PyYAML.
- Run every command from the repo root `/Users/benji/projects/personal/lap-timer` (or the block's worktree root). The repo has a top-level directory `main/`, so `git log main` is ambiguous (verified: `fatal: ambiguous argument 'main': both revision and filename`). Write `git log main --` or `origin/main`.
- Every tracked text file passes ci.yml's hygiene steps: no trailing whitespace (`ci.yml:14`), and no hit for the stale-comment pattern (`ci.yml:55`) unless it has an allow entry (Task 1 adds the only one).
- Commits: each commit message ends with the committing agent's own `Co-Authored-By:` trailer (umbrella Global Constraints). The commit commands below show the subject and body; add the trailer line.
- Hardware: nothing in S1 flashes a board or opens a serial port.

## Review Focus

Inputs and conditions the spec implies that a person will hit and that no umbrella test names. Each one is pinned by the test named here, in the task that owns the code:

1. **YAML implicit typing in hand-edited registers.** `sha: 7265059` is read as an int, `answer: yes` as `True`, `state: false` as `False`. Each must fail with a message that says to quote it. It must never be accepted silently or misread as a grant. Pinned: `test_registers.py::FindingSchema::test_unquoted_numeric_sha_is_named`, `::DecisionSchema::test_unquoted_yes_is_named` (Task 2), `test_gate_claims.py::ClaimsRepo::test_unquoted_false_state_is_named` (Task 4).
2. **A duplicate key in a hand-edited row** (two `state:` lines). PyYAML silently keeps the last one. The loader must refuse it. Pinned: `test_lib.py::StrictYaml::test_duplicate_key_is_refused` (Task 1).
3. **A grant answered "no", or a grant for a similarly named plan.** It must not count as a grant. Pinned: `test_registers.py::Queries::test_grant_needs_an_exact_plan_and_a_yes` (Task 2).
4. **A commit from a worktree while `CLAUDE_PROJECT_DIR` points at the main checkout, or with register edits left unstaged.** The git hook must gate the tree actually being committed. Pinned: `test_hooks.py::GitHooks::test_pre_commit_blocks_a_failing_register` (it sets `CLAUDE_PROJECT_DIR=/nonexistent-project`) and `::test_pre_commit_blocks_unstaged_register_edits` (Task 7).
5. **A commit on a branch that predates the gates** (parked branches such as `s5.2-conn-ble`). The shared `.git/hooks/pre-commit` must block with a reason that names the cause, not a shell error. Pinned: `test_hooks.py::GitHooks::test_a_tree_without_the_gates_cannot_commit` (Task 7).

The umbrella Review Focus rows that S1 owns are pinned under the names the umbrella gives: `test_lib.py::...::test_missing_input_is_fail` (Task 1), `test_gate_register.py::...::test_pinned_without_transcript_fails` (Task 3), `test_gate_claims.py::...::test_touching_cited_file_marks_stale` (Task 4), `test_hooks.py::...::test_hook_fails_closed_when_script_errors` (Task 7).

---

## Repo facts this plan relies on (verified 2026-10-17 at `24aa0c7`; re-verify before use)

| Fact | Command | Observed |
|---|---|---|
| No `tools/__init__.py`, `tools/gates/`, `CLAUDE.md`, `docs/superpowers/state/` yet | `ls tools/__init__.py CLAUDE.md docs/superpowers/state` | all "No such file or directory" |
| `.claude/` holds only `worktrees/`; `.gitignore` ignores `.claude/worktrees/` | `ls .claude; grep -n worktrees .gitignore` | `worktrees`; `.claude/worktrees/` |
| `.gitignore` ignores `.superpowers/` and `__pycache__/` | `grep -n -e superpowers -e pycache .gitignore` | both present |
| `tools/requirements.txt` = header + pyserial, Pillow, esptool | `cat tools/requirements.txt` | 4 lines, no PyYAML |
| ci.yml jobs | `grep -n "^  [a-z-]*:$" .github/workflows/ci.yml` | hygiene, host-tests, dc-host-tests, tools-tests (`ci.yml:124-144`), static-analysis, tracks-generated, coverage |
| tools-tests lists its modules explicitly and needs no install | `sed -n 124,140p .github/workflows/ci.yml` | `python3 -m unittest -v tools/test_devkit.py ...`; `fetch-depth: 0` |
| Stale-comment step's allow array and pattern | `sed -n 35,56p .github/workflows/ci.yml` | allow ends at line 52, `)` at 53; pattern at line 55 includes `later sessions?` over `tools` |
| The umbrella's `config.yml` trips that pattern | `git grep -nIE 'later sessions?' -- tools` on the prototype | `tools/gates/config.yml:27:  - 'later sessions?'` |
| Required checks on `main` | `gh api repos/superbrobenji/lap-timer/branches/main/protection --jq ...` | 9 contexts (hygiene, host-tests, tracks-generated, build ×3, dc-host-tests, tools-tests, static-analysis); `strict: true`, `enforce_admins: true`, `required_linear_history: true`, no PR-review rule |
| Spec and umbrella are not on `main` | `git log --oneline main.. --` on `pre-bench-wrap-up` | 4 commits (06921f4, 5d95a53, dc26459, 24aa0c7) ahead, branch not pushed; umbrella plan untracked |
| unittest discovery needs package markers | prototype: `python3 -m unittest discover -s tools/gates/tests -t .` without `tests/__init__.py` | `ImportError: Start directory is not importable` (3.9.6 and 3.12.14) |
| Local interpreters | `python3 --version; python3 -c "import yaml; print(yaml.__version__)"` | 3.9.6, PyYAML 6.0.3; `tools/.venv` python 3.12.14 has PyYAML 6.0.3 |
| `core.hooksPath` unset; no `.git/hooks/pre-commit` | `git config --get core.hooksPath; ls .git/hooks \| grep -v sample` | rc 1; nothing |
| git version | `git --version` | 2.50.1 |

Every Task 1-6 code block that gives a whole file (code, tests, fixtures, config.yml, the state README, gates.yml, the gates README) is byte-equal to the file shipped at block b2; the blocks that give only a starting point (DEC-0001 with its sha placeholder, the ledger's opening lines, the ci.yml insertion) equal the matching lines of the live file. At b2 198 tests pass on Python 3.9.6 and 3.12.14 and `run_all.py` gives 15 PASS lines. The totals of Tasks 7-9 (211, 218) are expected values derived from that count, not yet run.

## Interface notes (additions and readings the owner should see; nothing below changes an umbrella signature)

1. **Package markers and one test helper not in the locked file list:** `tools/gates/__init__.py` and `tools/gates/tests/__init__.py` (empty; unittest discovery refuses a start dir that is not a package, see the facts table), and `tools/gates/tests/gitfixture.py` (throwaway git repos for tests). No `tools/__init__.py`: `tools` stays a namespace package, so the existing `tools-tests` invocations are untouched.
2. **Additive helpers in `lib/__init__.py`:** `CODE_ROOT` (the checkout holding the gate code; config is read from here, never from `--root`, so fixture trees carry no config copy), `load_yaml(path, root=None)` (strict loader; with `root` every message names the file relative to it), `gate_args(prog)` (shared `--root`; `add_help=False`, `allow_abbrev=False`, `-h/--help` prints on stderr so `--json` stdout stays JSON), and `run_check(name, check, args) -> Result` (the fail-closed wrapper that both `run_gate` and `run_all.run` use). Additive helpers in `gitinfo.py`: `commit_exists(root, sha) -> bool`. Additive helpers in `registers.py`: `owner_errors`, `transition_errors`, `evidence_errors`, `finding_commit_errors`, `claim_commit_errors`, `decision_commit_errors`, `STATE_DIR`, `SEVERITIES`, the id regexes (ASCII digits only). In `gate_claims.py`: `verified_at_head(root, claim, head) -> bool`, for S2's `gate_dispatch`.
3. **"verified only if `verified_at == HEAD`" cannot be met literally.** A row committed in commit C cannot hold C's own sha, so a literal check fails every verified row at every HEAD. Implemented reading: a row is verified at HEAD when `state == verified` and no cited path differs between `verified_at` and HEAD, and none is staged. A cite ending in `/` covers its subtree. An unknown `verified_at` is a FAIL. This reading also covers spec §4.2's "a commit touching a cited file marks dependent rows stale": the gate fails until the row is marked or re-verified.
4. **`grant_for(decisions, plan, *, owner=None)` counts only a schema-valid row answered `"yes"`** (case-insensitive) with `answered_at.by` equal to the owner (default: config `owner.handle`), and only an exact `grant:<plan>` question, or that question followed by a space. The umbrella says "answered"; read literally, an answer of "no" would grant a merge.
5. **The umbrella's verbatim `config.yml` fails `ci.yml`'s stale-comment step** (`comment_patterns` contains `'later sessions?'`). Task 1 adds one allow entry, the minimum change, rather than altering the verbatim config.
6. **Handoff timing:** settled by the umbrella's "Owner rulings on the writers' questions", ruling 2 (no post-merge commits on protected `main`; the handoff lands on the block branch before the merge).
7. **Final verifier model:** settled by the umbrella's "Owner rulings on the writers' questions", ruling 9 (Fable on the final adversarial verifier and the exception reviewer).
8. **"Every id in any audit or review report exists here"** (spec §7) is built in Task 3 to owner ruling 5's grammar. A registered report is a file under `docs/reviews/` or `docs/superpowers/state/briefs/` whose first 20 lines contain the exact line `register: true` (DEC-0002: any line that starts `register:` (any case, optionally after a BOM) but is not exactly the marker, such as `register: yes`, `register: "true"` or a BOM before the marker, is a `FAIL register.marker.<file>`, never a silent un-register). Its ids are table first cells and leading list-item ids matching the finding regex, bare or wrapped in `**` / backticks, after an optional checkbox, followed by a space, tab, colon, comma, period or the end of the line. Unmarked files are ignored, so today's four reviews (none carries the marker, verified with `head -20 | grep -qx 'register: true'`) pass until the bootstrap marks them. `docs/reviews` must exist; `docs/superpowers/state/briefs` may be absent.
9. **Review packages at S1** are written under `.superpowers/sdd/strict-delivery/review/` (gitignored), not under the umbrella's `docs/superpowers/state/<plan>/review/`. A tracked `diff.patch` fails `ci.yml:14` (`git diff --check`): an unchanged blank line is a context line holding one space, which counts as trailing whitespace. S2 must settle the tracked location.
10. **Spec and plans are not on `main`** (facts table). Owner ruling 6: a docs-only PR from `pre-bench-wrap-up` merges first (b1 setup Step 0), and b1 and every later block branch from `main`.
11. **DEC-0002 and DEC-0003** (owner-delegated 2026-10-17, recorded in `decisions.yml` with the owner's quoted words): DEC-0002 = tighten the ruling-5 id grammar and fail near-miss markers; DEC-0003 = verify, every sha in the three registers (`history`, `cites`, evidence entries shaped like a sha, `asked_at`, `answered_at`, `verified_at`) must name a commit of the gated repository (`git cat-file -e <sha>^{commit}`). Fixtures therefore run in throwaway git repos (Task 3 `fixture_repo`), and S4's post-merge step must re-point a block branch's shas to the squash commit when it sets `merged`. `validate_decision(d, owner=None)` also requires `answered_at.by` to be the owner (umbrella schema), so `deferred` evidence and `grant_for` both rest on it.

## Files (S1 creates or modifies exactly these)

| Path | Task | Responsibility |
|---|---|---|
| `tools/gates/__init__.py`, `tools/gates/tests/__init__.py` | 1 | package markers (empty) |
| `tools/gates/lib/__init__.py` | 1 | Gate framework, strict YAML, `repo_root`, `CODE_ROOT` |
| `tools/gates/lib/config.py`, `tools/gates/config.yml` | 1 | config loader; config verbatim from the umbrella |
| `tools/gates/lib/gitinfo.py` | 1 | git/gh helpers |
| `tools/gates/tests/gitfixture.py`, `tools/gates/tests/test_lib.py` | 1 | test helper; framework/config/gitinfo tests |
| `docs/superpowers/state/{README.md,findings.yml,claims.yml,decisions.yml}` | 1 | registers bootstrapped: 0 findings, 0 claims, DEC-0001 |
| `docs/superpowers/state/strict-delivery/ledger.md` | 1 | this plan's ledger |
| `tools/requirements.txt` | 1 | `PyYAML==6.0.2` |
| `.github/workflows/ci.yml` (allow array, after line 52) | 1 | allow the config's own pattern line |
| `tools/gates/lib/registers.py`, `tools/gates/tests/test_registers.py` | 2 | loaders, validators, transitions |
| `tools/gates/gate_register.py`, `tools/gates/tests/test_gate_register.py`, `tools/gates/tests/fixtures/register/**` | 3 | the findings gate |
| `tools/gates/gate_claims.py`, `tools/gates/tests/test_gate_claims.py` | 4 | the claims gate |
| `tools/gates/gate_decisions.py`, `tools/gates/tests/test_gate_decisions.py` | 5 | the decisions gate |
| `tools/gates/run_all.py`, `tools/gates/tests/test_run_all.py`, `.github/workflows/gates.yml`, `tools/gates/README.md` | 6 | aggregation, CI job `gates`, docs |
| `tools/gates/hooks/{pre_tool_use.py,git-pre-commit.sh}`, `tools/gates/install-hooks.sh`, `tools/gates/tests/test_hooks.py` | 7 | Bash gating, git hook |
| `tools/gates/hooks/{session_start.py,user_prompt_submit.py}`, `.claude/settings.json`, `CLAUDE.md`, `tools/gates/README.md` (Hooks section), `test_hooks.py` (more classes) | 8 | session hooks, wiring, overrides |
| `~/.claude/skills/strict-delivery/SKILL.md`, `~/.claude/skills/strict-delivery/prompts/*.md` (8 files) | 9 | skill skeleton + prompt blocks (outside the repo; ledgered by sha256) |

---

# Block b1 — library, registers, `gate_register` (PR 1, tag `strict-delivery-b1`)

### Block b1 setup (controller, before Task 1)

- [ ] **Step 0: The owner answers DEC-0001, the docs land on `main`, then b1 branches from `main` (owner ruling 6)**

Owner ruling 6 parks the pre-bench fix work until this plan completes, and requires b1 and every later block to branch from `main`. The docs-only PR below is merged by the controller under `grant:strict-delivery`. That grant must therefore be answered first. DEC-0001 does not exist as a register row until Task 1, so the owner answers in chat now, and Task 1 records the answer per `docs/superpowers/state/README.md`. If the owner does not answer `yes`, the owner merges the docs PR by hand, and Task 1 records DEC-0001 unanswered.

(0.1) Ask the owner, verbatim: "DEC-0001 grant:strict-delivery -- may the controller merge each strict-delivery PR (the docs PR now, then blocks b1-b13) on all-green under this standing grant? Options: yes (each merges on all-green; every red line stops) / no (the owner merges each PR by hand)." Stop until the owner answers. Save the reply verbatim:

```bash
mkdir -p .superpowers/sdd/strict-delivery
cat > .superpowers/sdd/strict-delivery/DEC-0001-answer.txt <<'ANSWER'
<the owner's chat message, verbatim>
<the date and time it was sent>
ANSWER
```

(0.2) Open the docs-only PR from `pre-bench-wrap-up`. It carries the 2026-10-16 re-audit, the 2026-10-17 drift analysis, the strict-delivery spec and the six plan files.

```bash
git switch pre-bench-wrap-up
git status --short                                             # empty (commit any plan edits first)
git log --oneline main.. --                                     # docs commits only
git diff --stat main...pre-bench-wrap-up -- . ':!docs'          # empty: nothing outside docs/
cat > .superpowers/sdd/strict-delivery/docs-pr-body.md <<'BODY'
## Summary

Docs only, per owner ruling 6 of docs/superpowers/plans/2026-10-17-strict-delivery.md: the pre-bench fix work is parked until the strict-delivery plan completes, and every block branches from main.

- docs/reviews/2026-10-16-audit-post-fix-wave.md -- re-audit of main after the fix wave
- docs/reviews/2026-10-17-audit-drift-and-gaps.md -- drift and fix-wave gap analysis
- docs/superpowers/specs/2026-10-17-strict-delivery-design.md -- strict-delivery design (APPROVED 2026-10-17)
- docs/superpowers/plans/2026-10-17-strict-delivery*.md -- umbrella plan and sub-plans S1-S5

No code, CI, tool or firmware change. Merged by the controller under grant:strict-delivery (DEC-0001, answered by the owner in chat; recorded in decisions.yml by S1 Task 1).
BODY
git push -u origin pre-bench-wrap-up
gh pr create --base main --head pre-bench-wrap-up --title "docs: re-audit, drift analysis, strict-delivery spec and plans" --body-file .superpowers/sdd/strict-delivery/docs-pr-body.md
gh pr checks pre-bench-wrap-up --watch                          # every job green, advisory included
```

(0.3) Merge, but only if the reply saved in 0.1 is a yes. Otherwise the owner merges, and the controller waits. Then delete the branch, return to `main`, and remove the parked worktree. The branch `s5.2-conn-ble` stays: it is listed as parked, owner "sub-project C", in every handoff.

```bash
gh pr merge pre-bench-wrap-up --squash
git push origin --delete pre-bench-wrap-up 2>/dev/null || true   # gone already if GitHub auto-deleted it
git switch main && git pull --ff-only
git branch -D pre-bench-wrap-up
git -C ../lap-timer-worktrees/s52 status --short                # empty; else stop and ask the owner
git worktree remove ../lap-timer-worktrees/s52 || { rm -rf ../lap-timer-worktrees/s52 && git worktree prune; }   # spec §3.4: rm + prune for a tree with submodules
git worktree list                                               # the main checkout only
git branch --format='%(refname:short)'                          # main, s5.2-conn-ble
git switch -c block/strict-delivery-b1
git rev-parse --short=7 HEAD                                    # record as BASE_B1 in the ledger (Task 1 Step 7)
```

### Task 1: Gate framework, config, git helpers, register bootstrap

**Files:**
- Create: `tools/gates/__init__.py`, `tools/gates/tests/__init__.py` (both empty)
- Create: `tools/gates/lib/__init__.py`, `tools/gates/lib/config.py`, `tools/gates/config.yml`, `tools/gates/lib/gitinfo.py`
- Create: `tools/gates/tests/gitfixture.py`, `tools/gates/tests/test_lib.py`
- Create: `docs/superpowers/state/README.md`, `docs/superpowers/state/findings.yml`, `docs/superpowers/state/claims.yml`, `docs/superpowers/state/decisions.yml`, `docs/superpowers/state/strict-delivery/ledger.md`
- Modify: `tools/requirements.txt` (append one line), `.github/workflows/ci.yml` (insert two lines after line 52)

**Interfaces:**
- Consumes: nothing.
- Produces (umbrella "Gate contract", verbatim): `Line(status, id, reason="")`; `Result(gate, lines)` with `ok() -> bool`, `fail(id, reason)`, `passed(id)`, `render(as_json) -> str`; `GateError`; `repo_root() -> str`; `run_gate(name, check, argv=None) -> int`. Additive: `CODE_ROOT: str`, `load_yaml(path) -> object`, `gate_args(prog) -> argparse.ArgumentParser` (adds `--root`), `run_check(name, check, args: list) -> Result`. `config.load(root) -> dict`. `gitinfo.head_sha(root) -> str`, `branch(root) -> str`, `status_clean(root) -> bool`, `worktrees(root) -> list[str]`, `changed_files(root, base, head="HEAD") -> list[str]`, `staged_files(root) -> list[str]`, `pr_for_branch(root) -> dict | None`, `check_runs(pr) -> list[dict]` (`{name, conclusion}`), `tags_at(root, sha) -> list[str]`. Test helper `gitfixture`: `GIT`, `REPO`, `STATE`, `git(root, *args) -> str`, `write(root, rel, text) -> str`, `new_repo(registers=None) -> str`, `commit_all(root, msg="change") -> str`, `remove(root)`.

- [ ] **Step 1: Install PyYAML for the local interpreter and pin it**

```bash
python3 -c "import yaml; print(yaml.__version__)" || python3 -m pip install --user PyYAML==6.0.2
printf 'PyYAML==6.0.2\n' >> tools/requirements.txt
tail -1 tools/requirements.txt    # PyYAML==6.0.2
```

(PyYAML 6.0.3 is already installed locally; 6.0.x is API-identical for `SafeLoader`/`safe_dump`. CI installs exactly 6.0.2.)

- [ ] **Step 2: Write the test helper and the failing tests**

```bash
mkdir -p tools/gates/lib tools/gates/tests && : > tools/gates/__init__.py && : > tools/gates/tests/__init__.py
```

`tools/gates/tests/gitfixture.py`:

```python
"""Throwaway git repositories for gate tests. Every git call pins identity, signing and hooks so
the developer's global config (gpgsign, core.hooksPath, templates) cannot change the result."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile

GIT = ["git", "-c", "user.name=gate-test", "-c", "user.email=gate-test@example.invalid",
       "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "core.hooksPath=/dev/null",
       "-c", "init.defaultBranch=main"]
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
STATE = os.path.join("docs", "superpowers", "state")


def git(root, *args) -> str:
    p = subprocess.run(GIT + ["-C", root, *args], capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise AssertionError(f"git {' '.join(args)}: {p.stderr}")
    return p.stdout.strip()


def write(root, rel, text) -> str:
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def new_repo(registers=None) -> str:
    """A fresh repo with one commit. `registers` maps findings/claims/decisions to YAML text
    (default "[]\\n" each); pass a value None to leave that file out."""
    root = tempfile.mkdtemp(prefix="gate-test-")
    git(root, "init", "-q")
    regs = {"findings": "[]\n", "claims": "[]\n", "decisions": "[]\n"}
    regs.update(registers or {})
    for name, text in regs.items():
        if text is not None:
            write(root, os.path.join(STATE, f"{name}.yml"), text)
    write(root, "README.md", "fixture\n")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "fixture")
    return root


def commit_all(root, msg="change") -> str:
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", msg)
    return git(root, "rev-parse", "HEAD")


def remove(root) -> None:
    shutil.rmtree(root, ignore_errors=True)
```

`tools/gates/tests/test_lib.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/lib (Gate framework, config, gitinfo). Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import json
import os
import stat
import tempfile
import unittest

from tools.gates import lib
from tools.gates.lib import GateError, Result, config, gitinfo
from tools.gates.tests import gitfixture as gf


def run(check, args, name="demo"):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = lib.run_gate(name, check, args)
    return rc, buf.getvalue()


class GateFramework(unittest.TestCase):
    def test_missing_input_is_fail(self):
        def check(args):
            raise GateError("docs/superpowers/state/findings.yml: No such file or directory")
        rc, out = run(check, [])
        self.assertEqual(1, rc)
        self.assertEqual("FAIL demo.input: docs/superpowers/state/findings.yml: No such file or directory\n", out)

    def test_an_empty_result_is_fail_never_pass(self):
        rc, out = run(lambda args: Result("demo"), [])
        self.assertEqual(1, rc)
        self.assertEqual("FAIL demo.empty: gate produced no verdict\n", out)

    def test_an_unexpected_exception_is_fail(self):
        def check(args):
            raise KeyError("state")
        rc, out = run(check, [])
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL demo.input: "), out)

    def test_bad_arguments_are_fail_not_exit(self):
        def check(args):
            lib.gate_args("demo").parse_args(args)
            return Result("demo")
        with contextlib.redirect_stderr(io.StringIO()):
            rc, out = run(check, ["--no-such-flag"])
        self.assertEqual(1, rc)
        self.assertIn("FAIL demo.input: bad arguments", out)

    def test_pass_and_fail_lines_render_and_set_rc(self):
        def check(args):
            r = Result("demo")
            r.passed("demo.a")
            if args == ["bad"]:
                r.fail("demo.b", "broken")
            return r
        self.assertEqual((0, "PASS demo.a\n"), run(check, []))
        self.assertEqual((1, "PASS demo.a\nFAIL demo.b: broken\n"), run(check, ["bad"]))

    def test_json_flag_is_stripped_and_renders_json(self):
        seen = []
        def check(args):
            seen.append(args)
            r = Result("demo")
            r.fail("demo.x", "why")
            return r
        rc, out = run(check, ["--json", "--root", "r"])
        self.assertEqual([["--root", "r"]], seen)
        self.assertEqual({"gate": "demo", "ok": False,
                          "lines": [{"status": "FAIL", "id": "demo.x", "reason": "why"}]}, json.loads(out))
        self.assertEqual(1, rc)

    def test_repo_root_prefers_claude_project_dir(self):
        old = os.environ.get("CLAUDE_PROJECT_DIR")
        os.environ["CLAUDE_PROJECT_DIR"] = "/somewhere"
        try:
            self.assertEqual("/somewhere", lib.repo_root())
        finally:
            if old is None:
                del os.environ["CLAUDE_PROJECT_DIR"]
            else:
                os.environ["CLAUDE_PROJECT_DIR"] = old

    def test_code_root_is_the_checkout_holding_the_gates(self):
        self.assertTrue(os.path.isfile(os.path.join(lib.CODE_ROOT, "tools", "gates", "lib", "__init__.py")))


class StrictYaml(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="gate-yaml-")

    def tearDown(self):
        gf.remove(self.dir)

    def load(self, text):
        return lib.load_yaml(gf.write(self.dir, "x.yml", text))

    def test_duplicate_key_is_refused(self):
        with self.assertRaisesRegex(GateError, "duplicate key 'state'"):
            self.load("- {id: A-1, state: open, state: pinned}\n")

    def test_merge_key_is_refused(self):
        with self.assertRaisesRegex(GateError, "merge keys"):
            self.load("- {<<: {a: 1}, b: 2}\n")

    def test_invalid_and_missing_files_raise(self):
        with self.assertRaisesRegex(GateError, "invalid YAML"):
            self.load("x: [\n")
        with self.assertRaisesRegex(GateError, "No such file"):
            lib.load_yaml(os.path.join(self.dir, "absent.yml"))


class Config(unittest.TestCase):
    def test_repo_config_loads_with_the_owner_handle(self):
        cfg = config.load(lib.CODE_ROOT)
        self.assertEqual("superbrobenji", cfg["owner"]["handle"])
        self.assertEqual(["final_verifier", "exception_reviewer"], cfg["fable_roles"])
        self.assertEqual(12000, cfg["dispatch"]["max_prompt_chars"])

    def test_missing_config_and_missing_keys_raise(self):
        d = tempfile.mkdtemp(prefix="gate-cfg-")
        try:
            with self.assertRaisesRegex(GateError, "config.yml"):
                config.load(d)
            gf.write(d, "tools/gates/config.yml", "owner: {handle: x}\n")
            with self.assertRaisesRegex(GateError, "missing keys: triggers"):
                config.load(d)
        finally:
            gf.remove(d)


class GitInfo(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()

    def tearDown(self):
        gf.remove(self.root)

    def test_head_branch_status_worktrees(self):
        self.assertEqual(gf.git(self.root, "rev-parse", "HEAD"), gitinfo.head_sha(self.root))
        self.assertEqual("main", gitinfo.branch(self.root))
        self.assertTrue(gitinfo.status_clean(self.root))
        gf.write(self.root, "new.txt", "x\n")
        self.assertFalse(gitinfo.status_clean(self.root))
        self.assertEqual([os.path.realpath(self.root)],
                         [os.path.realpath(p) for p in gitinfo.worktrees(self.root)])

    def test_changed_and_staged_files(self):
        base = gitinfo.head_sha(self.root)
        gf.write(self.root, "a/b.c", "int x;\n")
        gf.git(self.root, "add", "a/b.c")
        self.assertEqual(["a/b.c"], gitinfo.staged_files(self.root))
        head = gf.commit_all(self.root)
        self.assertEqual(["a/b.c"], gitinfo.changed_files(self.root, base, head))
        self.assertEqual([], gitinfo.staged_files(self.root))

    def test_tags_at(self):
        gf.git(self.root, "tag", "demo-b1")
        self.assertEqual(["demo-b1"], gitinfo.tags_at(self.root, "HEAD"))

    def test_bad_revision_raises_gate_error(self):
        with self.assertRaises(GateError) as ctx:
            gitinfo.changed_files(self.root, "0000000", "HEAD")
        self.assertTrue(str(ctx.exception).startswith("git diff --name-only -z --no-renames 0000000 HEAD: "),
                        str(ctx.exception))
        self.assertNotIn(self.root, str(ctx.exception))

    def test_check_runs_reads_every_check_kind(self):
        pr = {"statusCheckRollup": [
            {"__typename": "CheckRun", "name": "gates", "conclusion": "SUCCESS"},
            {"__typename": "CheckRun", "name": "coverage", "conclusion": "FAILURE"},
            {"__typename": "StatusContext", "context": "ext/ci", "state": "PENDING"}]}
        self.assertEqual([{"name": "gates", "conclusion": "SUCCESS"},
                          {"name": "coverage", "conclusion": "FAILURE"},
                          {"name": "ext/ci", "conclusion": "PENDING"}], gitinfo.check_runs(pr))
        with self.assertRaises(GateError):
            gitinfo.check_runs({})


class PrForBranch(unittest.TestCase):
    """pr_for_branch() through a fake `gh` first on PATH (the real call path, no network)."""

    def setUp(self):
        self.bin = tempfile.mkdtemp(prefix="gate-gh-")
        self.old_path = os.environ["PATH"]
        os.environ["PATH"] = self.bin + os.pathsep + self.old_path

    def tearDown(self):
        os.environ["PATH"] = self.old_path
        gf.remove(self.bin)

    def fake_gh(self, script):
        path = gf.write(self.bin, "gh", "#!/bin/sh\n" + script)
        os.chmod(path, os.stat(path).st_mode | stat.S_IXUSR)

    def test_no_pr_is_none(self):
        self.fake_gh('echo \'no pull requests found for branch "x"\' >&2; exit 1\n')
        self.assertIsNone(gitinfo.pr_for_branch(self.bin))

    def test_pr_json_is_returned(self):
        self.fake_gh("echo '{\"number\": 7, \"statusCheckRollup\": []}'\n")
        self.assertEqual({"number": 7, "statusCheckRollup": []}, gitinfo.pr_for_branch(self.bin))

    def test_any_other_gh_failure_raises(self):
        self.fake_gh("echo 'HTTP 401: Bad credentials' >&2; exit 1\n")
        with self.assertRaisesRegex(GateError, "Bad credentials"):
            gitinfo.pr_for_branch(self.bin)


class ReviewRound1(unittest.TestCase):
    def test_json_only_recognised_as_first_argument(self):
        seen = []
        def check(args):
            seen.append(args)
            r = Result("demo")
            r.passed("demo.a")
            return r
        rc, out = run(check, ["--root", "--json"])
        self.assertEqual([["--root", "--json"]], seen)
        self.assertEqual("PASS demo.a\n", out)
        rc, out = run(check, ["--json", "--root", "x"])
        self.assertEqual([["--root", "--json"], ["--root", "x"]], seen)
        self.assertTrue(json.loads(out)["ok"])

    def test_repo_root_git_fallback_and_error(self):
        old = os.environ.pop("CLAUDE_PROJECT_DIR", None)
        cwd = os.getcwd()
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        try:
            os.chdir(lib.CODE_ROOT)
            self.assertEqual(os.path.realpath(gf.git(lib.CODE_ROOT, "rev-parse", "--show-toplevel")),
                             os.path.realpath(lib.repo_root()))
            os.chdir(outside)
            with self.assertRaisesRegex(GateError, "git rev-parse --show-toplevel"):
                lib.repo_root()
            old_path = os.environ["PATH"]
            os.environ["PATH"] = "/nonexistent"
            try:
                with self.assertRaisesRegex(GateError, "git rev-parse --show-toplevel"):
                    lib.repo_root()
            finally:
                os.environ["PATH"] = old_path
        finally:
            os.chdir(cwd)
            gf.remove(outside)
            if old is not None:
                os.environ["CLAUDE_PROJECT_DIR"] = old

    def test_unhashable_key_and_non_utf8_are_gate_errors(self):
        d = tempfile.mkdtemp(prefix="gate-yaml-")
        try:
            with self.assertRaisesRegex(GateError, "unhashable mapping key"):
                lib.load_yaml(gf.write(d, "k.yml", "? [a, b]\n: 1\n"))
            path = os.path.join(d, "b.yml")
            with open(path, "wb") as fh:
                fh.write(b"x: \xff\xfe\n")
            with self.assertRaisesRegex(GateError, "not valid UTF-8"):
                lib.load_yaml(path)
        finally:
            gf.remove(d)

    def test_missing_git_and_gh_binaries_are_gate_errors(self):
        root = gf.new_repo()
        old = os.environ["PATH"]
        os.environ["PATH"] = "/nonexistent"
        try:
            with self.assertRaisesRegex(GateError, "git"):
                gitinfo.head_sha(root)
            with self.assertRaisesRegex(GateError, "gh"):
                gitinfo.pr_for_branch(root)
        finally:
            os.environ["PATH"] = old
            gf.remove(root)

    def test_check_runs_rejects_non_dict_items(self):
        with self.assertRaises(GateError):
            gitinfo.check_runs({"statusCheckRollup": ["oops"]})

    def test_non_ascii_and_renamed_paths_are_verbatim(self):
        root = gf.new_repo()
        try:
            gf.write(root, "old.txt", "some content\nmore\nlines\n")
            gf.commit_all(root)
            base = gitinfo.head_sha(root)
            gf.git(root, "mv", "old.txt", "new.txt")
            gf.write(root, "\u00e9.txt", "x\n")
            gf.git(root, "add", "-A")
            self.assertEqual(["new.txt", "old.txt", "\u00e9.txt"], sorted(gitinfo.staged_files(root)))
            head = gf.commit_all(root)
            self.assertEqual(["new.txt", "old.txt", "\u00e9.txt"], sorted(gitinfo.changed_files(root, base, head)))
        finally:
            gf.remove(root)

    def test_detached_head_is_a_gate_error(self):
        root = gf.new_repo()
        try:
            gf.git(root, "checkout", "-q", "--detach")
            with self.assertRaises(GateError) as ctx:
                gitinfo.branch(root)
            self.assertEqual("git rev-parse --abbrev-ref HEAD: detached HEAD", str(ctx.exception))
        finally:
            gf.remove(root)


class FixWave(unittest.TestCase):
    """Block b1 final-review fix wave (items A6-A8, B4, B9, B10)."""

    def test_result_with_no_lines_is_not_ok(self):                                   # B4
        self.assertFalse(Result("g").ok())
        self.assertEqual(False, json.loads(Result("g").render(True))["ok"])
        r = Result("g")
        r.passed("g.a")
        self.assertTrue(r.ok())

    def test_text_render_is_one_physical_line_per_result_line(self):                 # B9
        r = Result("g")
        r.fail("g.a", "while parsing\n  a block\t\tsequence\n\nin x.yml")
        self.assertEqual("FAIL g.a: while parsing   a block\t\tsequence  in x.yml", r.render(False))   # breaks only
        r2 = Result("g")
        r2.fail("g.b", "a  b\r\nc\u2028d")
        self.assertEqual("FAIL g.b: a  b c d", r2.render(False))
        self.assertEqual("while parsing\n  a block\t\tsequence\n\nin x.yml",
                         json.loads(r.render(True))["lines"][0]["reason"])           # JSON keeps the raw text

    def test_help_goes_to_stderr_and_abbreviations_are_refused(self):                # B10
        def check(args):
            lib.gate_args("demo").parse_args(args)
            return Result("demo")
        for flag in ("--help", "-h"):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc, out = run(check, ["--json", flag])
            self.assertEqual(1, rc)
            self.assertFalse(out.startswith("usage"), out)
            self.assertEqual({"status": "FAIL", "id": "demo.input", "reason": "bad arguments (argparse exit 0)"},
                             json.loads(out)["lines"][0])
            self.assertIn("usage: demo", err.getvalue())
            self.assertIn("--root", err.getvalue())
        with contextlib.redirect_stderr(io.StringIO()):
            rc, out = run(check, ["--roo", "x"])
        self.assertEqual(1, rc)
        self.assertIn("FAIL demo.input: bad arguments", out)

    def test_load_yaml_messages_are_relative_to_the_root(self):                      # A7
        d = tempfile.mkdtemp(prefix="gate-yaml-")
        try:
            gf.write(d, "sub/x.yml", "- id: [\n")
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(os.path.join(d, "sub", "x.yml"), d)
            msg = str(ctx.exception)
            self.assertTrue(msg.startswith("sub/x.yml: invalid YAML: "), msg)
            self.assertIn('in "sub/x.yml", line', msg)
            self.assertNotIn(d, msg)
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(os.path.join(d, "sub", "absent.yml"), d)
            self.assertEqual("sub/absent.yml: No such file or directory", str(ctx.exception))
            with open(os.path.join(d, "sub", "b.yml"), "wb") as fh:
                fh.write(b"x: \xff\xfe\n")
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(os.path.join(d, "sub", "b.yml"), d)
            self.assertEqual("sub/b.yml: not valid UTF-8", str(ctx.exception))
            with self.assertRaises(GateError) as ctx:
                lib.load_yaml(gf.write(d, "sub/m.yml", "- {<<: {a: 1}}\n"), d)
            self.assertTrue(str(ctx.exception).startswith("sub/m.yml: invalid YAML: "), str(ctx.exception))
            self.assertNotIn(d, str(ctx.exception))
        finally:
            gf.remove(d)

    def test_config_messages_are_relative_to_the_root(self):                         # A7
        d = tempfile.mkdtemp(prefix="gate-cfg-")
        try:
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertEqual("tools/gates/config.yml: No such file or directory", str(ctx.exception))
            gf.write(d, "tools/gates/config.yml", "- a\n")
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertEqual("tools/gates/config.yml: top level must be a mapping", str(ctx.exception))
            gf.write(d, "tools/gates/config.yml", "owner: {handle: x}\n")
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertTrue(str(ctx.exception).startswith("tools/gates/config.yml: missing keys: triggers"))
            with open(os.path.join(lib.CODE_ROOT, "tools", "gates", "config.yml"), encoding="utf-8") as fh:
                good = fh.read()
            gf.write(d, "tools/gates/config.yml", good.replace("owner: {handle: superbrobenji}", "owner: {handle: ' '}"))
            with self.assertRaises(GateError) as ctx:
                config.load(d)
            self.assertEqual("tools/gates/config.yml: owner.handle must be a non-empty string", str(ctx.exception))
        finally:
            gf.remove(d)

    def test_git_messages_do_not_carry_the_root(self):                               # A7
        root = gf.new_repo()
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        try:
            with self.assertRaises(GateError) as ctx:
                gitinfo.head_sha(outside)
            self.assertTrue(str(ctx.exception).startswith("git rev-parse HEAD: "), str(ctx.exception))
            self.assertNotIn(outside, str(ctx.exception))
            with self.assertRaises(GateError) as ctx:
                gitinfo.tags_at(root, "no-such-rev")
            self.assertTrue(str(ctx.exception).startswith("git tag --points-at no-such-rev: "))
            self.assertNotIn(root, str(ctx.exception))
        finally:
            gf.remove(root)
            gf.remove(outside)

    def test_status_clean_ignores_the_users_untracked_files_setting(self):          # A6
        root = gf.new_repo()
        try:
            gf.git(root, "config", "status.showUntrackedFiles", "no")
            gf.write(root, "untracked.txt", "x\n")
            self.assertEqual("", gf.git(root, "status", "--porcelain"))      # the config hides it ...
            self.assertFalse(gitinfo.status_clean(root))                      # ... the helper must not
        finally:
            gf.remove(root)

    def test_commit_exists_runs_git_in_the_c_locale_and_maps_return_codes(self):    # round 2, item 4
        import subprocess
        from unittest import mock
        calls = []

        def fake(rc, err):
            def run(cmd, **kw):
                calls.append(dict(kw, env=dict(kw.get("env") or {})))     # copy now: os.environ is restored later
                return subprocess.CompletedProcess(cmd, rc, "", err)
            return run

        with mock.patch.dict(os.environ, {"LC_ALL": "de_DE.UTF-8"}):     # whatever the CI image exports
            self._codes(fake, mock)
        self.assertTrue(calls and all(c["env"]["LC_ALL"] == "C" for c in calls), calls)

    def _codes(self, fake, mock):
        import subprocess
        for rc, err, want in ((0, "", True), (128, "fatal: Not a valid object name x", False),
                              (128, "error: could not get object info", False)):
            with mock.patch.object(subprocess, "run", fake(rc, err)):
                self.assertIs(want, gitinfo.commit_exists(".", "abcdef1"))
        for rc, err in ((128, "fatal: not a git repository"), (1, "Not a valid object name"), (2, "")):
            with mock.patch.object(subprocess, "run", fake(rc, err)):
                with self.assertRaises(GateError):
                    gitinfo.commit_exists(".", "abcdef1")

    def test_a_missing_root_has_its_own_message(self):                                 # round 2, item 6
        with self.assertRaises(GateError) as ctx:
            gitinfo.head_sha("no/such/dir")
        self.assertEqual("git rev-parse HEAD: root no/such/dir does not exist", str(ctx.exception))
        with self.assertRaises(GateError) as ctx:
            gitinfo.commit_exists("no/such/dir", "abcdef1")
        self.assertEqual("git cat-file -e abcdef1^{commit}: root no/such/dir does not exist", str(ctx.exception))

    def test_a_root_that_is_a_file_is_not_a_directory(self):                           # round 3, item 2
        d = tempfile.mkdtemp(prefix="gate-file-")
        self.addCleanup(gf.remove, d)
        f = gf.write(d, "f.txt", "x\n")
        with self.assertRaises(GateError) as ctx:
            gitinfo.head_sha(f)
        self.assertEqual(f"git rev-parse HEAD: root {f} is not a directory", str(ctx.exception))
        with self.assertRaises(GateError) as ctx:
            gitinfo.commit_exists(f, "abcdef1")
        self.assertEqual(f"git cat-file -e abcdef1^{{commit}}: root {f} is not a directory", str(ctx.exception))

    def test_commit_exists(self):                                                    # A8
        root = gf.new_repo()
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        try:
            head = gitinfo.head_sha(root)
            self.assertTrue(gitinfo.commit_exists(root, head))
            self.assertTrue(gitinfo.commit_exists(root, head[:7]))
            self.assertFalse(gitinfo.commit_exists(root, "0000000"))
            self.assertFalse(gitinfo.commit_exists(root, gf.git(root, "rev-parse", "HEAD^{tree}")[:7]))  # a tree
            self.assertFalse(gitinfo.commit_exists(root, "--all"))
            with self.assertRaises(GateError) as ctx:
                gitinfo.commit_exists(outside, head)
            self.assertTrue(str(ctx.exception).startswith("git cat-file -e "), str(ctx.exception))
            self.assertNotIn(outside, str(ctx.exception))
        finally:
            gf.remove(root)
            gf.remove(outside)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_lib -v`
Expected: ERROR, `ImportError: cannot import name 'GateError' from 'tools.gates.lib'` (the package is empty).

- [ ] **Step 4: Write the framework, config and git helpers**

`tools/gates/lib/__init__.py`:

```python
"""Gate framework -- the umbrella plan's "Gate contract" (docs/superpowers/plans/2026-10-17-strict-delivery.md).

Every gate module exposes `check(args: list) -> Result` and
`def main(argv=None): return run_gate("<name>", check, argv)`.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import asdict, dataclass, field
from typing import Callable, List, Optional

import yaml

# The checkout that holds this gate code (tools/gates/lib/__init__.py -> four dirnames up).
# Config is read from here, never from a gate's --root, so a fixture tree needs no config copy.
CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


@dataclass
class Line:
    status: str   # "PASS" | "FAIL"
    id: str       # e.g. "register.owner.A2-IO-N2"
    reason: str = ""


@dataclass
class Result:
    gate: str
    lines: List[Line] = field(default_factory=list)

    def ok(self) -> bool:
        return all(l.status == "PASS" for l in self.lines) and bool(self.lines)

    def fail(self, id: str, reason: str) -> None:
        self.lines.append(Line("FAIL", id, reason))

    def passed(self, id: str) -> None:
        self.lines.append(Line("PASS", id))

    def render(self, as_json: bool) -> str:
        if as_json:
            return json.dumps({"gate": self.gate, "ok": self.ok(),
                               "lines": [asdict(l) for l in self.lines]}, indent=1)
        # one physical line per result line: only line breaks in a reason (PyYAML's are multi-line) become
        # spaces; space runs stay, so a quoted value shows exactly what was found
        return "\n".join(f"{l.status} {l.id}" + (f": {' '.join(l.reason.splitlines())}" if l.reason else "")
                         for l in self.lines)


class GateError(Exception):
    """Raised for a missing/unreadable input; main() converts it to FAIL <gate>.input."""


def repo_root() -> str:
    """CLAUDE_PROJECT_DIR if set, else `git rev-parse --show-toplevel`."""
    env = os.environ.get("CLAUDE_PROJECT_DIR")
    if env:
        return env
    try:
        p = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True, check=False)
    except OSError as exc:
        raise GateError(f"git rev-parse --show-toplevel: {exc.strerror or exc}") from None
    if p.returncode != 0:
        raise GateError(f"git rev-parse --show-toplevel: {p.stderr.strip()}")
    return p.stdout.strip()


class _StrictLoader(yaml.SafeLoader):
    """SafeLoader that refuses a duplicate key (PyYAML silently keeps the last one) and merge keys."""


def _mapping_no_duplicates(loader, node, deep=False):
    seen = set()
    for key_node, _ in node.value:
        if key_node.tag == "tag:yaml.org,2002:merge":
            raise yaml.constructor.ConstructorError(
                None, None, "merge keys (<<) are not allowed: write every key out", key_node.start_mark)
        key = loader.construct_object(key_node, deep=deep)
        try:
            duplicate = key in seen
        except TypeError:
            raise yaml.constructor.ConstructorError(
                None, None, f"unhashable mapping key {key!r}", key_node.start_mark) from None
        if duplicate:
            raise yaml.constructor.ConstructorError(
                None, None, f"duplicate key {key!r}", key_node.start_mark)
        seen.add(key)
    return loader.construct_mapping(node, deep=deep)


_StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping_no_duplicates)


def load_yaml(path: str, root=None):
    """Parse one YAML file strictly. Missing, unreadable, invalid or duplicate-key -> GateError.
    With `root`, every message names the file relative to it (machine-independent output)."""
    shown = os.path.relpath(path, root) if root is not None else path
    try:
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        return yaml.load(text, Loader=_StrictLoader)  # noqa: S506 -- _StrictLoader is a SafeLoader
    except OSError as exc:
        raise GateError(f"{shown}: {exc.strerror or exc}") from None
    except UnicodeDecodeError:
        raise GateError(f"{shown}: not valid UTF-8") from None
    except yaml.YAMLError as exc:
        detail = str(exc).replace('"<unicode string>"', f'"{shown}"')   # the text was parsed from a str
        raise GateError(f"{shown}: invalid YAML: {detail}") from None


class _HelpToStderr(argparse.Action):
    """-h/--help prints the usage on stderr, so a gate's stdout (JSON with --json) is never usage text."""

    def __init__(self, option_strings, dest, **kwargs):
        super().__init__(option_strings, dest, nargs=0, default=argparse.SUPPRESS,
                         help="show this help on stderr and exit")

    def __call__(self, parser, namespace, values, option_string=None):
        parser.print_help(sys.stderr)
        parser.exit(0)


def gate_args(prog: str) -> argparse.ArgumentParser:
    """The arguments every gate shares: --root (the tree whose registers are checked)."""
    ap = argparse.ArgumentParser(prog=prog, add_help=False, allow_abbrev=False)
    ap.add_argument("-h", "--help", action=_HelpToStderr)
    ap.add_argument("--root", default=None,
                    help="tree holding docs/superpowers/state (default: repo_root())")
    return ap


def run_check(name: str, check: Callable[[list], Result], args: list) -> Result:
    """check(args), with every failure mode turned into a FAIL line (fail closed)."""
    try:
        result = check(list(args))
    except SystemExit as exc:          # argparse: bad arguments are a FAIL, never a skip
        result = Result(name)
        result.fail(f"{name}.input", f"bad arguments (argparse exit {exc.code})")
    except Exception as exc:           # GateError and anything unexpected
        result = Result(name)
        result.fail(f"{name}.input", str(exc) or type(exc).__name__)
    if not isinstance(result, Result):
        result = Result(name)
        result.fail(f"{name}.input", "check() did not return a Result")
    if not result.lines:
        result.fail(f"{name}.empty", "gate produced no verdict")
    return result


def run_gate(name: str, check: Callable[[list], Result], argv: Optional[list] = None) -> int:
    """Parses --json (recognised only as argv[0], where the CLI table puts it; anywhere else it is
    an ordinary token, e.g. another option's value) and gate-specific args, runs check(args) -> Result, prints, returns 0 if ok else 1.
    Any exception (incl. GateError) becomes a FAIL line `<name>.input: <message>` and rc 1.
    An empty Result (no lines) is a FAIL `<name>.empty: gate produced no verdict` (fail closed)."""
    argv = list(sys.argv[1:] if argv is None else argv)
    as_json = bool(argv) and argv[0] == "--json"
    result = run_check(name, check, argv[1:] if as_json else argv)
    print(result.render(as_json))
    return 0 if result.ok() else 1
```

`tools/gates/lib/config.py`:

```python
"""tools/gates/config.yml loader (umbrella plan, "Config")."""
from __future__ import annotations

import os

from . import GateError, load_yaml

REQUIRED = ["owner", "triggers", "models", "model_rank", "fable_roles", "block", "rulings",
            "dispatch", "merge_markers", "comment_patterns", "exceptions", "tag_pattern"]


def load(root) -> dict:
    """Parse <root>/tools/gates/config.yml; a missing file, key or owner handle is a GateError."""
    path = os.path.join("tools", "gates", "config.yml")    # messages name it relative to `root`
    data = load_yaml(os.path.join(root, path), root)
    if not isinstance(data, dict):
        raise GateError(f"{path}: top level must be a mapping")
    missing = [k for k in REQUIRED if k not in data]
    if missing:
        raise GateError(f"{path}: missing keys: {', '.join(missing)}")
    owner = data["owner"]
    handle = owner.get("handle") if isinstance(owner, dict) else None
    if not isinstance(handle, str) or not handle.strip():
        raise GateError(f"{path}: owner.handle must be a non-empty string")
    return data
```

`tools/gates/config.yml`, verbatim from the umbrella "Config" block. Check it with `diff <(sed -n '/^owner: {handle/,/^tag_pattern/p' docs/superpowers/plans/2026-10-17-strict-delivery.md) tools/gates/config.yml` (no output):

```yaml
owner: {handle: superbrobenji}
triggers:
  controller: {tasks: 3, tokens: 250000, minutes: 90}
  implementer: {tasks: 1, tokens: 150000}
  reviewer: {reviews: 1, tokens: 120000}
models:            # minimum model per role; dispatch hook enforces
  controller: opus
  implementer: sonnet
  implementer_transcription: haiku
  task_reviewer: opus
  final_reviewer: opus
  final_verifier: fable
  exception_reviewer: fable
  audit_reviewer: opus
  audit_verifier: opus
  investigator: opus
model_rank: {haiku: 1, sonnet: 2, opus: 3, fable: 4}
fable_roles: [final_verifier, exception_reviewer]     # the only roles allowed on fable
block: {max_tasks: 3}
rulings: {max_per_block: 5}
dispatch: {max_prompt_chars: 12000}
merge_markers: ["fix before merge", "draft", "parked", "TODO", "not for merge"]
comment_patterns:   # regex; a hit must name a finding id or #issue on the same line or the next
  - 'lands? (in|with)'
  - 'until Plan \d'
  - 'plan 0?\d'
  - 'later sessions?'
  - 'wired in'
  - 'session \d'
  - 'task \d'
exceptions:
  cost: {blast: {tooling: 1, ui: 2, timing: 3, data-integrity: 3, supervision: 3}, verifiability: [0, 2], reach: {transient: 0, stored_or_3plus: 2}}
  benefit: {complexity: {helper: 1, module: 2, task_or_60_lines: 3}, runtime: [0, 2]}
tag_pattern: '^[a-z0-9-]+-b[0-9]+$'
```

`tools/gates/lib/gitinfo.py`:

```python
"""git/gh helpers (umbrella plan, "Git/gh helpers"). Every call is
subprocess.run([...], capture_output=True, text=True, check=False); a non-zero rc raises
GateError(f"{cmd}: {stderr}") -- except `gh pr view` reporting that the branch has no PR,
which is the documented None of pr_for_branch()."""
from __future__ import annotations

import json
import os
import re
import subprocess

from . import GateError


def _env() -> dict:
    """git's messages are matched by text: pin the C locale."""
    return {**os.environ, "LC_ALL": "C"}


def _check_root(cmd: list, cwd) -> None:
    if cwd is not None and not os.path.isdir(cwd):
        why = "is not a directory" if os.path.exists(cwd) else "does not exist"
        raise GateError(f"{' '.join(cmd)}: root {cwd} {why}")


def _run(cmd: list, cwd=None) -> str:
    """Run `cmd` in `cwd`. Messages name the command, never the directory (machine-independent)."""
    _check_root(cmd, cwd)
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False, env=_env())
    except OSError as exc:
        raise GateError(f"{' '.join(cmd)}: {exc.strerror or exc}") from None
    if p.returncode != 0:
        raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")
    return p.stdout


def _git(root, *args) -> str:
    return _run(["git", *args], cwd=str(root))


def head_sha(root) -> str:
    return _git(root, "rev-parse", "HEAD").strip()


def branch(root) -> str:
    """The checked-out branch name; a detached HEAD is a GateError (never the string "HEAD")."""
    name = _git(root, "rev-parse", "--abbrev-ref", "HEAD").strip()
    if name == "HEAD":
        raise GateError("git rev-parse --abbrev-ref HEAD: detached HEAD")
    return name


def status_clean(root) -> bool:
    # status.showUntrackedFiles=no (a common user setting) would hide untracked files: pin it
    return _git(root, "-c", "status.showUntrackedFiles=all", "status", "--porcelain=v1", "-uall") == ""


def worktrees(root) -> list:
    """Paths from `git worktree list --porcelain` (the main checkout first)."""
    out = _git(root, "worktree", "list", "--porcelain")
    return [l[len("worktree "):] for l in out.splitlines() if l.startswith("worktree ")]


def changed_files(root, base: str, head: str = "HEAD") -> list:
    """Paths that differ between two commits (`git diff --name-only -z --no-renames`; -z means no C-quoting of non-ASCII paths)."""
    out = _git(root, "diff", "--name-only", "-z", "--no-renames", base, head)
    return [l for l in out.split("\0") if l]


def staged_files(root) -> list:
    out = _git(root, "diff", "--cached", "--name-only", "-z", "--no-renames")
    return [l for l in out.split("\0") if l]


def pr_for_branch(root):
    """`gh pr view` for the checked-out branch, or None when the branch has no PR."""
    cmd = ["gh", "pr", "view", "--json",
           "number,title,body,state,isDraft,headRefOid,statusCheckRollup"]
    try:
        p = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, check=False)
    except OSError as exc:
        raise GateError(f"{' '.join(cmd)}: {exc.strerror or exc}") from None
    if p.returncode != 0:
        if "no pull requests found" in p.stderr.lower():
            return None
        raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")
    try:
        return json.loads(p.stdout)
    except ValueError as exc:
        raise GateError(f"{' '.join(cmd)}: not JSON: {exc}") from None


def check_runs(pr: dict) -> list:
    """Every check on the PR, required or not, as {name, conclusion}. A CheckRun carries
    name/conclusion (conclusion is "" while it runs); a commit StatusContext carries context/state."""
    if not isinstance(pr, dict) or not isinstance(pr.get("statusCheckRollup"), list):
        raise GateError("check_runs: PR JSON has no statusCheckRollup list")
    runs = []
    for item in pr["statusCheckRollup"]:
        if not isinstance(item, dict):
            raise GateError(f"check_runs: statusCheckRollup item is not an object: {item!r}")
        name = item.get("name") or item.get("context") or ""
        conclusion = item.get("conclusion") or item.get("state") or ""
        runs.append({"name": name, "conclusion": conclusion})
    return runs


def commit_exists(root, sha) -> bool:
    """True when `sha` (full or abbreviated hex) names a commit in the repository at `root`
    (`git cat-file -e <sha>^{commit}`); False for an unknown, ambiguous or non-commit object;
    any other git failure (not a repository, no git) is a GateError."""
    if not re.fullmatch(r"[0-9a-f]{4,40}", sha):
        return False
    cmd = ["git", "cat-file", "-e", f"{sha}^{{commit}}"]
    _check_root(cmd, str(root))
    try:
        p = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, check=False, env=_env())
    except OSError as exc:
        raise GateError(f"{' '.join(cmd)}: {exc.strerror or exc}") from None
    if p.returncode == 0:
        return True
    if p.returncode == 128 and ("Not a valid object name" in p.stderr or "could not get object info" in p.stderr):
        return False
    raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")


def tags_at(root, sha) -> list:
    return [l for l in _git(root, "tag", "--points-at", sha).splitlines() if l]
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_lib -v`
Expected: `Ran 39 tests` … `OK`.

Mutation check: in `run_check`, replace `except Exception as exc:` with `except GateError as exc:`. `test_an_unexpected_exception_is_fail` must now ERROR. Restore it and run again: OK.

- [ ] **Step 6: Bootstrap the registers and allow the config's own pattern in ci.yml**

`docs/superpowers/state/findings.yml` and `docs/superpowers/state/claims.yml`, each exactly:

```yaml
[]
```

`docs/superpowers/state/decisions.yml`. The umbrella says DEC-0001 is "created in S1 Task 1". The owner answered it in chat at setup Step 0.1, and this records that answer per `docs/superpowers/state/README.md`. `__ASKED_SHA__` is replaced by the command right after this block. If the reply saved in `.superpowers/sdd/strict-delivery/DEC-0001-answer.txt` was not a yes, write `answer: null` and `answered_at: null` instead.

```yaml
- id: DEC-0001
  class: merge-grant
  question: "grant:strict-delivery"
  options: []
  asked_at: {sha: "__ASKED_SHA__", by: session}
  answer: "yes"
  answered_at: {sha: "__ASKED_SHA__", by: superbrobenji}
```

```bash
python3 - <<'EOF'
import subprocess
sha = subprocess.run(["git", "rev-parse", "--short=7", "HEAD"], capture_output=True, text=True, check=True).stdout.strip()
p = "docs/superpowers/state/decisions.yml"
s = open(p).read()
assert "__ASKED_SHA__" in s
open(p, "w").write(s.replace("__ASKED_SHA__", sha))
EOF
grep -n -e asked_at -e answer docs/superpowers/state/decisions.yml   # sha = the BASE_B1 sha; answer: "yes"; by: superbrobenji
```

`docs/superpowers/state/README.md`:

```markdown
# Strict-delivery state

The registers are the only place a finding, a claim or an owner decision exists
(spec `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` §4). Schemas: the umbrella
plan `docs/superpowers/plans/2026-10-17-strict-delivery.md`, "Registers"; enforced by
`tools/gates/lib/registers.py` and the gates in `tools/gates/`.

| File | Holds | Gate |
|---|---|---|
| `findings.yml` | one row per finding, with owner, lifecycle state and evidence | `gate_register` |
| `claims.yml` | one row per factual statement in a tracked document | `gate_claims` (arrives in block b2) |
| `decisions.yml` | owner questions and merge grants (`grant:<plan>`) | `gate_decisions` (arrives in block b2) |
| `<plan>/ledger.md` | the plan's progress ledger, rulings, `CHECKPOINT b<N>` lines | by hand |
| `<plan>/handoff.md` | the checkpoint handoff | by hand |
| `<plan>/mutations/<id>.txt` | mutation transcripts that pin a finding | `gate_register` |

## Editing rules

- Each file is a YAML list; an empty register is `[]`, never an empty file.
- Quote every sha (`sha: "7265059"`): an all-digit sha is read as a number and fails.
- Quote `answer: "yes"` and `state: "false"`: YAML reads bare yes/no/on/off/true/false as booleans.
- No duplicate keys and no merge keys (`<<`): the loader refuses both.
- Findings: history starts at `open`, every step is a legal transition, the last history state
  equals `state`; `pinned` needs `pin.test` (`test_<name>` or `test_<file>::test_<name>`), an
  existing `pin.transcript` under `docs/superpowers/state/<plan>/mutations/` and the reviewer's
  report path in `evidence`; `deferred` needs `ruling` = a valid, unique decision that the owner
  answered; `closed` needs evidence; `merged` is written only by the post-merge script
  (`by: postmerge`).
- Every sha (history, cites, evidence entries shaped like a sha, `asked_at.sha`, `answered_at.sha`,
  `verified_at`) must name a commit of this repository: `gate_register` checks each with
  `git cat-file -e <sha>^{commit}` (decision DEC-0003). The registers name commits on `main`, so the
  gate needs full history: a shallow clone FAILs `register.evidence` by design, and CI checks out
  with `fetch-depth: 0`.
- Claims: `verified` needs `verified_at` and `by`; a verified row whose cited file changes goes
  stale (the `gate_claims` gate and its `--mark-stale` command arrive in block b2; the command
  rewrites `claims.yml` through PyYAML, so comments in it are not kept: keep none there).
  `false` needs `finding` = a registered finding id.
- Decisions: only the owner answers. The owner states the answer in chat; the session records it
  verbatim (a quoted string) with `answered_at.by: superbrobenji`, the owner handle in
  `tools/gates/config.yml`, and quotes the owner's words in the commit message. An agent never
  writes an answer of its own. `asked_at.sha` / `answered_at.sha` are the `main` commit at the time the
  row was created/answered (a question asked before the register existed records the register's
  creating commit).
- A git pre-commit hook that refuses a commit while this directory has unstaged edits arrives in block b3.
```

`docs/superpowers/state/strict-delivery/ledger.md`:

```markdown
# strict-delivery ledger

Umbrella: docs/superpowers/plans/2026-10-17-strict-delivery.md. S1: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md.
Spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md. Grant: decisions.yml grant:strict-delivery.
Lines: `- T<n> <commit subject> -- <gate result>`; `Ruling: what -- why -- cost if wrong -- spec cite`; `CHECKPOINT b<N> ...`.

## Block b1 (branch block/strict-delivery-b1)
```

`.github/workflows/ci.yml`: insert these two lines after line 52 (the `"components/drivers/imu_mpu6050/imu_mpu6050.c|The real I2C driver (WHO_AM_I"` entry), before the closing `)` on line 53:

```yaml
            # true: the strict-delivery comment gate's own pattern list (tools/gates/config.yml)
            "tools/gates/config.yml|- 'later sessions?'"
```

Verify the step's logic against the new tree (the allow entry must match exactly one hit, and no other hit may appear):

```bash
git add -A
git grep -nIE 'lands in [0-9]|arrives in [0-9]|lands with the|later sessions?|stubbed until|stub until|not in plan [0-9]|Plan 04 ships no display' -- tools/gates
# expect exactly: tools/gates/config.yml:27:  - 'later sessions?'
git diff --cached --check && echo whitespace-clean
```

- [ ] **Step 7: Ledger line, then commit**

Append to `docs/superpowers/state/strict-delivery/ledger.md`:

```markdown
- base BASE_B1=<the sha recorded in setup Step 0.3>
- T1 feat(gates): gate framework, config, git helpers, register bootstrap with DEC-0001 -- test_lib 39 OK
```

```bash
git add tools/gates tools/requirements.txt .github/workflows/ci.yml docs/superpowers/state
git commit -m "feat(gates): gate framework, config, git helpers, register bootstrap with DEC-0001" \
  -m "Strict-delivery S1 Task 1 (spec §4, §7; umbrella Gate contract, Config, Git/gh helpers). Registers start with zero findings, zero claims and DEC-0001 grant:strict-delivery, recorded as the owner answered it in chat at setup Step 0.1: <quote the saved reply verbatim>. ci.yml allow-lists the comment gate's own pattern line in config.yml."
```

### Task 2: Register loaders, schema validators, lifecycle rules

**Files:**
- Create: `tools/gates/lib/registers.py`
- Test: `tools/gates/tests/test_registers.py`

**Interfaces:**
- Consumes: `GateError`, `load_yaml`, `CODE_ROOT` (Task 1); `gitfixture.write`, `.remove`, `.STATE`.
- Produces (umbrella "Registers", verbatim): `FINDING_STATES`, `FINDING_TRANSITIONS`, `CLAIM_STATES`, `DECISION_CLASSES`, `OWNER_KINDS`; `load_findings(root) -> list[dict]`, `load_claims(root)`, `load_decisions(root)` (GateError on missing/empty/invalid/non-list); `validate_finding(f) -> list[str]`, `validate_claim(c)`, `validate_decision(d)` (each error is `"<field>: <problem>"`); `finding_by_id(findings, id) -> dict | None`; `open_decisions(decisions, *, owner=None) -> list[dict]` (every decision the owner has not answered: `answer` is null, `answered_at` is not a mapping, or `answered_at.by` is not the owner; owner defaults to config `owner.handle`); `grant_for(decisions, plan, *, owner=None) -> dict | None` (owner defaults to config `owner.handle`; the row must pass `validate_decision` and `answered_at.by` must be the owner). Additive: `owner_errors(owner) -> list[str]` (every message starts `owner: `), `transition_errors(f) -> list[str]` and `evidence_errors(f, root, decisions) -> list[str]` (call both only on a row with no schema errors), `row_id(row, index, pattern) -> str` (the id a result line prints: the row's own when well-formed, else `entry-<index>`), `memo_commit_exists() -> exists(root, sha)` (one `gitinfo.commit_exists` call per distinct root and sha), `PathError(GateError)` (message `<path relative to root>: <reason>`, with `.reason` and `.not_regular`), `rel_error(root, path, exc) -> PathError`, `require_regular(root, path)` (PathError unless a regular file; a FIFO would hang an open), `owner_handle(owner=None) -> str` (the owner, or config `owner.handle`), `safe_text(rel) -> str` (escapes control characters for a result-line id), `OWNER_ANSWER_ERROR` (the prefix of `validate_decision`'s non-owner-answer error, which is built from it), `STATE_DIR`, `SEVERITIES`, `FINDING_ID`, `CLAIM_ID`, `DECISION_ID`, `SHA`, `POSTMERGE = "postmerge"`.

- [ ] **Step 1: Write the failing tests**

`tools/gates/tests/test_registers.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/lib/registers.py: loaders, schema validators, lifecycle rules, and the
bootstrapped registers under docs/superpowers/state/. Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import copy
import os
import tempfile
import unittest

from tools.gates import lib
from tools.gates.lib import GateError, registers as reg
from tools.gates.tests import gitfixture as gf

FINDING = {
    "id": "T1-PIN-1", "source": "review", "severity": "HIGH",
    "class": "A CRC-bad frame is dropped and every later delta is applied to the old anchor.",
    "siblings": ["CRC-bad KEY"], "cites": [{"file": "a.c", "lines": "1-2", "sha": "aaaaaaa"}],
    "repro": None, "owner": {"kind": "plan", "plan": "demo", "task": 1}, "state": "open",
    "pin": None, "ruling": None, "evidence": [],
    "history": [{"state": "open", "sha": "aaaaaaa", "by": "audit"}],
}
CLAIM = {
    "id": "C-spec-1.1-demo", "doc": "docs/spec.md", "anchor": "§1.1", "text": "x is y",
    "direct": "grep -n y docs/spec.md", "contra": "if x were not y, test_x fails",
    "cites": ["a.c"], "verified_at": None, "by": None, "state": "unverified", "finding": None,
}
DECISION = {
    "id": "DEC-9001", "class": "triage", "question": "accept or widen?",
    "options": [{"label": "accept", "cost": "8 tasks"}, {"label": "widen", "cost": "18 tasks"}],
    "asked_at": {"sha": "aaaaaaa", "by": "session"}, "answer": None, "answered_at": None,
}


MUT = "docs/superpowers/state/demo/mutations/"


def with_(row, **kw):
    out = copy.deepcopy(row)
    out.update(kw)
    return out


class Loaders(unittest.TestCase):
    def test_bootstrapped_registers_are_valid(self):
        root = lib.CODE_ROOT
        self.assertEqual([], reg.load_findings(root))
        self.assertEqual([], reg.load_claims(root))
        decisions = reg.load_decisions(root)
        self.assertEqual(["DEC-0001", "DEC-0002", "DEC-0003"], [d["id"] for d in decisions])
        self.assertEqual([[], [], []], [reg.validate_decision(d) for d in decisions])
        self.assertEqual("merge-grant", decisions[0]["class"])
        self.assertEqual("grant:strict-delivery", decisions[0]["question"])

    def test_missing_empty_and_non_list_registers_raise(self):
        root = tempfile.mkdtemp(prefix="gate-reg-")
        try:
            with self.assertRaisesRegex(GateError, "findings.yml: No such file"):
                reg.load_findings(root)
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), "")
            with self.assertRaisesRegex(GateError, "empty file"):
                reg.load_findings(root)
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), "id: A-1\n")
            with self.assertRaisesRegex(GateError, "must be a YAML list"):
                reg.load_findings(root)
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), "- A-1\n")
            with self.assertRaisesRegex(GateError, "entry 0 is not a mapping"):
                reg.load_findings(root)
        finally:
            gf.remove(root)


class FindingSchema(unittest.TestCase):
    def test_a_well_formed_finding_has_no_errors(self):
        self.assertEqual([], reg.validate_finding(FINDING))

    def test_missing_and_unknown_keys(self):
        f = with_(FINDING, extra=1)
        del f["siblings"]
        self.assertEqual(["keys: missing siblings", "keys: unknown extra"], reg.validate_finding(f))

    def test_unquoted_numeric_sha_is_named(self):
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": 7265059}])
        self.assertIn("cites: entry 0 sha: parsed as a number; quote the sha exactly as written in the file",
                      reg.validate_finding(f))

    def test_owner_shapes(self):
        ok = [{"kind": "plan", "plan": "p", "task": 7}, {"kind": "issue", "number": 140},
              {"kind": "roadmap", "plan": 8}]
        for o in ok:
            self.assertEqual([], reg.owner_errors(o), o)
        bad = [None, {"kind": "person"}, {"kind": "issue", "number": "140"},
               {"kind": "issue", "number": True}, {"kind": "plan", "plan": "p"},
               {"kind": "roadmap", "plan": ""}]
        for o in bad:
            errs = reg.owner_errors(o)
            self.assertTrue(errs and all(e.startswith("owner: ") for e in errs), (o, errs))

    def test_history_must_end_at_the_current_state(self):
        f = with_(FINDING, state="planned")
        self.assertIn("history: last entry state 'open' != state 'planned'", reg.validate_finding(f))

    def test_bad_severity_id_and_ruling(self):
        f = with_(FINDING, id="lower-case", severity="SEVERE", ruling="D-1")
        errs = reg.validate_finding(f)
        self.assertTrue(any(e.startswith("id: ") for e in errs), errs)
        self.assertTrue(any(e.startswith("severity: ") for e in errs), errs)
        self.assertTrue(any(e.startswith("ruling: ") for e in errs), errs)


class Transitions(unittest.TestCase):
    def hist(self, *states):
        return with_(FINDING, state=states[-1],
                     history=[{"state": s, "sha": "aaaaaaa", "by": "x"} for s in states])

    def test_every_legal_edge_passes(self):
        for a, tos in reg.FINDING_TRANSITIONS.items():
            for b in tos:
                path = ["open"] if a == "open" else self.path_to(a)
                self.assertEqual([], reg.transition_errors(self.hist(*path, b)), (path, b))

    def path_to(self, state):
        paths = {"planned": ["open", "planned"], "in-progress": ["open", "planned", "in-progress"],
                 "fixed-unpinned": ["open", "planned", "in-progress", "fixed-unpinned"],
                 "pinned": ["open", "planned", "in-progress", "pinned"],
                 "deferred": ["open", "deferred"], "closed": ["open", "closed"],
                 "merged": ["open", "closed", "merged"]}
        return paths[state]

    def test_every_illegal_edge_fails(self):
        for a in reg.FINDING_STATES:
            for b in reg.FINDING_STATES:
                if b in reg.FINDING_TRANSITIONS[a]:
                    continue
                path = ["open"] if a == "open" else self.path_to(a)
                errs = reg.transition_errors(self.hist(*path, b))
                self.assertIn(f"transition: {a} -> {b} is illegal", errs[-1], (a, b))

    def test_history_must_start_open(self):
        self.assertEqual(["transition: history starts at 'planned', not 'open'"],
                         reg.transition_errors(self.hist("planned")))


class Evidence(unittest.TestCase):
    def setUp(self):
        self.root = tempfile.mkdtemp(prefix="gate-ev-")
        gf.write(self.root, MUT + "T1-PIN-1.txt", "finding: T1-PIN-1\n")
        gf.write(self.root, "st/demo/review/T1.md", "status: DONE\n")

    def tearDown(self):
        gf.remove(self.root)

    def pinned(self, **kw):
        f = with_(FINDING, state="pinned",
                  pin={"test": "test_x", "transcript": MUT + "T1-PIN-1.txt"},
                  evidence=["st/demo/review/T1.md"],
                  history=[{"state": s, "sha": "aaaaaaa", "by": "x"}
                           for s in ("open", "planned", "in-progress", "pinned")])
        f.update(kw)
        return f

    def test_pinned_with_transcript_and_review_passes(self):
        self.assertEqual([], reg.evidence_errors(self.pinned(), self.root, []))

    def test_pinned_needs_pin_transcript_file_and_review(self):
        self.assertIn("evidence: pinned needs pin {test, transcript}",
                      reg.evidence_errors(self.pinned(pin=None), self.root, []))
        gone = {"test": "test_x", "transcript": MUT + "absent.txt"}
        self.assertIn(f"evidence: pin transcript {MUT}absent.txt does not exist",
                      reg.evidence_errors(self.pinned(pin=gone), self.root, []))
        self.assertIn("evidence: pinned needs the reviewer's confirming report path in evidence",
                      reg.evidence_errors(self.pinned(evidence=["bbbbbbb"]), self.root, []))

    def test_evidence_entries_must_exist(self):
        self.assertIn("evidence: 'st/nope.md' is neither a commit sha nor a file under the repo root",
                      reg.evidence_errors(self.pinned(evidence=["st/nope.md"]), self.root, []))

    def test_evidence_paths_cannot_escape_the_repo_root(self):
        outside = os.path.join(os.path.dirname(self.root), os.path.basename(self.root) + "-out.md")
        with open(outside, "w", encoding="utf-8") as fh:
            fh.write("x\n")
        self.addCleanup(os.remove, outside)
        for p in ("../" + os.path.basename(outside),):
            self.assertIn(f"evidence: {p!r} is neither a commit sha nor a file under the repo root",
                          reg.evidence_errors(self.pinned(evidence=[p]), self.root, []), p)
        escaped = {"test": "test_x", "transcript": MUT + "../../../../../" + os.path.basename(outside)}
        self.assertIn(f"evidence: pin transcript must be docs/superpowers/state/<plan>/mutations/<file>, got {escaped['transcript']!r}",
                      reg.evidence_errors(self.pinned(pin=escaped), self.root, []))

    def test_absolute_and_nul_evidence_paths_are_errors_not_crashes(self):
        abs_p = os.path.join(self.root, "st/demo/review/T1.md")
        self.assertIn(f"evidence: path must be relative to the repo root: {abs_p!r}",
                      reg.evidence_errors(self.pinned(evidence=[abs_p]), self.root, []))
        abs_t = {"test": "test_x", "transcript": os.path.join(self.root, MUT + "T1-PIN-1.txt")}
        errs = reg.evidence_errors(self.pinned(pin=abs_t), self.root, [])
        self.assertIn("evidence: pin transcript: must be relative to the repo root", errs)
        self.assertFalse(any("does not exist" in e for e in errs), errs)
        nul = "st/a\0b.md"
        self.assertIn(f"evidence: {nul!r} is neither a commit sha nor a file under the repo root",
                      reg.evidence_errors(self.pinned(evidence=[nul]), self.root, []))
        bad = {"test": "test_x", "transcript": MUT + "a\0b.txt"}
        self.assertIn(f"evidence: pin transcript {MUT}a\0b.txt does not exist",
                      reg.evidence_errors(self.pinned(pin=bad), self.root, []))

    def test_deferred_needs_an_answered_owner_decision(self):
        f = with_(FINDING, state="deferred", ruling="DEC-9001",
                  history=[{"state": "open", "sha": "aaaaaaa", "by": "a"},
                           {"state": "deferred", "sha": "aaaaaaa", "by": "a"}])
        self.assertIn("evidence: ruling DEC-9001 is not in decisions.yml",
                      reg.evidence_errors(f, self.root, []))
        self.assertIn("evidence: ruling DEC-9001 is not answered by the owner",
                      reg.evidence_errors(f, self.root, [DECISION]))
        answered = with_(DECISION, answer="defer", answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        self.assertEqual([], reg.evidence_errors(f, self.root, [answered]))
        self.assertIn("evidence: deferred needs ruling = the owner's decision id",
                      reg.evidence_errors(with_(f, ruling=None), self.root, [answered]))

    def test_closed_needs_evidence_and_merged_needs_postmerge(self):
        closed = with_(FINDING, state="closed", history=[{"state": "open", "sha": "aaaaaaa", "by": "a"},
                                                          {"state": "closed", "sha": "aaaaaaa", "by": "a"}])
        self.assertEqual(["evidence: closed needs evidence (commits or report paths)"],
                         reg.evidence_errors(closed, self.root, []))
        merged = with_(closed, state="merged", evidence=["bbbbbbb"],
                       history=closed["history"] + [{"state": "merged", "sha": "aaaaaaa", "by": "session"}])
        self.assertEqual(["evidence: merged is set only by the post-merge script (by: postmerge), got 'session'"],
                         reg.evidence_errors(merged, self.root, []))


class ClaimSchema(unittest.TestCase):
    def test_a_well_formed_claim_has_no_errors(self):
        self.assertEqual([], reg.validate_claim(CLAIM))

    def test_verified_needs_sha_and_by(self):
        self.assertIn("state: verified needs verified_at and by",
                      reg.validate_claim(with_(CLAIM, state="verified")))

    def test_false_needs_a_finding_and_only_false_has_one(self):
        self.assertIn("finding: a false claim must name the finding id that records it",
                      reg.validate_claim(with_(CLAIM, state="false")))
        self.assertIn("finding: only a false claim carries a finding (set it to null)",
                      reg.validate_claim(with_(CLAIM, finding="A2-IO-N4")))
        self.assertEqual([], reg.validate_claim(with_(CLAIM, state="false", finding="A2-IO-N4")))

    def test_boolean_false_state_is_named(self):
        self.assertEqual(['state: YAML read false as a boolean; quote it: state: "false"',
                          "finding: only a false claim carries a finding (set it to null)"],
                         reg.validate_claim(with_(CLAIM, state=False, finding="A2-IO-N4")))

    def test_a_claim_must_cite_something(self):
        self.assertTrue(any(e.startswith("cites: ") for e in reg.validate_claim(with_(CLAIM, cites=[]))))


class DecisionSchema(unittest.TestCase):
    def test_a_well_formed_decision_has_no_errors(self):
        self.assertEqual([], reg.validate_decision(DECISION))

    def test_unquoted_yes_is_named(self):
        d = with_(DECISION, answer=True, answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        errs = reg.validate_decision(d)
        self.assertTrue(any("quote it" in e for e in errs), errs)

    def test_answer_and_answered_at_go_together(self):
        self.assertIn("answered_at: answer and answered_at are both null or both set",
                      reg.validate_decision(with_(DECISION, answer="accept")))

    def test_owner_questions_need_two_options_but_grants_do_not(self):
        self.assertIn("options: an owner question needs at least two options with their cost",
                      reg.validate_decision(with_(DECISION, options=[DECISION["options"][0]])))
        grant = with_(DECISION, **{"class": "merge-grant", "question": "grant:demo", "options": []})
        self.assertEqual([], reg.validate_decision(grant))
        self.assertIn("question: a merge-grant question is 'grant:<plan>'",
                      reg.validate_decision(with_(grant, question="may we merge?")))


class ReviewRound1(unittest.TestCase):
    def test_trailing_newline_is_rejected_in_every_id_and_sha(self):
        self.assertTrue(any(e.startswith("id: ") for e in reg.validate_finding(with_(FINDING, id="T1-PIN-1\n"))))
        self.assertTrue(any(e.startswith("ruling: ") for e in
                            reg.validate_finding(with_(FINDING, ruling="DEC-0001\n"))))
        self.assertTrue(any(e.startswith("id: ") for e in reg.validate_claim(with_(CLAIM, id="C-a-1\n"))))
        self.assertTrue(any(e.startswith("finding: ") for e in
                            reg.validate_claim(with_(CLAIM, state="false", finding="A2-IO-N4\n"))))
        self.assertTrue(any(e.startswith("id: ") for e in reg.validate_decision(with_(DECISION, id="DEC-0001\n"))))
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": "aaaaaaa\n"}])
        self.assertTrue(any(e.startswith("cites: entry 0 sha") for e in reg.validate_finding(f)))

    def test_mixed_type_unknown_keys_do_not_crash(self):
        f = with_(FINDING)
        f[1] = 1
        f["z"] = 2
        self.assertEqual(["keys: unknown z, 1"], reg.validate_finding(f))

    def test_octal_looking_sha_is_not_echoed_as_a_reconstructed_number(self):
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": 0o123456}])
        errs = reg.validate_finding(f)
        self.assertIn("cites: entry 0 sha: parsed as a number; quote the sha exactly as written in the file", errs)
        self.assertFalse(any("42798" in e for e in errs), errs)

    def test_grant_question_needs_a_plan_name(self):
        for q in ("grant:", "grant: x"):
            self.assertIn("question: a merge-grant question is 'grant:<plan>'",
                          reg.validate_decision(with_(DECISION, **{"class": "merge-grant", "question": q,
                                                                   "options": []})), q)
        self.assertEqual([], reg.validate_decision(with_(DECISION, **{
            "class": "merge-grant", "question": "grant:demo some text", "options": []})))

    def grant(self, by="superbrobenji", **kw):
        g = with_(DECISION, **{"class": "merge-grant", "question": "grant:demo", "options": [],
                               "answer": "YES", "answered_at": {"sha": "aaaaaaa", "by": by}})
        g.update(kw)
        return g

    def test_grant_for_checks_answerer_and_validity(self):
        g = self.grant()
        self.assertIs(g, reg.grant_for([g], "demo"))                       # two-arg form, owner from config
        self.assertIsNone(reg.grant_for([self.grant(by="someone-else")], "demo"))
        other = self.grant(by="someone-else")
        self.assertIs(other, reg.grant_for([other], "demo", owner="someone-else"))
        self.assertIsNone(reg.grant_for([with_(g, extra=1)], "demo"))      # malformed row
        self.assertIsNone(reg.grant_for([with_(g, id="bad")], "demo"))


class Queries(unittest.TestCase):
    def test_finding_by_id_and_open_decisions(self):
        self.assertIs(FINDING, reg.finding_by_id([FINDING], "T1-PIN-1"))
        self.assertIsNone(reg.finding_by_id([FINDING], "T1-PIN-2"))
        answered = with_(DECISION, id="DEC-9002", answer="accept",
                         answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        self.assertEqual(["DEC-9001"], [d["id"] for d in reg.open_decisions([DECISION, answered])])

    def test_grant_needs_an_exact_plan_and_a_yes(self):
        g = with_(DECISION, **{"class": "merge-grant", "question": "grant:demo", "options": [],
                               "answer": "yes", "answered_at": {"sha": "aaaaaaa", "by": "superbrobenji"}})
        self.assertIs(g, reg.grant_for([g], "demo"))
        self.assertIsNone(reg.grant_for([g], "dem"))
        self.assertIsNone(reg.grant_for([with_(g, question="grant:demo-2")], "demo"))
        self.assertIsNone(reg.grant_for([with_(g, answer="no")], "demo"))
        self.assertIsNone(reg.grant_for([with_(g, answer=None, answered_at=None)], "demo"))
        self.assertIs(g, reg.grant_for([DECISION, g], "demo"))


def deferred(ruling="DEC-9001"):
    return with_(FINDING, state="deferred", ruling=ruling,
                 history=[{"state": "open", "sha": "aaaaaaa", "by": "a"},
                          {"state": "deferred", "sha": "aaaaaaa", "by": "a"}])


class FixWave(unittest.TestCase):
    """Block b1 final-review fix wave."""

    def answered(self, **kw):
        return with_(DECISION, answer="defer", answered_at={"sha": "aaaaaaa", "by": "superbrobenji"}, **kw)

    def test_owner_is_evaluated_even_when_other_keys_are_missing(self):               # A1
        f = with_(FINDING, owner=None)
        del f["siblings"]
        self.assertEqual(["keys: missing siblings", "owner: missing (every finding needs an owner: a plan task, an issue or a roadmap plan)"],
                         reg.validate_finding(f))
        g = with_(FINDING, owner={"kind": "person"})
        del g["repro"]
        errs = reg.validate_finding(g)
        self.assertEqual(["keys: missing repro"], errs[:1])
        self.assertTrue(errs[1].startswith("owner: kind must be one of"), errs)
        h = with_(FINDING)
        del h["owner"]
        self.assertEqual(["keys: missing owner"], reg.validate_finding(h))

    def test_only_the_owner_answers_a_decision(self):                                  # A2
        agent = with_(DECISION, answer="yes", answered_at={"sha": "aaaaaaa", "by": "session"})
        self.assertEqual(["answered_at.by: only the owner (superbrobenji) answers a decision, got 'session'"],
                         reg.validate_decision(agent))
        self.assertEqual([], reg.validate_decision(agent, owner="session"))
        self.assertEqual([], reg.validate_decision(self.answered()))

    def test_deferred_needs_a_valid_owner_answered_unique_decision(self):             # A2
        root = tempfile.mkdtemp(prefix="gate-def-")
        self.addCleanup(gf.remove, root)
        f = deferred()
        self.assertEqual([], reg.evidence_errors(f, root, [self.answered()]))
        by_agent = with_(DECISION, answer="defer", answered_at={"sha": "aaaaaaa", "by": "session"})
        errs = reg.evidence_errors(f, root, [by_agent])
        self.assertEqual(["evidence: ruling DEC-9001 is not a valid owner-answered decision "
                          "(answered_at.by: only the owner (superbrobenji) answers a decision, got 'session')"], errs)
        boolean = with_(DECISION, answer=False, answered_at={"sha": "aaaaaaa", "by": "superbrobenji"})
        errs = reg.evidence_errors(f, root, [boolean])
        self.assertEqual(1, len(errs), errs)
        self.assertTrue(errs[0].startswith("evidence: ruling DEC-9001 is not a valid owner-answered decision (answer: "), errs)
        self.assertIn("quote it", errs[0])
        self.assertEqual(["evidence: ruling DEC-9001 appears more than once in decisions.yml"],
                         reg.evidence_errors(f, root, [self.answered(), self.answered()]))
        self.assertEqual(["evidence: ruling DEC-9001 is not answered by the owner"],
                         reg.evidence_errors(f, root, [DECISION]))

    def test_unicode_digits_are_not_ids(self):                                         # B8
        for bad in ("DEC-\uff10\uff10\uff10\uff11", "DEC-\u0663\u0663\u0663\u0663"):
            self.assertTrue(any(e.startswith("id: ") for e in reg.validate_decision(with_(DECISION, id=bad))), bad)
            self.assertTrue(any(e.startswith("ruling: ") for e in reg.validate_finding(with_(FINDING, ruling=bad))), bad)

    def test_sha_length_bounds(self):                                                   # B14
        for sha in ("aaaaaa", "a", "a" * 41):
            f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": sha}])
            self.assertTrue(any(e.startswith("cites: entry 0 sha") for e in reg.validate_finding(f)), sha)
        for sha in ("a" * 7, "a" * 40):
            f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": sha}])
            self.assertEqual([], reg.validate_finding(f), sha)

    def test_unquoted_dates_and_booleans_in_string_fields_get_the_quote_hint(self):  # B15
        import datetime
        for v in (datetime.date(2026, 10, 17), True, 7, 1.5):
            errs = reg.validate_finding(with_(FINDING, source=v))
            self.assertEqual(["source: must be a non-empty string (YAML read it as a date, boolean or number: quote it)"],
                             errs, v)
        self.assertEqual(["source: must be a non-empty string"], reg.validate_finding(with_(FINDING, source="")))
        self.assertEqual(["doc: must be a non-empty string (YAML read it as a date, boolean or number: quote it)"],
                         reg.validate_claim(with_(CLAIM, doc=datetime.date(2026, 10, 17))))

    def test_pinned_transcript_location_and_test_name(self):                           # B13
        root = tempfile.mkdtemp(prefix="gate-pin-")
        self.addCleanup(gf.remove, root)
        gf.write(root, "README.md", "x\n")
        gf.write(root, MUT + "T1-PIN-1.txt", "t\n")
        gf.write(root, "docs/superpowers/state/demo/review/T1.md", "r\n")
        base = with_(FINDING, state="pinned", evidence=["docs/superpowers/state/demo/review/T1.md"],
                     history=[{"state": s, "sha": "aaaaaaa", "by": "x"}
                              for s in ("open", "planned", "in-progress", "pinned")])
        ok = with_(base, pin={"test": "test_x", "transcript": MUT + "T1-PIN-1.txt"})
        self.assertEqual([], reg.evidence_errors(ok, root, []))
        self.assertEqual([], reg.evidence_errors(with_(ok, pin={"test": "test_mod::test_x", "transcript": MUT + "T1-PIN-1.txt"}), root, []))
        for where in ("README.md", "docs/superpowers/state/demo/review/T1.md", "docs/superpowers/state/mutations/T1-PIN-1.txt",
                      "docs/superpowers/state/../demo/mutations/T1-PIN-1.txt", "docs/superpowers/state/demo/mutations/a/T1-PIN-1.txt"):
            errs = reg.evidence_errors(with_(base, pin={"test": "test_x", "transcript": where}), root, [])
            self.assertEqual([f"evidence: pin transcript must be docs/superpowers/state/<plan>/mutations/<file>, got {where!r}"],
                             errs, where)
        for name in ("not a test", "x", "test_", "test_a::x", "test_a::", "test_a b", "test_a::test_b::test_c", "test_x\n"):
            errs = reg.evidence_errors(with_(ok, pin={"test": name, "transcript": MUT + "T1-PIN-1.txt"}), root, [])
            self.assertEqual([f"evidence: pin test must be test_<name> or test_<name>::test_<name>, got {name!r}"], errs, name)

    def test_load_errors_name_the_register_relative_to_the_root(self):                # A7
        root = tempfile.mkdtemp(prefix="gate-reg-")
        self.addCleanup(gf.remove, root)
        rel = "docs/superpowers/state/findings.yml"
        with self.assertRaises(GateError) as ctx:
            reg.load_findings(root)
        self.assertEqual(f"{rel}: No such file or directory", str(ctx.exception))
        for text, msg in (("", "empty file (an empty register is the YAML list [])"),
                          ("id: A-1\n", "top level must be a YAML list"), ("- A-1\n", "entry 0 is not a mapping")):
            gf.write(root, rel, text)
            with self.assertRaises(GateError) as ctx:
                reg.load_findings(root)
            self.assertEqual(f"{rel}: {msg}", str(ctx.exception))

    def commits(self):
        root = gf.new_repo()
        self.addCleanup(gf.remove, root)
        return root, gf.git(root, "rev-parse", "--short=7", "HEAD")

    def test_every_sha_field_must_name_a_commit(self):                                 # A8
        root, real = self.commits()
        fake = "0000000"
        msg = "{}: sha 0000000 is not a commit in this repository"
        f = with_(FINDING, cites=[{"file": "a.c", "lines": "1", "sha": real}, {"file": "b.c", "lines": "1", "sha": fake}],
                  evidence=[real, fake, "README.md"],
                  history=[{"state": "open", "sha": real, "by": "a"}])
        self.assertEqual([msg.format("cites[1].sha"), msg.format("evidence[1]")], reg.finding_commit_errors(f, root))
        g = with_(f, cites=[], evidence=[], history=[{"state": "open", "sha": fake, "by": "a"}])
        self.assertEqual([msg.format("history[0].sha")], reg.finding_commit_errors(g, root))
        d = with_(DECISION, asked_at={"sha": fake, "by": "session"}, answer="x",
                  answered_at={"sha": fake, "by": "superbrobenji"})
        self.assertEqual([msg.format("asked_at.sha"), msg.format("answered_at.sha")], reg.decision_commit_errors(d, root))
        self.assertEqual([], reg.decision_commit_errors(with_(DECISION, asked_at={"sha": real, "by": "s"}), root))
        c = with_(CLAIM, verified_at=fake, by="s", state="verified")
        self.assertEqual([msg.format("verified_at")], reg.claim_commit_errors(c, root))
        self.assertEqual([], reg.claim_commit_errors(with_(c, verified_at=real), root))
        self.assertEqual([], reg.claim_commit_errors(CLAIM, root))

    def test_the_commit_lookup_is_injectable_and_a_git_failure_propagates(self):      # A8
        seen = []
        f = with_(FINDING, cites=[], history=[{"state": "open", "sha": "abcdef1", "by": "a"}], evidence=["abcdef1"])
        self.assertEqual([], reg.finding_commit_errors(f, "r", lambda root, sha: seen.append((root, sha)) or True))
        self.assertEqual([("r", "abcdef1"), ("r", "abcdef1")], seen)
        outside = tempfile.mkdtemp(prefix="gate-nogit-")
        self.addCleanup(gf.remove, outside)
        with self.assertRaises(GateError):
            reg.finding_commit_errors(f, outside)

    def test_row_id_prints_only_a_well_formed_id(self):
        self.assertEqual("DEC-0001", reg.row_id({"id": "DEC-0001"}, 3, reg.DECISION_ID))
        for bad in ("DEC-1", "DEC-0001\nPASS x", 5, None, ["DEC-0001"]):
            self.assertEqual("entry-3", reg.row_id({"id": bad}, 3, reg.DECISION_ID))
        self.assertEqual("entry-0", reg.row_id({}, 0, reg.DECISION_ID))

    def test_memo_commit_exists_asks_git_once_per_distinct_sha(self):
        calls = []
        real = reg.gitinfo.commit_exists
        reg.gitinfo.commit_exists = lambda root, sha: calls.append((root, sha)) or sha == "abcdef1"
        try:
            exists = reg.memo_commit_exists()
            self.assertEqual([True, True, False, False, True],
                             [exists("r", "abcdef1"), exists("r", "abcdef1"), exists("r", "1234abc"),
                              exists("r", "1234abc"), exists("r2", "abcdef1")])
            self.assertEqual([("r", "abcdef1"), ("r", "1234abc"), ("r2", "abcdef1")], calls)
        finally:
            reg.gitinfo.commit_exists = real

    def test_owner_answer_error_prefix_is_what_validate_decision_says(self):
        d = copy.deepcopy(DECISION)
        d["answered_at"] = {"sha": "abcdef1", "by": "session"}
        d["answer"] = "yes"
        errs = reg.validate_decision(d, owner="boss")
        self.assertEqual(1, len(errs), errs)
        self.assertTrue(errs[0].startswith(reg.OWNER_ANSWER_ERROR + " (boss)"), errs)

    def test_open_decisions_means_not_answered_by_the_owner(self):
        def row(i, **kw):
            d = copy.deepcopy(DECISION)
            d.update(id=f"DEC-900{i}", **kw)
            return d
        done = {"sha": "abcdef1", "by": "boss"}
        rows = [row(1),
                row(2, answer="a", answered_at={"sha": "abcdef1", "by": "session"}),
                row(3, answer="a", answered_at=done),
                row(4, answer="a", answered_at="boss"),
                row(5, answer="a", answered_at=None)]
        ids = lambda **kw: [d["id"] for d in reg.open_decisions(rows, **kw)]
        self.assertEqual(["DEC-9001", "DEC-9002", "DEC-9004", "DEC-9005"], ids(owner="boss"))
        self.assertEqual(["DEC-9001", "DEC-9002", "DEC-9003", "DEC-9004", "DEC-9005"], ids(owner="someone"))
        self.assertEqual(["DEC-9001", "DEC-9002", "DEC-9003", "DEC-9004", "DEC-9005"], ids())   # config owner is not "boss"

    def test_open_decisions_defaults_to_the_config_owner(self):
        d = copy.deepcopy(DECISION)
        owner = reg.owner_handle()
        d.update(answer="a", answered_at={"sha": "abcdef1", "by": owner})
        self.assertEqual([], reg.open_decisions([d]))

    def test_path_error_carries_the_bare_reason(self):
        root = tempfile.mkdtemp(prefix="gate-reg-")
        self.addCleanup(gf.remove, root)
        gf.write(root, "d/f.md", "x\n")
        with self.assertRaises(reg.PathError) as cm:
            reg.require_regular(root, os.path.join(root, "d"))
        self.assertEqual(("not a regular file", True), (cm.exception.reason, cm.exception.not_regular))
        with self.assertRaises(reg.PathError) as cm:
            reg.require_regular(root, os.path.join(root, "d", "none.md"))
        self.assertEqual(("No such file or directory", False), (cm.exception.reason, cm.exception.not_regular))

    def test_safe_text_escapes_control_characters(self):
        self.assertEqual("docs/a.md", reg.safe_text("docs/a.md"))
        self.assertEqual("a\\nPASS x", reg.safe_text("a\nPASS x"))

    def test_require_regular_accepts_files_and_names_everything_else(self):
        root = tempfile.mkdtemp(prefix="gate-reg-")
        self.addCleanup(gf.remove, root)
        gf.write(root, "d/f.md", "x\n")
        reg.require_regular(root, os.path.join(root, "d", "f.md"))
        for rel, msg in (("d", "d: not a regular file"), ("d/none.md", "d/none.md: No such file or directory")):
            with self.assertRaises(GateError) as cm:
                reg.require_regular(root, os.path.join(root, rel))
            self.assertEqual(msg, str(cm.exception))
        if hasattr(os, "mkfifo"):
            os.mkfifo(os.path.join(root, "pipe"))
            with self.assertRaises(GateError) as cm:
                reg.require_regular(root, os.path.join(root, "pipe"))
            self.assertEqual("pipe: not a regular file", str(cm.exception))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_registers -v`
Expected: ERROR, `ImportError: cannot import name 'registers' from 'tools.gates.lib'`.

- [ ] **Step 3: Write the registers module**

`tools/gates/lib/registers.py`:

```python
"""Register loaders, schema validators and lifecycle rules (umbrella plan, "Registers";
spec §4). Every validator returns a list of "<field>: <problem>" strings, [] when the row is ok."""
from __future__ import annotations

import datetime
import os
import re
import stat

from . import CODE_ROOT, GateError, config, gitinfo, load_yaml

FINDING_STATES = ["open", "planned", "in-progress", "fixed-unpinned", "pinned", "deferred", "closed", "merged"]
FINDING_TRANSITIONS = {  # from -> allowed to
    "open": ["planned", "deferred", "closed"], "planned": ["in-progress", "open", "deferred"],
    "in-progress": ["fixed-unpinned", "pinned", "planned"], "fixed-unpinned": ["pinned"],
    "pinned": ["closed", "merged"], "deferred": ["open", "planned"], "closed": ["merged"], "merged": []}
CLAIM_STATES = ["unverified", "verified", "stale", "false"]
DECISION_CLASSES = ["triage", "spec-direction", "scope", "merge-grant", "hardware", "model-policy",
                    "issue-close", "finding-class"]
OWNER_KINDS = ["plan", "issue", "roadmap"]

SEVERITIES = ["CRITICAL", "HIGH", "MEDIUM", "LOW", "INFO"]
FINDING_ID = re.compile(r"^[A-Z0-9]+(-[A-Za-z0-9]+)+$")
CLAIM_ID = re.compile(r"^C-[A-Za-z0-9.§-]+$")
DECISION_ID = re.compile(r"^DEC-[0-9]{4}$")
GRANT_QUESTION = re.compile(r"grant:\S+( .*)?", re.S)
SHA = re.compile(r"^[0-9a-f]{7,40}$")
STATE_DIR = os.path.join("docs", "superpowers", "state")
PIN_TEST = re.compile(r"^test_[A-Za-z0-9_]+(::test_[A-Za-z0-9_]+)?$")
PIN_TRANSCRIPT = re.compile(r"^docs/superpowers/state/(?!\.\.?/)[^/]+/mutations/(?!\.\.?$)[^/]+$")
OWNER_ANSWER_ERROR = "answered_at.by: only the owner"    # the prefix of validate_decision's non-owner-answer error
POSTMERGE = "postmerge"   # the only `by` allowed on a history entry that sets `merged`

FINDING_KEYS = ["id", "source", "severity", "class", "siblings", "cites", "repro", "owner", "state",
                "pin", "ruling", "evidence", "history"]
CLAIM_KEYS = ["id", "doc", "anchor", "text", "direct", "contra", "cites", "verified_at", "by",
              "state", "finding"]
DECISION_KEYS = ["id", "class", "question", "options", "asked_at", "answer", "answered_at"]
OWNER_SHAPES = {"plan": {"kind", "plan", "task"}, "issue": {"kind", "number"}, "roadmap": {"kind", "plan"}}


def _load(root, name) -> list:
    path = os.path.join(STATE_DIR, name)    # messages name the file relative to `root`
    data = load_yaml(os.path.join(root, path), root)
    if data is None:
        raise GateError(f"{path}: empty file (an empty register is the YAML list [])")
    if not isinstance(data, list):
        raise GateError(f"{path}: top level must be a YAML list")
    for i, row in enumerate(data):
        if not isinstance(row, dict):
            raise GateError(f"{path}: entry {i} is not a mapping")
    return data


def load_findings(root) -> list:
    return _load(root, "findings.yml")


def load_claims(root) -> list:
    return _load(root, "claims.yml")


def load_decisions(root) -> list:
    return _load(root, "decisions.yml")


def _text(v) -> bool:
    return isinstance(v, str) and v.strip() != ""


def _hint(v) -> str:
    """The "quote it" hint for a string field YAML parsed as a date, boolean or number."""
    if isinstance(v, (datetime.date, bool, int, float)):
        return " (YAML read it as a date, boolean or number: quote it)"
    return ""


def _sha(v) -> bool:
    return isinstance(v, str) and SHA.fullmatch(v) is not None


def _sha_problem(field: str, v) -> list:
    if _sha(v):
        return []
    if isinstance(v, int) and not isinstance(v, bool):
        return [f"{field}: parsed as a number; quote the sha exactly as written in the file"]
    return [f"{field}: must be a 7-40 char lowercase hex sha string, got {v!r}"]


def _keys(row: dict, keys: list) -> list:
    errs = []
    missing = [k for k in keys if k not in row]
    if missing:
        errs.append(f"keys: missing {', '.join(missing)}")
    extra = sorted(set(row) - set(keys), key=repr)
    if extra:
        errs.append(f"keys: unknown {', '.join(map(str, extra))}")
    return errs


def _file_under(root, rel) -> bool:
    """True when `rel` names an existing file inside `root` (an absolute path or a `..` that
    leaves the root does not count)."""
    if os.path.isabs(rel):
        return False
    try:
        base = os.path.realpath(root)
        path = os.path.realpath(os.path.join(base, rel))
        return os.path.commonpath([base, path]) == base and os.path.isfile(path)
    except (ValueError, OSError):
        return False


def owner_errors(owner) -> list:
    """`{kind: plan, plan, task}` | `{kind: issue, number}` | `{kind: roadmap, plan}`."""
    if owner is None:
        return ["owner: missing (every finding needs an owner: a plan task, an issue or a roadmap plan)"]
    if not isinstance(owner, dict) or owner.get("kind") not in OWNER_KINDS:
        return [f"owner: kind must be one of {OWNER_KINDS}, got {owner!r}"]
    kind = owner["kind"]
    if set(owner) != OWNER_SHAPES[kind]:
        return [f"owner: kind {kind} needs exactly the keys {sorted(OWNER_SHAPES[kind])}"]
    errs = []
    if kind == "issue":
        n = owner["number"]
        if isinstance(n, bool) or not isinstance(n, int) or n <= 0:
            errs.append(f"owner: issue number must be a positive integer, got {n!r}")
    else:
        p = owner["plan"]
        if not (_text(p) or (isinstance(p, int) and not isinstance(p, bool) and p > 0)):
            errs.append(f"owner: plan must be a plan name or number, got {p!r}")
    if kind == "plan":
        t = owner["task"]
        if not (_text(t) or (isinstance(t, int) and not isinstance(t, bool) and t > 0)):
            errs.append(f"owner: task must be a task number or id, got {t!r}")
    return errs


def validate_finding(f: dict) -> list:
    errs = _keys(f, FINDING_KEYS)
    if any(e.startswith("keys: missing") for e in errs):
        return errs + (owner_errors(f["owner"]) if "owner" in f else [])   # the owner rule is never skipped
    if not isinstance(f["id"], str) or not FINDING_ID.fullmatch(f["id"]):
        errs.append(f"id: {f['id']!r} does not match {FINDING_ID.pattern}")
    if not _text(f["source"]):
        errs.append("source: must be a non-empty string" + _hint(f["source"]))
    if f["severity"] not in SEVERITIES:
        errs.append(f"severity: must be one of {SEVERITIES}, got {f['severity']!r}")
    if not _text(f["class"]):
        errs.append("class: the reviewer's class statement, verbatim, non-empty" + _hint(f["class"]))
    if not isinstance(f["siblings"], list) or not all(_text(s) for s in f["siblings"]):
        errs.append("siblings: must be a list of non-empty strings (may be empty)")
    if not isinstance(f["cites"], list):
        errs.append("cites: must be a list of {file, lines, sha}")
    else:
        for i, c in enumerate(f["cites"]):
            if not isinstance(c, dict) or set(c) != {"file", "lines", "sha"}:
                errs.append(f"cites: entry {i} must be exactly {{file, lines, sha}}")
                continue
            if not _text(c["file"]) or not _text(c["lines"]):
                errs.append(f"cites: entry {i} file and lines must be non-empty strings (quote lines)"
                            + _hint(c["file"]) + _hint(c["lines"]))
            errs += _sha_problem(f"cites: entry {i} sha", c["sha"])
    if f["repro"] is not None and not _text(f["repro"]):
        errs.append("repro: must be a path string or null" + _hint(f["repro"]))
    errs += owner_errors(f["owner"])
    if f["state"] not in FINDING_STATES:
        errs.append(f"state: must be one of {FINDING_STATES}, got {f['state']!r}")
    pin = f["pin"]
    if pin is not None and (not isinstance(pin, dict) or set(pin) != {"test", "transcript"}
                            or not _text(pin["test"]) or not _text(pin["transcript"])):
        errs.append("pin: must be null or {test, transcript}, both non-empty strings")
    if f["ruling"] is not None and not (isinstance(f["ruling"], str) and DECISION_ID.fullmatch(f["ruling"])):
        errs.append(f"ruling: must be null or a decision id DEC-NNNN, got {f['ruling']!r}")
    if not isinstance(f["evidence"], list) or not all(_text(e) for e in f["evidence"]):
        errs.append("evidence: must be a list of non-empty strings (commit shas or paths)")
    hist = f["history"]
    if not isinstance(hist, list) or not hist:
        errs.append("history: must be a non-empty list of {state, sha, by}")
    else:
        for i, h in enumerate(hist):
            if not isinstance(h, dict) or set(h) != {"state", "sha", "by"}:
                errs.append(f"history: entry {i} must be exactly {{state, sha, by}}")
                continue
            if h["state"] not in FINDING_STATES:
                errs.append(f"history: entry {i} state {h['state']!r} is not a finding state")
            errs += _sha_problem(f"history: entry {i} sha", h["sha"])
            if not _text(h["by"]):
                errs.append(f"history: entry {i} by must be a non-empty string" + _hint(h["by"]))
        last = hist[-1]
        if isinstance(last, dict) and last.get("state") != f["state"]:
            errs.append(f"history: last entry state {last.get('state')!r} != state {f['state']!r}")
    return errs


def transition_errors(f: dict) -> list:
    """History must start at `open` and every step must be legal. Call only on a valid row."""
    states = [h["state"] for h in f["history"]]
    errs = [] if states[0] == "open" else [f"transition: history starts at {states[0]!r}, not 'open'"]
    for a, b in zip(states, states[1:]):
        if b not in FINDING_TRANSITIONS[a]:
            errs.append(f"transition: {a} -> {b} is illegal (from {a}: {FINDING_TRANSITIONS[a]})")
    return errs


def evidence_errors(f: dict, root, decisions: list, owner=None) -> list:
    """What each state must carry (spec §4.1). Call only on a valid row."""
    errs = []
    for e in f["evidence"]:
        if not _sha(e) and os.path.isabs(e):
            errs.append(f"evidence: path must be relative to the repo root: {e!r}")
        elif not _sha(e) and not _file_under(root, e):
            errs.append(f"evidence: {e!r} is neither a commit sha nor a file under the repo root")
    states = [h["state"] for h in f["history"]]
    if "pinned" in states:
        pin = f["pin"]
        if pin is None:
            errs.append("evidence: pinned needs pin {test, transcript}")
        else:
            if not PIN_TEST.fullmatch(pin["test"]):
                errs.append(f"evidence: pin test must be test_<name> or test_<name>::test_<name>, got {pin['test']!r}")
            if os.path.isabs(pin["transcript"]):
                errs.append("evidence: pin transcript: must be relative to the repo root")
            elif not PIN_TRANSCRIPT.fullmatch(pin["transcript"]):
                errs.append("evidence: pin transcript must be docs/superpowers/state/<plan>/mutations/<file>, "
                            f"got {pin['transcript']!r}")
            elif not _file_under(root, pin["transcript"]):
                errs.append(f"evidence: pin transcript {pin['transcript']} does not exist")
        if not any(not _sha(e) and _file_under(root, e) for e in f["evidence"]):
            errs.append("evidence: pinned needs the reviewer's confirming report path in evidence")
    if f["state"] == "deferred":
        matches = [x for x in decisions if x.get("id") == f["ruling"]]
        if f["ruling"] is None:
            errs.append("evidence: deferred needs ruling = the owner's decision id")
        elif not matches:
            errs.append(f"evidence: ruling {f['ruling']} is not in decisions.yml")
        elif len(matches) > 1:
            errs.append(f"evidence: ruling {f['ruling']} appears more than once in decisions.yml")
        elif matches[0].get("answer") is None:
            errs.append(f"evidence: ruling {f['ruling']} is not answered by the owner")
        else:
            bad = validate_decision(matches[0], owner)
            if bad:
                errs.append(f"evidence: ruling {f['ruling']} is not a valid owner-answered decision ({'; '.join(bad)})")
    if f["state"] == "closed" and not f["evidence"]:
        errs.append("evidence: closed needs evidence (commits or report paths)")
    for h in f["history"]:
        if h["state"] == "merged" and h["by"] != POSTMERGE:
            errs.append(f"evidence: merged is set only by the post-merge script (by: {POSTMERGE}), got {h['by']!r}")
    return errs


def validate_claim(c: dict) -> list:
    errs = _keys(c, CLAIM_KEYS)
    if any(e.startswith("keys: missing") for e in errs):
        return errs
    if not isinstance(c["id"], str) or not CLAIM_ID.fullmatch(c["id"]):
        errs.append(f"id: {c['id']!r} does not match {CLAIM_ID.pattern}")
    for k in ("doc", "anchor", "text", "direct", "contra"):
        if not _text(c[k]):
            errs.append(f"{k}: must be a non-empty string" + _hint(c[k]))
    if not isinstance(c["cites"], list) or not c["cites"] or not all(_text(p) for p in c["cites"]):
        errs.append("cites: must be a non-empty list of paths (a claim citing nothing can never go stale)")
    if c["state"] is False:
        errs.append('state: YAML read false as a boolean; quote it: state: "false"')
    elif c["state"] not in CLAIM_STATES:
        errs.append(f"state: must be one of {CLAIM_STATES}, got {c['state']!r}")
    if c["verified_at"] is not None:
        errs += _sha_problem("verified_at", c["verified_at"])
    if c["by"] is not None and not _text(c["by"]):
        errs.append("by: must be null or a non-empty string" + _hint(c["by"]))
    if c["state"] == "verified" and (c["verified_at"] is None or c["by"] is None):
        errs.append("state: verified needs verified_at and by")
    if c["state"] == "false":
        if not (isinstance(c["finding"], str) and FINDING_ID.fullmatch(c["finding"])):
            errs.append("finding: a false claim must name the finding id that records it")
    elif c["finding"] is not None:
        errs.append("finding: only a false claim carries a finding (set it to null)")
    return errs


def validate_decision(d: dict, owner=None) -> list:
    """`owner` (default: config owner.handle) is the only handle allowed in answered_at.by."""
    errs = _keys(d, DECISION_KEYS)
    if any(e.startswith("keys: missing") for e in errs):
        return errs
    if not isinstance(d["id"], str) or not DECISION_ID.fullmatch(d["id"]):
        errs.append(f"id: {d['id']!r} does not match {DECISION_ID.pattern}")
    if d["class"] not in DECISION_CLASSES:
        errs.append(f"class: must be one of {DECISION_CLASSES}, got {d['class']!r}")
    if not _text(d["question"]):
        errs.append("question: must be a non-empty string" + _hint(d["question"]))
    elif d["class"] == "merge-grant" and not GRANT_QUESTION.fullmatch(d["question"]):
        errs.append("question: a merge-grant question is 'grant:<plan>'")
    opts = d["options"]
    if not isinstance(opts, list) or not all(
            isinstance(o, dict) and set(o) == {"label", "cost"} and _text(o["label"]) and _text(o["cost"])
            for o in opts):
        errs.append("options: must be a list of {label, cost}, both non-empty strings")
    elif len(opts) < 2 and d["class"] != "merge-grant":
        errs.append("options: an owner question needs at least two options with their cost")
    asked = d["asked_at"]
    if not isinstance(asked, dict) or set(asked) != {"sha", "by"} or not _text(asked.get("by")):
        errs.append("asked_at: must be {sha, by}")
    else:
        errs += _sha_problem("asked_at.sha", asked["sha"])
    ans = d["answer"]
    if ans is not None and not _text(ans):
        hint = " (YAML read yes/no/on/off as a boolean: quote it, e.g. answer: \"yes\")" \
            if isinstance(ans, bool) else ""
        errs.append(f"answer: must be null or a non-empty string, got {ans!r}{hint}")
    at = d["answered_at"]
    if at is not None:
        if not isinstance(at, dict) or set(at) != {"sha", "by"} or not _text(at.get("by")):
            errs.append("answered_at: must be null or {sha, by}")
        else:
            errs += _sha_problem("answered_at.sha", at["sha"])
            owner = owner_handle(owner)
            if at["by"] != owner:
                errs.append(f"{OWNER_ANSWER_ERROR} ({owner}) answers a decision, got {at['by']!r}")
    if (ans is None) != (at is None):
        errs.append("answered_at: answer and answered_at are both null or both set")
    return errs


class PathError(GateError):
    """A GateError worded `<path relative to root>: <reason>` (machine-independent); `reason` is the bare
    reason, `not_regular` says the path exists but is not a regular file."""

    def __init__(self, root, path, reason, not_regular=False):
        super().__init__(f"{os.path.relpath(path, root)}: {reason}")
        self.reason = reason
        self.not_regular = not_regular


def rel_error(root, path, exc) -> PathError:
    """A PathError worded `<path relative to root>: <strerror>`."""
    return PathError(root, path, exc.strerror or str(exc))


def require_regular(root, path) -> None:
    """`path` must be a regular file (after following symlinks): opening a FIFO would hang.
    Anything else, or a path that cannot be examined, is a PathError (a GateError)."""
    try:
        mode = os.stat(path).st_mode
    except OSError as exc:
        raise rel_error(root, path, exc) from None
    if not stat.S_ISREG(mode):
        raise PathError(root, path, "not a regular file", not_regular=True)


def safe_text(rel: str) -> str:
    """A path for a line id: control characters (a newline in a file name) are escaped, so the
    name cannot split or forge a result line."""
    return "".join(c if c.isprintable() else c.encode("unicode_escape").decode() for c in rel)


def _commit_errors(pairs, root, exists) -> list:
    return [f"{field}: sha {sha} is not a commit in this repository"
            for field, sha in pairs if _sha(sha) and not exists(root, sha)]


def row_id(row, index, pattern) -> str:
    """The id to print in a result line id: the row's own id only when it is a str matching
    `pattern`, else `entry-<index>` (a raw malformed id could carry a newline and forge or split a
    result line)."""
    rid = row.get("id")
    return rid if isinstance(rid, str) and pattern.fullmatch(rid) else f"entry-{index}"


def memo_commit_exists():
    """An `exists(root, sha) -> bool` for the *_commit_errors functions that asks git once per
    distinct (root, sha) and remembers the answer (a GateError is not remembered)."""
    seen = {}

    def exists(root, sha):
        key = (str(root), sha)
        if key not in seen:
            seen[key] = gitinfo.commit_exists(root, sha)
        return seen[key]

    return exists


def finding_commit_errors(f: dict, root, exists=gitinfo.commit_exists) -> list:
    """Every sha in a (valid) finding row -- history, cites, evidence entries shaped like a sha --
    must name a commit in the repository at `root` (DEC-0003). `exists(root, sha) -> bool`."""
    pairs = [(f"history[{i}].sha", h["sha"]) for i, h in enumerate(f["history"])]
    pairs += [(f"cites[{i}].sha", c["sha"]) for i, c in enumerate(f["cites"])]
    pairs += [(f"evidence[{i}]", e) for i, e in enumerate(f["evidence"])]
    return _commit_errors(pairs, root, exists)


def claim_commit_errors(c: dict, root, exists=gitinfo.commit_exists) -> list:
    return _commit_errors([("verified_at", c["verified_at"])], root, exists)


def decision_commit_errors(d: dict, root, exists=gitinfo.commit_exists) -> list:
    pairs = [("asked_at.sha", d["asked_at"]["sha"])]
    if d["answered_at"] is not None:
        pairs.append(("answered_at.sha", d["answered_at"]["sha"]))
    return _commit_errors(pairs, root, exists)


def owner_handle(owner=None) -> str:
    """`owner`, or config owner.handle when it is None."""
    return config.load(CODE_ROOT)["owner"]["handle"] if owner is None else owner


def finding_by_id(findings, id):
    return next((f for f in findings if f.get("id") == id), None)


def open_decisions(decisions, *, owner=None) -> list:
    """Every decision the owner has not answered (owner defaults to config owner.handle): `answer` is
    None, `answered_at` is not a mapping, or `answered_at.by` is not the owner. Only the owner's answer
    is an answer (spec §5), so a row answered by an agent is still open."""
    owner = owner_handle(owner)
    return [d for d in decisions
            if d.get("answer") is None or not isinstance(d.get("answered_at"), dict)
            or d["answered_at"].get("by") != owner]


def grant_for(decisions, plan: str, *, owner=None):
    """The merge grant for <plan>: a schema-valid decision of class merge-grant whose question is
    'grant:<plan>' (optionally followed by a space and free text), answered "yes" (case-insensitive)
    by the owner (default: config owner.handle). Any other row is no grant."""
    owner = owner_handle(owner)
    for d in decisions:
        if validate_decision(d, owner) != [] or d["class"] != "merge-grant":
            continue
        q = d["question"]
        if q != f"grant:{plan}" and not q.startswith(f"grant:{plan} "):
            continue
        ans = d["answer"]
        if (isinstance(ans, str) and ans.strip().lower() == "yes"
                and d["answered_at"]["by"] == owner):
            return d
    return None
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `python3 -m unittest tools.gates.tests.test_registers -v && python3 -m unittest discover -s tools/gates/tests -t .`
Expected: `Ran 52 tests` OK; whole suite `Ran 91 tests` OK.

Mutation check: in `transition_errors`, delete the `for a, b in zip(states, states[1:]):` loop (three lines). `test_every_illegal_edge_fails` must fail (an IndexError on the empty error list counts). Restore and run again: OK. Then in `grant_for`, replace `ans.strip().lower() == "yes"` with `True` (accept any answer). `test_grant_needs_an_exact_plan_and_a_yes` must fail. Restore.

- [ ] **Step 5: Ledger line, then commit**

Append `- T2 feat(gates): register loaders, schema validators and lifecycle rules -- test_registers 52 OK` to the ledger.

```bash
git add tools/gates/lib/registers.py tools/gates/tests/test_registers.py docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): register loaders, schema validators and lifecycle rules" \
  -m "Strict-delivery S1 Task 2 (spec §4.1-§4.3; umbrella Registers). Names YAML implicit-typing traps (unquoted sha, yes, false); grants need an exact plan and a yes."
```

### Task 3: `gate_register`

**Files:**
- Create: `tools/gates/gate_register.py`
- Create: fixtures under `tools/gates/tests/fixtures/register/` (`valid`, including two reports under `valid/docs/reviews/`; `illegal_transition`, `pinned_no_transcript`, `missing_owner`, `missing_file`, `report_unknown`)
- Test: `tools/gates/tests/test_gate_register.py`

**Interfaces:**
- Consumes: `GateError`, `Result`, `gate_args`, `repo_root`, `run_gate` (Task 1); `registers.load_findings`, `load_decisions`, `validate_finding`, `transition_errors`, `evidence_errors`, `validate_decision`, `finding_commit_errors`, `decision_commit_errors`, `row_id`, `memo_commit_exists`, `rel_error`, `require_regular`, `safe_text`, `FINDING_ID`, `DECISION_ID` (Task 2).
- Produces: `gate_register.check(args: list) -> Result` (args: `--root <tree>`), `main(argv=None) -> int`, `registered_reports(root) -> (found: list[str], bad: list[(path, line)])`, `report_ids(path, root) -> list[str]`, `REPORT_DIRS`, `MARKER = "register: true"`. Line ids: `register.{schema|owner|duplicate|transition|evidence}.<finding or decision id>` on FAIL (a malformed id prints as `entry-<index>`), `register.marker.<file relative to root>` for a near-miss marker, and `register.report.<file relative to root>.<id>: not in register` for a registered report citing an unregistered id (owner ruling 5). One `PASS register.<category>` per category in which every row was evaluated and none failed (a category with a skipped row prints no PASS), in the order schema, owner, duplicate, transition, evidence, report. `--root ""` is `FAIL register.input: --root is empty`; a missing `docs/reviews` is `FAIL register.input: docs/reviews: missing report directory`.

- [ ] **Step 1: Write the fixtures**

```bash
F=tools/gates/tests/fixtures/register
mkdir -p $F/valid/docs/superpowers/state/demo/mutations $F/valid/docs/superpowers/state/demo/review
for c in illegal_transition pinned_no_transcript missing_owner missing_file report_unknown; do
  mkdir -p $F/$c/docs/superpowers/state $F/$c/docs/reviews
  printf '[]\n' > $F/$c/docs/superpowers/state/decisions.yml
  : > $F/$c/docs/reviews/.gitkeep          # docs/reviews must exist (a missing one is a FAIL)
done
mkdir -p $F/valid/docs/reviews $F/report_unknown/docs/superpowers/state/briefs
printf '[]\n' > $F/report_unknown/docs/superpowers/state/findings.yml
```

(`missing_file` deliberately has no `findings.yml`.)

`tools/gates/tests/fixtures/register/valid/docs/superpowers/state/findings.yml`:

```yaml
- id: T1-OPEN-1
  source: audit
  severity: MEDIUM
  class: >-
    An open finding owned by an issue.
  siblings: []
  cites: [{file: components/core/a.c, lines: "10-20", sha: "aaaaaaa"}]
  repro: null
  owner: {kind: issue, number: 140}
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "aaaaaaa", by: audit}]
- id: T1-PIN-1
  source: review
  severity: HIGH
  class: >-
    A pinned finding: production-path test, mutation transcript and the reviewer's report.
  siblings: [CRC-bad KEY]
  cites: [{file: components/core/b.c, lines: "1-5", sha: "aaaaaaa"}]
  repro: null
  owner: {kind: plan, plan: demo, task: 3}
  state: pinned
  pin: {test: test_crc_bad_delta_fails, transcript: docs/superpowers/state/demo/mutations/T1-PIN-1.txt}
  ruling: null
  evidence: ["bbbbbbb", docs/superpowers/state/demo/review/T1-PIN-1.md]
  history:
    - {state: open, sha: "aaaaaaa", by: audit}
    - {state: planned, sha: "aaaaaaa", by: session}
    - {state: in-progress, sha: "bbbbbbb", by: session}
    - {state: pinned, sha: "bbbbbbb", by: reviewer}
- id: T1-DEF-1
  source: agent
  severity: LOW
  class: >-
    A finding the owner deferred by decision.
  siblings: []
  cites: []
  repro: null
  owner: {kind: roadmap, plan: 8}
  state: deferred
  pin: null
  ruling: DEC-9001
  evidence: []
  history: [{state: open, sha: "aaaaaaa", by: agent}, {state: deferred, sha: "aaaaaaa", by: session}]
```

`tools/gates/tests/fixtures/register/valid/docs/superpowers/state/decisions.yml`:

```yaml
- id: DEC-9001
  class: scope
  question: "Defer T1-DEF-1 to Plan 8 or fix now?"
  options: [{label: defer, cost: "none now"}, {label: fix, cost: "1 task"}]
  asked_at: {sha: "aaaaaaa", by: session}
  answer: "defer"
  answered_at: {sha: "aaaaaaa", by: superbrobenji}
```

`tools/gates/tests/fixtures/register/valid/docs/superpowers/state/demo/mutations/T1-PIN-1.txt`:

```text
finding: T1-PIN-1
test: test_crc_bad_delta_fails
step 1 (fix present): python3 -m unittest demo -> PASS
step 2 (fix reverted: git stash of components/core/b.c): python3 -m unittest demo -> FAIL
step 3 (fix restored): python3 -m unittest demo -> PASS
```

`tools/gates/tests/fixtures/register/valid/docs/superpowers/state/demo/review/T1-PIN-1.md`:

```markdown
status: DONE
T1-PIN-1 pin confirmed: mutation re-run, PASS/FAIL/PASS.
```

`tools/gates/tests/fixtures/register/valid/docs/reviews/2026-01-01-registered.md` (marked; every id it cites is registered → PASS):

```markdown
# A registered review

register: true

| Id | Severity | Class |
|---|---|---|
| T1-OPEN-1 | MEDIUM | An open finding owned by an issue. |
| T1-DEF-1 | LOW | A deferred finding. |

- T1-PIN-1: pinned by test_crc_bad_delta_fails.
- See T9-PROSE-1 in prose: not a list item, not a table cell, not checked.
```

`tools/gates/tests/fixtures/register/valid/docs/reviews/2026-01-02-unmarked.md` (no marker; its unknown ids are ignored):

```markdown
# An unmarked review (no marker line): ignored by the gate

| Id | Severity |
|---|---|
| T9-NOPE-1 | HIGH |

- T9-NOPE-2: never registered, and that is fine here.
```

`tools/gates/tests/fixtures/register/report_unknown/docs/superpowers/state/briefs/b1-brief.md` (marked; cites one unregistered id → FAIL):

```markdown
# A registered brief

register: true

| Finding | Task |
|---|---|
| T9-NOPE-1 | 3 |
```

`tools/gates/tests/fixtures/register/illegal_transition/docs/superpowers/state/findings.yml`:

```yaml
- id: T1-JUMP-1
  source: review
  severity: HIGH
  class: >-
    Well-formed YAML whose history jumps from open straight to pinned.
  siblings: []
  cites: []
  repro: null
  owner: {kind: issue, number: 1}
  state: pinned
  pin: {test: test_x, transcript: docs/superpowers/state/demo/mutations/T1-JUMP-1.txt}
  ruling: null
  evidence: []
  history: [{state: open, sha: "aaaaaaa", by: audit}, {state: pinned, sha: "aaaaaaa", by: session}]
```

`tools/gates/tests/fixtures/register/pinned_no_transcript/docs/superpowers/state/findings.yml`:

```yaml
- id: T1-NOPIN-1
  source: review
  severity: HIGH
  class: >-
    Marked pinned by hand with a test name but the mutation transcript was never recorded.
  siblings: []
  cites: []
  repro: null
  owner: {kind: plan, plan: demo, task: 1}
  state: pinned
  pin: {test: test_x, transcript: docs/superpowers/state/demo/mutations/T1-NOPIN-1.txt}
  ruling: null
  evidence: ["bbbbbbb"]
  history:
    - {state: open, sha: "aaaaaaa", by: audit}
    - {state: planned, sha: "aaaaaaa", by: session}
    - {state: in-progress, sha: "aaaaaaa", by: session}
    - {state: pinned, sha: "bbbbbbb", by: session}
```

`tools/gates/tests/fixtures/register/missing_owner/docs/superpowers/state/findings.yml`:

```yaml
- id: T1-ORPHAN-1
  source: audit
  severity: MEDIUM
  class: >-
    A finding nobody owns.
  siblings: []
  cites: []
  repro: null
  owner: null
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "aaaaaaa", by: audit}]
```

- [ ] **Step 2: Write the failing tests**

`tools/gates/tests/test_gate_register.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/gate_register.py through main() on fixture trees (the production path).
Run from the repo root: python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import re
import signal
import tempfile
import unittest

from tools.gates import gate_register
from tools.gates.tests import gitfixture as gf

FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures", "register")


def fixture_repo(case):
    """A throwaway git repo holding the fixture tree `case`. The fixtures write their shas as
    aaaaaaa / bbbbbbb; here those become two real commits of the repo (DEC-0003)."""
    root = gf.new_repo({"findings": None, "claims": None, "decisions": None})
    first = gf.git(root, "rev-parse", "--short=7", "HEAD")
    gf.write(root, "second.txt", "second\n")
    second = gf.commit_all(root)[:7]
    top = os.path.join(FIX, case)
    for dirpath, _, names in os.walk(top):
        for name in names:
            with open(os.path.join(dirpath, name), encoding="utf-8") as fh:
                text = fh.read()
            gf.write(root, os.path.relpath(os.path.join(dirpath, name), top),
                     text.replace("aaaaaaa", first).replace("bbbbbbb", second))
    return root


def main_out(root, *args):
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = gate_register.main(list(args) if args else ["--root", root])
    return rc, buf.getvalue()


def report_repo():
    """An empty-register git repo that has the required docs/reviews directory."""
    root = gf.new_repo()
    os.makedirs(os.path.join(root, "docs", "reviews"))
    return root


def run(case, *lead):
    """main() on a fixture tree; `lead` goes before --root (--json is recognised only as argv[0])."""
    root = fixture_repo(case)
    try:
        return main_out(root, *lead, "--root", root)
    finally:
        gf.remove(root)


class GateRegister(unittest.TestCase):
    def test_valid_register_passes_every_category(self):
        rc, out = run("valid")
        self.assertEqual("PASS register.schema\nPASS register.owner\nPASS register.duplicate\n"
                         "PASS register.transition\nPASS register.evidence\nPASS register.report\n", out)
        self.assertEqual(0, rc)

    def test_illegal_transition_fails(self):
        rc, out = run("illegal_transition")
        self.assertIn("FAIL register.transition.T1-JUMP-1: transition: open -> pinned is illegal", out)
        self.assertEqual(1, rc)

    def test_pinned_without_transcript_fails(self):
        rc, out = run("pinned_no_transcript")
        self.assertIn("FAIL register.evidence.T1-NOPIN-1: evidence: pin transcript "
                      "docs/superpowers/state/demo/mutations/T1-NOPIN-1.txt does not exist", out)
        self.assertIn("FAIL register.evidence.T1-NOPIN-1: evidence: pinned needs the reviewer's "
                      "confirming report path in evidence", out)
        self.assertEqual(1, rc)

    def test_missing_owner_fails(self):
        rc, out = run("missing_owner")
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: owner: missing", out)
        self.assertNotIn("PASS register.owner", out)
        self.assertEqual(1, rc)

    def test_missing_file_is_a_fail_input(self):
        rc, out = run("missing_file")
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL register.input: "), out)
        self.assertIn("findings.yml: No such file or directory", out)

    def test_registered_report_with_an_unknown_id_fails(self):
        rc, out = run("report_unknown")
        self.assertEqual("FAIL register.report.docs/superpowers/state/briefs/b1-brief.md.T9-NOPE-1: "
                         "not in register\n", out.split("PASS")[0])
        self.assertEqual(1, rc)

    def test_unmarked_report_is_ignored_and_marked_report_with_known_ids_passes(self):
        rc, out = run("valid")      # valid/docs/reviews: one marked (all ids known), one unmarked
        self.assertNotIn("T9-NOPE", out)
        self.assertNotIn("T9-PROSE", out)
        self.assertIn("PASS register.report", out)
        self.assertEqual(0, rc)

    def test_report_ids_reads_table_cells_and_list_items(self):
        path = os.path.join(FIX, "valid", "docs", "reviews", "2026-01-01-registered.md")
        self.assertEqual(["T1-OPEN-1", "T1-DEF-1", "T1-PIN-1"],
                         gate_register.report_ids(path, os.path.join(FIX, "valid")))

    def test_report_ids_ignores_what_is_not_a_finding_id(self):
        root = gf.new_repo()
        try:
            path = gf.write(root, "r.md", "register: true\n| t1-lower-1 | x |\n| T1-OPEN-1x y | x |\n"
                            "- t1-lower-1: no\n- Note: prose\n- T1-OPEN-1x\u00e9 y\n1. T1-DEF-1 numbered\n"
                            "- T1-FW-\uff11 full-width digit\n-T1-NOSPACE-1: no bullet space\n"
                            "| `T1-A-1 | unbalanced\n| ** T1-B-1** | spaces inside the wrapper\n")
            self.assertEqual(["T1-DEF-1"], gate_register.report_ids(path, root))
        finally:
            gf.remove(root)

    def test_report_ids_accepts_every_dec_0002_form(self):
        root = gf.new_repo()
        try:
            forms = ["- T1-A-1 space", "- T1-A-2: colon", "- T1-A-3, comma", "- T1-A-4. period", "- T1-A-5\ttab",
                     "- T1-A-6", "- **T1-A-7** bold", "- **T1-A-8**: bold colon", "- `T1-A-9` tick",
                     "- `T1-A-10`, tick comma", "- [ ] T1-A-11: open box", "- [x] T1-A-12 done box",
                     "- [X] **T1-A-13**: both", "* T1-A-14 star", "+ T1-A-15 plus", "1. T1-A-16 num", "2) T1-A-17 paren",
                     "  - T1-A-18 nested", "- T1-A-19\r", "| **T1-A-20** | bold cell |", "| `T1-A-21` | tick cell |",
                     "| T1-A-22 | plain cell |"]
            text = "register: true\n" + "\n".join(forms) + "\n"
            path = gf.write(root, "r.md", text)
            self.assertEqual([f"T1-A-{n}" for n in range(1, 23)], gate_register.report_ids(path, root))
            bare = gf.write(root, "b.md", "register: true\n- T1-B-1\n- T1-B-2")      # no newline at the end of the file
            self.assertEqual(["T1-B-1", "T1-B-2"], gate_register.report_ids(bare, root))
            crlf = gf.write(root, "c.md", "register: true\r\n- T1-C-1\r\n- **T1-C-2**\r\n")
            self.assertEqual(["T1-C-1", "T1-C-2"], gate_register.report_ids(crlf, root))
        finally:
            gf.remove(root)

    def test_report_ids_needs_the_root(self):
        with self.assertRaises(TypeError):
            gate_register.report_ids("x.md")

    def test_unreadable_report_subdirectory_fails_closed(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores directory permissions")
        root = gf.new_repo()
        locked = os.path.join(root, "docs", "reviews", "locked")
        try:
            gf.write(root, os.path.join("docs", "reviews", "locked", "x.md"), "register: true\n")
            os.chmod(locked, 0)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.input: ", buf.getvalue())
            self.assertEqual("FAIL register.input: docs/reviews/locked: Permission denied\n", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            if os.path.exists(locked):
                os.chmod(locked, 0o755)
            gf.remove(root)

    def test_report_path_that_is_a_file_fails_closed(self):
        root = gf.new_repo()
        try:
            gf.write(root, os.path.join("docs", "reviews"), "not a directory\n")
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.input: ", buf.getvalue())
            self.assertEqual("FAIL register.input: docs/reviews: not a directory\n", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_symlinked_report_directory_is_followed_and_cycles_stop(self):
        root = report_repo()
        try:
            briefs = os.path.join(root, "docs", "superpowers", "state", "briefs")
            gf.write(root, os.path.join("docs", "superpowers", "state", "real", "b.md"),
                     "register: true\n- T9-NOPE-1: x\n")
            os.makedirs(briefs)
            os.symlink(os.path.join(root, "docs", "superpowers", "state", "real"), os.path.join(briefs, "link"))
            os.symlink(briefs, os.path.join(briefs, "loop"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.report.docs/superpowers/state/briefs/link/b.md.T9-NOPE-1: "
                          "not in register", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_unreadable_report_file_fails_with_a_relative_path(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores file permissions")
        root = gf.new_repo()
        path = os.path.join(root, "docs", "reviews", "r.md")
        try:
            gf.write(root, os.path.join("docs", "reviews", "r.md"), "register: true\n- T1-X-1: x\n")
            os.chmod(path, 0)
            with self.assertRaises(gate_register.GateError) as ctx:
                gate_register.report_ids(path, root)
            self.assertEqual("docs/reviews/r.md: Permission denied", str(ctx.exception))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertEqual("FAIL register.input: docs/reviews/r.md: Permission denied\n", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            if os.path.exists(path):
                os.chmod(path, 0o644)
            gf.remove(root)

    def test_top_directory_reached_through_a_symlink_is_reported_once(self):
        root = gf.new_repo()
        try:
            briefs = os.path.join(root, "docs", "superpowers", "state", "briefs")
            gf.write(root, os.path.join("docs", "superpowers", "state", "briefs", "b.md"),
                     "register: true\n- T9-NOPE-1: x\n")
            os.symlink(briefs, os.path.join(briefs, "loop"))
            os.symlink(briefs, os.path.join(root, "docs", "reviews"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertEqual(1, buf.getvalue().count("T9-NOPE-1: not in register"), buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_dangling_symlink_in_a_report_directory_fails_with_a_relative_path(self):
        root = gf.new_repo()
        try:
            os.makedirs(os.path.join(root, "docs", "reviews"))
            os.symlink(os.path.join(root, "nowhere.md"), os.path.join(root, "docs", "reviews", "dangling.md"))
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertEqual("FAIL register.input: docs/reviews/dangling.md: No such file or directory\n",
                             buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_duplicate_ids_fail(self):
        root = report_repo()
        try:
            with open(os.path.join(FIX, "missing_owner", gf.STATE, "findings.yml")) as fh:
                row = fh.read().replace("owner: null", "owner: {kind: issue, number: 2}")
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), row + row)
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_register.main(["--root", root])
            self.assertIn("FAIL register.duplicate.T1-ORPHAN-1: id appears more than once", buf.getvalue())
            self.assertEqual(1, rc)
        finally:
            gf.remove(root)

    def test_json_output(self):
        rc, out = run("missing_owner", "--json")
        self.assertIn('"ok": false', out)
        self.assertEqual(1, rc)

    def test_the_repository_register_passes(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_register.main(["--root", gf.REPO])
        self.assertEqual(0, rc, buf.getvalue())


ORPHAN = os.path.join(FIX, "missing_owner", gf.STATE, "findings.yml")
NO_OWNER_MSG = "owner: missing (every finding needs an owner: a plan task, an issue or a roadmap plan)"


def findings_text(**edits):
    """The missing_owner fixture row with `key: value` lines replaced (value None deletes the line)."""
    with open(ORPHAN, encoding="utf-8") as fh:
        lines = fh.read().splitlines(keepends=True)
    out = []
    for line in lines:
        lead = "- " if line.startswith("- ") else "  "
        key = line.strip().split(":")[0].lstrip("- ")
        if key in edits and (line.startswith("- ") or line.startswith("  " + key + ":")):
            if edits[key] is not None:
                out.append(f"{lead}{key}: {edits[key]}\n")
            elif lead == "- ":
                raise AssertionError("cannot delete the first line")
            continue
        out.append(line)
    return "".join(out)


def gate_on(registers=None, files=None):
    """main() on a fresh repo (empty registers unless given) plus extra files; returns (rc, out, root)."""
    root = report_repo()
    for name, text in (registers or {}).items():
        gf.write(root, os.path.join(gf.STATE, f"{name}.yml"), text)
    for rel, text in (files or {}).items():
        gf.write(root, rel, text)
    gf.commit_all(root)
    rc, out = main_out(root)
    return rc, out, root


class FixWave(unittest.TestCase):
    """Block b1 final-review fix wave, every case through main()."""

    def gate(self, **kw):
        rc, out, root = gate_on(**kw)
        self.addCleanup(gf.remove, root)
        return rc, out, root

    # A1 -- a category PASS needs every row evaluated and none failed
    def test_a_row_without_an_owner_key_fails_owner_and_never_passes_it(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(owner=None)})
        self.assertEqual("FAIL register.owner.T1-ORPHAN-1: keys: missing owner\n"
                         "PASS register.schema\nPASS register.duplicate\nPASS register.report\n", out)
        self.assertEqual(1, rc)

    def test_null_owner_plus_another_missing_key_fails_both_categories(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(owner="null", siblings=None)})
        self.assertEqual(f"FAIL register.schema.T1-ORPHAN-1: keys: missing siblings\n"
                         f"FAIL register.owner.T1-ORPHAN-1: {NO_OWNER_MSG}\n"
                         "PASS register.duplicate\nPASS register.report\n", out)
        self.assertEqual(1, rc)

    def test_owner_and_other_keys_missing_together_fails_both_categories(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(owner=None, siblings=None)})
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: keys: missing siblings, owner", out)
        self.assertIn("FAIL register.schema.T1-ORPHAN-1: keys: missing siblings, owner", out)
        self.assertNotIn("PASS register.owner", out)
        self.assertNotIn("PASS register.schema", out)

    def test_categories_whose_rows_were_skipped_never_pass(self):
        rc, out, _ = self.gate(registers={"findings": findings_text(severity="SEVERE", owner="{kind: issue, number: 2}")})
        self.assertIn("FAIL register.schema.T1-ORPHAN-1: severity:", out)
        for cat in ("schema", "transition", "evidence"):
            self.assertNotIn(f"PASS register.{cat}", out)
        self.assertIn("PASS register.owner", out)
        rc, out, _ = self.gate(registers={"decisions": "- {id: DEC-0001, class: triage}\n"})
        self.assertIn("FAIL register.schema.DEC-0001: keys: missing ", out)
        self.assertNotIn("PASS register.schema", out)
        self.assertNotIn("PASS register.evidence", out)
        self.assertIn("PASS register.transition", out)

    # A2 -- deferred needs a valid, unique, owner-answered decision
    def deferred_gate(self, decisions):
        row = findings_text(owner="{kind: issue, number: 2}", state="deferred", ruling="DEC-9001").replace(
            "history: [{state: open, sha: \"aaaaaaa\", by: audit}]",
            "history: [{state: open, sha: \"aaaaaaa\", by: audit}, {state: deferred, sha: \"aaaaaaa\", by: s}]")
        root = report_repo()
        self.addCleanup(gf.remove, root)
        first = gf.git(root, "rev-parse", "--short=7", "HEAD")
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), row.replace("aaaaaaa", first))
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), decisions.replace("aaaaaaa", first))
        gf.commit_all(root)
        return main_out(root)

    DECISION = ('- id: DEC-9001\n  class: scope\n  question: "defer it?"\n'
                '  options: [{label: a, cost: x}, {label: b, cost: y}]\n'
                '  asked_at: {sha: "aaaaaaa", by: session}\n  answer: ANSWER\n  answered_at: AT\n')

    def decision(self, answer='"defer"', at='{sha: "aaaaaaa", by: superbrobenji}'):
        return self.DECISION.replace("ANSWER", answer).replace("AT", at)

    def test_deferred_on_an_owner_answered_decision_passes(self):
        rc, out = self.deferred_gate(self.decision())
        self.assertEqual(0, rc, out)

    def test_deferred_on_a_decision_the_agent_answered_fails(self):
        rc, out = self.deferred_gate(self.decision(at='{sha: "aaaaaaa", by: session}'))
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.schema.DEC-9001: answered_at.by: only the owner (superbrobenji) answers a decision, got 'session'", out)
        self.assertIn("FAIL register.evidence.T1-ORPHAN-1: evidence: ruling DEC-9001 is not a valid owner-answered decision", out)

    def test_deferred_on_a_boolean_answer_fails(self):
        rc, out = self.deferred_gate(self.decision(answer="no"))
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.schema.DEC-9001: answer: must be null or a non-empty string, got False", out)
        self.assertIn("evidence: ruling DEC-9001 is not a valid owner-answered decision (answer:", out)

    def test_duplicate_decision_ids_fail(self):
        rc, out = self.deferred_gate(self.decision() + self.decision())
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.duplicate.DEC-9001: id appears more than once", out)
        self.assertIn("evidence: ruling DEC-9001 appears more than once in decisions.yml", out)

    # A3
    def test_an_empty_root_is_a_fail_not_the_cwd_repo(self):
        for root in ("", "  ", "\t"):
            self.assertEqual((1, "FAIL register.input: --root is empty\n"), main_out(None, "--root", root))

    # A4 -- marker near misses
    def report(self, first_line, body="- T9-NOPE-1: x\n"):
        return self.gate(files={"docs/reviews/r.md": first_line + "\n" + body})

    def test_exact_marker_registers_the_report(self):
        for marker in ("register: true", "register: true\r"):
            rc, out, _ = self.report(marker)
            self.assertEqual(1, rc)
            self.assertIn("FAIL register.report.docs/reviews/r.md.T9-NOPE-1: not in register", out, marker)
            self.assertNotIn("register.marker", out)

    def test_near_miss_markers_fail_instead_of_unregistering_the_report(self):
        for marker in ("register: yes", "register: True", "Register: true", "register:true", "register: TRUE",
                       "register: on", "register: y", "register: 1", "REGISTER : Y", "  register: true",
                       "register: true  ", "register:\ttrue", "\ufeffregister: true",
                       'register: "true"', "register: 'true'", "register: true # x", "register: true.",
                       "register:  true", "register: false", "register: maybe", "register: true now", "register:"):
            rc, out, _ = self.report(marker)
            self.assertEqual(1, rc, marker)
            self.assertEqual(f"FAIL register.marker.docs/reviews/r.md: marker must be exactly 'register: true' "
                             f"(found {marker.rstrip(chr(13))!r})\n"
                             "PASS register.schema\nPASS register.owner\nPASS register.duplicate\n"
                             "PASS register.transition\nPASS register.evidence\n", out, marker)

    def test_two_space_marker_shows_both_spaces(self):
        rc, out, _ = self.report("register:  true")
        self.assertIn("(found 'register:  true')", out)

    def test_lines_that_are_not_marker_attempts_are_ignored(self):
        for marker in ("registered: true", "# register: true", "the register: true", "- register: true"):
            rc, out, _ = self.report(marker)
            self.assertEqual(0, rc, marker)
        late = "\n" * 20 + "register: yes\n- T9-NOPE-1: x\n"       # beyond the first 20 lines
        rc, out, _ = self.gate(files={"docs/reviews/late.md": late})
        self.assertEqual(0, rc, out)

    def test_a_filename_with_control_characters_cannot_split_a_line(self):
        rc, out, _ = self.gate(files={"docs/reviews/a\nPASS b.md": "register: yes\n",
                                      "docs/reviews/c\nPASS d.md": "register: true\n- T9-NOPE-1: x\n"})
        self.assertEqual(1, rc)
        self.assertIn("FAIL register.marker.docs/reviews/a\\nPASS b.md: marker must be exactly", out)
        self.assertIn("FAIL register.report.docs/reviews/c\\nPASS d.md.T9-NOPE-1: not in register", out)
        self.assertEqual([], [l for l in out.splitlines() if l.startswith("PASS") and "register." not in l])
        self.assertEqual(["PASS register.schema", "PASS register.owner", "PASS register.duplicate",
                          "PASS register.transition", "PASS register.evidence"],
                         [l for l in out.splitlines() if l.startswith("PASS")])

    def test_a_shallow_clone_fails_the_sha_check_by_design(self):
        import subprocess
        root = gf.new_repo()
        self.addCleanup(gf.remove, root)
        old = gf.git(root, "rev-parse", "--short=7", "HEAD")
        gf.write(root, os.path.join(gf.STATE, "findings.yml"),
                 findings_text(owner="{kind: issue, number: 2}").replace("aaaaaaa", old))
        gf.write(root, "docs/reviews/.gitkeep", "")
        gf.commit_all(root)
        self.assertEqual((0, ), (main_out(root)[0], ))
        parent = tempfile.mkdtemp(prefix="gate-clone-")
        self.addCleanup(gf.remove, parent)
        gf.git(parent, "clone", "-q", "--depth", "1", "file://" + root, "shallow")
        rc, out = main_out(os.path.join(parent, "shallow"))
        self.assertEqual(1, rc)
        self.assertIn(f"FAIL register.evidence.T1-ORPHAN-1: history[0].sha: sha {old} is not a commit in this repository", out)

    # A7 / A8 -- machine-independent texts, real commits
    def test_load_failures_name_files_relative_to_the_root(self):
        root = report_repo()
        self.addCleanup(gf.remove, root)
        os.remove(os.path.join(root, gf.STATE, "findings.yml"))
        self.assertEqual((1, "FAIL register.input: docs/superpowers/state/findings.yml: No such file or directory\n"),
                         main_out(root))
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), "- id: [\n")
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), "[]\n")
        rc, out = main_out(root)
        self.assertEqual(1, rc)
        self.assertEqual(1, out.count("\n"), out)                       # one physical line
        self.assertTrue(out.startswith("FAIL register.input: docs/superpowers/state/findings.yml: invalid YAML: "), out)
        self.assertIn('in "docs/superpowers/state/findings.yml", line', out)
        self.assertNotIn(root, out)
        for text, msg in (("", "empty file (an empty register is the YAML list [])"), ("id: x\n", "top level must be a YAML list"),
                          ("- x\n", "entry 0 is not a mapping")):
            gf.write(root, os.path.join(gf.STATE, "findings.yml"), text)
            self.assertEqual((1, f"FAIL register.input: docs/superpowers/state/findings.yml: {msg}\n"), main_out(root))
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), "[]\n")
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), "- {id: DEC-0001, id: DEC-0002}\n")
        rc, out = main_out(root)
        self.assertTrue(out.startswith("FAIL register.input: docs/superpowers/state/decisions.yml: invalid YAML: "), out)
        self.assertNotIn(root, out)

    def test_a_root_that_is_not_a_git_repository_is_a_fail_without_the_path(self):
        root = tempfile.mkdtemp(prefix="gate-plain-")
        self.addCleanup(gf.remove, root)
        gf.write(root, os.path.join(gf.STATE, "findings.yml"), findings_text(owner="{kind: issue, number: 2}"))
        gf.write(root, os.path.join(gf.STATE, "decisions.yml"), "[]\n")
        os.makedirs(os.path.join(root, "docs", "reviews"))
        rc, out = main_out(root)
        self.assertEqual(1, rc)
        self.assertTrue(out.startswith("FAIL register.input: git cat-file -e aaaaaaa^{commit}: "), out)
        self.assertNotIn(root, out)

    def test_every_sha_in_findings_and_decisions_must_be_a_commit(self):
        row = findings_text(owner="{kind: issue, number: 2}", evidence='["0000000"]',
                            cites='[{file: a.c, lines: "1", sha: "0000000"}]').replace('sha: "aaaaaaa"', 'sha: "0000000"')
        dec = ('- id: DEC-9001\n  class: scope\n  question: "q?"\n  options: [{label: a, cost: x}, {label: b, cost: y}]\n'
               '  asked_at: {sha: "0000000", by: session}\n  answer: "a"\n  answered_at: {sha: "0000000", by: superbrobenji}\n')
        rc, out, _ = self.gate(registers={"findings": row, "decisions": dec})
        self.assertEqual(1, rc)
        gone = "sha 0000000 is not a commit in this repository"
        for fid, field in (("T1-ORPHAN-1", "history[0].sha"), ("T1-ORPHAN-1", "cites[0].sha"),
                           ("T1-ORPHAN-1", "evidence[0]"), ("DEC-9001", "asked_at.sha"), ("DEC-9001", "answered_at.sha")):
            self.assertIn(f"FAIL register.evidence.{fid}: {field}: {gone}\n", out)
        self.assertNotIn("PASS register.evidence", out)

    def test_the_repository_registers_name_commits_of_this_repository(self):
        rc, out = main_out(gf.REPO)
        self.assertEqual(0, rc, out)

    # B6
    def test_a_malformed_id_cannot_forge_or_split_a_result_line(self):
        evil = findings_text(owner="{kind: issue, number: 2}", id='"T1-A-1\\nPASS register.forged"')
        rc, out, _ = self.gate(registers={"findings": evil})
        self.assertEqual(1, rc)
        self.assertEqual(["FAIL register.schema.entry-0: id: 'T1-A-1\\nPASS register.forged' does not match "
                          + "^[A-Z0-9]+(-[A-Za-z0-9]+)+$"], [l for l in out.splitlines() if l.startswith("FAIL")])
        self.assertEqual([], [l for l in out.splitlines() if "forged" in l and not l.startswith("FAIL")])
        self.assertEqual(4, len(out.splitlines()), out)             # one FAIL + three PASS: no line was split

    # B11
    def test_a_fifo_in_a_report_directory_fails_instead_of_hanging(self):
        root = report_repo()
        self.addCleanup(gf.remove, root)
        os.mkfifo(os.path.join(root, "docs", "reviews", "pipe.md"))
        old = signal.signal(signal.SIGALRM, lambda *a: (_ for _ in ()).throw(AssertionError("gate hung on a FIFO")))
        signal.alarm(10)
        try:
            rc, out = main_out(root)
            self.assertEqual((1, "FAIL register.input: docs/reviews/pipe.md: not a regular file\n"), (rc, out))
            with self.assertRaises(gate_register.GateError):
                gate_register.report_ids(os.path.join(root, "docs", "reviews", "pipe.md"), root)
        finally:
            signal.alarm(0)
            signal.signal(signal.SIGALRM, old)

    # B12
    def test_docs_reviews_must_exist_but_briefs_may_be_absent(self):
        root = gf.new_repo()
        self.addCleanup(gf.remove, root)
        self.assertEqual((1, "FAIL register.input: docs/reviews: missing report directory\n"), main_out(root))
        os.makedirs(os.path.join(root, "docs", "reviews"))
        rc, out = main_out(root)
        self.assertEqual(0, rc, out)
        self.assertFalse(os.path.exists(os.path.join(root, "docs", "superpowers", "state", "briefs")))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_register -v`
Expected: ERROR, `ImportError: cannot import name 'gate_register' from 'tools.gates'`.

- [ ] **Step 4: Write the gate**

`tools/gates/gate_register.py`:

```python
#!/usr/bin/env python3
"""gate_register -- the findings register (spec §4.1, §7): schema, an owner on every row, legal
lifecycle transitions, the evidence each state needs, every sha naming a commit in the gated
repository (DEC-0003), and every finding id cited by a registered report (umbrella "Owner rulings"
5, widened by DEC-0002). A registered report is a file under docs/reviews/ or
docs/superpowers/state/briefs/ with the exact line `register: true` among its first 20 lines; a
line there that starts like one (`register:` in any case, optionally after a UTF-8 BOM or spaces)
but is not exactly the marker (`register: yes`, `register: "true"`, `register: true # x`, trailing
spaces, a BOM ...) is a FAIL `register.marker.<file>`, never a silent
un-register. In a registered report a table row whose first cell is a finding id (optionally
wrapped in `**...**` or backticks), or a list item (`-`, `*`, `+`, `1.`, `1)`, optionally followed
by a `[ ]` / `[x]` checkbox) that starts with one (bare, `**id**` or `` `id` ``) followed by a
space, tab, colon, comma, period or the end of the line, must name a row in findings.yml (the id
matches registers.FINDING_ID via fullmatch). Files without the marker are ignored.
docs/reviews must exist (a missing one is a FAIL); docs/superpowers/state/briefs may be absent;
a report path that is not a directory, an entry that is not a regular file, or anything that
cannot be read is a FAIL; symlinked subdirectories are followed once each.

  python3 tools/gates/gate_register.py [--json] [--root <tree>]
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import registers  # noqa: E402

CATEGORIES = ["schema", "owner", "duplicate", "transition", "evidence", "report"]
REPORT_DIRS = [os.path.join("docs", "reviews"), os.path.join("docs", "superpowers", "state", "briefs")]
REQUIRED_REPORT_DIRS = [os.path.join("docs", "reviews")]
MARKER = "register: true"
MARKER_LINES = 20
# anything that reads as an attempt at the marker: it must then be the marker, exactly
MARKER_ATTEMPT = re.compile(r"^\ufeff?\s*register\s*:", re.I)
LIST_ITEM = re.compile(r"^\s*(?:[-*+]|[0-9]+[.)])\s+(?:\[[ xX]\]\s+)?"
                       r"(?:\*\*([^\s*]+)\*\*|`([^`\s]+)`|([^\s:,.*`]+))(?=[\s:,.]|$)")


def _first_visit(seen, path) -> bool:
    real = os.path.realpath(path)
    if real in seen:
        return False
    seen.add(real)
    return True


def _raise(exc):
    raise exc


def registered_reports(root) -> tuple:
    """(found, bad): `found` lists the files under REPORT_DIRS with the exact marker line in their
    first MARKER_LINES lines; `bad` lists (file, line) for a file with a near-miss marker line."""
    found, bad = [], []
    seen = set()    # realpaths of directories already walked: a symlink cycle stops here
    for d in REPORT_DIRS:
        top = os.path.join(root, d)
        if not os.path.lexists(top):
            if d in REQUIRED_REPORT_DIRS:
                raise GateError(f"{d}: missing report directory")
            continue    # an optional report directory that does not exist holds no reports
        if not os.path.isdir(top):
            raise GateError(f"{d}: not a directory")
        if not _first_visit(seen, top):
            continue    # this directory was already walked through a symlink
        for dirpath, dirnames, filenames in os.walk(top, onerror=lambda exc: _raise(registers.rel_error(root, exc.filename, exc)), followlinks=True):
            dirnames.sort()
            dirnames[:] = [n for n in dirnames if _first_visit(seen, os.path.join(dirpath, n))]
            for name in sorted(filenames):
                path = os.path.join(dirpath, name)
                registers.require_regular(root, path)
                try:
                    with open(path, encoding="utf-8", errors="replace") as fh:
                        head = [next(fh, "").rstrip("\r\n") for _ in range(MARKER_LINES)]
                except OSError as exc:
                    raise registers.rel_error(root, path, exc) from None
                attempts = [l for l in head if MARKER_ATTEMPT.match(l)]
                wrong = next((l for l in attempts if l != MARKER), None)
                if wrong is not None:
                    bad.append((path, wrong))
                if MARKER in attempts:
                    found.append(path)
    return found, bad


def _unwrap(cell: str) -> str:
    """`**id**` and `id` in backticks are the id."""
    for w in ("**", "`"):
        if len(cell) > 2 * len(w) and cell.startswith(w) and cell.endswith(w):
            return cell[len(w):-len(w)]
    return cell


def report_ids(path, root) -> list:
    """Finding ids a registered report cites: table first cells and leading list-item ids.
    An unreadable file is a GateError worded relative to `root`."""
    ids = []
    registers.require_regular(root, path)
    try:
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        raise registers.rel_error(root, path, exc) from None
    for line in lines:
        s = line.strip()
        if s.startswith("|"):
            cell = _unwrap(s.split("|")[1].strip())
            if registers.FINDING_ID.fullmatch(cell):
                ids.append(cell)
            continue
        m = LIST_ITEM.match(line)
        if m:
            token = m.group(1) or m.group(2) or m.group(3)
            if registers.FINDING_ID.fullmatch(token):
                ids.append(token)
    return ids


def _categories(error: str) -> list:
    """Which categories a finding-schema error belongs to: owner errors (including a missing
    `owner` key) are `owner`; every other error is `schema`."""
    if error.startswith("owner"):
        return ["owner"]
    if error.startswith("keys: missing "):
        names = [n.strip() for n in error[len("keys: missing "):].split(",")]
        return (["owner"] if "owner" in names else []) + (["schema"] if set(names) - {"owner"} else [])
    return ["schema"]


def check(args: list) -> Result:
    ns = gate_args("gate_register").parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    findings = registers.load_findings(root)
    decisions = registers.load_decisions(root)
    r = Result("register")
    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    exists = registers.memo_commit_exists()    # one git call per distinct sha

    seen = set()
    for i, f in enumerate(findings):
        fid = registers.row_id(f, i, registers.FINDING_ID)
        if fid in seen:
            r.fail(f"register.duplicate.{fid}", "id appears more than once")
        seen.add(fid)
        errs = registers.validate_finding(f)
        for e in errs:
            for cat in _categories(e):
                r.fail(f"register.{cat}.{fid}", e)
        if errs:
            skipped.update(["transition", "evidence"])   # lifecycle rules need a well-formed row
            continue
        for e in registers.transition_errors(f):
            r.fail(f"register.transition.{fid}", e)
        for e in registers.evidence_errors(f, root, decisions) + registers.finding_commit_errors(f, root, exists):
            r.fail(f"register.evidence.{fid}", e)
    seen = set()
    for i, d in enumerate(decisions):
        did = registers.row_id(d, i, registers.DECISION_ID)
        if did in seen:
            r.fail(f"register.duplicate.{did}", "id appears more than once")
        seen.add(did)
        errs = registers.validate_decision(d)
        for e in errs:
            r.fail(f"register.schema.{did}", e)
        if errs:
            skipped.add("evidence")
            continue
        for e in registers.decision_commit_errors(d, root, exists):
            r.fail(f"register.evidence.{did}", e)
    known = {f["id"] for f in findings
             if isinstance(f.get("id"), str) and registers.FINDING_ID.fullmatch(f["id"])}
    reports, bad = registered_reports(root)
    for path, line in bad:
        r.fail(f"register.marker.{registers.safe_text(os.path.relpath(path, root))}",
               f"marker must be exactly '{MARKER}' (found {line!r})")
    if bad:
        skipped.add("report")
    for path in reports:
        rel = registers.safe_text(os.path.relpath(path, root))
        for rid in sorted(set(report_ids(path, root)) - known):
            r.fail(f"register.report.{rel}.{rid}", "not in register")
    failed = {l.id.split(".")[1] for l in r.lines}
    for cat in CATEGORIES:
        if cat not in failed and cat not in skipped:
            r.passed(f"register.{cat}")
    return r


def main(argv=None):
    return run_gate("register", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: Run the tests and the gate itself**

```bash
python3 -m unittest tools.gates.tests.test_gate_register -v      # Ran 42 tests ... OK
python3 -m unittest discover -s tools/gates/tests -t .           # Ran 133 tests ... OK
python3 tools/gates/gate_register.py; echo rc=$?                 # needs full history (the registers name `main` commits; a shallow clone FAILs `register.evidence` by design, CI uses `fetch-depth: 0`); 6 PASS lines (docs/reviews has no marked report yet), rc=0
python3 tools/gates/gate_register.py --root tools/gates/tests/fixtures/register/illegal_transition; echo rc=$?   # FAIL register.transition.T1-JUMP-1 ..., rc=1
```

Mutation check (production path): in `registers.evidence_errors`, delete the `elif not _file_under(root, pin["transcript"])` branch for the pin transcript. `test_pinned_without_transcript_fails` must fail. Restore and run again: OK.

Mutation check (report scan): in `registered_reports`, replace `if MARKER in attempts:` with `if True:`. The unmarked fixture's ids are now scanned, and `test_unmarked_report_is_ignored_and_marked_report_with_known_ids_passes`, `test_valid_register_passes_every_category` and `test_the_repository_register_passes` must fail. Restore. Then replace `for rid in sorted(set(report_ids(path, root)) - known):` with `for rid in []:`. `test_registered_report_with_an_unknown_id_fails` must fail. Restore and run again: OK.

- [ ] **Step 6: Ledger line, then commit**

Append `- T3 feat(gates): gate_register -- test_gate_register 42 OK; suite 133 OK; gate_register at HEAD 6 PASS` to the ledger.

```bash
git add tools/gates/gate_register.py tools/gates/tests/test_gate_register.py tools/gates/tests/fixtures docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): gate_register -- findings schema, owners, transitions, evidence, registered-report ids" \
  -m "Strict-delivery S1 Task 3 (spec §4.1, §7; umbrella owner ruling 5). Fixtures: valid (with a marked and an unmarked report), illegal transition, pinned without transcript, missing owner, missing file (FAIL register.input), report_unknown (FAIL register.report.<file>.<id>)."
```

### CHECKPOINT b1

The umbrella's "Checkpoint protocol for this plan", verbatim:

> At every block end: run `python3 tools/gates/run_all.py`; dispatch final reviewer (opus) + final verifier (Fable) with the block's review packages; `gh pr ready`; wait for every check; merge with `gh pr merge --squash` only if `decisions.yml` has `grant:strict-delivery` answered `yes` by the owner; tag `strict-delivery-b<N>` on the squash commit and push the tag; delete branch + worktrees; write `docs/superpowers/state/strict-delivery/handoff.md`; append `CHECKPOINT b<N>` to the ledger; post the status update and the resume prompt; stop.

These steps carry it out by hand for block b1, because `gate_checkpoint` does not exist until b6. The one ordering change is umbrella Owner ruling 2: the handoff and the `CHECKPOINT` line are committed before the merge, and the post-merge facts are verified in step 8. The owner sees this in the status update.

- [ ] **Step 1: Gates at the block tip**

```bash
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/gate_register.py
git status --short     # empty
```

Every line PASS, `OK`. On any FAIL, stop: fix through the task's review loop before going on.

- [ ] **Step 2: Review packages (by hand; `gate_review_package` arrives in S2)**

For each task T in this block, with `B` = the commit before the task and `H` = the task's commit:

```bash
P=.superpowers/sdd/strict-delivery/review/b1/T<n>; mkdir -p "$P"
git log --oneline B..H > "$P/commits.txt"
git diff -U10 B..H > "$P/diff.patch"
{ python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/gate_register.py; } > "$P/gates.txt" 2>&1
```

Add `claims.md` (claim | direct command | contradiction | output excerpt | PASS/FAIL) for every behavioural claim in the task's commit message and ledger line. Add `mutation-<test>.txt` for each mutation check the task names, in the umbrella's three-step format. The package stays under the gitignored `.superpowers/` (Interface note 9).

- [ ] **Step 3: Owner items for this block**

These come before Step 4, which depends on them. Each owner-only question goes into `docs/superpowers/state/decisions.yml` under the next free `DEC-` id, with `asked_at: {sha: "<git rev-parse --short=7 main>", by: session}` (the `main` commit when the row is created: a branch-only sha is orphaned by the squash merge, and `gate_register` fails a sha that is not a commit), `answer: null`, `answered_at: null`. Present it to the owner and stop until they answer. Transcribe the owner's answer as a quoted string with `answered_at: {sha: "<main short>", by: superbrobenji}`, quote the owner's words in the commit message, and run `python3 -m unittest discover -s tools/gates/tests -t .` before each commit.

(a) **DEC-0001 `grant:strict-delivery`.** The owner answered it in chat at setup Step 0.1, and Task 1 recorded the answer. Verify `grep -n -A1 'answer:' docs/superpowers/state/decisions.yml` shows `answer: "yes"` and `by: superbrobenji`. If it shows `null`, ask the owner the Step 0.1 question again and transcribe the answer. b1 cannot merge without `"yes"`, and b2's CI fails while DEC-0001 is unanswered, because the umbrella plan cites it.

(b) Final verifier model: settled by the umbrella's "Owner rulings", ruling 9 (Fable). No question to ask.

(c) Handoff timing: settled by the umbrella's "Owner rulings", ruling 2 (on the block branch, before the merge). No question to ask.

- [ ] **Step 4: Final reviewer and final verifier**

Final verifier model: Fable (umbrella Owner ruling 9). Dispatch the final branch reviewer (opus) and then the final adversarial verifier on the whole branch `BASE_B1..HEAD`. Give each the review packages and these report paths: `.superpowers/sdd/strict-delivery/b1-final-review.md` and `.superpowers/sdd/strict-delivery/b1-final-verify.md`. The prompt-block files do not exist until Task 9. Each dispatch therefore cites, as its strict block, spec §5 dispatch contract item 4, §6 step 6, D8 and D16, plus the Review Focus of the umbrella and of this plan. Both must report `status: DONE` with no finding. Any finding goes through the fix loop (spec §6 step 7), then back to Step 1.

- [ ] **Step 5: Ledger CHECKPOINT line and handoff, committed on the branch**

```bash
TIP=$(git rev-parse --short=7 HEAD)     # the reviewed tip; recorded as `head`
```

Write `docs/superpowers/state/strict-delivery/handoff.md` (YAML front matter per the umbrella "Handoff"; values for this block):

```markdown
---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: main
head: "<TIP>"
base: "<BASE_B1>"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: <line number of this block's CHECKPOINT line in the ledger>
block: 1
next_task: "T4"
findings_in_flight: []
claims_verified_at_head: []
live_subagents: []
open_decisions: [<every DEC id in decisions.yml that the owner has not answered>]
rulings: [<this block's Ruling lines, one each; at most 5>]
grant: DEC-0001
parked_branches: [<every local branch but main: {name, reason, owner}>]
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps: [<one line each, found in this block>]
---

Post-merge facts (verified in CHECKPOINT b1 step 8, not by gate_checkpoint): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.
```

Append to the ledger:

```markdown
CHECKPOINT b1 tip <TIP> -- gates: <the Step 1 summary, e.g. "suite N OK, M PASS">; rulings: <count>; final review + verify: clean
```

```bash
git add docs/superpowers/state/strict-delivery/handoff.md docs/superpowers/state/strict-delivery/ledger.md
git commit -m "docs(state): strict-delivery CHECKPOINT b1 handoff and ledger line"
FINAL=$(git rev-parse HEAD)            # the tree the squash must reproduce
git push -u origin block/strict-delivery-b1
```

- [ ] **Step 6: PR, every check green, grant, merge**

```bash
gh pr create --draft --base main --head block/strict-delivery-b1 --title "strict-delivery b1: gate library, registers, gate_register" --body-file .superpowers/sdd/strict-delivery/b1-pr-body.md
gh pr ready block/strict-delivery-b1
gh pr checks block/strict-delivery-b1 --watch          # every job, advisory (coverage) included, must pass
python3 -c "import sys; sys.path.insert(0, '.'); from tools.gates.lib import registers as r; d = r.grant_for(r.load_decisions('.'), 'strict-delivery'); print('GRANT', d['id']) if d else sys.exit('NO GRANT: stop, the owner answers DEC-0001 first')"
gh pr merge block/strict-delivery-b1 --squash
```

The PR body (`.superpowers/sdd/strict-delivery/b1-pr-body.md`) lists the tasks, the gate output from Step 1, both final report summaries, the owner items from Step 3 with their state, and Interface notes 8–10 where they apply. It contains none of `fix before merge`, `draft`, `parked`, `TODO`, `not for merge` (config `merge_markers`). If any check is red or the grant line says `NO GRANT`, stop. Never merge red, never recommend it.

- [ ] **Step 7: Tag and clean up**

```bash
git switch main && git pull --ff-only
SQUASH=$(git rev-parse HEAD)
git log -1 --format=%s "$SQUASH"                  # the PR title
git tag strict-delivery-b1 "$SQUASH" && git push origin strict-delivery-b1
git push origin --delete block/strict-delivery-b1 2>/dev/null || true   # gone already if GitHub auto-deleted it
git branch -D block/strict-delivery-b1
git worktree list                                  # main checkout only; else `git worktree remove <path>` each
```

- [ ] **Step 8: Verify the clean state and the post-merge facts**

```bash
test "$(git rev-parse --short=7 "$SQUASH^")" = "<BASE_B1>" && echo parent-ok   # main moved by exactly this squash
git diff --quiet "$FINAL" "$SQUASH" && echo tree-ok                            # squash == the reviewed branch
git status --porcelain | wc -l                                                 # 0
git branch --format='%(refname:short)'                                         # main, plus only the parked branches in the handoff
gh pr list --head block/strict-delivery-b1 --state open                           # empty
git tag --points-at "$SQUASH"                                                  # strict-delivery-b1
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/gate_register.py
```

Every line as stated. Otherwise the checkpoint is not reached: fix it, and do not post.

- [ ] **Step 9: Status update and resume prompt, then stop**

Post to the owner: done since the last checkpoint (task ids, commits, gate results at main); what remains in S1 and in the umbrella (S2–S5); open decisions; rulings made in this block; parked items; the owner items from Step 3. Then post the resume prompt verbatim and stop. Do not start the next block. The owner runs `/clear`.

```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/gate_register.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 2 (sub-plan S1, task 4) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

This resume prompt departs from the umbrella template twice, because two things it names do not exist yet. `run_all.py` arrives in Task 6, so the prompt names the gates that exist at b1. The skill arrives in Task 9: until then, "invoke the strict-delivery skill" means reading this plan's Global Constraints and Interface notes. Tell the owner both.

---

# Block b2 — `gate_claims`, `gate_decisions`, `run_all`, CI job `gates` (PR 2, tag `strict-delivery-b2`)

### Block b2 setup

- [ ] **Step 1: Branch from the merged main and run the installed gate**

```bash
git switch main && git pull --ff-only
git switch -c block/strict-delivery-b2
git rev-parse --short=7 HEAD     # BASE_B2
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/gate_register.py
grep -n 'answer:' docs/superpowers/state/decisions.yml   # DEC-0001 answer: "yes" (else stop: b1 merged without a grant)
```

### Task 4: `gate_claims`

**Files:**
- Create: `tools/gates/gate_claims.py`
- Test: `tools/gates/tests/test_gate_claims.py`

**Interfaces:**
- Consumes: `GateError`, `Result`, `gate_args`, `repo_root`, `run_gate`; `gitinfo.changed_files`, `staged_files`, `head_sha`; `registers.load_claims`, `load_findings`, `validate_claim`, `claim_commit_errors`, `finding_by_id`, `row_id`, `memo_commit_exists`, `CLAIM_ID`, `STATE_DIR`.
- Produces: `gate_claims.check(args) -> Result` with args `--root <tree>`, `--head <sha>` (default HEAD of root; an empty or unknown `--head` is `FAIL claims.input`), `--mark-stale --base <sha>` (refuses to rewrite claims.yml, `FAIL claims.input`, unless every row validates and no id repeats; the rewrite drops YAML comments); `main(argv=None)`; `hits(cites, changed) -> list[str]`; `stale_reason(root, claim, head, staged) -> str` (`""` = holds); `verified_at_head(root, claim, head) -> bool` (for S2 `gate_dispatch`). Line ids: `claims.{schema|duplicate|false|stale}.<claim id>` on FAIL (`entry-<i>` when the id is not a well-formed claim id; a `verified_at` that is not a commit of the gated repository is `claims.stale.<id>`, DEC-0003); an empty `--root` is `FAIL claims.input`; `PASS claims.marked-stale.<id>` or `PASS claims.marked-stale.none` in mark mode; one `PASS claims.<category>` per clean category that every row reached (schema, duplicate, false, stale; a schema-invalid row skips false and stale).

- [ ] **Step 1: Write the failing tests**

`tools/gates/tests/test_gate_claims.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/gate_claims.py through main() on throwaway git repos (the production
path: real git history, real staged files). Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import unittest
from unittest import mock

from tools.gates import gate_claims
from tools.gates.lib import gitinfo, registers
from tools.gates.tests import gitfixture as gf

CLAIMS = os.path.join(gf.STATE, "claims.yml")
ROW = """- id: C-demo-1
  doc: docs/spec.md
  anchor: "§1"
  text: "a.c returns 1"
  direct: "grep -n 'return 1' src/a.c"
  contra: "if a.c returned 0 the grep finds nothing"
  cites: [{cites}]
  verified_at: {verified_at}
  by: {by}
  state: {state}
  finding: {finding}
"""


def row(state="verified", verified_at="null", by="reviewer", finding="null", cites="src/a.c"):
    return ROW.format(state=state, verified_at=verified_at, by=by, finding=finding, cites=cites)


class ClaimsRepo(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        gf.write(self.root, "src/a.c", "int a(void) { return 1; }\n")
        gf.write(self.root, "src/b.c", "int b;\n")
        self.v = gf.commit_all(self.root, "sources")

    def tearDown(self):
        gf.remove(self.root)

    def claims(self, text):
        gf.write(self.root, CLAIMS, text)
        return gf.commit_all(self.root, "claims")

    def run_gate(self, *extra):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_claims.main(["--root", self.root, *extra])
        return rc, buf.getvalue()

    def test_empty_register_passes(self):
        rc, out = self.run_gate()
        self.assertEqual("PASS claims.schema\nPASS claims.duplicate\nPASS claims.false\nPASS claims.stale\n", out)
        self.assertEqual(0, rc)

    def test_verified_row_with_untouched_cites_passes(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        gf.write(self.root, "src/b.c", "int b2;\n")       # not cited
        gf.commit_all(self.root)
        rc, out = self.run_gate()
        self.assertEqual(0, rc, out)

    def test_touching_cited_file_marks_stale(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        gf.write(self.root, "src/a.c", "int a(void) { return 0; }\n")
        gf.commit_all(self.root, "touch the cited file")
        rc, out = self.run_gate()
        self.assertIn(f"FAIL claims.stale.C-demo-1: cited file(s) changed since verified_at {self.v}: src/a.c", out)
        self.assertEqual(1, rc)
        rc, out = self.run_gate("--mark-stale", "--base", self.v)
        self.assertIn("PASS claims.marked-stale.C-demo-1", out)
        self.assertEqual(0, rc, out)
        self.assertEqual("stale", registers.load_claims(self.root)[0]["state"])
        self.assertEqual(self.v, registers.load_claims(self.root)[0]["verified_at"])

    def test_staged_change_to_a_cited_file_fails_before_the_commit(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        gf.write(self.root, "src/a.c", "int a(void) { return 2; }\n")
        gf.git(self.root, "add", "src/a.c")
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.stale.C-demo-1", out)
        self.assertEqual(1, rc)
        rc, out = self.run_gate("--mark-stale", "--base", "HEAD")
        self.assertIn("PASS claims.marked-stale.C-demo-1", out)
        self.assertEqual(0, rc, out)

    def test_directory_cite_covers_its_files(self):
        self.claims(row(verified_at=f'"{self.v}"', cites="src/"))
        gf.write(self.root, "src/b.c", "int b3;\n")
        gf.commit_all(self.root)
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.stale.C-demo-1: cited file(s) changed since verified_at", out)
        self.assertEqual(1, rc)

    def test_verified_at_unknown_commit_fails(self):
        for state in ("verified", "stale"):
            self.claims(row(state=state, verified_at='"1234abc"'))
            rc, out = self.run_gate()
            self.assertIn("FAIL claims.stale.C-demo-1: verified_at: sha 1234abc is not a commit in this repository", out)
            self.assertEqual(1, rc)

    def test_mark_stale_leaves_other_rows_alone(self):
        self.claims(row(verified_at=f'"{self.v}"', cites="src/b.c")
                    + row(state="unverified", verified_at="null", by="null").replace("C-demo-1", "C-demo-2"))
        gf.write(self.root, "src/a.c", "int a(void) { return 5; }\n")
        gf.commit_all(self.root)
        path = os.path.join(self.root, CLAIMS)
        with open(path, "rb") as fh:
            before = fh.read()
        rc, out = self.run_gate("--mark-stale", "--base", self.v)
        self.assertIn("PASS claims.marked-stale.none", out)
        with open(path, "rb") as fh:
            self.assertEqual(before, fh.read())
        self.assertEqual(["verified", "unverified"], [c["state"] for c in registers.load_claims(self.root)])
        self.assertEqual(0, rc, out)

    def test_mark_stale_without_base_is_fail_input(self):
        rc, out = self.run_gate("--mark-stale")
        self.assertEqual("FAIL claims.input: --mark-stale needs --base <sha>\n", out)
        self.assertEqual(1, rc)

    def test_false_row_needs_a_registered_finding(self):
        self.claims(row(state='"false"', verified_at="null", by="null", finding="A2-IO-N4"))
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.false.C-demo-1: finding A2-IO-N4 is not in findings.yml", out)
        self.assertEqual(1, rc)

    def test_false_row_without_finding_is_schema_fail(self):
        self.claims(row(state='"false"', verified_at="null", by="null"))
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.schema.C-demo-1: finding: a false claim must name the finding id", out)
        self.assertEqual(1, rc)

    def test_unquoted_false_state_is_named(self):
        self.claims(row(state="false", verified_at="null", by="null", finding="A2-IO-N4"))
        rc, out = self.run_gate()
        self.assertIn('FAIL claims.schema.C-demo-1: state: YAML read false as a boolean; quote it: state: "false"', out)
        self.assertEqual(1, rc)

    def test_duplicate_ids_fail(self):
        self.claims(row(state="unverified", verified_at="null", by="null") * 2)
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.duplicate.C-demo-1", out)
        self.assertEqual(1, rc)

    def test_missing_register_is_fail_input(self):
        os.remove(os.path.join(self.root, CLAIMS))
        rc, out = self.run_gate()
        self.assertTrue(out.startswith("FAIL claims.input: "), out)
        self.assertEqual(1, rc)

    def test_a_malformed_id_prints_as_entry_index(self):
        forged = row(state="unverified", verified_at="null", by="null").replace(
            "id: C-demo-1", 'id: "C-x\\nFAIL claims.forged.C-x"')
        numeric = row(state="unverified", verified_at="null", by="null").replace("id: C-demo-1", "id: 5")
        self.claims(forged + numeric)
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.schema.entry-0: id: ", out)
        self.assertIn("FAIL claims.schema.entry-1: id: ", out)
        self.assertFalse([l for l in out.splitlines() if l.startswith("FAIL claims.forged")], out)
        self.assertEqual(1, rc)

    def test_an_empty_root_is_a_fail_not_the_cwd_repo(self):
        for root in ("", "  ", "\t"):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_claims.main(["--root", root])
            self.assertEqual((1, "FAIL claims.input: --root is empty\n"), (rc, buf.getvalue()))

    def test_categories_whose_rows_were_skipped_never_pass(self):
        self.claims(row(state='"false"', verified_at="null", by="null"))     # no finding: schema error
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.schema.C-demo-1", out)
        for cat in ("schema", "false", "stale"):
            self.assertNotIn(f"PASS claims.{cat}", out)
        self.assertIn("PASS claims.duplicate", out)
        self.assertEqual(1, rc)

    def test_one_commit_lookup_per_distinct_sha(self):
        self.claims(row(verified_at=f'"{self.v}"') + row(verified_at=f'"{self.v}"').replace("C-demo-1", "C-demo-2"))
        calls = []
        real = gitinfo.commit_exists

        def counting(root, sha):
            calls.append(sha)
            return real(root, sha)

        with mock.patch.object(gitinfo, "commit_exists", counting):
            rc, out = self.run_gate()
        self.assertEqual(0, rc, out)
        self.assertEqual([self.v], calls)

    def test_a_false_row_with_an_unknown_commit_still_needs_its_finding(self):
        self.claims(row(state='"false"', verified_at='"1234abc"', by="null", finding="A2-IO-N4"))
        rc, out = self.run_gate()
        self.assertIn("FAIL claims.false.C-demo-1: finding A2-IO-N4 is not in findings.yml", out)
        self.assertIn("FAIL claims.stale.C-demo-1: verified_at: sha 1234abc is not a commit", out)
        self.assertNotIn("PASS claims.false", out)
        self.assertEqual(1, rc)

    def test_a_bad_head_is_an_input_failure(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        for head in ("", "  "):
            self.assertEqual((1, "FAIL claims.input: --head is empty\n"), self.run_gate("--head", head))
            self.assertEqual((1, "FAIL claims.input: --head is empty\n"),
                             self.run_gate("--head", head, "--mark-stale", "--base", self.v))
        rc, out = self.run_gate("--head", "deadbee")
        self.assertEqual((1, "FAIL claims.input: --head deadbee is not a commit in this repository\n"), (rc, out))
        head = gf.git(self.root, "rev-parse", "HEAD")
        rc, out = self.run_gate("--head", head)
        self.assertEqual(0, rc, out)

    def test_mark_stale_refuses_to_rewrite_an_invalid_register(self):
        bad = row(state="unverified", verified_at="null", by="null").replace("C-demo-1", "C-demo-2").replace(
            'text: "a.c returns 1"', "text: 5")
        self.claims(row(verified_at=f'"{self.v}"') + bad)
        gf.write(self.root, "src/a.c", "int a(void) { return 3; }\n")
        gf.commit_all(self.root)
        path = os.path.join(self.root, CLAIMS)
        with open(path, "rb") as fh:
            before = fh.read()
        rc, out = self.run_gate("--mark-stale", "--base", self.v)
        self.assertTrue(out.startswith("FAIL claims.input: refusing to rewrite "), out)
        self.assertIn("C-demo-2", out)
        self.assertEqual(1, rc)
        with open(path, "rb") as fh:
            self.assertEqual(before, fh.read())
        self.claims(row(verified_at=f'"{self.v}"') * 2)
        rc, out = self.run_gate("--mark-stale", "--base", self.v)
        self.assertIn("C-demo-1 (duplicate id)", out)
        self.assertEqual(1, rc)

    def test_an_unknown_verified_at_is_not_comparable(self):
        # main() rejects such a row first (claims.stale), so the defensive branch is reached
        # through the helper dispatch uses
        self.claims(row(verified_at=f'"{self.v}"'))
        c = dict(registers.load_claims(self.root)[0], verified_at="1234abc")
        head = gf.git(self.root, "rev-parse", "HEAD")
        self.assertIn("verified_at 1234abc is not comparable with", gate_claims.stale_reason(self.root, c, head, []))

    def test_verified_at_head_sees_a_staged_change(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        c = registers.load_claims(self.root)[0]
        head = gf.git(self.root, "rev-parse", "HEAD")
        self.assertTrue(gate_claims.verified_at_head(self.root, c, head))
        gf.write(self.root, "src/a.c", "int a(void) { return 4; }\n")
        gf.git(self.root, "add", "src/a.c")
        self.assertFalse(gate_claims.verified_at_head(self.root, c, head))

    def test_verified_at_head_for_dispatch(self):
        self.claims(row(verified_at=f'"{self.v}"'))
        c = registers.load_claims(self.root)[0]
        head = gf.git(self.root, "rev-parse", "HEAD")
        self.assertTrue(gate_claims.verified_at_head(self.root, c, head))
        gf.write(self.root, "src/a.c", "int a(void) { return 9; }\n")
        head = gf.commit_all(self.root)
        self.assertFalse(gate_claims.verified_at_head(self.root, c, head))
        self.assertFalse(gate_claims.verified_at_head(self.root, dict(c, state="unverified"), head))


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_claims -v`
Expected: ERROR, `ImportError: cannot import name 'gate_claims' from 'tools.gates'`.

- [ ] **Step 3: Write the gate**

`tools/gates/gate_claims.py`:

```python
#!/usr/bin/env python3
"""gate_claims -- the claim register (spec §4.2, §7).

  python3 tools/gates/gate_claims.py [--json] [--root <tree>] [--head <sha>]
  python3 tools/gates/gate_claims.py --mark-stale --base <sha> [--root <tree>]

A row is "verified at HEAD" when its state is `verified` and no file it cites differs between
`verified_at` and HEAD, nor is staged for the next commit. (`verified_at` cannot literally equal
the HEAD that contains the row: a commit cannot hold its own sha.) A cite ending in "/" covers
every file under that directory. --mark-stale rewrites every `verified` row whose cites
intersect changed_files(base, HEAD) + the staged files to `stale`, then checks the result. It
rewrites claims.yml with yaml.safe_dump, so YAML comments and hand formatting are lost, and it
refuses to write (FAIL claims.input) unless every row validates and has no duplicate id. --head
must name a commit of the gated repository (an empty or unknown --head is FAIL claims.input). Every
verified_at names a commit of the gated repository (DEC-0003); a row that does not is a FAIL
claims.stale.<id>. Row ids print forgery-safe (entry-<i> when the id is malformed), an empty
--root is an error, and a category some row never reached prints no PASS line.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import yaml  # noqa: E402

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

CATEGORIES = ["schema", "duplicate", "false", "stale"]


def hits(cites: list, changed) -> list:
    """The changed paths a claim's cites cover (exact path, or under a cite ending in '/')."""
    return sorted(p for p in set(changed)
                  if any(p == c or (c.endswith("/") and p.startswith(c)) for c in cites))


def stale_reason(root, claim: dict, head: str, staged) -> str:
    """'' when a verified claim still holds at head, else why it does not."""
    try:
        changed = gitinfo.changed_files(root, claim["verified_at"], head)
    except GateError as exc:
        return f"verified_at {claim['verified_at']} is not comparable with {head}: {exc}"
    hit = hits(claim["cites"], list(changed) + list(staged))
    if hit:
        return (f"cited file(s) changed since verified_at {claim['verified_at']}: {', '.join(hit)}; "
                f"re-verify the row or run gate_claims.py --mark-stale --base {claim['verified_at']}")
    return ""


def verified_at_head(root, claim: dict, head: str) -> bool:
    """For gate_dispatch (S2): the row is well-formed, `verified`, and not stale at head."""
    if registers.validate_claim(claim) or claim["state"] != "verified":
        return False
    return stale_reason(root, claim, head, gitinfo.staged_files(root)) == ""


def _claims_path(root) -> str:
    return os.path.join(root, registers.STATE_DIR, "claims.yml")


def mark_stale(root, claims: list, base: str, head: str, r: Result, exists) -> None:
    ids = [registers.row_id(c, i, registers.CLAIM_ID) for i, c in enumerate(claims)]
    bad = [ids[i] for i, c in enumerate(claims)
           if registers.validate_claim(c) or registers.claim_commit_errors(c, root, exists)]
    bad += [f"{n} (duplicate id)" for n in sorted({n for n in ids if ids.count(n) > 1})]
    if bad:
        raise GateError(f"refusing to rewrite {registers.STATE_DIR}/claims.yml: invalid or duplicate row(s) "
                        f"{', '.join(bad)}; fix them first (run without --mark-stale to see the failures)")
    changed = list(gitinfo.changed_files(root, base, head)) + gitinfo.staged_files(root)
    marked = 0
    for c in claims:
        if c.get("state") == "verified" and not registers.validate_claim(c) and hits(c["cites"], changed):
            c["state"] = "stale"
            marked += 1
            r.passed(f"claims.marked-stale.{c.get('id')}")
    if marked:
        with open(_claims_path(root), "w", encoding="utf-8") as fh:
            yaml.safe_dump(claims, fh, sort_keys=False, allow_unicode=True, width=100000)
    else:
        r.passed("claims.marked-stale.none")


def check(args: list) -> Result:
    ap = gate_args("gate_claims")
    ap.add_argument("--head", default=None, help="sha to check against (default: HEAD of --root)")
    ap.add_argument("--mark-stale", action="store_true",
                    help="rewrite stale `verified` rows as `stale` (drops YAML comments in claims.yml)")
    ap.add_argument("--base", default=None)
    ns = ap.parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    claims = registers.load_claims(root)
    findings = registers.load_findings(root)
    r = Result("claims")
    exists = registers.memo_commit_exists()    # one git call per distinct sha

    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    head = ns.head
    if head is not None:
        if not head.strip():
            raise GateError("--head is empty")
        if not exists(root, head):
            raise GateError(f"--head {head} is not a commit in this repository")
    if ns.mark_stale:
        if not ns.base:
            raise GateError("--mark-stale needs --base <sha>")
        head = head or gitinfo.head_sha(root)
        mark_stale(root, claims, ns.base, head, r, exists)
    staged = None
    seen = set()
    for i, c in enumerate(claims):
        cid = registers.row_id(c, i, registers.CLAIM_ID)
        if cid in seen:
            r.fail(f"claims.duplicate.{cid}", "id appears more than once")
        seen.add(cid)
        errs = registers.validate_claim(c)
        for e in errs:
            r.fail(f"claims.schema.{cid}", e)
        if errs:
            skipped.update(["false", "stale"])    # the false and stale rules need a well-formed row
            continue
        if c["state"] == "false" and registers.finding_by_id(findings, c["finding"]) is None:
            r.fail(f"claims.false.{cid}", f"finding {c['finding']} is not in findings.yml")
        bad_commit = registers.claim_commit_errors(c, root, exists)
        for e in bad_commit:
            r.fail(f"claims.stale.{cid}", e)
        if bad_commit:
            continue    # nothing to compare against
        if c["state"] == "verified":
            if head is None:
                head = gitinfo.head_sha(root)
            if staged is None:
                staged = gitinfo.staged_files(root)
            why = stale_reason(root, c, head, staged)
            if why:
                r.fail(f"claims.stale.{cid}", why)
    failed = {l.id.split(".")[1] for l in r.lines if l.status == "FAIL"}
    for cat in CATEGORIES:
        if cat not in failed and cat not in skipped:
            r.passed(f"claims.{cat}")
    return r


def main(argv=None):
    return run_gate("claims", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
python3 -m unittest tools.gates.tests.test_gate_claims -v   # Ran 23 tests ... OK
python3 tools/gates/gate_claims.py; echo rc=$?              # 4 PASS lines (0 claims), rc=0
```

Mutation check: in `check`, change `staged = gitinfo.staged_files(root)` to `staged = []`. `test_staged_change_to_a_cited_file_fails_before_the_commit` must fail. Restore. Then in `hits` drop the `c.endswith("/")` clause. `test_directory_cite_covers_its_files` must fail. Restore. Then remove the `claim_commit_errors` call (`test_verified_at_unknown_commit_fails` and `test_one_commit_lookup_per_distinct_sha` fail), drop `and cat not in skipped` (`test_categories_whose_rows_were_skipped_never_pass` fails), un-memoise `exists` (`test_one_commit_lookup_per_distinct_sha` fails), drop the empty-root check (`test_an_empty_root_is_a_fail_not_the_cwd_repo` fails) and make `registers.row_id` return the raw id (`test_a_malformed_id_prints_as_entry_index` fails). Restore each and run again: OK. Then for the false rule moved before the commit check (`test_a_false_row_with_an_unknown_commit_still_needs_its_finding`), the `--head` checks (`test_a_bad_head_is_an_input_failure`), the mark-stale refusal and its duplicate-id clause (`test_mark_stale_refuses_to_rewrite_an_invalid_register`), the `except GateError` in `stale_reason` (`test_an_unknown_verified_at_is_not_comparable`) and `registers.memo_commit_exists` / `row_id` (`test_registers` and `test_gate_register` tests): remove each and confirm a test fails.

- [ ] **Step 5: Ledger line, then commit**

Append `- T4 feat(gates): gate_claims -- test_gate_claims 23 OK` to the end of `docs/superpowers/state/strict-delivery/ledger.md` (the `## Block b2 (branch block/strict-delivery-b2)` heading and its base line already exist).

```bash
git add tools/gates/gate_claims.py tools/gates/tests/test_gate_claims.py docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): gate_claims -- claim schema, stale-at-HEAD detection, --mark-stale" \
  -m "Strict-delivery S1 Task 4 (spec §4.2, §7). A verified row holds while no cited path changed since verified_at and none is staged (a commit cannot carry its own sha, so verified_at == HEAD is read this way); false rows need a registered finding."
```

### Task 5: `gate_decisions`

**Files:**
- Create: `tools/gates/gate_decisions.py`
- Test: `tools/gates/tests/test_gate_decisions.py`

**Interfaces:**
- Consumes: `CODE_ROOT`, `GateError`, `Result`, `gate_args`, `repo_root`, `run_gate`; `config.load`; `registers.load_decisions`, `validate_decision(d, owner=owner)`, `decision_commit_errors`, `open_decisions`, `row_id`, `memo_commit_exists`, `PathError`, `require_regular`, `safe_text`, `OWNER_ANSWER_ERROR`, `DECISION_ID`, `DECISION_CLASSES`.
- Produces: `gate_decisions.check(args) -> Result` with args `--root <tree>` (an empty one is `FAIL decisions.input: --root is empty`), `--scan <path> ...` (relative paths resolve against root); `main(argv=None)`; `OWNER_ONLY_CLASSES: list[str]` (= all eight classes, spec §5); `REF = re.compile(r"DEC-\d{4}(?!\d)")`. Line ids: `decisions.{schema|duplicate|owner|open-ref|scan}.<id or path>` on FAIL, the row id being `entry-<i>` when the id is not a well-formed str (forgery-safe); `decisions.owner.<id>` carries `validate_decision`'s own `answered_at.by: only the owner (...)` error, every other schema error and every `decision_commit_errors` error (DEC-0003) is `decisions.schema.<id>`. One `PASS decisions.<category>` per category (schema, duplicate, owner, open-ref, scan) that failed nowhere and that every row reached: a row with an error other than the owner one skips `schema`, `owner` and `open-ref` (no PASS line for those); a row whose only error is the owner one still gets its sha check (`decisions.schema`). Only the owner's answer is an answer: the unanswered ids (open references) are `registers.open_decisions(decisions, owner=owner)`, filtered to well-formed DECISION_IDs. A `--scan` path that is not a regular file (a directory, a FIFO) is `FAIL decisions.scan.<rel>: not a regular file`; line numbers count `\n` only. One `commit_exists` call per distinct sha (`memo_commit_exists`). With no `--scan` path (absent, or `--scan` alone) nothing is scanned, so `scan` and `open-ref` print no PASS line (the other categories still do).
- [ ] **Step 1: Write the failing tests**

`tools/gates/tests/test_gate_decisions.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/gate_decisions.py through main() on throwaway trees.
Run from the repo root: python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import os
import signal
import unittest
from unittest import mock

from tools.gates import gate_decisions
from tools.gates.lib import gitinfo, registers
from tools.gates.tests import gitfixture as gf

DECISIONS = os.path.join(gf.STATE, "decisions.yml")
OPEN = """- id: DEC-9001
  class: triage
  question: "accept bucket A or widen it?"
  options: [{label: accept, cost: "8 tasks"}, {label: widen, cost: "18 tasks"}]
  asked_at: {sha: "SHA", by: session}
  answer: null
  answered_at: null
"""
ANSWERED = """- id: DEC-9002
  class: merge-grant
  question: "grant:demo"
  options: []
  asked_at: {sha: "SHA", by: session}
  answer: "yes"
  answered_at: {sha: "SHA", by: BY}
"""
PASS_ALL = ("PASS decisions.schema\nPASS decisions.duplicate\nPASS decisions.owner\n"
            "PASS decisions.open-ref\nPASS decisions.scan\n")


class GateDecisions(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        self.sha = gf.git(self.root, "rev-parse", "HEAD")

    def tearDown(self):
        gf.remove(self.root)

    def run_gate(self, decisions, *extra):
        gf.write(self.root, DECISIONS, decisions.replace("SHA", self.sha))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_decisions.main(["--root", self.root, *extra])
        return rc, buf.getvalue()

    def test_open_and_owner_answered_decisions_pass(self):
        gf.write(self.root, "docs/plan.md", "# plan\n\nnothing to wait on\n")
        rc, out = self.run_gate(OPEN + ANSWERED.replace("BY", "superbrobenji"), "--scan", "docs/plan.md")
        self.assertEqual(PASS_ALL, out)
        self.assertEqual(0, rc)

    def test_without_a_scan_list_no_scan_or_open_ref_line_passes(self):
        register = OPEN + ANSWERED.replace("BY", "superbrobenji")
        for extra in ((), ("--scan",)):    # no --scan at all, then --scan with no path
            rc, out = self.run_gate(register, *extra)
            self.assertEqual("PASS decisions.schema\nPASS decisions.duplicate\nPASS decisions.owner\n",
                             out, extra)
            self.assertNotIn("decisions.scan", out)
            self.assertNotIn("decisions.open-ref", out)
            self.assertEqual(0, rc)

    def test_an_agent_answer_fails(self):
        rc, out = self.run_gate(ANSWERED.replace("BY", "session"))
        self.assertIn("FAIL decisions.owner.DEC-9002: answered_at.by: only the owner (superbrobenji) "
                      "answers a decision, got 'session'", out)
        self.assertNotIn("FAIL decisions.schema", out)
        self.assertNotIn("PASS decisions.owner", out)
        self.assertEqual(1, rc)

    def test_every_class_is_owner_only(self):
        self.assertEqual(registers.DECISION_CLASSES, gate_decisions.OWNER_ONLY_CLASSES)
        for cls in gate_decisions.OWNER_ONLY_CLASSES:
            text = ANSWERED.replace("BY", "controller").replace("merge-grant", cls)
            if cls != "merge-grant":
                text = text.replace("options: []", "options: [{label: a, cost: x}, {label: b, cost: y}]") \
                           .replace('"grant:demo"', '"q?"')
            rc, out = self.run_gate(text)
            self.assertEqual(1, rc, cls)
            self.assertIn("FAIL decisions.owner.DEC-9002: answered_at.by: only the owner", out, cls)
            self.assertNotIn("PASS decisions.owner", out, cls)
            self.assertNotIn("FAIL decisions.schema", out, cls)

    def test_reference_to_an_unanswered_decision_fails(self):
        gf.write(self.root, "docs/plan.md", "# plan\n\nBlocked on DEC-9001 (triage).\nDEC-9002 is fine.\n")
        rc, out = self.run_gate(OPEN + ANSWERED.replace("BY", "superbrobenji"), "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/plan.md:3 references unanswered decision DEC-9001", out)
        self.assertNotIn("DEC-9002", out)
        self.assertNotIn("PASS decisions.open-ref", out)
        self.assertEqual(1, rc)

    def test_a_longer_id_is_not_a_reference_to_a_shorter_one(self):
        gf.write(self.root, "docs/plan.md", "See DEC-90012 for the long form.\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertEqual(0, rc, out)

    def test_unknown_ids_are_not_open_references(self):
        gf.write(self.root, "docs/plan.md", "Example id DEC-9999 in prose.\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertEqual(0, rc, out)

    def test_a_missing_scan_file_fails(self):
        rc, out = self.run_gate(OPEN, "--scan", "docs/absent.md")
        self.assertIn("FAIL decisions.scan.docs/absent.md: cannot read", out)
        self.assertNotIn("PASS decisions.scan", out)
        self.assertEqual(1, rc)

    def test_a_directory_and_a_non_utf8_scan_file_fail(self):
        gf.write(self.root, "docs/dir/x.md", "x\n")
        with open(os.path.join(self.root, "docs", "bin.md"), "wb") as fh:
            fh.write(b"\xff\xfe DEC-9001\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/dir", "docs/bin.md")
        self.assertIn("FAIL decisions.scan.docs/dir: not a regular file", out)
        self.assertIn("FAIL decisions.scan.docs/bin.md: cannot read: not valid UTF-8", out)
        self.assertNotIn("PASS decisions.scan", out)
        self.assertEqual(1, rc)

    def test_a_fifo_scan_path_fails_instead_of_hanging(self):
        if not hasattr(os, "mkfifo"):
            self.skipTest("os.mkfifo is not available")
        os.makedirs(os.path.join(self.root, "docs"), exist_ok=True)
        os.mkfifo(os.path.join(self.root, "docs", "pipe.md"))
        old = signal.signal(signal.SIGALRM, lambda *a: (_ for _ in ()).throw(AssertionError("gate hung on a FIFO")))
        signal.alarm(20)
        try:
            rc, out = self.run_gate(OPEN, "--scan", "docs/pipe.md")
        finally:
            signal.alarm(0)
            signal.signal(signal.SIGALRM, old)
        self.assertIn("FAIL decisions.scan.docs/pipe.md: not a regular file", out)
        self.assertNotIn("PASS decisions.scan", out)
        self.assertEqual(1, rc)

    def test_an_agent_answered_decision_is_still_unanswered_for_open_refs(self):
        agent = ANSWERED.replace("BY", "session").replace("DEC-9002", "DEC-9003")
        gf.write(self.root, "docs/plan.md", "Proceeding per DEC-9003.\n")
        rc, out = self.run_gate(agent, "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.owner.DEC-9003", out)
        self.assertIn("FAIL decisions.open-ref.DEC-9003: docs/plan.md:1 references unanswered decision DEC-9003", out)
        self.assertNotIn("PASS decisions.open-ref", out)
        self.assertEqual(1, rc)

    def test_an_agent_answer_with_an_unknown_commit_fails_owner_and_schema(self):
        text = ANSWERED.replace("BY", "session").replace('answered_at: {sha: "SHA"', 'answered_at: {sha: "1234abc"')
        rc, out = self.run_gate(text)
        self.assertIn("FAIL decisions.owner.DEC-9002", out)
        self.assertIn("FAIL decisions.schema.DEC-9002: answered_at.sha: sha 1234abc is not a commit in this "
                      "repository", out)
        self.assertEqual(1, rc)

    def test_line_numbers_count_newlines_only(self):
        gf.write(self.root, "docs/plan.md", "intro\n\x0c page break\u2028 DEC-9001 here\nlast\n")
        rc, out = self.run_gate(OPEN, "--scan", "docs/plan.md")
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/plan.md:2 references", out)
        self.assertEqual(1, rc)

    def test_a_scan_path_cannot_forge_a_result_line(self):
        rc, out = self.run_gate(OPEN, "--scan", "x\nPASS decisions.forged")
        self.assertFalse([l for l in out.splitlines() if l.startswith("PASS decisions.forged")], out)
        self.assertEqual(1, rc)

    def test_schema_errors_and_duplicates_fail(self):
        rc, out = self.run_gate(OPEN + OPEN.replace("answer: null", "answer: yes"))
        self.assertIn("FAIL decisions.duplicate.DEC-9001", out)
        self.assertIn("quote it", out)
        self.assertEqual(1, rc)

    def test_a_schema_error_skips_the_categories_it_never_reached(self):
        rc, out = self.run_gate(OPEN.replace("answer: null", "answer: yes"))
        self.assertIn("FAIL decisions.schema.DEC-9001", out)
        for cat in ("schema", "owner", "open-ref"):
            self.assertNotIn(f"PASS decisions.{cat}", out)
        self.assertIn("PASS decisions.duplicate", out)
        self.assertEqual(1, rc)

    def test_an_unknown_commit_is_a_schema_failure(self):
        for field in ("asked_at", "answered_at"):
            text = ANSWERED.replace("BY", "superbrobenji")
            text = text.replace(f"{field}: {{sha: \"SHA\"", f"{field}: {{sha: \"1234abc\"")
            rc, out = self.run_gate(text)
            self.assertIn(f"FAIL decisions.schema.DEC-9002: {field}.sha: sha 1234abc is not a commit "
                          "in this repository", out, field)
            self.assertEqual(1, rc, field)

    def test_a_malformed_id_prints_as_entry_index(self):
        forged = OPEN.replace("id: DEC-9001", 'id: "DEC-9\\nFAIL decisions.forged.x"')
        numeric = OPEN.replace("id: DEC-9001", "id: 5")
        rc, out = self.run_gate(forged + numeric)
        self.assertIn("FAIL decisions.schema.entry-0: id: ", out)
        self.assertIn("FAIL decisions.schema.entry-1: id: ", out)
        self.assertFalse([l for l in out.splitlines() if l.startswith("FAIL decisions.forged")], out)
        self.assertEqual(1, rc)

    def test_an_empty_root_is_a_fail_not_the_cwd_repo(self):
        for root in ("", "  ", "\t"):
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = gate_decisions.main(["--root", root])
            self.assertEqual((1, "FAIL decisions.input: --root is empty\n"), (rc, buf.getvalue()))

    def test_one_commit_lookup_per_distinct_sha(self):
        calls = []
        real = gitinfo.commit_exists

        def counting(root, sha):
            calls.append(sha)
            return real(root, sha)

        with mock.patch.object(gitinfo, "commit_exists", counting):
            rc, out = self.run_gate(OPEN + ANSWERED.replace("BY", "superbrobenji"))
        self.assertEqual(0, rc, out)
        self.assertEqual([self.sha], calls)

    def test_missing_register_is_fail_input(self):
        os.remove(os.path.join(self.root, DECISIONS))
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = gate_decisions.main(["--root", self.root])
        self.assertTrue(buf.getvalue().startswith("FAIL decisions.input: "), buf.getvalue())
        self.assertEqual(1, rc)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_gate_decisions -v`
Expected: ERROR, `ImportError: cannot import name 'gate_decisions' from 'tools.gates'`.

- [ ] **Step 3: Write the gate**

`tools/gates/gate_decisions.py`:

```python
#!/usr/bin/env python3
"""gate_decisions -- the decisions register (spec §4.3, §5 "Decision classes", §7).

  python3 tools/gates/gate_decisions.py [--json] [--root <tree>] [--scan <path> ...]

Fails on: a malformed row; an answer whose `answered_at.by` is not the owner handle in
tools/gates/config.yml (every class in OWNER_ONLY_CLASSES is answered by the owner alone; the
schema check in registers.validate_decision rejects it and this gate files it under `owner`);
an asked_at / answered_at sha that is not a commit of the gated repository (DEC-0003, filed under
`schema`); and any reference to an unanswered decision id (DEC-NNNN) in a --scan file. A --scan
path that cannot be read (missing, a directory, not UTF-8) is a FAIL, never a skip. Row ids print
forgery-safe (entry-<i> when the id is malformed), an empty --root is an error, and a category
some row never reached prints no PASS line (an empty or absent --scan list never prints PASS for
`scan` or `open-ref`).
"""
from __future__ import annotations

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import CODE_ROOT, GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config, registers  # noqa: E402

# spec §5: "Decision classes (owner-only, always)" -- every class in the register.
OWNER_ONLY_CLASSES = list(registers.DECISION_CLASSES)
REF = re.compile(r"DEC-\d{4}(?!\d)")
CATEGORIES = ["schema", "duplicate", "owner", "open-ref", "scan"]


def check(args: list) -> Result:
    ap = gate_args("gate_decisions")
    ap.add_argument("--scan", nargs="*", default=[], help="files that must not cite an unanswered decision")
    ns = ap.parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    owner = config.load(CODE_ROOT)["owner"]["handle"]
    decisions = registers.load_decisions(root)
    r = Result("decisions")
    exists = registers.memo_commit_exists()    # one git call per distinct sha
    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    if not ns.scan:    # nothing was scanned: no PASS line for a check that never ran
        skipped.update(["scan", "open-ref"])
    seen = set()
    for i, d in enumerate(decisions):
        did = registers.row_id(d, i, registers.DECISION_ID)
        if did in seen:
            r.fail(f"decisions.duplicate.{did}", "id appears more than once")
        seen.add(did)
        errs = registers.validate_decision(d, owner=owner)
        for e in errs:
            r.fail(f"decisions.{'owner' if e.startswith(registers.OWNER_ANSWER_ERROR) else 'schema'}.{did}", e)
        if any(not e.startswith(registers.OWNER_ANSWER_ERROR) for e in errs):
            skipped.update(["schema", "owner", "open-ref"])    # the sha and answer checks need a well-formed row
            continue
        for e in registers.decision_commit_errors(d, root, exists):
            r.fail(f"decisions.schema.{did}", e)
    unanswered = {d["id"] for d in registers.open_decisions(decisions, owner=owner)
                  if isinstance(d.get("id"), str) and registers.DECISION_ID.fullmatch(d["id"])}
    for path in ns.scan:
        full = path if os.path.isabs(path) else os.path.join(root, path)
        rel = registers.safe_text(os.path.relpath(full, root))
        try:
            registers.require_regular(root, full)    # opening a FIFO would hang the gate
        except registers.PathError as exc:
            r.fail(f"decisions.scan.{rel}", exc.reason if exc.not_regular else f"cannot read: {exc.reason}")
            continue
        try:
            with open(full, encoding="utf-8") as fh:
                text = fh.read()
        except OSError as exc:
            r.fail(f"decisions.scan.{rel}", f"cannot read: {exc.strerror or exc}")
            continue
        except UnicodeDecodeError:
            r.fail(f"decisions.scan.{rel}", "cannot read: not valid UTF-8")
            continue
        for n, line in enumerate(text.split("\n"), 1):
            for did in sorted(set(REF.findall(line)) & unanswered):
                r.fail(f"decisions.open-ref.{did}", f"{rel}:{n} references unanswered decision {did}; "
                                                    "the owner answers it first")
    failed = {l.id.split(".")[1] for l in r.lines if l.status == "FAIL"}
    for cat in CATEGORIES:
        if cat not in failed and cat not in skipped:
            r.passed(f"decisions.{cat}")
    return r


def main(argv=None):
    return run_gate("decisions", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests to verify they pass**

```bash
python3 -m unittest tools.gates.tests.test_gate_decisions -v                          # Ran 21 tests ... OK
python3 tools/gates/gate_decisions.py --scan docs/superpowers/plans/*.md; echo rc=$?  # 5 PASS lines, rc=0 (DEC-0001..0003 answered)
```

Mutation checks (`PYTHONDONTWRITEBYTECODE=1`; restore after each), each with the tests it kills:
- route every error to schema (`'owner' if e.startswith(registers.OWNER_ANSWER_ERROR) else 'schema'` -> `'schema'`): `test_an_agent_answer_fails`, `test_every_class_is_owner_only`, `test_an_agent_answer_with_an_unknown_commit_fails_owner_and_schema`, `test_an_agent_answered_decision_is_still_unanswered_for_open_refs`;
- pass `owner='x'` to `registers.open_decisions` in the gate: `test_reference_to_an_unanswered_decision_fails`;
- revert `registers.open_decisions` to `answer is None` only: `test_an_agent_answered_decision_is_still_unanswered_for_open_refs` and `test_open_decisions_means_not_answered_by_the_owner`;
- change the skip condition to `if errs:`: `test_an_agent_answer_with_an_unknown_commit_fails_owner_and_schema`;
- drop the `skipped.update(["schema", "owner", "open-ref"])` effect (`pass`): `test_a_schema_error_skips_the_categories_it_never_reached`;
- replace `registers.require_regular(root, full)` with `pass`: `test_a_directory_and_a_non_utf8_scan_file_fail` and `test_a_fifo_scan_path_fails_instead_of_hanging` (alarm);
- `text.split("\n")` -> `text.splitlines()`: `test_line_numbers_count_newlines_only`;
- drop the empty-`--root` check: `test_an_empty_root_is_a_fail_not_the_cwd_repo`;
- drop the `decision_commit_errors` loop: `test_an_unknown_commit_is_a_schema_failure`, `test_an_agent_answer_with_an_unknown_commit_fails_owner_and_schema`, `test_one_commit_lookup_per_distinct_sha`;
- use `gitinfo.commit_exists` instead of `memo_commit_exists()`: `test_one_commit_lookup_per_distinct_sha`;
- use `d.get("id")` instead of `row_id`: `test_a_malformed_id_prints_as_entry_index`;
- drop the `(?!\d)` in `REF`: `test_a_longer_id_is_not_a_reference_to_a_shorter_one`;
- drop the `UnicodeDecodeError` branch: `test_a_directory_and_a_non_utf8_scan_file_fail`;
- drop `safe_text` from the scan path: `test_a_scan_path_cannot_forge_a_result_line`;
- drop the duplicate FAIL: `test_schema_errors_and_duplicates_fail`;
- drop the `if not ns.scan:` branch (`skipped.update(["scan", "open-ref"])`): `test_without_a_scan_list_no_scan_or_open_ref_line_passes`.

Restore and run again: OK.

- [ ] **Step 5: Ledger line, then commit**

Append `- T5 feat(gates): gate_decisions -- test_gate_decisions 21 OK` to the ledger.

```bash
git add tools/gates/gate_decisions.py tools/gates/tests/test_gate_decisions.py docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): gate_decisions -- owner-only answers, no reference to an unanswered decision" \
  -m "Strict-delivery S1 Task 5 (spec §4.3, §5 decision classes, §7). Every class is owner-only; answered_at.by must equal config owner.handle; a missing --scan file is a FAIL."
```

### Task 6: `run_all`, CI job `gates`, `tools/gates/README.md`

**Files:**
- Create: `tools/gates/run_all.py`, `tools/gates/tests/test_run_all.py`, `.github/workflows/gates.yml`, `tools/gates/README.md`

**Interfaces:**
- Consumes: `Line`, `Result`, `GateError`, `gate_args`, `repo_root`, `run_check`, `run_gate`; the three gate modules (`tools.gates.gate_<name>.check`).
- Produces (umbrella "run_all.py"): `GATES = ["register", "claims", "decisions"]` (the gates installed at this block; S2–S4 append theirs); `run(names: list[str], args: dict) -> Result`. `args` keys: `"root"` (tree; default `repo_root()`; not a str is `FAIL <name>.input: --root is not a path`, blank is `--root is empty`, not an existing directory is `--root is not a directory: <root>`; never the cwd repo), plus `"<gate name>": [extra argv]`. `decisions` always gets `--scan` with every `docs/superpowers/plans/*.md` under root; none there (directory missing or empty) is `FAIL decisions.input: no docs/superpowers/plans/*.md under the root: nothing to scan`. Line ids are prefixed `<name>.` unless they already start with `<name>.` (a gate's line naming another gate stays under its own gate's prefix). Unknown name: `FAIL run_all.unknown`. A name given twice runs once and adds `FAIL run_all.duplicate`. No names: `FAIL run_all.empty`. Missing module: `FAIL <name>.input`. Also `gate_argv(name, args) -> list` (raises `GateError`, which `run` turns into the FAIL), `check(args) -> Result` and `main(argv=None) -> int` through `run_gate` (lib conventions: `--json` first, `--only a,b`, `--root <tree>`, no abbreviations, help on stderr, bad arguments are `FAIL run_all.input: bad arguments (argparse exit N)` rc 1). CI job id and name: `gates` (one job, no matrix; the gate tests run on Python 3.9 and 3.12).

- [ ] **Step 1: Write the failing tests**

`tools/gates/tests/test_run_all.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/run_all.py. Run from the repo root:
python3 -m unittest discover -s tools/gates/tests -t .
"""
from __future__ import annotations

import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import unittest
from unittest import mock

from tools.gates import run_all
from tools.gates.lib import Result
from tools.gates.tests import gitfixture as gf

PLANS = os.path.join("docs", "superpowers", "plans")
SCRIPT = os.path.join(gf.REPO, "tools", "gates", "run_all.py")


class RunAll(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        self.sha = gf.git(self.root, "rev-parse", "HEAD")
        os.makedirs(os.path.join(self.root, "docs", "reviews"))    # gate_register's required directory
        gf.write(self.root, os.path.join(PLANS, "p.md"), "a plan citing nothing\n")

    def tearDown(self):
        gf.remove(self.root)

    def cli(self, *args):
        env = dict(os.environ, CLAUDE_PROJECT_DIR=self.root)
        return subprocess.run([sys.executable, SCRIPT, *args], capture_output=True, text=True,
                              env=env, check=False)

    def test_gates_at_this_block(self):
        self.assertEqual(["register", "claims", "decisions"], run_all.GATES)

    def test_clean_tree_passes_and_lines_carry_the_gate_prefix(self):
        res = run_all.run(run_all.GATES, {"root": self.root})
        self.assertTrue(res.ok(), res.render(False))
        self.assertIn("PASS register.schema", res.render(False))
        self.assertIn("PASS claims.stale", res.render(False))
        self.assertIn("PASS decisions.open-ref", res.render(False))

    def test_a_failing_gate_fails_the_run(self):
        os.remove(os.path.join(self.root, gf.STATE, "findings.yml"))
        res = run_all.run(["register", "claims"], {"root": self.root})
        self.assertFalse(res.ok())
        self.assertTrue(res.render(False).startswith("FAIL register.input: "), res.render(False))
        self.assertIn("FAIL claims.input: ", res.render(False))   # claims reads findings.yml too

    def test_unknown_gate_is_fail(self):
        res = run_all.run(["register", "comments"], {"root": self.root})
        self.assertIn("FAIL run_all.unknown: no gate named 'comments'", res.render(False))
        self.assertFalse(res.ok())

    def test_a_missing_gate_script_is_fail(self):
        with mock.patch.object(run_all, "GATES", run_all.GATES + ["ghost"]):
            res = run_all.run(["ghost"], {"root": self.root})
        self.assertIn("FAIL ghost.input: No module named 'tools.gates.gate_ghost'", res.render(False))
        self.assertFalse(res.ok())

    def test_no_names_is_fail(self):
        res = run_all.run([], {"root": self.root})
        self.assertEqual("FAIL run_all.empty: no gate ran", res.render(False))

    def test_an_empty_root_is_fail_never_the_cwd_repo(self):
        for root in ("", "  "):
            res = run_all.run(["register", "decisions"], {"root": root})
            self.assertEqual("FAIL register.input: --root is empty\nFAIL decisions.input: --root is empty",
                             res.render(False), repr(root))

    def test_an_empty_root_on_the_command_line_is_fail(self):
        p = self.cli("--only", "register", "--root", "")
        self.assertEqual(1, p.returncode, p.stdout + p.stderr)
        self.assertEqual("FAIL register.input: --root is empty\n", p.stdout)

    def test_a_root_that_is_not_a_path_says_so(self):
        for root in (None, 5):
            res = run_all.run(["register", "decisions"], {"root": root})
            self.assertEqual("FAIL register.input: --root is not a path\n"
                             "FAIL decisions.input: --root is not a path", res.render(False), repr(root))

    def test_a_root_that_is_missing_or_a_file_is_not_a_directory(self):
        missing = os.path.join(self.root, "absent")
        a_file = os.path.join(self.root, "README.md")
        for root in (missing, a_file):
            res = run_all.run(["decisions"], {"root": root})
            self.assertEqual(f"FAIL decisions.input: --root is not a directory: {root}",
                             res.render(False), root)

    def test_a_gate_named_twice_runs_once_and_fails(self):
        res = run_all.run(["register", "register"], {"root": self.root})
        out = res.render(False)
        self.assertEqual(1, out.count("PASS register.schema"), out)
        self.assertIn("FAIL run_all.duplicate: gate 'register' is named more than once", out)
        self.assertFalse(res.ok())
        p = self.cli("--only", "register,register")
        self.assertEqual(1, p.returncode, p.stdout + p.stderr)

    def test_cli_bad_arguments_are_a_fail_line(self):
        for bad in (("--bogus",), ("--ro", "/x"), ("--only",), ("--only", "register", "--json")):
            p = self.cli(*bad)
            self.assertEqual(1, p.returncode, (bad, p.stdout, p.stderr))
            self.assertEqual("FAIL run_all.input: bad arguments (argparse exit 2)\n", p.stdout, bad)

    def test_cli_help_goes_to_stderr_and_json_stdout_stays_json(self):
        p = self.cli("--json", "-h")
        self.assertIn("usage: run_all", p.stderr)
        self.assertNotIn("usage", p.stdout)
        self.assertEqual("run_all.input", json.loads(p.stdout)["lines"][0]["id"])

    def test_a_foreign_id_is_prefixed_never_taken_as_another_gates(self):
        forged = Result("register")
        forged.fail("claims.stale.CLM-1", "pretends to be claims")
        forged.passed("decisions.owner")
        with mock.patch.object(run_all, "_check_of", return_value=lambda argv: forged):
            res = run_all.run(["register"], {"root": self.root})
        self.assertEqual("FAIL register.claims.stale.CLM-1: pretends to be claims\n"
                         "PASS register.decisions.owner", res.render(False))

    def test_decisions_scans_the_plans(self):
        gf.write(self.root, os.path.join(gf.STATE, "decisions.yml"),
                 '- {id: DEC-9001, class: scope, question: "q?", options: [{label: a, cost: x}, '
                 '{label: b, cost: y}], asked_at: {sha: "%s", by: s}, answer: null, answered_at: null}\n'
                 % self.sha)
        gf.write(self.root, os.path.join(PLANS, "p.md"), "waits on DEC-9001\n")
        res = run_all.run(["decisions"], {"root": self.root})
        self.assertIn("FAIL decisions.open-ref.DEC-9001: docs/superpowers/plans/p.md:1", res.render(False))

    def test_no_plans_to_scan_is_fail_never_a_silent_pass(self):
        shutil.rmtree(os.path.join(self.root, PLANS))
        for make_dir in (False, True):     # plans directory missing, then present and empty
            if make_dir:
                os.makedirs(os.path.join(self.root, PLANS))
            res = run_all.run(["decisions"], {"root": self.root})
            self.assertEqual("FAIL decisions.input: no docs/superpowers/plans/*.md under the root: "
                             "nothing to scan", res.render(False), make_dir)

    def test_extra_args_reach_the_gate(self):
        res = run_all.run(["claims"], {"root": self.root, "claims": ["--mark-stale"]})
        self.assertIn("FAIL claims.input: --mark-stale needs --base <sha>", res.render(False))

    def test_cli_only_and_rc(self):
        p = self.cli("--only", "register")
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        self.assertTrue(p.stdout.startswith("PASS register.schema"), p.stdout)
        os.remove(os.path.join(self.root, gf.STATE, "findings.yml"))
        p = self.cli("--only", "register")
        self.assertEqual(1, p.returncode)

    def test_cli_unknown_gate_is_rc_1(self):
        p = self.cli("--only", "comments")
        self.assertEqual(1, p.returncode, p.stdout + p.stderr)
        self.assertEqual("FAIL run_all.unknown: no gate named 'comments' "
                         "(installed: register, claims, decisions)\n", p.stdout)

    def test_cli_json(self):
        p = self.cli("--json", "--only", "claims")
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)
        out = json.loads(p.stdout)
        self.assertTrue(out["ok"])
        self.assertTrue(out["lines"])
        self.assertTrue(all(l["id"].startswith("claims.") for l in out["lines"]))

    def test_the_repository_passes_every_installed_gate(self):
        buf = io.StringIO()
        with contextlib.redirect_stdout(buf):
            rc = run_all.main([])
        self.assertEqual(0, rc, buf.getvalue())


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_run_all -v`
Expected: ERROR, `ImportError: cannot import name 'run_all' from 'tools.gates'`.

- [ ] **Step 3: Write `run_all.py`**

`tools/gates/run_all.py`:

```python
#!/usr/bin/env python3
"""run_all -- every installed gate, one aggregated Result (umbrella plan, "run_all.py").

  python3 tools/gates/run_all.py [--json] [--only a,b] [--root <tree>]

A gate not yet implemented at this block is absent from GATES, never stubbed to PASS. A gate
module that is missing or raises is a FAIL `<name>.input`, never a skip. A --root that is empty, not
a path or not a directory, a gate named twice, and a root with no docs/superpowers/plans/*.md for
`decisions` to scan are each a FAIL, never a pass. The command line follows lib's gate
conventions (run_gate): --json first, help on stderr, bad arguments are `FAIL run_all.input`.
"""
from __future__ import annotations

import glob
import importlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from tools.gates.lib import GateError, Line, Result, gate_args, repo_root, run_check, run_gate  # noqa: E402

GATES = ["register", "claims", "decisions"]
PLANS_GLOB = os.path.join("docs", "superpowers", "plans", "*.md")


def gate_argv(name: str, args: dict) -> list:
    """argv for one gate: --root, the gate's defaults, then args[name] (a list) verbatim."""
    root = args["root"] if "root" in args else repo_root()
    if not isinstance(root, str):
        raise GateError("--root is not a path")
    if not root.strip():
        raise GateError("--root is empty")
    if not os.path.isdir(root):
        raise GateError(f"--root is not a directory: {root}")
    argv = ["--root", root]
    if name == "decisions":
        plans = sorted(glob.glob(os.path.join(root, PLANS_GLOB)))
        if not plans:
            raise GateError(f"no {PLANS_GLOB.replace(os.sep, '/')} under the root: nothing to scan")
        argv += ["--scan", *plans]
    return argv + list(args.get(name, []))


def _check_of(name: str):
    def check(argv: list) -> Result:
        return importlib.import_module(f"tools.gates.gate_{name}").check(argv)
    return check


def run(names: list, args: dict) -> Result:
    """Run each named gate; lines are prefixed '<name>.' unless they already are."""
    total = Result("run_all")
    ran = set()
    for name in names:
        if name in ran:
            total.fail("run_all.duplicate", f"gate {name!r} is named more than once")
            continue
        ran.add(name)
        if name not in GATES:
            total.fail("run_all.unknown", f"no gate named {name!r} (installed: {', '.join(GATES)})")
            continue
        res = run_check(name, lambda argv, n=name: _check_of(n)(gate_argv(n, args)), [])
        for l in res.lines:
            lid = l.id if l.id.startswith(name + ".") else f"{name}.{l.id}"
            total.lines.append(Line(l.status, lid, l.reason))
    if not total.lines:
        total.fail("run_all.empty", "no gate ran")
    return total


def check(args: list) -> Result:
    ap = gate_args("run_all")
    ap.add_argument("--only", default=None, help="comma-separated gate names")
    ns = ap.parse_args(args)
    names = GATES if ns.only is None else [n.strip() for n in ns.only.split(",") if n.strip()]
    return run(names, {} if ns.root is None else {"root": ns.root})


def main(argv=None) -> int:
    return run_gate("run_all", check, argv)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run the tests and the aggregate**

```bash
python3 -m unittest tools.gates.tests.test_run_all -v     # Ran 21 tests ... OK
python3 -m unittest discover -s tools/gates/tests -t .    # Ran 198 tests ... OK
python3 tools/gates/run_all.py; echo rc=$?                # 15 PASS lines (6 register, 4 claims, 5 decisions), rc=0
```

`test_the_repository_passes_every_installed_gate` fails if DEC-0001 is still unanswered: the umbrella plan references it (lines 17 and 287). That is the intended block, not a bug.

Mutation checks (each run under `PYTHONDONTWRITEBYTECODE=1`, restored afterwards, then `Ran 21 tests ... OK`):
- `_check_of(name)` returns `Result(name)` (a gate with no lines): `test_clean_tree_passes_and_lines_carry_the_gate_prefix` fails (among 7 others), and `res.render(False)` shows `FAIL register.empty: gate produced no verdict`.
- `gate_argv` reads `root = args.get("root") or repo_root()`: `test_an_empty_root_is_fail_never_the_cwd_repo` and `test_an_empty_root_on_the_command_line_is_fail` fail.
- `gate_argv` loses its `if not os.path.isdir(root):` guard: `test_a_root_that_is_missing_or_a_file_is_not_a_directory` fails. Its `--root is not a path` message becomes `--root is empty`: `test_a_root_that_is_not_a_path_says_so` fails.
- `gate_argv` loses its `if not plans:` guard: `test_no_plans_to_scan_is_fail_never_a_silent_pass` fails.
- `run` keeps `lid = l.id` (no prefixing): `test_a_foreign_id_is_prefixed_never_taken_as_another_gates` fails.
- `run` loses its `if name in ran:` branch: `test_a_gate_named_twice_runs_once_and_fails` fails.
- `gate_argv` drops the `--scan` list: `test_decisions_scans_the_plans` fails.
- `main` returns 0 instead of `run_gate(...)`, or `check` parses with a plain `argparse.ArgumentParser`: the CLI tests (`test_cli_bad_arguments_are_a_fail_line`, `test_cli_help_goes_to_stderr_and_json_stdout_stays_json`, `test_cli_only_and_rc`) fail.

- [ ] **Step 5: Write the CI job and the README**

`.github/workflows/gates.yml`:

```yaml
name: gates
on:
  push:
    branches: [main]
  pull_request:
  workflow_dispatch:

jobs:
  gates:
    # Strict-delivery gate suite (docs/superpowers/specs/2026-10-17-strict-delivery-design.md §7-§8).
    # A missing gate script, register or config is a FAIL, never a skip: unlike ci.yml's
    # host-tests there is deliberately no "Probe inputs" / "Nothing to do" step here.
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0   # claim rows are compared against verified_at..HEAD; a shallow clone has no history
      - uses: actions/setup-python@v5
        with:
          # Both interpreters in this one job (no matrix: a matrix would rename the required check).
          # The last version listed is the default `python3` (here 3.12); every step names its own.
          python-version: |
            3.9
            3.12
      - name: Install PyYAML (pinned in tools/requirements.txt)
        run: |
          python3.9 -m pip install PyYAML==6.0.2
          python3.12 -m pip install PyYAML==6.0.2
      - name: Gate-script tests (Python 3.9)
        run: python3.9 -m unittest discover -v -s tools/gates/tests -t .
      - name: Gate-script tests (Python 3.12)
        run: python3.12 -m unittest discover -v -s tools/gates/tests -t .
      - name: Gates at HEAD
        run: python3.12 tools/gates/run_all.py
```

`tools/gates/README.md` (the Hooks section is added in Task 8, when the hooks exist):

````markdown
# tools/gates -- strict-delivery gate scripts

Spec: `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` §7-§8. Interfaces: the
umbrella plan `docs/superpowers/plans/2026-10-17-strict-delivery.md`, "Shared interfaces".

## Contract

- One script, one concern: `gate_<name>.py` exposes `check(args) -> Result` and `main(argv)`.
- Output: one line per verdict, `PASS <id>` or `FAIL <id>: <reason>`; `--json` for a JSON object.
- Exit 0 only when every line is PASS and there is at least one line; otherwise 1.
- A missing or unreadable input is `FAIL <gate>.input`, never "nothing to do"; an empty result
  is `FAIL <gate>.empty`; bad arguments are `FAIL <gate>.input: bad arguments`.
- `--root <tree>` points a gate at another tree's `docs/superpowers/state/`; config is always read
  from the checkout holding the gate code (`tools/gates/config.yml`).

## Installed gates

| Gate | Checks |
|---|---|
| `gate_register.py` | findings schema, an owner on every row, legal transitions, evidence per state |
| `gate_claims.py` | claims schema, every sha a commit of the repository, verified rows not stale at HEAD (history + staged files), `false` rows name a registered finding; `--mark-stale --base <sha>` rewrites stale rows |
| `gate_decisions.py` | decisions schema (every sha a commit of the repository), owner-only answers, no `--scan` file citing an unanswered `DEC-NNNN` |

`run_all.py` runs every installed gate (`GATES`) and aggregates the lines with a `<gate>.`
prefix; `--only a,b` selects, `--root <tree>` gates another tree (`--json` first, as for every
gate). An unknown, missing or repeated gate, an empty or non-directory `--root`, and a root with
no `docs/superpowers/plans/*.md` for `decisions` to scan are each a FAIL. For `decisions` it
scans every `docs/superpowers/plans/*.md`; run alone without `--scan`, `gate_decisions.py`
prints no `scan` or `open-ref` PASS line.

## Running

```sh
python3 -m pip install PyYAML==6.0.2              # once per interpreter (tools/requirements.txt)
python3 -m unittest discover -s tools/gates/tests -t .
python3 tools/gates/run_all.py
python3 tools/gates/gate_register.py --json
```

CI: `.github/workflows/gates.yml`, job `gates` (the unit tests on Python 3.9 and 3.12, then `run_all.py`; one job, full history checkout).
````

```bash
python3 -c "import yaml; j = yaml.safe_load(open('.github/workflows/gates.yml'))['jobs']; assert list(j) == ['gates'], j; print('job ok')"
git add -A && git diff --cached --check && echo whitespace-clean
git grep -nIE 'lands in [0-9]|arrives in [0-9]|lands with the|later sessions?|stubbed until|stub until|not in plan [0-9]|Plan 04 ships no display' -- tools/gates   # only config.yml:27
```

- [ ] **Step 6: Ledger line, then commit**

Append `- T6 feat(gates): run_all + CI job gates + README -- suite 198 OK; run_all 15 PASS` to the ledger.

```bash
git add tools/gates/run_all.py tools/gates/tests/test_run_all.py .github/workflows/gates.yml tools/gates/README.md docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): run_all and the required CI job gates" \
  -m "Strict-delivery S1 Task 6 (spec §7, §8 CI; umbrella run_all.py). GATES = register, claims, decisions; a missing gate module is a FAIL. gates.yml: full-history checkout, PyYAML 6.0.2, gate tests, gates at HEAD; no probe/skip step."
```

### CHECKPOINT b2

The umbrella's "Checkpoint protocol for this plan", verbatim:

> At every block end: run `python3 tools/gates/run_all.py`; dispatch final reviewer (opus) + final verifier (Fable) with the block's review packages; `gh pr ready`; wait for every check; merge with `gh pr merge --squash` only if `decisions.yml` has `grant:strict-delivery` answered `yes` by the owner; tag `strict-delivery-b<N>` on the squash commit and push the tag; delete branch + worktrees; write `docs/superpowers/state/strict-delivery/handoff.md`; append `CHECKPOINT b<N>` to the ledger; post the status update and the resume prompt; stop.

These steps carry it out by hand for block b2, because `gate_checkpoint` does not exist until b6. The one ordering change is umbrella Owner ruling 2: the handoff and the `CHECKPOINT` line are committed before the merge, and the post-merge facts are verified in step 8. The owner sees this in the status update.

- [ ] **Step 1: Gates at the block tip**

```bash
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py
git status --short     # empty
```

Every line PASS, `OK`. On any FAIL, stop: fix through the task's review loop before going on.

- [ ] **Step 2: Review packages (by hand; `gate_review_package` arrives in S2)**

For each task T in this block, with `B` = the commit before the task and `H` = the task's commit:

```bash
P=.superpowers/sdd/strict-delivery/review/b2/T<n>; mkdir -p "$P"
git log --oneline B..H > "$P/commits.txt"
git diff -U10 B..H > "$P/diff.patch"
{ python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py; } > "$P/gates.txt" 2>&1
```

Add `claims.md` (claim | direct command | contradiction | output excerpt | PASS/FAIL) for every behavioural claim in the task's commit message and ledger line. Add `mutation-<test>.txt` for each mutation check the task names, in the umbrella's three-step format. The package stays under the gitignored `.superpowers/` (Interface note 9).

- [ ] **Step 3: Owner items for this block**

(a) **Required check.** Ask the owner to add `gates` to the required status checks on `main` (branch protection is the owner's setting). Verify before Step 6's merge: `gh api repos/superbrobenji/lap-timer/branches/main/protection/required_status_checks --jq .contexts` lists `gates` beside the nine existing contexts.

(b) List this block's rulings (ledger `Ruling:` lines, at most 5) for the owner.

- [ ] **Step 4: Final reviewer and final verifier**

Final verifier model: Fable (umbrella Owner ruling 9). Dispatch the final branch reviewer (opus) and then the final adversarial verifier on the whole branch `BASE_B2..HEAD`. Give each the review packages and these report paths: `.superpowers/sdd/strict-delivery/b2-final-review.md` and `.superpowers/sdd/strict-delivery/b2-final-verify.md`. The prompt-block files do not exist until Task 9. Each dispatch therefore cites, as its strict block, spec §5 dispatch contract item 4, §6 step 6, D8 and D16, plus the Review Focus of the umbrella and of this plan. Both must report `status: DONE` with no finding. Any finding goes through the fix loop (spec §6 step 7), then back to Step 1.

- [ ] **Step 5: Ledger CHECKPOINT line and handoff, committed on the branch**

```bash
TIP=$(git rev-parse --short=7 HEAD)     # the reviewed tip; recorded as `head`
```

Write `docs/superpowers/state/strict-delivery/handoff.md` (YAML front matter per the umbrella "Handoff"; values for this block):

```markdown
---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: main
head: "<TIP>"
base: "<BASE_B2>"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: <line number of this block's CHECKPOINT line in the ledger>
block: 2
next_task: "T7"
findings_in_flight: []
claims_verified_at_head: []
live_subagents: []
open_decisions: [<every DEC id in decisions.yml that the owner has not answered>]
rulings: [<this block's Ruling lines, one each; at most 5>]
grant: DEC-0001
parked_branches: [<every local branch but main: {name, reason, owner}>]
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps: [<one line each, found in this block>]
---

Post-merge facts (verified in CHECKPOINT b2 step 8, not by gate_checkpoint): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.
```

Append to the ledger:

```markdown
CHECKPOINT b2 tip <TIP> -- gates: <the Step 1 summary, e.g. "suite N OK, M PASS">; rulings: <count>; final review + verify: clean
```

```bash
git add docs/superpowers/state/strict-delivery/handoff.md docs/superpowers/state/strict-delivery/ledger.md
git commit -m "docs(state): strict-delivery CHECKPOINT b2 handoff and ledger line"
FINAL=$(git rev-parse HEAD)            # the tree the squash must reproduce
git push -u origin block/strict-delivery-b2
```

- [ ] **Step 6: PR, every check green, grant, merge**

```bash
gh pr create --draft --base main --head block/strict-delivery-b2 --title "strict-delivery b2: gate_claims, gate_decisions, run_all, CI job gates" --body-file .superpowers/sdd/strict-delivery/b2-pr-body.md
gh pr ready block/strict-delivery-b2
gh pr checks block/strict-delivery-b2 --watch          # every job, advisory (coverage) included, must pass
python3 -c "import sys; sys.path.insert(0, '.'); from tools.gates.lib import registers as r; d = r.grant_for(r.load_decisions('.'), 'strict-delivery'); print('GRANT', d['id']) if d else sys.exit('NO GRANT: stop, the owner answers DEC-0001 first')"
gh pr merge block/strict-delivery-b2 --squash
```

The PR body (`.superpowers/sdd/strict-delivery/b2-pr-body.md`) lists the tasks, the gate output from Step 1, both final report summaries, the owner items from Step 3 with their state, and Interface notes 8–10 where they apply. It contains none of `fix before merge`, `draft`, `parked`, `TODO`, `not for merge` (config `merge_markers`). If any check is red or the grant line says `NO GRANT`, stop. Never merge red, never recommend it.

- [ ] **Step 7: Tag and clean up**

```bash
git switch main && git pull --ff-only
SQUASH=$(git rev-parse HEAD)
git log -1 --format=%s "$SQUASH"                  # the PR title
git tag strict-delivery-b2 "$SQUASH" && git push origin strict-delivery-b2
git push origin --delete block/strict-delivery-b2 2>/dev/null || true   # gone already if GitHub auto-deleted it
git branch -D block/strict-delivery-b2
git worktree list                                  # main checkout only; else `git worktree remove <path>` each
```

- [ ] **Step 8: Verify the clean state and the post-merge facts**

```bash
test "$(git rev-parse --short=7 "$SQUASH^")" = "<BASE_B2>" && echo parent-ok   # main moved by exactly this squash
git diff --quiet "$FINAL" "$SQUASH" && echo tree-ok                            # squash == the reviewed branch
git status --porcelain | wc -l                                                 # 0
git branch --format='%(refname:short)'                                         # main, plus only the parked branches in the handoff
gh pr list --head block/strict-delivery-b2 --state open                           # empty
git tag --points-at "$SQUASH"                                                  # strict-delivery-b2
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py
```

Every line as stated. Otherwise the checkpoint is not reached: fix it, and do not post.

- [ ] **Step 9: Status update and resume prompt, then stop**

Post to the owner: done since the last checkpoint (task ids, commits, gate results at main); what remains in S1 and in the umbrella (S2–S5); open decisions; rulings made in this block; parked items; the owner items from Step 3. Then post the resume prompt verbatim and stop. Do not start the next block. The owner runs `/clear`.

```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 3 (sub-plan S1, task 7) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

The skill arrives in Task 9. Until then, "invoke the strict-delivery skill" means reading this plan's Global Constraints and Interface notes. Tell the owner.

---

# Block b3 — hooks, `CLAUDE.md`, skill skeleton (PR 3, tag `strict-delivery-b3`)

### Block b3 setup

- [ ] **Step 1: Branch from the merged main and run the gates**

```bash
git switch main && git pull --ff-only
git switch -c block/strict-delivery-b3
git rev-parse --short=7 HEAD      # BASE_B3
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py
```

### Task 7: `pre_tool_use.py` (Bash branch), git pre-commit, `install-hooks.sh`

**Files:**
- Create: `tools/gates/hooks/pre_tool_use.py`, `tools/gates/hooks/git-pre-commit.sh`, `tools/gates/install-hooks.sh`
- Test: `tools/gates/tests/test_hooks.py` (classes `PreToolUse`, `GitHooks`; Task 8 adds more)

**Interfaces:**
- Consumes: `run_all.run(names, {"root": <toplevel>}) -> Result` (Task 6); the hook protocol in the umbrella (stdin JSON: `tool_name`, `tool_input.command`, `cwd`; exit 2 + stderr to block; exit 0 silent to allow).
- Produces: `pre_tool_use.py`. `Bash` event whose command contains `git [opts] commit` or `git [opts] push` → `run_all.run(["register", "claims"], {"root": git toplevel of cwd})`. Any other command or tool, `Agent` included, exits 0 at this block; S2 adds the Agent branch. Fail closed on any exception: `FAIL hook.internal: <Type>: <msg>`, exit 2. Also `gates_for(event) -> list[str]`, `GIT_VERB`, `GATED`. `git-pre-commit.sh`: unsets `CLAUDE_PROJECT_DIR`; refuses unstaged edits under `docs/superpowers/state` (`FAIL pre-commit.unstaged`); then `run_all.py --only register,claims`. `install-hooks.sh`: writes the marked shim to `$(git rev-parse --git-path hooks)/pre-commit` and moves a foreign hook to `pre-commit.pre-strict-delivery`. The marker line is exactly `# strict-delivery-gates-hook`. A tree without the script → `FAIL pre-commit.missing`.

- [ ] **Step 1: Write the failing tests**

`tools/gates/tests/test_hooks.py`:

```python
#!/usr/bin/env python3
"""Tests for tools/gates/hooks/*, install-hooks.sh, .claude/settings.json and CLAUDE.md. Each hook
runs as a subprocess with a JSON event on stdin -- the way Claude Code runs it. Run from the repo
root: python3 -m unittest discover -s tools/gates/tests -t .
"""
import json
import os
import shutil
import subprocess
import sys
import unittest

from tools.gates.tests import gitfixture as gf

HOOKS = os.path.join(gf.REPO, "tools", "gates", "hooks")
ORPHAN = """- id: T1-ORPHAN-1
  source: audit
  severity: MEDIUM
  class: >-
    A finding nobody owns.
  siblings: []
  cites: []
  repro: null
  owner: null
  state: open
  pin: null
  ruling: null
  evidence: []
  history: [{state: open, sha: "aaaaaaa", by: audit}]
"""


def hook(script, event, hooks_dir=HOOKS):
    env = {k: v for k, v in os.environ.items() if k != "CLAUDE_PROJECT_DIR"}
    raw = event if isinstance(event, str) else json.dumps(event)
    return subprocess.run([sys.executable, os.path.join(hooks_dir, script)], input=raw,
                          capture_output=True, text=True, env=env, check=False)


def bash_event(root, command):
    return {"session_id": "s", "transcript_path": "/dev/null", "cwd": root, "permission_mode": "default",
            "hook_event_name": "PreToolUse", "tool_name": "Bash", "tool_use_id": "t",
            "tool_input": {"command": command, "description": "d"}}


def copy_gates(dst):
    """The real tools/gates (code + scripts) copied into a throwaway repo, without test fixtures."""
    shutil.copytree(os.path.join(gf.REPO, "tools", "gates"), os.path.join(dst, "tools", "gates"),
                    ignore=shutil.ignore_patterns("tests", "__pycache__"))


class PreToolUse(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()

    def tearDown(self):
        gf.remove(self.root)

    def test_commit_on_a_failing_register_is_blocked(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m 'x'"))
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1: owner: missing", p.stderr)
        self.assertEqual("", p.stdout)

    def test_push_with_options_is_gated_too(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        p = hook("pre_tool_use.py", bash_event(self.root, "cd . && git -C . --no-pager push origin HEAD"))
        self.assertEqual(2, p.returncode, p.stderr)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stderr)

    def test_commit_on_a_clean_register_passes_silently(self):
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m 'x'"))
        self.assertEqual((0, "", ""), (p.returncode, p.stdout, p.stderr))

    def test_ungated_commands_and_tools_pass_even_on_a_failing_register(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        for cmd in ("git status", "git log --grep commit", "gh pr view", "ls"):
            p = hook("pre_tool_use.py", bash_event(self.root, cmd))
            self.assertEqual(0, p.returncode, (cmd, p.stderr))
        agent = dict(bash_event(self.root, ""), tool_name="Agent",
                     tool_input={"prompt": "p", "description": "d", "subagent_type": "general-purpose"})
        self.assertEqual(0, hook("pre_tool_use.py", agent).returncode)

    def test_hook_fails_closed_when_script_errors(self):
        broken = os.path.join(self.root, "broken")
        os.makedirs(os.path.join(broken, "tools", "gates", "hooks"))
        shutil.copy(os.path.join(HOOKS, "pre_tool_use.py"), os.path.join(broken, "tools", "gates", "hooks"))
        gf.write(broken, "tools/gates/__init__.py", "")
        gf.write(broken, "tools/gates/run_all.py", "def run(names, args):\n    raise RuntimeError('gate exploded')\n")
        p = hook("pre_tool_use.py", bash_event(self.root, "git commit -m x"),
                 hooks_dir=os.path.join(broken, "tools", "gates", "hooks"))
        self.assertEqual(2, p.returncode)
        self.assertEqual("FAIL hook.internal: RuntimeError: gate exploded\n", p.stderr)

    def test_malformed_stdin_fails_closed(self):
        for raw in ("", "not json", "[1, 2]"):
            p = hook("pre_tool_use.py", raw)
            self.assertEqual(2, p.returncode, raw)
            self.assertTrue(p.stderr.startswith("FAIL hook.internal: "), p.stderr)

    def test_cwd_outside_a_repo_fails_closed(self):
        outside = os.path.dirname(self.root)
        p = hook("pre_tool_use.py", bash_event(outside, "git commit -m x"))
        self.assertEqual(2, p.returncode)
        self.assertIn("FAIL hook.internal: RuntimeError: git -C", p.stderr)


class GitHooks(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        copy_gates(self.root)
        gf.commit_all(self.root, "gates")
        p = subprocess.run(["bash", "tools/gates/install-hooks.sh"], cwd=self.root,
                           capture_output=True, text=True, check=False)
        self.assertEqual(0, p.returncode, p.stderr)
        self.hook = os.path.join(self.root, ".git", "hooks", "pre-commit")

    def tearDown(self):
        gf.remove(self.root)

    def commit(self):
        env = {k: v for k, v in os.environ.items() if k != "CLAUDE_PROJECT_DIR"}
        env["CLAUDE_PROJECT_DIR"] = "/nonexistent-project"   # the hook must ignore it
        return subprocess.run(["git", "-c", "user.name=t", "-c", "user.email=t@t", "-c", "commit.gpgsign=false",
                               "commit", "-q", "-m", "x"], cwd=self.root, capture_output=True, text=True,
                              env=env, check=False)

    def test_install_writes_the_marked_shim_and_is_idempotent(self):
        with open(self.hook) as fh:
            first = fh.read()
        self.assertIn("# strict-delivery-gates-hook", first)
        p = subprocess.run(["bash", "tools/gates/install-hooks.sh"], cwd=self.root,
                           capture_output=True, text=True, check=False)
        self.assertEqual(0, p.returncode)
        self.assertNotIn("moved the existing", p.stdout)
        with open(self.hook) as fh:
            self.assertEqual(first, fh.read())

    def test_a_foreign_hook_is_moved_aside(self):
        gf.write(self.root, ".git/hooks/pre-commit", "#!/bin/sh\necho mine\n")
        p = subprocess.run(["bash", "tools/gates/install-hooks.sh"], cwd=self.root,
                           capture_output=True, text=True, check=False)
        self.assertIn("moved the existing pre-commit hook", p.stdout)
        with open(self.hook + ".pre-strict-delivery") as fh:
            self.assertEqual("#!/bin/sh\necho mine\n", fh.read())

    def test_pre_commit_blocks_a_failing_register(self):
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)
        gf.git(self.root, "add", "-A")
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL register.owner.T1-ORPHAN-1", p.stdout + p.stderr)

    def test_pre_commit_blocks_unstaged_register_edits(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        gf.write(self.root, os.path.join(gf.STATE, "findings.yml"), ORPHAN)   # not staged
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.unstaged", p.stderr)

    def test_a_tree_without_the_gates_cannot_commit(self):
        gf.git(self.root, "rm", "-r", "-q", "tools")
        p = self.commit()
        self.assertNotEqual(0, p.returncode)
        self.assertIn("FAIL pre-commit.missing", p.stderr)

    def test_pre_commit_passes_a_clean_tree(self):
        gf.write(self.root, "x.txt", "x\n")
        gf.git(self.root, "add", "x.txt")
        p = self.commit()
        self.assertEqual(0, p.returncode, p.stdout + p.stderr)


if __name__ == "__main__":
    unittest.main()
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_hooks -v`
Expected: FAIL/ERROR. `pre_tool_use.py` does not exist, so the subprocess exits 2 with `can't open file`, and the `GitHooks.setUp` assertion fails because `install-hooks.sh` is missing.

- [ ] **Step 3: Write the hook scripts**

```bash
mkdir -p tools/gates/hooks
```

`tools/gates/hooks/pre_tool_use.py`:

```python
#!/usr/bin/env python3
"""Claude Code PreToolUse hook (spec §8; umbrella plan, "Hook protocol").

Reads one JSON event on stdin. At strict-delivery block b3 only the Bash branch gates:
`git commit` and `git push` run the register and claim gates on the git toplevel of the event's
`cwd`; every other command, and every other tool (Agent included, until S2 adds gate_dispatch),
exits 0 silently. A failing gate exits 2 with the gate's lines on stderr; any internal error also
exits 2 (fail closed). Limits: a commit run in another tree via `cd <dir> && git commit` is gated
here against the session's cwd -- the git pre-commit hook gates the tree actually committed.
"""
from __future__ import annotations

import json
import os
import re
import subprocess
import sys

CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
GIT_VERB = re.compile(r"\bgit\b(?:\s+(?:-[Cc]\s+\S+|--?[A-Za-z][\w-]*(?:=\S+)?))*\s+(commit|push)\b")
GATED = {"commit": ["register", "claims"], "push": ["register", "claims"]}


def gates_for(event: dict) -> list:
    """Gate names an event needs, in order, without duplicates; [] = not gated."""
    if event.get("tool_name") != "Bash":
        return []
    command = (event.get("tool_input") or {}).get("command")
    if not isinstance(command, str):
        raise ValueError("Bash event without tool_input.command")
    names = []
    for m in GIT_VERB.finditer(command):
        for n in GATED[m.group(1)]:
            if n not in names:
                names.append(n)
    return names


def project_root(event: dict) -> str:
    cwd = event.get("cwd") or os.getcwd()
    p = subprocess.run(["git", "-C", cwd, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git -C {cwd} rev-parse --show-toplevel: {p.stderr.strip()}")
    return p.stdout.strip()


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    names = gates_for(event)
    if not names:
        return 0
    sys.path.insert(0, CODE_ROOT)
    from tools.gates import run_all   # lazy: an ungated command never needs PyYAML
    result = run_all.run(names, {"root": project_root(event)})
    if result.ok():
        return 0
    print(result.render(False), file=sys.stderr)
    return 2


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed: a broken hook blocks, it never waves through
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
```

`tools/gates/hooks/git-pre-commit.sh`:

```bash
#!/bin/sh
# strict-delivery-gates-hook
# git pre-commit (spec §8 "Git hooks"): the register and claim gates on the tree being committed.
# The gates read the working tree, so the registers must have no unstaged edits -- then what is
# checked is exactly what is committed. Installed by tools/gates/install-hooks.sh.
set -eu
unset CLAUDE_PROJECT_DIR   # gate the tree being committed (e.g. a worktree), not the session's project
root=$(git rev-parse --show-toplevel)
cd "$root"
if ! git diff --quiet -- docs/superpowers/state; then
  echo "FAIL pre-commit.unstaged: docs/superpowers/state has unstaged edits; stage them (git add docs/superpowers/state) or revert them" >&2
  exit 1
fi
exec python3 tools/gates/run_all.py --only register,claims
```

`tools/gates/install-hooks.sh`:

```bash
#!/usr/bin/env bash
# Installs the strict-delivery git hooks (spec §8 "Git hooks"). Idempotent. The installed
# pre-commit is a shim carrying the marker line below; it runs the TRACKED
# tools/gates/hooks/git-pre-commit.sh of whichever tree is being committed, so an update to the
# script needs no re-install. A foreign pre-commit is moved aside, never overwritten.
set -euo pipefail
MARKER='# strict-delivery-gates-hook'
root=$(git rev-parse --show-toplevel)
hooks=$(git -C "$root" rev-parse --git-path hooks)
case "$hooks" in /*) ;; *) hooks="$root/$hooks" ;; esac
mkdir -p "$hooks"
dst="$hooks/pre-commit"
if [ -e "$dst" ] && ! grep -qF "$MARKER" "$dst"; then
  mv "$dst" "$dst.pre-strict-delivery"
  echo "install-hooks: moved the existing pre-commit hook to $dst.pre-strict-delivery (it no longer runs)"
fi
cat > "$dst" <<'SHIM'
#!/bin/sh
# strict-delivery-gates-hook
# Installed by tools/gates/install-hooks.sh. Runs the tracked script of the tree being committed;
# a tree without it cannot commit (fail closed).
script="$(git rev-parse --show-toplevel)/tools/gates/hooks/git-pre-commit.sh"
if [ ! -f "$script" ]; then
  echo "FAIL pre-commit.missing: this tree has no tools/gates/hooks/git-pre-commit.sh (it predates the strict-delivery gates); rebase it onto main or ask the owner" >&2
  exit 1
fi
exec sh "$script" "$@"
SHIM
chmod +x "$dst"
echo "install-hooks: pre-commit -> tools/gates/hooks/git-pre-commit.sh ($dst)"
```

```bash
chmod +x tools/gates/hooks/pre_tool_use.py tools/gates/hooks/git-pre-commit.sh tools/gates/install-hooks.sh
```

- [ ] **Step 4: Run the tests to verify they pass, then install the hook in this checkout**

```bash
python3 -m unittest tools.gates.tests.test_hooks -v   # Ran 13 tests ... OK
python3 -m unittest discover -s tools/gates/tests -t .   # Ran 211 tests ... OK
bash tools/gates/install-hooks.sh                    # install-hooks: pre-commit -> ... (.git/hooks/pre-commit)
grep -c 'strict-delivery-gates-hook' .git/hooks/pre-commit   # 1
```

Mutation check: remove the `try/except` wrapper around `main()` in `pre_tool_use.py` (call `sys.exit(main())`). `test_hook_fails_closed_when_script_errors` must fail (rc 1 with a traceback, not 2). Restore. Then delete the `unset CLAUDE_PROJECT_DIR` line from `git-pre-commit.sh`. `test_pre_commit_blocks_a_failing_register` must fail: the gates read `/nonexistent-project`, and `FAIL register.input` is printed instead of `register.owner`. Restore and run again: OK.

- [ ] **Step 5: Ledger line, then commit (the new pre-commit hook gates this commit)**

Append `- T7 feat(gates): PreToolUse Bash gating, git pre-commit, install-hooks -- test_hooks 13 OK` to the ledger under `## Block b3 (branch block/strict-delivery-b3)` with `- base BASE_B3=<sha>`.

```bash
git add tools/gates/hooks tools/gates/install-hooks.sh tools/gates/tests/test_hooks.py docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): PreToolUse gating of git commit/push, git pre-commit hook, install-hooks.sh" \
  -m "Strict-delivery S1 Task 7 (spec §8; umbrella Hook protocol). Fail closed on any hook error; the git hook gates the tree being committed, refuses unstaged register edits and blocks trees that predate the gates."
```

Expected: the commit output first shows the 10 PASS lines of `run_all --only register,claims` from the pre-commit hook.

### Task 8: SessionStart and UserPromptSubmit hooks, `.claude/settings.json`, `CLAUDE.md`

**Files:**
- Create: `tools/gates/hooks/session_start.py`, `tools/gates/hooks/user_prompt_submit.py`, `.claude/settings.json`, `CLAUDE.md`
- Modify: `tools/gates/tests/test_hooks.py` (insert three classes above the `if __name__ == "__main__":` line), `tools/gates/README.md` (append the Hooks section)

**Interfaces:**
- Consumes: `install-hooks.sh` and its marker (Task 7); `hook()`, `copy_gates()` and `ORPHAN` in `test_hooks.py` (Task 7).
- Produces: `session_start.py` prints `strict-delivery: gates installed in <root> ...` or `strict-delivery: gates NOT installed in <root> ...`, one `strict-delivery: handoff <path> ...` line per `docs/superpowers/state/*/handoff.md`, the install-hooks output when the pre-commit is not ours, and `strict-delivery: invoke the strict-delivery skill before any other action`. Exit 2 on install failure (`FAIL hook.install: ...`) or internal error. `user_prompt_submit.py` prints one line starting `strict-delivery: `. `.claude/settings.json` is the umbrella's JSON without the `Stop` and `SubagentStop` entries. **S2 adds those two entries, together with `stop.py` and `subagent_stop.py`.** Until then a `Stop` entry would call a script that does not exist, and fail closed on every turn.

- [ ] **Step 1: Write the failing tests**

Insert into `tools/gates/tests/test_hooks.py`, directly above `if __name__ == "__main__":`:

```python
class SessionStart(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()

    def tearDown(self):
        gf.remove(self.root)

    def event(self):
        return {"session_id": "s", "transcript_path": "/dev/null", "cwd": self.root,
                "hook_event_name": "SessionStart", "source": "startup"}

    def test_reports_missing_gates(self):
        p = hook("session_start.py", self.event())
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("strict-delivery: gates NOT installed in", p.stdout)

    def test_installs_git_hooks_and_lists_handoffs(self):
        copy_gates(self.root)
        gf.write(self.root, "docs/superpowers/state/demo/handoff.md", "---\nplan: x\n---\n")
        p = hook("session_start.py", self.event())
        self.assertEqual(0, p.returncode, p.stderr)
        self.assertIn("strict-delivery: gates installed in", p.stdout)
        self.assertIn("strict-delivery: handoff docs/superpowers/state/demo/handoff.md", p.stdout)
        self.assertIn("install-hooks: pre-commit ->", p.stdout)
        with open(os.path.join(self.root, ".git", "hooks", "pre-commit")) as fh:
            self.assertIn("# strict-delivery-gates-hook", fh.read())
        p = hook("session_start.py", self.event())          # second start: already ours
        self.assertNotIn("install-hooks:", p.stdout)
        self.assertIn("invoke the strict-delivery skill", p.stdout)

    def test_install_failure_exits_2(self):
        copy_gates(self.root)
        gf.write(self.root, "tools/gates/install-hooks.sh", "echo boom >&2; exit 3\n")
        p = hook("session_start.py", self.event())
        self.assertEqual(2, p.returncode)
        self.assertIn("FAIL hook.install: install-hooks.sh exit 3: boom", p.stderr)


class UserPromptSubmit(unittest.TestCase):
    def test_one_line_reminder(self):
        p = hook("user_prompt_submit.py", {"hook_event_name": "UserPromptSubmit", "prompt": "hi", "cwd": "/"})
        self.assertEqual(0, p.returncode)
        self.assertEqual(1, len(p.stdout.splitlines()))
        self.assertTrue(p.stdout.startswith("strict-delivery: "))

    def test_malformed_stdin_fails_closed(self):
        p = hook("user_prompt_submit.py", "nope")
        self.assertEqual(2, p.returncode)


class ProjectWiring(unittest.TestCase):
    def test_settings_json_wires_existing_hook_scripts_without_stop_hooks(self):
        with open(os.path.join(gf.REPO, ".claude", "settings.json")) as fh:
            hooks = json.load(fh)["hooks"]
        self.assertEqual({"PreToolUse", "SessionStart", "UserPromptSubmit"}, set(hooks))  # Stop/SubagentStop: S2
        self.assertEqual(["Agent", "Bash"], [m["matcher"] for m in hooks["PreToolUse"]])
        commands = [h["command"] for groups in hooks.values() for g in groups for h in g["hooks"]]
        self.assertEqual(5, len(commands))
        for c in commands:
            script = c.split('"')[1].replace("${CLAUDE_PROJECT_DIR}", gf.REPO)
            self.assertTrue(os.path.isfile(script), c)

    def test_claude_md_carries_spec_section_12_verbatim(self):
        with open(os.path.join(gf.REPO, "docs/superpowers/specs/2026-10-17-strict-delivery-design.md")) as fh:
            spec = fh.read()
        section = spec.split("## 12. ", 1)[1].split("\n## 13. ", 1)[0]
        bullets = [l for l in section.splitlines() if l.startswith("- ")]
        self.assertEqual(11, len(bullets))
        with open(os.path.join(gf.REPO, "CLAUDE.md")) as fh:
            claude = fh.read().splitlines()
        for b in bullets:
            self.assertIn(b, claude)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `python3 -m unittest tools.gates.tests.test_hooks -v`
Expected: the 7 new tests FAIL/ERROR (`can't open file '.../session_start.py'`, `FileNotFoundError: .../.claude/settings.json`, `.../CLAUDE.md`). The 13 Task 7 tests still pass.

- [ ] **Step 3: Write the hooks, the settings and CLAUDE.md**

`tools/gates/hooks/session_start.py`:

```python
#!/usr/bin/env python3
"""Claude Code SessionStart hook (spec §8). Prints to stdout (injected as context): whether the
gates are installed in the event's project, every handoff document, and the skill instruction;
installs the git hooks when .git/hooks/pre-commit is not ours. Exit 2 (shown to the owner) when
the install fails or the hook itself breaks."""
from __future__ import annotations

import glob
import json
import os
import subprocess
import sys

MARKER = "# strict-delivery-gates-hook"


def toplevel(cwd: str) -> str:
    p = subprocess.run(["git", "-C", cwd, "rev-parse", "--show-toplevel"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git -C {cwd} rev-parse --show-toplevel: {p.stderr.strip()}")
    return p.stdout.strip()


def pre_commit_path(root: str) -> str:
    p = subprocess.run(["git", "-C", root, "rev-parse", "--git-path", "hooks/pre-commit"],
                       capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise RuntimeError(f"git rev-parse --git-path hooks/pre-commit: {p.stderr.strip()}")
    return os.path.join(root, p.stdout.strip())


def ours(path: str) -> bool:
    try:
        with open(path, encoding="utf-8") as fh:
            return MARKER in fh.read()
    except OSError:
        return False


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    root = toplevel(event.get("cwd") or os.getcwd())
    if not os.path.isfile(os.path.join(root, "tools", "gates", "run_all.py")):
        print(f"strict-delivery: gates NOT installed in {root} (no tools/gates/run_all.py); apply the "
              "skill's judgment rules and state that the gates are missing")
        return 0
    try:
        import yaml  # noqa: F401
        yaml_state = "PyYAML ok"
    except ImportError:
        yaml_state = ("PyYAML MISSING for this python3: python3 -m pip install PyYAML==6.0.2 "
                      "(every gated git commit/push fails closed until then)")
    print(f"strict-delivery: gates installed in {root} (tools/gates/run_all.py); {yaml_state}")
    for h in sorted(glob.glob(os.path.join(root, "docs", "superpowers", "state", "*", "handoff.md"))):
        print(f"strict-delivery: handoff {os.path.relpath(h, root)} -- read it before any dispatch")
    rc = 0
    if not ours(pre_commit_path(root)):
        p = subprocess.run(["bash", os.path.join(root, "tools", "gates", "install-hooks.sh")],
                           cwd=root, capture_output=True, text=True, check=False)
        print(p.stdout.strip())
        if p.returncode != 0:
            print(f"FAIL hook.install: install-hooks.sh exit {p.returncode}: {p.stderr.strip()}", file=sys.stderr)
            rc = 2
    print("strict-delivery: invoke the strict-delivery skill before any other action")
    return rc


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
```

`tools/gates/hooks/user_prompt_submit.py`:

```python
#!/usr/bin/env python3
"""Claude Code UserPromptSubmit hook (spec §8): a one-line reminder on stdout, nothing else.
Stdlib only, so it cannot fail for a missing package; a malformed event still fails closed."""
from __future__ import annotations

import json
import sys

REMINDER = ("strict-delivery: every change goes through the pipeline (CLAUDE.md, tools/gates/); verify "
            "every claim directly and by contradiction; owner-only decisions go to "
            "docs/superpowers/state/decisions.yml as questions, then stop.")


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    print(REMINDER)
    return 0


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
```

`.claude/settings.json`: the umbrella's JSON verbatim, minus the `"Stop"` and `"SubagentStop"` lines, which S2 adds:

```json
{"hooks": {
  "PreToolUse": [
    {"matcher": "Agent", "hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py\"", "timeout": 300}]},
    {"matcher": "Bash", "hooks": [{"type": "command", "if": "Bash(git *)", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py\"", "timeout": 900},
                                  {"type": "command", "if": "Bash(gh *)",  "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py\"", "timeout": 900}]}
  ],
  "SessionStart": [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/session_start.py\""}]}],
  "UserPromptSubmit": [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/user_prompt_submit.py\""}]}]
}}
```

Check: `diff <(sed -n '/^{"hooks": {/,/^}}/p' docs/superpowers/plans/2026-10-17-strict-delivery.md | grep -v -e '"Stop":' -e '"SubagentStop":') .claude/settings.json` prints nothing.

`CLAUDE.md`. The bullets are spec §12 verbatim. Generate them rather than retyping:

```bash
{ printf '# Project instructions (strict delivery)\n\nThese override superpowers and every other skill. Source: `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` §12; skill: `strict-delivery` (`~/.claude/skills/strict-delivery/SKILL.md`).\n\n'
  sed -n '/^## 12\. /,/^## 13\. /p' docs/superpowers/specs/2026-10-17-strict-delivery-design.md | grep '^- '; } > CLAUDE.md
```

Expected `CLAUDE.md`:

```markdown
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
```

```bash
chmod +x tools/gates/hooks/session_start.py tools/gates/hooks/user_prompt_submit.py
```

Append to `tools/gates/README.md`:

````markdown

## Hooks

`.claude/settings.json` wires `hooks/pre_tool_use.py` (Bash: `git commit` / `git push` run
register + claims), `hooks/session_start.py` and `hooks/user_prompt_submit.py`. Every hook exits 2
with the failing lines on stderr, and exits 2 on any internal error (fail closed).
`install-hooks.sh` installs the git `pre-commit` shim (marker `# strict-delivery-gates-hook`),
which runs `hooks/git-pre-commit.sh` of the tree being committed; SessionStart installs it when
missing.
````

- [ ] **Step 4: Run the tests to verify they pass**

```bash
python3 -m unittest tools.gates.tests.test_hooks -v      # Ran 20 tests ... OK
python3 -m unittest discover -s tools/gates/tests -t .   # Ran 218 tests ... OK
python3 tools/gates/run_all.py                            # 15 PASS
echo '{"cwd": "'"$PWD"'", "hook_event_name": "SessionStart", "source": "startup"}' | python3 tools/gates/hooks/session_start.py
git add -A && git diff --cached --check && echo whitespace-clean
```

Mutation check: add `"Stop": [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/stop.py\""}]}],` to `.claude/settings.json`. `test_settings_json_wires_existing_hook_scripts_without_stop_hooks` must fail. Restore.

- [ ] **Step 5: Ledger line, then commit**

Append `- T8 feat(gates): SessionStart/UserPromptSubmit hooks, .claude/settings.json, CLAUDE.md -- test_hooks 20 OK; suite 218 OK` to the ledger.

```bash
git add tools/gates/hooks tools/gates/tests/test_hooks.py tools/gates/README.md .claude/settings.json CLAUDE.md docs/superpowers/state/strict-delivery/ledger.md
git commit -m "feat(gates): session hooks, committed .claude/settings.json, CLAUDE.md overrides" \
  -m "Strict-delivery S1 Task 8 (spec §8, §12). settings.json = the umbrella's hooks without Stop/SubagentStop (S2 adds them with their scripts). CLAUDE.md carries spec §12 verbatim, pinned by a test."
```

The owner restarts the Claude Code session after this block merges: hooks in a new `.claude/settings.json` load at session start.

### Task 9: The `strict-delivery` skill skeleton and the eight prompt blocks

**Files (outside the repository; personal skill, spec D1):**
- Create: `~/.claude/skills/strict-delivery/SKILL.md`
- Create: `~/.claude/skills/strict-delivery/prompts/{controller,implementer,reviewer,verifier,exception-reviewer,investigator,audit-reviewer,audit-verifier}.md`
- Modify (repo): `docs/superpowers/state/strict-delivery/ledger.md` (sha256 of each file, so later drift is visible)

**Interfaces:**
- Consumes: the dispatch grammar and role names in the umbrella; `tools/gates/config.yml` `models`.
- Produces: the prompt files that S2's `gate_dispatch` compares byte for byte against each dispatch's `## Strict block`. Every file is UTF-8 with a single trailing newline. The dispatch cap is 12,000 characters (`dispatch.max_prompt_chars`), so each block stays at or under 4,000 bytes. Each block states: the verification method (direct + contradiction), the zero-bug rule, no subagents (the controller instead: the only dispatcher, with dispatch discipline), the report contract with the `status:` first line, budget obedience, and the owner-only stop rule. Reviewer and verifier blocks also state: execute every claim, re-run every gate, re-run the mutation, sibling check, and a contradiction pass on the new surface. Role → file: controller/sub-controller → `controller.md`; implementer and implementer_transcription → `implementer.md`; task_reviewer and final_reviewer → `reviewer.md`; final_verifier → `verifier.md`; exception_reviewer → `exception-reviewer.md`; investigator, audit_reviewer, audit_verifier → the files of the same names.

- [ ] **Step 1: Write the acceptance check first (it fails: the files do not exist)**

Save as `.superpowers/sdd/strict-delivery/check-skill.sh` (gitignored scratch):

```bash
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
  n=$(wc -c < "$f"); [ "$n" -le 4000 ] || { echo "$r.md is $n bytes (> 4000)"; bad=1; }
  [ "$(tail -c 1 "$f" | od -An -c | tr -d ' ')" = '\n' ] || { echo "$r.md: no trailing newline"; bad=1; }
  for p in "Verification method." "by contradiction" "Zero bugs." "Owner-only decisions." "NEEDS_DECISION" "Budget." "Report contract." 'first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`'; do need "$f" "$p"; done
  if [ "$r" = controller ]; then need "$f" "You are the only role that dispatches."; else need "$f" "No subagents."; fi
  case "$r" in reviewer|verifier)
    for p in "Execute, never trust." "Re-run every gate" "Re-run each mutation transcript" "Sibling check" "Contradiction pass on the new surface"; do need "$f" "$p"; done ;;
  esac
done
[ "$bad" = 0 ] && echo "skill skeleton OK"
exit "$bad"
```

Run: `bash .superpowers/sdd/strict-delivery/check-skill.sh`
Expected: `MISSING /Users/benji/.claude/skills/strict-delivery/SKILL.md`, exit 1.

- [ ] **Step 2: Write `SKILL.md`**

```bash
mkdir -p ~/.claude/skills/strict-delivery/prompts
```

`~/.claude/skills/strict-delivery/SKILL.md`:

```markdown
---
name: strict-delivery
description: Use at the start of every session and before every change of any size (code, tests, tools, CI, docs, spec, plans, bench doc, memory notes, PR bodies, issue text) - roles, decision classes, verification by direct check and contradiction, dispatch contract, pipeline and gates. Overrides superpowers.
---

# Strict delivery

Skeleton installed by strict-delivery S1 block b3; the final text lands in S5 block b13 after the
pressure scenarios. Spec: `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` in the
lap-timer repository.

## When it applies

Every change, any size, by any agent or by the owner through an agent. A one-line fix walks the
same pipeline; only the work inside each step scales. There is no size threshold and no
"too small for the process". In a repository without `tools/gates/run_all.py` apply every
judgment rule below and say, in the first reply, that the gates are missing.

## Precedence

1. The project's `CLAUDE.md` (it names the gate that enforces each override).
2. This skill and its prompt blocks in `prompts/`.
3. Superpowers, unmodified. Where it says otherwise (for example its reviewer's "do not re-run
   the suite"), the higher layer wins.

## Roles and models

| Role | Minimum model | Lifetime | Prompt block |
|---|---|---|---|
| Session / sub-controller | opus | one block per context | `prompts/controller.md` |
| Implementer | sonnet (haiku only when the brief carries complete code) | one task | `prompts/implementer.md` |
| Task reviewer | opus | one review | `prompts/reviewer.md` |
| Final branch reviewer | opus | one branch | `prompts/reviewer.md` |
| Final adversarial verifier | fable | one branch | `prompts/verifier.md` |
| Exception (P10) reviewer | fable | one row | `prompts/exception-reviewer.md` |
| Audit reviewer | opus | one area | `prompts/audit-reviewer.md` |
| Audit verifier | opus | one area | `prompts/audit-verifier.md` |
| Investigator (read-only) | opus | one question | `prompts/investigator.md` |

The machine-read copy is `tools/gates/config.yml` (`models`, `model_rank`, `fable_roles`). Fable
is used for the two roles above and nothing else. Only a controller dispatches. Every dispatch
appends the role's prompt block verbatim under `## Strict block`, per the dispatch grammar in the
umbrella plan `docs/superpowers/plans/2026-10-17-strict-delivery.md`.

## Decision classes

Owner-only, always: audit triage buckets; spec-vs-code direction; plan scope (what enters, what
defers); merge grants; anything touching hardware; model and cost policy; closing an issue; any
ruling that narrows or widens a finding's class. Each is written into
`docs/superpowers/state/decisions.yml` as a question with options and cost, and the path halts
until the owner answers (`answered_at.by` = the owner handle). Nothing may reference an
unanswered id (`gate_decisions`).

Controller rulings: only inside an approved plan, only plan-vs-spec or task-vs-task conflicts
the spec settles, at most 5 per block, ledgered as
`Ruling: what -- why -- cost if wrong -- spec cite`, listed at the checkpoint.

## Verification method

No claim in any document is trusted. Before acting on a claim, and before writing one, check it
twice: **direct** (run the command, open the file at the line, run the test) and **by
contradiction** (assume the claim is false, derive an observable consequence, run the check
that would show it). Record both commands with their output. A claim with no executable check is
not made; in a review it is a finding. Divergence: non-binding text is fixed inline with
evidence; spec-vs-code is an owner question; plan-vs-spec is corrected to the spec by the
controller and ledgered.

## Pipeline (spec §6) and the gate at each step

1. Intake: the change exists as a register row first (`gate_register`).
2. Claim verification: every claim row the work depends on is verified at HEAD (`gate_claims`).
3. Plan or micro-plan (`gate_plan`, from S2).
4. Dispatch through the hook (`gate_dispatch`, from S2; `gate_decisions` now).
5. Implement, TDD, production-path test, mutation transcript, full local gate run.
6. Task review, executed (`gate_review_package`, from S2).
7. Fix loop: rounds 1-3 resume the implementer, 4-5 a fresh agent one tier up; then an owner question.
8. Block end: final reviewer + Fable verifier, `gate_merge_ready` (from S4), merge on grant,
   cleanup, checkpoint (`gate_checkpoint`, from S2), stop for the owner's `/clear`.
9. Post-merge: the merged files become coverage rows for the next audit.

Run everything installed with `python3 tools/gates/run_all.py`; hooks and CI run the same
scripts.

## Judgment, not gated

- Whether a claim's contradiction check really tests the negation (the gate only sees that one exists).
- Whether a class statement is copied in full and a sibling list is complete.
- Whether a "minor" is in fact this change's bug (it is incomplete, never deferred).
- Whether a question is owner-only when its wording looks technical.
- Whether a reply recommends anything with a red line open (never).
- Refusing "continue" without the owner's `/clear` after a checkpoint.
```

- [ ] **Step 3: Write the eight prompt blocks (full text; S2 compares them byte for byte)**

`~/.claude/skills/strict-delivery/prompts/controller.md`:

```markdown
# Strict block: controller

You control one block of an approved plan under the strict-delivery process. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Dispatch discipline. You are the only role that dispatches. Every dispatch follows the umbrella plan's dispatch grammar: ## Role, ## Brief, ## Findings, ## Claims, ## Budget, ## Model, ## Report, ## Strict block (the role's prompt file verbatim), ## Task; the model is at least the role's minimum in tools/gates/config.yml and Fable only for final_verifier and exception_reviewer; no session history is pasted. You never do a child's work after it reports BLOCKED or DENIED, and never ask a child to perform an action you were denied.

Rulings. Only inside the approved plan, only on plan-vs-spec or task-vs-task conflicts the spec settles, at most 5 per block, each ledgered as `Ruling: what -- why -- cost if wrong -- spec cite` and listed at the checkpoint. Everything else is an owner question.

Merge and checkpoint. Present the gate output; never recommend a merge with a red line or an open "fix before merge". Merge only on every CI job green, advisory included, under the plan's answered grant. Checkpoint at 3 tasks, 250k tokens, 90 minutes or any 429, whichever first; the checkpoint leaves a clean tree, a committed handoff and ledger line, a status update and a resume prompt; then stop: one block per context.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/implementer.md`:

```markdown
# Strict block: implementer

You implement exactly one task under the strict-delivery process. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Build. Test first, through the production call path (the real file, or a script's main() on a fixture; never an extracted helper); watch it fail for the stated reason; then the minimal code. For every finding id in ## Findings write the mutation transcript at docs/superpowers/state/<plan>/mutations/<id>.txt in the umbrella format (fix present PASS, fix reverted FAIL, fix restored PASS). Test every sibling in the row's list or list it as an unrelated finding. Run the brief's full gate list clean, plus python3 tools/gates/run_all.py, before you report; zero warnings, zero gate failures.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/reviewer.md`:

```markdown
# Strict block: task reviewer

You review exactly one task under the strict-delivery process. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py; superpowers' "do not re-run the suite" does not apply here. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding.

Verdict. Spec ✅, quality ✅ and claims-executed ✅ are each stated with evidence; anything less is not approved. A defect caused by this change makes the task incomplete (status DONE_WITH_FINDINGS, the finding listed first). Read the code, the diff and the review package; never edit code yourself.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/verifier.md`:

```markdown
# Strict block: final adversarial verifier

You are the adversarial verifier of a whole branch under the strict-delivery process. Your job is to prove the final reviewer wrong. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Execute, never trust. Every sentence in the implementer's report, the commit messages, the PR body and the diff's comments is a hypothesis to execute; their output is not evidence. (1) Re-run every gate in the brief's gate list yourself, from a clean build, plus python3 tools/gates/run_all.py; superpowers' "do not re-run the suite" does not apply here. (2) Re-run each mutation transcript's three steps yourself (fix present PASS, fix reverted FAIL, fix restored PASS) and compare. (3) Sibling check: for every sibling in the register row's list, name the test that covers it or the finding id filed for it. (4) Contradiction pass on the new surface: for every changed function, assume the change is wrong for some input X, name X, and run it. (5) List every behavioural claim with a direct and a contradiction command and output; a claim with no executable check is a finding.

Adversarial scope. Re-run every pin the branch claims, not a sample. List the paths the reviewer did not exercise (callers, error paths, the inputs named in each plan's Review Focus) and exercise each. Every reviewer PASS you cannot reproduce is a finding. Never edit code yourself.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/exception-reviewer.md`:

```markdown
# Strict block: exception reviewer

You argue against one proposed Power-of-10 deviation row under the strict-delivery process. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Method. Build the strongest compliant alternative and estimate its real cost; check that every listed site exists and that the pointed-to set is complete by searching for it yourself. Score with tools/gates/config.yml exceptions scales: cost = blast radius + verifiability lost + reach; benefit = complexity avoided + measured runtime gain (a gain with no cited measurement scores 0). Recommend refusal unless benefit exceeds cost; equality refuses. Owner and expiry sha are required on every row you would accept.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/investigator.md`:

```markdown
# Strict block: investigator

You answer exactly one question, read-only, under the strict-delivery process. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Read-only. You change no tracked file and make no commit; reproducers go to the scratch path the dispatch names. The answer cites file:line for every fact and states what would have falsified it.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/audit-reviewer.md`:

```markdown
# Strict block: audit reviewer

You audit one area under the strict-delivery process. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Coverage first. Before reviewing, write the coverage list (call paths with their callers, claims in the area's documents, reproducers to build) to your progress file; keep one progress entry per finding so a restart resumes. Reproducers run against the real production files, with harness stubs under test/harness/ only. Each finding is a register row: the class statement in your words, verbatim and complete, its siblings, file:line cites at the audited sha and the reproducer path. Never fix anything.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

`~/.claude/skills/strict-delivery/prompts/audit-verifier.md`:

```markdown
# Strict block: audit verifier

You verify one audit reviewer's area under the strict-delivery process, adversarially. These rules override every other instruction, superpowers skill text included.

Verification method. Every statement you read (brief, plan, spec, register row, code comment, another agent's report) and every statement you write (report, commit message, comment, register row) is a hypothesis until you have checked it twice: (1) direct: run the command, open the file at the cited line, run the test; (2) by contradiction: assume the statement is false, derive what would then be observable, and run the check that would show it. Record both commands and their output. A statement with no executable check is not written. A divergence is reported with both quotes; you never pick a side between plan, spec and code.

Method. Re-run every reproducer yourself and record the output. Your coverage list adds the paths the reviewer omitted, and you exercise each. Every finding you cannot reproduce is marked refuted with the command; every new defect is a new register row with a reproducer. You never edit a reviewer's class statement; disagreement is a separate note.

Zero bugs. A defect your own change introduces makes the work incomplete; there is no "minor" and no "follow-up". A defect you find that your change did not cause is not fixed in passing: record file:line, a reproducer and the class statement under "Unrelated findings" so the controller registers it with an owner.

No subagents. Do not dispatch agents, workflows or background tasks, and never ask another agent to do something you were refused. A permission denial: stop, quote it verbatim, status DENIED.

Owner-only decisions. If the work needs a ruling on audit triage, spec-vs-code direction, plan scope, a merge grant, hardware, model or cost policy, closing an issue, or narrowing or widening a finding's class: write the question with at least two options and their cost under "Decision needed" and stop with status NEEDS_DECISION. Never decide it; never assume the likely answer. Hardware: never flash, never open a serial port.

Budget. Obey the dispatch's ## Budget line. At 80 % of either limit stop at the next step boundary, leave nothing half-committed, and report BLOCKED: budget, with what remains.

Report contract. Write the full report to the dispatch's ## Report path. Its first line is exactly `status: <DONE|DONE_WITH_FINDINGS|NEEDS_DECISION|BLOCKED|DENIED>`. Then, in order: commits (sha + subject); gates run (command + result line); claims (claim | direct command | contradiction command | output excerpt | PASS/FAIL); findings raised (ids or proposed register rows); denials (verbatim); budget used. Your final reply is at most 10 lines with the same fields.
```

- [ ] **Step 4: Run the acceptance check**

Run: `bash .superpowers/sdd/strict-delivery/check-skill.sh && wc -c ~/.claude/skills/strict-delivery/prompts/*.md`
Expected: `skill skeleton OK`; sizes between 2,600 and 3,700 bytes each (prototype: audit-reviewer 2897, audit-verifier 2775, controller 3442, exception-reviewer 2949, implementer 2993, investigator 2611, reviewer 3584, verifier 3647).

Mutation check: delete the sentence starting `Sibling check:` from `verifier.md`. The check must print `MISSING in verifier.md: Sibling check` and exit 1. Restore and run again: OK.

- [ ] **Step 5: Ledger the hashes, then commit**

```bash
{ echo "- T9 skill skeleton + 8 prompt blocks at ~/.claude/skills/strict-delivery (outside the repo); sha256:"
  (cd ~/.claude/skills/strict-delivery && shasum -a 256 SKILL.md prompts/*.md | sed 's/^/  - /'); } >> docs/superpowers/state/strict-delivery/ledger.md
git add docs/superpowers/state/strict-delivery/ledger.md
git commit -m "docs(state): ledger the strict-delivery skill skeleton and prompt-block hashes" \
  -m "Strict-delivery S1 Task 9 (spec D1, §5 dispatch contract item 4, §12). The skill lives in ~/.claude/skills/strict-delivery (personal layer); the ledger records the sha256 of SKILL.md and each prompt block so a later edit is visible against gate_dispatch's byte comparison."
```

### CHECKPOINT b3

The umbrella's "Checkpoint protocol for this plan", verbatim:

> At every block end: run `python3 tools/gates/run_all.py`; dispatch final reviewer (opus) + final verifier (Fable) with the block's review packages; `gh pr ready`; wait for every check; merge with `gh pr merge --squash` only if `decisions.yml` has `grant:strict-delivery` answered `yes` by the owner; tag `strict-delivery-b<N>` on the squash commit and push the tag; delete branch + worktrees; write `docs/superpowers/state/strict-delivery/handoff.md`; append `CHECKPOINT b<N>` to the ledger; post the status update and the resume prompt; stop.

These steps carry it out by hand for block b3, because `gate_checkpoint` does not exist until b6. The one ordering change is umbrella Owner ruling 2: the handoff and the `CHECKPOINT` line are committed before the merge, and the post-merge facts are verified in step 8. The owner sees this in the status update.

- [ ] **Step 1: Gates at the block tip**

```bash
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py
git status --short     # empty
```

Every line PASS, `OK`. On any FAIL, stop: fix through the task's review loop before going on.

- [ ] **Step 2: Review packages (by hand; `gate_review_package` arrives in S2)**

For each task T in this block, with `B` = the commit before the task and `H` = the task's commit:

```bash
P=.superpowers/sdd/strict-delivery/review/b3/T<n>; mkdir -p "$P"
git log --oneline B..H > "$P/commits.txt"
git diff -U10 B..H > "$P/diff.patch"
{ python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py; } > "$P/gates.txt" 2>&1
```

Add `claims.md` (claim | direct command | contradiction | output excerpt | PASS/FAIL) for every behavioural claim in the task's commit message and ledger line. Add `mutation-<test>.txt` for each mutation check the task names, in the umbrella's three-step format. The package stays under the gitignored `.superpowers/` (Interface note 9).

- [ ] **Step 3: Owner items for this block**

(a) Tell the owner that the hooks in the new `.claude/settings.json` load only in a new Claude Code session. After the merge, the owner restarts; the SessionStart line `strict-delivery: gates installed ...` confirms it.

(b) Tell the owner that the eight prompt blocks are now the contract `gate_dispatch` compares byte for byte (S2). Any edit to them is ledgered with a new sha256.

(c) List this block's rulings (at most 5) for the owner.

- [ ] **Step 4: Final reviewer and final verifier**

Final verifier model: Fable (umbrella Owner ruling 9). Dispatch the final branch reviewer (opus) and then the final adversarial verifier on the whole branch `BASE_B3..HEAD`. Give each the review packages and these report paths: `.superpowers/sdd/strict-delivery/b3-final-review.md` and `.superpowers/sdd/strict-delivery/b3-final-verify.md`. Each dispatch appends `~/.claude/skills/strict-delivery/prompts/reviewer.md` or `prompts/verifier.md` verbatim under `## Strict block`, per the umbrella's dispatch grammar. Both must report `status: DONE` with no finding. Any finding goes through the fix loop (spec §6 step 7), then back to Step 1.

- [ ] **Step 5: Ledger CHECKPOINT line and handoff, committed on the branch**

```bash
TIP=$(git rev-parse --short=7 HEAD)     # the reviewed tip; recorded as `head`
```

Write `docs/superpowers/state/strict-delivery/handoff.md` (YAML front matter per the umbrella "Handoff"; values for this block):

```markdown
---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: main
head: "<TIP>"
base: "<BASE_B3>"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: <line number of this block's CHECKPOINT line in the ledger>
block: 3
next_task: "S2-b4"
findings_in_flight: []
claims_verified_at_head: []
live_subagents: []
open_decisions: [<every DEC id in decisions.yml that the owner has not answered>]
rulings: [<this block's Ruling lines, one each; at most 5>]
grant: DEC-0001
parked_branches: [<every local branch but main: {name, reason, owner}>]
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps: [<one line each, found in this block>]
---

Post-merge facts (verified in CHECKPOINT b3 step 8, not by gate_checkpoint): main's squash commit has parent `base` and the same tree as `head` plus this handoff commit.
```

Append to the ledger:

```markdown
CHECKPOINT b3 tip <TIP> -- gates: <the Step 1 summary, e.g. "suite N OK, M PASS">; rulings: <count>; final review + verify: clean
```

```bash
git add docs/superpowers/state/strict-delivery/handoff.md docs/superpowers/state/strict-delivery/ledger.md
git commit -m "docs(state): strict-delivery CHECKPOINT b3 handoff and ledger line"
FINAL=$(git rev-parse HEAD)            # the tree the squash must reproduce
git push -u origin block/strict-delivery-b3
```

- [ ] **Step 6: PR, every check green, grant, merge**

```bash
gh pr create --draft --base main --head block/strict-delivery-b3 --title "strict-delivery b3: hooks, CLAUDE.md, skill skeleton" --body-file .superpowers/sdd/strict-delivery/b3-pr-body.md
gh pr ready block/strict-delivery-b3
gh pr checks block/strict-delivery-b3 --watch          # every job, advisory (coverage) included, must pass
python3 -c "import sys; sys.path.insert(0, '.'); from tools.gates.lib import registers as r; d = r.grant_for(r.load_decisions('.'), 'strict-delivery'); print('GRANT', d['id']) if d else sys.exit('NO GRANT: stop, the owner answers DEC-0001 first')"
gh pr merge block/strict-delivery-b3 --squash
```

The PR body (`.superpowers/sdd/strict-delivery/b3-pr-body.md`) lists the tasks, the gate output from Step 1, both final report summaries, the owner items from Step 3 with their state, and Interface notes 8–10 where they apply. It contains none of `fix before merge`, `draft`, `parked`, `TODO`, `not for merge` (config `merge_markers`). If any check is red or the grant line says `NO GRANT`, stop. Never merge red, never recommend it.

- [ ] **Step 7: Tag and clean up**

```bash
git switch main && git pull --ff-only
SQUASH=$(git rev-parse HEAD)
git log -1 --format=%s "$SQUASH"                  # the PR title
git tag strict-delivery-b3 "$SQUASH" && git push origin strict-delivery-b3
git push origin --delete block/strict-delivery-b3 2>/dev/null || true   # gone already if GitHub auto-deleted it
git branch -D block/strict-delivery-b3
git worktree list                                  # main checkout only; else `git worktree remove <path>` each
```

- [ ] **Step 8: Verify the clean state and the post-merge facts**

```bash
test "$(git rev-parse --short=7 "$SQUASH^")" = "<BASE_B3>" && echo parent-ok   # main moved by exactly this squash
git diff --quiet "$FINAL" "$SQUASH" && echo tree-ok                            # squash == the reviewed branch
git status --porcelain | wc -l                                                 # 0
git branch --format='%(refname:short)'                                         # main, plus only the parked branches in the handoff
gh pr list --head block/strict-delivery-b3 --state open                           # empty
git tag --points-at "$SQUASH"                                                  # strict-delivery-b3
python3 -m unittest discover -s tools/gates/tests -t . && python3 tools/gates/run_all.py
```

Every line as stated. Otherwise the checkpoint is not reached: fix it, and do not post.

- [ ] **Step 9: Status update and resume prompt, then stop**

Post to the owner: done since the last checkpoint (task ids, commits, gate results at main); what remains in S1 and in the umbrella (S2–S5); open decisions; rulings made in this block; parked items; the owner items from Step 3. Then post the resume prompt verbatim and stop. Do not start the next block. The owner runs `/clear`.

```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block 4 (sub-plan S2, task 1) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```

---

## Self-review (run 2026-10-17 against the spec and the umbrella)

1. **Spec coverage, S1 rows.** Rollout step 1: library ✓ (T1), schemas ✓ (T2), `gate_register` ✓ (T3), `gate_claims` ✓ (T4), `gate_decisions` ✓ (T5), tests ✓, registers bootstrapped empty plus DEC-0001 ✓ (T1). Rollout step 2: hooks wired to the three gates (Claude Code ✓ T7–T8, git ✓ T7, CI job ✓ T6), `CLAUDE.md` ✓ (T8), skill skeleton with prompt blocks ✓ (T9). §7 common contract: exit 0/1 ✓, `--json` ✓, missing input FAIL ✓, shared lib ✓, fixtures that fail and pass ✓, gate tests a required CI job ✓ (T6 plus the owner step in CHECKPOINT b2). §4.1 gates: schema ✓, owner ✓, transitions + evidence ✓, "every id in reports" ✓ (T3, owner ruling 5 grammar). §4.2: schema ✓, stale marking ✓, false rows resolved ✓ (to a finding only; the umbrella schema has no doc-fix-commit alternative). §4.3: answered by owner ✓, references blocked ✓ (plans scanned by `run_all`; S2 adds dispatch prompts). §8 hooks: SessionStart ✓, UserPromptSubmit ✓, PreToolUse Bash for commit/push ✓. The remaining verbs, Agent, Stop and SubagentStop are S2/S4 per the umbrella.
2. **Placeholder scan.** No TBD/TODO/"similar to". The only fill-ins are runtime values: the sha recorded at setup (`BASE_B<N>`), and `__ASKED_SHA__`, which a command replaces in the same step.
3. **Type consistency.** `run(names, args)` uses `args["root"]` in T6, T7 and the tests. `verified_at_head(root, claim, head)` is defined in T4 for S2. Line-id categories match between the gates, `run_all`'s prefixing and every assertion. The marker string `# strict-delivery-gates-hook` is the same in install-hooks.sh, git-pre-commit.sh, session_start.py and the tests.
4. **Review Focus.** Five lines, each pinned by a named test in its owning task; the umbrella's four S1 rows are pinned under their umbrella names.
