# Playbook: two parallel streams (DEC-0133)

Two streams run in parallel until the prototype is field-test ready (DEC-0134): **A, strict delivery** (umbrella plan "Replan 2026-10-29") and **B, prototype** (`docs/superpowers/plans/2026-10-29-prototype-field-ready.md`). No session runs for days: every session does one block or one sync, then stops at a `/clear` checkpoint. All state lives in tracked files; a fact that exists only in a conversation, a memory note or a git-ignored folder does not count until it is written into a tracked document (DEC-0131).

## Start of every session
Read, in this order, before any other step: this playbook, the board `docs/superpowers/state/program/board.md`, your stream's handoff, and the plan the handoff names. Once the program tooling block lands, run `python3 tools/program/load_context.py <kind>` and Read every entry it lists (see "Context contract"). Stop at your block's checkpoint.

## Where state lives
| what | path | written by |
|---|---|---|
| Stream board (one row per stream) | `docs/superpowers/state/program/board.md` | sync sessions; each block's checkpoint commit edits only its own stream's row |
| Stream A handoff and ledger | `docs/superpowers/state/strict-delivery/handoff.md`, `ledger.md` | stream A checkpoints |
| Program tooling ledger | `docs/superpowers/state/program-tooling/ledger.md` | the program tooling block |
| Stream B handoff and ledger | `docs/superpowers/state/prototype/handoff.md`, `ledger.md` | stream B checkpoints |
| Read lists (context contract) | `docs/superpowers/state/program/context.yml` | program tooling block; changed only through the pipeline |
| Document index | `docs/INDEX.md` (current paths, DEC-0146) | program tooling block; every later change through the pipeline |
| Registers | `docs/superpowers/state/findings.yml`, `decisions.yml`, `claims.yml` | both streams, in their ID ranges, append-only (see "Shared files") |

Each handoff is pushed on its stream's branch at every checkpoint, so a sync reads it with `git show origin/<branch>:<path>` (a read-list entry with a `ref`, see below).

Branches: stream A works on `block/strict-delivery-b5`; the program tooling block runs on its own branch `block/program-tooling` from the b5 tip; its PR's base is `block/strict-delivery-b5`, it merges under DEC-0001 before the b5 lean close (DEC-0148, DEC-0153); later blocks get one branch each from `main`. Stream B branches from the b5 tip that holds this playbook and DEC-0132..DEC-0157 (`block/prototype-b1`, then `block/prototype-b<N>` per block, DEC-0156), because `main` has neither the decisions nor the b5 gates its plan must pass. When b5 squash-merges, the next sync rebases stream B with `git rebase --onto origin/main strict-delivery-b5-tip <stream B branch>` (the tip tag the umbrella checkpoint protocol pushes before every merge) and runs the D34 set on the result. Stream B PRs merge only after that rebase; until then stream B blocks accumulate on its branch, each next block branching from the previous block's tip (DEC-0154). A stream B block PR then merges under grant:prototype-field-ready (DEC-0155) when every check passes, on the owner's sentence "merge when all checks pass".

