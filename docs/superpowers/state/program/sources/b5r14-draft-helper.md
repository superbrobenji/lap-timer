#### S2-b5-process-code: the D32 shared fail-closed helper -- D24 table and D28 whitelist (B5-PROC-2)

Surface (base 6a7b2f9; the gate code is unchanged since f1057e5: `git diff --stat f1057e5 6a7b2f9 -- tools/gates/*.py tools/gates/lib tools/gates/hooks` prints nothing). The helper is new code in `tools/gates/lib` (spec D32 :46; DEC-0066, DEC-0085, DEC-0093). Every gate, `make_review_package` and the hooks call it instead of `open`/`os`/`subprocess`/`re` for: E1 open a regular file for read (text or bytes); E2 list or walk a real directory; E3 write a target; E4 run git with forced output forms; E5 match a name or value whole (fullmatch). It also provides E0, the root check (DEC-0068 a), and R, the per-item result wrapper (`<pfx>.input` and isolation). It reads what each caller names, writes only the targets E3 is given, and runs only git. Callers: the six gates (`run_all.GATES`; D32 "For every gate:"), `make_review_package.py`, `hooks/pre_tool_use.py`, `hooks/session_start.py`, `hooks/user_prompt_submit.py` (stdin; a helper entry point, DEC-0129 sheet Q-FH-20 (a)). make_review_package and the hooks are under D32 by DEC-0129 sheet Q-FH-24 (a: "yes, every script under tools/gates"), whose own text adds "one spec line" that spec :46 does not hold yet (`grep -c make_review_package` spec -> 0). The D34 pre-commit gate becomes a thin shell shim exec'ing a Python hook that uses the helper (FH.77, DEC-0129 sheet Q-PC-17 (b)). The D31 generator lists helper calls (FH.70, Q-FH-19 = Q-SG-18, answered a; the sheet's Q-FH-19 row: "Q-SG-4 is not equal"). Base for this round: 2e43f13; gate code is unchanged since f1057e5 (`git diff --stat f1057e5 2e43f13 -- tools/gates/*.py tools/gates/lib tools/gates/hooks \| wc -l` -> 0).

D32 (spec :46), quoted once. Cells cite it by clause:
- **D32-a**: "For every gate: an input or write target that is unreadable, of the wrong kind, a symlink at any path component below the repository root, or a path argument that is itself a symlink (DEC-0093) FAILs that item's input check (`<pfx>.input`, as DEC-0066) naming the path"
- **D32-b**: "in a multi-item run one item's input failure FAILs that item and the others still run"
- **D32-c**: "names and values match as whole strings (fullmatch)"
- **D32-d**: "git commands whose output a gate reads force their output forms (pinned config and attributes)"
- The rest of :46: "One shared fail-closed helper in `tools/gates/lib` enforces it (register row B5-PROC-2); until it lands each gate applies the same rule in place. Table cells cite this default; only choices it does not settle are owner questions."

Q-D32-W answer, DEC-0129 sheet row Q-D32-W (recommendation, approved by DEC-0129 approve-except-flagged): "(c): clauses 1-4 (unwritable, missing parent, disk full or partial write, pre-existing outside the root) as (a); clause 5 (symlink component outside the root) as Q-SG-21". Option (a): "yes, each FAILs `<pfx>.input` naming the path (the cells settle; make_review_package.py:160's makedirs becomes a refusal or stays as creating a real directory; about 3 tests per gate)". Sheet row Q-SG-21, recommendation (a): "yes: Q-D32-W loses that clause in all three tables and the cells cite DEC-0093; 0 code, but a /tmp-through-symlink target is written silently". Cells cite the clause as **W1** unwritable, **W2** missing parent, **W3** disk full or partial write, **W4** pre-existing outside the root (each as (a)), **W5** symlink component outside the root (as Q-SG-21 (a)). Cells marked ‡ were observed at 2e43f13 in the guarded clone `r17-fix2-helper/repo` (`r17-fix2-helper/bin/p.sh`, `p2.sh`).

