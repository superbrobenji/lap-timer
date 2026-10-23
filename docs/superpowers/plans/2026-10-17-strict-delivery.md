# Strict Delivery Implementation Plan (umbrella)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This umbrella fixes the file structure, shared interfaces and block boundaries; the tasks live in the five sub-plans it names. Execute the sub-plans in order; each block is one PR.

**Goal:** Install the strict-delivery process (registers, gate scripts, hooks, CI, skill, workflows, reset checkpoints) so that every change to this repository is machine-gated for quality, function, spec and plan conformance before any product fix is planned.

**Architecture:** Three tracked YAML registers (findings, claims, decisions) are the only state. A Python gate library under `tools/gates/lib/` parses them and git/gh and exposes one `Gate` contract; thirteen gate scripts each check one concern and emit `PASS id` / `FAIL id: reason` lines with `--json`. Claude Code hooks, git hooks and one required CI job call the same scripts. A personal skill `~/.claude/skills/strict-delivery/` carries the judgment rules and the per-role prompt blocks; `CLAUDE.md` restates the overrides. Two Workflow scripts sequence the audit fan-out and the block loop.

**Tech Stack:** Python 3.9+ (stdlib + PyYAML 6, pinned in `tools/requirements.txt` and installed in CI), `python3 -m unittest` (repo convention: `tools/**/test_*.py` with `fixtures/` directories), bash for git hooks, Claude Code hooks (`.claude/settings.json`), GitHub Actions, Claude Code Workflow scripts (JavaScript, `.claude/workflows/`).

**Spec:** `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` (APPROVED 2026-10-17). Decisions D1–D23 in its §1 are binding; §11 is resolved.

## Global Constraints

- Every change, any size, goes through the pipeline (spec D20); this plan's own blocks are executed with the gates that exist at that point (spec §13 rollout).
- Block = PR, at most 3 tasks per block; a block ends in a clean tree (spec §3.4); one block per context; the session stops after each block for the owner's `/clear` (spec §3.5).
- Merge only on all-green (every CI job incl. advisory) under a per-plan grant entry `grant:<plan>` in `decisions.yml` (spec D22). The grant for this plan is decision `DEC-0001`, created in S1 Task 1 and answered by the owner before S1 Block 1 merges.
- Tag every merged block `<plan>-b<N>` (spec §11.2): this plan's blocks are `strict-delivery-b1` … `strict-delivery-b13`. Block branches are named `block/<plan>-b<N>` (e.g. `block/strict-delivery-b1`); a branch never shares a name with a tag.
- Model map (spec §5): controllers opus; implementers sonnet (haiku only for transcription of complete code); task reviewers opus; final branch reviewer opus; final adversarial verifier Fable; exception reviewer Fable (both opus while the DEC-0018 fallback is on). Never any other Fable use.
- Triggers (spec §3.1): controller 3 tasks / 250k tokens / 90 min; implementer 1 task / 150k; reviewer 1 review / 120k; any 429 → checkpoint at the task boundary.
- Zero warnings in every build; zero gate failures at every commit once the gate exists; every Python script exits non-zero on any failure path; a missing input is a FAIL never "nothing to do" (spec §7).
- Pinned = production-path test + mutation transcript (spec D7). For gate scripts "production path" = the script's `main()` invoked on a fixture, never a helper called in isolation.
- No claim trusted (spec D3): every plan-text claim about the repo (paths, line numbers, CI job names, hook fields) is verified by the implementer before use; a wrong claim is corrected in the plan and ledgered.
- Owner-only decisions (spec §5) stop the block with a decision entry; controller rulings max 5 per block, ledgered with cost-if-wrong.
- Hardware: nothing in this plan flashes or opens a serial port.
- Commit trailer: `Co-Authored-By: Claude Fable 5.1 <noreply@anthropic.com>` is NOT used by subagents; subagents use their own model's trailer; the squash commit carries the session's.

## Review Focus

