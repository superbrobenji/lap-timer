# T1 input-domain table: tools/program/load_context.py (spec D24, D31, D32; DEC-0171, DEC-0172)

Fix round 4/5 of program tooling Task 1. Skeleton built by hand (D31; the generator is row B5-PROC-2): one row per (path, argument, env or write target the loader reads or writes, D24/D31 checklist dimension). Tests are methods of `tools.gates.tests.test_load_context.LoadContext`, each through `load_context.main()` on a gitfixture repository (the stdout-encoding row runs the real file as a subprocess). Every FAIL verdict means: rc 1, only `FAIL context.<kind>.<id>: <reason>` lines, each one printable line, no expected list left listing anything (F4/F6), total stdout under 16,384 bytes (DEC-0172).

Sources cited: plan = `docs/superpowers/plans/2026-10-29-program-tooling.md` (:20 output under 16 KB, ranges; :32 Review Focus 2); playbook = `docs/superpowers/playbooks/two-streams.md` "Context contract" (:46-:50); D28 (outside the whitelist FAILs closed with a named check), D32 (unreadable, wrong kind, symlink at any component below the root, a path argument that is a symlink FAIL naming the path; names and values match as whole strings; git output forms pinned); DEC-0172 (clip-and-cap).

## Skeleton grep (base 4d0c6c7)

```
$ grep -nE "open\(|os\.(path\.|makedirs|replace|unlink|environ|symlink|stat|lstat|open)|shutil|glob|walk|pathlib|subprocess\.|add_argument|parse_args|environ|load_yaml\(|repo_root\(|sys\.stdout" tools/program/load_context.py   # at 4d0c6c7
38:sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
52:CONTEXT_DIR = os.path.join(".superpowers", "context")
72:def git_bytes(root: str, *args) -> "subprocess.CompletedProcess":
73:    return subprocess.run(["git", "-C", root, *args], capture_output=True, check=False)
95:    if path.startswith("/") or "\\" in path or os.path.normpath(path) != path or ".." in path.split("/"):
108:    return os.path.join(CONTEXT_DIR, "refs", tag, path)
129:            if os.path.islink(os.path.join(root, *parts[:i + 1])):
131:        full = os.path.join(root, path)
132:        if not os.path.isfile(full):
136:        with open(full, "rb") as fh:
155:    full = os.path.join(root, rel)
156:    os.makedirs(os.path.dirname(full), exist_ok=True)
157:    with open(full, "wb") as fh:
202:    if rel is None or not os.path.isfile(os.path.join(root, rel)):
204:    with open(os.path.join(root, rel), encoding="utf-8") as fh:
225:    d = os.path.join(root, CONTEXT_DIR, "expected", session)
226:    os.makedirs(d, exist_ok=True)
227:    tmp = os.path.join(d, f".{kind}.json.tmp")
228:    with open(tmp, "w", encoding="utf-8") as fh:
232:    os.replace(tmp, os.path.join(d, kind + ".json"))
262:        doc = load_yaml(os.path.join(root, CONTEXT_YML), root)
299:    session = os.environ.get("CLAUDE_CODE_SESSION_ID", "")
308:                os.unlink(os.path.join(root, CONTEXT_DIR, "expected", session, kind + ".json"))
316:    ap.add_argument("kind", help="a session kind or dispatch role named in context.yml")
318:        ns = ap.parse_args(argv)
321:        root = os.path.abspath(ns.root) if ns.root else repo_root()
327:    sys.stdout.write(text)
$ grep -nE "environ|subprocess\.run|open\(" tools/gates/lib/__init__.py   # load_yaml, repo_root (called by the loader)
61:    env = os.environ.get("CLAUDE_PROJECT_DIR")
65:        p = subprocess.run(["git", "rev-parse", "--show-toplevel"],
105:        with open(path, encoding="utf-8") as fh:
```

Read: `context.yml` (load_yaml :262), handoff (:202-:205), entry files (:129-:137), git objects (:73 ls-tree, rev-parse, show). Write: ref copies (:155-:158), expected dir and tmp/final list (:225-:232), removal (:308), stdout (:327). Argv: `kind` (:316), `--root` (gate_args). Env: `CLAUDE_CODE_SESSION_ID` (:299), `CLAUDE_PROJECT_DIR` (repo_root :61), and git's own `GIT_*` environment (every git call).

## Table

