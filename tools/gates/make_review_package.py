#!/usr/bin/env python3
"""make_review_package -- build .superpowers/sdd/<plan>/review/<task>/ (git-ignored) for one task's BASE..HEAD.

Writes commits.txt (git log --oneline --no-decorate), diff.patch (git diff -U10) and gates.txt
(run_all at HEAD minus the gates that judge the package or the PR). The implementer adds
mutation-<finding-id>.txt, siblings.md and claims.md; this script never touches those.
The output is what gate_review_package accepts: commit lines '<sha> <subject>', a diff starting
'diff --git a/', gate lines 'PASS|FAIL <id>'. git is run with the options that fix those forms
whatever the user's git config says: --no-pager; -c color.ui=never, -c color.diff=never and --no-color
(colour: color.diff, which also colours log's sha, beats color.ui); -c core.abbrev=12 (log shas of 12 hex,
COMMIT_LINE takes 7-40); -c i18n.logOutputEncoding=UTF-8 (subjects as UTF-8); -c log.showSignature=false;
log --oneline --no-decorate; diff --no-ext-diff --no-textconv --src-prefix=a/ --dst-prefix=b/.
color.decorate is moot under --no-decorate, color.status under status --porcelain (gitinfo); no
other git command's output reaches the package.
Same range guards as superpowers' subagent-driven-development scripts/review-package; a dirty tree is
refused, because gates.txt must describe HEAD, not the working tree.
Usage: make_review_package.py <plan> <task> <base> <head> [--only gate,gate]
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates import run_all  # noqa: E402
from tools.gates.lib import GateError, gitinfo, repo_root  # noqa: E402

STATE = "docs/superpowers/state"
SDD = ".superpowers/sdd"
EXCLUDE = ("review_package", "merge_ready")
IMPLEMENTER_FILES = ("siblings.md", "claims.md")
NAME = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")
# options that pin the forms gate_review_package reads, against the caller's git config
PLAIN = ("-c", "color.ui=never", "-c", "color.diff=never", "-c", "core.abbrev=12",
         "-c", "i18n.logOutputEncoding=UTF-8", "-c", "log.showSignature=false")


class Refused(Exception):
    pass


def _git(root: str, *args: str) -> bytes:
    r = subprocess.run(["git", "--no-pager", *args], cwd=root, capture_output=True, check=False,
                       env={**os.environ, "LC_ALL": "C"})
    if r.returncode != 0:
        err = r.stderr.decode("utf-8", "replace").strip()
        raise Refused(f"git {' '.join(args)}: {err or 'exit ' + str(r.returncode)}")
    return r.stdout


def _sha(root: str, rev: str, label: str) -> str:
    try:
        return _git(root, "rev-parse", "--verify", "--quiet", f"{rev}^{{commit}}").decode().strip()
    except Refused:
        raise Refused(f"unknown revision for {label}: {rev}") from None


def _range(root: str, base: str, head: str) -> tuple[str, str, int]:
    b, h = _sha(root, base, "base"), _sha(root, head, "head")
    anc = subprocess.run(["git", "merge-base", "--is-ancestor", b, h], cwd=root, capture_output=True, check=False)
    if anc.returncode != 0:
        raise Refused(f"HEAD is not a descendant of BASE: {base}..{head}")
    n = int(_git(root, "rev-list", "--count", f"{b}..{h}").decode().strip())
    if n == 0:
        raise Refused(f"empty commit range: {base}..{head}")
    if h != gitinfo.head_sha(root):
        raise Refused(f"check out {head} first: gates.txt must describe the commit under review")
    return b, h, n


def _write(path: str, data) -> None:
    with open(path, "wb") as f:
        f.write(data if isinstance(data, bytes) else data.encode("utf-8"))


def _package(root: str, a) -> tuple[str, int]:
    for label, value in (("plan", a.plan), ("task", a.task)):
        if not NAME.match(value):
            raise Refused(f"{label} {value!r} is not a directory name ({NAME.pattern})")
    if not os.path.isdir(os.path.join(root, STATE, a.plan)):
        raise Refused(f"{STATE}/{a.plan}/ missing: run tools/gates/new_plan_state.py {a.plan} first")
    if not gitinfo.status_clean(root):
        raise Refused("working tree is not clean: gates.txt must describe the commit under review; "
                      "commit or stash first (the ignored .superpowers/ does not count)")
    hidden = [l[2:] for l in _git(root, "ls-files", "-v").decode("utf-8", "replace").splitlines()
              if l[:1].islower() or l[:1] == "S"]    # assume-unchanged / skip-worktree: status cannot see them
    if hidden:
        raise Refused(f"tracked file(s) hidden from git status (assume-unchanged or skip-worktree): "
                      f"{', '.join(hidden[:5])}; clear the flag so gates.txt describes HEAD")
    b, h, n = _range(root, a.base, a.head)
    diff = _git(root, *PLAIN, "diff", "--no-color", "--no-ext-diff", "--no-textconv", "--src-prefix=a/",
                "--dst-prefix=b/", "-U10", f"{b}..{h}")
    if not diff.strip():
        raise Refused(f"empty diff for {a.base}..{a.head}: the commits change no file")
    pkg = os.path.join(root, SDD, a.plan, "review", a.task)
    try:
        os.makedirs(pkg, exist_ok=True)
        _write(os.path.join(pkg, "commits.txt"),
               _git(root, *PLAIN, "log", "--no-color", "--oneline", "--no-decorate", f"{b}..{h}"))
        _write(os.path.join(pkg, "diff.patch"), diff)
    except OSError as e:
        raise Refused(f"cannot write {os.path.relpath(pkg, root)}: {e.strerror or e}") from None
    return pkg, n


def main(argv=None) -> int:
    p = argparse.ArgumentParser(prog="make_review_package")
    p.add_argument("plan")
    p.add_argument("task")
    p.add_argument("base")
    p.add_argument("head")
    p.add_argument("--only", help="comma-separated gates for gates.txt (default: run_all.GATES minus "
                                  + ", ".join(EXCLUDE) + ")")
    a = p.parse_args(argv)
    try:
        root = repo_root()
        pkg, n = _package(root, a)
    except (Refused, GateError) as e:
        print(f"make_review_package: {e}", file=sys.stderr)
        return 1
    names = ([x.strip() for x in a.only.split(",") if x.strip()] if a.only is not None
             else [g for g in run_all.GATES if g not in EXCLUDE])    # as run_all --only
    res = run_all.run(names, {"root": root})
    try:
        _write(os.path.join(pkg, "gates.txt"), res.render(False) + "\n")
        pending = [f for f in IMPLEMENTER_FILES if not os.path.isfile(os.path.join(pkg, f))]
        if not any(f.startswith("mutation-") for f in os.listdir(pkg)):
            pending.append("mutation-<finding-id>.txt")
    except OSError as e:
        print(f"make_review_package: cannot write gates.txt: {e.strerror or e}", file=sys.stderr)
        return 1
    print(f"wrote {os.path.relpath(pkg, root)}: {n} commit(s); gates {'PASS' if res.ok() else 'FAIL'}")
    if pending:
        print(f"the implementer still adds: {', '.join(pending)}")
    return 0 if res.ok() else 1


if __name__ == "__main__":
    sys.exit(main())
