status: NEEDS_DECISION

Round 15 of block b5, opus sub-controller. Base 6a7b2f9, HEAD 0fec6c3. Started 10:25 SAST, report 11:35 (about 70 of 180 min; the 80 % line 12:49 was never passed). The tree is clean. Two state commits landed. B5-CAPS-1 is pinned. D36 is not committed and not installed: its review found two HIGH problems that need owner answers. All three tables had one fix round and one re-review; none is approvable. D30 fired (a new defect in b5 code), and the investigator report is in.

## commits
- e17dc90 chore(state): b5 round 14 item B + round 15 registrations -- 22 findings rows, DEC-0102..0122
- 0fec6c3 chore(state): B5-CAPS-1 pinned (b5 round 15, re-review CLEAN at 6a7b2f9)
- D34 before each one, in the checkout on the staged tree. Tree clean except the staged paths; `git ls-files -v | grep '^[a-zS]'` empty.
  - e17dc90: run_all rc 0, 208 PASS, 0 other. Suite 658 OK (skipped=1) on /usr/bin/python3 (137.8 s) and on tools/.venv/bin/python3 (174.4 s).
  - 0fec6c3: run_all rc 0, 208 PASS. Suite 658 OK (skipped=1), 145 s and 157 s.
  - The pre-commit hook printed register/claims PASS both times.
- First e17dc90 attempt was refused by the hook, verbatim: `FAIL pre-commit.unstaged: the gates read or run from these paths and they differ from the index; stage them (git add <path>) to commit them, or revert, delete or move them: !! .superpowers/sdd/2026-10-17-strict-delivery-S2-pipeline-gates-checkpoints/b5r14-itemB/ctl/repo/`.
  - That path was the round-14 controller's ignored guarded clone (HEAD f1057e5, clean).
  - I moved it to scratchpad/r14-ctl-moved/, as the hook text offers. Then I re-ran the full D34 set and committed. Nothing was done another way.

## item 1 (state rows; e17dc90)
- Dry-run in a guarded clone at 6a7b2f9 with the review packages copied in: run_all rc 0, 208 PASS. Without the packages the count is 100; the packages are git-ignored.
- Contradiction: B5-UR-1 moved to S2-T6 gives `FAIL review_package.strict-delivery.S2-T6.mutation.B5-UR-1: mutation-B5-UR-1.txt missing`.
- Totals: 73 findings, 122 decisions, 20 unanswered (DEC-0102..0121). DEC-0122 is the answered budget rule.
- Placements and severities are all controller proposals for the owner (DEC-0090 session-fit; owner tasks have no package).

| row | severity | owner task |
|---|---|---|
| B5-RB-2, -3, -4 | MEDIUM | S2-b5-fix-make-package |
| B5-RA-6 | MEDIUM | S2-b5-fix-review-package |
| B5-RC-2 | MEDIUM | S2-b5-fix-plan |
| B5-U9 | MEDIUM | S2-b5-process |
| B5-U10 | LOW | S2-b5-budget |
| B5-UF-2 | LOW | S2-b5-process |
| B5-UF-3 | LOW | S2-b5-caps-d3 |
| B5-UF-4 | LOW | S2-b5-fix-plan |
| B5-UD-2 | LOW | S2-b5-budget |
| B5-ID-1, -2 | LOW | S2-b5-fix-plan |
| B5-UR-1 load_yaml hangs on a FIFO register (lib/__init__.py:100-106) | MEDIUM | S2-b5-process-code |
| B5-UR-2 absolute path in an input reason (gate_plan.py:142) | LOW | S2-b5-process-code |
| B5-UR-3 gate_claims writes through a symlinked claims.yml outside the root (:95) | MEDIUM | S2-b5-process-code |
| B5-UR-4 make_review_package hangs on a FIFO write target (:82) | MEDIUM | S2-b5-fix-make-package |
| B5-UR-5 splitlines VT/FF, 12 hits | LOW | S2-b5-process-code |
| B5-UR-6 truncated package after a failed write (:160-163) | MEDIUM | S2-b5-fix-make-package |
| B5-UR-7 check ids carry paths (DEC-0082 a) | LOW | S2-b5-process-code |
| B5-UR-8 a sourceless yaml.pyc in tools/gates crashes run_all | MEDIUM | S2-b5-process-code |
| B5-UR-9 CI suite runs without -W error (gates.yml:30-32) | MEDIUM | S2-b5-process-code |