| # | dimension | values | expected verdict | test (or N/A with reason) |
|---|---|---|---|---|
| A1 | argv `kind`: count | none; two kinds | FAIL context.input (D28) | test_malformed_lists_and_arguments_fail_closed |
| A2 | argv `kind`: grammar, whole-string match | `../x`; `stream-a-block\n` (trailing line terminator); uppercase; leading `-` (an option) | FAIL context.input; fullmatch (D32) | test_malformed_lists_and_arguments_fail_closed; test_arguments_and_session_id_match_as_whole_strings |
| A3 | argv `kind`: not in `lists` | `sync` absent | FAIL context.<kind>.list | test_malformed_lists_and_arguments_fail_closed |
| A4 | argv `kind`: length | 100,000 characters | FAIL, the echoed kind clipped to 200 characters, stdout under 16,384 bytes (DEC-0172) | test_fail_output_values_are_clipped_and_the_output_capped_below_16_kb |
| A5 | argv: unknown option, its value echoed by argparse | `--bogus=<100,000 x>` | FAIL context.input on stdout, nothing unbounded on stderr (plan :20 "printed output") | test_arguments_and_session_id_match_as_whole_strings |
| A6 | argv `-h` | `-h` | usage on stderr, FAIL context.input (no load ran; gate_args contract) | test_arguments_and_session_id_match_as_whole_strings |
| R1 | `--root`: kind | missing; a regular file; a FIFO-free directory that is not a git work tree | FAIL context.input naming the root (D32 wrong kind) | test_root_must_be_a_work_tree_top_and_not_a_symlink |
| R2 | `--root`: symlink | the argument itself a symlink to the tree | FAIL context.input (D32, DEC-0093) | test_root_must_be_a_work_tree_top_and_not_a_symlink |
| R3 | `--root`: root vs toplevel (D24 path form) | a subdirectory of the work tree | FAIL context.input: entries are repository paths (`git show <ref>:<path>` is toplevel-relative, playbook :47), so a root below the top is the wrong kind (D32) | test_root_must_be_a_work_tree_top_and_not_a_symlink |
| R4 | `--root`: relative path, trailing `/` | `./x/`, relative | normalised by abspath, loads as the same tree | test_root_must_be_a_work_tree_top_and_not_a_symlink |
| R5 | `--root` absent: `CLAUDE_PROJECT_DIR` env | set to a missing dir, a symlink, a subdirectory; unset outside any repo | same verdicts as R1-R3 (the value is the root) | test_root_must_be_a_work_tree_top_and_not_a_symlink |
| E1 | env `CLAUDE_CODE_SESSION_ID` | unset; empty; `../../x`; valid id + `\n` | FAIL context.<kind>.session, no list written (playbook :49 "no expected list"; fullmatch D32) | test_no_session_id_fails_closed; test_arguments_and_session_id_match_as_whole_strings |
| E2 | env `GIT_DIR`, `GIT_WORK_TREE`, `GIT_INDEX_FILE`, `GIT_CONFIG_*`, other `GIT_*` | GIT_DIR naming another repository; GIT_WORK_TREE elsewhere | ignored: every git call runs on the root with `GIT_*` removed, LC_ALL=C (D32 pinned output forms; the root names the repository) | test_git_environment_and_config_cannot_change_what_is_read |
| E3 | repo config / attributes that change git output (D31) | `core.quotePath`, `color.ui=always`, `core.precomposeunicode=false`, a textconv attribute on the file at the ref | no effect: `ls-tree -z`, `rev-parse --verify`, `cat-file blob` print raw forms; pathspecs literal (`--literal-pathspecs`) | test_git_environment_and_config_cannot_change_what_is_read |
| C1 | context.yml: file state | missing; a directory; a FIFO; a symlink; a symlink in a component (`docs` linked); unreadable (mode 000); not UTF-8 | FAIL context.<kind>.list naming the file (D32); a FIFO never blocks | test_missing_context_file; test_context_yml_of_the_wrong_kind_is_a_named_fail |
| C2 | context.yml: git state | untracked; modified in the working tree | loaded from the working tree (the plan names the file by path, as every gate reads its registers) | test_context_yml_of_the_wrong_kind_is_a_named_fail (untracked loads) |
| C3 | context.yml grammar: top level type | mapping; list; scalar; empty; several YAML documents | non-mapping or several documents: FAIL ...list (D28) | test_malformed_context_yml_structure_is_a_named_fail_at_every_level; test_yaml_tags_documents_depth_and_numbers_are_named_fails |
| C4 | grammar: keys at every level | non-string (int, null, bool, date) at top, in `lists`, in an entry; duplicate key; merge key `<<` | FAIL ...list or ...<path> "unknown key"; duplicate/merge refused by the shared strict loader | test_malformed_context_yml_structure_is_a_named_fail_at_every_level; test_yaml_tags_documents_depth_and_numbers_are_named_fails |
| C5 | grammar: `lists`, kind list, entry types | null, list, mapping, string, bool, empty | FAIL ...list / ...entry (D28) | test_malformed_context_yml_structure_is_a_named_fail_at_every_level; test_malformed_lists_and_arguments_fail_closed |
| C6 | grammar: field types path/lines/why/ref | null, int, bool, list, mapping, float, `!!binary`, `!!set`, timestamp | FAIL ...entry or ...<path> naming the field (D28) | test_malformed_context_yml_structure_is_a_named_fail_at_every_level; test_yaml_tags_documents_depth_and_numbers_are_named_fails |
| C7 | grammar: YAML tags | `!!python/object/apply:os.system`, unknown local tag | FAIL ...list (SafeLoader refuses) | test_yaml_tags_documents_depth_and_numbers_are_named_fails |
| C8 | grammar: aliases and anchors | an alias of a whole entry; an alias of a field value; a self-recursive anchor; an alias chain (billion laughs, depth 4, 5, 8) | alias of an entry or value: loads like the written-out entry; recursive or exploded structures: FAIL ...entry, value clipped to 200 characters, output capped (DEC-0172), bounded work | test_yaml_aliases_and_anchors_load_like_written_out_entries; test_fail_output_values_are_clipped_and_the_output_capped_below_16_kb |
| C9 | grammar: nesting depth, number size | 5,000 nested lists; a 5,000-digit integer (ValueError on 3.12) | FAIL ...list, never `internal` | test_yaml_tags_documents_depth_and_numbers_are_named_fails |
| C10 | grammar: unknown entry keys | `extra`, non-printable key | FAIL ...<path> "unknown key" | test_malformed_lists_and_arguments_fail_closed; test_every_fail_line_is_escaped_and_one_line_at_every_level |
| P1 | entry `path`: form (D24 path form) | absolute; `..`; backslash; not normalised (`./a`, `a//b`, `a/`); under `.git/`; empty | FAIL ...<path> (whitelist, D28) | test_ref_and_path_that_could_inject_or_escape_are_refused; test_path_under_dot_git_is_refused |
| P2 | `path`: encoding / characters | C0, C1, DEL, tab, U+2028/2029/202E/200B/FEFF/0085; printable non-ASCII | non-printable: FAIL; printable non-ASCII: loads | test_path_with_a_control_character_is_refused; test_any_nonprintable_character_in_path_why_ref_or_lines_is_refused; test_printable_non_ascii_path_still_loads |
| P3 | `path`: case and Unicode normalisation | `docs/A.md` for tracked `docs/a.md`; NFD spelling of an NFC-tracked name | FAIL "not a regular file tracked at HEAD" (whole-string match, D32) | test_path_matches_the_tracked_name_as_a_whole_string |
| P4 | `path`: pathspec magic | a tracked file named `:(glob)*.md` | loads as the literal name (literal pathspecs) | test_git_environment_and_config_cannot_change_what_is_read |
| P5 | `path`: file type | directory; symlink; symlinked dir component; FIFO; empty file | FAIL ...<path> (D32) | test_symlinked_file_and_symlinked_directory_component_are_refused; test_directory_is_refused; test_empty_file_is_refused; test_unreadable_entry_file_is_a_named_fail (FIFO) |
| P6 | `path`: permissions | mode 000 | FAIL ...<path> "unreadable" (D32), not `internal` | test_unreadable_entry_file_is_a_named_fail |
| P7 | `path`: git state at HEAD | untracked; staged only; deleted in the working tree; modified in the working tree; unborn HEAD | untracked/staged/unborn: FAIL "tracked at HEAD"; deleted: FAIL missing path; modified: loads with the blob of the bytes on disk (playbook :48 "blob sha of the file as read") | test_untracked_file_is_refused; test_missing_path; test_ref_that_is_not_a_commit_and_unborn_head; test_prints_each_entry_and_writes_the_expected_list |
| P8 | `path`: length in the FAIL id | 5,000 characters | id clipped to 200 characters (DEC-0172) | test_fail_output_values_are_clipped_and_the_output_capped_below_16_kb |
| L1 | `lines` grammar | absent, empty, `5`, `0-3`, `3-2`, `a-b`, `1-3-5`, `-1-3`, int, past EOF, trailing `\n`, non-ASCII digits | FAIL "range" (fullmatch, D32) | test_missing_range_absent_or_malformed_or_past_the_end; test_any_nonprintable_character_in_path_why_ref_or_lines_is_refused |
| L2 | `lines` size | 2000 / 2001 lines; 16,000 / 16,001 bytes; per range not per file | plan :20 | test_limits_are_inclusive_2000_lines_and_16000_bytes; test_range_over_2000_lines; test_range_within_2000_lines_but_over_the_byte_cap; test_byte_cap_is_per_range_not_per_file |
| W1 | `why` | empty, blank, non-printable, folded `>` ending in `\n` | FAIL "why" | test_malformed_lists_and_arguments_fail_closed; test_any_nonprintable_character_in_path_why_ref_or_lines_is_refused |
| W2 | `why` with the output's field markers (` -- `, ` lines `) | -- | N/A: owner question Q-PROG-1, not this round's (dispatch) | N/A |
| F1 | `ref` grammar | `--output=x`; empty; non-printable; tab | FAIL "unsafe ref" | test_ref_and_path_that_could_inject_or_escape_are_refused; test_any_nonprintable_character_in_path_why_ref_or_lines_is_refused |
| F2 | `ref` git state | missing ref; a tree sha; a blob sha (not a commit); an annotated tag (peels to its commit) | missing / not a commit: FAIL "missing ref"; tag: loads | test_missing_ref; test_ref_that_is_not_a_commit_and_unborn_head |
| F3 | path at the ref | absent; a symlink; a directory; a submodule | FAIL "missing path at ref" / "symlink at ref" / "not a regular file at ref" | test_ref_without_the_path; test_file_at_a_ref_must_be_a_tracked_regular_file |
| H1 | handoff file (stream kinds) state | missing; a directory; a FIFO; a symlink; mode 000; not UTF-8 | FAIL context.<kind>.<handoff path> (D32: an input that is unreadable or of the wrong kind) | test_handoff_of_the_wrong_kind_or_grammar_is_a_named_fail |
| H2 | handoff front matter grammar | none; invalid YAML; list/scalar; duplicate `context_files`; merge key; context_files null/mapping/string; entries null/list; mixed key types | FAIL ...<handoff path>; duplicate and merge keys refused as in context.yml (the shared strict loader; the entries are context.yml entries, plan Step 3) | test_malformed_handoff_front_matter_fails; test_malformed_handoff_context_files_structure_is_a_named_fail; test_handoff_of_the_wrong_kind_or_grammar_is_a_named_fail |
| H3 | handoff `context_files` absent | field absent | adds nothing (module contract, plan Step 3) | test_handoff_without_context_files_adds_nothing |
| H4 | which handoff per kind; non-stream kinds | stream-a, stream-b, sync | own handoff only | test_stream_b_reads_the_prototype_handoff_not_the_other; test_kinds_without_a_handoff_ignore_handoffs |
| T1 | write target: ref copy `.superpowers/context/refs/<tag>/<path>` | the copy is a symlink; a directory; a component (`.superpowers`, `refs`) is a symlink or a regular file; unwritable directory | FAIL context.<kind>.<entry path> naming the write problem (D32), never written through a symlink | test_ref_copy_write_target_errors_are_named_fails |
| T2 | write target: expected list `.superpowers/context/expected/<sid>/<kind>.json` on success | the list is a symlink; a directory; `expected` or `<sid>` is a symlink or a regular file; the directory is unwritable | FAIL context.<kind>.<expected list path> (D32; F6), rc 1, never an exception | test_expected_list_write_errors_are_named_fails |
| T3 | removal of the earlier list on any failure (F4, F6) | the list's directory unwritable (file writable / file read-only); the list is a directory; a symlinked component; absent | FAIL ...<expected list path>: removed, or emptied when it cannot be removed, or "still stands" when neither is possible; absent: nothing to do | test_expected_list_removal_errors_are_named_fails; test_every_kind_of_failure_removes_an_earlier_expected_list |
| T4 | isolation of other kinds and sessions | other kind's list; other session's list | untouched | test_a_failure_leaves_other_kinds_and_other_sessions_alone |
| T5 | concurrent loaders of one session and kind | -- | N/A: one session runs its loader serially; the temp file is unique (mkstemp), so two runs cannot corrupt one list | N/A |
| O1 | output size, success | 16,383 / 16,384 bytes | 16,384 or more: FAIL ...output (plan :20) | test_output_boundary_16383_passes_and_16384_fails; test_output_over_16_kb |
| O2 | output size, failures (F5) | 300 missing paths; alias chains; 5,000-character values | every value clipped to 200 characters; FAIL lines printed while the total stays under 16,384 bytes, then `FAIL context.<kind>.output: N further failures not shown`; every failure counted (DEC-0172) | test_fail_output_values_are_clipped_and_the_output_capped_below_16_kb; test_every_failure_is_counted_when_the_output_is_capped |
| O3 | output form | every failure level | one printable line per FAIL | test_every_fail_line_is_escaped_and_one_line_at_every_level |
| O4 | stdout encoding | `PYTHONIOENCODING=ascii`, `LC_ALL=C` | stdout is UTF-8 (the byte count the cap measures) | test_stdout_is_utf8_whatever_the_locale |
| O5 | stdout closed or a broken pipe | -- | N/A: the output channel itself; no FAIL line can be delivered on it | N/A |
| X1 | multi-item isolation | three failing entries | every failure listed | test_all_failures_are_listed_not_only_the_first |
| X2 | anything else raised | a mocked RuntimeError; git not on PATH | FAIL context.<kind>.internal: unexpected <Type> (last resort) | test_every_kind_of_failure_removes_an_earlier_expected_list; test_git_environment_and_config_cannot_change_what_is_read |
