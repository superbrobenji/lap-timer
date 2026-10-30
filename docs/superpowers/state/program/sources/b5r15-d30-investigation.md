status: DONE_WITH_FINDINGS

# b5 round 15 D30 investigation: gate_plan fails open when git fails (B5-PROC-2 context)

Read-only. Scratch: `$S=/private/tmp/claude-501/-Users-~-projects-personal-lap-timer/b1ec9f20-b80f-4fda-abcf-bfd5962cee25/scratchpad/r15-d30`. `$S/repo` is a `git clone -q --no-hardlinks` of the checkout, checked out detached at e17dc90. `$S/ng` is a `git archive e17dc90 | tar -x` copy (not a repository). `$S/c` and `$S/e` are clones used for the git-failure states. Every writing script starts with `set -eu` and refuses to run unless `pwd -P` and `git rev-parse --show-toplevel` are under `$S` (scripts `$S/run.sh` and `$S/run2.sh`). `$S/e` gets a scratch commit ("closed", signing off, `--no-verify`). Interpreters: `/usr/bin/python3` (3.9.6) and `~/projects/personal/lap-timer/tools/.venv/bin/python3` (3.12.14). `/opt/homebrew/bin/python3.12` has no PyYAML (`ModuleNotFoundError: No module named 'yaml'`), so it was not used. Every python run had PYTHONDONTWRITEBYTECODE=1 and a fresh PYTHONPYCACHEPREFIX.

## commits
none (read-only; the scratch commit in `$S/e` is not in the checkout)

## gates run
- `gate_plan.py --root $S/ng --in-flight`, both interpreters: rc=0, 69 lines, 69 PASS, 0 FAIL.
- `gate_plan.py --root $S/repo --in-flight`, both interpreters: rc=0, 69 PASS, 0 FAIL (the same as the non-git copy).
- `run_all.py --root $S/ng` (3.9): rc=1, 89 PASS, FAIL `register.input` and `decisions.input` (`git cat-file -e ...: fatal: not a git repository`).
- `run_all.py --root $S/e --only plan` (state E2 below, 3.9): rc=0, `PASS plan.in_flight`.
- No unittest suite and no full run_all were run at e17dc90. They were not needed for the question, and the checkout is not committed to.

## 1. Reproduction at e17dc90 (direct, then by contradiction)

| case | state | gate_plan --in-flight (3.9.6 and 3.12.14, identical) |
|---|---|---|
| A | `$S/ng`: non-git copy, work-tree ledger present | rc=0, 69 PASS, 0 FAIL; no `plan.input` FAIL (DEC-0068 a requires one) |
| B (contradiction of A) | `$S/repo` intact clone | rc=0, 69 PASS: in the non-git root the work-tree ledger reads in flight because the git copies are None, so A alone hides no plan |
| C | `$S/c`: git works, ledger `rm`'d from the work tree, plan made bad (`### Task 99` + `TBD`) | rc=1, 75 lines, 5 FAIL, first `FAIL plan.strict-delivery.files.99` |
| D | the same tree with `.git/HEAD` = `garbage` (`git status` -> `fatal: not a git repository`) | **rc=0, 1 line `PASS plan.in_flight`**: the bad plan is never checked (fail-open) |
| E0 | `$S/e`: ledger + `CHECKPOINT b5 tip x -- test` committed | rc=0 `PASS plan.in_flight` (closed, correct) |
| E1 | B5-FIX-5 repro (a): `rm` the ledger, git works | rc=0, 69 `plan.strict-delivery.*` lines (in flight, correct) |
| E2 | the same, `.git` moved away (the state of the b5r7 incident, progress.md:142) | **rc=0, `PASS plan.in_flight`** (fail-open) |
| E3 | `rm` the ledger, `.git/index` cut to 20 bytes (ls-files and `git show :` fail, `ls-tree HEAD` works) | 69 slug lines: in flight (fail-closed) |
| F | `rm` the ledger, index cut, HEAD = `ref: refs/heads/unborn`; `git rev-parse --show-toplevel` still prints `$S/e` | **rc=0, `PASS plan.in_flight`**: open even with a toplevel that DEC-0068 a's check would accept |