1. **A gate that is skipped is worse than no gate.** Every hook and CI job must fail when its script is missing, errors, or receives no input (`FAIL gate: no input`); pinned by `tools/gates/tests/test_lib.py::test_missing_input_is_fail` (S1) and `test_hooks.py::test_hook_fails_closed_when_script_errors` (S1).
2. **A register edited by hand to the wrong state.** `gate_register` must reject an illegal transition even when the YAML is well-formed (e.g. `pinned` without transcript); pinned by `test_gate_register.py::test_pinned_without_transcript_fails` (S1).
3. **Stale claim rows that stay `verified`.** A commit touching a cited file must mark rows stale and the dispatch hook must block; pinned by `test_gate_claims.py::test_touching_cited_file_marks_stale` and `test_hooks.py::test_agent_dispatch_blocked_on_stale_claim` (S1, S2).
4. **A handoff that lies.** `gate_checkpoint` must fail when the handoff's HEAD sha, branch, worktree list or register ids disagree with git; pinned by `test_gate_checkpoint.py::test_sha_mismatch_fails` (S2).
5. **A merge with an advisory job red or a marker in the body.** `gate_merge_ready` must read every check run, not only required ones, and scan the body; pinned by `test_gate_merge_ready.py::test_advisory_red_fails` and `::test_body_marker_fails` (S4).

---

## File structure (locked here; sub-plans create exactly these)

```
CLAUDE.md                                   project overrides (spec §12)            S1
.claude/settings.json                       hooks config (committed)                S1
.claude/workflows/audit.js                  audit fan-out                           S4
.claude/workflows/block.js                  block loop                              S4
.github/workflows/gates.yml                 required CI job "gates"                 S1 (extended S2–S4)
docs/superpowers/state/README.md            what the registers are, how to edit     S1
docs/superpowers/state/findings.yml         findings register                       S1
docs/superpowers/state/claims.yml           claim register                          S1
docs/superpowers/state/decisions.yml        decisions (incl. grants)                S1
docs/superpowers/state/<plan>/ledger.md     per-plan ledger (SDD progress file)     S2
docs/superpowers/state/<plan>/handoff.md    checkpoint handoff                      S2
docs/superpowers/state/<plan>/mutations/    mutation transcripts <finding-id>.txt (TRACKED evidence) S2
.superpowers/sdd/<plan>/review/<task>/       review packages (git-ignored; archived at block end)   S2
tools/gates/README.md                       gate contract + how to run              S1
tools/gates/config.yml                      triggers, model map, scales, patterns   S1
tools/gates/lib/__init__.py                 Gate framework                          S1
tools/gates/lib/registers.py                loaders + schema validators             S1
tools/gates/lib/gitinfo.py                  git/gh helpers                          S1
tools/gates/lib/config.py                   config loader                           S1
tools/gates/gate_register.py                                                        S1
tools/gates/gate_claims.py                                                          S1
tools/gates/gate_decisions.py                                                       S1
tools/gates/gate_dispatch.py                                                        S2
tools/gates/gate_review_package.py                                                  S2
tools/gates/gate_plan.py                                                            S2
tools/gates/gate_checkpoint.py                                                      S2
tools/gates/gate_p10.py                     thin wrapper over tools/lint/power_of_10.py  S3
tools/gates/gate_comments.py                                                        S4
tools/gates/gate_state_docs.py                                                      S4
tools/gates/gate_bench.py                                                           S4
tools/gates/gate_merge_ready.py                                                     S4
tools/gates/gate_tools_py.py                                                        S4
tools/gates/run_all.py                      runs every gate, aggregates             S1 (grows)
tools/gates/hooks/pre_tool_use.py           Claude Code PreToolUse (Agent + Bash)   S1 (grows S2, S4)
tools/gates/hooks/stop.py                   Claude Code Stop                        S2
tools/gates/hooks/subagent_stop.py          Claude Code SubagentStop (report contract) S2
tools/gates/hooks/session_start.py          Claude Code SessionStart                S1
tools/gates/hooks/user_prompt_submit.py     reminder                                S1
tools/gates/hooks/git-pre-commit.sh         git hook                                S1
tools/gates/hooks/git-pre-push.sh           git hook                                S1 (b3, DEC-0005); S4 extends
tools/gates/install-hooks.sh                installs git hooks                      S1
tools/gates/postmerge.py                    cleanup + tag + state regen             S4
tools/gates/tests/test_*.py + fixtures/     one test module per gate/hook           S1–S4
tools/gates/skill-tests/                    pressure scenarios + transcripts        S5
tools/lint/power_of_10.py                   extended reach (rules 1,2,3,5,9 + all trees) S3
tools/lint/test_power_of_10.py + fixtures/  probe fixtures per rule                 S3
docs/power-of-10-deviations.md              two-tier columns; grace list removed    S3
~/.claude/skills/strict-delivery/SKILL.md   the skill                               S1 (skeleton), S5 (final)
~/.claude/skills/strict-delivery/prompts/{controller,implementer,reviewer,verifier,exception-reviewer,investigator,audit-reviewer,audit-verifier}.md   S1
~/.claude/skills/strict-delivery/checklists/{checkpoint,resume,block-end}.md        S2
```

