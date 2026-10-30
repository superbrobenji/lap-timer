---
plan: docs/superpowers/plans/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints.md
spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md
branch: block/strict-delivery-b5
head: "b1248ca"
base: "fe3f146"
ledger: docs/superpowers/state/strict-delivery/ledger.md
resume_line: 191
block: 5
next_task: "program tooling block (docs/superpowers/plans/2026-10-29-program-tooling.md, branch block/program-tooling from the b5 tip, PR base block/strict-delivery-b5, DEC-0148, DEC-0153), then the b5 lean close (umbrella 'Replan 2026-10-29' step 2)"
findings_in_flight: [{B3-S4PREPUSH-1: open}, {SD-S2-T1: pinned}, {SD-S2-T2: pinned}, {SD-S2-T3: pinned}, {SD-S2-T4: pinned}, {SD-S2-T5: pinned}, {SD-S2-T6: pinned}, {SD-S2-T7: planned}, {SD-S2-T8: planned}, {SD-S2-T9: planned}, {B4-FIX-1: pinned}, {B5-U1: open}, {B5-U2: open}, {B5-FIX-2: pinned}, {B5-FIX-3: pinned}, {B5-FIX-4: pinned}, {B5-FIX-5: planned}, {B5-U3: open}, {B5-FIX-6: pinned}, {B5-FIX-7: pinned}, {B5-FIX-8: pinned}, {B5-FIX-9: pinned}, {B5-FIX-10: planned}, {B5-FIX-11: pinned}, {B5-FIX-12: pinned}, {B5-FIX-13: pinned}, {B5-FIX-14: planned}, {B5-FIX-15: pinned}, {B5-FIX-16: planned}, {B5-FIX-17: pinned}, {B5-FIX-18: pinned}, {B5-FIX-19: pinned}, {B5-FIX-20: pinned}, {B5-FIX-21: pinned}, {B5-FIX-22: pinned}, {B5-FIX-23: pinned}, {B5-FIX-24: pinned}, {B5-FIX-25: pinned}, {B5-FIX-26: planned}, {B5-U4: open}, {B5-U5: open}, {B5-CAPS-1: pinned}, {B5-U6: planned}, {B5-U7: planned}, {B5-U8: planned}, {B5-PROC-1: planned}, {B5-PROC-2: planned}, {B5-PROC-3: planned}, {B5-UD-1: planned}, {B5-RD-1: planned}, {B5-CAPS-2: open}, {B5-RB-2: planned}, {B5-RB-3: planned}, {B5-RB-4: planned}, {B5-RA-6: planned}, {B5-RC-2: planned}, {B5-U9: planned}, {B5-U10: planned}, {B5-UF-2: planned}, {B5-UF-3: planned}, {B5-UF-4: planned}, {B5-UD-2: planned}, {B5-ID-1: planned}, {B5-ID-2: planned}, {B5-UR-1: planned}, {B5-UR-2: planned}, {B5-UR-3: planned}, {B5-UR-4: planned}, {B5-UR-5: planned}, {B5-UR-6: planned}, {B5-UR-7: planned}, {B5-UR-8: planned}, {B5-UR-9: planned}, {B5-UR-10: planned}, {B5-UR-11: planned}, {B5-UR-12: planned}, {B5-UR-13: planned}, {B5-UR-14: planned}, {B5-UR-15: planned}, {B5-UR-16: planned}, {B5-UR-17: planned}, {B5-UR-18: planned}, {B5-UR-19: planned}, {B5-UR-20: planned}, {B5-UR-21: planned}, {B5-UR-22: planned}, {B5-UR-23: planned}]
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings: []
grant: DEC-0001
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "the Agent hook is live from b4: write each dispatch prompt to a file and run gate_dispatch.py --prompt <file> --tool-model <m> before dispatching; a dispatch naming an unanswered DEC id is refused, so name the open D33 sweep questions by their decisions.yml line range, never by id"
  - "Bash output over about 25 KB reaches the context only as a 2 KB preview (probes 2026-10-29: 25,000 bytes whole, 30.1 KB moved to a file): read long files with the Read tool in ranges of at most 2000 lines and at most 16,000 bytes (the Read tool fails or truncates above 25,000 tokens (an error, a `PARTIAL view` of the first lines, or a `truncatedByTokenCap: true` tool_response, depending on the session) and dense text costs up to 1 token per byte plus about 1.3 per line, measured 2026-10-30; the first 2000 lines of the S2 plan are 114,712 bytes at 314b46d)"
  - "a usage-limit 429 terminates a sub-controller mid-loop: resume the same agent by message, finish the current task through its review loop, and ask the owner where the remaining tasks run (DEC-0008 precedent)"
  - "an implementer dispatch needs a planned or in-progress register row: register the task's row before dispatching"
  - "pre_tool_use.py refuses Bash command text holding -n/--no-verify after commit/push, core.hooksPath, GIT_CONFIG_*, or HOME=/XDG_CONFIG_HOME= before a verb, even inside a heredoc: put such text in files with the Write tool (git commit -F, probe scripts)"
  - "every commit, text-only included, first passes run_all and the gate suite on /usr/bin/python3 and tools/.venv/bin/python3 (spec D34); run the suite after every register edit and read the verdict line"
  - "paste prompt blocks from ~/.claude/skills/strict-delivery/prompts/ into the dispatch, never from memory (gate_dispatch compares byte for byte); controller.md is 5281 bytes of the 5500-byte cap"
  - "a ledger whose line 1 is not 'plan: <plan file>' turns run_all red (gate_plan plans_in_flight), and every plan in flight must pass gate_plan --plan"
  - "registers are shared with stream B: append whole rows only, never reorder (playbook 'Shared files')"
  - "git-pre-push.sh refuses any pushed sha that does not peel to HEAD (DEC-0006): push from the checked-out commit"
  - "before the b5 merge deletes the branch, push the annotated tag strict-delivery-b5-tip: stream B's rebase uses it"
  - "every agent reaps its background jobs before reporting; scratch probes run in a clone under the DEC-0032 guard, with no git config overrides (DEC-0143)"
  - "the gates CI job is still not a required status check on main (owner's action, carried from b2)"
