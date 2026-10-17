# strict-delivery ledger

Umbrella: docs/superpowers/plans/2026-10-17-strict-delivery.md. S1: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md.
Spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md. Grant: decisions.yml grant:strict-delivery.
Lines: `- T<n> <commit subject> -- <gate result>`; `Ruling: what -- why -- cost if wrong -- spec cite`; `CHECKPOINT b<N> ...`.

## Block b1 (branch block/strict-delivery-b1)
- base BASE_B1=73cd3cc
- T1 feat(gates): gate framework, config, git helpers, register bootstrap with DEC-0001 -- test_lib 28 OK
- T2 feat(gates): register loaders, schema validators and lifecycle rules -- test_registers 34 OK
- T2 plan-text corrections: __future__ import; evidence path-escape fix; counts 34 / 62
- T2 review round 1: fullmatch ids, grant owner check, evidence relative paths, validator robustness; test_registers 34, suite 62
- T2 review round 2: pin-transcript relative-path message pinned; plan text aligned
- T3 feat(gates): gate_register -- test_gate_register 12 OK; suite 74 OK; gate_register at HEAD 6 PASS
- T3 plan-text corrections: counts 12 / 74 (brief said 11 / 59); run() puts --json before --root; os.walk onerror -> GateError; LIST_ITEM captures a token and fullmatches FINDING_ID; known built from string ids; __future__ in the test
- T3 review round 1: onerror pinned; non-directory report path FAILs; symlinked report dirs followed (cycle-safe); LIST_ITEM per ruling 5 (no bare id at end of line); DEC-0002 asked; test_registers expects DEC-0001 and DEC-0002; test_gate_register 15, suite 77