Every state was restored afterwards (`git status --short | wc -l` -> 0 each time).
Mechanism, from code read at e17dc90: `plans_in_flight` (gate_plan.py:615-616) takes ledger names from the work tree ∪ `_git_ledgers(ls-files)` ∪ `_git_ledgers(ls-tree HEAD)`. `_git_ledgers` (:530-531) ignores `r.returncode`, so a failing listing adds nothing. A ledger that is only in the index or HEAD then gets no plan check.
Unit probe (3.9): `_git_ledgers($S/ng, ['ls-files'])` -> `set()`, `_git_bytes(...)` -> None, `_git_mode(...)` -> None, `test_modules($S/ng)` -> 0 keys. The same calls on `$S/repo` give `{'strict-delivery'}`, a copy, `100644` and 288 keys.
Reach: full run_all still FAILs in a non-git root, through register and decisions. `run_all --only plan` and a direct `gate_plan --in-flight` PASS (E2). The pre-commit hook runs only `register,claims` (git-pre-commit.sh:57,60).

## 2. Where it entered

- The idiom (git failure read as "no value") is in the approved plan text. S2 plan at fe3f146, Task 6 "gate_plan and the ledger contract" (:2630), code :3177-3180: `_committed` ... `return r.stdout if r.returncode == 0 else None`. It landed in 6b15653 (2026-10-19 20:46, `feat(gates): gate_plan ...`). There, ledger names came from the work tree only, so a failure read as in flight: fail-closed.
- The fail-open entered at **c0316eb** (2026-10-23 23:13, `fix(gates): B5-FIX-5, B5-FIX-6 ...`, round-7 fix wave, task S2-b5-finalfix2, implementer prompt b5r7-impl-prompt.md). This commit added `_git_ledgers` (blame: :528-535 = c0316ebd), so names now come from git listings. Its own comment at :531 records the choice: "a failing listing (no HEAD, not a repository) prints nothing on stdout". The :525 `returncode == 0 else None` is from 4b8a9d6 (B5-FIX-4, 10-23 21:12).
- `_git_mode` :556-557 is from **d3ed28d** (2026-10-26 18:55, `B5-FIX-5,-8,... fix round 1`, the R-A3-1 fix). `_git_bool` :582 is from 16766df (10-26 21:14, DEC-0049 core.symlinks). `test_modules` :193 is from c2badae (10-26 19:50, DEC-0037).
- Ancestry: `git merge-base --is-ancestor` reports c0316ebd, d3ed28de and 6b15653 as NOT ancestors of fe3f146, and all are ancestors of e17dc90; fe3f146 is an ancestor of e17dc90. All of this is b5 code.
- Plan text and D24 rows (S2 plan at e17dc90, section "b5 input domain and whitelists (DEC-0059)", :6916, written at 3313502, 2026-10-27 09:49):
  - T4.12 (:6937, gate_review_package) does list "or not in a repo (test_modules runs `git ls-files` with cwd = root)", with expected "FAIL review_package.input (DEC-0068, Q3 a); today ... no repo: none resolve".
  - T6 (gate_plan, :6978-7006) has **no** "not a repository" or "git command fails" value. T6.11/T6.12 (git object state) assume git answers. T6.12 expects "deleted in the work tree ... in flight", and E2/D/F contradict that expectation. T6.18/T6.19 (root form, DEC-0068) list only "a subdirectory of the repo top" and "through a symlink, or CLAUDE_PROJECT_DIR not the toplevel".
  - The plan text itself carries the code as written: :6609-6612 docstring "None when it fails (... not a repository)", :6618 the "failing listing" comment, :3459 "a failing git prints nothing".
- DEC-0068 (decisions.yml:631-639), option a: "root must equal the realpath of `git rev-parse --show-toplevel`, else FAIL `<gate>.input`: one lib check and 3 tests; B5-FIX-20's subdirectory support becomes a refusal." It was asked and answered at 740f6e9 (2026-10-27 10:23) and registered at 25848b3 (10-27 13:05). The code landed 10-23..10-26, so every git call site above predates the rule.
- `git grep -c DEC-0068 e17dc90 -- docs tools` -> plan 6, decisions.yml 1, findings.yml 0. No register row and no task brief owns implementing DEC-0068. The same holds for 16 of the 21 amendment answers DEC-0066..0086: findings rows cite only DEC-0066 (3), -0067, -0082 and -0085 (1 each).
- Neighbour already raised: b5r15-rr-skeleton.md:186 says lib `repo_root()` returns CLAUDE_PROJECT_DIR unchecked, against DEC-0068 a.

## 3. Why each review with the code in scope missed it

