"""tools/program/mutate.py through main() on fixture repositories (program tooling Task 1, fix round 8a, DEC-0234).
Run from the repo root: python3 -m unittest tools.gates.tests.test_mutate -v
"""
import contextlib
import hashlib
import io
import itertools
import json
import os
import re
import shutil
import signal
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

from tools.gates.tests import gitfixture as gf
from tools.program import mutate

PY = sys.executable
MUTATE = os.path.abspath(mutate.__file__)
# Row M-R3 / loader R1, R5 (DEC-0294 as-built-with-rows): every character other than " \t\n\r\v\f" that str.isspace()
# accepts (the same 23 on 3.9 and 3.12), each alone, and a mixed value
UNICODE_SPACES = ("\x1c", "\x1d", "\x1e", "\x1f", "\x85", "\xa0", "\u1680", "\u2000", "\u2001", "\u2002", "\u2003",
                  "\u2004", "\u2005", "\u2006", "\u2007", "\u2008", "\u2009", "\u200a", "\u2028", "\u2029", "\u202f",
                  "\u205f", "\u3000")

MOD_BASE = '''"""m"""
UNCHANGED = 1 == 2
import re

def g(a, b):
    if (a and
            b):
        return 0
    return 1
TAIL = 3 > 2
'''

MOD_HEAD = '''"""m"""
UNCHANGED = 1 == 2
import re

def g(a, b):
    if (a and
            not b):
        return 0
    return 1
def f(a, b, xs, s):
    c1 = a == b
    c2 = a != b
    c3 = a < b < s
    c4 = a <= b
    c5 = a > b
    c6 = a >= b
    c7 = a in xs
    c8 = a not in xs
    c9 = a is b
    c10 = a is not b
    c11 = a and b or s
    c13 = not a
    c14 = True
    c15 = False
    c16 = a + 7
    c17 = re.fullmatch(s, s)
    c18 = re.match(s, s)
    c19 = s.startswith(s)
    c20 = s.strip()
    c21 = "x"
    c22 = ""
    c23 = f"{a == b}"
    print(c1, c2)
    x = a; print(x)
    while a:
        break
    for x in xs:
        continue
    if b:
        raise ValueError
    elif a:
        pass
    while True:
        break
    assert a
    c24 = [x for x in xs if x]
    c25 = a if b else s
    c26 = (a ==
           b)
    if s:
        return None
    return c1
TAIL = 3 > 2
'''

# (line, operator, old, new) for MOD_HEAD: every operator on every changed line it matches, in id order
COND6 = "a and\n            not b"
MOD_MUTANTS = [
    (6, "not-add", COND6, "not (" + COND6 + ")"), (6, "cond-true", COND6, "True"), (6, "cond-false", COND6, "False"),
    (7, "not-remove", "not", ""),
    (11, "cmp-eq-ne", "==", "!="), (12, "cmp-ne-eq", "!=", "=="),
    (13, "cmp-lt-le", "<", "<="), (13, "cmp-lt-le", "<", "<="),
    (14, "cmp-le-lt", "<=", "<"), (15, "cmp-gt-ge", ">", ">="), (16, "cmp-ge-gt", ">=", ">"),
    (17, "cmp-in-notin", "in", "not in"), (18, "cmp-notin-in", "not in", "in"),
    (19, "cmp-is-isnot", "is", "is not"), (20, "cmp-isnot-is", "is not", "is"),
    (21, "and-or", "and", "or"), (21, "or-and", "or", "and"),
    (22, "not-remove", "not", ""),
    (23, "true-false", "True", "False"), (24, "false-true", "False", "True"),
    (25, "int-inc", "7", "8"), (25, "int-dec", "7", "6"),
    (26, "fullmatch-match", "fullmatch", "match"), (27, "match-search", "match", "search"),
    (28, "startswith-endswith", "startswith", "endswith"), (29, "strip-rstrip", "strip", "rstrip"),
    (30, "str-empty", '"x"', '""'), (32, "str-empty", 'f"{a == b}"', '""'),
    (33, "stmt-pass", "print(c1, c2)", "pass"),
    (35, "not-add", "a", "not (a)"), (35, "cond-true", "a", "True"), (35, "cond-false", "a", "False"),
    (36, "stmt-pass", "break", "pass"), (38, "stmt-pass", "continue", "pass"),
    (39, "not-add", "b", "not (b)"), (39, "cond-true", "b", "True"), (39, "cond-false", "b", "False"),
    (40, "stmt-pass", "raise ValueError", "pass"),
    (41, "not-add", "a", "not (a)"), (41, "cond-true", "a", "True"), (41, "cond-false", "a", "False"),
    (43, "not-add", "True", "not (True)"), (43, "true-false", "True", "False"),
    (43, "cond-true", "True", "True"), (43, "cond-false", "True", "False"),
    (44, "stmt-pass", "break", "pass"),
    (45, "not-add", "a", "not (a)"),
    (46, "not-add", "x", "not (x)"),
    (47, "not-add", "b", "not (b)"),
    (48, "cmp-eq-ne", "==", "!="),
    (50, "not-add", "s", "not (s)"), (50, "cond-true", "s", "True"), (50, "cond-false", "s", "False"),
    (51, "return-none", "None", "None"),
    (52, "return-none", "c1", "None"),
]
BADMATCH = {(43, "cond-true"), (51, "return-none")}         # the operator finds no text to change

DOC_HEAD = '''"""module doc"""
A = "a"


class C:
    """class doc"""
    B = "b"

    def m(self):
        """method doc"""
        "not a docstring"
        return "r"


def f():
    'single quoted doc'
    if A:
        "first in an if body"
    return 1


async def g():
    """async doc"""


def h():
    f"{A}"
    x = "y"


def k():
    ("implicit" "concat")
    return x
'''
# (id, line, operator, old) for DOC_HEAD: the docstrings (m0001 m0003 m0005 m0009 m0017 m0020) are not mutated and the
# ids of the other mutants are the ones they had when docstrings were mutated (DEC-0246, DEC-0247)
DOC_MUTANTS = [
    ("m0002", 2, "str-empty", '"a"'), ("m0004", 7, "str-empty", '"b"'), ("m0006", 11, "str-empty", '"not a docstring"'),
    ("m0007", 12, "return-none", '"r"'), ("m0008", 12, "str-empty", '"r"'),
    ("m0010", 17, "not-add", "A"), ("m0011", 17, "cond-true", "A"), ("m0012", 17, "cond-false", "A"),
    ("m0013", 18, "str-empty", '"first in an if body"'),
    ("m0014", 19, "int-inc", "1"), ("m0015", 19, "int-dec", "1"), ("m0016", 19, "return-none", "1"),
    ("m0018", 27, "str-empty", 'f"{A}"'), ("m0019", 28, "str-empty", '"y"'), ("m0021", 33, "return-none", "x"),
]

NOTHING_TEST = "import unittest\n\n\nclass T(unittest.TestCase):\n    def test_nothing(self):\n        pass\n"
KILL_HEAD = '"""k"""\n\n\ndef f(a):\n    return a > 1\n'
KILL_TEST = '''import unittest

from pkg import kill


class A(unittest.TestCase):
    def test_two_is_over(self):
        self.assertIs(kill.f(2), True)


class B(unittest.TestCase):
    def test_one_is_not_over(self):
        self.assertIs(kill.f(1), False)
'''
SPIN_HEAD = '"""s"""\n\n\ndef spin(n):\n    while n > 0:\n        n -= 1\n    return n\n'
SPIN_TEST = '''import unittest

from pkg import spin


class S(unittest.TestCase):
    def test_spin(self):
        self.assertEqual(spin.spin(3), 0)
'''
LOCKS_PY = '''import os
import stat
import tempfile

SHAPES = {"unlistable": ("d", (("d", 0o333),)), "unwritable": ("d", (("d/f", 0o444), ("d", 0o555))),
          "unsearchable": ("d", (("d", 0o666),)), "nested": ("a/b", (("a/b", 0o000),)),
          "inner": ("d/e", (("d/e", 0o000), ("d", 0o333)))}


def lock_tree(parent=None, shapes=None, outside=None):
    """Row M-X9: leave, under `parent` (default the run's TMPDIR), a tree of each shape whose modes block its
    removal, and a symlink to `outside` (default MUTATE_TEST_OUTSIDE), a directory beside the copy."""
    names = shapes or os.environ.get("MUTATE_TEST_SHAPES", ",".join([*SHAPES, "link"])).split(",")
    for shape in names:
        top = tempfile.mkdtemp(prefix=f"lock-{shape}-", dir=parent)
        if shape == "link":
            os.symlink(outside or os.environ["MUTATE_TEST_OUTSIDE"], os.path.join(top, "link"))
            continue
        if shape == "uchg":                 # row M-X9b: a file no restore of the modes makes removable
            os.mkdir(os.path.join(top, "d"))
            open(os.path.join(top, "d", "f"), "w").close()
            os.chflags(os.path.join(top, "d", "f"), stat.UF_IMMUTABLE)
            continue
        deep, modes = SHAPES[shape]
        os.makedirs(os.path.join(top, deep))
        open(os.path.join(top, deep, "f"), "w").close()
        for rel, mode in modes:
            os.chmod(os.path.join(top, rel), mode)
'''
LOCK_TEST = '''import os
import time
import unittest

from pkg import kill, locks


class A(unittest.TestCase):
    def test_two_is_over(self):
        locks.lock_tree()
        if kill.f(2) is not True and os.environ.get("MUTATE_TEST_HANG"):
            time.sleep(300)
        self.assertIs(kill.f(2), True)
'''
HANG_TEST = '''import os
import subprocess
import time
import unittest

from pkg import kill, locks

with open(os.environ["MUTATE_TEST_RUNLOG"], "a", encoding="utf-8") as log:
    log.write(f"{os.getpid()} {os.getpgrp()}\\n")
locks.lock_tree()                       # row M-X9: every run leaves a tree that blocks a plain removal


def hang():
    child = subprocess.Popen(["sleep", "300"])
    with open(os.environ["MUTATE_TEST_HANGLOG"], "a", encoding="utf-8") as fh:
        fh.write(f"{os.getpid()} {os.getpgrp()} {child.pid}\\n")
    while True:
        time.sleep(3600)


class A(unittest.TestCase):
    def test_two_is_over(self):
        if kill.f(2) is not True:
            hang()
        self.assertIs(kill.f(2), True)


class Z(unittest.TestCase):
    def test_hangs(self):
        hang()
'''
TMP_TEST = '''import json
import os
import tempfile
import time
import unittest

from pkg import kill


class A(unittest.TestCase):
    def test_two_is_over(self):
        made = tempfile.mkdtemp(prefix="gate-test-")
        with open(os.environ["MUTATE_TEST_TMPLOG"], "a", encoding="utf-8") as fh:
            fh.write(json.dumps([made, os.environ.get("TMPDIR"), os.environ.get("TMP"), os.environ.get("TEMP")]) + "\\n")
        if kill.f(2) is not True:
            time.sleep(300)
        self.assertIs(kill.f(2), True)
        os.rmdir(made)
'''
GITENV_TEST = '''import os
import subprocess
import unittest


class G(unittest.TestCase):
    def test_git_sees_the_copy(self):
        self.assertEqual(sorted(k for k in os.environ if k.startswith("GIT_")), [])
        top = subprocess.run(["git", "rev-parse", "--show-toplevel"], capture_output=True, text=True, check=True)
        self.assertEqual(os.path.realpath(top.stdout[:-1]), os.path.realpath(os.getcwd()))
'''
ALL_OPERATORS = ["cmp-eq-ne", "cmp-ne-eq", "cmp-lt-le", "cmp-le-lt", "cmp-gt-ge", "cmp-ge-gt", "cmp-in-notin",
                 "cmp-notin-in", "cmp-is-isnot", "cmp-isnot-is", "not-remove", "not-add", "and-or", "or-and",
                 "true-false", "false-true", "int-inc", "int-dec", "cond-true", "cond-false", "return-none",
                 "stmt-pass", "fullmatch-match", "match-search", "startswith-endswith", "strip-rstrip", "str-empty"]


