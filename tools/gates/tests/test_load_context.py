"""tools/program/load_context.py through main() on fixture repositories (program tooling Task 1).
Run from the repo root: python3 -m unittest tools.gates.tests.test_load_context -v
"""
import contextlib
import hashlib
import io
import json
import os
import re
import tempfile
import unittest
from unittest import mock

import yaml

from tools.gates.lib import config
from tools.gates.tests import gitfixture as gf
from tools.program import load_context

SID = "0b6f1c52-7d1e-4f0a-9a57-3c2d8e41aa10"
CTX = "docs/superpowers/state/program/context.yml"
A_HANDOFF = "docs/superpowers/state/strict-delivery/handoff.md"
B_HANDOFF = "docs/superpowers/state/prototype/handoff.md"
EXPECTED = ".superpowers/context/expected"
KIND = "stream-a-block"


def lines_file(n, width=0):
    return "".join(f"line {i}" + "x" * width + "\n" for i in range(1, n + 1))


def ctx(entries, kind=KIND):
    return yaml.safe_dump({"lists": {kind: entries}}, sort_keys=False)


def entry(path, lines, why="because", **kw):
    e = {"path": path, "lines": lines, "why": why}
    e.update(kw)
    return e


def front(context_files=None, extra=""):
    fm = "---\nplan: x\n" + extra
    if context_files is not None:
        fm += yaml.safe_dump({"context_files": context_files}, sort_keys=False)
    return fm + "---\n\nbody\n"


def blob(data: bytes) -> str:
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


