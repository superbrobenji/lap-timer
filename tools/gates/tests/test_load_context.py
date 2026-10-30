"""tools/program/load_context.py through main() on fixture repositories (program tooling Task 1).
Run from the repo root: python3 -m unittest tools.gates.tests.test_load_context -v
"""
import contextlib
import hashlib
import io
import json
import os
import re
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
        for e in (entry("README.md", "1-1", ref="--output=x"), entry("README.md", "1-1", ref=""),
                  entry("../outside.md", "1-1"), entry("/etc/hosts", "1-1"), entry("docs/../README.md", "1-1"),
                  entry("../outside.md", "1-1", ref="HEAD")):
            with self.subTest(e=e):
                rc, out = self.run_loader(entries=[e])
                self.assertFailed(rc, out, f"context.{KIND}.{e['path']}")

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
        self.put("docs/a.md", lines_file(5))
        for text in ("---\nplan: [\n---\nbody\n", "---\ncontext_files: 3\n---\n", "---\ncontext_files: [3]\n---\n",
                     "no front matter at all\n"):
            with self.subTest(text=text):
                self.put(A_HANDOFF, text)
                rc, out = self.run_loader(entries=[entry("docs/a.md", "1-5")])
                self.assertEqual(rc, 1, out)
                self.assertRegex(out, rf"(?m)^FAIL context\.{KIND}\.{re.escape(A_HANDOFF)}")

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
        self.assertFailed(rc, out, f"context.{KIND}..git/HEAD", ".git")

    def test_untracked_file_is_refused(self):
        self.put("docs/new.md", lines_file(3))
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
        self.assertRegex(out, rf"(?m)^FAIL context\.{KIND}\.docs/sub: ")

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
        other = os.path.join(self.root, EXPECTED, "other-session-1234", KIND + ".json")
        os.makedirs(os.path.dirname(other))
        with open(other, "w") as fh:
            fh.write("{}\n")
        rc, out = self.run_loader(raw="lists: ~\n")
        self.assertEqual(rc, 1, out)
        self.assertTrue(os.path.exists(os.path.join(self.root, EXPECTED, SID, "sync.json")))
        self.assertTrue(os.path.exists(other))

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
                    m = re.fullmatch(r"([1-9][0-9]*)-([1-9][0-9]*)", e["lines"])
                    self.assertIsNotNone(m)
                    self.assertLessEqual(int(m.group(2)) - int(m.group(1)) + 1, 2000)
                    self.assertTrue(e["why"].strip())


if __name__ == "__main__":
    unittest.main()