class Mutate(unittest.TestCase):
    def setUp(self):
        self.root = gf.new_repo()
        self.addCleanup(gf.remove, self.root)
        gf.write(self.root, "pkg/__init__.py", "")
        gf.write(self.root, "pkg/mod.py", MOD_BASE)
        gf.write(self.root, "pkg/kill.py", '"""k"""\n')
        gf.write(self.root, "pkg/spin.py", '"""s"""\n')
        gf.write(self.root, "pkg/test_mod.py", NOTHING_TEST)
        gf.write(self.root, "pkg/test_kill.py", KILL_TEST)
        gf.write(self.root, "pkg/test_spin.py", SPIN_TEST)
        gf.write(self.root, "pkg/empty.py", "")
        self.base = gf.commit_all(self.root, "base")
        gf.write(self.root, "pkg/mod.py", MOD_HEAD)
        gf.write(self.root, "pkg/kill.py", KILL_HEAD)
        gf.write(self.root, "pkg/spin.py", SPIN_HEAD)
        gf.write(self.root, "pkg/doc.py", DOC_HEAD)
        self.head = gf.commit_all(self.root, "head")
        self.scratch = tempfile.mkdtemp(prefix="mutate-test-")
        self.addCleanup(force_remove, self.scratch)

    def call(self, argv):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            rc = mutate.main(argv)
        return rc, out.getvalue()

    def args(self, files=("pkg/kill.py",), tests=("pkg.test_kill.A",), pys=(PY,), extra=()):
        argv = ["--root", self.root, "--base", self.base, "--head", self.head]
        for f in files:
            argv += ["--file", f]
        for t in tests:
            argv += ["--test", t]
        for p in pys:
            argv += ["--py", p]
        return argv + list(extra)

    def run_json(self, argv):
        rc, out = self.call(argv + ["--json"])
        return rc, json.loads(out)

    def wrapper_py(self, name="py2"):
        path = os.path.join(self.scratch, name)
        with open(path, "w", encoding="utf-8") as fh:
            fh.write(f'#!/bin/sh\nexec "{PY}" "$@"\n')
        os.chmod(path, 0o755)
        return path

    def assert_checkout_untouched(self):
        self.assertEqual(gf.git(self.root, "status", "--porcelain", "--untracked-files=all", "--ignored"), "")
        self.assertEqual(gf.git(self.root, "rev-parse", "HEAD"), self.head)

    # --- operators and sites -------------------------------------------------------------------------------

    def test_list_operators_prints_the_constant(self):
        rc, out = self.call(["--list-operators"])
        self.assertEqual(rc, 0)
        self.assertEqual([l.split(" -- ")[0] for l in out.splitlines()], ALL_OPERATORS)
        self.assertEqual([name for name, _ in mutate.OPERATORS], ALL_OPERATORS)
        self.assertTrue(all(l.split(" -- ")[1] for l in out.splitlines()))
        self.assertIn('str-empty -- a non-empty string literal (an f-string included) to "", except a docstring', out)

    def test_every_operator_applies_to_every_changed_site_it_matches(self):
        rc, doc = self.run_json(self.args(files=["pkg/mod.py"], tests=["pkg.test_mod"], extra=["--jobs", "4"]))
        got = [(m["line"], m["operator"], m["old"], m["new"]) for m in doc["mutants"]]
        self.assertEqual(got, MOD_MUTANTS)
        self.assertEqual([m["id"] for m in doc["mutants"]], [f"m{i:04d}" for i in range(1, len(MOD_MUTANTS) + 1)])
        for m in doc["mutants"]:
            want = "BADMATCH" if (m["line"], m["operator"]) in BADMATCH else "SURVIVED"
            self.assertEqual(m["results"], {PY: want}, m)
        self.assertEqual(rc, 1)
        self.assertEqual(doc["rc"], 1)
        self.assert_checkout_untouched()

    def test_every_operator_has_a_fixture_site(self):
        self.assertEqual(sorted({op for _, op, _, _ in MOD_MUTANTS}), sorted(ALL_OPERATORS))

    def test_only_changed_lines_are_sites(self):
        rc, doc = self.run_json(self.args(files=["pkg/mod.py"], tests=["pkg.test_mod"], extra=["--jobs", "4"]))
        lines = {m["line"] for m in doc["mutants"]}
        self.assertTrue(lines.isdisjoint({1, 2, 3, 5, 8, 9, 53}), lines)
        changed = set(range(10, 53)) | {7}
        for m in doc["mutants"]:
            span = set(range(m["line"], m["line"] + m["old"].count("\n") + 1))
            self.assertTrue(span & changed, m)

    # --- results ----------------------------------------------------------------------------------------------

    def test_text_output_and_rc_one_when_a_mutant_survives(self):
        rc, out = self.call(self.args())
        self.assertEqual(rc, 1)
        lines = out.splitlines()
        self.assertEqual(lines[:4], [f"m0001 pkg/kill.py:5 return-none KILLED {PY}",
                                     f"m0002 pkg/kill.py:5 cmp-gt-ge SURVIVED {PY}",
                                     f"m0003 pkg/kill.py:5 int-inc KILLED {PY}",
                                     f"m0004 pkg/kill.py:5 int-dec SURVIVED {PY}"])
        self.assertEqual(lines[4:], [f"summary: base {self.base} head {self.head}, 4 mutants in 1 file, 1 test, 1 interpreter",
                                     f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH",
                                     "SURVIVED m0002 pkg/kill.py:5:14 cmp-gt-ge '>' -> '>='",
                                     "SURVIVED m0004 pkg/kill.py:5:16 int-dec '1' -> '0'"])
        self.assert_checkout_untouched()

    def test_every_test_runs_and_rc_zero_when_all_are_killed(self):
        rc, out = self.call(self.args(tests=["pkg.test_kill.A", "pkg.test_kill.B"]))
        self.assertEqual(rc, 0, out)
        self.assertEqual([l.split()[3] for l in out.splitlines()[:4]], ["KILLED"] * 4)
        self.assertIn(f"summary {PY}: 4 KILLED, 0 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_each_mutant_runs_under_each_interpreter(self):
        py2 = self.wrapper_py()
        rc, out = self.call(self.args(pys=[PY, py2]))
        self.assertEqual(rc, 1)
        lines = out.splitlines()
        self.assertEqual(lines[:8], [f"m0001 pkg/kill.py:5 return-none KILLED {PY}",
                                     f"m0001 pkg/kill.py:5 return-none KILLED {py2}",
                                     f"m0002 pkg/kill.py:5 cmp-gt-ge SURVIVED {PY}",
                                     f"m0002 pkg/kill.py:5 cmp-gt-ge SURVIVED {py2}",
                                     f"m0003 pkg/kill.py:5 int-inc KILLED {PY}",
                                     f"m0003 pkg/kill.py:5 int-inc KILLED {py2}",
                                     f"m0004 pkg/kill.py:5 int-dec SURVIVED {PY}",
                                     f"m0004 pkg/kill.py:5 int-dec SURVIVED {py2}"])
        self.assertIn(f"summary {py2}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", lines)

    def test_a_survivor_on_one_interpreter_only_gives_rc_one(self):
        py2 = os.path.join(self.scratch, "py-fails")
        with open(py2, "w", encoding="utf-8") as fh:        # exits 1 whenever the mutated source is in the tree
            fh.write(f'#!/bin/sh\nif grep -q "a >= 1" pkg/kill.py; then exit 1; fi\nexec "{PY}" "$@"\n')
        os.chmod(py2, 0o755)
        rc, doc = self.run_json(self.args(tests=["pkg.test_kill.A", "pkg.test_kill.B"], pys=[py2, PY]))
        self.assertEqual(rc, 0, doc)
        rc, doc = self.run_json(self.args(pys=[py2, PY]))
        res = {m["operator"]: m["results"] for m in doc["mutants"]}
        self.assertEqual(res["cmp-gt-ge"], {py2: "KILLED", PY: "SURVIVED"})
        self.assertEqual(doc["summary"], {py2: {"KILLED": 3, "SURVIVED": 1, "BADMATCH": 0},
                                          PY: {"KILLED": 2, "SURVIVED": 2, "BADMATCH": 0}})
        self.assertEqual(rc, 1)

    def test_ids_are_stable_across_runs_and_file_order(self):
        a = self.run_json(self.args(files=["pkg/mod.py", "pkg/kill.py"], tests=["pkg.test_mod"], extra=["--jobs", "4"]))[1]
        b = self.run_json(self.args(files=["pkg/kill.py", "pkg/mod.py"], tests=["pkg.test_mod"], extra=["--jobs", "2"]))[1]
        key = [(m["id"], m["file"], m["line"], m["col"], m["operator"], m["old"], m["new"]) for m in a["mutants"]]
        self.assertEqual(key, [(m["id"], m["file"], m["line"], m["col"], m["operator"], m["old"], m["new"])
                               for m in b["mutants"]])
        self.assertEqual([m["file"] for m in a["mutants"][:4]], ["pkg/kill.py"] * 4)
        self.assertEqual(a["mutants"][4]["id"], "m0005")
        self.assertEqual(a["mutants"][4]["file"], "pkg/mod.py")

    def test_docstrings_are_not_mutated_and_the_other_ids_keep_their_numbers(self):
        rc, doc = self.run_json(self.args(files=["pkg/doc.py"], tests=["pkg.test_mod"], extra=["--jobs", "4"]))
        self.assertEqual([(m["id"], m["line"], m["operator"], m["old"]) for m in doc["mutants"]], DOC_MUTANTS)
        self.assertEqual(rc, 1)

    def test_ids_select_a_range_or_one_id(self):
        rc, out = self.call(self.args(extra=["--ids", "m0002-m0003"]))
        self.assertEqual(rc, 1)
        self.assertEqual(out.splitlines(), [
            f"m0002 pkg/kill.py:5 cmp-gt-ge SURVIVED {PY}",
            f"m0003 pkg/kill.py:5 int-inc KILLED {PY}",
            f"summary: base {self.base} head {self.head}, ids m0002-m0003, 2 mutants in 1 file, 1 test, 1 interpreter",
            f"summary {PY}: 1 KILLED, 1 SURVIVED, 0 BADMATCH",
            "SURVIVED m0002 pkg/kill.py:5:14 cmp-gt-ge '>' -> '>='"])
        rc, out = self.call(self.args(extra=["--ids", "m0003"]))
        self.assertEqual((rc, out.splitlines()[0]), (0, f"m0003 pkg/kill.py:5 int-inc KILLED {PY}"))
        self.assertEqual(len(out.splitlines()), 3)
        full = self.run_json(self.args(files=["pkg/doc.py"], tests=["pkg.test_mod"], extra=["--jobs", "4"]))[1]
        part = self.run_json(self.args(files=["pkg/doc.py"], tests=["pkg.test_mod"], extra=["--ids", "m0001-m0010"]))[1]
        self.assertEqual(part["mutants"], [m for m in full["mutants"] if m["id"] <= "m0010"])
        self.assertEqual([m["id"] for m in part["mutants"]], ["m0002", "m0004", "m0006", "m0007", "m0008", "m0010"])
        self.assertEqual(part["ids"], "m0001-m0010")
        self.assertIsNone(full["ids"])
        self.assert_checkout_untouched()

    def test_a_bad_id_spec_is_a_named_fail(self):
        doc = ["--root", self.root, "--base", self.base, "--head", self.head, "--file", "pkg/doc.py", "--test",
               "pkg.test_mod", "--py", PY]
        shape = "is not an id m<nnnn> or a range m<nnnn>-m<nnnn> as the tool prints them"
        cases = [(self.args(extra=[f"--ids={spec}"]), f"FAIL mutate.ids: --ids {spec!r} {shape}")
                 for spec in ("", "m2", "m00002", "0002", "M0002", "m0002-", "-m0002", " m0002", "m0002 ",
                              "m0001,m0002", "m0001-m0002-m0003", "m0001-0002", "m0001--m0002", "m\u0662\u0662\u0662\u0662")]
        cases += [
            (self.args(extra=["--ids", "m0003-m0002"]), "FAIL mutate.ids: --ids 'm0003-m0002' ends before it starts"),
            (self.args(extra=["--ids", "m0000"]), "FAIL mutate.ids: --ids 'm0000' is outside m0001-m0004"),
            (self.args(extra=["--ids", "m0005"]), "FAIL mutate.ids: --ids 'm0005' is outside m0001-m0004"),
            (self.args(extra=["--ids", "m0000-m0004"]), "FAIL mutate.ids: --ids 'm0000-m0004' is outside m0001-m0004"),
            (self.args(extra=["--ids", "m0001-m0005"]), "FAIL mutate.ids: --ids 'm0001-m0005' is outside m0001-m0004"),
            (doc + ["--ids", "m0022"], "FAIL mutate.ids: --ids 'm0022' is outside m0001-m0021"),
            (doc + ["--ids", "m0020"], "FAIL mutate.ids: --ids 'm0020' selects no mutant: every id in it is a docstring"),
            (doc + ["--ids", "m0001"], "FAIL mutate.ids: --ids 'm0001' selects no mutant: every id in it is a docstring"),
            (self.args(extra=["--ids", "m0001", "--ids", "m0002"]), "FAIL mutate.args: --ids is given more than once"),
            (self.args(extra=["--ids", "m0001", "--ids", "m0001"]), "FAIL mutate.args: --ids is given more than once"),
            (self.args(extra=["--root", self.root]), "FAIL mutate.args: --root is given more than once"),
            (self.args(extra=["--base", self.base]), "FAIL mutate.args: --base is given more than once"),
            (self.args(extra=["--head", self.head]), "FAIL mutate.args: --head is given more than once"),
            (self.args(extra=["--jobs", "2", "--jobs", "3"]), "FAIL mutate.args: --jobs is given more than once"),
            (self.args(extra=["--ids", "-m0002"]), "FAIL mutate.args: bad arguments: argument --ids: expected one argument"),
            (["--list-operators", "--ids", "m0001"], "FAIL mutate.args: --list-operators takes no other argument"),
        ]
        for argv, want in cases:
            with self.subTest(argv=argv):
                self.assertEqual(self.call(argv), (2, want + "\n"))
        rc, doc_json = self.run_json(self.args(extra=["--ids", "m9"]))
        self.assertEqual((rc, doc_json), (2, {"rc": 2, "fail": [{"check": "mutate.ids", "reason": f"--ids 'm9' {shape}"}]}))
        self.assert_checkout_untouched()

    def test_a_hung_mutant_is_killed_by_the_timeout(self):
        with mock.patch.object(mutate, "TIMEOUT_FLOOR", 3.0), mock.patch.object(mutate, "TIMEOUT_FACTOR", 1.0):
            rc, doc = self.run_json(self.args(files=["pkg/spin.py"], tests=["pkg.test_spin"]))
        res = {(m["line"], m["operator"], m["new"]): (m["results"][PY], m["reason"][PY]) for m in doc["mutants"]}
        self.assertEqual(res[(5, "cond-true", "True")], ("KILLED", "timeout"))
        self.assertEqual(res[(6, "int-dec", "0")], ("KILLED", "timeout"))
        self.assertEqual(res[(5, "cmp-gt-ge", ">=")], ("KILLED", "tests failed (rc 1)"))
        self.assertEqual(rc, 0, doc)

    def test_a_timeout_kills_the_whole_test_process_group(self):
        pidfile = os.path.join(self.scratch, "child.pid")
        gf.write(self.root, "pkg/test_orphan.py", KILL_TEST.replace(
            "        self.assertIs(kill.f(2), True)",
            "        if kill.f(2) is not True:\n"
            "            import os, subprocess\n"
            "            child = subprocess.Popen(['sleep', '300'])\n"
            "            open(os.environ['MUTATE_TEST_PIDFILE'], 'w').write(str(child.pid))\n"
            "            while True:\n"
            "                pass\n"
            "        self.assertIs(kill.f(2), True)"))
        self.head = gf.commit_all(self.root, "orphan")
        with mock.patch.object(mutate, "TIMEOUT_FLOOR", 3.0), mock.patch.object(mutate, "TIMEOUT_FACTOR", 1.0), \
                mock.patch.dict(os.environ, {"MUTATE_TEST_PIDFILE": pidfile}):
            rc, doc = self.run_json(self.args(tests=["pkg.test_orphan.A"]))
        self.assertEqual(doc["mutants"][0]["reason"], {PY: "timeout"})
        with open(pidfile, encoding="utf-8") as fh:
            pid = int(fh.read())
        alive = True
        for _ in range(50):                 # the kill is delivered; give the kernel a moment to reap it
            try:
                os.kill(pid, 0)
            except ProcessLookupError:
                alive = False
                break
            time.sleep(0.1)
        self.assertFalse(alive, f"sleep {pid} outlived the timed-out test run")
        self.assertEqual(rc, 1)

    # --- the scratch copy -------------------------------------------------------------------------------------

    def test_the_copy_is_never_the_checkout(self):
        inside = os.path.join(self.root, "sub")
        os.mkdir(inside)
        for name, target in (("the root", self.root), ("inside the root", inside),
                             ("the root through a symlink", self.symlink_to(self.root))):
            with self.subTest(name), mock.patch.object(mutate.tempfile, "mkdtemp", return_value=target):
                rc, out = self.call(self.args())
                self.assertEqual(rc, 2)
                self.assertEqual(out, f"FAIL mutate.guard: the scratch copy {target!r} is the root or inside it\n")
        self.assertFalse(os.path.exists(inside))        # round 9a (M-E1): the refused, empty scratch directory is removed
        self.assert_checkout_untouched()

    def symlink_to(self, target):
        link = os.path.join(self.scratch, "link")
        os.symlink(target, link)
        return link

    def test_a_copy_that_is_not_its_own_work_tree_top_is_refused(self):
        real_run = mutate.git

        def fake(root, *args, **kw):            # the clone "succeeds" but leaves no repository of its own
            if args[:1] == ("clone",):
                os.makedirs(args[-1])
                return b""
            return real_run(root, *args, **kw)
        nested = os.path.join(self.scratch, "nested")
        os.mkdir(nested)
        gf.git(nested, "init", "-q")
        bare = os.path.join(self.scratch, "bare")
        os.mkdir(bare)
        for where, top in ((nested, r"'.*/nested'"), (bare, "'none'")):
            with self.subTest(where), mock.patch.object(mutate, "git", fake), \
                    mock.patch.object(mutate.tempfile, "mkdtemp", return_value=os.path.join(where, "s")):
                os.mkdir(os.path.join(where, "s"))
                rc, out = self.call(self.args())
                self.assertEqual(rc, 2)
                self.assertRegex(out, r"\AFAIL mutate.guard: the scratch copy '.*/s/tree' is not its own work-tree top"
                                      r" \(git toplevel " + top + r"\)\n\Z")
                self.assertFalse(os.path.exists(os.path.join(where, "s")))

    def test_scratch_directories_are_removed(self):
        made = []
        real = tempfile.mkdtemp

        def record(*a, **kw):
            made.append(real(*a, **kw))
            return made[-1]
        with mock.patch.object(mutate.tempfile, "mkdtemp", record):
            rc, _ = self.call(self.args(extra=["--jobs", "2"]))
        self.assertEqual(rc, 1)
        self.assertEqual(len(made), 2)
        self.assertEqual([p for p in made if os.path.exists(p)], [])

    def test_a_test_that_dirties_the_copy_is_a_named_fail(self):
        gf.write(self.root, "pkg/test_dirty.py", KILL_TEST.replace(
            "        self.assertIs(kill.f(2), True)",
            "        if kill.f(2) is not True:\n            open('stray.txt', 'w').close()\n        self.assertIs(kill.f(2), True)"))
        self.head = gf.commit_all(self.root, "dirty")
        rc, out = self.call(self.args(tests=["pkg.test_dirty.A"]))
        self.assertEqual(rc, 2)
        self.assertEqual(out, "FAIL mutate.copy: m0001 left the copy changed: ?? stray.txt\n")
        self.assert_checkout_untouched()

    def test_the_file_is_restored_after_a_test_overwrites_it(self):
        gf.write(self.root, "pkg/test_clobber.py", KILL_TEST.replace(
            "        self.assertIs(kill.f(2), True)",
            "        if kill.f(2) is not True:\n            open(kill.__file__, 'w').write('junk')\n"
            "        self.assertIs(kill.f(2), True)"))
        self.head = gf.commit_all(self.root, "clobber")
        rc, doc = self.run_json(self.args(tests=["pkg.test_clobber.A"]))
        self.assertEqual([m["results"][PY] for m in doc["mutants"]], ["KILLED", "SURVIVED", "KILLED", "SURVIVED"])
        self.assertEqual(rc, 1)

    def test_a_failed_restore_is_a_named_fail(self):
        gf.write(self.root, "pkg/test_lock.py", KILL_TEST.replace(
            "        self.assertIs(kill.f(2), True)",
            "        if kill.f(2) is not True:\n            import os; os.chmod(kill.__file__, 0o444)\n"
            "        self.assertIs(kill.f(2), True)"))
        self.head = gf.commit_all(self.root, "lock")
        if os.geteuid() == 0:
            self.skipTest("root ignores file modes")
        rc, out = self.call(self.args(tests=["pkg.test_lock.A"]))
        self.assertEqual(rc, 2)
        self.assertRegex(out, r"\AFAIL mutate.restore: m0001 pkg/kill.py could not be restored \(.*Permission denied.*\)\n\Z")

    def test_a_restore_that_differs_is_a_named_fail(self):
        real = mutate.write_file
        calls = []

        def short(path, data):
            calls.append(data)
            real(path, data[:-1] if len(calls) == 2 else data)     # the second write is the first restore
        with mock.patch.object(mutate, "write_file", short):
            rc, out = self.call(self.args())
        self.assertEqual(rc, 2)
        self.assertEqual(out, "FAIL mutate.restore: m0001 pkg/kill.py differs from its original after the restore\n")

    # --- inputs outside the rules ---------------------------------------------------------------------------

    def test_inputs_outside_the_rules_are_named_fails_with_rc_two(self):
        self.maxDiff = None
        notexec = os.path.join(self.scratch, "notexec")
        open(notexec, "w").close()
        os.chmod(notexec, 0o644)
        missing_py = os.path.join(self.scratch, "nope")
        gf.write(self.root, "pkg/bad.py", "def (:\n")
        gf.write(self.root, "pkg/latin.py", "")
        gf.write(self.root, "pkg/cr.py", "")
        gf.write(self.root, "notes.txt", "x\n")
        base2 = gf.commit_all(self.root, "more")
        gf.write(self.root, "pkg/bad.py", "def (:\n    pass\n")
        with open(os.path.join(self.root, "pkg/latin.py"), "wb") as fh:
            fh.write(b"X = '\xe9'\n")
        with open(os.path.join(self.root, "pkg/cr.py"), "wb") as fh:
            fh.write(b"X = 1\r\nY = 2\r\n")
        os.symlink("kill.py", os.path.join(self.root, "pkg/link.py"))
        gf.write(self.root, "notes.txt", "y\n")
        head2 = gf.commit_all(self.root, "head2")
        r, b, h = self.root, self.base, self.head
        ok = ["--file", "pkg/kill.py", "--test", "pkg.test_kill.A", "--py", PY]
        rng = ["--root", r, "--base", b, "--head", h]
        rng2 = ["--root", r, "--base", base2, "--head", head2]
        cases = [
            ([], "FAIL mutate.args: --root is missing"),
            (["--root", r, "--head", h] + ok, "FAIL mutate.args: --base is missing"),
            (["--root", r, "--base", b] + ok, "FAIL mutate.args: --head is missing"),
            (rng + ["--test", "pkg.test_kill.A", "--py", PY], "FAIL mutate.args: no --file"),
            (rng + ["--file", "pkg/kill.py", "--py", PY], "FAIL mutate.args: no --test"),
            (rng + ["--file", "pkg/kill.py", "--test", "pkg.test_kill.A"], "FAIL mutate.args: no --py"),
            (rng + ok + ["--bogus"], "FAIL mutate.args: bad arguments: unrecognized arguments: --bogus"),
            (rng + ok + ["--file", "pkg/kill.py"], "FAIL mutate.args: --file 'pkg/kill.py' is given twice"),
            (rng + ok + ["--test", "pkg.test_kill.A"], "FAIL mutate.args: --test 'pkg.test_kill.A' is given twice"),
            (rng + ok + ["--py", PY], f"FAIL mutate.args: --py {PY!r} is given twice"),
            (rng + ok + ["--jobs", "0"], "FAIL mutate.args: --jobs '0' is not an integer from 1 to 64"),
            (rng + ok + ["--jobs", "65"], "FAIL mutate.args: --jobs '65' is not an integer from 1 to 64"),
            (rng + ok + ["--jobs", "x"], "FAIL mutate.args: --jobs 'x' is not an integer from 1 to 64"),
            (["--list-operators", "--root", r], "FAIL mutate.args: --list-operators takes no other argument"),
            (["--list-operators", "--list-operators"], "FAIL mutate.args: --list-operators takes no other argument"),
            (["--root", "", "--base", b, "--head", h] + ok, "FAIL mutate.root: --root is empty"),
            (["--root", os.path.join(r, "nope"), "--base", b, "--head", h] + ok,
             f"FAIL mutate.root: --root {os.path.join(r, 'nope')!r} is not a directory"),
            (["--root", os.path.join(r, "pkg"), "--base", b, "--head", h] + ok,
             f"FAIL mutate.root: --root {os.path.join(r, 'pkg')!r} is not the top of a git work tree"),
            (["--root", self.scratch, "--base", b, "--head", h] + ok,
             f"FAIL mutate.root: --root {self.scratch!r} is not the top of a git work tree"),
            (["--root", r, "--base", "f" * 40, "--head", h] + ok, f"FAIL mutate.sha: --base {'f' * 40!r} is not a commit"),
            (["--root", r, "--base", b, "--head", "nope"] + ok, "FAIL mutate.sha: --head 'nope' is not a commit"),
            (["--root", r, "--base=--all", "--head", h] + ok, "FAIL mutate.sha: --base '--all' is not a commit"),
            (["--root", r, "--base", b, "--head", h + ":pkg"] + ok, f"FAIL mutate.sha: --head {h + ':pkg'!r} is not a commit"),
            (rng + ["--file", "pkg/nope.py", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg/nope.py' is not a regular file at {h}"),
            (rng + ["--file", "pkg", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg' is not a regular file at {h}"),
            (rng2 + ["--file", "pkg/link.py", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg/link.py' is not a regular file at {head2}"),
            (rng + ["--file", "pkg/test_mod.py", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg/test_mod.py' has no changed lines in {b}..{h}"),
            (rng + ["--file", "../x.py", "--test", "pkg.test_kill.A", "--py", PY],
             "FAIL mutate.file: '../x.py' is not a normalized path relative to the root"),
            (rng + ["--file", os.path.join(r, "pkg/kill.py"), "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: {os.path.join(r, 'pkg/kill.py')!r} is not a normalized path relative to the root"),
            (rng + ["--file", "./pkg/kill.py", "--test", "pkg.test_kill.A", "--py", PY],
             "FAIL mutate.file: './pkg/kill.py' is not a normalized path relative to the root"),
            (rng + ["--file", "", "--test", "pkg.test_kill.A", "--py", PY],
             "FAIL mutate.file: '' is not a normalized path relative to the root"),
            (rng2 + ["--file", "pkg/bad.py", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg/bad.py' at {head2} is not Python source (line 1)"),
            (rng2 + ["--file", "pkg/latin.py", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg/latin.py' at {head2} is not UTF-8"),
            (rng2 + ["--file", "pkg/cr.py", "--test", "pkg.test_kill.A", "--py", PY],
             f"FAIL mutate.file: 'pkg/cr.py' at {head2} has a carriage return"),
            (rng2 + ["--file", "notes.txt", "--test", "pkg.test_kill.A", "--py", PY],
             "FAIL mutate.file: 'notes.txt' is not a .py file"),
            (rng + ["--file", "pkg/kill.py", "--test=-x", "--py", PY], "FAIL mutate.test: '-x' is not a unittest id"),
            (rng + ["--file", "pkg/kill.py", "--test", "pkg..x", "--py", PY], "FAIL mutate.test: 'pkg..x' is not a unittest id"),
            (rng + ["--file", "pkg/kill.py", "--test", "pkg.test_kill.A", "--py", missing_py],
             f"FAIL mutate.py: {missing_py!r} is not an executable file"),
            (rng + ["--file", "pkg/kill.py", "--test", "pkg.test_kill.A", "--py", notexec],
             f"FAIL mutate.py: {notexec!r} is not an executable file"),
            (rng + ["--file", "pkg/kill.py", "--test", "pkg.test_kill.A", "--py", self.scratch],
             f"FAIL mutate.py: {self.scratch!r} is not an executable file"),
        ]
        for argv, want in cases:
            with self.subTest(argv=argv):
                rc, out = self.call(argv)
                self.assertEqual((rc, out), (2, want + "\n"))
        self.assertEqual(gf.git(self.root, "status", "--porcelain", "--untracked-files=all", "--ignored"), "")

    def test_tests_that_fail_or_run_nothing_unmutated_are_named_fails(self):
        gf.write(self.root, "pkg/test_red.py", NOTHING_TEST.replace("pass", "self.fail('red')"))
        self.head = gf.commit_all(self.root, "red")
        rc, out = self.call(self.args(tests=["pkg.test_red"]))
        self.assertEqual((rc, out), (2, f"FAIL mutate.baseline: the tests fail on the unmutated copy under {PY!r} (rc 1)\n"))
        rc, out = self.call(self.args(tests=["pkg.empty"]))
        self.assertEqual((rc, out), (2, f"FAIL mutate.baseline: the tests run no test on the unmutated copy under {PY!r}\n"))
        rc, out = self.call(self.args(tests=["pkg.no_such_module"]))
        self.assertEqual((rc, out), (2, f"FAIL mutate.baseline: the tests fail on the unmutated copy under {PY!r} (rc 1)\n"))

    def test_json_failures_and_no_traceback(self):
        rc, doc = self.run_json(["--list-operators"])
        self.assertEqual((rc, doc), (2, {"rc": 2, "fail": [{"check": "mutate.args",
                                                           "reason": "--list-operators takes no other argument"}]}))
        rc, doc = self.run_json(["--root", ""])
        self.assertEqual((rc, doc), (2, {"rc": 2, "fail": [{"check": "mutate.args", "reason": "--base is missing"}]}))
        with mock.patch.object(mutate, "run_all", side_effect=RuntimeError("boom")):
            rc, out = self.call(self.args())
        self.assertEqual((rc, out), (2, "FAIL mutate.internal: RuntimeError: boom\n"))

    def test_a_long_value_is_clipped_in_the_fail_line(self):
        rc, out = self.call(["--root", self.root, "--base", "x" * 500, "--head", self.head, "--file", "pkg/kill.py",
                             "--test", "pkg.test_kill.A", "--py", PY])
        self.assertEqual(rc, 2)
        self.assertEqual(out, "FAIL mutate.sha: --base " + repr("x" * 500)[:mutate.CLIP] + "... is not a commit\n")

    def test_git_environment_cannot_redirect_the_run(self):
        other = gf.new_repo()
        self.addCleanup(gf.remove, other)
        with mock.patch.dict(os.environ, {"GIT_DIR": os.path.join(other, ".git"), "GIT_WORK_TREE": other,
                                          "GIT_INDEX_FILE": os.path.join(other, ".git", "index")}):
            rc, out = self.call(self.args())
        self.assertEqual(rc, 1, out)
        self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())

    # --- fix round 9a: the rows of T1-mutate-input-domain.md (F18, F17, DEC-0251..DEC-0276) ------------------------

    def tool(self, argv, env=None, cwd=None):
        """The real file as a process: (rc, stdout, stderr), for rows whose input is the process environment."""
        full = dict(os.environ if env is None else env)
        p = subprocess.run([PY, MUTATE, *argv], capture_output=True, cwd=cwd or self.scratch, env=full, check=False)
        return p.returncode, p.stdout.decode("utf-8"), p.stderr.decode("utf-8", "replace")

    def call_in(self, cwd, argv):
        old = os.getcwd()
        os.chdir(cwd)
        try:
            return self.call(argv)
        finally:
            os.chdir(old)

    def kill_args(self, root, extra=()):
        return ["--root", root, "--base", self.base, "--head", self.head, "--file", "pkg/kill.py",
                "--test", "pkg.test_kill.A", "--py", PY] + list(extra)

    def copy_repo(self, name):
        """The fixture repository copied to <scratch>/<name> (str or bytes), or None when the filesystem refuses it."""
        dest = os.path.join(self.scratch, os.fsdecode(name))
        try:
            shutil.copytree(self.root, dest, symlinks=True)
        except (OSError, shutil.Error):
            shutil.rmtree(dest, ignore_errors=True)
            return None
        return dest

    def commit_files(self, files, msg):
        for rel, data in files.items():
            path = os.path.join(self.root, rel)
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as fh:
                fh.write(data.encode("utf-8") if isinstance(data, str) else data)
        return gf.commit_all(self.root, msg)

    def test_an_option_without_its_value_is_a_named_fail(self):
        """Rows M-R1, M-S1, M-F1, M-T1, M-P1: an option as the last argument, with no value."""
        for name in ("--root", "--base", "--head", "--file", "--test", "--py", "--jobs", "--ids"):
            with self.subTest(name):
                rc, out = self.call(self.args() + [name])
                self.assertEqual((rc, out), (2, f"FAIL mutate.args: bad arguments: argument {name}: expected one argument\n"))

    def test_a_whitespace_only_root_is_empty(self):
        """Row M-R3 (DEC-0253 empty-form): never a path, never the cwd's tree."""
        for root in (" ", "  ", "\t", "\n", " \t\n"):
            for argv in (self.kill_args(root), ["--root=" + root] + self.kill_args(self.root)[2:]):
                with self.subTest(root=ascii(root), form=argv[0][:7]):
                    rc, out = self.call_in(self.root, argv)
                    self.assertEqual((rc, out), (2, "FAIL mutate.root: --root is empty\n"))

    def test_a_unicode_whitespace_only_root_is_empty(self):
        """Row M-R3 (DEC-0294 as-built-with-rows): str.strip() takes every Unicode whitespace character as empty; the
        same whole line as the ASCII values, today's result, never the cwd's tree."""
        every = tuple(c for c in map(chr, range(0x110000)) if c.isspace() and c not in " \t\n\r\v\f")
        self.assertEqual(UNICODE_SPACES, every)
        for root in UNICODE_SPACES + ("\xa0 \u3000",):
            for argv in (self.kill_args(root), ["--root=" + root] + self.kill_args(self.root)[2:]):
                with self.subTest(root=ascii(root), form=argv[0][:7]):
                    rc, out = self.call_in(self.root, argv)
                    self.assertEqual((rc, out), (2, "FAIL mutate.root: --root is empty\n"))

    def test_a_relative_root_is_normalised_and_a_bad_one_named_absolute(self):
        """Row M-R5 (DEC-0254 name-abspath): relative, `..` and trailing-/ roots load as the tree; a relative bad root is
        named by its absolute path."""
        want = self.call(self.kill_args(self.root))
        parent, name = os.path.split(self.root)
        for cwd, root in ((parent, name), (parent, "./" + name + "/"), (parent, name + "/"),
                          (self.root, os.path.join(self.root, "pkg", "..")), (self.root, ".")):
            with self.subTest(root=root):
                self.assertEqual(self.call_in(cwd, self.kill_args(root)), want)
        absolute = os.path.join(os.path.realpath(self.root), "nope")     # the cwd as the process sees it
        for root in ("nope", "./nope/", "pkg/../nope"):
            with self.subTest(root=root):
                rc, out = self.call_in(self.root, self.kill_args(root))
                self.assertEqual((rc, out), (2, f"FAIL mutate.root: --root {absolute!r} is not a directory\n"))

    def test_a_symlinked_root_is_refused(self):
        """Row M-R6 (DEC-0255 refuse-symlink, D32): the same whole line with or without a trailing /, absolute or relative."""
        link = os.path.join(self.scratch, "lnk")
        os.symlink(self.root, link)
        seen = os.path.join(os.path.realpath(self.scratch), "lnk")      # a relative one, from the cwd as the process sees it
        for root, want in ((link, link), (link + "/", link), ("lnk", seen), ("lnk/", seen)):
            with self.subTest(root=root):
                rc, out = self.call_in(self.scratch, self.kill_args(root))
                self.assertEqual((rc, out), (2, f"FAIL mutate.root: --root {want!r} is a symlink\n"))

    def test_a_root_of_the_wrong_kind_is_not_a_directory(self):
        """Row M-R7 (D32): a regular file and a FIFO."""
        plain = os.path.join(self.scratch, "plain")
        open(plain, "w").close()
        fifo = os.path.join(self.scratch, "fifo")
        os.mkfifo(fifo)
        for root in (plain, fifo):
            with self.subTest(root=root):
                self.assertEqual(self.call(self.kill_args(root)), (2, f"FAIL mutate.root: --root {root!r} is not a directory\n"))

    def test_git_dirs_bare_and_unreadable_roots_are_not_a_top(self):
        """Row M-R8: <top>/.git, a bare repository, a plain directory, mode 000."""
        bare = os.path.join(self.scratch, "bare.git")
        gf.git(self.scratch, "clone", "-q", "--bare", self.root, bare)
        plain = os.path.join(self.scratch, "plain")
        os.mkdir(plain)
        roots = [os.path.join(self.root, ".git"), bare, plain]
        if os.geteuid() != 0:
            locked = os.path.join(self.scratch, "locked")
            shutil.copytree(self.root, locked)
            os.chmod(locked, 0)
            self.addCleanup(os.chmod, locked, 0o755)
            roots.append(locked)
        for root in roots:
            with self.subTest(root=root):
                self.assertEqual(self.call(self.kill_args(root)),
                                 (2, f"FAIL mutate.root: --root {root!r} is not the top of a git work tree\n"))

    def test_a_root_whose_name_ends_in_whitespace_is_that_root(self):
        """Row M-R9 (F17, DEC-0256 loader-fix): only git's final newline is removed from its toplevel."""
        want = self.call(self.kill_args(self.copy_repo("t")))
        self.assertEqual(want[0], 1, want)
        for name in ("t ", "t\t", "t \t", "t  "):
            with self.subTest(name=ascii(name)):
                self.assertEqual(self.call(self.kill_args(self.copy_repo(name))), want)

    def test_a_root_whose_name_is_not_valid_utf8_is_that_root(self):
        """Rows M-R10 and M-R13 (F17, DEC-0256, R8): a root named with a byte that is not UTF-8 loads (Linux; skipped
        where the filesystem refuses the name), and a missing one is a named FAIL with no traceback."""
        missing = os.path.join(self.scratch, os.fsdecode(b"t-\xff"))
        self.assertEqual(self.call(self.kill_args(missing)), (2, f"FAIL mutate.root: --root {missing!r} is not a directory\n"))
        root = self.copy_repo(b"tree-\xff")
        if root is None:
            self.skipTest("the filesystem refuses a name that is not UTF-8 (macOS APFS: EILSEQ)")
        want = self.call(self.kill_args(self.copy_repo("tree")))
        self.assertEqual(want[0], 1, want)
        self.assertEqual(self.call(self.kill_args(root)), want)

    def test_any_work_tree_top_is_a_root(self):
        """Row M-R11 (DEC-0266 any-worktree-top): a linked worktree top and a repository other than this project."""
        want = self.call(self.kill_args(self.root))
        self.assertEqual(want[0], 1, want)
        wt = os.path.join(self.scratch, "wt")
        gf.git(self.root, "worktree", "add", "-q", "--detach", wt, self.head)
        self.assertEqual(self.call(self.kill_args(wt)), want)
        self.assertNotEqual(os.path.realpath(self.root), os.path.realpath(gf.REPO))

    def test_a_dirty_root_is_read_at_head_and_left_as_it_was(self):
        """Row M-R12 (DEC-0247 clone rule): a modified file and an untracked file in the root."""
        want = self.call(self.kill_args(self.root))
        gf.write(self.root, "pkg/kill.py", "def f(a):\n    return True\n")
        gf.write(self.root, "stray.py", "X = 1\n")
        before = gf.git(self.root, "status", "--porcelain", "--untracked-files=all")
        self.assertEqual(self.call(self.kill_args(self.root)), want)
        self.assertEqual(gf.git(self.root, "status", "--porcelain", "--untracked-files=all"), before)
        with open(os.path.join(self.root, "pkg/kill.py"), encoding="utf-8") as fh:
            self.assertEqual(fh.read(), "def f(a):\n    return True\n")

    def test_a_tmpdir_whose_name_is_not_valid_utf8_holds_the_copy(self):
        """Row M-G1 (F17, DEC-0257): the clone's toplevel is decoded with os.fsdecode (Linux; skipped where the
        filesystem refuses the name)."""
        want = self.call(self.kill_args(self.root))
        tmp = os.path.join(self.scratch, os.fsdecode(b"tmp-\xff"))
        try:
            os.mkdir(tmp)
        except OSError:
            self.skipTest("the filesystem refuses a name that is not UTF-8 (macOS APFS: EILSEQ)")
        with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
            got = self.call(self.kill_args(self.root))
        self.assertEqual(got, want)
        self.assertEqual(os.listdir(tmp), [])

    def test_git_ceiling_directories_cannot_redirect_the_run(self):
        """Row M-G2 subcase: GIT_CEILING_DIRECTORIES naming the root."""
        want = self.call(self.kill_args(self.root))
        with mock.patch.dict(os.environ, {"GIT_CEILING_DIRECTORIES": self.root}):
            self.assertEqual(self.call(self.kill_args(self.root)), want)

    def test_repository_diff_config_and_attributes_cannot_change_the_sites(self):
        """Rows M-G3, M-G4 (DEC-0260 pin-diff, D32): diff.algorithm, diff.indentHeuristic, diff.interHunkContext in the
        root's config and a `-diff` attribute leave the mutant list as it is without them."""
        body = "def f(a):\n    x = a > 1\n    y = a > 2\n    z = a > 3\n    return x\n"
        base = self.commit_files({"pkg/cfg.py": body}, "cfg base")
        head = self.commit_files({"pkg/cfg.py": "def f(a):\n    x = a > 9\n    y = a > 2\n    z = a > 8\n    return x\n"},
                                 "cfg head")
        argv = ["--root", self.root, "--base", base, "--head", head, "--file", "pkg/cfg.py", "--test", "pkg.test_mod",
                "--py", PY, "--jobs", "4"]

        def key():
            rc, doc = self.run_json(argv)
            return rc, [(m["id"], m["line"], m["operator"]) for m in doc["mutants"]] if "mutants" in doc else doc
        want = key()
        self.assertEqual({line for _, line, _ in want[1]}, {2, 4})
        for k, v in (("diff.algorithm", "patience"), ("diff.indentHeuristic", "false"), ("diff.interHunkContext", "5")):
            with self.subTest(k):
                gf.git(self.root, "config", k, v)
                self.assertEqual(key(), want)
                gf.git(self.root, "config", "--unset", k)
        with self.subTest("-diff attribute"):
            with open(os.path.join(self.root, ".git", "info", "attributes"), "w", encoding="utf-8") as fh:
                fh.write("*.py -diff\n")
            self.assertEqual(key(), want)

    def test_repository_diff_algorithm_cannot_change_the_mod_sites(self):
        """Row M-G3 on a text where patience and myers choose different lines (changed lines: myers 2, 4, 5; patience 1, 4, 5)."""
        base = self.commit_files({"pkg/alg.py": "pass\nA = 1 > 0\nC = 3 > 2\nA = 1 > 0\nA = 1 > 0\n"}, "alg base")
        head = self.commit_files({"pkg/alg.py": "A = 1 > 0\npass\nA = 1 > 0\nD = 4 > 3\nB = 2 > 1\n"}, "alg head")
        argv = ["--root", self.root, "--base", base, "--head", head, "--file", "pkg/alg.py", "--test", "pkg.test_mod",
                "--py", PY, "--jobs", "4"]
        want = [(m["line"], m["operator"]) for m in self.run_json(argv)[1]["mutants"]]
        self.assertEqual(sorted({line for line, _ in want}), [4, 5])          # line 2 (pass) has no operator site
        gf.git(self.root, "config", "diff.algorithm", "patience")
        self.assertEqual([(m["line"], m["operator"]) for m in self.run_json(argv)[1]["mutants"]], want)

    def test_git_missing_is_a_named_git_fail(self):
        """Row M-G5 (DEC-0261 named-git-fail, R5): git not on PATH."""
        with mock.patch.dict(os.environ, {"PATH": "/nonexistent"}):
            self.assertEqual(self.call(self.kill_args(self.root)), (2, "FAIL mutate.git: git cannot run (No such file or directory)\n"))

    def test_commit_ish_forms_resolve_to_the_full_sha(self):
        """Row M-S2 (DEC-0267 rev-parse-forms): each form runs and the summary prints the full sha."""
        self.assertEqual(gf.git(self.root, "rev-parse", "main@{1}"), self.base)
        gf.git(self.root, "tag", "light", self.head)
        gf.git(self.root, "tag", "-a", "-m", "ann", "ann", self.head)
        b, h = self.base, self.head
        cases = [(b, b, form, h) for form in ("HEAD", "main", "refs/heads/main", "light", "ann", h[:7], h.upper())]
        cases += [(form, b, h, h) for form in ("HEAD~1", "HEAD^", "main@{1}")]
        self.run_forms(cases)
        dangling = self.commit_files({"README.md": "dangling\n"}, "dangling")
        gf.git(self.root, "reset", "-q", "--hard", h)
        self.assertEqual(gf.git(self.root, "branch", "--contains", dangling), "")
        self.run_forms([(b, b, dangling, dangling)])

    def run_forms(self, cases):
        for b_arg, b_sha, h_arg, h_sha in cases:
            with self.subTest(base=b_arg, head=h_arg):
                argv = ["--root", self.root, "--base", b_arg, "--head", h_arg] + self.kill_args(self.root)[6:] + ["--ids", "m0001"]
                rc, out = self.call(argv)
                self.assertEqual(rc, 0, out)
                self.assertIn(f"summary: base {b_sha} head {h_sha}, ids m0001, 1 mutants in 1 file, 1 test, 1 interpreter",
                              out.splitlines())

    def test_values_that_are_not_a_commit_are_named_fails(self):
        """Row M-S3 (D28, D32): forms that are not a commit, matched whole."""
        tree = gf.git(self.root, "rev-parse", self.head + "^{tree}")
        blob = gf.git(self.root, "rev-parse", self.head + ":pkg/kill.py")
        h = self.head
        for v in (tree, blob, h + ":pkg", f"{self.base}..{h}", h + "^{tree}", ":/base", "f" * 40, "nope", "", " ",
                  h + " ", " " + h, h + "\n", "x" * 500):
            with self.subTest(v=v[:50]):
                rc, out = self.call(["--root", self.root, "--base", self.base, "--head=" + v] + self.kill_args(self.root)[6:])
                self.assertEqual((rc, out), (2, f"FAIL mutate.sha: --head {mutate.show(v)} is not a commit\n"))
        self.assertEqual(self.call(["--root", self.root, "--base", self.base, "--head", "-x"] + self.kill_args(self.root)[6:]),
                         (2, "FAIL mutate.args: bad arguments: argument --head: expected one argument\n"))

    def test_a_range_with_no_changed_line_is_a_named_fail(self):
        """Row M-S4 (DEC-0268 keep-split): base == head, reversed, unrelated history."""
        gf.git(self.root, "checkout", "-q", "--orphan", "other")
        other = gf.commit_all(self.root, "unrelated")
        gf.git(self.root, "checkout", "-q", "main")
        for b, h in ((self.head, self.head), (self.head, self.base), (other, self.head)):
            with self.subTest(base=b, head=h):
                rc, out = self.call(["--root", self.root, "--base", b, "--head", h] + self.kill_args(self.root)[6:])
                self.assertEqual((rc, out), (2, f"FAIL mutate.file: 'pkg/kill.py' has no changed lines in {b}..{h}\n"))

    def test_file_path_forms_that_are_not_normalized_are_refused(self):
        """Row M-F2 subcases: `//` and a trailing `/`."""
        for v in ("pkg//kill.py", "pkg/kill.py/", "pkg/./kill.py", "pkg/../pkg/kill.py"):
            with self.subTest(v=v):
                argv = self.args(files=[v])
                self.assertEqual(self.call(argv), (2, f"FAIL mutate.file: {v!r} is not a normalized path relative to the root\n"))

    def test_a_file_matches_the_tracked_name_as_a_whole_string(self):
        """Row M-F3 (D32): no pathspec magic, no case folding, no whitespace trimming."""
        for v in ("pkg/kill.py ", " ", "pkg/Kill.py", "pkg/*.py", ":(glob)pkg/*.py", ".git/config", "pkg/kill.p"):
            with self.subTest(v=v):
                self.assertEqual(self.call(self.args(files=[v])), (2, f"FAIL mutate.file: {v!r} is not a regular file at {self.head}\n"))

    def test_a_symlinked_component_or_deleted_file_is_refused_and_100755_runs(self):
        """Row M-F4 (D32)."""
        os.symlink("pkg", os.path.join(self.root, "lnk"))
        os.remove(os.path.join(self.root, "pkg/spin.py"))
        os.chmod(os.path.join(self.root, "pkg/kill.py"), 0o755)
        head = gf.commit_all(self.root, "kinds")
        for v in ("lnk/kill.py", "lnk", "pkg/spin.py"):
            with self.subTest(v=v):
                rc, out = self.call(["--root", self.root, "--base", self.base, "--head", head, "--file", v, "--test",
                                     "pkg.test_kill.A", "--py", PY])
                self.assertEqual((rc, out), (2, f"FAIL mutate.file: {v!r} is not a regular file at {head}\n"))
        self.assertEqual(gf.git(self.root, "ls-tree", head, "pkg/kill.py").split()[0], "100755")
        rc, out = self.call(["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py", "--test",
                             "pkg.test_kill.A", "--py", PY])
        self.assertEqual(rc, 1, out)
        self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_names_that_are_not_py_files_are_refused(self):
        """Row M-F5 subcases (DEC-0269 as-built): `.PY` and `.pyi`."""
        head = self.commit_files({"pkg/x.PY": "X = 1\n", "pkg/x.pyi": "X: int\n"}, "not py")
        for v in ("pkg/x.PY", "pkg/x.pyi"):
            with self.subTest(v=v):
                rc, out = self.call(["--root", self.root, "--base", self.head, "--head", head, "--file", v, "--test",
                                     "pkg.test_kill.A", "--py", PY])
                self.assertEqual((rc, out), (2, f"FAIL mutate.file: {v!r} is not a .py file\n"))

    def test_a_bom_is_refused_and_a_coding_cookie_is_read_as_utf8(self):
        """Row M-F6 (DEC-0269 as-built)."""
        head = self.commit_files({"pkg/bom.py": b"\xef\xbb\xbfX = 1\n",
                                  "pkg/cookie.py": "# -*- coding: latin-1 -*-\nX = '\u00e9' == 'x'\n"}, "enc")
        rng = ["--root", self.root, "--base", self.head, "--head", head, "--test", "pkg.test_mod", "--py", PY]
        self.assertEqual(self.call(rng + ["--file", "pkg/bom.py"]),
                         (2, f"FAIL mutate.file: 'pkg/bom.py' at {head} is not Python source (line 1)\n"))
        rc, doc = self.run_json(rng + ["--file", "pkg/cookie.py"])
        self.assertEqual([(m["line"], m["operator"], m["old"]) for m in doc["mutants"]],
                         [(2, "str-empty", "'\u00e9'"), (2, "cmp-eq-ne", "=="), (2, "str-empty", "'x'")])
        self.assertEqual(rc, 1)

    def test_a_nul_byte_is_a_named_fail_on_each_interpreter(self):
        """Row M-F8 (DEC-0270 as-built): the tool's interpreter decides the text."""
        head = self.commit_files({"pkg/nul.py": b"X = 1\x00\n"}, "nul")
        rc, out = self.call(["--root", self.root, "--base", self.head, "--head", head, "--file", "pkg/nul.py", "--test",
                             "pkg.test_mod", "--py", PY])
        want = ("FAIL mutate.internal: ValueError: source code string cannot contain null bytes\n"
                if sys.version_info < (3, 12) else f"FAIL mutate.file: 'pkg/nul.py' at {head} is not Python source (line None)\n")
        self.assertEqual((rc, out), (2, want))

    def test_the_tool_interpreter_decides_python_source(self):
        """Row M-F9 (DEC-0270 as-built): `match` is source on 3.10+ only."""
        head = self.commit_files({"pkg/m.py": "match x:\n    case 1:\n        y = 2\n"}, "match")
        rc, out = self.call(["--root", self.root, "--base", self.head, "--head", head, "--file", "pkg/m.py", "--test",
                             "pkg.test_mod", "--py", PY])
        if sys.version_info < (3, 10):
            self.assertEqual((rc, out), (2, f"FAIL mutate.file: 'pkg/m.py' at {head} is not Python source (line 1)\n"))
        else:
            self.assertEqual(rc, 1, out)
            self.assertEqual(out.splitlines()[:4], [f"m000{i} pkg/m.py:{2 + (i > 2)} int-{op} SURVIVED {PY}"
                                                 for i, op in ((1, "inc"), (2, "dec"), (3, "inc"), (4, "dec"))])

    def test_changed_lines_without_an_operator_site_pass_with_no_mutant(self):
        """Row M-F10 (DEC-0268, DEC-0247): a comment and `pass`, or only a docstring, changed."""
        head = self.commit_files({"pkg/kill.py": KILL_HEAD + "# note\npass\n", "pkg/doc.py": DOC_HEAD.replace("module doc", "mod")},
                                 "no sites")
        for f in ("pkg/kill.py", "pkg/doc.py"):
            with self.subTest(f):
                rc, out = self.call(["--root", self.root, "--base", self.head, "--head", head, "--file", f, "--test",
                                     "pkg.test_mod", "--py", PY])
                self.assertEqual((rc, out.splitlines()), (0, [
                    f"summary: base {self.head} head {head}, 0 mutants in 1 file, 1 test, 1 interpreter",
                    f"summary {PY}: 0 KILLED, 0 SURVIVED, 0 BADMATCH"]))

    def test_an_added_or_renamed_file_is_all_sites(self):
        """Row M-F12 (DEC-0234 "every changed line"; --no-renames)."""
        gf.git(self.root, "mv", "pkg/mod.py", "pkg/ren.py")
        head = self.commit_files({"pkg/new.py": "A = 1 == 2\nB = 3\n"}, "add and rename")
        rng = ["--root", self.root, "--base", self.head, "--head", head, "--test", "pkg.test_mod", "--py", PY, "--jobs", "4"]
        doc = self.run_json(rng + ["--file", "pkg/new.py"])[1]
        self.assertEqual(sorted({m["line"] for m in doc["mutants"]}), [1, 2])
        ren = self.run_json(rng + ["--file", "pkg/ren.py"])[1]
        lines = {m["line"] for m in ren["mutants"]}
        self.assertTrue({2, 53}.issubset(lines), lines)         # UNCHANGED and TAIL: unchanged text, but the file is new

    def test_non_ascii_and_space_file_names_run(self):
        """Row M-F13 (DEC-0269 as-built)."""
        for name in ("pkg/\u00e9.py", "pkg/sp .py"):
            with self.subTest(name):
                head = self.commit_files({name: KILL_HEAD}, "name")
                rc, out = self.call(["--root", self.root, "--base", self.base, "--head", head, "--file", name, "--test",
                                     "pkg.test_mod", "--py", PY, "--ids", "m0002"])
                self.assertEqual((rc, out.splitlines()[0]), (1, f"m0002 {name}:5 return-none SURVIVED {PY}"))

    def test_a_file_whose_name_is_not_valid_utf8_runs(self):
        """Row M-F14 (DEC-0258 fsencode, R8): compared as os.fsencode bytes (Linux; skipped where the filesystem refuses)."""
        name = os.fsdecode(b"pkg/k\xff.py")
        try:
            head = self.commit_files({name: KILL_HEAD}, "bytes name")
        except OSError:
            self.skipTest("the filesystem refuses a name that is not UTF-8 (macOS APFS: EILSEQ)")
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", name, "--test", "pkg.test_mod",
                "--py", PY, "--ids", "m0002"]
        rc, out = self.call(argv)
        self.assertEqual((rc, out.splitlines()[0]), (1, f"m0002 {name}:5 return-none SURVIVED {PY}"))
        rc, out, err = self.tool([os.fsencode(a) for a in argv])
        self.assertEqual((rc, out.splitlines()[0], err), (1, f"m0002 pkg/k\\udcff.py:5 return-none SURVIVED {PY}", ""))

    def test_test_ids_match_the_ascii_grammar_whole(self):
        """Row M-T2 (D32, DEC-0271 as-built)."""
        for v in ("", " ", "pkg.test_kill.A ", "pkg.test_kill.A\n", "-x", "pkg..x", "pkg/test_kill.py", "pkg.t\u00e9st"):
            with self.subTest(v=ascii(v)):
                self.assertEqual(self.call(self.args(tests=[]) + ["--test=" + v]), (2, f"FAIL mutate.test: {v!r} is not a unittest id\n"))

    def test_test_ids_that_do_not_exist_are_baseline_fails(self):
        """Row M-T3 subcases: a missing class, a missing method, one good id with one missing."""
        for tests in (["pkg.test_kill.Nope"], ["pkg.test_kill.A.test_nope"], ["pkg.test_kill.A", "pkg.test_kill.Nope"]):
            with self.subTest(tests):
                self.assertEqual(self.call(self.args(tests=tests)),
                                 (2, f"FAIL mutate.baseline: the tests fail on the unmutated copy under {PY!r} (rc 1)\n"))

    def test_overlapping_test_ids_are_a_named_fail(self):
        """Row M-T4 (DEC-0265, DEC-0276): the id inside the other is named first, in either order."""
        for tests in (["pkg.test_kill", "pkg.test_kill.A"], ["pkg.test_kill.A", "pkg.test_kill"], ["pkg", "pkg.test_kill.A"]):
            inner, outer = sorted(tests, key=len, reverse=True)
            with self.subTest(tests):
                self.assertEqual(self.call(self.args(tests=tests)),
                                 (2, f"FAIL mutate.args: --test {inner!r} overlaps --test {outer!r}\n"))
        rc, out = self.call(self.args(tests=["pkg.test_kill.A", "pkg.test_kill.B", "pkg.test_kil"]))
        self.assertEqual(rc, 2)                              # a prefix that is not a dotted component does not overlap
        self.assertIn("FAIL mutate.baseline:", out)

    def test_py_values_that_are_not_an_executable_file_are_refused(self):
        """Row M-P2 subcases: '', ' ', a trailing space, a bare name."""
        for v in ("", " ", PY + " ", "python3"):
            with self.subTest(v=v):
                self.assertEqual(self.call(self.args(pys=[]) + ["--py=" + v]), (2, f"FAIL mutate.py: {v!r} is not an executable file\n"))

    def test_a_symlinked_interpreter_is_accepted(self):
        """Row M-P3 (DEC-0259 exception): absolute and relative, labelled by the argument as given."""
        os.symlink(PY, os.path.join(self.scratch, "venvlink"))
        for v in (os.path.join(self.scratch, "venvlink"), "venvlink", "./venvlink"):
            with self.subTest(v=v):
                rc, out = self.call_in(self.scratch, self.args(pys=[v]))
                self.assertEqual(rc, 1, out)
                self.assertIn(f"summary {v}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_one_interpreter_under_two_spellings_is_a_named_fail(self):
        """Row M-P4 (DEC-0265, DEC-0276): compared by realpath."""
        link = os.path.join(self.scratch, "venvlink")
        os.symlink(PY, link)
        for a, b in ((PY, link), (link, os.path.realpath(PY)), (PY, os.path.join(os.path.dirname(PY), ".", os.path.basename(PY)))):
            with self.subTest(a=a, b=b):
                self.assertEqual(self.call(self.args(pys=[a, b])),
                                 (2, f"FAIL mutate.args: --py {b!r} is the same interpreter as --py {a!r}\n"))

    def test_an_interpreter_that_fails_is_a_baseline_fail(self):
        """Row M-P5 (D28): /usr/bin/false, an exit 3, /bin/echo."""
        three = os.path.join(self.scratch, "three")
        with open(three, "w", encoding="utf-8") as fh:
            fh.write("#!/bin/sh\nexit 3\n")
        os.chmod(three, 0o755)
        for v in (shutil.which("false"), three, shutil.which("echo")):
            with self.subTest(v=v):
                self.assertEqual(self.call(self.args(pys=[v])),
                                 (2, f"FAIL mutate.baseline: the tests run no test on the unmutated copy under {v!r}\n"))

    def test_a_fake_interpreter_is_trusted_as_d28_scopes(self):
        """Row M-P6 (D28 honest mistakes): an executable that prints unittest's line is believed."""
        fake = os.path.join(self.scratch, "fake")
        with open(fake, "w", encoding="utf-8") as fh:
            fh.write("#!/bin/sh\necho 'Ran 1 test in 0.000s' >&2\nexit 0\n")
        os.chmod(fake, 0o755)
        rc, out = self.call(self.args(pys=[fake]))
        self.assertEqual(rc, 1, out)
        self.assertIn(f"summary {fake}: 0 KILLED, 4 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_jobs_grammar_is_matched_whole(self):
        """Row M-J1 (DEC-0271 as-built)."""
        for v in ("", " 2", "2 ", "01", "+2", "\u0662", "1e1", "0", "65", "x", "-1"):
            with self.subTest(v=ascii(v)):
                self.assertEqual(self.call(self.args(extra=["--jobs=" + v])),
                                 (2, f"FAIL mutate.args: --jobs {v!r} is not an integer from 1 to 64\n"))
        for v in ("1", "64"):
            with self.subTest(v):
                self.assertEqual(self.call(self.args(extra=["--jobs", v]))[0], 1)

    def test_json_given_twice_is_a_named_fail(self):
        """Row M-O1 (DEC-0265, DEC-0276): in JSON form; `--json=x` and `--js` are argparse refusals in text form."""
        rc, out = self.call(self.args(extra=["--json", "--json"]))
        self.assertEqual((rc, json.loads(out)), (2, {"rc": 2, "fail": [{"check": "mutate.args", "reason": "--json is given more than once"}]}))
        self.assertEqual(self.call(self.args(extra=["--json=x"])),
                         (2, "FAIL mutate.args: bad arguments: argument --json: ignored explicit argument 'x'\n"))
        self.assertEqual(self.call(self.args(extra=["--js"])), (2, "FAIL mutate.args: bad arguments: unrecognized arguments: --js\n"))

    def test_the_output_form_follows_the_parsed_json_option(self):
        """Row M-O2 (DEC-0272 parsed-json-usage): `--json` after `--` is not the option."""
        self.assertEqual(self.call(self.args(extra=["--", "--json"])),
                         (2, "FAIL mutate.args: bad arguments: unrecognized arguments: -- --json\n"))
        rc, out = self.call(self.args(extra=["--json", "--bogus"]))
        self.assertEqual((rc, json.loads(out)["fail"][0]["reason"]), (2, "bad arguments: unrecognized arguments: --bogus"))

    def test_h_prints_usage(self):
        """Row M-O3 (DEC-0272, DEC-0276, A6): usage on stderr, the whole FAIL line on stdout, rc 2."""
        for argv in (["-h"], self.args(extra=["-h"])):
            with self.subTest(argv=argv[-1]):
                err = io.StringIO()
                with contextlib.redirect_stderr(err):
                    rc, out = self.call(argv)
                self.assertEqual((rc, out), (2, "FAIL mutate.args: bad arguments (argparse exit 0)\n"))
                self.assertTrue(err.getvalue().startswith("usage: mutate "), err.getvalue())
        rc, out, err = self.tool(["-h"])
        self.assertEqual((rc, out), (2, "FAIL mutate.args: bad arguments (argparse exit 0)\n"))
        self.assertTrue(err.startswith("usage: mutate "), err)

    def test_a_tmpdir_inside_the_root_is_a_guard_fail(self):
        """Row M-E1 (DEC-0032): a real TMPDIR inside the root; the refused scratch directory is not left in the root."""
        sub = os.path.join(self.root, "sub")
        os.mkdir(sub)
        for tmp in (self.root, sub):
            with self.subTest(tmp=tmp):
                rc, out, err = self.tool(self.kill_args(self.root), env=dict(os.environ, TMPDIR=tmp))
                self.assertEqual((rc, err), (2, ""))
                self.assertRegex(out, r"\AFAIL mutate.guard: the scratch copy '" + re.escape(tmp) + r"/mutate-[^/']+' is the root or "
                                      r"inside it\n\Z")
        self.assertEqual(os.listdir(sub), [])
        self.assertEqual(sorted(n for n in os.listdir(self.root) if n.startswith("mutate-")), [])

    def test_a_tmpdir_that_is_not_a_writable_directory_is_a_guard_fail(self):
        """Row M-E2 (DEC-0262 check-tmpdir, DEC-0276): missing, read-only, a regular file, empty; nothing runs."""
        ro = os.path.join(self.scratch, "ro")
        os.mkdir(ro, 0o555)
        self.addCleanup(os.chmod, ro, 0o755)
        plain = os.path.join(self.scratch, "plain")
        open(plain, "w").close()
        values = [os.path.join(self.scratch, "nope"), plain, ""] + ([ro] if os.geteuid() != 0 else [])
        for tmp in values:
            with self.subTest(tmp=tmp), mock.patch.dict(os.environ, {"TMPDIR": tmp}), \
                    mock.patch.object(tempfile, "tempdir", None), mock.patch.object(mutate.Copy, "make") as make:
                self.assertEqual(self.call(self.kill_args(self.root)),
                                 (2, f"FAIL mutate.guard: TMPDIR {tmp!r} is not a writable directory\n"))
                make.assert_not_called()

    def test_python_path_variables_cannot_redirect_the_tests(self):
        """Row M-E3 (DEC-0273 drop-python-paths, DEC-0276): the six variables are removed from the test runs."""
        want = self.call(self.kill_args(self.root))
        self.assertEqual(want[0], 1, want)
        junk = os.path.join(self.scratch, "nope")
        sets = [{"PYTHONSAFEPATH": "1", "PYTHONPATH": self.root}, {"PYTHONPATH": self.root}, {"PYTHONHOME": junk},
                {"PYTHONSAFEPATH": "1"}, {"PYTHONUSERBASE": junk}, {"PYTHONSTARTUP": junk}, {"PYTHONPLATLIBDIR": "nope"}]
        for env in sets:
            with self.subTest(env=sorted(env)), mock.patch.dict(os.environ, env):
                self.assertEqual(self.call(self.kill_args(self.root)), want)

    def test_python_path_variables_that_change_the_result_are_removed(self):
        """Row M-E3 (F19 of task-1-rereview-9c.md): a PYTHONPATH directory holding sitecustomize.py and a PYTHONUSERBASE
        whose user site holds usercustomize.py, each ending the interpreter, would end every test run if kept; removed,
        the result equals the run without them."""
        stop = "import os\nos._exit(3)\n"
        sitepath = os.path.join(self.scratch, "sitepath")
        gf.write(sitepath, "sitecustomize.py", stop)
        base = getattr(sys, "_base_executable", None) or PY     # a venv disables the user site; the base keeps it
        userbase = os.path.join(self.scratch, "userbase")
        clean = {k: v for k, v in os.environ.items() if not k.startswith("PYTHON")}

        def py_run(exe, env, code):
            return subprocess.run([exe, "-c", code], env=dict(clean, **env), capture_output=True, text=True, check=False)
        probe = py_run(base, {"PYTHONUSERBASE": userbase},
                       "import site; print(site.ENABLE_USER_SITE, site.getusersitepackages())")
        enabled, usersite = probe.stdout.split()
        self.assertEqual(enabled, "True", f"{base} has no user site, so PYTHONUSERBASE has no effect: {probe}")
        gf.write(usersite, "usercustomize.py", stop)
        cases = [(PY, {"PYTHONPATH": sitepath}), (base, {"PYTHONUSERBASE": userbase})]
        for exe, env in cases:
            with self.subTest(env=sorted(env), py=exe):
                self.assertEqual(py_run(exe, env, "pass").returncode, 3, "the value has no effect when kept")
                self.assertEqual(py_run(exe, {}, "pass").returncode, 0)
                argv = self.kill_args(self.root)[:-1] + [exe]
                want = self.call(argv)
                self.assertEqual(want[0], 1, want)
                self.assertIn(f"summary {exe}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", want[1].splitlines())
                with mock.patch.dict(os.environ, env):
                    self.assertEqual(self.call(argv), want)

    def test_a_test_run_killed_on_timeout_leaves_no_file_outside_the_copy(self):
        """Row M-E5 (DEC-0297 tmpdir-in-copy): every test run's TMPDIR, TMP and TEMP is <scratch>/tmp, removed with the
        scratch directory; the hung runs, killed on their timeout, leave nothing in the caller's TMPDIR."""
        head = self.commit_files({"pkg/test_tmp.py": TMP_TEST}, "tmp")
        tmp = os.path.join(self.scratch, "tmp")
        os.mkdir(tmp)
        log = os.path.join(self.scratch, "tmp.log")
        others = {"TMP": os.path.join(self.scratch, "nope-tmp"), "TEMP": os.path.join(self.scratch, "nope-temp")}
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_tmp.A", "--py", PY, "--jobs", "2", "--json"]
        with mock.patch.object(mutate, "TIMEOUT_FLOOR", 3.0), mock.patch.object(mutate, "TIMEOUT_FACTOR", 1.0), \
                mock.patch.dict(os.environ, dict(others, TMPDIR=tmp, MUTATE_TEST_TMPLOG=log)), \
                mock.patch.object(tempfile, "tempdir", None):
            rc, out = self.call(argv)
        doc = json.loads(out)
        self.assertEqual(rc, 1, doc)
        self.assertEqual(sorted(m["reason"][PY] for m in doc["mutants"]),
                         ["tests passed", "tests passed", "timeout", "timeout"])
        with open(log, encoding="utf-8") as fh:
            rows = [json.loads(line) for line in fh]
        self.assertEqual(len(rows), 5, rows)              # the baseline and the 4 mutants
        for made, tmpdir, tmp_, temp in rows:
            with self.subTest(made=made):
                scratch = os.path.dirname(tmpdir)
                self.assertEqual((tmp_, temp, os.path.dirname(made)), (tmpdir, tmpdir, tmpdir))
                self.assertEqual((os.path.basename(tmpdir), os.path.dirname(scratch)), ("tmp", tmp))
                self.assertTrue(os.path.basename(scratch).startswith("mutate-"), scratch)
                self.assertFalse(os.path.exists(made))
        self.assertEqual(os.listdir(tmp), [])
        self.assertFalse(any(os.path.exists(p) for p in others.values()))

    def test_git_variables_of_the_caller_do_not_reach_the_tests(self):
        """Run (docstring: GIT_* removed from the test runs): the caller's GIT_DIR and GIT_WORK_TREE naming the root
        would point a test's git at the checkout; the tests see no GIT_* variable and git finds the copy."""
        head = self.commit_files({"pkg/test_gitenv.py": GITENV_TEST}, "gitenv")
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_kill.A", "--test", "pkg.test_gitenv.G", "--py", PY]
        want = self.call(argv)
        self.assertEqual(want[0], 1, want)
        self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", want[1].splitlines())
        env = {"GIT_DIR": os.path.join(self.root, ".git"), "GIT_WORK_TREE": self.root}
        with mock.patch.dict(os.environ, env):
            self.assertEqual(self.call(argv), want)

    def test_stdout_is_utf8_whatever_the_locale(self):
        """Row M-E4 (DEC-0263 utf8-stdout, O4): a non-ASCII --file name under PYTHONIOENCODING=ascii and LC_ALL=C."""
        name = "pkg/\u00e9.py"
        head = self.commit_files({name: KILL_HEAD}, "name")
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", name, "--test", "pkg.test_mod", "--py", PY]
        for extra in ({"PYTHONIOENCODING": "ascii"}, {"LC_ALL": "C", "LANG": "C"}):
            with self.subTest(extra):
                rc, out, err = self.tool(argv, env=dict(os.environ, **extra))
                self.assertEqual((rc, err), (1, ""))
                self.assertEqual(out.splitlines()[0], f"m0002 {name}:5 return-none SURVIVED {PY}")
                self.assertEqual(out.splitlines()[4], f"summary: base {self.base} head {head}, 4 mutants in 1 file, 1 test, 1 interpreter")

    def test_output_is_not_capped(self):
        """Row M-X1 (DEC-0274 no-cap): a run printing more than 16,384 bytes prints every line."""
        n = 120
        head = self.commit_files({"pkg/big.py": "".join(f"X{i} = {i}\n" for i in range(n))}, "big")
        rc, out = self.call(["--root", self.root, "--base", self.head, "--head", head, "--file", "pkg/big.py", "--test",
                             "pkg.test_mod", "--py", PY, "--jobs", "8"])
        lines = out.splitlines()
        self.assertGreater(len(out.encode()), 16384)
        self.assertEqual(rc, 1)
        self.assertEqual(len(lines), 2 * n + 2 + 2 * n)
        self.assertEqual(lines[-1], f"SURVIVED m{2 * n:04d} pkg/big.py:{n}:{len(f'X{n - 1} = ') + 1} int-dec '{n - 1}' -> '{n - 2}'")

    def test_an_internal_fail_is_one_clipped_line(self):
        """Row M-X2 (DEC-0264 clip-one-line, DEC-0276, O3): escaped and clipped to 200 characters plus `...`."""
        text = "a\nb\tc   d" + "x" * 5000                     # runs of spaces collapse, as the loader's one_line
        want_reason = mutate.clip("RuntimeError: a\\nb\\tc d" + "x" * 5000)
        for where in ("run_all", "run_mutant"):
            with self.subTest(where), mock.patch.object(mutate, where, side_effect=RuntimeError(text)):
                rc, out = self.call(self.args())
                self.assertEqual((rc, out), (2, f"FAIL mutate.internal: {want_reason}\n"))
                self.assertEqual(len(out.splitlines()), 1)
        self.assertEqual(len(want_reason), 203)

    # --- run lifecycle (rows M-X3, M-X4, M-X5, M-X7; DEC-0275, DEC-0276, DEC-0277..DEC-0280) -----------------

    def outside_dir(self):
        """A 0o555 directory holding a file, beside the copies: the target of the fixture's symlink (row M-X9)."""
        path = tempfile.mkdtemp(prefix="outside-", dir=self.scratch)
        open(os.path.join(path, "f"), "w").close()
        os.chmod(path, 0o555)
        return path

    def assert_outside_untouched(self, path):
        self.assertEqual((oct(os.stat(path).st_mode & 0o7777), os.listdir(path)), (oct(0o555), ["f"]))

    def lock_run(self, shapes, prefix=(), extra=(), hang=""):
        """Row M-X9: the real file as a process (`prefix` may wrap it) on pkg.test_lock.A, whose runs leave `shapes`
        under the copy's TMPDIR; (rc, stdout, stderr, the TMPDIR's listing after the run)."""
        tmp = tempfile.mkdtemp(prefix="tmp-", dir=self.scratch)
        outside = self.outside_dir()
        if gf.git(self.root, "show", "-s", "--format=%s", "HEAD") != "lock":
            self.commit_files({"pkg/kill.py": KILL_HEAD, "pkg/locks.py": LOCKS_PY, "pkg/test_lock.py": LOCK_TEST}, "lock")
        head = gf.git(self.root, "rev-parse", "HEAD")
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_lock.A", "--py", PY, *extra]
        env = dict(os.environ, TMPDIR=tmp, MUTATE_TEST_SHAPES=shapes, MUTATE_TEST_OUTSIDE=outside, MUTATE_TEST_HANG=hang)
        p = subprocess.run([PY, *(prefix or (MUTATE,)), *argv], capture_output=True, cwd=self.scratch, env=env, check=False,
                           timeout=120)
        self.assert_outside_untouched(outside)
        self.assert_checkout_untouched_at(head)
        return p.returncode, p.stdout.decode(), p.stderr.decode(), [os.path.join(tmp, n) for n in os.listdir(tmp)]

    def test_a_locked_tree_in_the_copy_is_removed_at_the_end(self):
        """Row M-X9: a tree a test leaves in the copy whose modes block a plain removal (each shape alone, the symlink
        to a directory outside, and all together) is removed at the run's normal end; the symlink is not followed."""
        if os.geteuid() == 0:
            self.skipTest("root ignores file modes")
        for shapes in ("unlistable", "unwritable", "unsearchable", "nested", "inner", "link",
                       "unlistable,unwritable,unsearchable,nested,inner,link"):
            with self.subTest(shapes=shapes):
                rc, out, err, left = self.lock_run(shapes)
                self.assertEqual((rc, err, left), (1, "", []), out)
                self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_a_locked_tree_is_removed_after_a_mutant_timeout(self):
        """Rows M-X6, M-X9: the copy whose mutant runs were killed on their timeout (int-inc and return-none hang) is
        removed with the locked trees every run left (the real file's main(), its timeout constants set low)."""
        if os.geteuid() == 0:
            self.skipTest("root ignores file modes")
        wrap = ("-c", "import sys; sys.path.insert(0, sys.argv[1]); import mutate; mutate.TIMEOUT_FLOOR = 3.0; "
                      "mutate.TIMEOUT_FACTOR = 1.0; sys.exit(mutate.main(sys.argv[2:]))", os.path.dirname(MUTATE))
        rc, out, err, left = self.lock_run("unlistable,unwritable,unsearchable,nested,inner,link", prefix=wrap,
                                           extra=["--json"], hang="1")
        self.assertEqual((rc, err, left), (1, "", []), out)
        reasons = sorted(m["reason"][PY] for m in json.loads(out)["mutants"])
        self.assertEqual(reasons, ["tests passed", "tests passed", "timeout", "timeout"])

    def assert_only_uchg_left(self, copies):
        """Row M-X9b: each copy left holds only the immutable files its runs made; the restore removed the rest (the
        tree, the other locked shapes)."""
        for copy in copies:
            held = [os.path.relpath(os.path.join(top, f), copy) for top, _, files in os.walk(copy) for f in files]
            self.assertTrue(held)
            for path in held:
                self.assertRegex(path, r"\Atmp/lock-uchg-[^/]+/d/f\Z")

    def assert_results_then(self, out, tail):
        """Row M-X9b, text form: the result lines printed as each mutant finished, then `tail`, no summary."""
        self.assertTrue(out.endswith(tail), out)
        for row in out[:len(out) - len(tail)].splitlines():
            self.assertRegex(row, r"\Am\d{4} pkg/kill\.py:\d+ \S+ (KILLED|SURVIVED) \S+\Z")

    def test_a_copy_left_after_the_restore_is_a_named_cleanup_fail(self):
        """Row M-X9b (owner answer 2026-11-01, named-fail): a copy that still cannot be removed after the restore (a
        uchg file) ends the run with `FAIL mutate.cleanup: <copy> left`, rc 2: at the normal end, after a mutant's
        timeout (--json), before the interrupt line on SIGINT, SIGTERM (also --json) and SIGHUP, and in the next run
        after a SIGKILL (the stale copy). The real file as a process, but the next run's main() in-process."""
        if not hasattr(os, "chflags") or not hasattr(stat, "UF_IMMUTABLE"):
            self.skipTest("os.chflags is not available: without root no path here resists the owner's restore")
        if os.geteuid() == 0:
            self.skipTest("root ignores file modes")
        line = "FAIL mutate.cleanup: {} left\n".format
        with self.subTest("normal end"):
            rc, out, err, left = self.lock_run("uchg,unlistable,nested")
            self.assertEqual(len(left), 1, left)
            self.assertEqual((rc, err), (2, ""))
            self.assert_results_then(out, line(mutate.show(left[0])))
            self.assert_only_uchg_left(left)
        with self.subTest("timeout"):
            wrap = ("-c", "import sys; sys.path.insert(0, sys.argv[1]); import mutate; mutate.TIMEOUT_FLOOR = 3.0; "
                          "mutate.TIMEOUT_FACTOR = 1.0; sys.exit(mutate.main(sys.argv[2:]))", os.path.dirname(MUTATE))
            rc, out, err, left = self.lock_run("uchg", prefix=wrap, extra=["--json"], hang="1")
            self.assertEqual(len(left), 1, left)
            self.assertEqual((rc, json.loads(out), err),
                             (2, {"rc": 2, "fail": [{"check": "mutate.cleanup", "reason": f"{mutate.show(left[0])} left"}]},
                              ""))
            self.assert_only_uchg_left(left)
        head = self.hang_head()
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_hang.A", "--py", PY, "--jobs", "1"]
        for sig, name, extra in ((signal.SIGINT, "SIGINT", []), (signal.SIGTERM, "SIGTERM", []),
                                 (signal.SIGTERM, "SIGTERM", ["--json"]), (signal.SIGHUP, "SIGHUP", [])):
            with self.subTest(name, json=bool(extra)):
                tmp, files = self.hang_env()
                p = self.start_tool(argv + extra, dict(os.environ, TMPDIR=tmp, MUTATE_TEST_SHAPES="uchg", **files))
                self.wait_for_hang(p, files["MUTATE_TEST_HANGLOG"])
                os.kill(p.pid, sig)
                out, err = p.communicate(timeout=30)
                left = [os.path.join(tmp, n) for n in os.listdir(tmp)]
                self.assertEqual(len(left), 1, left)
                stop = f"stopped by {name}"
                want = (json.dumps({"rc": 2, "fail": [{"check": "mutate.cleanup", "reason": f"{mutate.show(left[0])} left"},
                                                      {"check": "mutate.interrupt", "reason": stop}]}) + "\n" if extra
                        else line(mutate.show(left[0])) + f"FAIL mutate.interrupt: {stop}\n")
                self.assertEqual((p.returncode, out.decode(), err.decode()), (2, want, ""))
                self.assert_runs_gone(files["MUTATE_TEST_RUNLOG"])
                self.assert_only_uchg_left(left)
        with self.subTest("stale copy"):
            tmp, files = self.hang_env()
            p = self.start_tool(argv, dict(os.environ, TMPDIR=tmp, MUTATE_TEST_SHAPES="uchg", **files))
            self.wait_for_hang(p, files["MUTATE_TEST_HANGLOG"])
            os.kill(p.pid, signal.SIGKILL)
            p.wait()
            self.assert_runs_gone(files["MUTATE_TEST_RUNLOG"])
            stale = [os.path.join(tmp, n) for n in os.listdir(tmp)]
            self.assertEqual(len(stale), 1, stale)
            with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
                rc, out = self.call(self.kill_args(self.root))
            self.assertEqual(rc, 2)
            self.assert_results_then(out, line(mutate.show(stale[0])))
            self.assertEqual([os.path.join(tmp, n) for n in os.listdir(tmp)], stale)   # the next run's own copy is gone
            self.assert_only_uchg_left(stale)

    def hang_head(self):
        """A head whose pkg.test_hang.A hangs on a mutant of kill.py and pkg.test_hang.Z always hangs."""
        return self.commit_files({"pkg/kill.py": KILL_HEAD, "pkg/locks.py": LOCKS_PY, "pkg/test_hang.py": HANG_TEST},
                                 "hang")

    def hang_env(self):
        where = tempfile.mkdtemp(prefix="hang-", dir=self.scratch)   # its own per call (row M-X9b runs several)
        tmp = os.path.join(where, "tmp")
        os.mkdir(tmp)
        files = {"MUTATE_TEST_HANGLOG": os.path.join(where, "hangs.log"),
                 "MUTATE_TEST_RUNLOG": os.path.join(where, "runs.log"),
                 "MUTATE_TEST_OUTSIDE": self.outside_dir()}
        self.addCleanup(kill_recorded_groups, files["MUTATE_TEST_RUNLOG"])
        return tmp, files

    def start_tool(self, argv, env, ignore=()):
        """The real file as a process in its own session, killed with its group and reaped when the test ends."""
        def dispositions():
            for sig in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP):
                signal.signal(sig, signal.SIG_IGN if sig in ignore else signal.SIG_DFL)
        p = subprocess.Popen([PY, MUTATE, *argv], stdout=subprocess.PIPE, stderr=subprocess.PIPE, cwd=self.scratch,
                             env=env, start_new_session=True, preexec_fn=dispositions)
        # the caller's dispositions are not inherited (a background job of a shell starts with SIGINT ignored, DEC-0290;
        # nohup ignores SIGHUP), each set to the test's own

        def reap():
            with contextlib.suppress(ProcessLookupError, PermissionError):
                os.killpg(p.pid, signal.SIGKILL)
            p.communicate()
        self.addCleanup(reap)
        return p

    def wait_for_hang(self, p, hanglog):
        """(pid, pgid, child pid) of the first hung test run once it has written them; the tool must still run."""
        deadline = time.monotonic() + 60
        while not recorded(hanglog):
            self.assertIsNone(p.poll(), "the tool ended before the hung test run started")
            self.assertLess(time.monotonic(), deadline, "the hung test run did not start within 60 s")
            time.sleep(0.05)
        return recorded(hanglog)[0]

    def assert_runs_gone(self, runlog):
        """No live process of any test run's group remains: polled up to 10 s, passing as soon as none is left
        (DEC-0280; checked by the process listing; the groups from the run log, re-read at each poll)."""
        deadline = time.monotonic() + 10
        while True:
            groups = sorted({row[1] for row in recorded(runlog)})
            left = {g: group_members(g) for g in groups}
            left = {g: m for g, m in left.items() if m}
            if not left or time.monotonic() > deadline:
                break
            time.sleep(0.1)
        self.assertTrue(groups, "no test run recorded itself")
        self.assertEqual(left, {}, "processes of the test runs' groups outlived the tool")

    def interrupt_run(self, sig, name):
        head = self.hang_head()
        tmp, files = self.hang_env()
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_hang.A", "--py", PY, "--jobs", "1"]
        p = self.start_tool(argv, dict(os.environ, TMPDIR=tmp, **files))
        pid, pgid, _ = self.wait_for_hang(p, files["MUTATE_TEST_HANGLOG"])
        self.assertNotEqual(pgid, p.pid)
        os.kill(p.pid, sig)
        try:
            out, err = p.communicate(timeout=30)    # the mutant timeout is at least TIMEOUT_FLOOR (60 s)
        except subprocess.TimeoutExpired:
            self.fail(f"{name}: the tool did not stop within 30 s of the signal")
        self.assertEqual((p.returncode, out.decode(), err.decode()), (2, f"FAIL mutate.interrupt: stopped by {name}\n", ""))
        runs = recorded(files["MUTATE_TEST_RUNLOG"])
        self.assertEqual(len(runs), 2, f"a mutant started after {name}: {runs}")   # the baseline and the hung mutant
        self.assertEqual(runs[1], (pid, pgid))
        self.assert_runs_gone(files["MUTATE_TEST_RUNLOG"])
        self.assertEqual(os.listdir(tmp), [])           # row M-X9: with the locked trees each run left
        self.assert_outside_untouched(files["MUTATE_TEST_OUTSIDE"])
        self.assert_checkout_untouched_at(head)

    def assert_checkout_untouched_at(self, head):
        self.assertEqual(gf.git(self.root, "status", "--porcelain", "--untracked-files=all", "--ignored"), "")
        self.assertEqual(gf.git(self.root, "rev-parse", "HEAD"), head)

    def test_sigint_stops_the_run_and_leaves_nothing(self):
        """Row M-X3 (DEC-0275, DEC-0276): at once, no further mutant, test processes killed, scratch removed, rc 2."""
        self.interrupt_run(signal.SIGINT, "SIGINT")

    def ignored_run(self, ignored, stop, name):
        """Started with `ignored` inherited as ignored: that signal leaves the run going (no handler is installed);
        `stop` still stops it with its FAIL line."""
        head = self.hang_head()
        tmp, files = self.hang_env()
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_hang.A", "--py", PY, "--jobs", "1"]
        p = self.start_tool(argv, dict(os.environ, TMPDIR=tmp, **files), ignore=(ignored,))
        pid, pgid, _ = self.wait_for_hang(p, files["MUTATE_TEST_HANGLOG"])
        os.kill(p.pid, ignored)
        time.sleep(3)
        self.assertIsNone(p.poll(), f"the tool stopped on a {signal.Signals(ignored).name} it inherited as ignored")
        self.assertEqual(len(recorded(files["MUTATE_TEST_RUNLOG"])), 2)
        self.assertTrue(group_members(pgid), "the hung test run was killed by the ignored signal")
        os.kill(p.pid, stop)
        try:
            out, err = p.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            self.fail(f"the tool did not stop within 30 s of {name}")
        self.assertEqual((p.returncode, out.decode(), err.decode()), (2, f"FAIL mutate.interrupt: stopped by {name}\n", ""))
        self.assert_runs_gone(files["MUTATE_TEST_RUNLOG"])
        self.assertEqual(os.listdir(tmp), [])

    def test_an_inherited_ignored_sigint_is_kept(self):
        """Row M-X3b (DEC-0290 keep-inherited-ignore): started with SIGINT ignored (nohup, a background job), the tool
        installs no SIGINT handler: SIGINT does not stop the run; SIGTERM still does (M-X4)."""
        self.ignored_run(signal.SIGINT, signal.SIGTERM, "SIGTERM")

    def test_an_inherited_ignored_sigterm_is_kept(self):
        """Row M-X4b (DEC-0293 keep-inherited-ignore): started with SIGTERM ignored, the tool installs no SIGTERM
        handler: SIGTERM does not stop the run; SIGINT still does (M-X3)."""
        self.ignored_run(signal.SIGTERM, signal.SIGINT, "SIGINT")

    def test_an_inherited_ignored_sighup_is_kept(self):
        """Row M-X4c (DEC-0299 keep-inherited-ignore): started with SIGHUP ignored (nohup), SIGHUP does not stop the run;
        SIGTERM still does."""
        self.ignored_run(signal.SIGHUP, signal.SIGTERM, "SIGTERM")

    def test_sighup_stops_the_run_and_leaves_nothing(self):
        """Row M-X4c (DEC-0296 sighup-as-sigterm, DEC-0298 name-sighup): as M-X4, the line naming SIGHUP."""
        self.interrupt_run(signal.SIGHUP, "SIGHUP")

    def test_sigterm_stops_the_run_and_leaves_nothing(self):
        """Row M-X4 (DEC-0275, DEC-0276): as M-X3 with SIGTERM."""
        self.interrupt_run(signal.SIGTERM, "SIGTERM")

    def test_a_signal_handler_of_the_caller_is_restored_after_main(self):
        """main() installs the M-X3/M-X4 handlers for its run only: the caller's handlers come back, on a FAIL too."""
        sigs = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT)   # SIGQUIT: never changed (row M-X4d)
        before = [signal.getsignal(s) for s in sigs]
        seen = []
        real = mutate.execute

        def execute(ns):
            seen.append([signal.getsignal(s) for s in sigs])
            return real(ns)
        for argv in (self.args(), ["--root", ""]):
            with self.subTest(argv=argv[:2]), mock.patch.object(mutate, "execute", execute):
                self.call(argv)
                self.assertEqual([signal.getsignal(s) for s in sigs], before)
        for during in seen:     # the stop handler while the run goes (unless the suite itself ignores the signal)
            self.assertEqual([h == mutate.Stop.handler for h in during],
                             [b != signal.SIG_IGN for b in before[:3]] + [False])

    def test_a_hung_baseline_is_killed_by_its_timeout(self):
        """Row M-X5 (DEC-0275, DEC-0276): 600 s, the process group killed, scratch removed, rc 2 (the constant patched
        down here, as the row's test cell says)."""
        self.assertEqual(mutate.BASELINE_TIMEOUT, 600)
        head = self.hang_head()
        tmp, files = self.hang_env()
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_hang.Z", "--py", PY]
        for limit, text in ((5.0, "5"), (5.5, "5.5")):
            with self.subTest(limit=limit), mock.patch.object(mutate, "BASELINE_TIMEOUT", limit), \
                    mock.patch.dict(os.environ, dict(files, TMPDIR=tmp)), mock.patch.object(tempfile, "tempdir", None):
                for log in (files["MUTATE_TEST_HANGLOG"], files["MUTATE_TEST_RUNLOG"]):
                    with contextlib.suppress(FileNotFoundError):
                        os.unlink(log)
                guard = threading.Timer(limit + 55, kill_recorded_groups, [files["MUTATE_TEST_RUNLOG"]])
                guard.start()                   # without the timeout the run would never end: the test ends red
                self.addCleanup(guard.cancel)
                self.assertEqual(self.call(argv), (2, "FAIL mutate.baseline: the tests did not finish on the unmutated "
                                                      f"copy under {PY!r} within {text} s\n"))
                self.assertEqual(len(recorded(files["MUTATE_TEST_HANGLOG"])), 1)
                self.assert_runs_gone(files["MUTATE_TEST_RUNLOG"])
                self.assertEqual(os.listdir(tmp), [])       # row M-X9: with the locked trees the run left
                self.assert_outside_untouched(files["MUTATE_TEST_OUTSIDE"])

    def test_test_processes_die_with_a_sigkilled_tool(self):
        """Row M-X7 (DEC-0277, DEC-0279, DEC-0280): SIGKILL of the tool; its test processes kill their own group."""
        self.assertEqual(mutate.WATCH_PERIOD, 1.0)
        head = self.hang_head()
        tmp, files = self.hang_env()
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_hang.A", "--py", PY, "--jobs", "2"]
        p = self.start_tool(argv, dict(os.environ, TMPDIR=tmp, **files))
        self.wait_for_hang(p, files["MUTATE_TEST_HANGLOG"])
        os.kill(p.pid, signal.SIGKILL)
        p.wait()
        self.assert_runs_gone(files["MUTATE_TEST_RUNLOG"])
        left = sorted(os.listdir(tmp))
        self.assertEqual(len(left), 2, left)              # the killed run's copies, each recording the run's pid
        for name in left:
            self.assertTrue(name.startswith("mutate-"), name)
            with open(os.path.join(tmp, name, "mutate.pid"), encoding="utf-8") as fh:
                self.assertEqual(fh.read(), f"{p.pid} {host_id()}\n")
        with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
            self.assertEqual(self.call(self.kill_args(self.root))[0], 1)
        self.assertEqual(os.listdir(tmp), [])             # the next run removed the stale copies, and its own
        self.assert_outside_untouched(files["MUTATE_TEST_OUTSIDE"])   # row M-X9: the copies' locked trees too

    def test_the_next_run_removes_only_stale_scratch_copies(self):
        """Row M-X7 (DEC-0278): a mutate-* copy whose recorded pid is gone is removed; one whose pid runs (a concurrent
        run's) is kept, as is anything that is not such a copy."""
        tmp = os.path.join(self.scratch, "tmp")
        os.mkdir(tmp)
        gone = subprocess.Popen(["true"])
        gone.wait()
        dead, live = str(gone.pid), str(os.getpid())

        def copy(name, pid_text, kind="dir"):
            path = os.path.join(tmp, name)
            os.makedirs(os.path.join(path, "tree", "pkg"))
            gf.write(path, "tree/pkg/x.py", "x = 1\n")
            if pid_text is not None:
                pidfile = os.path.join(path, "mutate.pid")
                if kind == "dir":
                    gf.write(path, "mutate.pid", pid_text)
                else:
                    os.mkdir(pidfile)
            return path
        hid = host_id()
        stale = [copy("mutate-aaaaaaaa", f"{dead} {hid}\n"), copy("mutate-test-bbbbbbbb", f"{dead} {hid}\n")]
        kept = [copy("mutate-cccccccc", f"{live} {hid}\n"), copy("mutate-r-old", dead + "\n"),     # old format: DEC-0300
                copy("mutate-r-old-test", dead + "\n"), copy("mutate-r-live-old", live + "\n"),
                copy("mutate-r-tab", f"{dead}\t{hid}\n"), copy("mutate-r-two", f"{dead}  {hid}\n"),
                copy("mutate-r-upper", f"{dead} {hid.upper()}\n"), copy("mutate-r-short", f"{dead} {hid[:-1]}\n"),
                copy("mutate-r-long", f"{dead} {hid}0\n"), copy("mutate-r-nonl", f"{dead} {hid}"),
                copy("mutate-r-lead", f" {dead} {hid}\n"), copy("mutate-r-more", f"{dead} {hid}\n1\n"),
                copy("mutate-r-zero", f"0 {hid}\n"), copy("mutate-r-huge", f"{'9' * 30} {hid}\n"),
                copy("mutate-r-dir", f"{dead} {hid}\n", kind="pid-is-a-dir"),
                copy("mutate-dddddddd", None), copy("mutate-eeeeeeee", "x\n"),
                copy("mutate-ffffffff", ""), copy("mutate-gggggggg", "0\n"), copy("mutate-hhhhhhhh", "9" * 30 + "\n"),
                copy("mutate-iiiiiiii", dead + "\n", kind="pid-is-a-dir"), copy("other-jjjjjjjj", f"{dead} {hid}\n"),
                copy("mutate-kkkkkkkk", " " + dead + "\n"), copy("mutate-llllllll", dead),
                copy("mutate-oooooooo", dead + "\n1\n"), copy("mutate-pppppppp", "1\n"),
                copy("mutate-qqqqqqqq", None)]
        target = copy("target", f"{dead} {hid}\n")
        outside = self.outside_dir()
        locks = {}
        exec(LOCKS_PY, locks)                    # row M-X9: the stale copies hold every locked shape
        for path in stale:
            locks["lock_tree"](parent=path, outside=outside)
        guarded = [os.path.join(target, "ro"), os.path.join(kept[0], "ro")]   # the symlink's target; a live copy
        for path in guarded:
            os.mkdir(path, 0o555)
        os.symlink(os.path.join(target, "mutate.pid"), os.path.join(kept[-1], "mutate.pid"))   # a symlinked pid file
        os.symlink(target, os.path.join(tmp, "mutate-mmmmmmmm"))
        kept += [target, os.path.join(tmp, "mutate-mmmmmmmm")]
        plain = os.path.join(tmp, "mutate-nnnnnnnn")
        open(plain, "w").close()
        kept.append(plain)
        with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
            self.assertEqual(self.call(self.kill_args(self.root))[0], 1)
        self.assertEqual(sorted(os.listdir(tmp)), sorted(os.path.basename(k) for k in kept))
        self.assertTrue(os.path.isfile(os.path.join(target, "tree", "pkg", "x.py")))
        self.assertEqual([oct(os.stat(path).st_mode & 0o7777) for path in guarded], [oct(0o555)] * 2)
        self.assert_outside_untouched(outside)

    def test_a_stale_copy_from_another_host_or_namespace_is_kept(self):
        """Row M-X7 (DEC-0295 record-namespace): a dead pid with this host's id is removed; a pid file whose id is
        another host's or pid namespace's is kept whatever its pid; the old `<pid>\\n` form is kept (DEC-0300 keep-old-format)."""
        tmp = os.path.join(self.scratch, "tmp")
        os.mkdir(tmp)
        gone = subprocess.Popen(["true"])
        gone.wait()
        dead, live, hid = str(gone.pid), str(os.getpid()), host_id()
        other_ns = hashlib.sha256(os.fsencode(os.uname().nodename) + b"\0" + b"pid:[1]").hexdigest()
        other_host = hashlib.sha256(b"another-host\0" + os.fsencode(pid_namespace())).hexdigest()
        files = {"mutate-same": f"{dead} {hid}\n", "mutate-ns": f"{dead} {other_ns}\n",
                 "mutate-host": f"{dead} {other_host}\n", "mutate-zeros": f"{dead} {'0' * 64}\n",
                 "mutate-live-ns": f"{live} {other_ns}\n", "mutate-old": f"{dead}\n"}
        for name, text in files.items():
            gf.write(os.path.join(tmp, name), "mutate.pid", text)
        self.assertNotIn(hid, (other_ns, other_host))
        with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
            self.assertEqual(self.call(self.kill_args(self.root))[0], 1)
        self.assertEqual(sorted(os.listdir(tmp)), sorted(set(files) - {"mutate-same"}))

    def test_a_signal_while_the_copies_are_made_stops_before_the_next_copy(self):
        """Rows M-X3, M-X4: no further copy is made after the signal, and the FAIL is the interrupt whatever the
        stopped step made of it (a clone killed by the terminal's SIGINT is a git FAIL)."""
        tmp = os.path.join(self.scratch, "tmp")
        os.mkdir(tmp)
        for how in ("returns", "fails"):
            made = []

            def make(copy, root, head):
                made.append(copy.scratch)
                mutate.Stop.handler(signal.SIGINT, None)
                if how == "fails":
                    raise mutate.Fail("git", "git clone failed: 'killed'")
            with self.subTest(how), mock.patch.object(mutate.Copy, "make", make), \
                    mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
                self.assertEqual(self.call(self.args(extra=["--jobs", "2"])),
                                 (2, "FAIL mutate.interrupt: stopped by SIGINT\n"))
                self.assertEqual(len(made), 1)
                self.assertEqual(os.listdir(tmp), [])

    def test_a_signal_during_the_report_ends_the_output_with_its_fail(self):
        """Rows M-X3, M-X4: stdout ends with the FAIL line whenever the signal comes."""
        real = mutate.report_text

        def report(*a):
            real(*a)
            mutate.Stop.handler(signal.SIGTERM, None)
        with mock.patch.object(mutate, "report_text", report):
            rc, out = self.call(self.args())
        self.assertEqual((rc, out.splitlines()[-1]), (2, "FAIL mutate.interrupt: stopped by SIGTERM"))
        self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())
        rc, out = self.call(self.args())                     # the next run in the same process is not stopped
        self.assertEqual((rc, out.splitlines()[-1].startswith("SURVIVED ")), (1, True), out)

    def test_a_signal_between_the_start_and_the_registration_kills_the_run(self):
        """Rows M-X3, M-X4: a signal that lands after a test run starts but before the handler can see it still kills
        that run at once (not by a timeout)."""
        head = self.hang_head()
        tmp, files = self.hang_env()
        real, runs = subprocess.Popen, []

        def popen(args, *a, **kw):
            p = real(args, *a, **kw)
            if args[:3] == [sys.executable, "-I", "-c"]:       # a test run, not a git call
                runs.append(p.pid)
                mutate.Stop.handler(signal.SIGINT, None)
            return p
        fired = []

        def guard():
            fired.append(True)
            kill_recorded_groups(files["MUTATE_TEST_RUNLOG"])
        timer = threading.Timer(60, guard)
        timer.start()
        self.addCleanup(timer.cancel)
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_hang.Z", "--py", PY]
        with mock.patch.object(mutate.subprocess, "Popen", popen), mock.patch.dict(os.environ, dict(files, TMPDIR=tmp)), \
                mock.patch.object(tempfile, "tempdir", None):
            self.assertEqual(self.call(argv), (2, "FAIL mutate.interrupt: stopped by SIGINT\n"))
        timer.cancel()
        self.assertEqual(fired, [], "the run was not killed by the tool")
        self.assertEqual(len(runs), 1)
        self.assertEqual(group_members(runs[0]), [])          # the tool killed and reaped it before it returned
        self.assertEqual(os.listdir(tmp), [])

    def test_a_tmpdir_that_cannot_be_listed_runs_and_removes_nothing(self):
        """Row M-X7 (DEC-0278): the removal of stale copies needs a listing; a TMPDIR that is writable but cannot be
        listed (mode 333) runs as before."""
        if os.geteuid() == 0:
            self.skipTest("root ignores file modes")
        tmp = os.path.join(self.scratch, "tmp")
        os.mkdir(tmp, 0o333)
        self.addCleanup(os.chmod, tmp, 0o755)
        with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None):
            rc, out = self.call(self.args())
        self.assertEqual(rc, 1, out)
        self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_an_unreadable_pid_file_is_kept(self):
        """Row M-X7b (as built, pending Q-R10b-1): a mutate-* directory whose regular pid file cannot be read (mode 000)
        is kept, though it holds a dead pid with this host's id; the run goes on as without it. The listing is put in
        each order of the unreadable copy, a stale copy and the run's own copy, so no earlier entry decides it."""
        if os.geteuid() == 0:
            self.skipTest("root ignores file modes")
        gone = subprocess.Popen(["true"])
        gone.wait()
        stale_text = f"{gone.pid} {host_id()}\n"
        real_listdir = os.listdir
        for order in itertools.permutations(("unread", "stale", "own")):
            with self.subTest(order=order):
                tmp = os.path.join(self.scratch, "tmp-" + "-".join(order))
                os.mkdir(tmp)
                gf.write(os.path.join(tmp, "mutate-stale"), "mutate.pid", stale_text)
                pidfile = os.path.join(tmp, "mutate-unread", "mutate.pid")
                gf.write(os.path.join(tmp, "mutate-unread"), "mutate.pid", stale_text)
                os.chmod(pidfile, 0o000)
                self.addCleanup(os.chmod, pidfile, 0o644)
                with self.assertRaises(PermissionError):
                    open(pidfile, "rb").close()
                lists = []

                def listdir(path=".", tmp=tmp, order=order):
                    names = real_listdir(path)
                    if path != tmp:
                        return names
                    rank = {"mutate-unread": order.index("unread"), "mutate-stale": order.index("stale")}
                    lists.append(sorted(names, key=lambda n: rank.get(n, order.index("own"))))
                    return lists[-1]
                with mock.patch.dict(os.environ, {"TMPDIR": tmp}), mock.patch.object(tempfile, "tempdir", None), \
                        mock.patch.object(mutate.os, "listdir", listdir):
                    rc, out = self.call(self.args())
                self.assertEqual(rc, 1, out)
                self.assertNotIn("FAIL", out)
                self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())
                self.assertEqual([[n[:7] if n not in ("mutate-unread", "mutate-stale") else n for n in names]
                                  for names in lists],
                                 [[{"unread": "mutate-unread", "stale": "mutate-stale", "own": "mutate-"}[k]
                                   for k in order]])
                self.assertEqual(real_listdir(tmp), ["mutate-unread"])

    def test_main_from_another_thread_runs_without_installing_handlers(self):
        """Row M-X8 (as built, pending Q-R10b-2): main() called from a thread that is not the main thread installs no
        stop handler (signal.signal works only in the main thread) and runs as from the main thread."""
        sigs = (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT)
        before = [signal.getsignal(s) for s in sigs]
        rc, out = self.call(self.args())
        seen, result = [], []
        real = mutate.execute

        def execute(ns):
            seen.append([signal.getsignal(s) for s in sigs])
            return real(ns)

        def body():
            try:
                result.append(self.call(self.args()))
            except BaseException as exc:     # the thread's outcome is asserted below, never lost
                result.append(exc)
        with mock.patch.object(mutate, "execute", execute):
            th = threading.Thread(target=body)
            th.start()
            th.join(300)
        self.assertFalse(th.is_alive())
        self.assertEqual(result, [(rc, out)])
        self.assertEqual(rc, 1, out)
        self.assertEqual(seen, [before])
        self.assertEqual([signal.getsignal(s) for s in sigs], before)

    def test_no_run_starts_after_a_signal(self):
        """Rows M-X3, M-X4: a signal between two mutant runs; the next run does not start."""
        real_clean, real_popen, runs = mutate.Copy.assert_clean, subprocess.Popen, []

        def clean(copy, who):
            real_clean(copy, who)
            if who == "m0001":
                mutate.Stop.handler(signal.SIGINT, None)

        def popen(args, *a, **kw):
            if args[:3] == [sys.executable, "-I", "-c"]:
                runs.append(args)
            return real_popen(args, *a, **kw)
        with mock.patch.object(mutate.Copy, "assert_clean", clean), mock.patch.object(mutate.subprocess, "Popen", popen):
            rc, out = self.call(self.args(extra=["--jobs", "1"]))
        self.assertEqual((rc, out), (2, f"m0001 pkg/kill.py:5 return-none KILLED {PY}\n"
                                        "FAIL mutate.interrupt: stopped by SIGINT\n"))
        self.assertEqual(len(runs), 2)                         # the baseline and m0001

    def test_a_test_run_killed_by_a_signal_reports_that_signal(self):
        """As built before round 9b (the M-X7 watcher passes the status on): a test process killed by SIGABRT."""
        head = self.commit_files({"pkg/test_abort.py": KILL_TEST.replace(
            "        self.assertIs(kill.f(2), True)",
            "        if kill.f(2) is not True:\n            import os, signal; os.kill(os.getpid(), signal.SIGABRT)\n"
            "        self.assertIs(kill.f(2), True)")}, "abort")
        argv = ["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                "--test", "pkg.test_abort.A", "--py", PY]
        rc, doc = self.run_json(argv)
        self.assertEqual(rc, 1, doc)
        self.assertEqual([m["reason"][PY] for m in doc["mutants"]],
                         [f"tests failed (rc -{int(signal.SIGABRT)})", "tests passed", f"tests failed (rc -{int(signal.SIGABRT)})",
                          "tests passed"])

    def test_a_module_in_the_root_cannot_shadow_the_watcher(self):
        """Row M-X7: the watcher runs isolated (-I), so a json.py or subprocess.py at the top of the copy (its cwd) is
        not what it imports."""
        head = self.commit_files({"json.py": "raise SystemExit('the root json.py was imported')\n",
                                  "subprocess.py": "raise SystemExit('the root subprocess.py was imported')\n"}, "shadow")
        rc, out = self.call(["--root", self.root, "--base", self.base, "--head", head, "--file", "pkg/kill.py",
                             "--test", "pkg.test_kill.A", "--py", PY])
        self.assertEqual(rc, 1, out)
        self.assertIn(f"summary {PY}: 2 KILLED, 2 SURVIVED, 0 BADMATCH", out.splitlines())

    def test_an_interpreter_that_cannot_start_is_an_internal_fail(self):
        """As built before round 9b (the M-X7 watcher must keep it): an executable file the system cannot exec."""
        bad = os.path.join(self.scratch, "noexec")
        with open(bad, "w", encoding="utf-8") as fh:
            fh.write("not a script\n")
        os.chmod(bad, 0o755)
        reason = f"OSError: [Errno 8] Exec format error: {bad!r}"     # clipped as M-X2 clips it: TMPDIR sets its length
        if len(reason) > mutate.CLIP:
            reason = reason[:mutate.CLIP] + "..."
        self.assertEqual(self.call(self.args(pys=[bad])), (2, f"FAIL mutate.internal: {reason}\n"))


def force_remove(path):
    """Test cleanup: `path` removed even when a run left a locked tree (rows M-X9, M-X9b: modes, the uchg flag) in it;
    symlinks not followed."""
    for top, dirs, files in os.walk(path):
        for name in dirs + files:
            sub = os.path.join(top, name)
            if hasattr(os, "chflags"):
                with contextlib.suppress(OSError):
                    os.chflags(sub, 0, follow_symlinks=False)
            if name in dirs and not os.path.islink(sub):
                with contextlib.suppress(OSError):
                    os.chmod(sub, 0o700)
    shutil.rmtree(path, ignore_errors=True)


def pid_namespace():
    """Row M-X7 (DEC-0295): the pid namespace link on Linux, "" where /proc/self/ns/pid cannot be read (macOS)."""
    try:
        return os.readlink("/proc/self/ns/pid")
    except OSError:
        return ""


def host_id():
    """Row M-X7 (DEC-0295): the id a run records beside its pid, as the row states it."""
    return hashlib.sha256(os.fsencode(os.uname().nodename) + b"\0" + os.fsencode(pid_namespace())).hexdigest()


def group_members(pgid):
    """Pids of the live (not zombie) processes in process group pgid, from the process listing."""
    out = subprocess.run(["ps", "-A", "-o", "pid=,pgid=,stat="], capture_output=True, text=True, check=True).stdout
    rows = [line.split() for line in out.splitlines()]
    return [int(r[0]) for r in rows if len(r) >= 3 and r[1] == str(pgid) and not r[2].startswith("Z")]


def recorded(log):
    """The complete lines of a fixture log as tuples of ints ([] while it does not exist)."""
    try:
        with open(log, encoding="utf-8") as fh:
            text = fh.read()
    except FileNotFoundError:
        return []
    return [tuple(int(v) for v in line.split()) for line in text.splitlines(keepends=True) if line.endswith("\n")]


def kill_recorded_groups(runlog):
    """Test cleanup: every test run's group the run log names, if it is still there."""
    for row in recorded(runlog):
        with contextlib.suppress(ProcessLookupError, PermissionError):
            os.killpg(row[1], signal.SIGKILL)


if __name__ == "__main__":
    unittest.main()
