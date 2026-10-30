status: DONE_WITH_FINDINGS

# b5 process root-cause investigation (investigator, opus, read-only)

Evidence base: register rows B5-FIX-2..26, B5-U1..U5, SD-S2-T4..T6 (docs/superpowers/state/findings.yml:129-1567); DEC-0014..0057 (decisions.yml); 67 commits fe3f146..6f6355a; the S2 plan as approved at fe3f146 (`git show fe3f146:<plan>`, cited below as plan0:<line>); task/re-review/final reports in this SDD dir; strict prompts in ~/.claude/skills/strict-delivery/prompts/. No code fix is proposed.

## commits
none (read-only).

## gates run
None of the gate suites were run: the question is about history, not the tip. Scratch reproducer only (see claims C9).

## 1. Origin table

Origin codes: **P** = code or contract text in the approved plan at fe3f146, copied in; **R** = semantics written by the controller/session into a fix-round or round brief as "binding controller resolutions" (t4-review2-body.md:3, t5-review1-body.md:3, t6-review1-body.md:3, b5r4-controller-brief.md:22), never put to the owner; **T** = implementer's own choice in the original task or a task fix round; **W** = created by a B5-FIX fix wave; **C** = the class or sibling list left incomplete after a fix (a new sub-input found later); **S** = session state/plan text; **N** = new owner requirement.
Miss codes: **scope** = not in its scope; **diff** = review only of the fix diff; **budget** = budget stop; **looked** = looked, no test of that input; **never** = input class never named anywhere; **bound** = the brief told the reviewer the semantics "bind", so the review checked conformance to the brief, not correctness.