**D31 skeleton (by hand).** Grep 1 (328 hits; no test files): `grep -nE '\bopen\(|\bos\.[a-z_]+(\.[a-z_]+)?\(|shutil\.|glob\.|pathlib|Path\(|\.read_text|\.write_text|\.iterdir|subprocess\.|add_argument|\.(match|search|fullmatch)\(|re\.compile' tools/gates/*.py tools/gates/lib/*.py tools/gates/hooks/*.py`. Grep 2: `grep -nE 'tempfile|pathlib|\bPath\b|io\.open|codecs|os\.scandir|os\.access|os\.replace|os\.rename|fnmatch|stdin' <same files>` adds pre_tool_use.py:74,:278, gate_plan.py:364 (fnmatch.fnmatchcase) and user_prompt_submit.py:15 (json.load(stdin)). There is no shutil, pathlib or scandir hit. Pure-string `os.path` calls touch no filesystem (N/A). Shell hooks are the pre-commit table's.
- E1 read (r):
  - lib/__init__.py:105 (load_yaml: findings, claims and decisions.yml via registers.py:44; config.yml via config.py:15)
  - gate_plan.py:142 (--plan, --source, the ledger's plan), :546 (ledger bytes); kind tests :366, :426, :434, :544
  - gate_review_package.py:53 (package files), :200 (test files, errors=replace); kind tests :157, :175, :198, :242, :281, :331, :342 (+getsize), :346
  - gate_dispatch.py:82 (--prompt), :255 (role block), :158, :252
  - gate_decisions.py:69, :75 (--scan)
  - gate_register.py:75-77, :102-104 (reports, errors=replace)
  - make_review_package.py:100-102, :124-126; kind and existence tests :110 (`os.path.exists(<root>/.GIT)`, picks the pathspec magic), :189 (`os.path.isfile` per package file)
  - session_start.py:38 (islink), :41 (hook bytes), :65
  - registers.py:109, :386 (require_regular: os.stat follows links)
- E2 list or walk (r):
  - gate_plan.py:540, :612
  - gate_review_package.py:308, :371, :376, :379 (lstat), :386, :391, :404, :408, :435
  - gate_register.py:62-70 (os.walk followlinks=True)
  - make_review_package.py:98, :108 (glob), :139, :190
  - run_all.py:37, :42 (glob)
  - session_start.py:61, :76 (glob)
  - gitinfo.py:21-22
- E3 write (w): make_review_package.py:82 (_write wb), :160 (makedirs); gate_claims.py:95.
- E4 git (run):
  - gitinfo.py:30 (rev-parse, status with -c status.showUntrackedFiles=all, worktree list, diff --name-only -z, ls-tree -z, ls-files -s -z, diff --cached), :143, :154
  - lib/__init__.py:65
  - gate_plan.py:524 (show), :530, :556, :582 (config --bool)
  - gate_review_package.py:193
  - make_review_package.py:53 (PLAIN pins), :70 (merge-base, unpinned)
  - session_start.py:22, :30; pre_tool_use.py:247
- E5 match (r):
  - names and values: make_review_package.py:104, :128, :137 (NAME `.match` with `$`); gate_plan.py:152, :156, :364 (fnmatchcase), :408, :419, :433, :518, :533, :628; gate_review_package.py:126, :159, :168, :178, :186, :196, :230; gate_dispatch.py:201
  - already fullmatch: gate_register.py:112, :118, :178; gate_decisions.py:64; registers.py:79, :153, :183, :234, :238, :293, :316, :328, :334, :409; gitinfo.py:138
  - line grammars: gate_plan.py:73, :166-330; gate_review_package.py:70, :79; gate_register.py:81, :115; pre_tool_use.py:133-238 (Q-FH-17)
- argparse (arg):
  - lib/__init__.py:132-133 (-h, --root)
  - gate_claims.py:103-106; gate_decisions.py:37; gate_plan.py:639-642; gate_review_package.py:420-421; gate_dispatch.py:355-359; run_all.py:78; make_review_package.py:171-175 (plan, task, base, head, --only)
  - path arguments: --root, --plan, --source, --dir, --scan, --prompt, --prompts-dir; names that compose a write path: plan, task
- No entry point fits (Q-FH-20):
  - gitinfo.py:106 (gh); session_start.py:79 (bash)
  - gate_dispatch.py:80 (`--prompt -`); hooks' `json.load(stdin)` (user_prompt_submit.py:15, pre_tool_use, session_start)
  - lib/__init__.py:61 (CLAUDE_PROJECT_DIR); pre_tool_use.py:285 (SD_PROMPTS_DIR); gate_dispatch.py:41 (`~/.claude` prompts dir)
  - pre_tool_use.py:278-291 (temp file outside the root)
  - containment logic: pre_tool_use.py:295 (`os.path.realpath`); registers.py:104-109; gate_plan.py:370; gate_review_package.py:359, :425-426; gate_register.py:44
  - existence as a verdict: gate_dispatch.py:158, :224-227; gate_plan.py:408, :434
  - mode bits: gate_plan.py:569-573 (DEC-0034/0081)

**Table.** Probes ran in guarded clones under the scratch dirs `r14-draft-helper/` (f1057e5) and `r15-fix-helper/` (6a7b2f9). Each script starts with `set -eu` and asserts `pwd -P` and the toplevel lie under the scratch dir. `r.py` prints `rc lines fails | first FAIL or input line`. Cells marked † were re-observed at 6a7b2f9 (`r15-fix-helper/bin/p.sh` and two inline probes); the rest were observed at f1057e5, where the code was the same. Notation: `GP` = `/usr/bin/python3 <clone>/tools/gates/gate_plan.py` (absolute script path); `P` = `docs/superpowers/plans/2026-10-13-fix-F0-pr101-closeout.md`; `MRP` = `make_review_package.py strict-delivery t HEAD~1 HEAD --only claims`. Baseline: `GP --plan P` -> rc=1 lines=53 fails=35, meaning the file is read normally.

| row | item (path or argument) | dimension | values | expected verdict | settled by | today |
|---|---|---|---|---|---|---|
| FH.1 | path argument (E1/E2) | path form | relative, cwd = a subdirectory | resolve against the root | DEC-0129 sheet Q-FH-1 (a: the root, as today) | † `cd docs && GP --plan superpowers/plans/<P name>` -> `FAIL plan.file: ... no such plan file` (resolved against the root) |
| FH.2 | path argument | path form | absolute, inside the root | accepted | D32-a (no clause refuses it) | † `GP --plan <clone>/P` -> baseline |
| FH.3 | path argument | path form | absolute, outside the root, no link | accepted (read as a checked input; D32-a still applies to the file) | DEC-0129 sheet Q-FH-2 (b: accept) | `GP --plan $S/out/o.md` -> baseline |
| FH.4 | path argument | path form | `docs/../P` | accepted (read as a checked input; D32-a still applies to the file) | DEC-0129 sheet Q-FH-2 (b: accept) | `GP --plan docs/../P` -> baseline |
| FH.5 | path argument | path form | `../repo/P` | accepted (read as a checked input; D32-a still applies to the file) | DEC-0129 sheet Q-FH-2 (b: accept) | `GP --plan ../repo/P` -> baseline |
| FH.6 | path argument | path form | trailing slash on a file | FAIL `<pfx>.input` (wrong kind) | D32-a | `GP --plan P/` -> `FAIL plan.file: P/: no such plan file` |
| FH.7 | path argument | symlink | the argument is a link (inside the root) | FAIL `<pfx>.input` | D32-a | `GP --plan plans/link.md` -> baseline (followed; R-C-2) |
| FH.8 | path argument | symlink | the argument is a link to outside the root | FAIL `<pfx>.input` | D32-a | † `ln -s $S/out/o.md olink.md; GP --plan olink.md` -> baseline (followed) |
| FH.9 | path argument | symlink | a directory component below the root is a link | FAIL `<pfx>.input` | D32-a | `GP --plan ldir/<P name>` -> baseline |
| FH.10 | path argument | symlink | a component above the root is a link (the path is outside) | accepted | D32-a ("below the repository root"); DEC-0093 below-root-plus-args; for write targets the same shape is FH.10b | `GP --plan $S/outlink/o.md` -> baseline |
| FH.10b | E3 target | symlink above root | the root reached through a link above it (cwd `/tmp/...` -> `/private/tmp/...`) | accepted, written | DEC-0129 sheet Q-D32-W (c) W5 as Q-SG-21 (a): "the cells cite DEC-0093"; DEC-0093 below-root-plus-args | ‡ `cd /tmp/<scratch>/repo && make_review_package.py strict-delivery t HEAD~1 HEAD --only claims` -> `wrote .superpowers/sdd/strict-delivery/review/t: 1 commit(s); gates PASS` (toplevel prints `/private/tmp/...`) |
| FH.11 | path argument | case | `DOCS/...` on APFS | FAIL (byte for byte per component); id form OPEN Q-FH-6 | DEC-0129 sheet Q-FH-3 (a: DEC-0086 a byte for byte per component for every argument) | `GP --plan DOCS/superpowers/plans/<P name>` -> baseline |
| FH.12 | path argument | Unicode | NFD name for an NFC file | FAIL (byte for byte per component); id form OPEN Q-FH-6 | DEC-0129 sheet Q-FH-3 (a) | `GP --plan plans/café.md` -> baseline |
| FH.13 | E1 input | kind | directory | FAIL `<pfx>.input` | D32-a | `GP --plan docs` -> `FAIL plan.file: docs: no such plan file` |
| FH.14 | E1 input | kind | FIFO | FAIL `<pfx>.input`, no hang | D32-a | `GP --plan ff` -> `FAIL plan.file`; findings.yml a FIFO: `gate_register.py` -> `TIMEOUT 15s (hang)` |
| FH.15 | E1 input | kind | socket | FAIL `<pfx>.input` | D32-a | `GP --plan sock` -> `FAIL plan.file` |
| FH.16 | E1 input | kind | device | FAIL `<pfx>.input` | D32-a | `GP --plan /dev/null` -> `FAIL plan.file` |
| FH.17 | E1 input | kind | dangling link | FAIL `<pfx>.input` | D32-a | `GP --plan dang` -> `FAIL plan.file` |
| FH.18 | E1 input | kind | missing | FAIL `<pfx>.input`; prefix form OPEN Q-FH-6 | §7 :203; DEC-0129 sheet Q-FH-4 (a: `<pfx>.input`) | `GP --plan nope.md` -> `FAIL plan.file: nope.md: no such plan file` |
| FH.19 | E1 input | permissions | mode 000 file | FAIL `<pfx>.input` naming the path | D32-a; DEC-0085 a | `GP --plan u.md` -> `FAIL plan.input: [Errno 13] Permission denied: '/private/tmp/.../u.md'` |
| FH.20 | E2 dir | permissions | mode 000 directory in a walk | FAIL `<pfx>.input` | D32-a | `gate_register.py` -> `FAIL register.input: docs/reviews/d: Permission denied` |
| FH.21 | E2 dir | permissions | listable but not searchable (600) | FAIL `<pfx>.input`, empty or not | DEC-0129 sheet Q-FH-5 (a: FAIL always); D32-a "unreadable" | empty d 600 -> `rc=0 PASS register.schema` |
| FH.22 | E1 text | encoding | UTF-8, LF, final LF | read | DEC-0067 a | baseline |
| FH.23 | E1 text | encoding | BOM | FAIL `<pfx>.encoding` | DEC-0067 a | `GP --plan bom.md` -> baseline; a BOM findings.yml -> `PASS register.schema` |
| FH.24 | E1 text | encoding | non-UTF-8 byte | FAIL naming the file; id `<pfx>.encoding` or `<gate>.input` | DEC-0067, DEC-0085; id OPEN Q-FH-6 | `GP --plan bad.md` -> `FAIL plan.input: 'utf-8' codec can't decode byte 0xff` |
| FH.25 | E1 text | encoding | CRLF | FAIL `<pfx>.encoding` | DEC-0067 a | `GP --plan crlf.md` -> baseline; a CRLF findings.yml -> PASS |
| FH.26 | E1 text | encoding | empty | FAIL `<pfx>.encoding` | DEC-0067 a ("non-empty") | `GP --plan empty.md` -> `rc=1 fails=8`; empty findings.yml -> `FAIL register.input: ... empty file` |
| FH.27 | E1 text | encoding | no final LF | FAIL `<pfx>.encoding` | DEC-0129 sheet Q-FH-7 (a) | `GP --plan nolf.md` -> baseline |
| FH.28 | E1 text | encoding | reads with errors=replace or as bytes (reports, test files, role blocks, hook files, stdin) | DEC-0067 applies: a non-UTF-8 byte FAILs `<pfx>.encoding` naming the file | DEC-0129 sheet Q-FH-8 (a: yes, reports, test files, blocks, hook files and stdin) | † `docs/reviews/zz.md` = `register: \377 B5-NOPE-1`; `gate_register.py` -> `FAIL register.marker.docs/reviews/zz.md: ... (found 'register: � B5-NOPE-1')` |
| FH.29 | E1 text | line terminators | `str.splitlines` also splits on VT, FF, FS-RS, NEL, U+2028 and U+2029 | LF only | DEC-0067 a ("LF only") | † a VT replaces the LF before `**Goal:**`: `GP` -> `PASS plan.header.goal`; with a space instead -> `FAIL plan.header.goal` |
| FH.30 | E1 text | grammar | DEC-0067 a as EBNF: `file = line, {line}; line = {char - LF}, LF; char = UTF-8 scalar - (U+FEFF at offset 0) - CR` | conforming files read; anything else FAILs `<pfx>.encoding` | DEC-0067 a; last-line LF required: DEC-0129 sheet Q-FH-7 (a) | see FH.22-27 |
| FH.31 | E0 root | path form | `--root` = realpath of toplevel | PASS | DEC-0068 a | baseline |
| FH.32 | E0 root | path form | `--root docs` (subdirectory) | FAIL `<gate>.input` | DEC-0068 a | `GP --root docs --plan P` -> `FAIL plan.input: docs/superpowers/state/findings.yml: No such file or directory` |
| FH.33 | E0 root | symlink | `--root` is itself a link to the toplevel | FAIL naming the path; id `<pfx>.input` (D32-a) or `<gate>.input` (DEC-0068 a): OPEN Q-FH-6 | D32-a; DEC-0129 sheet Q-FH-9 SETTLED: "S:46 D32 'a path argument that is itself a symlink (DEC-0093) FAILs that item's input check'; `--root` is a path argument (lib/__init__.py:133)" | † `GP --root $S/rootlink --plan P` -> baseline (accepted) |
| FH.34 | E0 root | worktree | `--root` a linked worktree | PASS | DEC-0068 a | `gate_register.py --root $S/wt` -> `rc=0 PASS register.schema` |
| FH.35 | E0 root | env | CLAUDE_PROJECT_DIR = a subdirectory | FAIL `<gate>.input` | DEC-0068 a | `gate_register.py` -> `FAIL register.input: ... findings.yml: No such file or directory` |
| FH.36 | E0 root | symlink above root | root under /tmp (-> /private/tmp) | accepted | DEC-0093 below-root-plus-args ("one realpath of the root"); DEC-0068 a | † `GP --root /tmp/<scratch>/repo --plan P` -> baseline |
| FH.37 | E3 target | symlink | a component below the root links outside (`review` -> `$S/out2`) | FAIL `<pfx>.input`, nothing written | D32-a; make_review_package under D32: DEC-0129 sheet Q-FH-24 (a) | `MRP` -> `rc=0 wrote ...review/t`; the files land in `$S/out2/t` (R-B-2) |
| FH.38 | E3 target | symlink | the target is itself a link to outside (claims.yml, diff.patch) | FAIL `<pfx>.input` | D32-a; the diff.patch half: DEC-0129 sheet Q-FH-24 (a) | † claims.yml -> `$S/out3/claims.yml` (a verified claim citing findings.yml); `gate_claims.py --mark-stale --base <HEAD~1>` -> `PASS claims.marked-stale.C-demo-1`, outside file now `state: stale`; † diff.patch -> `$S/out3/o.txt`: `MRP` -> `rc=0`, outside file now `diff --git ...` |
| FH.39 | E3 target | kind | the target is a directory, FIFO or socket | FAIL `<pfx>.input` | D32-a (wrong kind); make_review_package under D32: DEC-0129 sheet Q-FH-24 (a) | † diff.patch a directory: `MRP` -> `cannot write ...: Is a directory` rc=1, no check id; a FIFO -> `TIMEOUT 15s (hang)` |
| FH.40 | E3 target | kind | the target exists as a regular file below the root | overwrite | S2 plan :3795 ("writes `.superpowers/sdd/<plan>/review/<task>/{commits.txt,diff.patch,gates.txt}`") | † diff.patch `STALE`: `MRP` -> rc=0, file now `diff --git ...` |
| FH.41 | E3 target | parent | parent directory missing | FAIL `<pfx>.input` naming the path, or create a real directory: OPEN Q-FH-10 | DEC-0129 sheet Q-D32-W (c) W2 as (a), whose own text reads "makedirs becomes a refusal or stays as creating a real directory"; that choice is Q-FH-10, flagged OPEN by DEC-0129 | † `.superpowers` absent: `MRP` -> rc=0, `review/t` created |
| FH.42 | E3 target | permissions | target unwritable (444) | FAIL `<pfx>.input` naming the path; Q-FH-11 ("which id?", flagged OPEN by DEC-0129) diverges: W1 (a) states the id | DEC-0129 sheet Q-D32-W (c) W1 as (a); OPEN Q-FH-11 | † diff.patch 444: `MRP` -> `cannot write ...: Permission denied` rc=1, no check id |
| FH.42b | E3 target | permissions | parent unwritable (555): target absent; target present and writable | absent: FAIL `<pfx>.input` naming the path (the target cannot be written); present: OPEN Q-FH-25 | absent: DEC-0129 sheet Q-D32-W (c) W1 as (a); present: OPEN Q-FH-25 | ‡ parent 555, files present: `MRP` -> `rc=0 ... gates PASS` (overwritten in place); parent 555 empty: `MRP` -> `make_review_package: cannot write .superpowers/sdd/strict-delivery/review/t: Permission denied` rc=1 |
| FH.43 | E3 target | partial write | file-size limit or ENOSPC mid-write | FAIL `<pfx>.input` naming the path; written by temp + rename, so no truncated target is left | DEC-0129 sheet Q-D32-W (c) W3 as (a); write form DEC-0129 sheet Q-FH-12 (a: temp + rename) | † `ulimit -f 4; trap '' XFSZ; MRP` -> `cannot write ...: File too large` rc=1; `wc -c diff.patch` -> 4096 (truncated) |
| FH.43b | E3 target | pre-existing outside the root | a write target outside the root that already exists | FAIL `<pfx>.input` naming the path | DEC-0129 sheet Q-D32-W (c) W4 as (a) | OPEN: the only outside-root writer, pre_tool_use.py:278 `tempfile.mkstemp`, makes a fresh name, so no call site writes a pre-existing outside-root target to observe |
| FH.44 | E3 path form | names composing the target | `plan`/`task` with LF; case and Unicode on APFS; `..` | LF: FAIL; case and Unicode: FAIL (byte for byte per component); `..`: FAIL (the first character must be `[A-Za-z0-9]`) | LF: D32-c; case and Unicode: DEC-0129 sheet Q-FH-22 (a); `..`: the NAME grammar `^[A-Za-z0-9][A-Za-z0-9._-]*$`, S2 plan :4448 (make_review_package code); D32 reach: DEC-0129 sheet Q-FH-24 (a) | † `make_review_package.py strict-delivery $'t\n' HEAD~1 HEAD --only claims` -> `rc=0 wrote .../review/t`; `ls review \| od -c` -> `t \n`; ‡ `... strict-delivery .. ...` -> `task '..' is not a directory name (^[A-Za-z0-9][A-Za-z0-9._-]*$)` rc=1; ‡ `T` then `t` -> both `rc=0 wrote`, `ls review` -> `T` (one directory) |
| FH.45 | E4 git | attributes | `* -diff` in `.git/info/attributes` | forced: a text patch | D32-d | `MRP`-like run -> `gates PASS`; diff.patch holds `Binary files a/CLAUDE.md and b/CLAUDE.md differ` (R-B-1) |
| FH.46 | E4 git | attributes | `diff=tc` + `diff.tc.textconv` | forced: no textconv | D32-d | `git show HEAD:./<ledger>` line 1 unchanged; `git show --textconv` -> `TEXTCONV` |
| FH.47 | E4 git | attributes | clean filter on ledger.md | forced: no filter | D32-d | `git diff --stat` empty; `git status --porcelain` -> ` M ...ledger.md` |
| FH.48 | E4 git | attributes | `binary`, `eol=crlf`, `text`, `export-subst` | forced | D32-d | † `* binary`: `git diff HEAD~1 HEAD \| grep -c '^Binary files'` -> 6; ‡ `* eol=crlf`, `* text`, `* export-subst` each: `^Binary files` 0, `git diff HEAD~1 HEAD \| shasum` -> ab563e77 (= no attributes), `git show HEAD:P \| shasum` -> 399bfc13 (= no attributes), `GP --plan P` -> baseline |
| FH.49 | E4 git | global files | `core.attributesFile`, `~/.config/git/attributes` | forced | D32-d | `cat ~/.config/git/attributes` -> No such file; core.attributesFile unset; the populated case needs a write under home: OPEN |
| FH.50 | E4 git | ignore files | `.git/info/exclude`, `core.excludesFile`, `~/.config/git/ignore` | pinned: E4 runs with no info or global exclude and an empty excludesFile | DEC-0129 sheet Q-FH-13 (a) | clone `core.excludesFile` hides `tools/gates/zz.py` from `git status --porcelain` (0 lines); host global ignore -> `**/.claude/settings.local.json` |
| FH.51 | E4 git | config keys | color, abbrev, diff.algorithm, diff.relative, log.*, i18n, core.quotePath; LC_ALL | one list of the `git help --config` keys that change the read forms, plus LC_ALL=C | DEC-0129 sheet Q-FH-14 (a) | make_review_package pins 5 keys (:44-45), gitinfo pins 1 (:56); `git config --list --show-origin` -> global `core.filemode=false`, `core.autocrlf=false` |
| FH.52 | E4 git | config read on purpose | core.fileMode, core.symlinks (gate_plan.py:582) | exempt by name from pinning | DEC-0129 sheet Q-FH-15 (a) | as FH.51 |
| FH.53 | E4 git | failure | the root is not a repository | FAIL `<gate>.input` | DEC-0068 a ("root must equal the realpath of `git rev-parse --show-toplevel`, else FAIL `<gate>.input`"); the sheet's Q-FH-16 row: "settled part: DEC-0068 a" | † `GP --root $S/nogit --plan docs/x.md` -> `FAIL plan.input: docs/superpowers/state/findings.yml: No such file or directory` (no git run); ‡ registers, plans and specs copied to `$S/ng2` (no `.git`): `GP --root $S/ng2 --in-flight` -> rc=0 lines=69 fails=0 (fail-open; the in-repo control prints the same 69 lines), `gate_register.py --root $S/ng2` -> `FAIL register.input: git cat-file -e 151f2bb^{commit}: fatal: not a git repository` |
| FH.53b | E4 git | failure | non-zero exit inside a repository | FAIL `<pfx>.input` | DEC-0129 sheet Q-FH-16 (a) | OPEN: no in-repo git failure was induced; the unchecked-rc sites are gate_plan.py:524, :530, :556, :582 (`check=False`) |
| FH.54 | E4 git | object state | HEAD path mode 120000 or 160000 | ledger: FAIL `plan.<slug>.ledger`; other E4 reads: FAIL `<pfx>.input` naming the path | DEC-0081 a; DEC-0129 sheet Q-FH-21 (a) | OPEN (not observed) |
| FH.54b | E4 git | object state | path absent at HEAD | other E4 reads: FAIL `<pfx>.input` naming the path; an in-flight ledger absent at HEAD stays in flight | DEC-0129 sheet Q-FH-21 (a), whose why reads "an in-flight ledger absent at HEAD stays in flight ... whatever a selects" | OPEN (not observed) |
| FH.54c | E4 git | object state | unborn HEAD | FAIL `<pfx>.input` | DEC-0129 sheet Q-FH-21 (a) | OPEN (not observed) |
| FH.54d | E4 git | object state | detached HEAD; a path absent from the index (`git rm --cached`); intent-to-add; skip-worktree | OPEN Q-FH-26 | OPEN Q-FH-26 | ‡ detached (`rev-parse --abbrev-ref HEAD` -> HEAD): `run_all.py` -> rc=0 lines=100 fails=0; † ledger removed from the index -> `PASS plan.strict-delivery.header.agentic` (in flight) |
| FH.55 | E5 value | anchoring | trailing LF | FAIL (fullmatch) | D32-c | `make_review_package.py $'strict-delivery\n' t ...` -> `docs/superpowers/state/strict-delivery\n/ missing` (accepted by NAME; R-B-13); task: FH.44 |
| FH.56 | E5 value | anchoring | trailing CR | FAIL | D32-c | `... $'strict-delivery\r' ...` -> `plan 'strict-delivery\r' is not a directory name` |
| FH.57 | E5 value | anchoring | empty | FAIL | D32-c | `... '' ...` -> `plan '' is not a directory name` |
| FH.58 | E5 value | scope | line grammars (headers, fences, list items, hook command scanners) | prefix matching stays; fullmatch binds ids, names, shas and budgets only; each line grammar is written as a grammar in its gate's own table | DEC-0129 sheet Q-FH-17 (b); D24 "text grammar written as a grammar" | OPEN (a rule question; nothing to observe) |
| FH.59 | R multi-item | isolation | review_package --all, one package file mode 000 | that item FAILs, the others run | D32-b | `--all` -> 1 line total `FAIL review_package.input: [Errno 13]` (R-A-6) |
| FH.60 | R multi-item | isolation | --all, one package file non-UTF-8 | same | D32-b | -> `FAIL review_package.input: 'utf-8' codec`; lines for the good package t2: 0 |
| FH.61 | R multi-item | symlink | --all, `<task>` a link to outside | that item FAILs | D32-a, D32-b | -> 6 lines under `review_package.p.t3.` (followed) |
| FH.62 | R multi-item | kind | --all, `<plan>` (an E2 entry) is a file | FAIL (missing input) | §7 :203 ("a non-directory where a directory is expected is", DEC-0022); D32-a (wrong kind) | † `.superpowers/sdd/pfile` a file: `gate_review_package.py --all` -> `rc=0 PASS review_package.all` (R-A-5) |
| FH.63 | R multi-item | isolation | --in-flight, a second ledger's plan non-UTF-8 | that plan FAILs, strict-delivery still checked | D32-b | `GP --in-flight` -> 1 line `FAIL plan.input: 'utf-8' codec` |
| FH.64 | R multi-item | kind, isolation | make_review_package ledger scan (:98-102), `state/zz/ledger.md` a FIFO or directory | that ledger FAILs `<pfx>.input`, nothing written | D32-a, D32-b; make_review_package under D32: DEC-0129 sheet Q-FH-24 (a) | † FIFO ledger: `MRP` -> `rc=0 wrote ...; gates PASS` (skipped silently; R-B-3) |
| FH.65 | R multi-item | isolation | decisions --scan with a FIFO, a mode-000 file and a good file | each FAILs alone | D32-b; id OPEN Q-FH-6 | `gate_decisions.py --scan s.f s.md P` -> `rc=1 fails=2`, `FAIL decisions.scan.s.f: not a regular file`; P is scanned |
| FH.66 | R multi-item | symlink | a --scan argument is a link | FAIL `<pfx>.input` | D32-a | `gate_decisions.py --scan s.l` -> `rc=0 PASS` |
| FH.67 | R multi-item | symlink | docs/reviews holds a directory link to outside | FAIL that item | D32-a, D32-b | `gate_register.py` -> `rc=0 PASS register.schema` (followlinks=True) |
| FH.68 | R multi-item | isolation | docs/reviews holds a FIFO among reports | FAIL that item, the others run | D32-a, D32-b | `gate_register.py` -> 1 line `FAIL register.input: docs/reviews/ff.md: not a regular file` |
| FH.69 | E1 register | symlink | findings.yml a link to outside | FAIL `register.input` | D32-a | `gate_register.py` -> `rc=0 PASS register.schema` |
| FH.70 | D31 generator (other drafter) | skeleton | gates call the helper, not `open`/`os` | the generator lists the helper's E0-E5 calls as filesystem and subprocess calls (a list beside the helper, pinned by a test) | DEC-0129 sheet Q-SG-18 (a = Q-FH-19 a) | OPEN (nothing exists) |
| FH.71 | R run_all | isolation | one gate's input FAILs | the other gates still run | D32-b | † findings.yml 000, `run_all.py` -> claims, plan, register and review_package 1 FAIL each; decisions 5 PASS and dispatch.config 15 PASS still run |
| FH.72 | R result | id form | `<pfx>` = gate or item prefix; `.encoding`; `plan.<slug>.ledger`; `decisions.scan.<rel>`; the path in the id or in the reason | one rule | DEC-0066, -0067, -0081, -0085; OPEN Q-FH-6 | see FH.19, FH.24, FH.65, FH.73 |
| FH.73 | R result | id grammar | an id that carries a path (with `/`) | FAIL `<gate>.input` | DEC-0082 a ("lib refuses an id outside `[A-Za-z0-9_.-]+`"); where the path goes instead: OPEN Q-FH-6 | † `gate_decisions.py --scan sub/x.md sub/f.md` -> `FAIL decisions.scan.sub/f.md: not a regular file` |
| FH.74 | R result | reason form | absolute vs root-relative path in the reason | OPEN (D32-a says only "naming the path") | OPEN Q-FH-6 | FH.19 shows an absolute path |
| FH.75 | pre-commit export (other drafter) | root | the root is a fresh export of the index, not a toplevel | E0 accepts it, or not | OPEN Q-FH-18 | OPEN (nothing exists) |
| FH.76 | no-fit call sites | scope | gh, bash, stdin, env paths, temp file, the `~/.claude` prompts dir | a helper entry point for each | DEC-0129 sheet Q-FH-20 (a) | OPEN (a scope question) |
| FH.77 | pre-commit gate as a caller (other drafter) | caller | a shell hook cannot import `tools/gates/lib` | a thin shell shim exec'ing a Python hook that uses the helper; its export root needs an E0 mode (OPEN Q-FH-18) | DEC-0129 sheet Q-PC-17 (b) | † `head -1 tools/gates/hooks/git-pre-commit.sh` is a shell script (pre-commit table PC.68) |
| FH.78 | path D34 mandates (other drafter) | symlink | `tools/.venv/bin/python3 -> python3.12`, below the root | accepted as named, the link resolved (one exception, written into D32) | DEC-0129 sheet Q-PC-10 (a) | † `ls -l <main>/tools/.venv/bin/python3` -> `python3 -> python3.12` |

N/A (4):
- NUL in an argv value: execve cannot carry NUL, and YAML values are already fullmatched in registers.py.
- Git object state for E1/E2/E3: these read the work tree; git state reaches the helper only through E4 (FH.45-54).
- Pure-string `os.path` calls: no filesystem access.
- P-1 stale bytecode (plan :7185): the helper runs no interpreter; the pre-commit table covers it (PC.33, PC.34).

**D28 whitelist.**
- E0: root == realpath(`git rev-parse --show-toplevel`) (DEC-0068 a), and `--root` is not itself a link (D32-a; sheet Q-FH-9 SETTLED).
- Path arguments: relative ones resolve against the root (sheet Q-FH-1 a); outside the root or with `..` accepted as checked inputs (Q-FH-2 b); every component equals the on-disk name byte for byte (Q-FH-3 a); `tools/.venv/bin/python3` accepted as named (Q-PC-10 a).
- E1: a regular file with no symlink at or below the root on its path; UTF-8 without BOM, LF only, non-empty, final LF (DEC-0066 a, DEC-0067 a, D32-a; sheet Q-FH-7 a), for every E1 text read (Q-FH-8 a).
- E2: a real directory, readable and searchable (sheet Q-FH-5 a), with the same link rule.
- E3: a regular file or an absent target whose path has no symlink below the root (D32-a), writable, not pre-existing outside the root, written by temp + rename (sheet Q-D32-W (c) W1, W3, W4 as (a); Q-FH-12 a); a link above the root is accepted (W5 as Q-SG-21 a); names byte for byte (Q-FH-22 a). A missing parent: OPEN Q-FH-10; parent 555 with the target present: OPEN Q-FH-25.
- E5: fullmatch for ids, names, shas and budgets (D32-c; sheet Q-FH-17 b).
- E4: the pinned set: one list of the `git help --config` keys that change the read forms plus LC_ALL=C (sheet Q-FH-14 a), no info or global exclude and an empty excludesFile (Q-FH-13 a), core.fileMode and core.symlinks exempt by name (Q-FH-15 a); a git failure FAILs `<pfx>.input` (Q-FH-16 a), a non-repository root `<gate>.input` (DEC-0068 a); non-blob objects FAIL (Q-FH-21 a); detached HEAD and index states OPEN Q-FH-26.
- Anything else FAILs `<pfx>.input` (encoding: `<pfx>.encoding`); the `<pfx>` form OPEN Q-FH-6.

Data at 2e43f13, main checkout (‡ re-run):
- `git ls-files -s | awk '$1==120000' | wc -l` -> 0
- `find docs tools/gates .superpowers ! -type f ! -type d | wc -l` -> 0
- `r17-fix2-helper/bin/wl.sh`, run from the main checkout, in full:
  ```
  set -eu
  n=0 bom=0 cr=0 empty=0 nolf=0 bad=0
  for f in $(ls docs/superpowers/state/*.yml docs/superpowers/plans/*.md docs/superpowers/state/*/ledger.md tools/gates/config.yml; find .superpowers/sdd -path '*/review/*' -type f); do
    n=$((n+1))
    [ "$(head -c3 "$f" | od -An -tx1 | tr -d ' ')" = efbbbf ] && bom=$((bom+1))
    grep -q "$(printf '\r')" "$f" && cr=$((cr+1))
    [ -s "$f" ] || empty=$((empty+1))
    [ -s "$f" ] && [ "$(tail -c1 "$f" | od -An -tx1 | tr -d ' ')" != 0a ] && nolf=$((nolf+1))
    iconv -f UTF-8 -t UTF-8 "$f" >/dev/null 2>&1 || bad=$((bad+1))
  done
  echo "files=$n bom=$bom cr=$cr empty=$empty nolf=$nolf bad=$bad"
  ```
  -> `files=110 bom=0 cr=0 empty=0 nolf=0 bad=0`. Contradiction: the same tests flag a planted `\xef\xbb\xbfx\r\n` file (bom, cr) and a planted `x` (nolf).
- `grep -l '^Binary files' .superpowers/sdd/*/review/*/diff.patch | wc -l` -> 0: no package holds a binary stub, so a forced E4 rewrites none.

**Open questions.**

| id | question | options (at least two, each with its cost) | rows |
|---|---|---|---|
| Q-FH-1 | Relative path arguments resolve against? | a: the root, as today (0 code, 1 test) / b: the cwd (1 change per argument, about 7 tests; breaks the run_all argv built from the root) | FH.1 -- answered: DEC-0129 sheet Q-FH-1 (a) |
| Q-FH-2 | Path arguments outside the root or with `..`? DEC-0093's chosen text says "covers --plan outside the root". | a: FAIL `<pfx>.input` unless normalised and inside (about 3 tests; /tmp fixtures and the hook's temp --prompt break; touches DEC-0093's reading) / b: accept (0 code, 1 test) | FH.3-5 -- answered: DEC-0129 sheet Q-FH-2 (b) |
| Q-FH-3 | Case and Unicode forms of path arguments? | a: DEC-0086 a byte for byte per component for every argument (a listing per component, about 4 tests) / b: accept what APFS resolves (0 code; a wrong-case path reads the file) | FH.11-12 -- answered: DEC-0129 sheet Q-FH-3 (a) |
| Q-FH-4 | Missing-input id? | a: `<pfx>.input` (renames plan.file, dispatch.brief, `...file.<name>`; about 10 tests change) / b: keep the per-gate ids (0 code; two id families stay) | FH.18 -- answered: DEC-0129 sheet Q-FH-4 (a) |
| Q-FH-5 | A listable, non-searchable directory? | a: FAIL always (1 stat per directory, 1 test) / b: FAIL only when an entry is needed (0 code; an empty 600 directory PASSes) | FH.21 -- answered: DEC-0129 sheet Q-FH-5 (a) |
| Q-FH-6 | `<pfx>` and reason form (deferred Q36): gate or item prefix; `.encoding` vs `.input`; DEC-0081 ledger id; `decisions.scan.<rel>`; relative path in the reason? | a: item prefix + `.input`, encoding `.encoding`, root-relative reason, path never in the id (renames in 3 gates, about 12 tests) / b: gate-level `<gate>.input` only, as DEC-0085 (about 6 tests; multi-item FAILs lose their item) | FH.24, .65, .72-74 -- OPEN: the sheet row reads "no recommendation" |
| Q-FH-7 | A text file with no final LF? | a: FAIL `<pfx>.encoding` (0 of 110 files today, 1 test) / b: accept (0 code; the EBNF gets an optional last LF) | FH.27, .30 -- answered: DEC-0129 sheet Q-FH-7 (a) |
| Q-FH-8 | Does DEC-0067 reach every E1 text read? | a: yes: reports, test files, blocks, hook files and stdin (about 5 call sites, 5 tests; a non-UTF-8 test file then FAILs) / b: only its named rows (0 code; errors=replace stays) | FH.28 -- answered: DEC-0129 sheet Q-FH-8 (a) |
| Q-FH-9 | `--root` given through a link? R-FH-3 shows that D32-a settles FH.33 (FAIL); kept open for the owner. | a: realpath, then compare (1 call; contradicts D32-a) / b: FAIL `<gate>.input` (1 lstat, 1 test) | FH.33 -- SETTLED: the sheet's Q-FH-9 row cites spec :46 D32 and lib/__init__.py:133 |
| Q-FH-10 | E3 parent missing? | = Q-D32-W; round-14 options (quoted at b5r14-tablerev-skeleton.md:94, carried by the sheet's Q-FH-10 row): a create (makedirs, as today) / b FAIL [no cost survives at any source] | FH.41 -- OPEN: flagged by DEC-0129 |
| Q-FH-11 | Unwritable target: which id? | = Q-D32-W; round-14 text known only as "Unwritable target: which id? ... (D32 names 'unreadable')" (b5r14-tablerev-skeleton.md:94); its a/b options and costs survive at no source (the sheet's Q-FH-11 row) | FH.42 -- OPEN: flagged by DEC-0129 |
| Q-FH-12 | Partial writes? | = Q-D32-W (the id); the write form (a: temp + rename, about 10 lines and 2 tests / b: plain write, 0 code, leaves truncated files) stays open here | FH.43 -- answered: DEC-0129 sheet Q-FH-12 (a); its id half by Q-D32-W (c) W3 as (a) |
| Q-FH-13 | Are ignore files "pinned config"? `core.excludesFile` is a config key (D32-d "pinned config"); DEC-0098 scope-default-config ("its D24 table carries the ignore and filter rows") and DEC-0101 drop-the-promise ("P-1, ignore, filter and global files are its D24 rows") put them in the hook's table. | a: yes: E4 runs with no info or global exclude and an empty excludesFile (gates lose user ignores; about 3 tests) / b: no: the pre-commit export covers them (0 helper code) | FH.50 -- answered: DEC-0129 sheet Q-FH-13 (a) |
| Q-FH-14 | The pinned config set and env? | a: one list of the `git help --config` keys that change the read forms, plus LC_ALL=C (a table, about 1 test per key) / b: today's 6 keys plus LC_ALL (0 new code; e.g. diff.algorithm stays unpinned) | FH.51 -- answered: DEC-0129 sheet Q-FH-14 (a) |
| Q-FH-15 | Config a gate reads on purpose? | a: exempt by name: core.fileMode, core.symlinks (2 lines, 2 tests) / b: pin them too (DEC-0041/0049 semantics change; about 4 tests) | FH.52 -- answered: DEC-0129 sheet Q-FH-15 (a) |
| Q-FH-16 | A git failure in E4? | a: FAIL `<pfx>.input` (gate_plan.py:524/:530/:556 change; about 4 tests) / b: the caller decides (0 helper code; each gate's own table covers it) | FH.53 -- answered: DEC-0129 sheet Q-FH-16 (a); non-repository value DEC-0068 a (FH.53) |
| Q-FH-17 | Which matches are "names and values"? | a: every regex the helper runs (line grammars rewritten as full-line patterns; about 30 patterns, 30 tests) / b: ids, names, shas and budgets only (about 8 sites, 8 tests) | FH.58 -- answered: DEC-0129 sheet Q-FH-17 (b) |
| Q-FH-18 | (pre-commit drafter) the export root vs DEC-0068 | a: E0 gets an export mode (about 10 lines, 3 tests) / b: the export is a worktree, so it is a toplevel (0 helper code; the export uses `git worktree add`) | FH.75 -- OPEN: the sheet row reads "no recommendation" |
| Q-FH-19 | (generator drafter) helper calls in the skeleton; = Q-SG-4, Q-SG-18 | a: the generator lists E0-E5 calls (a list beside the helper, 1 test) / b: the generator reads the helper's internals only (0 list; rows lose the caller's item) | FH.70 -- answered: = Q-SG-18, DEC-0129 sheet Q-SG-18 (a = Q-FH-19 a); the sheet's Q-FH-19 row: "Q-SG-4 is not equal" |
| Q-FH-20 | No-fit call sites (gh, bash, stdin, env, temp file, `~/.claude`) | a: a helper entry point for each (about 6 entries, 12 tests) / b: out of scope, listed in each gate's table (0 helper code) | FH.76 -- answered: DEC-0129 sheet Q-FH-20 (a) |
| Q-FH-21 | E4 reads of git objects that are not regular blobs (HEAD path mode 120000 or 160000, absent at HEAD, unborn or detached HEAD) | a: FAIL `<pfx>.input` naming the path (one mode check per E4 read, about 4 tests) / b: the caller decides, as gate_plan.py:524 returns None today (0 code; each gate's own table must cover it) | FH.54 -- answered: DEC-0129 sheet Q-FH-21 (a); detached HEAD and index states OPEN Q-FH-26 |
| Q-FH-22 | Case and Unicode forms of names that compose an E3 write path (plan, task) on APFS | a: byte for byte per component, as DEC-0086 a for --dir (one listing per component, 2 tests) / b: accept what the filesystem resolves (0 code; `T` and `t` write into one package) | FH.44 -- answered: DEC-0129 sheet Q-FH-22 (a) |
| Q-FH-23 | One reading of D32 for write targets across the three tables: do "unwritable" and "missing parent" fall under D32's "unreadable, of the wrong kind"? | = Q-D32-W | FH.41-43 -- answered with Q-D32-W |
| Q-FH-24 | (re-review R2-FH-2) Does D32 ("For every gate:") reach make_review_package.py and the hooks, which `run_all.GATES` does not list and the spec never names? | a: yes, every script under tools/gates (the D32 cells for FH.37-39, FH.44 and FH.64 stand; the hooks join the helper's callers; 0 text beyond one spec line) / b: gates only (those cells read OPEN, and make_review_package and the hooks need their own rule or DEC per row; about 6 cells reopen) | FH.37-39, FH.44, FH.64, surface -- answered: DEC-0129 sheet Q-FH-24 (a) |
| Q-FH-25 | A write target whose parent is unwritable (555) but which exists and is writable: under the temp + rename write form (sheet Q-FH-12 a) the temp file cannot be made if it lives in that parent, while a plain overwrite succeeds. Where does the temp file live, and what is the verdict? | a: FAIL `<pfx>.input` naming the path as W1: temp + rename always, and a rename into a 555 directory cannot succeed wherever the temp file lives (0 extra code; a 555 package directory with present files, which writes today, FAILs; about 1 test) / b: overwrite in place when the parent is unwritable, temp + rename otherwise (2 write paths, about 2 tests; a truncated target is possible again on that path, against Q-FH-12 a's aim) | FH.42b -- OPEN (new, round 17) |
| Q-FH-26 | E4 reads under a detached HEAD, and of a path absent from the index, intent-to-add or skip-worktree. The sheet's Q-FH-21 question lists "unborn or detached HEAD", but its answer a is "FAIL `<pfx>.input` naming the path (one mode check per E4 read)", and a mode check finds a regular blob under a detached HEAD; the index states are in neither. | a: detached HEAD is accepted; index-absent, intent-to-add and skip-worktree paths FAIL `<pfx>.input` naming the path (one index-flag read per E4 read, about 3 tests) / b: detached HEAD FAILs every E4 read as Q-FH-21 lists it (every detached clone and CI checkout FAILs; 1 test) / c: all four accepted, the caller decides (0 helper code; each gate's own table covers them) | FH.54d -- OPEN (new, round 17) |
| Q-D32-W | (shared, as the controller writes it) Do D32's input-check FAILs cover a write target that is unwritable, has a missing parent, hits disk full or a partial write, pre-exists outside the root, or passes a symlink component outside the root? | (a) yes, each FAILs `<pfx>.input` naming the path (the cells settle; make_review_package.py:160's makedirs becomes a refusal or stays as creating a real directory; about 3 tests per gate) (b) no, each stays an owner question per table (0 code now; the three tables keep OPEN cells) | FH.41-43 -- answered: DEC-0129 sheet Q-D32-W (c): W1-W4 as (a), W5 as Q-SG-21 (a) |

**Coverage.**
- B5-PROC-2 (helper clause): FH.1-78 with FH.10b, FH.42b, FH.43b, FH.53b, FH.54b-d.
- B5-PROC-2 siblings:
  - DEC-0098/0101 ignore, attribute, filter and global files: FH.45-50.
  - P-1 and the review packages run_all reads: the pre-commit table (N/A line, FH.75).
  - Spec §8: FH.75, FH.77.
  - git-pre-commit.sh, install-hooks.sh and B5-U3: the pre-commit table.
- Defects reached: R-B-1 FH.45; R-B-2 FH.37; R-B-3 FH.64; R-B-4 FH.19, FH.42; R-B-13 FH.55, FH.44; R-A-5 FH.62; R-A-6 FH.59; R-C-2 FH.7.
- Uncovered: the populated global attributes file (FH.49) needs a write under home.

**Fix round 1 (b5 round 15).**

| finding | fixed rows | how | evidence (command -> output) |
|---|---|---|---|
| R-FH-1 | whitelist | anchored command; 0 | `grep -l 'Binary files' ...diff.patch \| wc -l` -> 3; `grep -l '^Binary files' ... \| wc -l` -> 0 |
| R-FH-2 | FH.36 | cite the chosen option text plus DEC-0068 a | decisions.yml DEC-0093: "below-root-plus-args ... one realpath of the root"; "no change for /tmp paths" is option below-root |
| R-FH-3 | FH.33, Q-FH-9 | expected FAIL by D32-a; Q-FH-9 kept open with the note (rule 2) | lib/__init__.py:133 `--root` is an argument; † `GP --root $S/rootlink --plan P` -> baseline |
| R-FH-4 | FH.2, 8, 28, 29, 36, 38, 39, 40, 41, 42, 43, 48, 53, 71, 73 | observed (†) | p.sh lines in the cells; FIFO target -> TIMEOUT; claims link -> outside `state: stale` |
| R-FH-5 | FH.1 | absolute script path in `GP` | † `cd docs && GP ...` -> `FAIL plan.file` |
| R-FH-6 | header, every D32 cell | D32 quoted in full at :46 as clauses a-d; cells cite the clause | `grep -n '^| D32 ' spec` -> 46 (at 6a7b2f9) |
| R-FH-7 | skeleton | added gate_review_package.py:198, :386; session_start.py:38; gitinfo.py:22; the E5 lines; gate_plan.py:364; user_prompt_submit.py:15 | `sed -n 364p gate_plan.py` -> fnmatchcase; `sed -n 15p user_prompt_submit.py` -> `json.load(sys.stdin)` |
| R-FH-8 | FH.62 (new) | E2 non-directory row | † `.superpowers/sdd/pfile` -> `PASS review_package.all` |
| R-FH-9 | FH.54 (new), Q-FH-21 | object-state row | † detached and index-absent runs in the cell |
| R-FH-10 | FH.30 (new) | EBNF of DEC-0067 a | -- |
| R-FH-11 | FH.44 (new), Q-FH-22 | E3 name path form | † task `t\n` -> `wrote .../review/t`, `od -c` -> `t \n` |
| R-FH-12 | FH.64 (new), Coverage | R-B-3 mapped to the ledger scan | † FIFO ledger -> `rc=0 ... gates PASS` (corrected in fix round 2: the rc comes from the `--only` value, not the sha; ‡ `--only claims` -> rc=0 `gates PASS`, `--only gate_claims` -> rc=1 `gates FAIL`, gates.txt `FAIL run_all.unknown: no gate named 'gate_claims'`) |
| R-FH-13 | Q-FH-1..22 | a cost on every option | -- |
| R-FH-14 | FH.41-43, FH.70, FH.77, FH.78 | (b) Q-D32-W; (c) Q-FH-19 = Q-SG-4 = Q-SG-18; (a) FH.77 Q-PC-17; (d) FH.78 Q-PC-10 | precommit draft :41 PC.31 now reads Q-D32-W; :78 PC.68; :52 PC.42; skeleton review: "OPEN Q-SG-18 = Q-FH-19" |
| R-FH-15 | whitelist | command written | bash loop -> files=110, all counts 0 |
| R-FH-16 | Q-FH-2, Q-FH-13 | citations added; Q-FH-13 not narrowed (rule 2: never narrow) | DEC-0093 and DEC-0098/0101 text as quoted |
| R-FH-17 | N/A | P-1 line | plan :7185 (was :7221 at f1057e5) |
| R-FH-18 | FH.73 | expected FAIL `<gate>.input` (DEC-0082 a); the path's place goes to Q-FH-6 | † `--scan sub/f.md` -> `FAIL decisions.scan.sub/f.md` |

Line cites fixed for 6a7b2f9:
- D32 :47 -> :46
- plan :7221 -> :7185 (`grep -n 'S2-b5-process-code (row B5-PROC-2)'`)
- plan :3831 -> :3795 (`grep -n 'Produces: CLI .python3 tools/gates/make_review_package'`)

Q ids added: Q-FH-21, Q-FH-22, Q-FH-23 (= Q-D32-W), Q-D32-W (the shared row). Q-FH-10, -11 and -12 now cite Q-D32-W.

**Fix round 2 (b5 round 17).** Base 2e43f13 (`git rev-parse HEAD` -> 2e43f132...; tree clean). Guarded clone `r17-fix2-helper/repo` (`set -eu`; `pwd -P` and the toplevel asserted under the scratch dir; 0 porcelain lines after each probe). Sheet `docs/superpowers/state/strict-delivery/decision-sheets/b5r16.md`, `shasum -a 256` -> 8bef9f62...839b, equal to DEC-0129's question.

| item | rows | how | evidence (command -> output) |
|---|---|---|---|
| R2-FH-1 | FH.53, FH.53b (new) | split: not a repository -> FAIL `<gate>.input` by DEC-0068 a, observed with registers present; in-repo failure -> sheet Q-FH-16 (a) | ‡ `GP --root $S/ng2 --in-flight` -> rc=0 lines=69 fails=0; control in the clone -> 69 lines, 0 FAIL; `gate_register.py --root $S/ng2` -> `FAIL register.input: git cat-file -e 151f2bb^{commit}: fatal: not a git repository` |
| R2-FH-2 | D32-a quote, rest of :46, surface, FH.37, 38, 39, 44, 64 | "For every gate:" quoted; the helper and "Table cells cite" sentences quoted; the make_review_package and hook cells cite sheet Q-FH-24 (a) (Q-FH-24 added, marked answered) | `sed -n 46p spec` -> begins "Default contract (DEC-0088 O2; amends D25). For every gate:"; `grep -c make_review_package spec` -> 0 |
| R2-FH-3 | FH.48, FH.42b (new) | eol, text, export-subst observed; parent-555 cases observed | ‡ each attribute -> `^Binary files` 0, diff sha ab563e77 = none, show sha 399bfc13 = none; parent 555 present -> rc=0 `gates PASS`; empty -> `Permission denied` rc=1 |
| R2-FH-4 | skeleton | added make_review_package.py:110, :189, pre_tool_use.py:295; moved gate_register.py:112, :118, :178 and gate_decisions.py:64 to "already fullmatch" | `sed -n <l>p` -> `os.path.exists(os.path.join(root, ".GIT"))`, `os.path.isfile(os.path.join(pkg, f))`, `os.path.realpath(path)`, `FINDING_ID.fullmatch` x3, `DECISION_ID.fullmatch` |
| R2-FH-5 | FH.44 | `..` verdict cites the NAME grammar at S2 plan :4448 (outside the DEC-0092 form example :6952-7217); today observed | `grep -n 'A-Za-z0-9\]\[A-Za-z0-9._-\]' plan` -> :4448 `NAME = re.compile(...)`; ‡ `task '..' is not a directory name (^[A-Za-z0-9][A-Za-z0-9._-]*$)` |
| R2-FH-6 | fix-table row R-FH-12 | explanation corrected: the rc follows the `--only` value | ‡ `--only claims` -> rc=0; `--only gate_claims` -> rc=1, `FAIL run_all.unknown: no gate named 'gate_claims'` |
| R2-FH-7 | Q-FH-13 | labels corrected | decisions.yml :913 `scope-default-config` "carries the ignore and filter rows", :943 `drop-the-promise` "P-1, ignore, filter and global files are its D24 rows"; answers :917, :947 by `grep -n answer:` offsets 9 and 39 from :909 |
| R2-FH-8 | whitelist | the loop written in full (`bin/wl.sh`) | -> files=110 bom=0 cr=0 empty=0 nolf=0 bad=0; planted files flag bom, cr, nolf |
| R2-FH-9 | Q-FH-10, Q-FH-11, FH.43 | round-14 options restored beside "= Q-D32-W" (Q-FH-11's are lost; stated); FH.43 cites the write form, now answered (Q-FH-12 a) | sheet :228-230 (rows Q-FH-10..12) |
| R2-FH-10 | FH.54, FH.54b-d (new) | split by value; detached HEAD and index states: Q-FH-26 (new), since the Q-FH-21 answer's mechanism (a mode check) does not FAIL a detached HEAD although its question lists it | ‡ detached `run_all.py` -> rc=0 lines=100 fails=0 |
| Q-D32-W (c) | FH.10b (new), 41, 42, 42b, 43, 43b (new), whitelist E3 | sheet row quoted under the D32 quote; cells cite W1-W5 | sheet :165; ‡ FH.10b `cd /tmp/... && MRP` -> `wrote ... gates PASS` |
| Q-SG-21 (a) | FH.10b, FH.10 | W5 accepted, cites DEC-0093; resolves re-review D-2 (FH.10 vs PC.70) | sheet :166 |
| Q-FH-1 (a) | FH.1 | | sheet :219 |
| Q-FH-2 (b) | FH.3-5 | | sheet :220 |
| Q-FH-3 (a) | FH.11, 12 | id form left to Q-FH-6 | sheet :221 |
| Q-FH-4 (a) | FH.18 | | sheet :222 |
| Q-FH-5 (a) | FH.21 | | sheet :223 |
| Q-FH-7 (a) | FH.27, 30 | | sheet :225 |
| Q-FH-8 (a) | FH.28 | | sheet :226 |
| Q-FH-9 SETTLED | FH.33 | settling line cited; id form left to Q-FH-6 (as the sheet row says) | sheet :227 |
| Q-FH-12 (a) | FH.43 | | sheet :230 |
| Q-FH-13 (a) | FH.50 | | sheet :231 |
| Q-FH-14 (a) | FH.51 | | sheet :232 |
| Q-FH-15 (a) | FH.52 | | sheet :233 |
| Q-FH-16 (a) | FH.53b | | sheet :234 |
| Q-FH-17 (b) | FH.58 | | sheet :235 |
| Q-SG-18 (a = Q-FH-19 a) | FH.70, surface | equality corrected to Q-FH-19 = Q-SG-18 (the sheet's Q-FH-19 row) | sheet :237, :260 |
| Q-FH-20 (a) | FH.76, surface | | sheet :238 |
| Q-FH-21 (a) | FH.54, 54b, 54c | | sheet :239 |
| Q-FH-22 (a) | FH.44 | | sheet :240 |
| Q-FH-24 (a) | FH.37-39, 44, 64, surface | the answer's "one spec line" is not in the spec yet (stated in the surface) | sheet :167 |
| Q-PC-10 (a) | FH.78 | | sheet :178 |
| Q-PC-17 (b) | FH.77, surface | | sheet :185 |

Line cites checked at 2e43f13: spec D31-D35 :45-:49 (`grep -n '^| D3[1-5] '`); plan :3795, :7185 (`grep -n`); gate_plan.py :524, :530, :556, :582 (`grep -n subprocess.run`; the re-review's :557 is the line after :556's call start). None changed.

Q ids still open: Q-FH-6 (no recommendation), Q-FH-10 and Q-FH-11 (flagged by DEC-0129), Q-FH-18 (no recommendation), Q-FH-25 and Q-FH-26 (new, round 17).