- I reproduced every B5-UR row in the guarded clone at 6a7b2f9, direct and contradiction (script scratchpad/r15-item1/probe.sh; rows script b5r15-rows2.py):
  - FIFO findings.yml: TIMEOUT 10 s; regular file: rc 0.
  - Unreadable u.md: the reason names `/private/tmp/.../u.md`.
  - Outside claims.yml now reads `state: stale`.
  - FIFO diff.patch: TIMEOUT; regular: rc 1 with the package written.
  - VT and FF each split into 2 lines; split on LF gives 1.
  - With `ulimit -f 4`: diff.patch 4096 bytes of 200086.
  - `FAIL decisions.scan.sub/f.md`.
  - yaml.pyc: ImportError traceback; without it: `PASS claims.stale`.
  - grep gates.yml: no `-W error`.
- Not registered. All three are process notes or owner items, and all are owner proposals:
  - R-B-13 "NAME accepts t\n". Reproduced: a directory 't\n' is written. DEC-0089 folds it into pinned B5-FIX-9, which is held by DEC-0102.
  - The `-I` note. Reproduced on .venv 3.12: `-I` ignores PYTHONDONTWRITEBYTECODE. On 3.9 nothing is written in the tree. No tracked file uses -I.
  - The round-14 drafter's ~/Library/Caches write.

## item 2 (D36: not committed, not installed)
- Review (b5r15-d36rev-report.md) of sha256 887a4036...85a1 (5411 bytes):
  - Verdict: spec ❌ quality ❌ claims ✅.
  - R-d36-1 HIGH: the session can never dispatch a sub-controller under D36, because 180 > 144.
  - R-d36-2 HIGH: a like-work floor above 144 never fits.
  - R-d36-3 MEDIUM: no rule when the ledger has no harness duration.
  - R-d36-4 MEDIUM: "like work" is undefined and not marked judgment.
  - R-d36-5 MEDIUM: §3.1 names no minutes clock.
  - R-d36-6 to R-d36-8 LOW.