| id | file:line (at cite sha) | introduced (sha) | by | first review with it in scope | miss | found by |
|---|---|---|---|---|---|---|
| B5-FIX-2 | gate_dispatch.py:97-109 | spec D18 (no fallback) | N (DEC-0018, 2026-10-19) | n/a | n/a (new requirement) | session 4bf208e |
| B5-FIX-3a pin title substring | gate_plan.py:62 | 605b768 | R: b5r4-controller-brief.md:22 "title contains `pin` (case-insensitive)"; t6-fix5-prompt.md:35 "apply it as written" | task-6-review5 (raised R5-4) | bound | session 7569366 |
| B5-FIX-3b two id forms | gate_plan.py:62-63 | 605b768/02398e3 | T | task-6-review6 | looked | session 7569366 |
| B5-FIX-4 V-1 color.diff/log | make_review_package.py:35 | 41940dc (pins only color.ui) on P plan0:2548-2549 (no pins) | P+T | task-5-review1:49 (tested color.ui only) | looked | b5-final-verify (pass 1) |
| B5-FIX-4 V-2 core.abbrev=4, UTF-16 log | gate_review_package.py:28 | 119e0e8 | R: F6 `^[0-9a-f]{7,40} \S` (t4-review2-body.md:3) | task-4-review2 | bound + never | final pass 1 |
| B5-FIX-4 V-3 different CHECKPOINT at HEAD vs work tree | gate_plan.py:487-503 | 6b15653 | P plan0:2667 contract + plan0:3170-3197 `_closed(text) and _closed(committed)` | task-6-review1 | looked (Review Focus 5 tested only the uncommitted-line case) | final pass 1 |
| B5-FIX-4 V-4 hyphen pin titles | gate_plan.py pin regex | 1a6ff7c | W (B5-FIX-3 fix) | b5r5-review1..4 | diff/looked | final pass 1 |
| B5-FIX-4 V-5 T4 transcript step 2 | mutations/SD-S2-T4.txt | 9c94595 | S (controller pin) | none (pins not reviewed) | scope | final pass 1 |
| B5-FIX-4 F1 empty review dir | gate_review_package.py:262-277 | 6dc2657 | T | task-4-review1 | looked | final pass 1 |
| B5-FIX-5 (a) deleted ledger, (b) staged-only | gate_plan.py:493-511 | 4b8a9d6 (B5-FIX-4 V-3 fix, byte compare) on P plan0:2667 | W | b5-final-review2/verify2 (scoped to the B5-FIX-4 wave, verify2 "Focus:") | caught by own review | b5-final-verify2 |
| DEC-0034 mode/type/symlink ledger | gate_plan.py:493-511 | c0316eb | W (B5-FIX-5 fix) | b5r8-review1 | caught by next review | b5r8-review1 R1-1 |
| DEC-0041 core.fileMode=false fails closed | gate_plan.py | ec1ff2f/f13930c | W (DEC-0034 fix) | b5r9-rr-A3 | caught | b5r9-rr-A3 D-A3-1 |
| DEC-0049 core.symlinks=false | gate_plan.py | c2badae | W (DEC-0041 fix) | b5r10-rr | caught | b5r10 Q7 |
| B5-FIX-6 .superpowers/sdd a file | gate_review_package.py:262-280 | 6dc2657 | P plan0:2259-2261 (`if os.path.isdir(d)` filter) | task-4-review1 | never (file type) | final pass 2 (DEC-0022 D-1) |
| B5-FIX-7 quoted first path / empty subject | gate_review_package.py:34, :123 | 119e0e8 | R: F6 "diff.patch starts `diff --git a/`" replaced plan0:2225 `startswith("diff --git")`, which accepted the quoted form | task-4-review2 | bound + never | final A2-R1/A2-V1 (round 7) |
| B5-FIX-8 str/bool rank | gate_dispatch.py:102-103 | 1a6ff7c | W (B5-FIX-2 fix) | b5r5-review1..4 | diff/looked | final A4-V1 |
| DEC-0028 fallback any rank in [opus,fable) | gate_dispatch.py:97-105 | 1a6ff7c | W (widened DEC-0019's "opus = on") | b5r5-review1..4 | looked | final A4-R1 |
| B5-FIX-9 untested name/hint branches | make_review_package.py:132-134 | 41940dc | T | task-5-review1..3 (ran a mutant set without M22/M29) | looked | final A2-R2/V3 |
| B5-FIX-10 passed(reason) | lib/__init__.py:41-50 | 1a6ff7c (`def passed(self, id: str, reason: str = "")`; parent :41 had no reason) | W (B5-FIX-2 fix, shared lib) | b5r5-review1..4 | diff (spec :191 not checked) | final A5-R1/V4 |
| DEC-0040 GATE_LINE accepts reason | gate_review_package.py:36 | 119e0e8 | R: F1 "every non-blank line `^(PASS\|FAIL) \S+`" | task-4-review2 | bound | b5r9 U-A4-2 |
| DEC-0043 legacy packages fail strict line | packages S2-b5-* | c2badae/16766df | W (DEC-0040 fix) | b5r10 | caught | b5r10 Q1 |
| DEC-0047 render still prints reason | lib render | W scope gap of B5-FIX-10 fix | W | b5r9 | looked | b5r10 D-A5-1 |
| B5-FIX-11 plan cites "ruling 2b" | plan :5495 | session re-sync | S | none before final | scope | final A5-R2 |
| B5-FIX-12 stray Files item | gate_plan.py:250-306 | 6b15653..605b768 | R ("Fix-1 design pick never asked", DEC-0026 text) | task-6-review2..6 | looked | final A3-R1 |
| B5-FIX-13 absolute/.. paths | gate_plan.py:196-204 | aa22d0c | T | task-6-review2 | looked | final A3-V3 |
| B5-FIX-14 case/dir/backslash | gate_plan.py:468-474 | 6b15653 | P plan0:3147-3153 raw-string set overlap | task-6-review1 | never (filesystem identity) | final A3-V1 |
| DEC-0045 NFC/NFD; DEC-0057 caseless | gate_plan.py overlap | residual | C | b5r9/b5r10 | looked | b5r10 R-A3-1; b5r11 R-P2A3-1 |
| B5-FIX-15 U+0131 folds, Cf joins | gate_plan.py:60-78 | 173ae2d | W (B5-FIX-4 V-4 fix) | b5-final-review2/verify2 (wave-scoped) | diff/looked | final A3-R2/V2 |
| B5-FIX-16 ignored files pass clean check | make_review_package.py:88-95 | b66deaf | T | task-5-review2:43,54 (probed ignored files, judged rc 0 correct) | looked (no stated rule) | final A2-V2 |
| DEC-0033 pycache refuses every package | make_review_package.py | B5-FIX-16 fix (round 8) | W | own implementer | caught | b5r8 impl |
| DEC-0039, -0048, -0053, -0055 (other plans, spec file, tracked symlink, all specs) | make_review_package.py | residual | C | b5r9..r11 | looked | b5r9 R-A2-1; b5r10 Q6; b5r11 R-A2r11-Q1, D2 |
| DEC-0044 / DEC-0054 pycache scope; code contradicts DEC-0044 | make_review_package.py:39, :113-115 | 16766df | C then W | b5r10/r11 | looked | b5r10 Q2; b5r11 D1 |
| B5-FIX-17 module half ignored | gate_review_package.py:73-101 | 119e0e8 | P plan0:2154 `test not in steps[0].group("cmd")` (substring), narrowed by R F3 "last dotted or after `::`" (written from task-4-review1 F3) | task-4-review1 | bound (reviewer's own resolution) | final A1-R1 |
| B5-FIX-18 quoted test id | gate_review_package.py:73-75 | 119e0e8 | R F3 (whitespace split) | task-4-review2 | never (shell quoting) | final A1-R3 |
| B5-FIX-19 untested fence-indent / cap / ckpt branches | gate_plan.py:179, :290, :465 | aa22d0c, 6b15653, 4b8a9d6 | T (aa22d0c subject claims "a test per branch") | task-6-review2 | looked | final A3-R3 |
| B5-FIX-20 root a subdirectory | gate_plan.py:500-545 | 6b15653 (`HEAD:{rel}` at :260 = plan0:3179) + c0316eb (index read) | P + W | task-6-review1 | never (path form) | final A3-R4 |
| B5-FIX-21 unreadable plan dir | gate_review_package.py:258-268 | 2ce84d2 | T (T4 fix round 2) | task-4-review3 (scoped "round 119e0e8..2ce84d2") | diff + never (permissions) | final A1-V1 |
| B5-FIX-22 first table only | gate_review_package.py:50-63 | 6dc2657 docstring = plan0:2129; code collected every `\|` line until R F5 "first table only" (task-4-review1.md:58 → t4-review2-body.md:3) | P + R | task-4-review1 (raised F5 the wrong way round) | bound | final A1-V2 |
| DEC-0038 FAIL after separator / no leading pipe | same | residual | C | b5r8 | looked | b5r9 DQ-2 |
| B5-FIX-23 dangling symlink untested | gate_review_package.py:264 | 2ce84d2 | T | task-4-review3 | diff/looked | final A1-V3 |
| B5-FIX-24 untested plans_in_flight branches | gate_plan.py:515-538 | c0316eb | W (B5-FIX-5 fix) | round 7 A3 | caught | final A3-V6/V7 |
| B5-FIX-25 bare decode | gate_plan.py:541-545 | c0316eb (parent count 0, tip count 1) | W (B5-FIX-5 fix) | round 7 A3 | caught | final A3-V8 |
| B5-FIX-26 sibling name syntax only | gate_review_package.py:174-175 | 119e0e8 | R F4 "second cell a test name or registered id" | task-4-review2 | bound | final A1-R2 (round 8) |
| DEC-0037 "test trees" undefined | B5-FIX-26 fix | DEC-0035 text | W/underspec | b5r9 | caught | b5r9 DQ-1 |
| DEC-0046, -0051, -0052 static / nested def / Unity in #if 0 | B5-FIX-26 fix | 79f0d16, 16766df, 163aee0 | C/W | b5r10, b5r11 | looked | b5r10 Q4; b5r11 Q-A1-1, Q-P2A1 |

## 2. Counts (52 defect units; widenings counted one each)

By origin: W fix-introduced 16 (V-4, FIX-5, DEC-0034, -0041, -0049, FIX-8, DEC-0028, FIX-10, DEC-0043, -0047, FIX-15, FIX-24, FIX-25, DEC-0033, -0054, -0037); C sibling/class left incomplete 11 (DEC-0038, -0039, -0044, -0045, -0046, -0048, -0051, -0052, -0053, -0055, -0057); T 8 (FIX-3b, F1, FIX-9, -13, -16, -19, -21, -23); P 7 (V-1, V-3, FIX-6, -14, -17, -20, -22); R 7 (FIX-3a, V-2, FIX-7, -12, -18, -26, DEC-0040); S 2 (V-5, FIX-11); N 1 (FIX-2). P and R together: 14; with W and C: 41 of 51 non-N.

Miss reason at the first review that had the code (51 non-N): looked-no-test 20; bound 8; never named 6 (V-2, FIX-6, -7, -14, -18, -20, -21 counted where never is primary: FIX-6, -14, -18, -20, -21, and V-2/FIX-7 jointly with bound); diff-only 6 (V-4, FIX-8, -10, -15, -21, -23); scope 2 (V-5, FIX-11); budget 0 as primary at the first review, but the first full-branch pass (b5-final-review.md, b5-final-verify.md, both `status: BLOCKED` on the 120k cap, DEC-0023) and r7 partials (b5-final-A1/A3-review-r7-partial.md BLOCKED) left 17 of the round-7 defects (FIX-7..26) for a later pass; caught by the fix's own or the next review 9 (FIX-5, DEC-0033, -0034, -0041, -0043, -0049, FIX-24, -25, DEC-0037).

## 3. Input-domain analysis

| gate | dimension | defects on it | named before the first defect? |
|---|---|---|---|
| gate_plan.plans_in_flight | git object state (HEAD / index / work tree; deleted, staged, mode, type, symlink) | V-3, FIX-5, DEC-0034, -0041, -0049, FIX-24 | partly: plan0:2667 names "working tree or HEAD" only; Review Focus 5 (plan0:40) names one case; index, deletion, mode, type: nowhere |
| gate_plan.plans_in_flight | path form (root vs repo top) | FIX-20 | nowhere |
| gate_plan.plans_in_flight | encoding | FIX-25 | nowhere |
| gate_plan overlap/Files | path form and filesystem identity (case, NFC/NFD, backslash, dir containment, absolute, ..) | FIX-13, -14, DEC-0045, -0057 | nowhere (plan0 test "line ranges do not hide an overlap" only) |
| gate_plan parser | text grammar (Files item placement, fences, pin title) | FIX-3, -12, -19, V-4, FIX-15 | not in plan: DEC-0017 records "neither the spec nor the S2 plan defines how a Files item is written"; pin title from b5r4 brief :22 |
| gate_review_package | text grammar of package files (gates/commits/diff/claims/siblings/transcript) | FIX-7, -17, -18, -22, -26, DEC-0038, -0040, -0046, -0051, -0052 | plan0 code checked only diff `startswith("diff --git")`, FAIL-prefixed gates lines and the first table; the grammar came from R F1-F6 |
| gate_review_package --all | file type and permissions (file vs dir, mode 000, dangling symlink) | FIX-6, -21, -23, F1 | nowhere |
| make_review_package | git config (color.*, core.abbrev, i18n.logOutputEncoding, quotePath, fileMode, symlinks) | V-1, V-2, FIX-7, DEC-0041, -0049 | color.ui only, by task-5-review1:49; rest nowhere |
| make_review_package | ignore state (exclude, gitignore, pycache, symlink to ignored) | FIX-16, DEC-0033, -0039, -0044, -0048, -0053, -0054, -0055 | probed by task-5-review2:43 and accepted; no rule in plan or brief |
| gate_dispatch fallback | config value types (str/bool rank, model choice) | FIX-8, DEC-0028 | nowhere (DEC-0019 proposal "opus = on") |
| lib Result | output format vs spec :191 | FIX-10, DEC-0040, -0047 | spec :191 named it; the fix-wave review did not check lib changes against it |

The reviewer prompt (reviewer.md:7 item 4) asks for "assume the change is wrong for some input X, name X"; it names no dimension. The verifier prompt (verifier.md:9) points at "the inputs named in each plan's Review Focus", and the S2 Review Focus (plan0:32-40) names 5 inputs, 1 of them in b5.

## 4. Underspecification (DEC-0024..0057, 34 decisions)

25 decide gate semantics the plan should have fixed before implementation: 0024 (PASS line form; spec settled it, plan code did not enforce it), 0025, 0026, 0027, 0028, 0029, 0033, 0034, 0035, 0037, 0038, 0039, 0040, 0041, 0044, 0045, 0046, 0048, 0049, 0051, 0052, 0053, 0054, 0055, 0057. 9 are process or migration rulings (0030, 0031, 0032, 0036, 0042, 0043, 0047, 0050, 0056).
Plan text that left them open (plan0 = plan at fe3f146):
- plan0:2667 "a plan is in flight when the last non-empty ledger line is not a CHECKPOINT line in the working tree or in the HEAD commit (or the ledger is not committed)": no index, deletion, mode/type, root form → 0034, 0041, 0049.
- plan0:2129 "Data rows of the first Markdown table in text" → 0038 (and FIX-22).
- plan0:2154 `test not in steps[0].group("cmd")` and plan0:2177 "'sibling | test name or finding id'" → 0035, 0037, 0046, 0051, 0052.
- plan0:2963 `FILE_RE = re.compile(r"^- (?:Create|Modify|Test|Delete): \`([^\`]+)\`")` with no placement or path rule → 0026, 0027, 0045, 0057 (and DEC-0017 before them).
- plan0:2548-2549 bare `git log --oneline` / `git diff -U10`, and plan Task 5 sets no clean-tree rule → 0029, 0033, 0039, 0044, 0048, 0053, 0054, 0055.
- plan0:3153 / plan0:2226-2230 gates lines checked by `startswith("FAIL ")` only → 0040; lib signature not stated → 0024.
- DEC-0018/0019 add the fallback with "opus = on" and no type rule → 0028.

## 5. Fix-introduced defects (W, 16)

| defect | fix | the fix's own review | what it checked / missed |
|---|---|---|---|
| V-4 hyphen titles | 1a6ff7c (B5-FIX-3) | b5r5-review1..4 (fixwave, diff ranges review-7569366..1a6ff7c, -1a6ff7c..c8e30a5 ...) | checked word boundary on ASCII words; no test of `pin-less`, Unicode hyphens |
| FIX-8, DEC-0028 | 1a6ff7c (B5-FIX-2) | b5r5-review1..4 | rank mutant killed (c8e30a5); no non-int rank, no other model |
| FIX-10 | 1a6ff7c (lib) | b5r5-review1..4 | reviewed the dispatch use; did not check a shared-lib signature change against spec :191 |
| FIX-15 | 173ae2d (B5-FIX-4) | b5-final-review2/verify2, scoped "Focus: the B5-FIX-4 wave" | added Unicode lookarounds; no `re.I` special folds, no category Cf |
| FIX-5 | 4b8a9d6 (B5-FIX-4 V-3) | verify2 | caught by it: byte compare of work tree vs HEAD skips index and deletion |
| DEC-0034 | c0316eb (B5-FIX-5) | b5r8-review1 | caught: bytes equal, mode/type differ |
| FIX-24, FIX-25 | c0316eb | round 7 A3 | caught: new branches untested; bare decode |
| DEC-0041 | DEC-0034 fix | b5r9-rr-A3 | caught: comparing modes ignores core.fileMode |
| DEC-0049 | DEC-0041 fix | b5r10 | caught: the same for core.symlinks |
| DEC-0033 | B5-FIX-16 fix | own implementer | literal ignored-file check refused every package (tools/gates/__pycache__) |
| DEC-0054 | 16766df | b5r11 D1 | code exempted paths DEC-0044 said to report |
| DEC-0043 | DEC-0040 fix | b5r10 Q1 | strict line broke three reviewed packages |
| DEC-0047 | B5-FIX-10 fix | b5r10 D-A5-1 | render path left printing a reason; S3 plan relies on it |
| DEC-0037 | DEC-0035 fix | b5r9 DQ-1 | "test trees" undefined; resolution scope picked by the implementer |

Pattern: each fix to plans_in_flight or the ignore check added one more git or filesystem dimension. The next review found the dimension next to it (bytes → index → mode → fileMode → symlinks; exclude → pycache → other plans → spec → symlink → all specs). The fix's class statement named only the reproduced input, never the dimension.

## 6. Root causes, ranked by defects explained

1. **No input-domain model for the gates (plan, brief and prompts name no dimensions).** 33 defects: every row in §3 marked "nowhere" or "partly", plus the C rows that are next steps on the same dimension (V-1, V-2, V-3, FIX-5, -6, -7, -13, -14, -16, -18, -20, -21, -23, -25, DEC-0033, -0034, -0038, -0039, -0041, -0044, -0045, -0046, -0048, -0049, -0051, -0052, -0053, -0054, -0055, -0057, FIX-8, -15, V-4). Evidence: reviewer.md:7 item 4 has no dimension list; plan0:32-40 Review Focus names 1 b5 input; task-review matrix (grep, this report's C7): no T4 review mentions quotePath, abbrev, permissions or root-subdirectory.
2. **Gate semantics were decided during fix rounds by the controller and handed to reviewers as binding.** 7 R defects (FIX-3a, V-2, FIX-7, -12, -18, -26, DEC-0040) plus 3 where R narrowed P (FIX-17, FIX-22, DEC-0040's later fallout DEC-0043); and 25 of 34 DECs ask afterwards what the plan should have said (§4). Evidence: t4-review2-body.md:3 "the controller's fix-round resolutions (these bind ...) F1 ... F5 first table only; F6 ... diff.patch starts `diff --git a/`"; plan0:2225 accepted `diff --git` (scratch reproducer C9 shows git emits `diff --git "a/...`); b5r4-controller-brief.md:22; DEC-0026 "Fix-1 design pick never asked". CLAUDE.md allows controller rulings only on plan-vs-spec conflicts, max 5; the ledger b5 section records 6 ruling lines (grep, C8) while T4 round 1 alone carried F1-F10.
3. **Fixes and re-reviews are scoped to the fix diff and the reproduced input, not the class.** 16 W + 11 C = 27 (§5). Evidence: re-review titles "scoped re-review ... round 6dc2657..119e0e8" (task-4-review2.md:3), task-6-review2..6 "scoped"; b5-final-verify2 "Focus: the B5-FIX-4 wave"; review-<a>..<b>.diff files in this dir are per round.
4. **Plan code was approved with weak behaviour and copied in (P), and D3 re-sync then wrote the shipped code back into the plan, so the plan never acts as an independent oracle.** 7 P defects (V-1, V-3, FIX-6, -14, -17, -20, -22). Evidence: plan0:2129, :2154, :2259-2261, :2548, :2667, :3147-3153, :3179 match the defective code; the plan grew from 4831 lines (fe3f146) to 8389 (HEAD) by re-sync.
5. **Mutation coverage is reviewer-chosen, not exhaustive.** 5 defects (FIX-9, -19, -23, -24, B5-FIX-4 F1). Evidence: task-5-review1:33 lists its own mutant set; aa22d0c subject "a test per branch" while mutants close-indent, cap, ckpt-endfiles survived (B5-FIX-19 class).
6. **Review size beyond the 120k reviewer cap.** It did not create defects, but it delayed 17 findings by two rounds. Evidence: b5-final-review.md and b5-final-verify.md `status: BLOCKED`; DEC-0023 "31+ commits, 32 files, a 496 KB diff".

## 7. Process changes

1. **Input-domain matrix in the plan template (covers RC1, RC4).** Each gate task carries a table: dimension → values → expected verdict, drawn from a fixed checklist (git object state HEAD/index/work tree incl. delete/mode/type; git config keys that change output; file type/permissions/symlink; encoding; path form incl. root ≠ toplevel, case/NFC; text grammar with an explicit EBNF). gate_plan FAILs a gate task without it; the plan reviewer checks it before approval. It would have caught, at plan approval: V-1, V-2, V-3, FIX-5, -6, -7, -13, -14, -18, -20, -21, -25, DEC-0034, -0041, -0049 (about 15), and most C rows at the first fix. Cost: about 30-60 plan lines per gate and one owner read. Misses: dimensions outside the checklist, untested branches (RC5) and semantics left blank in the grammar.
2. **No semantics in fix-round briefs (covers RC2).** A fix-round finding whose fix needs an acceptance rule not in the plan or spec stops as a decision question before code, with the grammar written out. Reviewers get the brief's rules as hypotheses ("resolutions" are not binding on the reviewer). Add this to gate_decisions or controller.md. It would have caught FIX-3a, V-2, FIX-7, -12, -17, -18, -22, -26, DEC-0040 at task review round 1 (owner sees the grammar), and it would move DEC-0026..0057-type questions before implementation. Cost: owner latency per task fix round. Misses: wrong owner picks.
3. **Class-wide fixes and re-reviews (covers RC3).** A fix's class names the dimension, not the reproduced value. The fix review re-runs the dimension's matrix row (from change 1) over the whole function, not the round diff, and checks every shared-lib change against the spec lines it touches. It would have caught V-4, FIX-8, -10, -15, DEC-0028, -0033, -0034, -0041, -0047, -0049, -0054 at the fix's own review (about 11 of 16 W), and the C chains in one round instead of three. Cost: re-reviews about 2× tokens; needs blocks small enough for the cap (change 5). Misses: dimensions not in the matrix.
4. **Mechanical mutation gate (covers RC5).** A script mutates every changed branch (comparison, constant, boolean, early return) and FAILs on any survivor, run by the reviewer and in gate_review_package. It would have caught FIX-9, -19, -23, -24 and F1 at task review 1. Cost: minutes of runtime per task; equivalent mutants need a register exception. Misses: missing behaviour (no branch to mutate), e.g. FIX-6, -14.
5. **Design simplicity and a stated threat model (covers RC1, RC6).** The spec states that the gates defend against honest agent mistakes, not adversarial inputs. Inputs outside the model FAIL closed by a whitelist (ASCII path grammar, refuse non-regular files, refuse non-default git config, one table per file); they are not handled case by case. Tally over the 51 non-N units: adversarial or exotic, about 17 (V-2, FIX-6, -7 quoted path, -14 case/backslash, -15, -21, -23, DEC-0034, -0041, -0044, -0045, -0049, -0051, -0052, -0053, -0054, -0057). Honest-mistake, about 34 (the rest, e.g. FIX-3, -5, -9, -10, -12, -16, -17, -19, -20, -22, -24, -25, -26, V-1, V-3). A whitelist fail-closed rule would have retired most of the 17 with one decision each instead of a chain. Cost: some legitimate inputs refused (DEC-0046 shows 611 of 727 Unity tests are `static`, so a whitelist needs data). Misses: honest-mistake defects (they still need changes 1-4).
Also: cap a review package below the reviewer token cap (split by task at plan time, not after a BLOCKED pass) so a pass covers all of its area (RC6).

## claims

| claim | direct | contradiction | output | verdict |
|---|---|---|---|---|
| C1 plan0 docstring says first table | `git show fe3f146:<plan> \| grep -n 'first Markdown table'` | parent of 6dc2657 lacks the file; grep at 6dc2657 = 1 | plan0:2129; count 1 | PASS |
| C2 constructs absent in parent, present at the introducing sha | `git show <sha>^:<f> \| grep -cF <s>` vs `<sha>:<f>` | same command, the other side | 119e0e8 diff/names_test/TEST_NAME 0→2/1/1; 2ce84d2 lexists 0→1; 605b768 PIN_TITLE 0→1; 1a6ff7c fallback_error 0→1; 173ae2d pin_title 0→1; c0316eb decode 0→1; b66deaf status 0→1; aa22d0c normpath 0→2; 41940dc color.ui 0→1 | PASS |
| C3 passed() gained reason in 1a6ff7c | `git show 1a6ff7c:tools/gates/lib/__init__.py \| grep -n 'def passed'` | `git show 1a6ff7c^:...` | `:41 def passed(self, id: str, reason: str = "")` vs `:41 def passed(self, id: str)` | PASS |
| C4 T4 re-review bound to controller resolutions | `sed -n 3p t4-review2-body.md` | grep for "these bind" absent elsewhere would refute; also found in t5/t6 review1 bodies ("binding control...") | quoted in §6 RC2 | PASS |
| C5 plan contract names only work tree and HEAD | `sed -n 2667p plan0` | grep 'index' in that line → none | quoted §4 | PASS |
| C6 first full-branch pass hit the budget cap | `head -1 b5-final-review.md b5-final-verify.md` | a DONE status would refute | both `status: BLOCKED` | PASS |
| C7 T4 reviews never named quotePath/abbrev/subdirectory root | `grep -ciE` matrix over task-*-review*.md | non-zero counts would refute | quotePath 0,0,0,0; abbrev 0 everywhere; subdirectory 0 everywhere | PASS |
| C8 ledger b5 ruling lines | `grep -ciE '^- ruling\|controller ruling'` on ledger b5 section | n/a | 6 | PASS |
| C9 git emits a quoted first path and empty subject that F6 rejects | scratch repo under rca/q (guarded): `git diff` → `diff --git "a/\303\274.txt" ...`; `git log --oneline -1` → `9bd81b5 ` | `re.match(r'^[0-9a-f]{7,40} \S', line)` | `COMMIT_LINE match: False`; `core.abbrev=4` → `b7da` (4 hex) | PASS |
| C10 plan grew by re-sync | `wc -l` HEAD plan; `git show fe3f146:<plan> \| wc -l` | n/a | 8389 vs 4831 | PASS |

Approximate counts: in the §2 miss-reason split and the §7.5 threat-model split, the boundary between some categories is my judgment (e.g. whether "root a subdirectory" counts as honest). Each list is spelled out so the owner can re-tally.

## findings raised
None in code. Unrelated finding, process: this investigation's own guard deviation (below).

## Decision needed
None for this report. Changes 1-5 are proposals for the owner (plan template, decision policy, model/cost of re-reviews).

## denials
None. Deviation: two early commands wrote `plan0.md` and `ledb5.txt` into the scratchpad root via `git show ... >` run from the checkout directory, not from a scratch clone (DEC-0032 guard not applied). No tracked file changed; `git status` was not altered. All later writes ran under the guard in scratchpad/rca/q.

## budget used
About 95k of 120k tokens; about 35 of 90 minutes. No background jobs were started.
