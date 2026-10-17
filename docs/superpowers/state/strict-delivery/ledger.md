# strict-delivery ledger

Umbrella: docs/superpowers/plans/2026-10-17-strict-delivery.md. S1: docs/superpowers/plans/2026-10-17-strict-delivery-S1-registers-core-gates.md.
Spec: docs/superpowers/specs/2026-10-17-strict-delivery-design.md. Grant: decisions.yml grant:strict-delivery.
Lines: `- T<n> <commit subject> -- <gate result>`; `Ruling: what -- why -- cost if wrong -- spec cite`; `CHECKPOINT b<N> ...`.

## Block b1 (branch block/strict-delivery-b1)
- base BASE_B1=73cd3cc
- T1 feat(gates): gate framework, config, git helpers, register bootstrap with DEC-0001 -- test_lib 21 OK
- T2 feat(gates): register loaders, schema validators and lifecycle rules -- test_registers 28 OK