## Shared interfaces (every sub-plan consumes these verbatim)

### Gate contract (`tools/gates/lib/__init__.py`)

```python
from dataclasses import dataclass, field, asdict
from typing import List, Callable, Optional
import json, sys, os

@dataclass
class Line:
    status: str   # "PASS" | "FAIL"
    id: str       # e.g. "register.owner.A2-IO-N2"
    reason: str = ""

@dataclass
class Result:
    gate: str
    lines: List[Line] = field(default_factory=list)
    def ok(self) -> bool: return all(l.status == "PASS" for l in self.lines) and bool(self.lines)
    def fail(self, id: str, reason: str) -> None: self.lines.append(Line("FAIL", id, reason))
    def passed(self, id: str) -> None: self.lines.append(Line("PASS", id))
    def render(self, as_json: bool) -> str:
        if as_json: return json.dumps({"gate": self.gate, "ok": self.ok(), "lines": [asdict(l) for l in self.lines]}, indent=1)
        return "\n".join(f"{l.status} {l.id}" + (f": {l.reason}" if l.reason else "") for l in self.lines)

class GateError(Exception):
    """Raised for a missing/unreadable input; main() converts it to FAIL <gate>.input."""

def repo_root() -> str:
    """CLAUDE_PROJECT_DIR if set, else `git rev-parse --show-toplevel`."""

def run_gate(name: str, check: Callable[[list], Result], argv: Optional[list] = None) -> int:
    """Parses --json and gate-specific args, runs check(args) -> Result, prints, returns 0 if ok else 1.
    Any exception (incl. GateError) becomes a FAIL line `<name>.input: <message>` and rc 1.
    An empty Result (no lines) is a FAIL `<name>.empty: gate produced no verdict` (fail closed)."""
```

Every gate module exposes `check(args: list) -> Result` and `def main(argv=None): return run_gate("<name>", check, argv)`; `if __name__ == "__main__": sys.exit(main())`. Tests call `main([...])` with `contextlib.redirect_stdout` exactly as `tools/lint/test_power_of_10.py` does.

### Registers (`tools/gates/lib/registers.py`)

```python
FINDING_STATES = ["open","planned","in-progress","fixed-unpinned","pinned","deferred","closed","merged"]
FINDING_TRANSITIONS = {  # from -> allowed to
  "open": ["planned","deferred","closed"], "planned": ["in-progress","open","deferred"],
  "in-progress": ["fixed-unpinned","pinned","planned"], "fixed-unpinned": ["pinned"],
  "pinned": ["closed","merged"], "deferred": ["open","planned"], "closed": ["merged"], "merged": [] }
CLAIM_STATES = ["unverified","verified","stale","false"]
DECISION_CLASSES = ["triage","spec-direction","scope","merge-grant","hardware","model-policy","issue-close","finding-class"]
OWNER_KINDS = ["plan","issue","roadmap"]

def load_findings(root) -> list[dict]   # raises GateError on missing/invalid YAML
def load_claims(root) -> list[dict]
def load_decisions(root) -> list[dict]
def validate_finding(f: dict) -> list[str]     # schema errors, [] if ok
def validate_claim(c: dict) -> list[str]
def validate_decision(d: dict) -> list[str]
def finding_by_id(findings, id) -> dict | None
def open_decisions(decisions) -> list[dict]     # answer is None
def grant_for(decisions, plan: str) -> dict | None   # class merge-grant, question starts "grant:<plan>", answered
```