class LoadContext(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        self.addCleanup(gf.remove, self.root)
        patcher = mock.patch.dict(os.environ, {"CLAUDE_CODE_SESSION_ID": SID})
        patcher.start()
        self.addCleanup(patcher.stop)
        for rel in (A_HANDOFF, B_HANDOFF):                # both handoffs exist in the real tree; a missing one FAILs (D32)
            gf.write(self.root, rel, front(None))
        gf.commit_all(self.root, "handoffs")

    def put(self, rel, text):
        gf.write(self.root, rel, text)

    def run_loader(self, kind=KIND, entries=None, raw=None, argv=None, commit=True):
        if raw is not None:
            self.put(CTX, raw)
        elif entries is not None:
            self.put(CTX, ctx(entries, kind))
        if commit and gf.git(self.root, "status", "--porcelain"):      # the loader reads files tracked at HEAD
            gf.commit_all(self.root, "fixture")
        out = io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(io.StringIO()):
            rc = load_context.main(["--root", self.root, *(argv if argv is not None else [kind])])
        return rc, out.getvalue()

    def expected(self, kind=KIND):
        with open(os.path.join(self.root, EXPECTED, SID, kind + ".json"), encoding="utf-8") as f:
            return json.load(f)

    def assertFailed(self, rc, out, fid, text=""):
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, r"(?m)^FAIL " + re.escape(fid) + r"(:|$)")
        self.assertIn(text, out)
        self.assertNotIn("PASS", out)
        self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED, SID, KIND + ".json")),
                         "a failed load must not leave an expected list")

    # -- the happy path ---------------------------------------------------------------------
    def test_prints_each_entry_and_writes_the_expected_list(self):
        data = lines_file(30).encode()
        self.put("docs/a.md", data.decode())
        rc, out = self.run_loader(entries=[entry("docs/a.md", "5-12", "the a doc")])
        self.assertEqual(rc, 0, out)
        self.assertEqual(out, f"docs/a.md lines 5-12 blob {blob(data)} -- the a doc\n")
        self.assertEqual(blob(data), gf.git(self.root, "hash-object", "docs/a.md"))
        self.assertEqual(self.expected(), {
            "kind": KIND, "session": SID,
            "entries": [{"path": "docs/a.md", "lines": "5-12", "blob": blob(data)}]})

    def test_limits_are_inclusive_2000_lines_and_16000_bytes(self):
        self.put("docs/two-k.md", "x\n" * 2000)          # 2000 lines + the empty line after the final \n
        tail = ["tail"]
        at_cap = ["y" * 99] * 159 + ["y" * 100] + tail          # lines 1-160 joined: 159*99 + 100 + 159 = 16,000 bytes
        self.put("docs/edge.md", "\n".join(at_cap) + "\n")
        self.put("docs/over.md", "\n".join(["y" * 99] * 159 + ["y" * 101] + tail) + "\n")     # 16,001 bytes
        rc, out = self.run_loader(entries=[entry("docs/two-k.md", "1-2000"), entry("docs/edge.md", "1-160")])
        self.assertEqual(rc, 0, out)
        rc, out = self.run_loader(entries=[entry("docs/over.md", "1-160")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/over.md", "over 16000 bytes")

    def test_read_counts_the_empty_line_after_a_final_newline(self):
        self.put("docs/a.md", "one\ntwo\n")                  # the Read tool reports totalLines 3 for this
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
        self.assertEqual(rc, 0, out)
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-4")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/a.md", "range")

    # -- every failure is named and rc 1 ---------------------------------------------------------
    def test_missing_path(self):
        rc, out = self.run_loader(entries=[entry("docs/nope.md", "1-3")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/nope.md", "missing path")

    def test_missing_range_absent_or_malformed_or_past_the_end(self):
        self.put("docs/a.md", lines_file(10))
        for lines in (None, "", "5", "0-3", "3-2", "a-b", "1-3-5", "-1-3", 7, "1-12"):
            with self.subTest(lines=lines):
                e = entry("docs/a.md", lines)
                if lines is None:
                    del e["lines"]
                rc, out = self.run_loader(entries=[e])
                self.assertFailed(rc, out, f"context.{KIND}.docs/a.md", "range")

    def test_range_over_2000_lines(self):
        self.put("docs/long.md", lines_file(2100))
        rc, out = self.run_loader(entries=[entry("docs/long.md", "1-2001")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/long.md", "over 2000 lines")

    def test_range_within_2000_lines_but_over_the_byte_cap(self):
        self.put("docs/dense.md", lines_file(1500, width=60))      # 1500 lines of ~75 bytes: about 112 KB
        for rng in ("1-1500", "1-300"):
            rc, out = self.run_loader(entries=[entry("docs/dense.md", rng)])
            self.assertFailed(rc, out, f"context.{KIND}.docs/dense.md", "over 16000 bytes")
        rc, out = self.run_loader(entries=[entry("docs/dense.md", "1-200")])
        self.assertEqual(rc, 0, out)

    def test_byte_cap_is_per_range_not_per_file(self):
        self.put("docs/dense.md", lines_file(1500, width=60))
        rc, out = self.run_loader(entries=[entry("docs/dense.md", "1-200"), entry("docs/dense.md", "201-400")])
        self.assertEqual(rc, 0, out)

    def test_entry_with_a_ref_is_copied_and_the_copy_is_listed(self):
        gf.git(self.root, "checkout", "-q", "-b", "other")
        self.put("docs/theirs.md", "from the other branch\nline two\n")
        gf.commit_all(self.root, "other")
        gf.git(self.root, "checkout", "-q", "main")
        self.assertFalse(os.path.exists(os.path.join(self.root, "docs/theirs.md")))
        rc, out = self.run_loader(entries=[entry("docs/theirs.md", "1-2", "their handoff", ref="other")])
        self.assertEqual(rc, 0, out)
        m = re.fullmatch(r"(\.superpowers/context/refs/[^ ]+/docs/theirs\.md) lines 1-2 blob ([0-9a-f]{40}) -- their handoff\n", out)
        self.assertIsNotNone(m, out)
        copy = os.path.join(self.root, m.group(1))
        with open(copy, "rb") as f:
            data = f.read()
        self.assertEqual(data, gf.git(self.root, "show", "other:docs/theirs.md").encode() + b"\n")
        self.assertEqual(m.group(2), blob(data))
        self.assertEqual(self.expected()["entries"][0]["path"], m.group(1))

    def test_missing_ref(self):
        rc, out = self.run_loader(entries=[entry("README.md", "1-1", ref="no/such-branch")])
        self.assertFailed(rc, out, f"context.{KIND}.README.md", "missing ref")

    def test_ref_without_the_path(self):
        rc, out = self.run_loader(entries=[entry("docs/absent.md", "1-1", ref="HEAD")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/absent.md", "missing path at ref")

    def test_ref_and_path_that_could_inject_or_escape_are_refused(self):
        self.put("docs/a.md", lines_file(3))           # tracked: only the form check can refuse its unnormalised spellings
        form = "path is absolute, has '..' or a backslash, or is not normalised"
        for e, text in ((entry("README.md", "1-1", ref="--output=x"), "unsafe ref"), (entry("README.md", "1-1", ref=""), "unsafe ref"),
                        (entry("../outside.md", "1-1"), form), (entry("/etc/hosts", "1-1"), form),
                        (entry("docs/../README.md", "1-1"), form), (entry("../outside.md", "1-1", ref="HEAD"), form),
                        (entry("./docs/a.md", "1-1"), form), (entry("docs//a.md", "1-1"), form), (entry("docs/a.md/", "1-1"), form)):
            with self.subTest(e=e):
                rc, out = self.run_loader(entries=[e])
                self.assertFailed(rc, out, f"context.{KIND}.{e['path']}", text)
        with self.subTest("an empty path"):
            rc, out = self.run_loader(entries=[entry("", "1-1")])
            self.assertFailed(rc, out, f"context.{KIND}.entry", "path is missing")

    def test_output_over_16_kb(self):
        self.put("docs/a.md", lines_file(10))
        why = "w" * 400
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-1", why)] * 40)       # about 21 KB
        self.assertFailed(rc, out, f"context.{KIND}.output", "16384")
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-1", why)] * 20)        # about 10 KB
        self.assertEqual(rc, 0, out)

    def test_output_boundary_16383_passes_and_16384_fails(self):
        self.put("docs/a.md", lines_file(10))
        line = len(f"docs/a.md lines 1-1 blob {'0' * 40} -- \n")
        for size, want in ((16383, 0), (16384, 1)):
            with self.subTest(size=size):
                fixed = size - 3 * line                         # three entries; the last why takes the remainder
                whys = ["w" * 4000, "w" * 4000, "w" * (fixed - 8000)]
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-1", w) for w in whys])
                if want:
                    self.assertFailed(rc, out, f"context.{KIND}.output", "16384")
                else:
                    self.assertEqual(rc, 0, out)
                    self.assertEqual(len(out.encode()), size)

    def test_all_failures_are_listed_not_only_the_first(self):
        self.put("docs/a.md", lines_file(3))
        rc, out = self.run_loader(entries=[entry("docs/x.md", "1-1"), entry("docs/a.md", "1-9"), entry("docs/y.md", "1-1")])
        self.assertEqual(rc, 1)
        self.assertEqual(len(re.findall(r"(?m)^FAIL ", out)), 3, out)

    def test_malformed_lists_and_arguments_fail_closed(self):
        self.put("docs/a.md", lines_file(3))
        cases = [
            ("kind missing from the file", dict(kind="sync", raw=ctx([entry("docs/a.md", "1-1")])), "context.sync.list"),
            ("not yaml", dict(raw="lists: [\n"), f"context.{KIND}.list"),
            ("lists not a mapping", dict(raw="lists: 3\n"), f"context.{KIND}.list"),
            ("list not a sequence", dict(raw="lists: {stream-a-block: 3}\n"), f"context.{KIND}.list"),
            ("empty list", dict(raw="lists: {stream-a-block: []}\n"), f"context.{KIND}.list"),
            ("entry not a mapping", dict(raw="lists: {stream-a-block: [3]}\n"), f"context.{KIND}.entry"),
            ("unknown key", dict(entries=[dict(entry("docs/a.md", "1-1"), extra="x")]), f"context.{KIND}.docs/a.md"),
            ("empty why", dict(entries=[entry("docs/a.md", "1-1", "")]), f"context.{KIND}.docs/a.md"),
            ("blank why", dict(entries=[entry("docs/a.md", "1-1", "   ")]), f"context.{KIND}.docs/a.md"),
            ("path not a string", dict(entries=[entry(3, "1-1")]), f"context.{KIND}.entry"),
            ("no kind argument", dict(argv=[]), "context.input"),
            ("two kinds", dict(argv=[KIND, "sync"]), "context.input"),
            ("kind with a slash", dict(argv=["../x"]), "context.input"),
        ]
        for name, kw, fid in cases:
            with self.subTest(name):
                rc, out = self.run_loader(**kw)
                self.assertEqual(rc, 1, out)
                self.assertRegex(out, r"(?m)^FAIL " + re.escape(fid) + r"(:|$)")

    def test_missing_context_file(self):
        rc, out = self.run_loader(argv=[KIND])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, rf"(?m)^FAIL context\.{KIND}\.list: ")

    def test_no_session_id_fails_closed(self):
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-1")]))
        for env in ({}, {"CLAUDE_CODE_SESSION_ID": ""}, {"CLAUDE_CODE_SESSION_ID": "../../x"}):
            with self.subTest(env=env), mock.patch.dict(os.environ, env):
                if not env:
                    os.environ.pop("CLAUDE_CODE_SESSION_ID", None)
                rc, out = self.run_loader(argv=[KIND])
                self.assertEqual(rc, 1, out)
                self.assertRegex(out, rf"(?m)^FAIL context\.{KIND}\.session: ")
        self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED)))

    # -- the handoff's context_files ------------------------------------------------------------
    def test_handoff_context_files_are_appended_for_stream_a(self):
        self.put("docs/a.md", lines_file(5))
        self.put("docs/task.md", lines_file(9))
        self.put(A_HANDOFF, front([entry("docs/task.md", "2-4", "the task's own file")]))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
        self.assertEqual(rc, 0, out)
        got = [l.split(" ")[0] for l in out.splitlines()]
        self.assertEqual(got, ["docs/a.md", "docs/task.md"])
        self.assertEqual([e["path"] for e in self.expected()["entries"]], ["docs/a.md", "docs/task.md"])

    def test_stream_b_reads_the_prototype_handoff_not_the_other(self):
        self.put("docs/a.md", lines_file(5))
        self.put("docs/b-task.md", lines_file(5))
        self.put("docs/a-task.md", lines_file(5))
        self.put(B_HANDOFF, front([entry("docs/b-task.md", "1-5")]))
        self.put(A_HANDOFF, front([entry("docs/a-task.md", "1-5")]))
        rc, out = self.run_loader(kind="stream-b-block", entries=[entry("docs/a.md", "1-5")])
        self.assertEqual(rc, 0, out)
        self.assertEqual([l.split(" ")[0] for l in out.splitlines()], ["docs/a.md", "docs/b-task.md"])

    def test_handoff_without_context_files_adds_nothing(self):
        self.put("docs/a.md", lines_file(5))
        self.put(A_HANDOFF, front(None, extra="other: 1\n"))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(out.splitlines()), 1)

    def test_kinds_without_a_handoff_ignore_handoffs(self):
        self.put("docs/a.md", lines_file(5))
        self.put(A_HANDOFF, front([entry("docs/nope.md", "1-5")]))
        rc, out = self.run_loader(kind="sync", entries=[entry("docs/a.md", "1-5")])
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(out.splitlines()), 1)

    def test_handoff_entries_are_checked_like_any_entry(self):
        self.put("docs/a.md", lines_file(5))
        self.put(A_HANDOFF, front([entry("docs/gone.md", "1-5")]))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/gone.md", "missing path")

    def test_malformed_handoff_front_matter_fails(self):
        """Row H2: each reason whole; a front matter not at the very start of the file (fix round 7, F13d) is none."""
        self.put("docs/a.md", lines_file(5))
        cf = "context_files: [{path: docs/a.md, lines: 1-5, why: w}]\n"
        none = "handoff has no front matter"
        cases = (("---\nplan: [\n---\nbody\n", "front matter is not valid YAML: "),
                 ("---\ncontext_files: 3\n---\n", "context_files is not a list"),
                 ("---\ncontext_files: [3]\n---\n", "context_files holds an entry that is not a mapping"),
                 ("no front matter at all\n", none),
                 ("# title\n---\n" + cf + "---\nbody\n", none),
                 ("\n---\n" + cf + "---\nbody\n", none),
                 (" ---\n" + cf + "---\nbody\n", none),
                 ("body\n\n---\n" + cf + "---\n", none))
        for text, reason in cases:
            with self.subTest(text=text):
                self.put(A_HANDOFF, text)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
                self.assertAllNamed(rc, out, f"context.{KIND}.{A_HANDOFF}", f"FAIL context.{KIND}.{A_HANDOFF}: {reason}")
                self.assertEqual(len(out.splitlines()), 1, out)
        with self.subTest("the same front matter at the start loads"):
            self.put(A_HANDOFF, "---\n" + cf + "---\nbody\n")
            rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
            self.assertEqual(rc, 0, out)
            self.assertEqual(len(out.splitlines()), 2, out)

    # -- the path whitelist (D28): a regular file tracked at HEAD (at the ref), no symlink component, no control char
    def test_path_with_a_control_character_is_refused(self):
        for bad in ("docs/a\nfake.md", "docs/a\tb.md", "docs/a\x01.md", "docs/a\x7f.md", "docs/a\r.md"):
            with self.subTest(path=repr(bad)):
                self.put(bad, lines_file(3))                      # tracked, so only the printable check can refuse it
                rc, out = self.run_loader(entries=[entry(bad, "1-1")])
                self.assertEqual(rc, 1, out)
                self.assertEqual(len(out.splitlines()), 1, out)          # never an injected extra line
                self.assertRegex(out, r"(?m)^FAIL context\." + KIND + r"\.[^\n]*: path holds a non-printable character")
                self.assertIn(bad.encode("unicode_escape").decode(), out)

    def test_path_under_dot_git_is_refused(self):
        rc, out = self.run_loader(entries=[entry(".git/HEAD", "1-1")])
        self.assertFailed(rc, out, f"context.{KIND}..git/HEAD", "path is under .git/")

    def test_untracked_file_is_refused(self):
        self.put("docs/new.md", lines_file(3))
        rc, out = self.run_loader(entries=[entry("docs/new.md", "1-3")], commit=False)
        self.assertFailed(rc, out, f"context.{KIND}.docs/new.md", "not a regular file tracked at HEAD")
        with self.subTest("a symlink at HEAD, a regular file on disk (row P7, fix round 7 F13f)"):
            self.head_mode_fixture("docs/new.md", "120000", lines_file(3))
            rc, out = self.run_loader(entries=[entry("docs/new.md", "1-3")], commit=False)
            self.assertFailed(rc, out, f"context.{KIND}.docs/new.md", "not a regular file tracked at HEAD")

    def test_symlinked_file_and_symlinked_directory_component_are_refused(self):
        self.put("real/a.md", lines_file(3))
        os.symlink("real/a.md", os.path.join(self.root, "link.md"))
        os.symlink("real", os.path.join(self.root, "linkdir"))
        for path in ("link.md", "linkdir/a.md"):
            with self.subTest(path=path):
                rc, out = self.run_loader(entries=[entry(path, "1-3")])
                self.assertFailed(rc, out, f"context.{KIND}.{path}", "symlink")

    def test_directory_is_refused(self):
        self.put("docs/sub/a.md", lines_file(3))
        rc, out = self.run_loader(entries=[entry("docs/sub", "1-1")])
        self.assertEqual(rc, 1, out)
        self.assertRegex(out, rf"(?m)^FAIL context\.{KIND}\.docs/sub: missing path \(not a file under the root\)$")

    def test_file_at_a_ref_must_be_a_tracked_regular_file(self):
        gf.git(self.root, "checkout", "-q", "-b", "other")
        self.put("real/a.md", lines_file(3))
        os.symlink("real/a.md", os.path.join(self.root, "link.md"))
        gf.commit_all(self.root, "other")
        gf.git(self.root, "checkout", "-q", "main")
        for path, text in (("link.md", "symlink"), ("real", "not a regular file"), ("real/nope.md", "missing path at ref")):
            with self.subTest(path=path):
                rc, out = self.run_loader(entries=[entry(path, "1-1", ref="other")])
                self.assertFailed(rc, out, f"context.{KIND}.{path}", text)

    def test_handoff_context_files_go_through_the_same_whitelist(self):
        self.put("docs/a.md", lines_file(5))
        self.put("docs/new.md", lines_file(5))
        self.put(A_HANDOFF, front([entry("docs/new.md", "1-5")]))
        gf.commit_all(self.root, "handoff")
        os.unlink(os.path.join(self.root, "docs/new.md"))
        self.put("docs/new.md", lines_file(5))        # tracked, so fine
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
        self.assertEqual(rc, 0, out)
        self.put(A_HANDOFF, front([entry("docs/ghost.md", "1-5"), entry("docs/x\ny.md", "1-1")]))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
        self.assertEqual(rc, 1, out)
        self.assertEqual(len(out.splitlines()), 2, out)
        self.assertIn("docs/ghost.md", out)
        self.assertIn("non-printable character", out)

    NONPRINTABLE = ("\u0085", "\u2028", "\u2029", "\u202e", "\t", "\x9b", "\u200b", "\ufeff")

    def assertOneEscapedLine(self, rc, out, reason):
        self.assertEqual(rc, 1, out)
        self.assertEqual(len(out.splitlines()), 1, ascii(out))                  # no injected line
        self.assertRegex(out, rf"^FAIL context\.{KIND}\.[^\n]*: ")
        self.assertIn(reason, out)
        self.assertTrue(out.endswith("\n") and out[:-1].isprintable(), ascii(out))     # no raw non-printable character
        self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED, SID, KIND + ".json")))

    def test_any_nonprintable_character_in_path_why_ref_or_lines_is_refused(self):
        self.put("docs/a.md", lines_file(3))
        for ch in self.NONPRINTABLE:
            with self.subTest(char=ascii(ch), field="path"):
                bad = f"docs/a{ch}.md"
                self.put(bad, lines_file(3))                   # created and tracked: only the printable check can refuse it
                rc, out = self.run_loader(entries=[entry(bad, "1-3")])
                self.assertOneEscapedLine(rc, out, "path holds a non-printable character")
            with self.subTest(char=ascii(ch), field="why"):
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", f"ok{ch}bad")])
                self.assertOneEscapedLine(rc, out, "why holds a non-printable character")
            with self.subTest(char=ascii(ch), field="ref"):
                name = f"b{ch}x"
                try:
                    gf.git(self.root, "branch", name, "HEAD")      # a ref that exists, so only the ref check can refuse it
                except AssertionError:
                    pass                                           # git itself refuses this name (tab)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", ref=name)])
                self.assertOneEscapedLine(rc, out, "unsafe ref")
            with self.subTest(char=ascii(ch), field="lines"):
                rc, out = self.run_loader(entries=[entry("docs/a.md", f"1-3{ch}")])
                self.assertOneEscapedLine(rc, out, "missing range")

    def test_every_fail_line_is_escaped_and_one_line_at_every_level(self):
        ch = "\u202e"
        self.put("docs/a.md", lines_file(3))
        cases = {
            "invalid context.yml": dict(raw=f"lists: [\n  {ch}: x\n  {ch} bad: [\n"),
            "invalid context.yml, control char": dict(raw=f"lists: {{a: \"x{ch}\ty\"\n"),
            "lists not a mapping": dict(raw=f"lists: 3{ch}\n"),
            "kind missing": dict(kind="sync", raw=ctx([entry("docs/a.md", "1-1")])),
            "unknown key": dict(entries=[dict(entry("docs/a.md", "1-1"), **{f"k{ch}": 1})]),
        }
        for name, kw in cases.items():
            with self.subTest(name):
                rc, out = self.run_loader(**kw)
                self.assertEqual(rc, 1, out)
                self.assertGreaterEqual(len(out.splitlines()), 1)
                self.assertEqual(len(out.splitlines()), 1, ascii(out))
                self.assertRegex(out, r"^FAIL context\.[a-z-]+\.[^\n]*: ")
                self.assertTrue(out.endswith("\n") and out[:-1].isprintable(), ascii(out))
        with self.subTest("a run of spaces in a FAIL id is one space (one_line: escaped, then spaces collapsed)"):
            rc, out = self.run_loader(entries=[entry("docs/a  b.md", "1-1")])
            self.assertEqual(out, f"FAIL context.{KIND}.docs/a b.md: path holds a character outside [A-Za-z0-9._/-] and "
                                  "printable non-ASCII (whitespace included)\n")
        for name, front_text in (("handoff yaml error", f"---\nplan: [\n{ch}x: y\n---\nbody\n"),
                                 ("handoff yaml error, line separator", f"---\nplan: \"a\u2028b\u0085{ch}\n---\nbody\n")):
            with self.subTest(name):
                self.put(A_HANDOFF, front_text)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
                self.assertEqual(rc, 1, out)
                self.assertEqual(len(out.splitlines()), 1, ascii(out))
                self.assertTrue(out.endswith("\n") and out[:-1].isprintable(), ascii(out))

    def test_printable_non_ascii_path_still_loads(self):
        self.put("docs/\u00fcn\u00ef\u4e2d.md", lines_file(3))
        rc, out = self.run_loader(entries=[entry("docs/\u00fcn\u00ef\u4e2d.md", "1-3", "caf\u00e9")])
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(out.splitlines()), 1)

    def test_a_blank_or_folded_why_is_a_named_fail(self):
        """Row W1 (fix round 7, F13c): a whitespace-only why is empty; a folded or literal block ending in a newline is
        non-printable; a folded block with strip chomping (`>-`) is one printable line and loads."""
        self.put("docs/a.md", lines_file(3))
        gf.commit_all(self.root, "a")
        head = f"lists:\n  {KIND}:\n    - path: docs/a.md\n      lines: 1-3\n      why: "
        empty, unprintable = "why is missing or empty", "why holds a non-printable character"
        cases = {"spaces": ('"   "\n', empty), "one space": ('" "\n', empty), "a tab": ('"\\t"\n', empty),
                 "spaces and a tab": ('" \\t "\n', empty), "an ideographic space": ('"\\u3000"\n', empty),
                 "an empty folded block": (">\n", empty), "a folded block": (">\n        the a doc\n", unprintable),
                 "a literal block": ("|\n        one\n        two\n", unprintable)}
        for name, (value, reason) in cases.items():
            with self.subTest(name):
                rc, out = self.run_loader(raw=head + value, commit=False)
                self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md", f"FAIL context.{KIND}.docs/a.md: {reason}\n")
                self.assertEqual(len(out.splitlines()), 1, out)
        with self.subTest("a folded block with strip chomping loads"):
            rc, out = self.run_loader(raw=head + ">-\n        the a\n        doc\n", commit=False)
            self.assertEqual(rc, 0, out)
            self.assertTrue(out.endswith(" -- the a doc\n"), out)

    def test_why_with_a_control_character_is_refused(self):
        self.put("docs/a.md", lines_file(3))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", "ok\x01bad")])
        self.assertFailed(rc, out, f"context.{KIND}.docs/a.md", "non-printable character")

    def test_empty_file_is_refused(self):
        self.put("docs/empty.md", "")
        for lines in ("1-1", "1-2"):
            with self.subTest(lines=lines):
                rc, out = self.run_loader(entries=[entry("docs/empty.md", lines)])
                self.assertFailed(rc, out, f"context.{KIND}.docs/empty.md", "empty file")

    # -- malformed structure at any level is a named FAIL, never an exception (D28) ---------------------
    GOOD = "{path: docs/a.md, lines: 1-3, why: w}"

    def assertNamedOneLineFail(self, rc, out):
        self.assertEqual(rc, 1, out)
        self.assertEqual(len(out.splitlines()), 1, ascii(out))
        self.assertRegex(out, rf"^FAIL context\.{KIND}\.[^\n]*: ")
        self.assertNotIn("Traceback", out)
        self.assertTrue(out[:-1].isprintable(), ascii(out))

    def test_malformed_context_yml_structure_is_a_named_fail_at_every_level(self):
        self.put("docs/a.md", lines_file(3))
        k, g = KIND, self.GOOD
        raws = {
            "mixed str and int keys in an entry": f"lists: {{{k}: [{{path: docs/a.md, lines: 1-3, why: w, 1: x, b: y}}]}}\n",
            "null key in an entry": f"lists: {{{k}: [{{path: docs/a.md, lines: 1-3, why: w, ~: y, b: z}}]}}\n",
            "only an int key in an entry": f"lists: {{{k}: [{{path: docs/a.md, lines: 1-3, why: w, 1: x}}]}}\n",
            "bool key in an entry": f"lists: {{{k}: [{{path: docs/a.md, lines: 1-3, why: w, true: x}}]}}\n",
            "int key in lists": f"lists: {{1: [{g}], {k}: [{g}]}}\n",
            "null key in lists": f"lists: {{~: [{g}], {k}: [{g}]}}\n",
            "int top-level key": f"1: x\nlists: {{{k}: [{g}]}}\n",
            "null top-level key": f"~: x\nlists: {{{k}: [{g}]}}\n",
            "top level is a list": f"- lists\n",
            "top level is a scalar": "3\n",
            "top level is empty": "",
            "lists is null": "lists: ~\n",
            "lists is a list": f"lists: [{g}]\n",
            "kind list is null": f"lists: {{{k}: ~}}\n",
            "kind list is a mapping": f"lists: {{{k}: {g}}}\n",
            "kind list is a string": f"lists: {{{k}: docs/a.md}}\n",
            "entry is null": f"lists: {{{k}: [~]}}\n",
            "entry is a list": f"lists: {{{k}: [[docs/a.md, 1-3, w]]}}\n",
            "entry is a string": f"lists: {{{k}: [docs/a.md]}}\n",
            "entry is a bool": f"lists: {{{k}: [true]}}\n",
        }
        for field, bad in (("path", ("~", "3", "true", "[docs/a.md]", "{a: b}", "1.5")),
                           ("lines", ("~", "3", "true", "[1, 3]", "{a: b}", "1.5")),
                           ("why", ("~", "3", "true", "[w]", "{a: b}", "1.5")),
                           ("ref", ("~", "3", "true", "[HEAD]", "{a: b}", "1.5"))):
            for v in bad:
                e = {"path": "docs/a.md", "lines": "1-3", "why": "w"}
                body = ", ".join(f"{kk}: {vv}" for kk, vv in e.items() if kk != field)
                raws[f"{field} is {v}"] = f"lists: {{{k}: [{{{body}, {field}: {v}}}]}}\n"
        for name, raw in raws.items():
            with self.subTest(name):
                rc, out = self.run_loader(raw=raw)
                self.assertNamedOneLineFail(rc, out)
                self.assertNotIn("unexpected", out)              # a named check refused it, not the catch-all
                if name.endswith("key in an entry") or name.startswith("mixed"):
                    self.assertIn("unknown key", out)

    def test_malformed_handoff_context_files_structure_is_a_named_fail(self):
        self.put("docs/a.md", lines_file(3))
        body = "{path: docs/a.md, lines: 1-3, why: w"
        texts = {
            "front matter is a list": "---\n- a\n---\nbody\n",
            "front matter is a scalar": "---\n3\n---\nbody\n",
            "context_files is null": "---\ncontext_files: ~\n---\nbody\n",
            "context_files is a mapping": f"---\ncontext_files: {body}}}\n---\nbody\n",
            "context_files is a string": "---\ncontext_files: docs/a.md\n---\nbody\n",
            "entry is null": "---\ncontext_files: [~]\n---\nbody\n",
            "entry is a list": "---\ncontext_files: [[docs/a.md]]\n---\nbody\n",
            "entry mixed key types": f"---\ncontext_files: [{body}, 1: x, b: y}}]\n---\nbody\n",
            "entry null key": f"---\ncontext_files: [{body}, ~: x, b: y}}]\n---\nbody\n",
            "entry path null": "---\ncontext_files: [{path: ~, lines: 1-3, why: w}]\n---\nbody\n",
            "entry lines list": "---\ncontext_files: [{path: docs/a.md, lines: [1], why: w}]\n---\nbody\n",
            "entry why mapping": "---\ncontext_files: [{path: docs/a.md, lines: 1-3, why: {a: b}}]\n---\nbody\n",
            "entry ref int": "---\ncontext_files: [{path: docs/a.md, lines: 1-3, why: w, ref: 3}]\n---\nbody\n",
        }
        for name, text in texts.items():
            with self.subTest(name):
                self.put(A_HANDOFF, text)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
                self.assertNamedOneLineFail(rc, out)
                self.assertNotIn("unexpected", out)
                if "key" in name:
                    self.assertIn("unknown key", out)
                if name in ("entry is null", "entry is a list"):
                    self.assertRegex(out, rf"^FAIL context\.{KIND}\.{re.escape(A_HANDOFF)}: context_files holds an entry that is not a mapping$")

    # -- fail closed: any failure removes the session's earlier expected list for the kind ------------
    def test_every_kind_of_failure_removes_an_earlier_expected_list(self):
        self.put("docs/a.md", lines_file(3))
        g = self.GOOD
        good = [entry("docs/a.md", "1-3")]
        overflow = [entry("docs/a.md", "1-1", "w" * 400)] * 60

        def mock_exc():
            return mock.patch.object(load_context, "check_entry", side_effect=RuntimeError("boom"))

        failures = {
            "entry: missing path": dict(entries=[entry("docs/nope.md", "1-3")]),
            "entry: bad range": dict(entries=[entry("docs/a.md", "1-9")]),
            "list: invalid yaml": dict(raw="lists: [\n"),
            "list: lists not a mapping": dict(raw="lists: 3\n"),
            "list: lists null": dict(raw="lists: ~\n"),
            "list: kind missing": dict(raw=f"lists: {{sync: [{g}]}}\n"),
            "list: kind list empty": dict(raw=f"lists: {{{KIND}: []}}\n"),
            "list: kind list not a list": dict(raw=f"lists: {{{KIND}: 3}}\n"),
            "list: mixed key types": dict(raw=f"lists: {{{KIND}: [{{path: docs/a.md, lines: 1-3, why: w, 1: x, b: y}}]}}\n"),
            "list: context.yml missing": dict(raw=None),
            "output over the cap": dict(entries=overflow),
        }
        for name, kw in failures.items():
            with self.subTest(name):
                rc, out = self.run_loader(entries=good)
                self.assertEqual(rc, 0, out)
                self.assertTrue(os.path.exists(os.path.join(self.root, EXPECTED, SID, KIND + ".json")), name)
                if name == "list: context.yml missing":
                    os.unlink(os.path.join(self.root, CTX))
                    gf.commit_all(self.root, "drop")
                    rc, out = self.run_loader(argv=[KIND])
                else:
                    rc, out = self.run_loader(**kw)
                self.assertEqual(rc, 1, out)
                self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED, SID, KIND + ".json")), name)
        with self.subTest("handoff failure"):
            rc, out = self.run_loader(entries=good)
            self.assertEqual(rc, 0, out)
            self.put(A_HANDOFF, "no front matter\n")
            rc, out = self.run_loader(entries=good)
            self.assertEqual(rc, 1, out)
            self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED, SID, KIND + ".json")))
        with self.subTest("an exception"):
            self.put(A_HANDOFF, front(None))
            rc, out = self.run_loader(entries=good)
            self.assertEqual(rc, 0, out)
            with mock_exc():
                rc, out = self.run_loader(entries=good)
            self.assertNamedOneLineFail(rc, out)
            self.assertIn("RuntimeError", out)
            self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED, SID, KIND + ".json")))

    def test_a_failure_leaves_other_kinds_and_other_sessions_alone(self):
        self.put("docs/a.md", lines_file(3))
        both = yaml.safe_dump({"lists": {KIND: [entry("docs/a.md", "1-3")], "sync": [entry("docs/a.md", "1-3")]}})
        rc, out = self.run_loader(raw=both)
        self.assertEqual(rc, 0, out)
        rc, out = self.run_loader(kind="sync", argv=["sync"])
        self.assertEqual(rc, 0, out)
        other = os.path.join(self.root, EXPECTED, "1b6f1c52-7d1e-4f0a-9a57-3c2d8e41aa11", KIND + ".json")
        os.makedirs(os.path.dirname(other))
        with open(other, "w") as fh:
            fh.write("{}\n")
        rc, out = self.run_loader(raw="lists: ~\n")
        self.assertEqual(rc, 1, out)
        self.assertTrue(os.path.exists(os.path.join(self.root, EXPECTED, SID, "sync.json")))
        self.assertTrue(os.path.exists(other))

    # -- fix round 4: the input-domain table docs/superpowers/state/program-tooling/T1-input-domain.md -----------
    def call(self, argv, env=None, cwd=None):
        """main(argv) as is (no --root added); (rc, stdout, stderr)."""
        out, err = io.StringIO(), io.StringIO()
        old = os.getcwd()
        with mock.patch.dict(os.environ, env or {}), contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            try:
                if cwd:
                    os.chdir(cwd)
                rc = load_context.main(argv)
            finally:
                os.chdir(old)
        return rc, out.getvalue(), err.getvalue()

    def assertAllNamed(self, rc, out, fid=None, text=""):
        """rc 1, every line a printable FAIL line, never the catch-all, total under 16,384 bytes."""
        self.assertEqual(rc, 1, out)
        self.assertLess(len(out.encode()), 16384)
        for line in out.splitlines():
            self.assertRegex(line, r"^FAIL context\.[^\n]*: ", ascii(out))
            self.assertTrue(line.isprintable(), ascii(line))
        self.assertNotIn("internal", out)
        if fid is not None:
            self.assertRegex(out, r"(?m)^FAIL " + re.escape(fid) + r": ")
        self.assertIn(text, out)

    @contextlib.contextmanager
    def deadline(self, seconds):
        """Fail the test (not hang it) when the body runs over `seconds` (row C8: bounded work)."""
        import signal

        def over(*_):
            raise AssertionError(f"over the {seconds} s deadline")
        old = signal.signal(signal.SIGALRM, over)
        signal.setitimer(signal.ITIMER_REAL, seconds)
        try:
            yield
        finally:
            signal.setitimer(signal.ITIMER_REAL, 0)
            signal.signal(signal.SIGALRM, old)

    def outside(self):
        d = gf.new_repo()
        self.addCleanup(gf.remove, d)
        return d

    def unblock_fifo_later(self, path, after=3.0):
        """A daemon thread opens `path` for writing after `after` s if a reader blocks on it, so a loader that opens a FIFO cannot hang the suite."""
        import threading
        import time

        def run():
            time.sleep(after)
            try:
                os.close(os.open(path, os.O_WRONLY | os.O_NONBLOCK))
            except OSError:
                pass
        threading.Thread(target=run, daemon=True).start()

    LIST = os.path.join(EXPECTED, SID, KIND + ".json")

    def test_fail_output_values_are_clipped_and_the_output_capped_below_16_kb(self):
        self.put("docs/a.md", lines_file(3))
        for depth in (4, 5, 9):                  # 9: 10**9 leaves; the echo's work is bounded, never the expanded size
            with self.subTest(alias_chain=depth), self.deadline(20):
                rows = ["x0: &a0 [" + ", ".join(["xxxxxxxxxx"] * 10) + "]"]
                rows += [f"x{i}: &a{i} [" + ", ".join([f"*a{i - 1}"] * 10) + "]" for i in range(1, depth + 1)]
                rc, out = self.run_loader(raw="\n".join(rows) + f"\nlists: {{{KIND}: [*a{depth}, *a{depth}]}}\n")
                self.assertAllNamed(rc, out, f"context.{KIND}.entry", "entry is not a mapping")
                self.assertEqual(len(out.splitlines()), 2, out)
                for line in out.splitlines():
                    self.assertLessEqual(len(line.split(": ", 1)[1]), len("entry is not a mapping: ") + 200)
        with self.subTest("a 5,000-character path, ref and lines"):
            long = "docs/" + "p" * 5000 + ".md"
            rc, out = self.run_loader(entries=[entry(long, "1-1"), entry("docs/a.md", "1-1", ref="-" + "r" * 5000),
                                               entry("docs/a.md", "1-" + "9" * 5000 + "x")])
            self.assertAllNamed(rc, out)
            self.assertEqual(len(out.splitlines()), 3, out)
            ident = out.splitlines()[0][len(f"FAIL context.{KIND}."):].split(": ")[0]
            self.assertEqual(len(ident), 200)
            self.assertTrue(all(len(l) < 200 + 200 + 120 for l in out.splitlines()), out)
        with self.subTest("a 100,000-character kind"):
            rc, out, _ = self.call(["--root", self.root, "k" * 100000])
            self.assertAllNamed(rc, out)
            self.assertLess(len(out), 600, out)
        with self.subTest("300 missing paths"):
            rc, out = self.run_loader(entries=[entry(f"docs/missing-{i:03d}-" + "q" * 80 + ".md", "1-1") for i in range(300)])
            self.assertAllNamed(rc, out)
            m = re.search(rf"(?m)^FAIL context\.{KIND}\.output: (\d+) further failures not shown\n\Z", out)
            self.assertIsNotNone(m, out[-300:])

    def test_every_failure_is_counted_when_the_output_is_capped(self):
        rc, out = self.run_loader(entries=[entry(f"docs/gone-{i:04d}.md", "1-1") for i in range(1000)])
        self.assertAllNamed(rc, out)
        lines = out.splitlines()
        m = re.fullmatch(rf"FAIL context\.{KIND}\.output: (\d+) further failures not shown", lines[-1])
        self.assertIsNotNone(m, lines[-1])
        self.assertEqual(len(lines) - 1 + int(m.group(1)), 1000)
        self.assertEqual([l.split(": ")[0] for l in lines[:-1]], [f"FAIL context.{KIND}.docs/gone-{i:04d}.md" for i in range(len(lines) - 1)])
        self.assertGreater(len(out.encode()) + len(lines[0]) + 1, 16384 - len(lines[-1]) - 1)     # filled: the next line would cross

    def reset_context_dir(self):
        d = os.path.join(self.root, ".superpowers")
        for base, dirs, _ in os.walk(d):
            for x in dirs:
                if not os.path.islink(os.path.join(base, x)):
                    os.chmod(os.path.join(base, x), 0o755)
        if os.path.islink(d) or os.path.isfile(d):
            os.unlink(d)
        else:
            gf.remove(d)

    @unittest.skipIf(os.geteuid() == 0, "root ignores permissions")
    def test_expected_list_write_errors_are_named_fails(self):
        self.put("docs/a.md", lines_file(3))
        good = [entry("docs/a.md", "1-3")]
        far = self.outside()
        exp, sid_dir = os.path.join(self.root, EXPECTED), os.path.join(self.root, EXPECTED, SID)
        target = os.path.join(far, "victim.json")

        def setup(case):
            if case == "list is a directory":
                os.makedirs(os.path.join(sid_dir, KIND + ".json"))
            elif case == "list is a symlink":
                os.makedirs(sid_dir)
                with open(target, "w") as fh:
                    fh.write("victim\n")
                os.symlink(target, os.path.join(sid_dir, KIND + ".json"))
            elif case == "expected is a regular file":
                os.makedirs(os.path.dirname(exp))
                open(exp, "w").close()
            elif case == "session directory is a symlink":
                os.makedirs(exp)
                os.symlink(far, sid_dir)
            elif case == ".superpowers is a symlink":
                os.symlink(far, os.path.join(self.root, ".superpowers"))
            elif case == "session directory unwritable":
                os.makedirs(sid_dir)
                os.chmod(sid_dir, 0o555)
        for case in ("list is a directory", "list is a symlink", "expected is a regular file",
                     "session directory is a symlink", ".superpowers is a symlink", "session directory unwritable"):
            with self.subTest(case):
                self.reset_context_dir()
                setup(case)
                rc, out = self.run_loader(entries=good)
                self.assertAllNamed(rc, out, f"context.{KIND}.{self.LIST}")
                self.assertFalse(os.path.isfile(os.path.join(far, KIND + ".json")), "written through a symlink")
                if case == "list is a symlink":
                    with open(target) as fh:
                        self.assertEqual(fh.read(), "victim\n")
                self.reset_context_dir()

    @unittest.skipIf(os.geteuid() == 0, "root ignores permissions")
    def test_expected_list_removal_errors_are_named_fails(self):
        self.put("docs/a.md", lines_file(3))
        good, bad = [entry("docs/a.md", "1-3")], [entry("docs/nope.md", "1-3")]
        listed = os.path.join(self.root, self.LIST)
        sid_dir = os.path.dirname(listed)
        self.addCleanup(lambda: os.path.isdir(sid_dir) and os.chmod(sid_dir, 0o755))
        for file_mode, text in ((0o644, "emptied"), (0o444, "still stands")):
            with self.subTest(file_mode=oct(file_mode)):
                self.reset_context_dir()
                rc, out = self.run_loader(entries=good)
                self.assertEqual(rc, 0, out)
                os.chmod(listed, file_mode)
                os.chmod(sid_dir, 0o555)
                rc, out = self.run_loader(entries=bad)
                os.chmod(sid_dir, 0o755)
                self.assertAllNamed(rc, out, f"context.{KIND}.{self.LIST}", text)
                self.assertIn("docs/nope.md", out)
                with open(listed, "rb") as fh:
                    self.assertEqual(fh.read() == b"", text == "emptied")
        with self.subTest("the list is a directory"):
            self.reset_context_dir()
            os.makedirs(listed)
            rc, out = self.run_loader(entries=bad)
            self.assertAllNamed(rc, out, f"context.{KIND}.{self.LIST}", "could not be removed")
        with self.subTest("a symlinked session directory"):
            self.reset_context_dir()
            far = self.outside()
            with open(os.path.join(far, KIND + ".json"), "w") as fh:
                fh.write("{}\n")
            os.makedirs(os.path.dirname(sid_dir))
            os.symlink(far, sid_dir)
            rc, out = self.run_loader(entries=bad)
            self.assertAllNamed(rc, out, f"context.{KIND}.{self.LIST}", "symlink")
            self.assertTrue(os.path.isfile(os.path.join(far, KIND + ".json")), "removed through a symlink")
        self.reset_context_dir()

    @unittest.skipIf(os.geteuid() == 0, "root ignores permissions")
    def test_ref_copy_write_target_errors_are_named_fails(self):
        gf.git(self.root, "checkout", "-q", "-b", "other")
        self.put("docs/theirs.md", "from the other branch\n")
        gf.commit_all(self.root, "other")
        gf.git(self.root, "checkout", "-q", "main")
        e = [entry("docs/theirs.md", "1-1", ref="other")]
        copy = os.path.join(self.root, load_context.ref_copy_path("other", "docs/theirs.md"))
        refs = os.path.join(self.root, ".superpowers", "context", "refs")
        far = self.outside()
        victim = os.path.join(far, "victim.md")

        def setup(case):
            if case == "copy is a symlink":
                os.makedirs(os.path.dirname(copy))
                with open(victim, "w") as fh:
                    fh.write("victim\n")
                os.symlink(victim, copy)
            elif case == "copy is a directory":
                os.makedirs(copy)
            elif case == "refs is a regular file":
                os.makedirs(os.path.dirname(refs))
                open(refs, "w").close()
            elif case == ".superpowers is a symlink":
                os.symlink(far, os.path.join(self.root, ".superpowers"))
            elif case == "copy directory unwritable":
                os.makedirs(os.path.dirname(copy))
                os.chmod(os.path.dirname(copy), 0o555)
        for case in ("copy is a symlink", "copy is a directory", "refs is a regular file", ".superpowers is a symlink",
                     "copy directory unwritable"):
            with self.subTest(case):
                self.reset_context_dir()
                setup(case)
                rc, out = self.run_loader(entries=e)
                self.assertAllNamed(rc, out, f"context.{KIND}.docs/theirs.md")
                self.assertFalse(os.path.exists(os.path.join(far, "context")), "written through a symlink")
                if case == "copy is a symlink":
                    with open(victim) as fh:
                        self.assertEqual(fh.read(), "victim\n")
                self.reset_context_dir()

    def test_context_yml_of_the_wrong_kind_is_a_named_fail(self):
        self.put("docs/a.md", lines_file(3))
        gf.commit_all(self.root, "a")
        text = ctx([entry("docs/a.md", "1-3")])
        full = os.path.join(self.root, CTX)
        prog = os.path.dirname(full)
        cases = ("a directory", "a FIFO", "a symlink", "a symlinked directory component", "not UTF-8")
        if os.geteuid() != 0:
            cases += ("mode 000",)
        for case in cases:
            with self.subTest(case):
                gf.remove(prog) if not os.path.islink(prog) else os.unlink(prog)
                os.makedirs(prog)
                if case == "a directory":
                    os.makedirs(full)
                elif case == "a FIFO":
                    os.mkfifo(full)
                    self.unblock_fifo_later(full)
                elif case == "a symlink":
                    self.put("docs/real.yml", text)
                    os.symlink(os.path.join(self.root, "docs/real.yml"), full)
                elif case == "a symlinked directory component":
                    os.rmdir(prog)
                    self.put("docs/real-prog/context.yml", text)
                    os.symlink(os.path.join(self.root, "docs/real-prog"), prog)
                elif case == "not UTF-8":
                    with open(full, "wb") as fh:
                        fh.write(b"lists: \xff\xfe\n")
                elif case == "mode 000":
                    self.put(CTX, text)
                    os.chmod(full, 0)
                    self.addCleanup(lambda: os.path.exists(full) and os.chmod(full, 0o644))
                rc, out = self.run_loader(argv=[KIND], commit=False)
                self.assertAllNamed(rc, out, f"context.{KIND}.list", CTX)
                if case in ("a directory", "a FIFO"):
                    self.assertIn("not a regular file", out)          # refused before any open: a FIFO never blocks
                if os.path.exists(full) and not os.path.islink(full) and os.path.isfile(full):
                    os.chmod(full, 0o644)
        with self.subTest("untracked context.yml loads from the working tree"):
            gf.remove(prog) if not os.path.islink(prog) else os.unlink(prog)
            gf.commit_all(self.root, "no context")
            self.put(CTX, text)
            rc, out = self.run_loader(argv=[KIND], commit=False)
            self.assertEqual(rc, 0, out)

    def test_handoff_of_the_wrong_kind_or_grammar_is_a_named_fail(self):
        """Row H1/H2: the handoff is the only failing input (docs/a.md committed, fix round 7 F13b), each reason whole."""
        self.put("docs/a.md", lines_file(3))
        gf.commit_all(self.root, "a")
        good = front([entry("docs/a.md", "1-3")])
        full = os.path.join(self.root, A_HANDOFF)
        reasons = {"missing": "handoff missing or unreadable (No such file or directory)",
                   "a directory": "handoff not a regular file", "a FIFO": "handoff not a regular file",
                   "a symlink": f"handoff symlink in path ({A_HANDOFF})", "not UTF-8": "handoff is not valid UTF-8",
                   "duplicate context_files": "front matter is not valid YAML: ", "merge key": "front matter is not valid YAML: ",
                   "mode 000": "handoff unreadable (Permission denied)"}
        cases = ("missing", "a directory", "a FIFO", "a symlink", "not UTF-8", "duplicate context_files", "merge key")
        if os.geteuid() != 0:
            cases += ("mode 000",)
        for case in cases:
            with self.subTest(case):
                if os.path.isdir(full) and not os.path.islink(full):
                    os.rmdir(full)
                elif os.path.lexists(full):
                    os.unlink(full)
                if case == "a directory":
                    os.makedirs(full)
                elif case == "a FIFO":
                    os.mkfifo(full)
                    self.unblock_fifo_later(full)
                elif case == "a symlink":
                    self.put("docs/real-handoff.md", good)
                    os.symlink(os.path.join(self.root, "docs/real-handoff.md"), full)
                elif case == "not UTF-8":
                    with open(full, "wb") as fh:
                        fh.write(b"---\nplan: \xff\n---\n")
                elif case == "duplicate context_files":
                    self.put(A_HANDOFF, "---\ncontext_files: []\ncontext_files: [{path: docs/a.md, lines: 1-3, why: w}]\n---\n")
                elif case == "merge key":
                    self.put(A_HANDOFF, "---\nbase: &b {context_files: []}\n<<: *b\n---\n")
                elif case == "mode 000":
                    self.put(A_HANDOFF, good)
                    os.chmod(full, 0)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")], commit=False)
                if os.path.isfile(full) and not os.path.islink(full):
                    os.chmod(full, 0o644)
                self.assertAllNamed(rc, out, f"context.{KIND}.{A_HANDOFF}", f"FAIL context.{KIND}.{A_HANDOFF}: {reasons[case]}")
                self.assertEqual(len(out.splitlines()), 1, out)          # the handoff, and nothing else, fails

    def test_unreadable_entry_file_is_a_named_fail(self):
        self.put("docs/a.md", lines_file(3))
        self.put("docs/pipe.md", lines_file(3))
        gf.commit_all(self.root, "files")
        os.unlink(os.path.join(self.root, "docs/pipe.md"))
        os.mkfifo(os.path.join(self.root, "docs/pipe.md"))
        self.unblock_fifo_later(os.path.join(self.root, "docs/pipe.md"))
        rc, out = self.run_loader(entries=[entry("docs/pipe.md", "1-1")], commit=False)
        self.assertAllNamed(rc, out, f"context.{KIND}.docs/pipe.md", "missing path (not a file under the root)")      # never opened
        if os.geteuid() != 0:
            full = os.path.join(self.root, "docs/a.md")
            os.chmod(full, 0)
            self.addCleanup(os.chmod, full, 0o644)
            rc, out = self.run_loader(entries=[entry("docs/a.md", "1-1")], commit=False)
            self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md", "unreadable")

    def test_root_must_be_a_work_tree_top_and_not_a_symlink(self):
        self.put("docs/a.md", lines_file(3))
        self.put("docs/sub/" + CTX, ctx([entry("docs/a.md", "1-3")]))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "tree")
        far = self.outside()
        link = os.path.join(far, "link")
        os.symlink(self.root, link)
        plain = tempfile.mkdtemp(prefix="not-a-repo-")         # outside any git work tree
        self.addCleanup(gf.remove, plain)
        os.makedirs(os.path.join(plain, os.path.dirname(CTX)))
        with open(os.path.join(plain, CTX), "w") as fh:
            fh.write(ctx([entry("docs/a.md", "1-3")]))
        cases = {"missing": os.path.join(far, "nope"), "a regular file": os.path.join(self.root, "README.md"),
                 "a symlink": link, "a subdirectory": os.path.join(self.root, "docs/sub"), "not a git work tree": plain}
        for name, root in cases.items():
            kind_text = "is not a directory" if name in ("missing", "a regular file") else ""
            with self.subTest(root=name):
                rc, out, _ = self.call(["--root", root, KIND])
                self.assertAllNamed(rc, out, "context.input", kind_text)
            with self.subTest(env=name):
                rc, out, _ = self.call([KIND], env={"CLAUDE_PROJECT_DIR": root})
                self.assertAllNamed(rc, out, "context.input", kind_text)
        with self.subTest("relative root with a trailing slash"):
            rc, out, _ = self.call(["--root", os.path.basename(self.root) + "/", KIND], cwd=os.path.dirname(self.root))
            self.assertEqual(rc, 0, out)
        # Row R4 (fix round 7, F13a): the root is normalised by abspath before the symlink check, so a symlinked root
        # given with a trailing slash (os.path.islink("link/") follows the link) or relative is still the named symlink
        # FAIL, and a relative bad root is named by its absolute path.
        in_far = os.path.join(os.path.realpath(far), "link")         # os.getcwd() in `far` is its real path
        for name, argv, env, cwd, named in (("relative link/", ["--root", "link/", KIND], None, far, in_far),
                                            ("relative link", ["--root", "link", KIND], None, far, in_far),
                                            ("absolute link/", ["--root", link + "/", KIND], None, None, link),
                                            ("CLAUDE_PROJECT_DIR link/", [KIND], {"CLAUDE_PROJECT_DIR": link + "/"}, None, link),
                                            ("CLAUDE_PROJECT_DIR relative link/", [KIND], {"CLAUDE_PROJECT_DIR": "link/"}, far, in_far)):
            with self.subTest(name):
                rc, out, _ = self.call(argv, env=env, cwd=cwd)
                self.assertAllNamed(rc, out, "context.input")
                self.assertEqual(out, f"FAIL context.input: root {named} is a symlink\n")
        with self.subTest("a relative missing root is named by its absolute path"):
            rc, out, _ = self.call(["--root", "nope", KIND], cwd=far)
            self.assertEqual(out, f"FAIL context.input: root {os.path.join(os.path.realpath(far), 'nope')} is not a directory\n")
        with self.subTest("no root outside any repository"):
            os.environ.pop("CLAUDE_PROJECT_DIR", None)
            rc, out, _ = self.call([KIND], cwd=plain)
            self.assertAllNamed(rc, out, "context.input")

    def test_git_environment_and_config_cannot_change_what_is_read(self):
        self.put("docs/a.md", lines_file(3))
        self.put("docs/:(glob)*.md", lines_file(3))
        self.put("docs/été.md", lines_file(3))
        other = self.outside()
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
        self.assertEqual(rc, 0, out)
        for env in ({"GIT_DIR": os.path.join(other, ".git")}, {"GIT_WORK_TREE": other}, {"GIT_LITERAL_PATHSPECS": "0"},
                    {"GIT_CONFIG_COUNT": "1", "GIT_CONFIG_KEY_0": "core.precomposeunicode", "GIT_CONFIG_VALUE_0": "false"}):
            with self.subTest(env=env):
                rc, out, _ = self.call(["--root", self.root, KIND], env=env)      # the fixture is committed: no git call of the test under env
                self.assertEqual(rc, 0, out)
        with self.subTest("pathspec magic in a tracked name is literal"):      # restored (DEC-0103); DEC-0170 refuses it by name
            rc, out = self.run_loader(entries=[entry("docs/:(glob)*.md", "1-3")])
            self.assertAllNamed(rc, out, f"context.{KIND}.docs/:(glob)*.md", "path holds a character outside [A-Za-z0-9._/-]")
        with self.subTest("repository config and attributes"):
            with open(os.path.join(self.root, ".git", "config"), "a") as fh:
                fh.write('[core]\n\tquotePath = true\n[color]\n\tui = always\n[diff "up"]\n\ttextconv = tr a-z A-Z\n')
            self.put(".gitattributes", "*.md diff=up\n")
            gf.commit_all(self.root, "attrs")
            rc, out = self.run_loader(entries=[entry("docs/été.md", "1-3"), entry("docs/a.md", "1-3", ref="HEAD")])
            self.assertEqual(rc, 0, out)
            copy = out.splitlines()[1].split(" ")[0]
            with open(os.path.join(self.root, copy), "rb") as fh:
                self.assertEqual(fh.read(), lines_file(3).encode())
        with self.subTest("git not on PATH"), mock.patch.dict(os.environ, {"PATH": os.path.join(other, "no-bin")}):
            rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")], commit=False)
            self.assertEqual(rc, 1, out)
            self.assertEqual(len(out.splitlines()), 1, out)
            self.assertRegex(out, rf"^FAIL context\.(input|{KIND}\.[^\n]*): ")

    def test_path_matches_the_tracked_name_as_a_whole_string(self):
        self.put("docs/a.md", lines_file(3))
        self.put("docs/é.md", lines_file(3))           # NFC
        for path in ("docs/A.md", "docs/é.md"):           # case variant; NFD spelling
            with self.subTest(path=ascii(path)):
                rc, out = self.run_loader(entries=[entry(path, "1-3")])
                self.assertAllNamed(rc, out)
                self.assertRegex(out, r"(missing path|tracked at HEAD)")

    def test_yaml_tags_documents_depth_and_numbers_are_named_fails(self):
        self.put("docs/a.md", lines_file(3))
        k = KIND
        raws = {
            "python tag": f"lists: {{{k}: [!!python/object/apply:os.system [true]]}}\n",
            "unknown local tag": f"lists: {{{k}: [!foo x]}}\n",
            "python tag building a valid entry": f"lists: {{{k}: [!!python/object/apply:builtins.dict [[[path, docs/a.md], [lines, 1-3], [why, w]]]]}}\n",
            "two documents": f"lists: {{{k}: [{self.GOOD}]}}\n---\nlists: {{}}\n",
            "deep nesting": "lists: " + "[" * 5000 + "]" * 5000 + "\n",
            "deep nesting in an entry": f"lists: {{{k}: [" + "[" * 5000 + "]" * 5000 + "]}\n",
            "5000-digit lines": f"lists: {{{k}: [{{path: docs/a.md, lines: {'7' * 5000}, why: w}}]}}\n",
            "binary path": f"lists: {{{k}: [{{path: !!binary ZG9jcy9hLm1k, lines: 1-3, why: w}}]}}\n",
            "set entry": f"lists: {{{k}: [!!set {{docs/a.md}}]}}\n",
            "timestamp lines": f"lists: {{{k}: [{{path: docs/a.md, lines: 2026-10-30, why: w}}]}}\n",
            "date key in an entry": f"lists: {{{k}: [{{path: docs/a.md, lines: 1-3, why: w, 2026-10-30: x}}]}}\n",
            "duplicate key": f"lists: {{{k}: [{self.GOOD}]}}\nlists: {{}}\n",
            "merge key": f"base: &b {{path: docs/a.md}}\nlists: {{{k}: [{{<<: *b, lines: 1-3, why: w}}]}}\n",
        }
        for name, raw in raws.items():
            with self.subTest(name):
                rc, out = self.run_loader(raw=raw)
                self.assertAllNamed(rc, out)
                self.assertEqual(len(out.splitlines()), 1, out)

    def test_arguments_and_session_id_match_as_whole_strings(self):
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        for argv in ([KIND + "\n"], ["Stream-a-block"], ["-x"], ["--bogus=" + "x" * 100000, KIND], ["-h"]):
            with self.subTest(argv=ascii(argv)[:40]):
                rc, out, err = self.call(["--root", self.root, *argv])
                self.assertAllNamed(rc, out, "context.input")
                self.assertLess(len(out.encode()) + len(err.encode()), 16384)
        with self.subTest("session id with a trailing line terminator"):
            rc, out, _ = self.call(["--root", self.root, KIND], env={"CLAUDE_CODE_SESSION_ID": SID + "\n"})
            self.assertAllNamed(rc, out, f"context.{KIND}.session")

    def test_the_session_id_is_a_lowercase_uuid(self):
        """Row E1 (fix round 7, DEC-0188 uuid): CLAUDE_CODE_SESSION_ID must fullmatch the hook's session id form, a
        lowercase UUID 8-4-4-4-12; anything else is FAIL context.<kind>.session and no expected list is written."""
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        bad = {"upper case": SID.upper(), "no hyphens": SID.replace("-", ""), "one short": SID[:-1], "one long": SID + "0",
               "braces": "{" + SID + "}", "urn prefix": "urn:uuid:" + SID, "a non-hex letter": "g" + SID[1:],
               "hyphens moved": SID[:9] + "-" + SID[9:13] + SID[14:], "a leading space": " " + SID,
               "a trailing newline": SID + "\n", "a fullwidth digit": "\uff10" + SID[1:],
               "the old 8-64 form, 8": "abcdefgh", "the old 8-64 form, 64": "a" * 64, "the old form, word": "r6probe-session-0002",
               "a path": "../../x", "empty": ""}
        for name, sid in bad.items():
            with self.subTest(name):
                rc, out, _ = self.call(["--root", self.root, KIND], env={"CLAUDE_CODE_SESSION_ID": sid})
                self.assertAllNamed(rc, out, f"context.{KIND}.session", f"FAIL context.{KIND}.session: CLAUDE_CODE_SESSION_ID "
                                    "is unset or not a session id (a lowercase UUID): the expected list has no key\n")
                self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED)), name)
        for name, sid in (("the fixture id", SID), ("all zeros", "00000000-0000-0000-0000-000000000000"),
                          ("all f", "ffffffff-ffff-ffff-ffff-ffffffffffff")):
            with self.subTest(name):
                rc, out, _ = self.call(["--root", self.root, KIND], env={"CLAUDE_CODE_SESSION_ID": sid})
                self.assertEqual(rc, 0, out)
                self.assertTrue(os.path.isfile(os.path.join(self.root, EXPECTED, sid, KIND + ".json")), name)

    def test_ref_that_is_not_a_commit_and_unborn_head(self):
        self.put("docs/a.md", lines_file(3))
        gf.commit_all(self.root, "a")
        blob_sha = gf.git(self.root, "rev-parse", "HEAD:docs/a.md")
        tree_sha = gf.git(self.root, "rev-parse", "HEAD^{tree}")
        gf.git(self.root, "tag", "-a", "-m", "t", "annotated", "HEAD")
        for ref in (blob_sha, tree_sha):
            with self.subTest(ref=ref):
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", ref=ref)])
                self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md", "missing ref")
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", ref="annotated")])
        self.assertEqual(rc, 0, out)
        unborn = self.outside()
        gf.remove(os.path.join(unborn, ".git"))
        gf.git(unborn, "init", "-q")
        gf.write(unborn, "docs/a.md", lines_file(3))
        gf.write(unborn, CTX, ctx([entry("docs/a.md", "1-3")]))
        rc, out, _ = self.call(["--root", unborn, KIND])
        self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md", "tracked at HEAD")

    def test_stdout_is_utf8_whatever_the_locale(self):
        import subprocess
        import sys
        name = "docs/über.md"
        self.put(name, lines_file(3))
        self.put(CTX, ctx([entry(name, "1-3", "café")]))
        gf.commit_all(self.root, "ctx")
        env = {**os.environ, "PYTHONIOENCODING": "ascii", "LC_ALL": "C", "LANG": "C", "PYTHONDONTWRITEBYTECODE": "1"}
        p = subprocess.run([sys.executable, load_context.__file__, "--root", self.root, KIND],
                           capture_output=True, env=env, check=False)
        self.assertEqual(p.returncode, 0, p.stdout + p.stderr)
        self.assertEqual(p.stdout.decode("utf-8"), f"{name} lines 1-3 blob {blob(lines_file(3).encode())} -- café\n")

    def test_yaml_aliases_and_anchors_load_like_written_out_entries(self):
        self.put("docs/a.md", lines_file(3))
        rc, out = self.run_loader(raw=f"w: &w the why\nlists: {{{KIND}: [&e {{path: docs/a.md, lines: 1-3, why: *w}}, *e]}}\n")
        self.assertEqual(rc, 0, out)
        self.assertEqual(len(out.splitlines()), 2, out)
        self.assertTrue(out.splitlines()[0].endswith(" -- the why"), out)
        rc, out = self.run_loader(raw=f"lists: {{{KIND}: [&r [*r]]}}\n")
        self.assertAllNamed(rc, out, f"context.{KIND}.entry", "not a mapping")

    # -- fix round 5: F7, F8, F9 and DEC-0166/0167/0170/0173/0175 (table rows L1, L3, L4, T3, A4, P4, P9, H2, R6, E2, B1-B3) --
    def board(self, b_cell="`block/prototype-b9` / worktree", extra_rows=""):
        head = "| stream | block | branch / worktree | next task |\n|---|---|---|---|\n"
        return ("# Stream board\n\nprose\n\n" + head + "| A strict delivery | x | `block/program-tooling` / main | t |\n"
                + (f"| B prototype | b9 | {b_cell} | t |\n" if b_cell is not None else "") + extra_rows
                + "\n## Sync log\n| date | by |\n|---|---|\n")

    def stream_b_branch(self, branch="block/prototype-b9", text="from b9\nsecond\n"):
        """A commit holding B's handoff, reachable as origin/<branch> (a remote-tracking ref)."""
        gf.git(self.root, "checkout", "-q", "-b", "tmp-b")
        self.put(B_HANDOFF, text)
        gf.commit_all(self.root, "b")
        gf.git(self.root, "update-ref", f"refs/remotes/origin/{branch}", "HEAD")
        gf.git(self.root, "checkout", "-q", "main")
        gf.git(self.root, "branch", "-q", "-D", "tmp-b")

    def test_range_numbers_are_clipped_and_any_digit_count_is_a_named_fail(self):
        self.put("docs/a.md", lines_file(3))
        long9 = "9" * 5000
        for lines in ("1-" + long9, long9 + "-" + long9, long9 + "-1", "1-" + "9" * 19, "1-" + "9" * 300, long9 + "-end"):
            with self.subTest(lines=lines[:30] + f"...({len(lines)})"):
                rc, out = self.run_loader(entries=[entry("docs/a.md", lines), entry("docs/missing.md", "1-1")])
                self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md", "range")
                # the digit guard names the problem itself on every interpreter (3.9 int() has no digit limit; fix round 7)
                self.assertIn(f"FAIL context.{KIND}.docs/a.md: missing range: a line number of over 18 digits is past the end "
                              f"of any file, got {load_context.echo(lines)}\n", out)
                self.assertRegex(out, rf"(?m)^FAIL context\.{KIND}\.docs/missing\.md: missing path")      # X1: never hidden
                for line in out.splitlines():
                    self.assertLess(len(line), 200 + 200 + 120, line[:300])
        with self.subTest("300 entries of 4,000-digit ranges: clipped lines, most of them shown"):
            rc, out = self.run_loader(entries=[entry("docs/a.md", "1-" + "8" * 4000)] * 300)
            self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md", "range")
            self.assertGreater(len(out.splitlines()), 40, out[:300])

    @unittest.skipIf(os.geteuid() == 0, "root ignores permissions")
    def test_an_earlier_list_that_cannot_be_checked_is_named_as_possibly_standing(self):
        self.put("docs/a.md", lines_file(3))
        good, bad = [entry("docs/a.md", "1-3")], [entry("docs/nope.md", "1-3")]
        listed = os.path.join(self.root, self.LIST)
        sid_dir, exp_dir = os.path.dirname(listed), os.path.dirname(os.path.dirname(listed))
        self.addCleanup(lambda: [os.path.isdir(d) and os.chmod(d, 0o755) for d in (exp_dir, sid_dir)])
        for which, d, mode in (("session dir", sid_dir, 0o600), ("session dir", sid_dir, 0o000), ("expected dir", exp_dir, 0o000)):
            with self.subTest(which=which, mode=oct(mode)):
                self.reset_context_dir()
                rc, out = self.run_loader(entries=good)
                self.assertEqual(rc, 0, out)
                self.put(CTX, ctx(bad))
                gf.commit_all(self.root, "bad list")
                os.chmod(d, mode)
                rc, out = self.run_loader(commit=False)
                os.chmod(d, 0o755)
                self.assertAllNamed(rc, out, f"context.{KIND}.{self.LIST}", "may still stand")
                self.assertIn("docs/nope.md", out)
                self.assertTrue(os.path.isfile(listed))          # it did stand: the FAIL line is the only notice
        self.reset_context_dir()

    def test_a_long_kind_is_clipped_in_the_fail_id_and_the_failure_is_shown(self):
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        rc, out, _ = self.call(["--root", self.root, "k" * 100000])
        self.assertAllNamed(rc, out)
        self.assertEqual(len(out.splitlines()), 1, out[:300])
        self.assertTrue(out.startswith("FAIL context." + "k" * 197 + "....list: no list named " + "k" * 197 + "... in "), out[:300])
        self.assertNotIn("further failures not shown", out)

    def test_pathspec_magic_at_the_path_start_is_refused_by_the_whitelist(self):
        for name in (":(glob)*.md", ":(icase)a.md", ":!a.md", "*.md"):
            with self.subTest(path=name):
                self.put(name, lines_file(3))           # tracked, at the path start: git would read it as magic or a glob
                rc, out = self.run_loader(entries=[entry(name, "1-3")])
                self.assertAllNamed(rc, out, f"context.{KIND}.{name}", "path holds a character outside [A-Za-z0-9._/-]")

    def test_paths_are_narrowed_to_a_safe_set_and_any_whitespace_fails(self):
        for name in ("docs/a b.md", "docs/a:b.md", "docs/a+b.md", "docs/a'b.md", "docs/a@b.md", "docs/(x).md",
                     "docs/x lines 1-3 blob 0 -- y.md", "docs/a,b.md", "docs/a~b.md"):
            with self.subTest(path=name):
                self.put(name, lines_file(3))
                rc, out = self.run_loader(entries=[entry(name, "1-3")])
                self.assertAllNamed(rc, out, f"context.{KIND}.{name}", "path holds a character outside [A-Za-z0-9._/-]")
        for name in ("docs/a b.md", "docs/a　b.md", "docs/a b.md"):      # non-ASCII whitespace
            with self.subTest(path=ascii(name)):
                self.put(name, lines_file(3))
                rc, out = self.run_loader(entries=[entry(name, "1-3")])
                self.assertAllNamed(rc, out, None, "path holds")
        with self.subTest("handoff context_files use the same check"):
            self.put("docs/c d.md", lines_file(3))
            self.put(A_HANDOFF, front([entry("docs/c d.md", "1-3")]))
            self.put("docs/ok-1_2.v.md", lines_file(3))
            rc, out = self.run_loader(entries=[entry("docs/ok-1_2.v.md", "1-3")])
            self.assertAllNamed(rc, out, f"context.{KIND}.docs/c d.md", "outside [A-Za-z0-9._/-]")
        with self.subTest("every allowed ASCII character and printable non-ASCII load"):
            self.put(A_HANDOFF, front(None))
            for name in ("docs/AZaz09._-/x.md", "docs/über-日本.md"):
                self.put(name, lines_file(3))
                rc, out = self.run_loader(entries=[entry(name, "1-3")])
                self.assertEqual(rc, 0, out)

    def test_handoff_front_matter_depth_and_number_size_are_named_fails(self):
        self.put("docs/a.md", lines_file(3))
        cases = {"5,000-deep nesting": "---\nplan: " + "[" * 5000 + "]" * 5000 + "\n---\n",
                 "5,000-digit integer": "---\nplan: " + "9" * 5000 + "\n---\n"}
        for name, text in cases.items():
            with self.subTest(name):
                self.put(A_HANDOFF, text)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
                self.assertNotIn("internal", out)
                if name.startswith("5,000-deep") or rc != 0:         # 3.9 reads a 5,000-digit int; 3.11+ refuses it
                    self.assertAllNamed(rc, out, f"context.{KIND}.{A_HANDOFF}", "front matter is not valid YAML")

    def test_a_duplicate_key_in_any_handoff_field_fails_the_load(self):
        self.put("docs/a.md", lines_file(3))
        for text in ("---\nplan: a\nplan: b\n---\n", "---\nnext: {x: 1, x: 2}\ncontext_files: []\n---\n"):
            with self.subTest(text=text):
                self.put(A_HANDOFF, text)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
                self.assertAllNamed(rc, out, f"context.{KIND}.{A_HANDOFF}", "front matter is not valid YAML")

    def test_root_discovery_ignores_the_git_environment(self):
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3", "from repo A")]))
        gf.commit_all(self.root, "a")
        other = self.outside()
        gf.write(other, "docs/b.md", lines_file(3))
        gf.write(other, CTX, ctx([entry("docs/b.md", "1-3", "from repo B")]))
        for rel in (A_HANDOFF, B_HANDOFF):
            gf.write(other, rel, front(None))
        gf.commit_all(other, "b")
        os.environ.pop("CLAUDE_PROJECT_DIR", None)
        for env in ({"GIT_DIR": os.path.join(other, ".git"), "GIT_WORK_TREE": other}, {"GIT_DIR": os.path.join(other, ".git")}):
            with self.subTest(env=sorted(env)):
                rc, out, _ = self.call([KIND], env=env, cwd=os.path.join(self.root, "docs"))
                self.assertEqual(rc, 0, out)
                self.assertTrue(out.startswith("docs/a.md lines 1-3 blob ") and out.endswith(" -- from repo A\n"), out)
                self.assertFalse(os.path.exists(os.path.join(other, EXPECTED)), "the other repository's list was written")

    def test_open_ended_range_resolves_to_the_last_line(self):
        self.put("docs/a.md", lines_file(30))
        self.put("docs/nonl.md", "one\ntwo\nthree")
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-end", "all"), entry("docs/a.md", "5-end"),
                                           entry("docs/nonl.md", "2-end"), entry("docs/a.md", "30-end")])
        self.assertEqual(rc, 0, out)
        self.assertEqual([" ".join(l.split(" ")[:3]) for l in out.splitlines()],
                         ["docs/a.md lines 1-30", "docs/a.md lines 5-30", "docs/nonl.md lines 2-3", "docs/a.md lines 30-30"])
        self.assertEqual([e["lines"] for e in self.expected()["entries"]], ["1-30", "5-30", "2-3", "30-30"])
        import subprocess
        for path, nl, end in (("docs/a.md", True, 30), ("docs/nonl.md", False, 3)):      # row L3: wc -l counts newline characters
            with self.subTest(wc=path):
                wc = int(subprocess.run(["wc", "-l", os.path.join(self.root, path)], capture_output=True, text=True,
                                        check=True).stdout.split()[0])
                self.assertEqual(end, wc if nl else wc + 1)

    def test_open_ended_range_is_split_within_2000_lines_and_16000_bytes(self):
        self.put("docs/long.md", lines_file(4500))                 # about 10 bytes a line: the byte cap binds
        self.put("docs/short.md", "x\n" * 4500)                     # 2 bytes a line: the 2000-line cap binds
        self.put("docs/dense.md", lines_file(1500, width=60))
        for path, n in (("docs/long.md", 4500), ("docs/short.md", 4500), ("docs/dense.md", 1500)):
            with self.subTest(path=path):
                rc, out = self.run_loader(entries=[entry(path, "1-end", "the whole file")])
                self.assertEqual(rc, 0, out)
                with open(os.path.join(self.root, path), "rb") as fh:
                    rows = fh.read().split(b"\n")
                got = [tuple(map(int, l.split(" ")[2].split("-"))) for l in out.splitlines()]
                self.assertEqual(got[0][0], 1)
                self.assertEqual(got[-1][1], n)
                for (a, b), nxt in zip(got, got[1:] + [(n + 1, None)]):
                    self.assertEqual(nxt[0], b + 1)                       # consecutive, no gap, no overlap
                    self.assertLessEqual(b - a + 1, 2000)
                    self.assertLessEqual(len(b"\n".join(rows[a - 1:b])), 16000)
                    if b < n:                                             # greedy: one more line would cross a cap
                        self.assertTrue(b - a + 1 == 2000 or len(b"\n".join(rows[a - 1:b + 1])) > 16000, (a, b))
                self.assertGreater(len(got), 2)
                if path == "docs/short.md":
                    self.assertEqual(got, [(1, 2000), (2001, 4000), (4001, 4500)])
                self.assertTrue(all(l.endswith(" -- the whole file") for l in out.splitlines()))
                self.assertEqual([e["lines"] for e in self.expected()["entries"]], [f"{a}-{b}" for a, b in got])

    def test_open_ended_range_failures_are_named(self):
        self.put("docs/a.md", lines_file(10))
        self.put("docs/wide.md", "short\n" + "w" * 16001 + "\nshort\n")
        self.put("docs/empty.md", "")
        cases = (("docs/a.md", "11-end", "past the last line"), ("docs/a.md", "1-End", "range"), ("docs/a.md", "end-end", "range"),
                 ("docs/a.md", "1-endx", "range"), ("docs/a.md", "1-end\n", "range"), ("docs/a.md", "0-end", "range"),
                 ("docs/wide.md", "1-end", "line 2 is 16001 bytes"), ("docs/empty.md", "1-end", "empty file"))
        for path, lines, text in cases:
            with self.subTest(lines=ascii(lines), path=path):
                rc, out = self.run_loader(entries=[entry(path, lines)])
                self.assertAllNamed(rc, out, f"context.{KIND}.{path}", text)
        with self.subTest("a split that overflows the 16 KB output"):
            self.put("docs/huge.md", lines_file(100000))
            rc, out = self.run_loader(entries=[entry("docs/huge.md", "1-end", "w" * 300)])
            self.assertAllNamed(rc, out, f"context.{KIND}.output", "16384")

    def test_open_ended_range_on_a_ref_resolves_on_the_copy(self):
        gf.git(self.root, "checkout", "-q", "-b", "other")
        self.put("docs/theirs.md", lines_file(7))
        gf.commit_all(self.root, "other")
        gf.git(self.root, "checkout", "-q", "main")
        rc, out = self.run_loader(entries=[entry("docs/theirs.md", "3-end", ref="other")])
        self.assertEqual(rc, 0, out)
        self.assertRegex(out, r"^\.superpowers/context/refs/[^ ]+/docs/theirs\.md lines 3-7 blob ")

    def test_sync_reads_stream_b_handoff_from_the_board_branch(self):
        self.stream_b_branch()
        self.put("docs/superpowers/state/program/board.md", self.board())
        rc, out = self.run_loader(kind="sync", entries=[entry(B_HANDOFF, "1-end", "B's handoff", ref="board:B")])
        self.assertEqual(rc, 0, out)
        want = load_context.ref_copy_path("origin/block/prototype-b9", B_HANDOFF)
        self.assertEqual(out, f"{want} lines 1-2 blob {blob(b'from b9' + bytes([10]) + b'second' + bytes([10]))} -- B's handoff\n")
        self.assertEqual(self.expected("sync")["entries"][0]["path"], want)

    def test_board_problems_are_named_fails(self):
        board_rel = "docs/superpowers/state/program/board.md"
        self.stream_b_branch()
        cases = {
            "board missing": (None, "board"),
            "no stream B row": (self.board(b_cell=None), "no stream B row"),
            "two stream B rows": (self.board(extra_rows="| B again | b2 | `block/prototype-b2` | t |\n"), "more than one stream B row"),
            "no backticked branch": (self.board(b_cell="block/prototype-b9 / worktree"), "backticked branch"),
            "empty backticks": (self.board(b_cell="`` / worktree"), "not a branch name"),
            "unsafe branch": (self.board(b_cell="`--upload-pack=x` / w"), "not a branch name"),
            "branch with a space": (self.board(b_cell="`block/a b` / w"), "not a branch name"),
            "branch with a dot-dot component": (self.board(b_cell="`block/../x` / w"), "not a branch name"),
            "branch with dot-dot inside a name": (self.board(b_cell="`block/a..b` / w"), "not a branch name"),
            "branch ending in .lock": (self.board(b_cell="`block/x.lock` / w"), "not a branch name"),
            "branch ending in a dot": (self.board(b_cell="`block/x.` / w"), "not a branch name"),
            "missing ref": (self.board(b_cell="`block/prototype-b7` / w"),
                            "missing ref origin/block/prototype-b7 (the branch in the board's stream B row)"),
            "row without a branch cell": (self.board(b_cell=None, extra_rows="| B prototype | b9 |\n"), "no branch cell"),
            "no stream table": ("# Stream board\n\n| what | branch |\n|---|---|\n| B | `block/prototype-b9` |\n", "no stream table"),
            "board not UTF-8": (None, "not valid UTF-8"),
            "no branch column": (self.board().replace("branch / worktree", "where"), "no branch column"),
            "long cell": (self.board(b_cell="`" + "b" * 5000 + "` / w"), "missing ref"),
        }
        for name, (text, reason) in cases.items():
            with self.subTest(name):
                full = os.path.join(self.root, board_rel)
                if name == "board not UTF-8":
                    with open(full, "wb") as fh:
                        fh.write(self.board().encode() + bytes([0xff]))
                elif text is None:
                    if os.path.exists(full):
                        os.unlink(full)
                else:
                    self.put(board_rel, text)
                rc, out = self.run_loader(kind="sync", entries=[entry(B_HANDOFF, "1-end", ref="board:B")], argv=["sync"])
                self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", reason)
                self.assertLess(max(len(l) for l in out.splitlines()), 600)

    # -- fix round 6: F10 (rows B1, B2, B3, B4, L3) ---------------------------------------------
    BOARD_REL = "docs/superpowers/state/program/board.md"

    def run_sync(self, commit=False):
        return self.run_loader(kind="sync", entries=[entry(B_HANDOFF, "1-end", ref="board:B")], argv=["sync"], commit=commit)

    def test_every_board_read_problem_is_a_named_fail(self):
        """Row B1: each problem input_problem() or the read names, never read through, never a hang, never `internal`."""
        self.stream_b_branch()
        self.put(self.BOARD_REL, self.board())
        self.put(CTX, ctx([entry(B_HANDOFF, "1-end", ref="board:B")], "sync"))
        gf.commit_all(self.root, "board")
        full = os.path.join(self.root, self.BOARD_REL)
        prog = os.path.dirname(full)
        cases = {"a symlink": "symlink in path (docs/superpowers/state/program/board.md)", "a directory": "not a regular file",
                 "a FIFO": "not a regular file", "missing": "missing or unreadable (No such file or directory)",
                 "not UTF-8": "is not valid UTF-8"}
        if os.geteuid() != 0:
            cases["mode 000"] = "unreadable (Permission denied)"
        for case, reason in cases.items():
            with self.subTest(case):
                os.unlink(full)
                if case == "a directory":
                    os.makedirs(full)
                elif case == "a FIFO":
                    os.mkfifo(full)
                    self.unblock_fifo_later(full)
                elif case == "a symlink":
                    self.put("docs/real-board.md", self.board())
                    os.symlink(os.path.join(self.root, "docs/real-board.md"), full)
                elif case == "not UTF-8":
                    with open(full, "wb") as fh:
                        fh.write(self.board().encode() + bytes([0xff]))
                elif case == "mode 000":
                    self.put(self.BOARD_REL, self.board())
                    os.chmod(full, 0)
                    self.addCleanup(lambda: os.path.isfile(full) and os.chmod(full, 0o644))
                rc, out = self.run_sync()
                if case == "mode 000":
                    os.chmod(full, 0o644)
                self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", f"ref board:B: board {self.BOARD_REL} {reason}")
                self.assertEqual(len(out.splitlines()), 1, out)
                if os.path.isdir(full) and not os.path.islink(full):
                    os.rmdir(full)
                elif os.path.lexists(full):
                    os.unlink(full)
                gf.git(self.root, "checkout", "--", self.BOARD_REL)
        with self.subTest("a symlinked directory component (shared with context.yml: row C1 names it first)"):
            real = os.path.join(self.root, "docs/real-program")
            os.rename(prog, real)
            os.symlink(real, prog)
            rc, out = self.run_sync()
            self.assertAllNamed(rc, out, "context.sync.list", "symlink in path (docs/superpowers/state/program)")
            os.unlink(prog)
            os.rename(real, prog)
        with self.subTest("the restored board loads"):
            rc, out = self.run_sync()
            self.assertEqual(rc, 0, out)

    def test_board_names_and_table_bounds_match_as_whole_strings(self):
        """Row B2: the row label and the header cell are whole words, an escaped pipe stays in its cell, a blank line ends the table."""
        self.stream_b_branch()
        b9_row = "| B prototype | b9 | `block/prototype-b9` / w | t |\n"
        cases = {
            "a B2 row label": (self.board(b_cell=None, extra_rows=b9_row.replace("| B prototype", "| B2 prototype")),
                               "no stream B row"),
            "a streams header": (self.board().replace("| stream | block |", "| streams | block |"), "no stream table"),
            "a Stream header (case)": (self.board().replace("| stream | block |", "| Stream | block |"), "no stream table"),
            "a lower-case b row label (case)": (self.board(b_cell=None, extra_rows=b9_row.replace("| B prototype", "| b prototype")),
                                                "no stream B row"),
            "a B row after the table's end": (self.board(b_cell=None, extra_rows="\n" + b9_row), "no stream B row"),
        }
        for name, (text, reason) in cases.items():
            with self.subTest(name):
                self.put(self.BOARD_REL, text)
                rc, out = self.run_sync(commit=True)
                self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", f"ref board:B: {self.BOARD_REL}: {reason}")
        with self.subTest("an escaped pipe inside a cell"):
            self.put(self.BOARD_REL, self.board(b_cell=None, extra_rows="| B prototype | b9 \\| b10 | `block/prototype-b9` / w | t |\n"))
            rc, out = self.run_sync(commit=True)
            self.assertEqual(rc, 0, out)
            self.assertTrue(out.startswith(load_context.ref_copy_path("origin/block/prototype-b9", B_HANDOFF) + " lines 1-2 "), out)

    def test_the_branch_column_is_the_header_cell_whose_first_word_is_branch(self):
        """Row B2 (fix round 7, F14, DEC-0187 first-word-whole): the branch column is the first header cell whose first
        space-separated word (DEC-0235 spaces-only) is exactly `branch`; `branching ...`, `branches`, `Branch`,
        `branch/worktree` and `branch<tab>/ worktree` are not."""
        self.stream_b_branch()
        head = "| stream | {h1} | {h2} | next task |\n|---|---|---|---|\n"
        row = "| A strict delivery | `block/program-tooling` | `block/program-tooling` / main | t |\n| B prototype | {c1} | {c2} | t |\n"

        def board(h1, h2, c1="`block/prototype-b7`", c2="`block/prototype-b9` / w"):
            return "# Stream board\n\n" + head.format(h1=h1, h2=h2) + row.format(c1=c1, c2=c2) + "\n"
        b9 = load_context.ref_copy_path("origin/block/prototype-b9", B_HANDOFF) + " lines 1-2 "
        loads = {"a branching column before the branch column": board("branching notes", "branch / worktree"),
                 "a branches column before the branch column": board("branches", "branch / worktree"),
                 "a Branch column before the branch column": board("Branch", "branch / worktree"),
                 "a branch/worktree column before the branch column": board("branch/worktree", "branch / worktree"),
                 "an old branch column before the branch column": board("old branch", "branch / worktree"),
                 "the branch column alone": board("block", "branch"),
                 "two branch columns: the first counts": board("block", "branch / worktree").replace(
                     "| next task |", "| branch b |").replace("| t |\n| B", "| `block/prototype-b7` |\n| B").replace(
                     "/ w | t |", "/ w | `block/prototype-b7` |")}
        for name, text in loads.items():
            with self.subTest(name):
                self.put(self.BOARD_REL, text)
                rc, out = self.run_sync(commit=True)
                self.assertEqual(rc, 0, out)
                self.assertTrue(out.startswith(b9), out)
        no_column = f"ref board:B: {self.BOARD_REL}: no branch column (a header cell whose first word is `branch`)"
        for name, h2 in (("only a branching column", "branching notes"), ("only a branches column", "branches"),
                         ("only a Branch column", "Branch"), ("only a branch/worktree column", "branch/worktree"),
                         ("only a BRANCH column", "BRANCH / worktree"), ("only an empty header cell", ""),
                         ("only an old branch column", "old branch"),
                         ("a tab after the first word (DEC-0235 spaces-only; loaded in round 7)", "branch\t/ worktree")):
            with self.subTest(name):
                self.put(self.BOARD_REL, board("block", h2, c1="b9"))
                rc, out = self.run_sync(commit=True)
                self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", f"FAIL context.sync.{B_HANDOFF}: {no_column}\n")

    def test_the_board_is_read_from_the_working_tree_and_must_be_tracked(self):
        """Row B4 (DEC-0177 working-tree): the board on disk, tracked at HEAD, through the D32 checks."""
        self.stream_b_branch()
        self.put(CTX, ctx([entry(B_HANDOFF, "1-end", ref="board:B")], "sync"))
        gf.commit_all(self.root, "context")
        not_tracked = f"ref board:B: board {self.BOARD_REL} is not a regular file tracked at HEAD"
        with self.subTest("untracked"):
            self.put(self.BOARD_REL, self.board())
            rc, out = self.run_sync()
            self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", not_tracked)
        with self.subTest("staged, not committed"):
            gf.git(self.root, "add", self.BOARD_REL)
            rc, out = self.run_sync()
            self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", not_tracked)
        with self.subTest("an uncommitted edit to the B row is what the loader uses"):
            self.put(self.BOARD_REL, self.board(b_cell="`block/prototype-b7` / w"))
            gf.commit_all(self.root, "board names b7, which origin lacks")
            self.put(self.BOARD_REL, self.board())
            rc, out = self.run_sync()
            self.assertEqual(rc, 0, out)
            self.assertTrue(out.startswith(load_context.ref_copy_path("origin/block/prototype-b9", B_HANDOFF) + " lines 1-2 "), out)
            self.assertIn(self.BOARD_REL, gf.git(self.root, "status", "--porcelain"))
        with self.subTest("a symlink at HEAD, a regular file on disk (fix round 7 F13f)"):
            gf.git(self.root, "rm", "-q", "--cached", self.BOARD_REL)
            gf.git(self.root, "commit", "-q", "-m", "board untracked")
            os.unlink(os.path.join(self.root, self.BOARD_REL))
            self.head_mode_fixture(self.BOARD_REL, "120000", self.board())
            rc, out = self.run_sync()
            self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}", not_tracked)

    def head_mode_fixture(self, rel, mode, disk_text):
        """Commit `rel` with git mode `mode` at HEAD (120000 symlink, 040000 directory, 160000 submodule, 100755
        executable), then leave a regular file holding `disk_text` on disk at `rel` (uncommitted for the first three)."""
        full = os.path.join(self.root, rel)
        if os.path.isdir(full) and not os.path.islink(full):
            gf.remove(full)
        elif os.path.lexists(full):
            os.unlink(full)
        if mode == "120000":
            self.put("docs/target.md", disk_text)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            os.symlink(os.path.relpath(os.path.join(self.root, "docs/target.md"), os.path.dirname(full)), full)
            gf.commit_all(self.root, "symlink at HEAD")
            os.unlink(full)
        elif mode == "040000":
            self.put(rel + "/inner.md", disk_text)
            gf.commit_all(self.root, "directory at HEAD")
            gf.remove(full)
        elif mode == "160000":
            sha = gf.git(self.root, "rev-parse", "HEAD")
            gf.git(self.root, "update-index", "--add", "--cacheinfo", f"160000,{sha},{rel}")
            gf.git(self.root, "commit", "-q", "-m", "submodule at HEAD")
        elif mode == "100755":
            self.put(rel, disk_text)
            os.chmod(full, 0o755)
            gf.commit_all(self.root, "executable at HEAD")
        self.put(rel, disk_text)
        if mode == "100755":
            os.chmod(full, 0o755)
        self.assertEqual(gf.git(self.root, "ls-tree", "HEAD", "--", rel).split(" ")[0], mode)
        self.assertTrue(os.path.isfile(full) and not os.path.islink(full))

    def test_a_file_whose_head_mode_is_not_regular_is_refused_even_when_regular_on_disk(self):
        """Rows P7 and B4 (fix round 7, F13f): the file on disk must be tracked at HEAD as a regular file (mode 100644
        or 100755); a symlink, a directory or a submodule at HEAD with a regular file on disk is refused, for an entry
        and for the board. An executable (100755) entry, board or file at a ref loads."""
        for mode in ("120000", "040000", "160000"):
            with self.subTest(entry=mode):
                self.setUp()
                self.head_mode_fixture("docs/a.md", mode, lines_file(3))
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")], commit=False)
                self.assertAllNamed(rc, out, f"context.{KIND}.docs/a.md",
                                    f"FAIL context.{KIND}.docs/a.md: not a regular file tracked at HEAD\n")
                self.assertEqual(len(out.splitlines()), 1, out)
            with self.subTest(board=mode):
                self.setUp()
                self.stream_b_branch()
                self.put(CTX, ctx([entry(B_HANDOFF, "1-end", ref="board:B")], "sync"))
                gf.commit_all(self.root, "context")
                self.head_mode_fixture(self.BOARD_REL, mode, self.board())
                rc, out = self.run_sync()
                self.assertAllNamed(rc, out, f"context.sync.{B_HANDOFF}",
                                    f"FAIL context.sync.{B_HANDOFF}: ref board:B: board {self.BOARD_REL} is not a regular file tracked at HEAD\n")
                self.assertEqual(len(out.splitlines()), 1, out)
        with self.subTest("an executable entry and an executable board load"):
            self.setUp()
            self.stream_b_branch()
            self.head_mode_fixture("docs/a.md", "100755", lines_file(3))
            self.head_mode_fixture(self.BOARD_REL, "100755", self.board())
            self.put(CTX, ctx([entry("docs/a.md", "1-3")], KIND) + ctx([entry(B_HANDOFF, "1-end", ref="board:B")], "sync").replace("lists:\n", ""))
            gf.commit_all(self.root, "context")
            rc, out = self.run_loader(argv=[KIND], commit=False)
            self.assertEqual(out, f"docs/a.md lines 1-3 blob {blob(lines_file(3).encode())} -- because\n")
            rc, out = self.run_sync()
            self.assertEqual(rc, 0, out)
            self.assertTrue(out.startswith(load_context.ref_copy_path("origin/block/prototype-b9", B_HANDOFF) + " lines 1-2 "), out)
        with self.subTest("an executable file at a ref loads"):
            self.setUp()
            gf.git(self.root, "checkout", "-q", "-b", "other")
            self.put("docs/x.md", lines_file(3))
            os.chmod(os.path.join(self.root, "docs/x.md"), 0o755)
            gf.commit_all(self.root, "executable at other")
            gf.git(self.root, "checkout", "-q", "main")
            rc, out = self.run_loader(entries=[entry("docs/x.md", "1-3", ref="other")])
            self.assertEqual(out, f"{load_context.ref_copy_path('other', 'docs/x.md')} lines 1-3 blob {blob(lines_file(3).encode())} -- because\n")

    def test_a_modified_file_loads_with_the_blob_of_the_bytes_on_disk(self):
        """Row P7: a tracked file edited in the working tree is read as it is on disk (playbook: the blob of the file as read)."""
        self.put("docs/a.md", lines_file(3))
        gf.commit_all(self.root, "a")
        self.put("docs/a.md", lines_file(4))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-4")], commit=False)
        self.assertEqual(rc, 0, out)
        self.assertIn("docs/a.md", gf.git(self.root, "status", "--porcelain"))
        self.assertEqual(out, f"docs/a.md lines 1-4 blob {blob(lines_file(4).encode())} -- because\n")

    def test_open_ended_range_split_is_inclusive_at_16000_bytes(self):
        """Row L3: two lines joining to exactly 16,000 bytes stay one entry; one byte more splits them."""
        self.put("docs/edge.md", "a" * 7999 + "\n" + "b" * 8000 + "\n")
        self.put("docs/over.md", "a" * 7999 + "\n" + "b" * 8001 + "\n")
        for path, want in (("docs/edge.md", ["1-2"]), ("docs/over.md", ["1-1", "2-2"])):
            with self.subTest(path=path):
                rc, out = self.run_loader(entries=[entry(path, "1-end")])
                self.assertEqual(rc, 0, out)
                self.assertEqual([l.split(" ")[2] for l in out.splitlines()], want)

    # -- fix round 8b: every SURVIVED mutant of the r8 run (r8-mutate.txt), F15, F16, DEC-0235 -------------------------
    # Each test pins the observable value a mutant changes: the whole output text, the whole FAIL reason, the rc, the
    # bytes written. Mutant ids are those of r8-mutate.txt (base 762a7bb, head f7be2a4).
    def test_an_empty_root_is_a_named_fail_never_the_cwd_tree(self):
        """Row R1 (F15): `--root ""` (and a whitespace-only --root) is FAIL context.input "--root is empty", as the gates
        (gate_register, gate_claims, gate_decisions, run_all) refuse it; never the cwd's tree, never root discovery."""
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        plain = tempfile.mkdtemp(prefix="not-a-repo-")
        self.addCleanup(gf.remove, plain)
        for root in ("", " ", "  ", "\t", "\n", " \t\n"):
            for cwd in (self.root, plain):
                for form in (["--root", root, KIND], ["--root=" + root, KIND], [KIND, "--root", root]):
                    with self.subTest(root=ascii(root), cwd=os.path.basename(cwd), form=ascii(form[0])[:9]):
                        rc, out, err = self.call(form, cwd=cwd)
                        self.assertEqual((rc, out, err), (1, "FAIL context.input: --root is empty\n", ""))
                        self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED)))
        with self.subTest("CLAUDE_PROJECT_DIR set: an empty --root still fails"):
            rc, out, _ = self.call(["--root", "", KIND], env={"CLAUDE_PROJECT_DIR": self.root}, cwd=plain)
            self.assertEqual((rc, out), (1, "FAIL context.input: --root is empty\n"))
        with self.subTest("an empty CLAUDE_PROJECT_DIR is unset (lib.repo_root's rule): the cwd's tree is found"):
            rc, out, _ = self.call([KIND], env={"CLAUDE_PROJECT_DIR": ""}, cwd=os.path.join(self.root, "docs"))
            self.assertEqual((rc, out), (0, f"docs/a.md lines 1-3 blob {blob(lines_file(3).encode())} -- because\n"))

    def test_a_root_whose_name_ends_in_whitespace_is_that_root(self):
        """Rows R1/R5/R6: git prints the top level followed by one newline; only that newline is removed, so a work tree
        whose directory name ends in a space or a tab loads (by --root, CLAUDE_PROJECT_DIR and discovery)."""
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        want = f"docs/a.md lines 1-3 blob {blob(lines_file(3).encode())} -- because\n"
        parent = tempfile.mkdtemp(prefix="spaced-")
        self.addCleanup(gf.remove, parent)
        for name in ("tree ", "tree\t", "tree  \t "):
            moved = os.path.join(os.path.realpath(parent), name)
            os.rename(self.root, moved)
            try:
                for how, argv, env, cwd in (("--root", ["--root", moved, KIND], None, None),
                                            ("CLAUDE_PROJECT_DIR", [KIND], {"CLAUDE_PROJECT_DIR": moved}, None),
                                            ("discovery", [KIND], None, os.path.join(moved, "docs"))):
                    with self.subTest(name=ascii(name), how=how):
                        if how == "discovery":
                            os.environ.pop("CLAUDE_PROJECT_DIR", None)
                        rc, out, _ = self.call(argv, env=env, cwd=cwd)
                        self.assertEqual((rc, out), (0, want))
                        self.assertTrue(os.path.isfile(os.path.join(moved, EXPECTED, SID, KIND + ".json")))
            finally:
                os.rename(moved, self.root)

    def test_argument_and_discovery_failures_are_pinned_whole(self):
        """Rows A2, A5, A6, R5, R6, X2 (m0888 m0889 m0892 m0896 m0897 m0916 m0917 m0942 m0945 m0947 m0954 m0961)."""
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        plain = tempfile.mkdtemp(prefix="not-a-repo-")
        self.addCleanup(gf.remove, plain)
        os.environ.pop("CLAUDE_PROJECT_DIR", None)
        no_git = {"PATH": os.path.join(plain, "no-bin")}
        cases = (("unknown option", ["--root", self.root, "--bogus", KIND], None, None,
                  "FAIL context.input: bad arguments: unrecognized arguments: --bogus\n"),
                 ("kind outside the grammar", ["--root", self.root, "../x"], None, None,
                  "FAIL context.input: kind ../x is not [a-z0-9_-]+\n"),
                 ("-h", ["--root", self.root, "-h"], None, None, "FAIL context.input: bad arguments (argparse exit 0)\n"),
                 ("no root outside any work tree", [KIND], None, plain,
                  "FAIL context.input: no --root, no CLAUDE_PROJECT_DIR, and the working directory is not in a git work tree\n"),
                 ("no root, git not on PATH", [KIND], no_git, os.path.join(self.root, "docs"),
                  "FAIL context.input: git cannot run (No such file or directory)\n"),
                 ("--root, git not on PATH", ["--root", self.root, KIND], no_git, None,
                  "FAIL context.input: git cannot run (No such file or directory)\n"))
        for name, argv, env, cwd, want in cases:
            with self.subTest(name):
                rc, out, err = self.call(argv, env=env, cwd=cwd)
                self.assertEqual((rc, out), (1, want))
                if name == "-h":
                    self.assertEqual(err.splitlines()[0], "usage: load_context [-h] [--root ROOT] kind")
                    self.assertIn("\n  kind         a session kind or dispatch role named in context.yml\n", err)
                else:
                    self.assertEqual(err, "")
                self.assertFalse(os.path.exists(os.path.join(self.root, EXPECTED)))

    def test_failure_reasons_are_pinned_whole(self):
        """Rows C6, C9, H2, X2 (m0612 m0686 m0781 m0782 m0835): the FAIL id and the reason, whole."""
        self.put("docs/a.md", lines_file(3))
        good = entry("docs/a.md", "1-3")
        with self.subTest("ref not a string"):
            rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", ref=5)])
            self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.docs/a.md: ref is not a string: 5\n"))
        with self.subTest("front matter not a mapping"):
            self.put(A_HANDOFF, "---\n- a\n---\nbody\n")
            rc, out = self.run_loader(entries=[good])
            self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.{A_HANDOFF}: front matter is not a mapping\n"))
            self.put(A_HANDOFF, front(None))
        with self.subTest("context.yml nested too deep to parse"):
            rc, out = self.run_loader(raw="lists: " + "[" * 5000 + "]" * 5000 + "\n")
            self.assertEqual(rc, 1, out)
            self.assertTrue(out.startswith(f"FAIL context.{KIND}.list: {CTX}: cannot be parsed: RecursionError: "), out)
            self.assertEqual(len(out.splitlines()), 1, out)
        with self.subTest("anything else raised"):
            with mock.patch.object(load_context, "check_entry", side_effect=RuntimeError("boom")):
                rc, out = self.run_loader(entries=[good])
            self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.internal: unexpected RuntimeError: boom\n"))

    def test_yaml_values_are_echoed_bounded_per_level_and_per_container(self):
        """Rows C6, C8 (m0174-m0181): an echoed YAML value shows at most 3 levels and 6 items of a list, mapping or set."""
        self.put("docs/a.md", lines_file(3))
        cases = (("a list of 7", "[1, 2, 3, 4, 5, 6, 7]", "[1, 2, 3, 4, 5, 6, ...]"),
                 ("a list of 6", "[1, 2, 3, 4, 5, 6]", "[1, 2, 3, 4, 5, 6]"),
                 ("a mapping of 7", "{a: 1, b: 2, c: 3, d: 4, e: 5, f: 6, g: 7}", "{'a': 1, 'b': 2, 'c': 3, 'd': 4, 'e': 5, 'f': 6, ...}"),
                 ("a mapping of 6", "{a: 1, b: 2, c: 3, d: 4, e: 5, f: 6}", "{'a': 1, 'b': 2, 'c': 3, 'd': 4, 'e': 5, 'f': 6}"),
                 ("a set of 7", "!!set {g, f, e, d, c, b, a}", "{'a', 'b', 'c', 'd', 'e', 'f', ...}"),
                 ("a set of 6", "!!set {f, e, d, c, b, a}", "{'a', 'b', 'c', 'd', 'e', 'f'}"),
                 ("4 levels", "[[[[1]]]]", "[[[[...]]]]"),
                 ("3 levels", "[[[1]]]", "[[[1]]]"))
        for name, value, shown in cases:
            with self.subTest(name):
                rc, out = self.run_loader(raw=f"lists: {{{KIND}: [{{path: docs/a.md, lines: 1-3, why: w, ref: {value}}}]}}\n")
                self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.docs/a.md: ref is not a string: {shown}\n"))

    def test_a_value_of_exactly_200_characters_is_echoed_whole(self):
        """Row P8 (m0188): 200 characters is the clip, inclusive; 201 is cut to 197 and `...`."""
        p200 = "docs/" + "p" * 192 + ".md"
        p201 = "docs/" + "p" * 193 + ".md"
        rc, out = self.run_loader(entries=[entry(p200, "1-1"), entry(p201, "1-1")])
        self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.{p200}: missing path (not a file under the root)\n"
                                        f"FAIL context.{KIND}.{p201[:197]}...: missing path (not a file under the root)\n"))

    def test_the_ref_copy_path_is_pinned_whole(self):
        """Row T1 (m0337-m0343): the copy of `git show <ref>:<path>` is .superpowers/context/refs/<tag>/<path>, <tag> the
        ref with every character outside [A-Za-z0-9._-] written `_`, cut to 40 characters, then `-` and the first 8 hex
        digits of sha1(ref)."""
        name = "feature/x." + "y" * 36 + "-z/w"
        gf.git(self.root, "checkout", "-q", "-b", name)
        self.put("docs/x.md", lines_file(3))
        gf.commit_all(self.root, "x")
        gf.git(self.root, "checkout", "-q", "main")
        sha8 = hashlib.sha1(name.encode()).hexdigest()[:8]
        tag = "feature_x." + "y" * 30 + "-" + sha8                       # 40 characters of "feature_x.yyy...yyy-z_w", then -<sha8>
        self.assertEqual(len(tag), 40 + 1 + 8)
        rc, out = self.run_loader(entries=[entry("docs/x.md", "1-3", ref=name)])
        self.assertEqual((rc, out), (0, f".superpowers/context/refs/{tag}/docs/x.md lines 1-3 blob {blob(lines_file(3).encode())} -- because\n"))
        self.assertTrue(os.path.isfile(os.path.join(self.root, ".superpowers/context/refs", tag, "docs/x.md")))

    def test_a_ref_with_git_revision_syntax_anywhere_in_it_is_unsafe(self):
        """Row F1 (m0334): the ref grammar is matched as a whole string; `:`, `?`, `*`, `[`, `\\`, whitespace and a `~`
        or `^` not followed by digits only are refused wherever they stand. `HEAD~1` and `HEAD^` load."""
        self.put("docs/a.md", lines_file(3))
        gf.commit_all(self.root, "a")
        self.put("docs/b.md", "b\n")
        gf.commit_all(self.root, "b")
        for ref in ("main:docs/a.md", "HEAD:docs/a.md", "ma?n", "ma*n", "ma[n", "ma\\n", "ma n", "HEAD~x", "HEAD^x",
                    "HEAD~1:docs/a.md", "HEAD^{tree}", "HEAD@{0}:x"):
            with self.subTest(ref=ref):
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", ref=ref)])
                self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.docs/a.md: unsafe ref {ref}\n"))
        for ref in ("HEAD~1", "HEAD^"):
            with self.subTest(ref=ref):
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", ref=ref)])
                self.assertEqual(rc, 0, out)

    def test_a_ref_whose_blob_cannot_be_read_is_a_named_fail(self):
        """Row F3 (m0475 m0479 m0480): the tree at the ref names the file but its blob object is missing (a damaged or
        partial repository): `git cat-file blob` fails, FAIL "missing path at ref <ref>", no copy written."""
        gf.git(self.root, "checkout", "-q", "-b", "other")
        self.put("docs/x.md", "only on other 8b\n")
        gf.commit_all(self.root, "x")
        gf.git(self.root, "checkout", "-q", "main")
        sha = gf.git(self.root, "rev-parse", "other:docs/x.md")
        os.unlink(os.path.join(self.root, ".git", "objects", sha[:2], sha[2:]))
        rc, out = self.run_loader(entries=[entry("docs/x.md", "1-1", ref="other")])
        self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.docs/x.md: missing path at ref other\n"))
        self.assertFalse(os.path.exists(os.path.join(self.root, load_context.CONTEXT_DIR, "refs")))

    def test_a_line_number_of_exactly_18_digits_is_converted(self):
        """Row L1 (m0498): 18 digits is the bound, inclusive; 19 digits is refused before int()."""
        self.put("docs/a.md", lines_file(3))
        n18, n19 = "9" * 18, "9" * 19
        cases = ((f"1-{n18}", f"range 1-{n18} is over 2000 lines"),
                 (f"{n18}-end", f"missing range: {n18}-end starts past the last line (3)"),
                 (f"{n18}-{n18}", f"missing range: {n18}-{n18} is past the end of the file (4 lines)"),
                 (f"1-{n19}", f"missing range: a line number of over 18 digits is past the end of any file, got 1-{n19}"),
                 (f"{n19}-end", f"missing range: a line number of over 18 digits is past the end of any file, got {n19}-end"))
        for lines, reason in cases:
            with self.subTest(lines=lines):
                rc, out = self.run_loader(entries=[entry("docs/a.md", lines)])
                self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.docs/a.md: {reason}\n"))

    def test_a_single_line_of_exactly_16000_bytes_is_one_open_ended_range(self):
        """Row L4 (m0539, F16b): one line of 16,000 bytes is readable (lines 1-1); 16,001 bytes is the named FAIL."""
        for n, final in ((16000, "\n"), (16000, ""), (16001, "\n")):
            with self.subTest(bytes=n, final=ascii(final)):
                data = "z" * n + final
                self.put("docs/one.md", data)
                rc, out = self.run_loader(entries=[entry("docs/one.md", "1-end")])
                if n == 16000:
                    self.assertEqual((rc, out), (0, f"docs/one.md lines 1-1 blob {blob(data.encode())} -- because\n"))
                else:
                    self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.docs/one.md: line 1 is 16001 bytes, over 16000 "
                                                    "bytes: 1-end cannot be split into readable ranges\n"))

    def test_the_why_is_one_line_with_runs_of_spaces_collapsed(self):
        """Row W1 output (F16c): the printed why is the why's words joined by one space (` -- a b` for `a   b`)."""
        self.put("docs/a.md", lines_file(3))
        for why, shown in (("a   b", "a b"), ("  lead and trail  ", "lead and trail"), ("one", "one")):
            with self.subTest(why=ascii(why)):
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3", why)])
                self.assertEqual((rc, out), (0, f"docs/a.md lines 1-3 blob {blob(lines_file(3).encode())} -- {shown}\n"))

    def test_the_expected_list_bytes_are_pinned(self):
        """Row T2 (m0720 m0721 m0722): the expected list is JSON with indent 1 and a final newline."""
        data = lines_file(30).encode()
        self.put("docs/a.md", data.decode())
        rc, out = self.run_loader(entries=[entry("docs/a.md", "5-12"), entry("docs/a.md", "1-1")])
        self.assertEqual(rc, 0, out)
        with open(os.path.join(self.root, self.LIST), "rb") as fh:
            got = fh.read()
        b = blob(data)
        self.assertEqual(got, ('{\n "kind": "stream-a-block",\n "session": "' + SID + '",\n "entries": [\n  {\n'
                               '   "path": "docs/a.md",\n   "lines": "5-12",\n   "blob": "' + b + '"\n  },\n  {\n'
                               '   "path": "docs/a.md",\n   "lines": "1-1",\n   "blob": "' + b + '"\n  }\n ]\n}\n').encode())

    def miss(self, path):
        return f"FAIL context.{KIND}.{path}: missing path (not a file under the root)\n"

    def path_of(self, i, line_bytes):
        """A missing path whose FAIL line is exactly `line_bytes` bytes."""
        n = line_bytes - len(self.miss(""))
        path = f"docs/c{i:03d}" + "q" * (n - 12) + ".md"
        self.assertEqual(len(path), n)
        self.assertTrue(12 <= n <= 200, n)
        return path

    def rest(self, k):
        return f"FAIL context.{KIND}.output: {k} further failures not shown\n"

    def test_the_failure_output_cap_is_exact_at_16384_bytes(self):
        """Row O2 (F16e; m0859 m0862 m0863 m0864 m0873, DEC-0172): FAIL lines totalling 16,383 bytes are printed whole,
        16,384 bytes are capped; when capped, a line is shown exactly when the lines so far, it and the count line stay
        under 16,384 bytes."""
        with self.subTest("all lines total 16,383 bytes: printed whole"):
            paths = [self.path_of(i, 229) for i in range(71)]
            last = 16383 - sum(len(self.miss(p)) for p in paths)
            paths.append(self.path_of(71, last))
            want = "".join(self.miss(p) for p in paths)
            self.assertEqual(len(want.encode()), 16383)
            rc, out = self.run_loader(entries=[entry(p, "1-1") for p in paths])
            self.assertEqual((rc, out), (1, want))
        with self.subTest("all lines total 16,384 bytes: capped"):
            paths[-1] = self.path_of(71, last + 1)
            lines = [self.miss(p) for p in paths]
            self.assertEqual(len("".join(lines).encode()), 16384)
            rc, out = self.run_loader(entries=[entry(p, "1-1") for p in paths])
            shown = max(s for s in range(len(lines)) if len("".join(lines[:s]) + self.rest(len(lines) - s)) < 16384)
            self.assertEqual((rc, out), (1, "".join(lines[:shown]) + self.rest(len(lines) - shown)))
            self.assertLess(len(out.encode()), 16384)
        for total in (16383, 16384):
            with self.subTest(f"the shown lines, the next line and the count line total {total} bytes"):
                head = [self.path_of(i, 240) for i in range(67)]
                tail = [self.path_of(100 + i, 240) for i in range(20)]           # 20 or 21 not shown: two digits either way
                n = total - sum(len(self.miss(p)) for p in head) - len(self.rest(20))
                paths = head + [self.path_of(67, n)] + tail
                lines = [self.miss(p) for p in paths]
                rc, out = self.run_loader(entries=[entry(p, "1-1") for p in paths])
                if total == 16383:                     # under 16,384: line 68 is shown
                    want = "".join(lines[:68]) + self.rest(20)
                    self.assertEqual(len(want.encode()), 16383)
                else:                                  # 16,384 is not under it: line 68 is not shown
                    want = "".join(lines[:67]) + self.rest(21)
                self.assertEqual((rc, out), (1, want))

    def test_an_invalid_session_id_never_removes_a_file(self):
        """Row E1/T3 (m0841 m0843): with a session id that is not a lowercase UUID as a whole string, no earlier list is
        looked for, so a file its spelling would name (a `..` path, an empty component) is never removed."""
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        exp = os.path.join(self.root, EXPECTED)
        os.makedirs(os.path.join(exp, SID))
        victims = {"../../x": os.path.join(self.root, ".superpowers/x", KIND + ".json"),
                   SID + "/../../../x": os.path.join(self.root, ".superpowers/x", KIND + ".json"),
                   "": os.path.join(exp, KIND + ".json"),
                   SID + "\n": os.path.join(exp, SID + "\n", KIND + ".json")}
        for sid, victim in victims.items():
            with self.subTest(sid=ascii(sid)):
                os.makedirs(os.path.dirname(victim), exist_ok=True)
                with open(victim, "w") as fh:
                    fh.write("victim\n")
                rc, out, _ = self.call(["--root", self.root, KIND], env={"CLAUDE_CODE_SESSION_ID": sid})
                self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.session: CLAUDE_CODE_SESSION_ID is unset or not a "
                                                "session id (a lowercase UUID): the expected list has no key\n"))
                with open(victim) as fh:
                    self.assertEqual(fh.read(), "victim\n")

    def test_an_earlier_list_that_is_a_symlink_is_removed_not_followed(self):
        """Row T3 (m0202 m0243): the earlier list itself being a symlink is removed (the link, never its target); only a
        symlinked directory component is refused."""
        self.put("docs/a.md", lines_file(3))
        far = self.outside()
        target = os.path.join(far, "victim.json")
        with open(target, "w") as fh:
            fh.write("victim\n")
        listed = os.path.join(self.root, self.LIST)
        os.makedirs(os.path.dirname(listed))
        os.symlink(target, listed)
        rc, out = self.run_loader(entries=[entry("docs/nope.md", "1-3")])
        self.assertEqual((rc, out), (1, self.miss("docs/nope.md")))
        self.assertFalse(os.path.lexists(listed))
        with open(target) as fh:
            self.assertEqual(fh.read(), "victim\n")

    def unlink_dir_error(self):
        """The platform's strerror for unlink() of a directory (EPERM on macOS, EISDIR on Linux)."""
        d = tempfile.mkdtemp(prefix="unlink-probe-")
        self.addCleanup(gf.remove, d)
        with self.assertRaises(OSError) as cm:
            os.unlink(d)
        return cm.exception.strerror

    @unittest.skipIf(os.geteuid() == 0, "root ignores permissions")
    def test_removal_and_write_failures_are_pinned_whole_and_leave_no_temporary_file(self):
        """Rows T2, T3 (m0237 m0261): the whole reason names the OS error text; a failed write leaves no temporary file."""
        self.put("docs/a.md", lines_file(3))
        listed = os.path.join(self.root, self.LIST)
        sid_dir = os.path.dirname(listed)
        self.addCleanup(lambda: os.path.isdir(sid_dir) and os.chmod(sid_dir, 0o755))
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
        self.assertEqual(rc, 0, out)
        os.chmod(sid_dir, 0o555)
        rc, out = self.run_loader(entries=[entry("docs/nope.md", "1-3")])
        os.chmod(sid_dir, 0o755)
        self.assertEqual((rc, out), (1, self.miss("docs/nope.md") + f"FAIL context.{KIND}.{self.LIST}: earlier expected "
                                        "list could not be removed (Permission denied): emptied instead\n"))
        self.reset_context_dir()
        os.makedirs(listed)
        rc, out = self.run_loader(entries=[entry("docs/a.md", "1-3")])
        self.assertEqual((rc, out), (1, f"FAIL context.{KIND}.{self.LIST}: cannot write {self.LIST}: Is a directory\n"
                                        f"FAIL context.{KIND}.{self.LIST}: earlier expected list could not be removed or "
                                        f"emptied ({self.unlink_dir_error()}): it still stands\n"))
        self.assertEqual(os.listdir(sid_dir), [KIND + ".json"])
        self.reset_context_dir()

    def test_git_environment_variables_never_reach_git(self):
        """Row E2 (F16a): every GIT_* variable is removed for every git call, root discovery included; a GIT_OBJECT_DIRECTORY,
        GIT_COMMON_DIR, GIT_INDEX_FILE, GIT_CEILING_DIRECTORIES or GIT_NAMESPACE of the caller changes nothing."""
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3"), entry("docs/a.md", "1-3", ref="HEAD")]))
        gf.commit_all(self.root, "ctx")
        rc, want = self.run_loader(argv=[KIND])
        self.assertEqual(rc, 0, want)
        empty = self.outside()
        os.environ.pop("CLAUDE_PROJECT_DIR", None)
        for env in ({"GIT_OBJECT_DIRECTORY": os.path.join(empty, "objects-none")}, {"GIT_COMMON_DIR": os.path.join(empty, ".git")},
                    {"GIT_INDEX_FILE": os.path.join(empty, "no-index")}, {"GIT_NAMESPACE": "x"},
                    {"GIT_CEILING_DIRECTORIES": self.root}, {"GIT_CONFIG_GLOBAL": os.path.join(empty, "cfg"), "GIT_CONFIG_NOSYSTEM": "1"}):
            for how, argv, cwd in (("--root", ["--root", self.root, KIND], None), ("discovery", [KIND], os.path.join(self.root, "docs"))):
                with self.subTest(env=sorted(env), how=how):
                    rc, out, err = self.call(argv, env=env, cwd=cwd)
                    self.assertEqual((rc, out, err), (0, want, ""))

    def test_the_real_file_imports_this_repository_whatever_pythonpath_holds(self):
        """Row O5b (m0128): `python3 tools/program/load_context.py` puts this repository first on sys.path, so the
        `tools.gates` of another `tools` namespace directory on PYTHONPATH is never imported (this repository's `tools`
        is a namespace package: a regular `tools` package elsewhere shadows it whatever the order, an unrelated finding)."""
        import subprocess
        import sys
        self.put("docs/a.md", lines_file(3))
        self.put(CTX, ctx([entry("docs/a.md", "1-3")]))
        gf.commit_all(self.root, "ctx")
        fake = self.outside()
        gf.write(fake, "tools/gates/__init__.py", "raise SystemExit('the other tools.gates was imported')\n")
        script = os.path.abspath(load_context.__file__)
        for extra in ({}, {"PYTHONSAFEPATH": "1"}):
            with self.subTest(env=sorted(extra)):
                env = dict(os.environ, PYTHONPATH=fake, PYTHONDONTWRITEBYTECODE="1", **extra)
                p = subprocess.run([sys.executable, script, "--root", self.root, KIND], capture_output=True, text=True, env=env)
                self.assertEqual((p.returncode, p.stdout, p.stderr),
                                 (0, f"docs/a.md lines 1-3 blob {blob(lines_file(3).encode())} -- because\n", ""))

    # -- board.py (rows B2, DEC-0187, DEC-0235) ------------------------------------------------------------------------
    def test_board_table_rows_and_cells(self):
        """Row B2 (m0004 m0007 m0008 m0011 m0013 m0014 m0017 m0027 m0028 m0034 m0035 m0040 m0045 m0046 m0047): through
        board.stream_table and board.cells, the functions the loader and the drift check call."""
        from tools.program import board
        for line, want in (("| a | b |", ["a", "b"]), ("|a|b|", ["a", "b"]), ("|a|b", ["a", "b"]), (" | a | b | ", ["a", "b"]),
                           ("|a|b\\|", ["a", "b\\|"]), ("| a \\| b | c |", ["a \\| b", "c"]), ("|", [""]), ("||", [""])):
            with self.subTest(cells=line):
                self.assertEqual(board.cells(line), want)
        tables = (("separator rows are not rows",
                   "x\n| stream | branch |\n|---|:---:|\n| ---x | ---y |\n| A | `a` |\n|:---|---:|\n| B | `b` |\n\n| B | c |\n",
                   (["stream", "branch"], [["---x", "---y"], ["A", "`a`"], ["B", "`b`"]])),
                  ("a table with no separator row", "| stream | branch |\n| A | `a` |\n| B | `b` |\n",
                   (["stream", "branch"], [["A", "`a`"], ["B", "`b`"]])),
                  ("no trailing pipes", "| stream | branch\n|---|---\n| A | `a`\n| B | `b`\nB | after\n",
                   (["stream", "branch"], [["A", "`a`"], ["B", "`b`"]])),
                  ("compact cells", "|stream|branch|\n|---|---|\n|B|`b`|\n", (["stream", "branch"], [["B", "`b`"]])),
                  ("a line not starting with a pipe is not the header", "> stream | note\n- stream | x |\n| stream | branch |\n| B | `b` |\n",
                   (["stream", "branch"], [["B", "`b`"]])))
        for name, text, want in tables:
            with self.subTest(name):
                self.assertEqual(board.stream_table(text), want)
        with self.subTest("no table"):
            with self.assertRaises(board.BoardError):
                board.stream_table("stream | branch |\n")

    def test_board_branch_column_and_cell(self):
        """Row B2 (m0093, F16d): the first branch column counts even when the row is too short for a later one; the
        first backticked name of the cell is the branch."""
        from tools.program import board
        two = "| stream | branch / worktree | notes | branch old |\n|---|---|---|---|\n"
        self.assertEqual(board.stream_branch(two + "| B | `block/x` |\n", "B"), "block/x")
        self.assertEqual(board.stream_branch(two + "| B | `block/x` / `wt-name` | n | `block/y` |\n", "B"), "block/x")
        self.stream_b_branch()
        self.put(self.BOARD_REL, self.board(b_cell="`block/prototype-b9` / `wt-name`"))
        rc, out = self.run_sync(commit=True)
        self.assertEqual(rc, 0, out)
        self.assertTrue(out.startswith(".superpowers/context/refs/origin_block_prototype-b9-"), out)
        with self.subTest("a compact board with no trailing pipes loads through the loader"):
            self.put(self.BOARD_REL, "# board\n|stream|branch / worktree\n|---|---\n|A|`block/program-tooling`\n|B|`block/prototype-b9`\n")
            rc, out = self.run_sync(commit=True)
            self.assertEqual(rc, 0, out)
            self.assertTrue(out.startswith(".superpowers/context/refs/origin_block_prototype-b9-"), out)

    def test_the_first_word_rules_split_on_spaces_only(self):
        """DEC-0235 spaces-only: the branch-column rule takes the header cell's first word as the row-label rule takes
        the row label's, split on a space (U+0020) only; a tab, a vertical tab, a form feed, U+001C-U+001F, U+0085,
        NBSP, U+2003, U+2028 or U+3000 after `branch` (or `B`) joins the word. Whitespace at the cell's ends is stripped."""
        from tools.program import board
        sep = "|---|---|\n"
        no_column = "no branch column (a header cell whose first word is `branch`)"
        for ws in ("\t", "\x0b", "\x0c", "\x1c", "\x1d", "\x1e", "\x1f", "\x85", "\xa0", "\u2003", "\u2028", "\u3000"):
            with self.subTest(header=ascii(ws)):
                with self.assertRaises(board.BoardError) as cm:
                    board.stream_branch(f"| stream | branch{ws}/ worktree |\n" + sep + "| B | `block/x` |\n", "B")
                self.assertEqual(str(cm.exception), no_column)
            with self.subTest(row_label=ascii(ws)):
                with self.assertRaises(board.BoardError) as cm:
                    board.stream_branch("| stream | branch |\n" + sep + f"| B{ws}prototype | `block/x` |\n", "B")
                self.assertEqual(str(cm.exception), "no stream B row")
            with self.subTest(header_end=ascii(ws)):
                self.assertEqual(board.stream_branch(f"| stream | {ws}branch{ws} |\n" + sep + f"| {ws}B{ws} | `block/x` |\n", "B"), "block/x")
        for header in ("branch / worktree", "branch", "branch  two spaces"):
            with self.subTest(header=header):
                self.assertEqual(board.stream_branch(f"| stream | {header} |\n" + sep + "| B prototype | `block/x` |\n", "B"), "block/x")
        with self.subTest("a newline in the header cell ends the header line"):
            with self.assertRaises(board.BoardError) as cm:
                board.stream_branch("| stream | branch\nx / worktree |\n" + sep + "| B | `block/x` |\n", "B")
            self.assertEqual(str(cm.exception), "no stream B row")
        with self.subTest("through the loader: a tab after branch is no branch column"):
            self.stream_b_branch()
            self.put(self.BOARD_REL, self.board().replace("branch / worktree", "branch\t/ worktree"))
            rc, out = self.run_sync(commit=True)
            self.assertEqual((rc, out), (1, f"FAIL context.sync.{B_HANDOFF}: ref board:B: {self.BOARD_REL}: {no_column}\n"))

    # -- the committed read lists ------------------------------------------------------------
    def test_committed_context_yml_has_a_list_for_every_kind_and_dispatch_role(self):
        real = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
        with open(os.path.join(real, CTX), encoding="utf-8") as f:
            lists = yaml.safe_load(f)["lists"]
        conf = config.load(real)
        want = {"stream-a-block", "stream-b-block", "sync"} | set(conf["models"])
        self.assertEqual(set(lists), want)
        for name, entries in lists.items():
            for e in entries:
                with self.subTest(list=name, path=e.get("path")):
                    self.assertLessEqual(set(e), {"path", "lines", "why", "ref"})
                    m = re.fullmatch(r"([1-9][0-9]*)-([1-9][0-9]*|end)", e["lines"])
                    self.assertIsNotNone(m)
                    if m.group(2) != "end":
                        self.assertLessEqual(int(m.group(2)) - int(m.group(1)) + 1, 2000)
                    self.assertTrue(e["why"].strip())


if __name__ == "__main__":
    unittest.main()
