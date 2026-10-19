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
| `gate_dispatch.py` | `--config-check` (run_all's default): the model map, `fable_roles` (equal to the fable-minimum roles), `model_rank` (haiku < sonnet < opus < fable), the prompt cap and the optional `fable_fallback` (DEC-0018: absent = off, else a model ranked at least opus and below fable) agree with the dispatch grammar; `--prompt`: one dispatch prompt, where a `fable_roles` role on the `fable_fallback` model is `PASS dispatch.model.fallback` (see the gate_dispatch section) |
| `gate_review_package.py` | `--all` (run_all's default; no package is a PASS) or `--dir .superpowers/sdd/<plan>/review/<task>`: the five package files plus one PASS/FAIL/PASS `mutation-<id>.txt` per finding the task owns, a siblings table at least as long as the register's list, a claims table with no FAIL row |
| `gate_plan.py` | `--plan P [--source report-or-register:query]`: the writing-plans header blocks, `**Files:**` (one backticked path per item), `**Findings:**`, `**Siblings:**`, `**Gates:**` and a pin step per task, every routed id registered and in exactly one task, source-id coverage, PARALLEL file overlap, tasks per block <= `block.max_tasks`, no placeholder text; `--in-flight` (run_all's default): the same checks, without a source, on the plan named on line 1 of every ledger that is in flight |

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
     `_SYSTEM` or `_NOSYSTEM` assignment (`=` or `+=`) anywhere in any Bash command (a
     `git config core.hooksPath` on its own would move the hooks for every later command), a
     `HOME` or `XDG_CONFIG_HOME` assignment (`=` or `+=`, `env HOME=...` too) before a gated verb
     (it moves the global git config, which can set `core.hooksPath`), and `--no-verify` (or the
     abbreviations `--no-veri`, `--no-verif`), `-n` or a short-option cluster holding `n` (`-anm`)
     in the arguments of a gated verb, quoted (`git "push"`) or not: up to an unquoted separator
     outside every parenthesis the arguments open, or an unpaired `)`, quote or backtick (so the
     rest of an enclosing `$(...)` is not read); quotes, `$'...'`, `${...}`, backticks, `$(...)`,
     `<(...)`, `>(...)`, `$((...))` at any depth and the redirections `>&`, `<&`, `&>`, `>|` do
     not end them. `-n` is `--no-verify` for commit and `--dry-run` for push; it is blocked for
     both. Every gated verb gets its own argument scan, so a verb mentioned in a quoted string, a
     heredoc or a comment never hides a later verb's flag (`echo "git push"; git commit -n`).
  2. `git [options] commit|push` is matched in the text wherever it stands: after any separator,
     `$(`, a backtick, `VAR=val`, a wrapper (`sudo`, `env`, `nice`, `time`, ...) or a path
     (`/usr/bin/git`), the git word in any case (`GIT`, `Git`: a case-insensitive file system runs
     them, so a prose `Git push` runs the gates too). The options are git 2.50.1's global options,
     each checked with
     `git <option> version` (`-C <dir>`, `-c <k=v>`, `--git-dir`, `--work-tree`, `--namespace`,
     `--config-env`, `--attr-source`, `--shallow-file`, `--no-pager`, `-p`, `--bare`, `--exec-path`,
     the pathspec flags, ...; quoted values and values holding `$(...)` or backticks included), read
     left to right as git does, plus a `$X` word (`git $X commit` is gated). The verb is a whole
     word (`commit-graph`, `pushd`, `--push-option` are not verbs). A backslash-newline is removed
     first. There is no data/code analysis: a mention inside quotes, a heredoc or a comment
     (`echo "git push"`, a python heredoc that prints it) runs the gates too; on a healthy tree that
     costs well under a second and blocks
     nothing.
  3. The tree: each `-C <dir>` of the gated git word, applied in turn as git does, from the
     directory of a leading `cd <dir>` or `cd -- <dir>` followed by `&&`, `||` (the `|| exit` /
     `|| return` idiom), `;` or a newline, else from the event's `cwd`. A
     directory that is not in a repository exits 2 with `FAIL hook.target: <dir>: <git stderr>`; a
     missing `cwd` with `FAIL hook.input`. Every tree named is gated once with the register and
     claim gates.

  Documented limits (DEC-0005): A verb hidden behind an expansion, alias, function, `eval`, a
  payload file, or a wrapper the regex does not see (`$G push`, `git $V`, `eval "$CMD"`,
  `bash script-that-pushes.sh`, `xargs ... git` without a literal `git push`) is not gated by this
  hook. The git hooks gate it: pre-commit for commits, pre-push for pushes. The same holds for a git
  word or verb with a quoted or escaped piece (`g"it" push`, `git p"ush"`); for a tree changed in a
  way the hook does not follow (a later `cd`, a leading `cd` in a form item 3 does not name such
  as `cd -P <dir>`, `pushd`, a subshell, `GIT_DIR=`, `--git-dir`,
  `--work-tree`, `env -C`), where the hook gates the tree it can name and the git hooks gate the
  real one; for an option value the hook cannot read as one word
  (`git -C $(dirname $(pwd)) commit`) or a global option git adds after 2.50.1, where the verb is
  not seen and so neither is a bypass flag after it; for a quoted or escaped bypass flag
  (`--no-"verify"`, `core.hooks\Path`); for a flag after a `case` pattern's `)` inside a
  substitution (`$(case x in x) ...;; esac)`) or after a double-quoted `$(...)` that both nests
  parentheses and quotes (`"$(a $(b ")"))"`), where the argument scan ends early; and for a bypass
  in a form the text does not name as one (`include.path`, a config file or hook script edited by
  other means). A `-C` or `cd` directory is not expanded: one holding a variable, `$(...)` or a
  backtick (`cd "$WT" &&`, `git -C $(pwd)`) exits 2 with `FAIL hook.target`, a block of a real
  command; name the directory literally. A `HOME` or `XDG_CONFIG_HOME` assignment that only
  precedes a verb in data is blocked as a bypass too. A commit message that holds `-n`,
  `--no-verify` or `core.hooksPath` as a word, or a `-n` inside a substitution in the arguments
  (`git commit -m "$(echo -n x)"`), is blocked as a bypass: write the message to a file and use
  `git commit -F <file>`. So is, rarely, a `-n` after the arguments: a scan that meets an earlier
  verb's scan deeper inside a parenthesis reads on to the end of the command. Every scan is linear
  in the command length. Every other command and tool exits 0 silently, after one python3 start.
  The `Agent` branch gates every subagent dispatch: it runs `dispatch`, `claims` and `decisions`
  (never `register`) in the tree of the event's `cwd` (a missing or empty `cwd` is `FAIL hook.input`,
  a `cwd` outside a repository `FAIL hook.target`), with the prompt (`tool_input.prompt`) written to a
  temporary file for `gate_dispatch --prompt` and `gate_decisions --scan`, and `tool_input.model` as
  `--tool-model`. An empty or missing prompt is `FAIL dispatch.input`; a missing or disagreeing
  `tool_input.model` is `FAIL dispatch.model.agree` (the subagent would otherwise inherit the session
  model). `tool_input.subagent_type`, when present, is passed as `--subagent-type`: `fork` (any case,
  surrounding blanks ignored) is `FAIL dispatch.fork`, because a fork runs on the session model with the
  session context whatever `tool_input.model` says (ruling 4, B4-FIX-1); a non-string value is
  `FAIL dispatch.fork` too; any other value, or none, is checked as before. `SD_PROMPTS_DIR` overrides `--prompts-dir` (tests only). Exit 2 prints the gate lines on stderr; the temporary file in a reason is shown as `<tool_input.prompt>`.
- `session_start.py`: SessionStart cannot block and Claude Code injects only the stdout of an exit-0
  hook, so this hook always exits 0. It prints whether the gates are installed in the event's
  project, one line per `docs/superpowers/state/*/handoff.md`, the output of `install-hooks.sh` when
  the repository's pre-commit or pre-push hook (`$(git rev-parse --git-path hooks)/<hook>`) is not
  ours (a symlink never is), and the instruction to invoke the strict-delivery skill. Every failure
  is a `FAIL ...` line on stdout (the model sees it), then the instruction line (the line is also
  copied to stderr, which Claude Code shows neither the model nor the owner on exit 0; it appears in
  the debug log only):
  `FAIL hook.install` when `install-hooks.sh` fails or refuses (a hooks directory outside the
  repository's git directory, or a second foreign hook over an existing `<hook>.pre-strict-delivery`
  backup), `FAIL hook.input` for a missing or non-directory `cwd`, `FAIL hook.internal` for anything
  else (a cwd outside a repository, malformed stdin).
- `user_prompt_submit.py`: one reminder line; malformed stdin exits 2 with `FAIL hook.internal`.
- `install-hooks.sh` installs two git shims (marker `# strict-delivery-gates-hook`): `pre-commit`,
  which runs `hooks/git-pre-commit.sh`, and `pre-push`, which runs `hooks/git-pre-push.sh`, each
  from the tree git runs in (`FAIL pre-commit.missing` / `FAIL pre-push.missing` for a tree without
  the script). A foreign hook, or a symlink (even one to the tracked script: the link itself is
  moved, never written through), is moved to `<hook>.pre-strict-delivery`; an existing backup is
  never overwritten, and then nothing is installed. `git-pre-commit.sh` runs
  `run_all.py --only register,claims` and refuses (`FAIL pre-commit.unstaged`) while a worktree-side
  change, an untracked or an ignored file sits under `docs/superpowers/state`, `docs/reviews`,
  `tools/gates` or among the root-level `*.py`, `*.pyc`, `*.so` and `*/__init__.*`: stage it with
  the commit or remove it. Exempt: `tools/gates/tests`, `tools/gates/README.md`, any `.DS_Store`
  and the `__pycache__` directories under `tools/gates`. A finding's evidence file is gated by the
  register itself: it must be a regular file git tracks (`register.evidence`), so an untracked or
  ignored one, or a tracked symlink, fails the gate wherever it lies. `git-pre-push.sh` ignores its
  `<remote> <url>` arguments and reads the ref lines on stdin: every pushed local sha, whatever
  the ref kind, is peeled to a commit and must be HEAD (DEC-0006, read literally), else it is
  refused first (`FAIL pre-push.not-head`; a branch, `bad~0:refs/heads/y`, a tag of an older
  commit or a non-commit object alike: push it from that commit checked out, so the gates see its
  tree); `HEAD:refs/heads/x`, a tag of HEAD and a delete pass; from an unborn HEAD only deletes
  pass (`FAIL pre-push.not-head: ... pushed from an unborn HEAD`), and then no gate runs. Then it
  runs `git-pre-commit.sh` with `--pre-push`: the same paths must not differ from HEAD at all,
  staged changes included, because a push sends commits, not the index
  (`FAIL pre-push.uncommitted`); then the same two gates. S4 extends it with merge-readiness.

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