Finding schema (all keys required; `pin`, `ruling` may be `null`): `id` (regex `^[A-Z0-9]+(-[A-Za-z0-9]+)+$`), `source`, `severity` ∈ {CRITICAL,HIGH,MEDIUM,LOW,INFO}, `class` (non-empty string), `siblings` (list, may be empty), `cites` (list of `{file, lines, sha}`), `repro` (string or null), `owner` (`{kind, plan, task}` | `{kind: issue, number}` | `{kind: roadmap, plan}`), `state`, `pin` (`{test, transcript}` or null), `ruling` (decision id or null), `evidence` (list of strings), `history` (list of `{state, sha, by}`; last entry's state == `state`).
Claim schema: `id` (`^C-[A-Za-z0-9.§-]+$`), `doc`, `anchor`, `text`, `direct`, `contra`, `cites` (list of paths), `verified_at` (sha or null), `by`, `state`, `finding` (required non-null iff state == false).
Decision schema: `id` (`^DEC-[0-9]{4}$`), `class`, `question`, `options` (list of `{label, cost}`, ≥2 unless class == merge-grant), `asked_at` (`{sha, by}`), `answer` (null or string), `answered_at` (null or `{sha, by}`; `by` must equal `config.owner.handle` when non-null).

### Config (`tools/gates/config.yml`, loaded by `lib/config.py::load(root) -> dict`)

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

### Git/gh helpers (`tools/gates/lib/gitinfo.py`)

```python
def head_sha(root) -> str
def branch(root) -> str
def status_clean(root) -> bool
def worktrees(root) -> list[str]            # paths from `git worktree list --porcelain`
def changed_files(root, base: str, head: str = "HEAD") -> list[str]
def staged_files(root) -> list[str]
def pr_for_branch(root) -> dict | None       # gh pr view --json number,title,body,state,isDraft,headRefOid,statusCheckRollup
def check_runs(pr: dict) -> list[dict]       # every check incl. non-required: name, conclusion
def tags_at(root, sha) -> list[str]
```
All subprocess calls use `subprocess.run([...], capture_output=True, text=True, check=False)`; a non-zero rc raises `GateError(f"{cmd}: {stderr}")`.

### Hook protocol (`tools/gates/hooks/*.py`) — verified against https://code.claude.com/docs/en/hooks.md on 2026-10-17

Claude Code hooks read one JSON object on stdin. Common fields: `session_id`, `transcript_path`, `cwd`, `permission_mode`, `hook_event_name`; when the call comes from a subagent also `agent_id`, `agent_type` (hooks from `.claude/settings.json` DO run inside subagents, so every subagent commit and dispatch is gated too). `PreToolUse` adds `tool_name`, `tool_input`, `tool_use_id`; `tool_input` for `Bash` has `command`, `description`; for `Agent` has `prompt`, `description`, `subagent_type`, `model` (optional alias). `Stop` adds `stop_hook_active` (true when Claude is already continuing because of a stop hook — the hook must exit 0 then, loop guard), `last_assistant_message`, `background_tasks`. `SubagentStop` adds `stop_hook_active`, `agent_id`, `agent_type`, `agent_transcript_path`, `last_assistant_message`. `SessionStart` adds `source` ∈ {startup, resume, clear, compact, fork}. `UserPromptSubmit` adds `prompt`.

Blocking: exit code 2 with the gate's rendered lines on stderr (Claude receives them as the reason). Exit 0 with no output = no decision (normal flow continues). `SessionStart` injects context by printing to stdout. Every hook wraps its body in `try/except Exception: print("FAIL hook.internal: <exc>", file=sys.stderr); sys.exit(2)` — hooks fail closed. `SessionStart` is the exception (owner ruling 5b, S1 b3): it cannot block, so it prints `FAIL hook.<id>: ...` on stdout, where the model sees it, and exits 0. Hooks call gate modules through `run_all.run(names, args) -> Result` so output is identical to the CLI. `CLAUDE_PROJECT_DIR` is the project root and stays fixed inside worktrees; hooks resolve the repo for the *current* `cwd` with `git rev-parse --show-toplevel` when gating a worktree.

Matcher: a value of letters/digits/`_-,| ` is an exact list; `"Agent|Bash"` matches exactly those two tools. The Bash handler carries no `if` filter: `if` uses permission-rule matching, which does not see `/usr/bin/git push`, `bash -c 'git push'`, `env -C <dir> git push` or `sudo git push`, so `pre_tool_use.py` receives every Bash call and alone decides what is gated (an ungated command exits 0 silently; owner ruling 5, S1 Task 8 review).

`.claude/settings.json` (committed):
```json
{"hooks": {
  "PreToolUse": [
    {"matcher": "Agent", "hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py\"", "timeout": 300}]},
    {"matcher": "Bash", "hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/pre_tool_use.py\"", "timeout": 900}]}
  ],
  "Stop":         [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/stop.py\"", "timeout": 300}]}],
  "SubagentStop": [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/subagent_stop.py\"", "timeout": 60}]}],
  "SessionStart": [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/session_start.py\""}]}],
  "UserPromptSubmit": [{"hooks": [{"type": "command", "command": "python3 \"${CLAUDE_PROJECT_DIR}/tools/gates/hooks/user_prompt_submit.py\""}]}]
}}
```
`pre_tool_use.py` dispatches on `tool_name`: `Agent` → `gate_dispatch` + `gate_claims` + `gate_decisions`; `Bash` → parse the command: `git commit` → register/claims/comments/p10(staged); `git push` → the same plus merge_ready when a PR exists; `gh pr ready|merge`, `git merge` → merge_ready + grant; `git worktree add` → gate_plan overlap table for the named task; any other git/gh command → exit 0. `stop.py` → `gate_checkpoint` unless `stop_hook_active`. `subagent_stop.py` → the subagent's report file (path parsed from `## Report:` in its transcript's first user message) must exist and begin with a `status:` line ∈ {DONE, DONE_WITH_FINDINGS, NEEDS_DECISION, BLOCKED, DENIED}; otherwise exit 2 with "write your report file first" (loop guard via `stop_hook_active`).