---

Stream A handoff (playbook `docs/superpowers/playbooks/two-streams.md`), written at the 2026-10-29 replan; it replaces the b4 handoff.

State at `667e0d7`: b5 (S2 Tasks 4-6, `gate_review_package` and `gate_plan`) is open on `block/strict-delivery-b5`; the owner replanned the rest of the umbrella plan (DEC-0132..DEC-0163, umbrella "Replan 2026-10-29"). Next comes the program tooling block on its own branch and PR (DEC-0148), then the b5 lean close (umbrella step 2), then S3 Tasks 1-6 (b7, b8).

`head` and `resume_line` name `b1248ca`, the commit that landed this handoff, and the ledger's 191 lines at it. `findings_in_flight` lists every register row not closed or merged once the landing commit has closed PROG-1: the 88 rows not closed at `667e0d7` less PROG-1; PROG-2 (the MANIFEST citation-map rows, DEC-0165) is registered closed in the same commit, so it is not in flight either (87 rows). `open_decisions` is empty on purpose: the sixteen open D33 sweep questions are the unanswered rows from `docs/superpowers/state/decisions.yml:1028@667e0d7 "- id: DEC-0106"` to `docs/superpowers/state/decisions.yml:1303@667e0d7 "answered_at: null"`, deferred by the replan; naming one by id in a dispatch blocks it. PROG-1 (the replan documents) is closed in the commit that lands them (DEC-0158), not pinned: history planned -> open -> closed (the transitions `tools/gates/lib/registers.py` allows, each new entry at the existing commit `667e0d7`), `ruling: DEC-0158`, `pin: null`, evidence the tracked imported re-review reports `docs/superpowers/state/program/sources/program-docs-review.md` and `docs/superpowers/state/program/sources/program-docs-rereview2.md` and no commit sha (a commit cannot name its own). The landing commit writes that row into `docs/superpowers/state/findings.yml`; `docs/superpowers/state/strict-delivery/mutations/PROG-1.txt` replays the row's repro command and claims no pin.

Resume prompt (a fresh session, after `/clear`):
```
Invoke the strict-delivery skill. Read, in this order: docs/superpowers/playbooks/two-streams.md, docs/superpowers/state/program/board.md, docs/superpowers/state/strict-delivery/handoff.md, and docs/superpowers/plans/2026-10-29-program-tooling.md (the umbrella docs/superpowers/plans/2026-10-17-strict-delivery.md section "Replan 2026-10-29" gives the order). Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Create block/program-tooling from the tip of block/strict-delivery-b5 and run the program tooling block (Tasks 1-3) with superpowers:subagent-driven-development, models per tools/gates/config.yml, lean path (DEC-0145). Stop at the block's checkpoint for /clear.
```
