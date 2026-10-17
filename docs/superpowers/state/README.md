# Strict-delivery state

The registers are the only place a finding, a claim or an owner decision exists
(spec `docs/superpowers/specs/2026-10-17-strict-delivery-design.md` §4). Schemas: the umbrella
plan `docs/superpowers/plans/2026-10-17-strict-delivery.md`, "Registers"; enforced by
`tools/gates/lib/registers.py` and the gates in `tools/gates/`.

| File | Holds | Gate |
|---|---|---|
| `findings.yml` | one row per finding, with owner, lifecycle state and evidence | `gate_register` |
| `claims.yml` | one row per factual statement in a tracked document | `gate_claims` |
| `decisions.yml` | owner questions and merge grants (`grant:<plan>`) | `gate_decisions` |
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
  stale (`gate_claims` fails it; `gate_claims.py --mark-stale --base <sha>` rewrites such rows as
  `stale` through PyYAML, so comments in `claims.yml` are not kept: keep none there). Every cite is a
  normalised repo-relative path (decision DEC-0004): no `./`, `..`, leading `/`, `//`, NUL or
  surrounding space; a directory cite ends in exactly one `/`. `gate_claims` also fails a
  `verified` row whose cite is not a file (or, with a trailing `/`, a directory) at `verified_at`,
  because such a row could never go stale. `false` needs `finding` = a registered finding id.
- Decisions: only the owner answers. The owner states the answer in chat; the session records it
  verbatim (a quoted string) with `answered_at.by: superbrobenji`, the owner handle in
  `tools/gates/config.yml`, and quotes the owner's words in the commit message. An agent never
  writes an answer of its own. `asked_at.sha` / `answered_at.sha` are the `main` commit at the time the
  row was created/answered (a question asked before the register existed records the register's
  creating commit).
- A git pre-commit hook that refuses a commit while this directory has unstaged edits arrives in block b3.