### Dispatch prompt grammar (checked by `gate_dispatch`, produced by the skill's prompt blocks)

A dispatch prompt is Markdown with these headed sections, in this order; `gate_dispatch` parses by heading:
```
## Role: <controller|implementer|implementer_transcription|task_reviewer|final_reviewer|final_verifier|exception_reviewer|audit_reviewer|audit_verifier|investigator>
## Brief: <path>                       (required for implementer, implementer_transcription, task_reviewer, final_reviewer, final_verifier, exception_reviewer; audit_reviewer, audit_verifier, investigator and controller may give none -- DEC-0009)
## Findings: <id>, <id>                 (register ids; may be "none" for investigator/audit roles)
## Claims: <id>, <id>                   (claim ids this dispatch depends on; may be "none")
## Budget: tokens=<n> minutes=<n>
## Model: <haiku|sonnet|opus|fable>
## Report: <path>
## Strict block
<verbatim contents of ~/.claude/skills/strict-delivery/prompts/<role>.md>
## Task
<the task-specific text>
```
`gate_dispatch` rules: role present and known; model rank ≥ `models[role]` rank; fable only if role ∈ `fable_roles`; brief path exists when required; every finding id exists in the register and is `planned` or `in-progress` (implementer) or any state (reviewer); every claim id exists and is `verified` at HEAD — defined as: state `verified`, and no path in `cites` changed between `verified_at` and HEAD or in the index (`tools.gates.gate_claims.verified_at_head(root, claim, head) -> bool`, with `stale_reason(...)` beside it, as S1 defines them; a commit cannot name its own sha, so equality with HEAD is never required); report path's directory exists; no unanswered decision id (`DEC-[0-9]{4}`) mentioned anywhere in the prompt; prompt length ≤ `dispatch.max_prompt_chars`; the strict block equals the prompt file byte-for-byte.

### Review package (`tools/gates/gate_review_package.py` input; produced by the controller via `review-package` script plus the implementer's artefacts)