- Fix round (fresh opus implementer): R-d36-5, -6, -7 fixed. R-d36-1..4 and -8 left OPEN, because each needs an owner choice. A plan.patch now stages the S2 :7185 sha256 list.
- Re-review (b5r15-d36rr-report.md) of sha256 efe8056f54b749e0e6728d470729f0fa8042627ae913cb32da2299804f7c8db8 (5500 bytes, 0 bytes margin to the DEC-0095 cap; check-skill `skill skeleton OK`):
  - The three fixes hold.
  - Patches apply with -F0 at e17dc90. With them applied: run_all 208 PASS, suite 658 OK on both interpreters.
  - New LOW findings: R2-d36-1 (`fit before` vs `<=`), R2-d36-2 (the session has no ## Budget line to refer to), R2-d36-3 (Q-d36-1 (b) conflicts with "since its start").
  - New questions: Q-d36-6 (block at the cap), Q-d36-7 (is :7185 the B5-PROC-1 install record, or the live per-round check?).
- Commit: none. The spec patch is not clean, so D34's review-before-commit rule blocks it.
- Install: none. The staged sha256 for an install after the owner answers is efe8056f54b749e0e6728d470729f0fa8042627ae913cb32da2299804f7c8db8. It will change with any further text round.
- Pre-fix copies are in b5r15-budget/r0/.
- Not staged (the review marks them must-change): a ledger.md hash line and the SKILL.md "Judgment, not gated" bullet.
- Unrelated: U-1 (ledger.md has no b0fdc5ed hash line) and U-2 (the 5500 cap is enforced only by the ignored check-skill.sh). Both are proposed rows, not registered.

## item 3 (B5-CAPS-1: pinned, 0fec6c3)
- Re-review (b5r15-rr-caps.md): DONE, no R-rc15 finding.
  - B5-CAPS-1 CLEAN at 6a7b2f9. The D3 blocks are equal; the only config diff is fable_fallback (B5-UF-3). The only live old cap is spec D22 :36 (B5-U9).
  - Package regenerated over 72b7018..6a7b2f9, so R-rc13-2 is closed. `--dir` gives only PASS, and all 8 claims.md rows re-run PASS.
  - Both transcripts replay OK / FAILED / OK.
  - Kill check 12/12. This is the M1-M12 re-run; it is not a claims.md finding because D27 starts at b6 and the package no longer claims it.
  - B5-UD-1 and B5-RD-1 fixes CLEAN. Their pin form stays DEC-0103 (owner).
- Pin:
  - state pinned; pin test_gate_dispatch::test_budget_caps_follow_dec_0060 with mutations/B5-CAPS-1.txt;
  - evidence docs/superpowers/state/strict-delivery/reviews/S2-b5-caps.md, which holds the report verbatim;
  - history in-progress 41aec62, pinned 6a7b2f9; a ledger.md line.
- Contradiction (clone): transcript renamed to B5-CAPS-9 gives `FAIL register.evidence.B5-CAPS-1: evidence: pin transcript docs/superpowers/state/strict-delivery/mutations/B5-CAPS-9.txt does not exist`.

## item 4 (B5-PROC-2 tables; one fix round each, serial PC -> FH -> SG; one re-review each)
- Shared D32 reading for write targets: ruling R-r15-1 in b5r15-fix-common.md; all three tables now cite one owner question, Q-D32-W.
  - All three re-reviewers found a defect in that ruling: it quotes "no change for /tmp paths" as DEC-0093's choice.
  - Direct check: decisions.yml:865 puts those words in option below-root, which the owner did not pick. Contradiction: the chosen option (:867) has no "/tmp".
  - Also: an "unreadable" write target is D32's own word, and the ruling left it out (R2-PC-7).
  - R-r15-1 is withdrawn as written. Correcting the shared rule is a round-16 text item.
- PC:
  - Fixed: R-PC-1..15. Draft 144 lines, 77 rows.
  - Re-review DONE_WITH_FINDINGS (spec ❌ quality ❌ claims ✅; 20 of 25 changed rows run). R2-PC-1 and R2-PC-3 are MEDIUM: Q-PC-23 is settled by S1 plan :5800-5807 and :6617, and "Q-PC-19 = Q-D32-W" drops the 0700 mode and placement options. R2-PC-2, -4..-7 are LOW.
  - No round-16 re-review slot needed this round: it was re-reviewed. Its R2 fixes need a round 16.
- FH:
  - Fixed: R-FH-1..18; some only in part, per the re-review. Draft 226 lines, 78 rows.
  - Re-review DONE_WITH_FINDINGS (spec ❌ quality ❌ claims ✅; 23 of 23 changed rows run). R2-FH-1 and R2-FH-2 are MEDIUM: DEC-0068 a settles FH.53, and the D32 quote drops "For every gate:". R2-FH-3..10 are LOW. New question Q-FH-24.
- SG:
  - Fixed: R-SG-1..12. R-SG-1 HIGH was the round-14 drafter's own grep error, now withdrawn. Draft 146 lines, 81 rows.
  - Re-review DONE_WITH_FINDINGS (spec ❌ quality ❌ claims ❌; 61 of 81 rows run). R2-SG-1, -2 and -11 are MEDIUM. R2-SG-3..10 are LOW. New question Q-SG-21.
- Every Q-* question stays open (no re-reviewer found one answered or dropped). R2-PC-5 and R2-SG-11 each say a cell or an equality narrows one.
- The open questions follow, verbatim from each draft's Open questions table (id | question), truncated at 230 characters. Full option texts are in the drafts. Added by the re-reviews: Q-FH-24 (does D32 "For every gate:" reach make_review_package.py and the hooks?) and Q-SG-21 (does DEC-0093 settle that a write target with a symlink component outside the root, not itself the path argument, is accepted?).
```
== skeleton
 Q-SG-1 | Is the generator a §7 gate (in `tools/gates/`, `PASS`/`FAIL` lines, `--json`, `<pfx>.input`; in `run_all` or not) or a helper like make_review_package (skeleton on success, refusal message and rc 1)? 
 Q-SG-2 | How are calls found? 
 Q-SG-3 | What does it take, and what is "a gate"? 
 Q-SG-4 (= Q-FH-19 = Q-SG-18) | Does it follow imports into `tools/gates/lib` (findings.yml, config.yml, `--root`, gitinfo's git runs), and are a lib module's rows per importing gate or once? 
 Q-SG-5 | For a command run through a wrapper (`_git(...)`, `gitinfo._run(cmd)`), does the row name the wrapper's call site with its literal args, or the `subprocess` call? 
 Q-SG-6 | What does an item name for a computed path (join of a constant, loop variable, value read from a file, module constant)? 
 Q-SG-7 | Do inputs outside D31's families get rows: env vars (`CLAUDE_PROJECT_DIR`), `sys.stdin`, `sys.argv`, `tempfile`, `importlib`, args passed on by `parse_known_args`? 
 Q-SG-8 | Are shell sources (`git-pre-commit.sh`, `git-pre-push.sh`, `install-hooks.sh`; the PC hook if it is shell) in scope? 
 Q-SG-9 | Where does the dimension list come from, and at what granularity (D24's first clause one dimension or two; "file type, permissions, symlinks" one or three)? 
 Q-SG-10 | Output: stdout or a file; format (the plan table's 7 columns, a SG-style id); sort key; dedup key; escaping; encoding 
 Q-SG-11 | Is the source read from the working tree, the index or HEAD? 
 Q-SG-12 | A source path outside the root: FAIL or accepted? 
 Q-SG-13 | A non-UTF-8, BOM or CRLF source: FAIL, or decode? (DEC-0067 a settles it if it reaches the generator: Q-SG-17) 
 Q-SG-14 | An empty source (or one with no calls): an empty skeleton or a FAIL? (DEC-0067 a "non-empty" settles it if it reaches the generator: Q-SG-17) 
 Q-SG-15 | Does anything check a filled table against a fresh skeleton (every row filled or argued N/A)? 
 Q-SG-16 | Path form of the argument and of items: canonical spelling (case, `..`, NFC/NFD), and which root (worktree toplevel vs main checkout, `CLAUDE_PROJECT_DIR`)? (the root half: DEC-0068 a, if it reaches the generator, Q-SG-
 Q-SG-17 | Do DEC-0067 a, DEC-0068 a and DEC-0085 a reach the generator's source reads and root (one answer with Q-FH-8)? 
 Q-SG-18 (= Q-FH-19 = Q-SG-4) | Once gates call the D32 helper, how does the generator find their reads, writes and git runs (= Q-FH-19)? 
 Q-SG-19 | Under `--all` (Q-SG-3 c), is the source set taken from the filesystem or from git? 
 Q-SG-20 | Does the generator's own run carry the P-1 row (its imports of tools/gates/lib)? 
 Q-D32-W | (shared, as the controller writes it) Do D32's input-check FAILs cover a write target that is unwritable, has a missing parent, hits disk full or a partial write, pre-exists outside the root, or passes a symlink compone
== precommit
 Q-PC-1 | How is the export made so the gates and the git-reading tests see the committed tree as a repository? 
 Q-PC-2 | Assume-unchanged and skip-worktree entries in the index at commit time: refuse or export? 
 Q-PC-3 | Intent-to-add entries? 
 Q-PC-4 | Do clean/smudge filters, eol conversion and the config keys of PC.26 apply to the export? 
 Q-PC-5 | How does the export run read the checkout's review packages? 
 Q-PC-6 | Unborn HEAD and changed gitlinks: accepted? 
 Q-PC-7 | Where are the export and prefix dirs, and how are they cleaned? 
 Q-PC-8 | Must each interpreter be a given version? 
 Q-PC-9 | Check ids for the hook's own lines? 
 Q-PC-10 | The venv interpreter is a symlink below the root (`python3 -> python3.12`); D32 FAILs that, D34 requires it. Which governs? 
 Q-PC-11 | Does the hook keep today's work-tree guard (`pre-commit.unstaged`) once it tests the export? 
 Q-PC-12 | Does pre-push run the D34 set on HEAD, and is CI's suite step given `-W error` for commits that skipped the hook? 
 Q-PC-13 | Does the Bash-hook commit branch (B5-U3) run the D34 set too? 
 Q-PC-14 | Verbs that run no pre-commit hook (merge, cherry-pick, rebase, revert): covered how? 
 Q-PC-15 | Which inherited Python variables are cleared, and does the hook run `-I`? 
 Q-PC-16 | Is about 300 s per commit (measured) acceptable? 
 Q-PC-17 | Does the hook stay POSIX shell applying D32 in place, or become Python using the D32 helper? 
 Q-PC-18 | Does the D31 generator scan this hook (shell, or Python per Q-PC-17)? 
 Q-PC-4 note | R-PC-3: D34 :48 and DEC-0098 settle that the export holds the index blobs byte for byte (PC.19), which option b contradicts where `.gitattributes` converts content; Q-PC-4 stays open as asked (fix rules: never narro
 Q-PC-19 | = Q-D32-W. Write targets outside the repository root (export and prefix directories under TMPDIR): which kind, symlink, pre-existing and unwritable rule applies, given that D32/DEC-0093 reach only below the root? 
 Q-PC-20 | Isolation of the four D34 commands: run all four after a red one, or stop at the first? 
 Q-PC-21 | §8 :230's "register/claims/comments/p10 on staged files": once comments and p10 land (S3/S4), do they run inside run_all on the export, or separately on the staged paths? 
 Q-PC-22 | Which copy of the hook script and of run_all runs: the work tree's (as the shim does today) or the export's? 
 Q-PC-23 | The installer refuses a hooks directory outside the common dir (install-hooks.sh:16-19), a rule no spec line, plan line or DEC states: keep it? 
 Q-D32-W | Do D32's input-check FAILs cover a write target that is unwritable, has a missing parent, hits disk full or a partial write, pre-exists outside the root, or passes a symlink component outside the root? 
== helper
 Q-FH-1 | Relative path arguments resolve against? 
 Q-FH-2 | Path arguments outside the root or with `..`? DEC-0093's chosen text says "covers --plan outside the root". 
 Q-FH-3 | Case and Unicode forms of path arguments? 
 Q-FH-4 | Missing-input id? 
 Q-FH-5 | A listable, non-searchable directory? 
 Q-FH-6 | `<pfx>` and reason form (deferred Q36): gate or item prefix; `.encoding` vs `.input`; DEC-0081 ledger id; `decisions.scan.<rel>`; relative path in the reason? 
 Q-FH-7 | A text file with no final LF? 
 Q-FH-8 | Does DEC-0067 reach every E1 text read? 
 Q-FH-9 | `--root` given through a link? R-FH-3 shows that D32-a settles FH.33 (FAIL); kept open for the owner. 
 Q-FH-10 | E3 parent missing? 
 Q-FH-11 | Unwritable target: which id? 
 Q-FH-12 | Partial writes? 
 Q-FH-13 | Are ignore files "pinned config"? `core.excludesFile` is a config key (D32-d "pinned config"); DEC-0098 a and DEC-0101 a put "the ignore and filter rows" in the hook's table. 
 Q-FH-14 | The pinned config set and env? 
 Q-FH-15 | Config a gate reads on purpose? 
 Q-FH-16 | A git failure in E4? 
 Q-FH-17 | Which matches are "names and values"? 
 Q-FH-18 | (pre-commit drafter) the export root vs DEC-0068 
 Q-FH-19 | (generator drafter) helper calls in the skeleton; = Q-SG-4, Q-SG-18 
 Q-FH-20 | No-fit call sites (gh, bash, stdin, env, temp file, `~/.claude`) 
 Q-FH-21 | E4 reads of git objects that are not regular blobs (HEAD path mode 120000 or 160000, absent at HEAD, unborn or detached HEAD) 
 Q-FH-22 | Case and Unicode forms of names that compose an E3 write path (plan, task) on APFS 
 Q-FH-23 | One reading of D32 for write targets across the three tables: do "unwritable" and "missing parent" fall under D32's "unreadable, of the wrong kind"? 
 Q-D32-W | (shared, as the controller writes it) Do D32's input-check FAILs cover a write target that is unwritable, has a missing parent, hits disk full or a partial write, pre-exists outside the root, or passes a symlink compone
```

## D36 compliance (each dispatch: clock at dispatch, minutes budget, basis, harness duration)
| child | clock | budget | basis | harness |
|---|---|---|---|---|
| PC fix (resumed a604bc357c453ac02) | 3 | 30 | drafter PC 22 min (r14 harness) | 5 min |
| D36 review af7be95326e62d3be | 14 | 45 | r13 text re-reviews 22 min, caps 17 min (ledger timestamps: no harness basis existed; this is R-d36-3's case) | 13 min |
| PC re-review a40dce31f2d036357 | 15 | 100 | r14 PC table reviewer 96 min | 16 min |
| FH fix (resumed a8ece3a01fde6f525) | 15 | 30 | 22 min | 6 min |
| caps re-review a1c3ea330fd3f4aed | 18 | 45 | r13 caps re-review 17 min (ledger) + suites | 35 min |
| FH re-review a92832074691ab3a8 | 21 | 100 | 96 min | 12 min |
| SG fix (resumed a36fbb99c1b00f084) | 21 | 30 | 22 min | 5 min |
| SG re-review ac25ff2c9e36988a9 | 26 | 100 | 96 min | 9 min |
| D36 fix af2155bde7d55e4a1 | 29 | 30 | 22 min | 26 min |
| D36 re-review ae0d73e7819749f09 | 57 | 45 | D36 review 13 min | 11 min |
| D30 investigator a1f36464b9dfa221c | 57 | 40 | r13 investigator 15 min (ledger) | 11 min |

- Every dispatch met clock + budget <= 144, and every child ended inside its budget.
- Tokens (harness), fix rounds: 195945, 233688 (93 % of 250000) and 208081 (83 %). The resumed drafters carry their round-14 context.
- Tokens (harness), the rest: reviewers 96122-163549; D36 fix 71736; investigator 134167.
- Controller defect: the SG resume and the FH re-review went out after FH's report file and first message, but before FH's hand-back notification. Effect: none. The FH draft was last written at 10:44:08 (stat), before the dispatch.

## D30 trigger
- The FH re-review found a new defect in this block's code outside every recorded class: gate_plan reads a failing git as "in flight" or "no value".
  - Blame: gate_plan.py:524/:530 come from c0316ebd and :557 from d3ed28de. Neither is an ancestor of fe3f146.
  - No fix line was active (DEC-0092), so nothing had to be stopped.
- I dispatched a read-only investigator (b5r15-d30-investigation.md, DONE_WITH_FINDINGS):
  - The real fail-open is :530 `_git_ledgers` when every git command fails. Repro: B5-FIX-5 repro (a) with .git moved away -> `PASS plan.in_flight`.
  - DEC-0068 a (non-repository root -> `<gate>.input`) is applied nowhere, and no row cites it.
  - New root causes beyond RC-A..E: RC-F (dimensions tabled one at a time), RC-G (mutation review cannot see an omitted rc check), RC-H (an answered code-changing DEC gets no register row; 16 of DEC-0066..0086 have none).
  - Ranked process changes: (1) a suite test with a failing git stub; (2) the D32 helper owns every git call, with other calls banned; (3) gate_register FAILs an answered spec-direction DEC that no finding cites; (4) paired git-failure rows in the D31 skeleton.
- Owner class call: widen B5-FIX-5, which is open and unpinned, or add a new git-outcome row. Q-FH-16 also needs an answer (a or b).

## rulings
- R-r15-1 (5 of 5): `Ruling: one shared D32 reading for write targets -- wrong kind, symlink below the root, a symlinked path argument, multi-item are D32's words; the rest is owner question Q-D32-W -- cost if wrong: one text round -- spec D32 :46, D25, DEC-0093`.
- Withdrawn as written: its DEC-0093 quote is false, and it leaves out "unreadable". No ruling is left for this block.

## run_all
At 0fec6c3 in the checkout: rc 0, 208 PASS, 0 other.

## git status --short
empty. HEAD 0fec6c3; the branch is not pushed.

## agents and jobs
- All exited:
  - drafters (resumed) a604bc357c453ac02, a8ece3a01fde6f525, a36fbb99c1b00f084
  - reviewers af7be95326e62d3be, a40dce31f2d036357, a1c3ea330fd3f4aed, a92832074691ab3a8, ac25ff2c9e36988a9, ae0d73e7819749f09
  - implementer af2155bde7d55e4a1
  - investigator a1f36464b9dfa221c
- Shell jobs, all finished:
  - D34 runs bsx98i1zn and bt2ic8lrj: exit 0.
  - Waiters b0o5bc6eu, bl7lwzx8z: stopped by me as duplicates.
  - bc49mni5k, bn5tmnb2m: ended.
  - Monitor bpklpupv9: expired.

## denials
- Pre-commit hook refusal, verbatim, under commits above. Handled by moving the stale clone, as the hook message offers.
- The Agent hook refused my first D36 dispatch, which carried a placeholder strict block: `FAIL dispatch.strict: strict block differs from ~/.claude/skills/strict-delivery/prompts/reviewer.md at character 0`. Re-sent with the verbatim block.
- Children: none.

## unrelated findings (proposed rows, not registered: reproduction by the controller is still due)
- run_all.py:80 strips `--only` names (`' register '` -> rc 0); pre-b5 151f2bba.
- lib/__init__.py:61 returns CLAUDE_PROJECT_DIR unchecked against DEC-0068 a; pre-b5 e2a20af0.
- The gate_plan git-failure fail-open and DEC-0068 a unowned (D30 above).
- U-1: ledger.md has no hash line for the installed controller.md.
- U-2: the 5500 cap is ungated.
- SG reviewer disclosure: two stray /tmp writes, removed at once.

## Decision needed (owner)
1. D36: Q-d36-1..7 (R-d36-1 and -2 are HIGH). Commit or install only after these, plus one text round and one review.
2. Q-D32-W, plus Q-SG-21, Q-FH-24, and the R-r15-1 correction: the shared write-target rule for the three tables.
3. Table questions: Q-PC-1..23, Q-FH-1..24, Q-SG-1..21.
4. D30: the git-outcome class (widen B5-FIX-5 or a new row), Q-FH-16, and the four ranked process changes (b5r15-d30-investigation.md §6).
5. Placements and severities of the 22 new rows (item 1), and the three unregistered process notes.
6. Carried, unanswered: DEC-0102 (reopen a widened pinned row), DEC-0103 (pin form of B5-UD-1 and B5-RD-1), DEC-0104 (owner task ids), DEC-0105..0121 (D33 sweep).

## open items for round 16
- Table text round 2 for PC, FH and SG (the R2-* findings plus the R-r15-1 correction), then one re-review each. Measured basis: 9-16 min harness.
- Register the unrelated findings above, after reproducing them.
- D36 text round 2 after the owner answers.

## budget used
- Controller: about 70 of 180 minutes.
- Tokens: about 260k by context use; the harness count governs.