| review (sha) | what it did with git failure | why it missed |
|---|---|---|
| b5r7-review1 (c0316eb, BLOCKED) | :26 case "15 not a git repo -> in flight" | It tested a non-repo root only with the ledger in the work tree. Its case 2 ("deleted in working tree") ran only with working git. The product (deleted × git failing) was never run, and with no rule yet, "in flight" was accepted as correct. |
| b5r8-review1 (c0316eb) | :24 "14 (not a repo)" in flight | same matrix, same gap |
| b5-final-A3 review + verify (head c0316eb) | A3-R4 / A3-V5: root a **subdirectory** (became B5-FIX-20) | It looked at root vs toplevel, never at "no toplevel". Its per-branch mutation sweep of `_git_bytes` and `_git_ledgers` mutates existing branches, and an absent rc check has no branch to mutate. |
| b5r9-rr-A3 (f13930c) | :112 K7 "`_git_mode` empty -> None is safe ... SURVIVED (equivalent)" | It argued the None default safe for `_git_mode` only (true: see §5) and did not carry the question to `_git_ledgers`. |
| b5r10-rr-A3 (6aaba81), b5r11-rr-A3 (46e4c17), b5r11-rr-P2A3 (16766df) | 0 hits for not-a-repo or git failure (grep) | Scoped to the B5-FIX-5 siblings (mode, type, pathspec, fileMode). M16/M17 ("no ls-files / no HEAD ledger list") were killed: they prove the listings are used, not that a failing one is caught. |
| b5r12-rr-amend (3313502), the D24 table review | :77 "Not run ... T6.18, T6.19 (root forms: need a second repo layout; budget)" | The T6 table had no not-a-repo row to run, and the two root rows it did have were skipped for budget. |
| b5r10-rr-A1 / R-A1-3 N18 -> c2badae / 79f0d16 | added `test_a_root_git_cannot_list_resolves_no_test_name` (test_gate_review_package.py:496-502) | This **pinned** the silent behaviour of :193 as intended ("the siblings check FAILs, the gate never raises"). That was before DEC-0068. Nobody revisited it afterwards, although T4.12 now expects `review_package.input`. |
| b5r14-draft-helper / tablerev-helper (f1057e5) | FH.51 / FH.53 "git failure ... OPEN Q-FH-16"; Q-FH-16 option a names "gate_plan.py:524/:530/:556 change" (b5r14-draft-helper.md:178) | The drafter saw the sites but framed them as an open question, not a defect, and did not see that DEC-0068 a settles the not-a-repository value. The table review marked FH.51 "observed", but its command `GP --root $S/out3 --plan docs/x.md` exits at the missing findings.yml before git runs (tablerev-helper.md:51), so the "today" cell observed a different path. b5r15-rr-helper caught this (R2-FH-1). |

Greps used: `grep -nE "not a git repo|not a repository|no repository|not in a repo|_git_ledgers|_git_bytes|_git_mode|git failure|failing git|failing listing|returncode|DEC-0068"` over progress.md and every b5 review report. Hits are cited above, and the files with 0 hits are listed in the table.

## 4. Does round 13 (RC-A..RC-E) explain it?

Partly. Three of its root causes cover parts of this defect:
- **RC-A** (the D24 table is a drafter-chosen value list, and folding an answer does not re-derive the rows it reaches) explains why T6 lacks "not a repository" while T4.12 has it after the same DEC-0068 fold.
- **RC-C** (an idiom found in one place is not swept) applies because tools/gates/lib/gitinfo.py:1-4 already has the checked form ("a non-zero rc raises GateError"), and make_review_package `_git` (:50-56) raises on rc≠0. Meanwhile gate_plan grew four unchecked local wrappers. This mirrors `fullmatch` in registers.py versus `match` elsewhere.
- **RC-E** (today cells not mechanical) explains FH.51 being "observed" through a path that never reaches git.

Not covered by RC-A..E (new):
- **RC-F, dimensions are tabled one at a time.** The fail-open needs two values at once: work-tree deletion (git object state) and a failing git command. Every matrix (b5r7 15 vs 2, b5r8 14 vs 2) ran each value alone. RC-A speaks of path × dimension, not dimension × dimension. The new D31 skeleton (spec :45) also crosses paths with dimensions, not dimensions with each other.
- **RC-G, mutation review cannot find an omitted error check.** D27 and every reviewer sweep (A3 verify, b5r9 K7, b5r10 M16/M17) mutate branches that exist. A missing `returncode` test has no branch to mutate, so a "100 % killed" transcript says nothing about it.
- **RC-H, an answered spec-direction DEC that changes code gets no register row.** DEC-0068 has 0 findings rows, and 16 of the 21 amendment answers have none. Nothing makes an implementer apply DEC-0068 to existing code. The rule lives only in D24 cells, and earlier tests pin the opposite behaviour (test_gate_review_package.py:496-502).

