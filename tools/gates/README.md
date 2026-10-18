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

Design (owner decision DEC-0005): the Claude Code Bash hook is a best-effort textual check with
documented limits; the git hooks, pre-commit and pre-push, are the enforcement. They run for every
commit and push, however it was launched.

- `pre_tool_use.py`: exits 2 with the failing lines on stderr, also on any internal error
  (`FAIL hook.internal`, fail closed). Bash, in order:
  1. Hook bypasses exit 2 with `FAIL hook.bypass: <form found>` before any gate runs:
     `core.hooksPath` (any case) or a `GIT_CONFIG_PARAMETERS`, `_COUNT`, `_KEY_*`, `_GLOBAL`,
     `_SYSTEM` or `_NOSYSTEM` assignment anywhere in any Bash command (a `git config core.hooksPath`
     on its own would move the hooks for every later command), and `--no-verify` (or the
     abbreviations `--no-veri`, `--no-verif`), `-n` or a short-option cluster holding `n` (`-anm`)
     after a gated verb, up to the end of its simple command (quote-aware). `-n` is `--no-verify`
     for commit and `--dry-run` for push; it is blocked for both.
  2. `git [options] commit|push` is matched in the text wherever it stands: after any separator,
     `$(`, a backtick, `VAR=val`, a wrapper (`sudo`, `env`, `nice`, `time`, ...) or a path
     (`/usr/bin/git`). The options are the ones git takes before a subcommand (`-C <dir>`,
     `-c <k=v>`, `--git-dir`, `--work-tree`, `--namespace`, `--config-env`, `--no-pager`, `-p`,
     `--bare`, `--exec-path`, ...; quoted values included), read left to right as git does, plus a
     `$X` word (`git $X commit` is gated). The verb is a whole word (`commit-graph`, `pushd`,
     `--push-option` are not verbs). A backslash-newline is removed first. There is no data/code
     analysis: a mention inside quotes, a heredoc or a comment (`echo "git push"`, a python heredoc
     that prints it) runs the gates too; on a healthy tree that costs well under a second and blocks
     nothing.
  3. The tree: each `-C <dir>` of the gated git word, applied in turn as git does, from the
     directory of a leading `cd <dir> &&` (or `;`, or a newline), else from the event's `cwd`. A
     directory that is not in a repository exits 2 with `FAIL hook.target: <dir>: <git stderr>`; a
     missing `cwd` with `FAIL hook.input`. Every tree named is gated once with the register and
     claim gates.

  Documented limits (DEC-0005): A verb hidden behind an expansion, alias, function, `eval`, a
  payload file, or a wrapper the regex does not see (`$G push`, `git $V`, `eval "$CMD"`,
  `bash script-that-pushes.sh`, `xargs ... git` without a literal `git push`) is not gated by this
  hook. The git hooks gate it: pre-commit for commits, pre-push for pushes. The same holds for a git
  word or verb with a quoted or escaped piece (`g"it" push`, `git p"ush"`); for a tree changed in a
  way the hook does not follow (a later `cd`, `pushd`, a subshell, `GIT_DIR=`, `--git-dir`,
  `--work-tree`, `env -C`), where the hook gates the tree it can name and the git hooks gate the real
  one; and for a bypass the text does not show (a config file or hook script edited by other
  means). A commit message that holds `-n`, `--no-verify` or `core.hooksPath` as a word is blocked
  as a bypass: write the message to a file and use `git commit -F <file>`. Every scan is linear in
  the command length. Every other command and tool exits 0 silently, after one python3 start; the
  `Agent` branch (the dispatch gate) arrives with S2.
- `session_start.py`: SessionStart cannot block and Claude Code injects only the stdout of an exit-0
  hook, so this hook always exits 0. It prints whether the gates are installed in the event's
  project, one line per `docs/superpowers/state/*/handoff.md`, the output of `install-hooks.sh` when
  the repository's pre-commit or pre-push hook (`$(git rev-parse --git-path hooks)/<hook>`) is not
  ours, and the instruction to invoke the strict-delivery skill. Every failure is a `FAIL ...` line
  on stdout (the model sees it), then the instruction line (the line is also copied to stderr, which
  Claude Code shows neither the model nor the owner on exit 0; it appears in the debug log only):
  `FAIL hook.install` when `install-hooks.sh` fails or refuses (a hooks directory outside the
  repository's git directory, or a second foreign hook over an existing `<hook>.pre-strict-delivery`
  backup), `FAIL hook.input` for a missing or non-directory `cwd`, `FAIL hook.internal` for anything
  else (a cwd outside a repository, malformed stdin).
- `user_prompt_submit.py`: one reminder line; malformed stdin exits 2 with `FAIL hook.internal`.
- `install-hooks.sh` installs two git shims (marker `# strict-delivery-gates-hook`): `pre-commit`,
  which runs `hooks/git-pre-commit.sh`, and `pre-push`, which runs `hooks/git-pre-push.sh`, each
  from the tree git runs in (`FAIL pre-commit.missing` / `FAIL pre-push.missing` for a tree without
  the script). A foreign hook is moved to `<hook>.pre-strict-delivery`; an existing backup is never
  overwritten, and then nothing is installed. `git-pre-commit.sh` runs
  `run_all.py --only register,claims` and refuses (`FAIL pre-commit.unstaged`) while a worktree-side
  change, an untracked or an ignored file sits under `docs/superpowers/state`, `docs/reviews`,
  `tools/gates` or among the root-level `*.py`, `*.pyc`, `*.so` and `*/__init__.*`: stage it with the
  commit or remove it. Exempt: `tools/gates/tests`, `tools/gates/README.md`, any `.DS_Store` and the
  `__pycache__` directories under `tools/gates`. `git-pre-push.sh` runs the same script with
  `--pre-push`: the same paths must not differ from HEAD at all, staged changes included, because a
  push sends commits, not the index (`FAIL pre-push.uncommitted`); then the same two gates. It
  ignores its `<remote> <url>` arguments and reads nothing from stdin (S4 adds merge-readiness over
  the ref lines). The gates check the working tree, which equals HEAD for those paths; a push of a
  ref other than HEAD is checked against HEAD.
