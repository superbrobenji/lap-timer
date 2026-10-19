# Fixture Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development.

**Goal:** Exercise gate_plan.

**Architecture:** Two blocks; one parallel pair in the first.

**Tech Stack:** Python 3.9.

**Spec:** `docs/cited.md`

## Global Constraints

- The fixture has no constraints beyond itself.

## Review Focus

1. Overlap between parallel tasks.

---

## Block b1 — first

### Task 1: alpha

**Mode:** PARALLEL

**Files:**
- Create: `src/alpha.py`
- Test: `tests/test_alpha.py`

**Findings:** SD-FX-1

**Siblings:** sib-a

**Gates:** run_all

- [ ] **Step 1: Write the failing test**

- [ ] **Step 2: Mutation run and pin** — the pin test is `test_alpha::test_alpha_fails`.

### Task 2: beta

**Mode:** PARALLEL

**Files:**
- Create: `src/beta.py`
- Modify: `src/shared.py:10-20`
- Create: `tools/gates/gate_beta.py`

**Findings:** SD-FX-2

**Siblings:** none -- the register row lists no siblings

**Gates:** run_all, beta

- [ ] **Step 1: Pin**

```bash
python3 -m unittest tools.gates.tests.test_beta  # pin test_beta::test_beta_fails
```

- [ ] **Step 2: CHECKPOINT b1**

## Block b2 — second

### Task 3: gamma

**Files:**
- Modify: `src/shared.py`

**Findings:** none — fixture maintenance with no register row

**Siblings:** none -- no register row

**Gates:** beta

- [ ] **Step 1: CHECKPOINT b2**