## 5. Class statement and D33 hits

Class (by input dimension): **outcome of a git subprocess whose output a gate reads.** The values: the root is not a repository, `.git` is missing or unreadable, the index is unreadable, HEAD is unresolvable, a config value is invalid, git is missing (OSError). In each, the gate reads the empty or None result as data ("no ledger", "no copy", "default mode", "no tracked tests") instead of FAILing `<gate>.input`.
DEC-0068 a settles the not-a-repository value. Failure inside a repository is OPEN (Q-FH-16). Whether D/E2/F fall inside B5-FIX-5's class, which is open, planned and unpinned (findings.yml:574-611; its repro (a) is exactly E1/E2), or form a new row is an owner class call (D26/D33). See Decision needed.

D33 sweep: `grep -rn 'subprocess.run(\["git"\|check=False' tools/gates/*.py tools/gates/lib/*.py`

| hit | on failure | direction and reproducer |
|---|---|---|
| gate_plan.py:530 `_git_ledgers` | empty set | **fail-open**: D, E2, F above; A (non-repo, no `plan.input`) |
| gate_plan.py:524-525 `_git_bytes` | None | Fail-closed. E3 (index unreadable) -> in flight. If all three copies are None and the name came from a listing, `newest=b""` gives `FAIL plan.input: line 1 must be 'plan: ...'` (code :621-628). This breaks DEC-0068 a only in the non-repo case (no input FAIL), as in A. |
| gate_plan.py:556-557 `_git_mode` | None -> modes differ -> in flight | Fail-closed. It is reached only when `copies[0]==copies[1]==copies[2]` (short-circuit at :630), i.e. right after both `git show` calls succeeded, so failure needs a race between calls. No reproducer; the stated reason is that it is unreachable without one. |
| gate_plan.py:582 `_git_bool` | "true" | Not independently reachable. A bad value (`core.fileMode maybe`) gives `fatal: bad boolean config value` from `git config --bool` and from `git ls-files` alike (both run in `$S/c`), so :530 decides first. Outside a repository it is not a failure: it reads `~/.gitconfig` (`git config --show-origin --bool core.fileMode` in `$S/ng` -> `file:~/.gitconfig false`, rc 0), and all ledgers are in flight by bytes anyway. |
| gate_review_package.py:193 `test_modules` | `{}` | Fail-closed with the wrong check: `FAIL review_package.siblings: ... defined in no test module named` (pinned by test :496-502). T4.12 expects `review_package.input`. Unit probe: 0 keys in `$S/ng` vs 288 in `$S/repo`. |
| make_review_package.py:70 `merge-base --is-ancestor` | rc 1 and rc 128 both -> "HEAD is not a descendant" | Fail-closed, wrong message. Reachable only with both shas verified first (:66), so it needs corruption between calls. Code read only; not run. |
| make_review_package.py:53, lib/__init__.py:65, lib/gitinfo.py:30/:106/:143 | raise on rc≠0 | the checked form; not hits |

## 6. Process changes for the owner (ranked; each option's cost)

1. **Fault-injection suite test (covers RC-G, RC-F).** One test that runs every gate's existing fixtures with `git` on PATH replaced by a stub that exits 128. Any verdict that turns from FAIL or in-flight to PASS, or any gate that does not FAIL `<gate>.input`, FAILs the suite.
   - Cost: about 1 test module plus a PATH shim; one fixture run per gate on both interpreters (a few seconds); it would have turned red at c0316eb.
   - Alternative: the same as a new D27 rule ("mutate by deleting the error check of every external call"). This needs a reviewer per gate and is not mechanical.
2. **The D32 helper owns every git call (covers RC-C).** Land the B5-PROC-2 helper's git wrapper (rc≠0 or OSError -> `<pfx>.input`). Add a suite test that FAILs any `subprocess.run(["git"` in tools/gates outside lib.
   - Cost: 5 call sites migrated (gate_plan 4, gate_review_package 1), about 4-6 tests, the pinned test :496-502 rewritten. It needs Q-FH-16 answered (a: FAIL input for every failure, b: callers decide).
   - Option b keeps today's per-site semantics, at the cost of one D24 row per site.
3. **A register row for every answered code-changing DEC (covers RC-H).** gate_register FAILs an answered `spec-direction` DEC that no findings row cites, unless the DEC is marked text-only.
   - Cost: one check plus about 3 tests, and about 16 rows to register now (DEC-0068..0086 gaps), each needing an owner task.
   - Alternative: the controller audits this by hand at each checkpoint (no code; the same miss risk as today).
