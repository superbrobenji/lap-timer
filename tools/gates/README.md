# tools/gates -- strict-delivery gate scripts

Spec: `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` §7-§8. Interfaces: the
umbrella plan `docs/superpowers/plans/2026-10-17-strict-delivery.md`, "Shared interfaces".

## Contract

- One script, one concern: `gate_<name>.py` exposes `check(args) -> Result` and `main(argv)`.
- Output: one line per verdict, `PASS <id>` or `FAIL <id>: <reason>`; `--json` for a JSON object.
- Exit 0 only when every line is PASS and there is at least one line; otherwise 1.
- A missing or unreadable input is `FAIL <gate>.input`, never "nothing to do"; an empty result
  is `FAIL <gate>.empty`; bad arguments are `FAIL <gate>.input: bad arguments`; `-h` prints the
  usage on stderr and reports `FAIL <gate>.input: help requested; no gate was run`.
- `--root <tree>` points a gate at another tree's `docs/superpowers/state/`; config is always read
  from the checkout holding the gate code (`tools/gates/config.yml`).

## Installed gates

| Gate | Checks |
|---|---|
| `gate_register.py` | findings schema, an owner on every row, legal transitions, evidence per state |
| `gate_claims.py` | claims schema, every sha a commit of the repository, every cite a normalised repo-relative path that exists at `verified_at` (DEC-0004), verified rows not stale at HEAD (history + staged files), `false` rows name a registered finding; `--mark-stale --base <sha>` (a commit) rewrites stale rows |
| `gate_decisions.py` | decisions schema (every sha a commit of the repository), owner-only answers, no `--scan` file citing an unanswered `DEC-NNNN` |

`run_all.py` runs every installed gate (`GATES`) and aggregates the lines with a `<gate>.`
prefix; `--only a,b` selects, `--root <tree>` gates another tree (`--json` first, as for every
gate). An unknown, missing or repeated gate, an empty or non-directory `--root`, and a root with
no `docs/superpowers/plans/*.md` for `decisions` to scan are each a FAIL. For `decisions` it
scans every `docs/superpowers/plans/*.md` (extra `--scan` args add to them); a relative `--root` works from any directory; run alone without `--scan`, `gate_decisions.py`
prints no `scan` or `open-ref` PASS line.

## Running

```sh
python3 -m pip install PyYAML==6.0.2              # once per interpreter (tools/requirements.txt)
python3 -m unittest discover -s tools/gates/tests -t .
python3 tools/gates/run_all.py
python3 tools/gates/gate_register.py --json
```

CI: `.github/workflows/gates.yml`, job `gates` (the unit tests on Python 3.9 and 3.12, then `run_all.py`; one job, full history checkout).

## Hooks

`.claude/settings.json` wires `hooks/pre_tool_use.py`, `hooks/session_start.py` and
`hooks/user_prompt_submit.py`; a new Claude Code session loads it. Every hook exits 2 with the
failing lines on stderr, and also on any internal error (`FAIL hook.internal`, fail closed).

- `pre_tool_use.py` (Bash): `git commit` and `git push` run the register and claim gates on the
  tree the command names (the event's cwd, moved by a leading `cd <dir> &&` and by each `git -C
  <dir>`). A gated command whose tree it cannot tell (another `cd`, a subshell, `--git-dir`,
  `GIT_DIR=`, an expanded `-C` word, a nested shell, `eval`, `xargs`, `env -C`) exits 2 with
  `FAIL hook.target`. Every other command and tool exits 0 silently; the `Agent` branch (the
  dispatch gate) arrives with S2.
- `session_start.py`: prints whether the gates are installed in the project, one line per
  `docs/superpowers/state/*/handoff.md` and the instruction to invoke the strict-delivery skill;
  runs `install-hooks.sh` when `.git/hooks/pre-commit` is not ours. A refusal of
  `install-hooks.sh` (a hooks directory outside the repository's git directory, or a second foreign
  pre-commit over an existing `pre-commit.pre-strict-delivery` backup) exits 2 with
  `FAIL hook.install`.
- `user_prompt_submit.py`: one reminder line.
- `install-hooks.sh` installs the git `pre-commit` shim (marker `# strict-delivery-gates-hook`),
  which runs `hooks/git-pre-commit.sh` of the tree being committed (`FAIL pre-commit.missing` for a
  tree without it). That script runs `run_all.py --only register,claims` and refuses
  (`FAIL pre-commit.unstaged`) while a worktree-side change, an untracked or an ignored file sits
  under `docs/superpowers/state`, `docs/reviews`, `tools/gates` (except `tests/` and `README.md`)
  or among the root-level `*.py`, `*.pyc`, `*.so` and `*/__init__.*`: stage it with the commit or
  remove it.
