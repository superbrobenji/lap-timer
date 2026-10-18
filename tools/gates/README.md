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

`.claude/settings.json` wires `hooks/pre_tool_use.py` (matchers `Agent` and `Bash`, the Bash entry
without an `if` filter, so the script sees every Bash call), `hooks/session_start.py` and
`hooks/user_prompt_submit.py`; a new Claude Code session loads it. `Stop` and `SubagentStop` arrive
with S2.

- `pre_tool_use.py`: exits 2 with the failing lines on stderr, also on any internal error
  (`FAIL hook.internal`, fail closed). Bash: `git commit` and `git push` in command position (the
  first word of a simple command, bare, path-qualified or quoted, after `VAR=val`, `if`/`then`/`do`/`!`
  and the wrappers `sudo`, `doas`, `command`, `builtin`, `nohup`, `time`, `timeout`, `exec`, `nice`,
  `ionice`, `setsid`, `stdbuf`, `env`, `noglob`, `caffeinate` with their options; also inside
  `$(...)` and backticks) run the register and claim gates on the tree the command names: the
  event's cwd, moved by a leading `cd <dir> &&` or `;` and by each `git -C <dir>`. A verb that is
  only mentioned (`echo git push`, `grep 'git push' docs`, a quoted argument or commit message, a
  heredoc body, a comment) is data and is not gated. A gated command whose tree it cannot tell
  (another `cd`, `pushd`, a subshell, `--git-dir`, `--work-tree`, `GIT_DIR=`, an expanded `-C` word)
  exits 2 with `FAIL hook.target`; so does any command that holds a launcher (`bash -c`, `eval`,
  `xargs`, `| sh`, a shell fed a heredoc, `find -exec`/`-execdir`, `env -C`, `sudo -D`) and also
  names the verb anywhere, quoted or not. Every other command and tool exits 0 silently, after one
  python3 start; the `Agent` branch (the dispatch gate) arrives with S2.
- `session_start.py`: SessionStart cannot block and Claude Code injects only the stdout of an exit-0
  hook, so this hook always exits 0. It prints whether the gates are installed in the event's
  project, one line per `docs/superpowers/state/*/handoff.md`, the output of `install-hooks.sh` when
  the repository's pre-commit hook (`$(git rev-parse --git-path hooks)/pre-commit`) is not ours, and
  the instruction to invoke the strict-delivery skill. Every failure is a `FAIL ...` line on stdout
  (the model sees it), then the instruction line (the line is also copied to stderr, which Claude Code
  shows neither the model nor the owner on exit 0; it appears in the debug log only): `FAIL
  hook.install` when `install-hooks.sh` fails or refuses (a hooks directory outside the repository's
  git directory, or a second foreign pre-commit over an existing `pre-commit.pre-strict-delivery`
  backup), `FAIL hook.input` for a missing or non-directory `cwd`, `FAIL hook.internal` for anything
  else (a cwd outside a repository, malformed stdin).
- `user_prompt_submit.py`: one reminder line; malformed stdin exits 2 with `FAIL hook.internal`.
- `install-hooks.sh` installs the git `pre-commit` shim (marker `# strict-delivery-gates-hook`),
  which runs `hooks/git-pre-commit.sh` of the tree being committed (`FAIL pre-commit.missing` for a
  tree without it). That script runs `run_all.py --only register,claims` and refuses
  (`FAIL pre-commit.unstaged`) while a worktree-side change, an untracked or an ignored file sits
  under `docs/superpowers/state`, `docs/reviews`, `tools/gates` or among the root-level `*.py`,
  `*.pyc`, `*.so` and `*/__init__.*`: stage it with the commit or remove it. Exempt: `tools/gates/tests`,
  `tools/gates/README.md`, any `.DS_Store` and the `__pycache__` directories under `tools/gates`.