Directory `.superpowers/sdd/<plan>/review/<task>/` (git-ignored: a tracked `diff.patch` would trip ci.yml's whitespace check and bloat history; spec §3.3 keeps packages ignored) containing: `commits.txt` (`git log --oneline base..head`), `diff.patch` (`git diff -U10 base..head`), `gates.txt` (full `run_all` output at head), `mutation-<finding-id>.txt` for every finding the task closes (format below; the reviewer, once satisfied, copies it to the TRACKED path `docs/superpowers/state/<plan>/mutations/<finding-id>.txt`, which is what the register's `pin.transcript` names), `siblings.md` (table: sibling | test name or finding id filed), `claims.md` (table: claim text | direct command | contradiction | output excerpt | PASS/FAIL).
Mutation transcript format (one file per finding):
```
finding: A2-IO-N2
test: test_export_crc_bad_delta_fails_the_export
step 1 (fix present): <command> -> PASS
step 2 (fix reverted: <how, e.g. git stash of <files> | patch -R <file>>): <command> -> FAIL
step 3 (fix restored): <command> -> PASS
```
`gate_review_package` fails if any file is missing, if any mutation file lacks the three steps with PASS/FAIL/PASS, if `siblings.md` has fewer rows than the finding's `siblings` list, or if `claims.md` has a FAIL row.

### Handoff (`docs/superpowers/state/<plan>/handoff.md`, checked by `gate_checkpoint`)

YAML front matter (parsed) + Markdown body (free):
```yaml
---
plan: docs/superpowers/plans/<plan>.md
spec: docs/superpowers/specs/<spec>.md
branch: main
head: <sha>
base: <sha>
ledger: docs/superpowers/state/<plan>/ledger.md
resume_line: <n>
block: <n>
next_task: <task id or "none">
findings_in_flight: [<id>: <state>, ...]
claims_verified_at_head: [<id>, ...]
live_subagents: []
open_decisions: [<DEC id>, ...]
rulings: [<one line each>]
grant: DEC-0001
parked_branches: [{name, reason, owner}]
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps: [<one line each>]
---
```
The handoff is committed ON THE BLOCK BRANCH before merge (main is protected: strict checks + enforce_admins, so no post-merge commit can land without its own PR). `head` is the branch tip at the time of the final gate run; after the squash merge `gate_checkpoint --plan <slug>` verifies instead that `git rev-parse main` is a commit whose parent is `base` and whose tree equals the branch tip's tree, and that `branch` is now `main` (the handoff's `branch` field names the block branch; the post-merge check maps it). `gate_checkpoint` fails on: pre-merge HEAD ≠ `head` plus state-only commits (commits after `head` may touch only `docs/superpowers/state/`) / post-merge tree differs from `head` outside `docs/superpowers/state/`; `branch` ≠ current; `git worktree list` has more than the main checkout; `git status --porcelain` non-empty; `live_subagents` non-empty; any finding id in `findings_in_flight` whose register state differs; any claim in `claims_verified_at_head` not `verified` at `head`; an open PR for this plan's branches (`gh pr list --head`); `rulings` count > `rulings.max_per_block`; the ledger lacks a line `CHECKPOINT b<block>` after `resume_line`; `next_task` not "none" when `block` is the plan's last.

### `run_all.py`

```python
GATES = ["register","claims","decisions","plan","dispatch","review_package","p10","comments","state_docs","bench","merge_ready","checkpoint","tools_py"]
def run(names: list[str], args: dict) -> Result   # imports tools.gates.gate_<name>, calls check with args built per gate; aggregates lines prefixed "<name>."
def main(argv=None) -> int                        # --only a,b  --json ; unknown gate name is FAIL run_all.unknown
```
Gates not yet implemented at a given block are absent from `GATES` (never stubbed to PASS).

### CLI flags and conventions (fixed here so every sub-plan uses the same names)

