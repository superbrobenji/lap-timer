"""tools/program/mutate.py through main() on fixture repositories (program tooling Task 1, fix round 8a, DEC-0234).
Run from the repo root: python3 -m unittest tools.gates.tests.test_mutate -v
"""
import contextlib
import io
import json
import os
import shutil
import sys
import tempfile
import time
import unittest
from unittest import mock

from tools.gates.tests import gitfixture as gf
from tools.program import mutate

PY = sys.executable

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
        self.head = gf.commit_all(self.root, "head")
        self.scratch = tempfile.mkdtemp(prefix="mutate-test-")
        self.addCleanup(shutil.rmtree, self.scratch, True)

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
        os.rmdir(inside)
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


if __name__ == "__main__":
    unittest.main()
