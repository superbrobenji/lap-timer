---
plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md
spec: docs/superpowers/specs/2026-09-14-lap-timer-design.md
branch: block/prototype-b1
head: "a6e475e"
base: "39a3e8a"
ledger: docs/superpowers/state/prototype/ledger.md
resume_line: 39
block: 1
next_task: "b1 fix round per DEC-0325 script-gate-first, once stream A lands the option-line gate; apply DEC-0326..DEC-0331"
findings_in_flight: ["PT-B1-T1: in-progress", "PT-B1-T2: in-progress", "PT-B1-T3: in-progress"]
claims_verified_at_head: []
live_subagents: []
open_decisions: []
rulings: ["b2 spec amendments (7.5, 8.5, the 10.2 example) all in Task 6; Tasks 4 and 5 list no spec file (ledger :31)", "superseded by DEC-0300: b1 packages carry a placeholder grep transcript, no checker (ledger :22)", "withdrawn: no transcript for tracker rows (ledger :13)", "withdrawn: BOM not built = not in hand (ledger :15; DEC-0309)"]
grant: DEC-0155
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "DEC-0300..DEC-0331 live only in docs/superpowers/state/prototype/pending-decisions.yml until stream A lands DEC-0314 (test_registers.py:54 gapless ids); a dispatch prompt that names them is refused by the Agent hook (gate_dispatch dispatch.decisions): name the pending file and the option labels in the prompt, and the DEC ids only in brief files"
  - "this session's worktree isolation refuses compound shell commands (variables, heredocs feeding scripts that name git, chained git): run plain commands, and write files with the Write/Edit tools"
  - "never flash without the owner's explicit 'ready'; after an OTA push never open the lap-timer port for 35 s; one 5 V source at a time (DEC-0135)"
  - "with the dev-kit wired, USB esptool fails on the shared UART0 RX: flash via the dev-kit OTA push, or hold the dev-kit in reset for USB"
  - "ESP32 consoles use CR line endings: host tools send a bare \\r"
  - "build only via source tools/idf-env.sh (pins IDF 5.3.2); zero warnings; every firmware gate list builds test_apps/core_selftest"
  - "files outside stream B's ownership (playbook 'Ownership'), ci.yml included, are raised at the next sync, never edited"
  - "stream B ids: decisions DEC-0300..DEC-0499, findings prefix PT-; registers are append-only (playbook 'Shared files')"
  - "the Agent hook refuses a dispatch that names an unanswered DEC id or breaks the umbrella dispatch grammar: run gate_dispatch.py --prompt <file> --tool-model <m> first (mkdispatch.sh under .superpowers/sdd/prototype/ does it)"
  - "every commit, text-only included, first passes run_all and the gate suite on both interpreters (spec D34)"
  - "a controller brief or answer-field summary is text the implementer copies: quote option lines only, never offer wording no answer states (ledger :36)"
---

Stream B handoff (playbook `docs/superpowers/playbooks/two-streams.md`). State at 2026-10-30 (session 3): block b1 stopped under spec D30 a third time, not at its checkpoint.

Session 3 ran the owner's one combined fix round (DEC-0315): Task 2 feed8fc, Task 1 7530cf3, Task 3 a6e475e, each re-reviewed by opus in a scratch clone. All gates green at a6e475e. Every prior finding was resolved, but each re-review again found defects the round caused, and a read-only investigation shows they are not converging (ledger :35-36). No further fix round until the owner answers DEC-0325 (how b1 continues) and the content questions DEC-0326..DEC-0331, staged unanswered in `docs/superpowers/state/prototype/pending-decisions.yml` (decisions.yml still rejects stream B ids until stream A lands DEC-0314). Open review reports: `.superpowers/sdd/prototype/reviews/b1-T1-rereview-3.md`, `b1-T2-rereview-3.md`, `b1-T3-rereview-1.md`; the investigation `b1-d30-3-investigation.md` (all git-ignored; closing PT-B1-T1..T3 under DEC-0300 imports the final reports into a tracked path). Issue #151's body is stale against the plan (ledger :37). Issues: #151 (DEC-0304 umbrella), #152 (spec 8.5 FT=0, DEC-0324).

2026-10-31: the owner answered DEC-0325..DEC-0331 (ledger :39). DEC-0325 script-gate-first: stream A builds the gate that flags every DEC id use without its chosen option line quoted (tools/gates is stream A's, raised at the next sync, never edited here); b1's fix round runs on that gate's output. Until it lands, b1 waits.

`grant: DEC-0155` (grant:prototype-field-ready = yes): a block PR merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch (DEC-0154), then when every check passes, on the owner's sentence "merge when all checks pass".

Resume prompt (a fresh session, after `/clear`, once stream A's option-line gate is on this branch):
```
Invoke the strict-delivery skill. Read, in this order: docs/superpowers/playbooks/two-streams.md, docs/superpowers/state/program/board.md, docs/superpowers/state/prototype/handoff.md, docs/superpowers/state/prototype/ledger.md, docs/superpowers/state/prototype/pending-decisions.yml and docs/superpowers/plans/2026-10-29-prototype-field-ready.md. Enter the worktree ../lap-timer-worktrees/prototype (branch block/prototype-b1). Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Continue b1 as the owner's DEC-0325 answer says (one fix round driven by stream A's option-line gate output), applying DEC-0326..DEC-0331 and the open findings of .superpowers/sdd/prototype/reviews/b1-T1-rereview-3.md, b1-T2-rereview-3.md and b1-T3-rereview-1.md (read b1-d30-3-investigation.md first), with superpowers:subagent-driven-development, models per tools/gates/config.yml, lean path (DEC-0145). Never flash without the owner's "ready". Stop at b1's checkpoint for /clear.
```
