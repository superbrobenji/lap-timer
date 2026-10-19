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

- [ ] **Step 1: Write the failing test**

### Task 2: beta

**Mode:** PARALLEL

**Files:**
- Create: `src/beta.py`
- Modify: `src/shared.py:10-20`

**Findings:** SD-FX-2

- [ ] **Step 1: CHECKPOINT b1**

## Block b2 — second

### Task 3: gamma

**Files:**
- Modify: `src/shared.py`

**Findings:** none — fixture maintenance with no register row

- [ ] **Step 1: CHECKPOINT b2**
