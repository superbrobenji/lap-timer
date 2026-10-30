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
                rc, out = self.run_loader(entries=[entry(bad, "1-1")])
                self.assertEqual(rc, 1, out)
                self.assertEqual(len(out.splitlines()), 1, out)          # never an injected extra line
                self.assertRegex(out, r"(?m)^FAIL context\." + KIND + r"\.[^\n]*: ")
                self.assertIn("non-printable character", out)

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

    def test_any_nonprintable_character_in_path_why_ref_or_lines_is_refused(self):
        self.put("docs/a.md", lines_file(3))
        for ch in self.NONPRINTABLE:
            for field in ("path", "why", "ref", "lines"):
                with self.subTest(char=ascii(ch), field=field):
                    e = entry("docs/a.md", "1-3")
                    if field == "path":
                        e["path"] = f"docs/a{ch}.md"
                    elif field == "why":
                        e["why"] = f"ok{ch}bad"
                    elif field == "ref":
                        e["ref"] = f"HEAD{ch}"
                    else:
                        e["lines"] = f"1-3{ch}"
                    rc, out = self.run_loader(entries=[e])
                    self.assertEqual(rc, 1, out)
                    self.assertEqual(len(out.splitlines()), 1, ascii(out))      # no injected line
                    self.assertRegex(out, rf"^FAIL context\.{KIND}\.[^\n]*: ")
                    self.assertTrue(out.isprintable() or out.endswith("\n") and out[:-1].isprintable(), ascii(out))

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