| Interface | Form |
|---|---|
| Any gate | `python3 tools/gates/gate_<name>.py [--json] [gate args]`; `check(args)` receives argv without `--json`; rc 0/1 |
| `run_all.py` | `python3 tools/gates/run_all.py [--json] [--only a,b]` |
| `gate_review_package.py` | `--dir .superpowers/sdd/<plan>/review/<task>` (git-ignored; see "Review package") or `--all` (run_all's default, CI) |
| `gate_plan.py` | `--plan <path> [--source <report-or-register>]` |
| `gate_dispatch.py` | `[--json] --prompt <file>` (or `-` for stdin) `[--tool-model <m>] [--prompts-dir <dir>]`, or `[--json] --config-check` (run_all's default, CI) |
| `gate_merge_ready.py` | `--pr <number> [--post]` |
| `gate_checkpoint.py` | `--plan <slug>` (reads `docs/superpowers/state/<slug>/handoff.md`) |
| `gate_claims.py` | `[--mark-stale --base <sha>]` |
| `gate_decisions.py` | `--scan <paths...>` |
| `postmerge.py` | `--plan <slug> --block <N> --pr <number>` |
| Workflows | `audit` and `block`, input `{brief: <path>}` |
| Gate-suite CI | job `gates` in `.github/workflows/gates.yml` runs every `tools/gates/tests/test_*.py` and `run_all.py` |
| Owner answers | the owner states the answer in chat; the session records it verbatim with `answered_at.by: superbrobenji` (documented in `docs/superpowers/state/README.md`) |
| Finding schema | `cites: []` is valid (filled when the cited file exists); `source` is free text |
| Owner convention | `owner.plan` = the plan's short slug (`strict-delivery` for every sub-plan of this umbrella); `owner.task` = `S<n>-T<m>` for sub-plan task `m` of S<n> (the tracker rows `SD-S<n>-T<m>` use the same form, and the review directory is named by it), `conformance` / `bootstrap` / `dryrun` for the S5-owned generated work, `S<n>-b<N>-fixwave` for a block's final-review fix wave -- DEC-0013 (keep-s2-form; the S1 b1-b3 blocks tracked no rows of their own) |
| Prompt file per role | `controller`→`controller.md`; `implementer` and `implementer_transcription`→`implementer.md`; `task_reviewer` and `final_reviewer`→`reviewer.md`; `final_verifier`→`verifier.md`; `exception_reviewer`→`exception-reviewer.md`; `audit_reviewer`→`audit-reviewer.md`; `audit_verifier`→`audit-verifier.md`; `investigator`→`investigator.md` |
| Waiting for the owner mid-block (e.g. an unanswered grant) | the Stop hook blocks once with the open decision ids; on the second stop (`stop_hook_active`) it allows the stop; the session's last message says "waiting for the owner: DEC-…" |


## Owner rulings on the writers' questions (2026-10-17, binding)

1. "Verified at HEAD" = state `verified` and no cited path changed since `verified_at` (or in the index). Confirmed.
2. No post-merge commits on protected `main`: handoff and state-doc regeneration land on the block branch before merge; `postmerge.py` tags, cleans, and leaves register `merged` edits for the next block's branch. Confirmed.
3. P10 full reach: **a violation in any file a task touches is fixed in that task** (zero-bug rule; the task is incomplete until the touched files are clean); violations in untouched files are tracked as open findings rows (`S3-P10-*`) until the conformance sweep, which closes the rest. A stale or missing tracking row blocks.
4. PD-7 `cmd_emit` fails tier-B scoring and is reworked in the sweep. Confirmed.
5. **Report-id grammar (defined now, built in S1 Task 3):** a finding id matches the register regex `^[A-Z0-9]+(-[A-Za-z0-9]+)+$`. A report under `docs/reviews/` or `docs/superpowers/state/briefs/` whose first 20 lines contain the line `register: true` is a *registered report*; in such a file every Markdown table row whose first cell matches the regex, and every list item beginning with such an id followed by a space or colon, names a finding that MUST exist in `findings.yml` (`FAIL register.report.<file>.<id>: not in register`). Reports without the marker are ignored by the gate; the bootstrap load adds the marker to the three 2026-10-13/16/17 reports after loading them (their historical ids are loaded with the prefixes `A1-` / `A2-` / `DG-` and the report gets an `id-map` table so the scan resolves them).
6. The pre-bench fix work is **parked until this plan is complete**. Before b1: open a docs-only PR from `pre-bench-wrap-up` (audit report, drift analysis, spec, these plans), merge it, delete the branch; b1 and every later block branch from `main`. The parked worktree `../lap-timer-worktrees/s52` is removed (branch `s5.2-conn-ble` stays, listed in the handoff as parked with owner "sub-project C"). No other worktree or branch may exist at a checkpoint.
7. Type checker: **mypy** (`mypy==1.11.2`, pinned in `tools/requirements.txt`, installed in the `gates` CI job). `gate_tools_py` runs `mypy --strict tools/gates` (must pass) and `mypy --ignore-missing-imports tools/lint tools/*.py tools/tracks tools/sim` (failures become findings rows for the sweep; after the sweep this set must pass too). Chosen over pyright because it needs no Node toolchain in CI.
8. Claim granularity: S5's rule stands — one row per sentence that carries a number, a path, a state word (done/pending/required/blocking/merged/open/closed/passes/fails) or a universal (always/never/every/only). 2,569 rows at 24aa0c7.
9. Fable on the final adversarial verifier and the exception reviewer stands (DEC-0018 adds an opus fallback while Fable is unavailable); the 2026-10-17 memory note is superseded (S5 Task 3 rewrites it).

## Sub-plans and blocks (13 blocks, each one PR, tags `strict-delivery-b1..b13`)

| Sub-plan | Blocks | Tasks | Spec rollout step |
|---|---|---|---|
| **S1** `2026-10-17-strict-delivery-S1-registers-core-gates.md` | b1: lib + schemas + registers + `gate_register`; b2: `gate_claims` + `gate_decisions` + `run_all` + CI job `gates`; b3: hooks (`pre_tool_use` for Bash verbs, `session_start`, `user_prompt_submit`, git pre-commit, `install-hooks.sh`), `CLAUDE.md`, skill skeleton + prompt files | 9 | 1, 2 |
| **S2** `2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md` | b4: `gate_dispatch` + Agent matcher in `pre_tool_use`; b5: `gate_review_package` + `gate_plan`; b6: `gate_checkpoint` + `stop.py` + handoff format + checklists + state dir per plan (ledger) | 9 | 3 |
| **S3** `2026-10-17-strict-delivery-S3-p10-reach.md` | b7: rules 1 (call graph), 2 (loop bounds), 3 (full heap census) + probes; b8: rules 5 (density, exclusions), 9 (typedef/struct fn-ptrs, pointed-to set) + all trees incl. devcontroller and test harness allowance + `gate_p10` wrapper; b9: deviation register two-tier columns + scoring check + grace-list removal (violations → register rows) | 9 | 4 |
| **S4** `2026-10-17-strict-delivery-S4-remaining-gates-workflows.md` | b10: `gate_comments` + `gate_tools_py` + `gate_bench`; b11: `gate_state_docs` (generators for roadmap/README facts + owner tables) + `gate_merge_ready` + extends the b3 `git-pre-push.sh` with merge-readiness + `postmerge.py` (cleanup, tag, regen); b12: workflows `audit.js` + `block.js` | 9 | 5, 6 |
| **S5** `2026-10-17-strict-delivery-S5-validation-bootstrap-conformance.md` | b13: skill red/green scenarios + dry run + final SKILL.md; then the bootstrap load and the conformance sweep are run AS audit/fix work through the installed pipeline (they are plans of their own, written by `audit.js`'s output and the owner's decisions; S5 writes only their briefs and gates) | 3 + 2 generated plans | 7, 8, 9 |

Order: S1 → S2 → S3 → S4 → S5. Each sub-plan starts with the gates its predecessors installed; the dispatch hook is live from b4 onwards, `gate_checkpoint` from b6, `gate_merge_ready` from b11 (before b11, the controller runs the merge-readiness checklist in `checklists/block-end.md` by hand and ledgers it).

## Checkpoint protocol for this plan (applies from Block 1, by hand until b6 automates it)

At every block end: run `python3 tools/gates/run_all.py`; dispatch final reviewer (opus) + final verifier (Fable, or opus while the DEC-0018 fallback is on) with the block's review packages; `gh pr ready`; wait for every check; before the merge deletes the block branch, push an annotated tag `strict-delivery-b<N>-tip` at the branch tip (the handoff commit, the PR head) so every block commit the registers cite stays reachable in a fresh clone (DEC-0015 tip-tag; the gates CI job on `main` went red after the b4 squash until `strict-delivery-b4-tip` was pushed); merge with `gh pr merge --squash` only if `decisions.yml` has `grant:strict-delivery` answered `yes` by the owner; tag `strict-delivery-b<N>` on the squash commit and push the tag; delete branch + worktrees; write `docs/superpowers/state/strict-delivery/handoff.md`; append `CHECKPOINT b<N>` to the ledger; post the status update and the resume prompt; stop.

Resume prompt template:
```
Invoke the strict-delivery skill. Read docs/superpowers/state/strict-delivery/handoff.md. Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue the strict-delivery plan at block <N+1> (sub-plan <S>, task <T>) with superpowers:subagent-driven-development, opus sub-controller, models per tools/gates/config.yml.
```
