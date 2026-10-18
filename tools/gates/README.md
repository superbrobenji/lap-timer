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
  (`FAIL hook.internal`, fail closed). Bash: a backslash-newline is removed (the shell joins the
  lines with nothing between them), then one linear pass splits the command into data and code.
  Data: single-quoted spans and escaped characters, ANSI-C `$'...'` spans (escapes decoded),
  quoted-delimiter heredoc bodies (`<<'E'`, `<<"E"`, `<<\E`), the text of double-quoted spans and
  unquoted-delimiter heredoc bodies outside `$(...)`, backticks and `${...}`, and every character of a
  word that holds one of these. A quoted span or escaped character made only of `[A-Za-z0-9_./=+-]`
  is code, so quote removal joins it to its word (`g"it" push`, `gi\t push`, `git p"ush"` are
  `git push`); a comment (a `#` that starts a word, never inside `${...}`) is neither.
  `git commit` and `git push` are then matched textually in the code anywhere (any wrapper,
  `VAR=val`, redirection or path-qualified git word before the verb) and run the register and claim
  gates on the tree the command names: the event's cwd, moved by a leading `cd <dir> &&` or `;` and
  by each `git -C <dir>`. So `echo "$(git push)"` and a mention outside quotes (`echo git push`) are
  gated: a blocked command is the cost of failing closed. A `git` word in data with a verb word
  after it in the same pipeline (a lone verb word only when the code holds a shell launcher, so
  `python3 -c "print('push')"` passes), or a git call the hook cannot read (`git p*`, `git "$V"`,
  `git $V`, `echo push | xargs git`, `$G push`, `{git,} push`), exits 2 with `FAIL hook.target:
  cannot tell which tree <verb> runs in, or whether it runs` unless every command on the way keeps it
  as text: the head is echo, printf (without `-v`), grep, egrep, fgrep, rg, gh, cat, tee, jq, awk
  (no `system`, `getline`, `|` or `-f`), xargs running echo, grep, egrep, fgrep, rg, cat, jq, head,
  tail or wc, or git with a subcommand among commit, push, log, show, tag, notes, stash, merge,
  shortlog, status, diff, add, branch, rev-list, describe, blame (no `-c`/`--config-env`); sed is not
  one (GNU sed `e` runs its pattern space); the data is not in a `VAR=val` or
  redirection prefix; there is no `>(...)`; the output is piped only into those commands or head,
  tail, wc, sort, uniq, cut, less, more, tr, nl, column, fold; and the same holds for each command
  whose word holds it (a heredoc body belongs to the command on its opener line). So
  `grep -rn 'git push' docs | head`, `grep 'git push' f | awk '{print $1}'`,
  `find . | xargs grep -l 'git push'`, `git log --grep='git push'` and
  `gh pr create --body "$(cat <<'EOF' ...)"` pass, while `bash -c 'git push'`, `eval`, `watch`,
  `su -c`, `python3 -c`, a heredoc fed to an interpreter, `| sh`, `sed`, `sudo sh -c`, `env`,
  `xargs sh` or an unknown command holding it fail. A gated command whose tree it cannot tell (another `cd`, `pushd`,
  a subshell, `--git-dir`, `--work-tree`, `GIT_DIR=`, an expanded `-C` word) or that holds a launcher
  in its code (`bash -c`, `eval`, `xargs`, `| sh`, a shell fed a heredoc, `find -exec`/`-execdir`,
  `env -C`, `sudo -D`) also exits 2 with `FAIL hook.target`. A `$X` word between git and the
  subcommand reads as an option (`git $X commit` gates the cwd tree). Not followed: aliases and
  functions defined earlier, a git word and a subcommand that are both variables or globs (`$G $V`),
  a `-C $VAR` directory, a payload written to a file and run later, text a program assembles at run
  time; an unterminated quote or heredoc is code. Every other command and tool exits 0 silently,
  after one python3 start; the `Agent` branch (the dispatch gate) arrives with S2.
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
