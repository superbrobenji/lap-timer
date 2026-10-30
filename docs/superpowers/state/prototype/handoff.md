---
plan: docs/superpowers/plans/2026-10-29-prototype-field-ready.md
spec: docs/superpowers/specs/2026-09-14-lap-timer-design.md
branch: block/prototype-b1
head: "f5f35d6"
base: "39a3e8a"
ledger: docs/superpowers/state/prototype/ledger.md
resume_line: 21
block: 1
next_task: "b1 continuation chosen by DEC-0315 (fix round over Tasks 1-3, or investigation first) after the owner answers DEC-0315..DEC-0324"
findings_in_flight: ["PT-B1-T1: in-progress", "PT-B1-T2: in-progress", "PT-B1-T3: in-progress"]
claims_verified_at_head: []
live_subagents: []
open_decisions: ["DEC-0315..DEC-0324 (docs/superpowers/state/prototype/pending-decisions.yml, unanswered)"]
rulings: ["superseded by DEC-0300: b1 packages carry a placeholder grep transcript, no checker (ledger :22)", "withdrawn: no transcript for tracker rows (ledger :13)", "withdrawn: BOM not built = not in hand (ledger :15; DEC-0309)"]
grant: DEC-0155
parked_branches:
  - {name: s5.2-conn-ble, reason: "BLE deferred to sub-project C", owner: "sub-project C"}
env: {idf: "source tools/idf-env.sh", flash: "owner says ready", ota_window_s: 35}
traps:
  - "DEC-0300..DEC-0314 are answered but live only in docs/superpowers/state/prototype/pending-decisions.yml until stream A lands DEC-0314 (test_registers.py:54 gapless ids); a dispatch prompt that names them may be refused by the Agent hook: name the pending file and the option labels instead, or ask stream A first"
  - "block/prototype-b1 does not exist yet: the first stream B session creates it from the tip of block/strict-delivery-b5 (main has neither DEC-0132..DEC-0157 nor the b5 gates the plan must pass)"
  - "never flash without the owner's explicit 'ready'; after an OTA push never open the lap-timer port for 35 s; one 5 V source at a time (DEC-0135)"
  - "with the dev-kit wired, USB esptool fails on the shared UART0 RX: flash via the dev-kit OTA push, or hold the dev-kit in reset for USB"
  - "ESP32 consoles use CR line endings: host tools send a bare \\r"
  - "build only via source tools/idf-env.sh (pins IDF 5.3.2); zero warnings; every firmware gate list builds test_apps/core_selftest"
  - "files outside stream B's ownership (playbook 'Ownership'), ci.yml included, are raised at the next sync, never edited"
  - "stream B ids: decisions DEC-0300..DEC-0499, findings prefix PT-; registers are append-only (playbook 'Shared files')"
  - "the Agent hook refuses a dispatch that names an unanswered DEC id or breaks the umbrella dispatch grammar: run gate_dispatch.py --prompt <file> --tool-model <m> first"
  - "every commit, text-only included, first passes run_all and the gate suite on both interpreters (spec D34)"
---

Stream B handoff (playbook `docs/superpowers/playbooks/two-streams.md`). State at 2026-10-30 (session 2): block b1 stopped under spec D30 a second time, not at its checkpoint.

Session 2 applied DEC-0300..DEC-0314, ran the one fix wave (Task 1 db8be72, Task 2 cd84605) and Task 3 (f5f35d6, b2-b6 written as Tasks 4-18), each with an opus review. All gates green at f5f35d6. Every review found open text defects (ledger :25); the fix wave again introduced some, so no further round runs until the owner answers DEC-0315 (how b1 continues) and the content questions DEC-0316..DEC-0324, staged unanswered in `docs/superpowers/state/prototype/pending-decisions.yml` (decisions.yml still rejects stream B ids until stream A lands DEC-0314). Issues: #151 (DEC-0304 umbrella, body regenerated from the plan at f5f35d6), #152 (spec 8.5 FT=0, DEC-0324). Review reports, briefs and packages (placeholder transcripts under DEC-0300) are git-ignored under `.superpowers/sdd/prototype/`; closing PT-B1-T1..T3 under DEC-0300 imports the final review reports into a tracked path and needs DEC-0300 in decisions.yml.

`grant: DEC-0155` (grant:prototype-field-ready = yes): a block PR merges only after strict-delivery b5 has merged and a sync has rebased stream B onto `main`; until then stream B blocks accumulate on its branch (DEC-0154), then when every check passes, on the owner's sentence "merge when all checks pass".

Resume prompt (a fresh session, after `/clear`, once the owner has answered DEC-0315..DEC-0324):
```
Invoke the strict-delivery skill. Read, in this order: docs/superpowers/playbooks/two-streams.md, docs/superpowers/state/program/board.md, docs/superpowers/state/prototype/handoff.md, docs/superpowers/state/prototype/ledger.md, docs/superpowers/state/prototype/pending-decisions.yml and docs/superpowers/plans/2026-10-29-prototype-field-ready.md. Enter the worktree ../lap-timer-worktrees/prototype (branch block/prototype-b1). Run python3 tools/gates/run_all.py at HEAD and stop on any FAIL. Apply the owner's answers to DEC-0315..DEC-0324 and continue b1 as DEC-0315 says (open findings: the review reports under .superpowers/sdd/prototype/reviews/ named in the ledger), with superpowers:subagent-driven-development, models per tools/gates/config.yml, lean path (DEC-0145). Never flash without the owner's "ready". Stop at b1's checkpoint for /clear.
```
