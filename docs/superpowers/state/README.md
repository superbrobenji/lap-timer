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
| `<plan>/ledger.md` | the plan's progress ledger, rulings, `CHECKPOINT b<N>` lines | by hand until `gate_checkpoint` |
| `<plan>/handoff.md` | the checkpoint handoff | by hand until `gate_checkpoint` |
| `<plan>/mutations/<id>.txt` | mutation transcripts that pin a finding | `gate_register` (exists), `gate_review_package` |

## Editing rules

- Each file is a YAML list; an empty register is `[]`, never an empty file.
- Quote every sha (`sha: "7265059"`): an all-digit sha is read as a number and fails.
- Quote `answer: "yes"` and `state: "false"`: YAML reads bare yes/no/on/off/true/false as booleans.
- No duplicate keys and no merge keys (`<<`): the loader refuses both.
- Findings: history starts at `open`, every step is a legal transition, the last history state
  equals `state`; `pinned` needs `pin.test`, an existing `pin.transcript` and the reviewer's
  report path in `evidence`; `deferred` needs `ruling` = an answered decision; `closed` needs
  evidence; `merged` is written only by the post-merge script (`by: postmerge`).
- Claims: `verified` needs `verified_at` and `by`; a verified row whose cited file changes goes
  stale; mark it with `python3 tools/gates/gate_claims.py --mark-stale --base <sha>` (this
  rewrites `claims.yml` through PyYAML, so comments in it are not kept: keep none there).
  `false` needs `finding` = a registered finding id.
- Decisions: only the owner answers. The owner states the answer in chat; the session records it
  verbatim (a quoted string) with `answered_at.by: superbrobenji`, the owner handle in
  `tools/gates/config.yml`, and quotes the owner's words in the commit message. An agent never
  writes an answer of its own.
- The git pre-commit hook refuses a commit while this directory has unstaged edits.