4. **Cross-dimension rows in the D31 skeleton (covers RC-F).** For every "git object state" row, add a paired row "the same state, git failing". For every subprocess call, add a "subprocess outcome" dimension (rc≠0, OSError, timeout).
   - Cost: spec D31 text (one sentence, owner) plus about 2× the git-state rows per gate (T6: about 4-6 rows), reviewer time to run them.
   - Alternative: rely on option 1, which observes the product mechanically, and add no rows.
5. Not recommended alone: fix only gate_plan.py:530 in the next fix round. Cost: 1 line plus 1 test. It leaves the class open at the other sites and repeats the D30 pattern.

## claims

| claim | direct command | contradiction command | output excerpt | result |
|---|---|---|---|---|
| gate_plan fails open in a non-git root (as reported) | `gate_plan --root $S/ng --in-flight` | the same on `$S/repo` | both rc=0 69 PASS: reported "fail-open" is no plan.input FAIL; the plan is still checked | PASS (refined) |
| gate_plan skips a git-only ledger when git fails | E2 / D / F | E1 / C (git working) | `PASS plan.in_flight` vs 69 slug lines / 5 FAIL | PASS |
| c0316eb and d3ed28d are not ancestors of fe3f146 | `git merge-base --is-ancestor c0316ebd fe3f146` | the same against e17dc90 | "NOT ancestor of fe3f146"; "ancestor of e17dc90" | PASS |
| :524/:530 from c0316eb, :557 from d3ed28d | `git blame -L 521,535 -L 556,557 e17dc90` | the :525 line | :524,:528-535 c0316ebd; :556-557 d3ed28de; **:525 4b8a9d62** | PASS (with :525 exception) |
| DEC-0068 answered after the code landed | decisions.yml:638-639 answered_at 740f6e9 | `git log -1 --date=iso 740f6e9` | 2026-10-27 10:23 vs c0316eb 10-23, d3ed28d 10-26 | PASS |
| T6 has no not-a-repository row | `sed -n 6978,7006p` plan; grep "not a repo\|no repo\|not in a repo" | T4.12 has one | only :6937 (T4.12) matches | PASS |
| findings.yml cites DEC-0068 0 times | `git grep -c DEC-0068 e17dc90 -- docs tools` | DEC-0066 count | 0 vs 3 | PASS |
| :582 failure not independently reachable | `git config core.fileMode maybe` then `git config --bool` and `git ls-files` | (the same commands: both must fail) | both `fatal: bad boolean config value` rc 128 | PASS |
| :193 fails closed under no repo | `test_modules($S/ng)` | `test_modules($S/repo)` | 0 vs 288 keys; test :496-502 asserts siblings FAIL | PASS |
| run_all masks it except with --only plan | `run_all --root $S/e` (E2) | `run_all --root $S/e --only plan` | rc=1 FAIL register.input vs rc=0 PASS plan.in_flight | PASS |

## findings raised (proposed register rows; the controller registers)
- **gate_plan git-failure fail-open**. tools/gates/gate_plan.py:528-535 (`_git_ledgers` ignores returncode). Repro: commit a ledger, rm it, make every git command fail (E2: move `.git`; F: unborn HEAD + unreadable index), then `gate_plan --in-flight` -> rc 0 `PASS plan.in_flight`. Class: git subprocess outcome (§5). Siblings: :524, :556, :582, gate_review_package.py:193, make_review_package.py:70. Owner class call: inside B5-FIX-5 (open, planned) or a new row.
- **DEC-0068 a not applied in code and not owned**: no gate FAILs `<gate>.input` for a non-repository root (A; gate_review_package pinned the opposite at test_gate_review_package.py:496-502). 0 findings rows cite it.
- Process: RC-F, RC-G, RC-H (§4) for the D30 record.

## Decision needed (owner; not blocking this report)
- Class: (a) widen B5-FIX-5 to "and every git command failing" (its fix covers :530, plus one re-review of T6.11-12 under failure; about 2 tests), or (b) a new row for the git-outcome class across all 5 sites (about 6 tests plus migration to the helper; settles Q-FH-16 first).
- Q-FH-16 (failure inside a repository): (a) FAIL `<pfx>.input` (4 gate_plan sites plus 1 in gate_review_package; about 4-6 tests), or (b) callers decide (0 helper code; one D24 row per site).

## Unrelated findings
none beyond the D33 hits above (all in this block's code).

## denials
none

## background jobs
none started

## budget used
about 130k of 250k tokens (estimate; the harness count governs); about 25 of 40 minutes (`date` at end 11:31 SAST).