## Ownership (a stream needing the other's file stops and raises it at the next sync)
| stream | owns |
|---|---|
| A | `tools/gates/`, `tools/lint/`, `tools/program/`, `.claude/`, `.github/workflows/gates.yml`, `.github/workflows/ci.yml` (S3 Tasks 1 and 6 change it; DEC-0157), `.github/PULL_REQUEST_TEMPLATE.md` (DEC-0169), `.gitignore`, `CLAUDE.md`, `docs/INDEX.md`, `docs/power-of-10-checklist.md`, `docs/power-of-10-deviations.md`, the strict-delivery spec, the Power of 10 spec `docs/superpowers/specs/2026-09-17-power-of-10-compliance-design.md`, the strict-delivery plans (umbrella, S1-S5, program tooling, docs reorganisation) and `docs/superpowers/state/strict-delivery/`, `program/`, `program-tooling/`, `docs-reorganisation/` (the docs reorganisation block's ledger), `docs/superpowers/state/README.md`, `docs/superpowers/playbooks/`, `docs/reviews/`, the strict-delivery skill and its prompts under `~/.claude/skills/strict-delivery/` |
| B | `components/`, `main/`, `test/`, `test_apps/`, `devcontroller/`, `CMakeLists.txt`, `build.sh`, `partitions.csv`, `sdkconfig.defaults*`, `dependencies.lock`, `keys/`, `README.md`, `docs/pitch.md`, `docs/display-glyphs.md`, `tools/` except gates, lint and program, `docs/bench/`, `docs/hardware/`, `docs/measurements.md`, the lap-timer spec `docs/superpowers/specs/2026-09-14-lap-timer-design.md` and the roadmap, every other spec and plan under `docs/superpowers/specs/` and `docs/superpowers/plans/` (the product specs and plans), the prototype plan and `docs/superpowers/state/prototype/`, `.github/workflows/firmware.yml` |

A path neither row names belongs to no stream: the session that needs it stops and raises it at the next sync, where the owner assigns it (DEC-0157) and the table gets a row. Stream B needing a `ci.yml` change (a new firmware gate list, for example) asks at a sync; stream A makes it (DEC-0157). The docs reorganisation block (stream A) moves and rewrites references in files of both rows, stream B's included (`docs/`, `.github/workflows/firmware.yml`, the helpers under `tools/`); it runs only when stream B has no branch open, with every document moved at once rather than stream A's first (DEC-0146, DEC-0139).

## Shared files
- Registers: each stream appends whole rows at the end of the file and never edits or reorders the other stream's rows. A rebase conflict between two appended blocks is resolved by keeping both blocks, the one already on `main` first; the sync runs the D34 set on the result.
- Board: a block's checkpoint commit edits only its own stream's row; only a sync edits the other row or appends to the sync log.
- Memory notes under `~/.claude/projects/`: shared like the registers: each stream adds its own notes and never edits the other's; the docs move rewrites them once, with no stream B branch open (DEC-0164).

ID ranges: stream A records decisions DEC-0132..DEC-0299 and DEC-0500..DEC-0699 (DEC-0512) and findings with its existing prefixes; stream B records decisions DEC-0300..DEC-0499 and findings with the prefix `PT-`. The docs move (DEC-0139) runs right after field-readiness, when stream B has no branch open (DEC-0146).

## Session kinds
1. **Stream A block** -- the owner starts it with the resume prompt in stream A's handoff. It reads its read list, runs one block of the umbrella replan under the strict-delivery skill, checkpoints (clean tree, handoff and ledger committed and pushed, its board row updated), and stops for `/clear`.
2. **Stream B block** -- the same for the prototype plan. Inside a block, tasks on disjoint files run in parallel worktrees (one implementer each, one task review each), then merge into the stream branch one at a time.
3. **Sync** (about 20 minutes; the owner says "sync") -- reads the sync read list; reads the board and both handoffs; runs the drift check; rebases the stream that did not merge onto `main` after a merge; asks every pending owner decision as one batch and records the answers; updates the board; stops for `/clear`. Run one at the start of each working day and after every merge to `main`.

Two terminals may run a stream A block and a stream B block at the same time. A usage-limit stop (429) costs time only: the next session resumes from the stream's ledger.

## Context contract (built by the program tooling block)
- `docs/superpowers/state/program/context.yml` holds one read list per session kind (stream-a-block, stream-b-block, sync) and per dispatch role. An entry is `path`, `lines` (a range `a-b` of at most 2000 lines and at most 16,000 bytes: the Read tool returns at most 2000 lines per call and fails or truncates above 25,000 tokens (an error, a `PARTIAL view` of the first lines, or a `truncatedByTokenCap: true` tool_response, depending on the session), and dense text costs up to 1 token per byte plus about 1.3 per line (measured 2026-10-30; the plan program-tooling Global Constraints give the measurement), and at 314b46d the first 2000 lines of the S2 plan are 114,712 bytes, so a longer section is split into several entries) or `a-end` for a file that grows (handoffs, board, ledgers, registers: the loader resolves `end` to the file's current last line and splits the range into consecutive entries, each within 2000 lines and 16,000 bytes, and prints and records the resolved ranges; DEC-0166), `why`, and an optional `ref` (a git ref, for a file that lives on another branch; `ref: board:B` reads `origin/<branch>`, the branch being the first backticked name in the branch cell of stream B's board row, and a missing row, cell or ref FAILs; DEC-0167). Each checkpoint writes the next task's plan section into its handoff's `context_files` (path, lines, why), which the loader appends to the stream's list (DEC-0168).
- `tools/program/load_context.py <kind>` prints only the read list: for each entry the path to Read, the line range, the blob sha and the why. Its whole output stays under 16 KB, below the Bash tool's limit (a 25,000-byte output reached the context whole, a 30.1 KB one was moved to a file with a 2 KB preview, probes of 2026-10-29). An entry with a `ref` is written by the loader from `git show <ref>:<path>` to a git-ignored copy under `.superpowers/context/`, and that copy is the path to Read. A missing path, ref or line range is `FAIL context.<kind>.<path>` and rc 1. The loader also writes the expected list for the session.
- The session reads every entry with the Read tool (`offset` and `limit` set to the range). A PostToolUse hook on Read records the path, the blob sha of the file as read and the line range Read returned into the session's receipt.
- A PreToolUse hook on Edit, Write, NotebookEdit, Agent and the Bash commit verbs refuses until the receipt covers every expected entry: each range is inside ranges read from a file whose blob sha equals the file's current sha. The refusal names the entries still missing. A session with no expected list (the loader never ran) is refused too, and the refusal names the loader command.
- A session's own Edit, Write or NotebookEdit on a read-list file refreshes its receipt to the new blob sha, so its own handoff, ledger and board lines never lock it: PreToolUse stores the file's blob sha before the edit, keyed by the tool-use id, and PostToolUse refreshes only when that stored sha equals the receipt's. A change made by anyone else, or by a Bash command, still requires a new Read.

## Drift checks (the sync runs them; `tools/program/drift_check.py` once the program tooling block lands)
- every block on the board names the roadmap rows or requirement ids (F1-F12) it delivers, and every in-scope row of the prototype plan has an owner;
- a block past 1.5 times its estimate is reported to the owner at once;
- a stream branch behind `main` after the other stream merged is rebased in the sync;
- every register id lies in its stream's range;
- every pending owner decision is on the board;
- every citation in a handoff, the board or a brief has the form `path:line@sha "quote"`, where `sha` is a commit and the quote is found on that line of `git show <sha>:<path>`; the path is tracked at that commit; a bare `path:line` with no `@sha` and quote is a FAIL (citation check);
- every read-list path exists (at its `ref` when it has one).

## Stated limits
- The receipt proves that the session's Read calls returned every listed range of the current file; it does not prove that the content was understood. Reviews that execute every claim stay the net for wrong conclusions.
- The citation check verifies the citations a document contains; a file fact written with no citation at all passes it. The net for an uncited fact is the review, which executes every claim.
- A tracked document cites only tracked files: a git-ignored workspace file has no commit to cite, so it is imported into a tracked document first.
- A Bash command that changes a read-list file does not refresh the receipt; the session reads the file again.

## Hardware
Only stream B touches hardware. Never flash without the owner's explicit "ready"; after an OTA push never open the lap-timer port for 35 s. Power: one 5 V source at a time (DEC-0135).
