"""git/gh helpers (umbrella plan, "Git/gh helpers"). Every call is
subprocess.run([...], capture_output=True, text=True, check=False); a non-zero rc raises
GateError(f"{cmd}: {stderr}") -- except `gh pr view` reporting that the branch has no PR,
which is the documented None of pr_for_branch()."""
from __future__ import annotations

import json
import os
import re
import subprocess

from . import GateError


def _env() -> dict:
    """git's messages are matched by text: pin the C locale."""
    return {**os.environ, "LC_ALL": "C"}


def _check_root(cmd: list, cwd) -> None:
    if cwd is not None and not os.path.isdir(cwd):
        why = "is not a directory" if os.path.exists(cwd) else "does not exist"
        raise GateError(f"{' '.join(cmd)}: root {cwd} {why}")


def _run(cmd: list, cwd=None) -> str:
    """Run `cmd` in `cwd`. Messages name the command, never the directory (machine-independent)."""
    _check_root(cmd, cwd)
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False, env=_env())
    except OSError as exc:
        raise GateError(f"{' '.join(cmd)}: {exc.strerror or exc}") from None
    if p.returncode != 0:
        raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")
    return p.stdout


def _git(root, *args) -> str:
    return _run(["git", *args], cwd=str(root))


def head_sha(root) -> str:
    return _git(root, "rev-parse", "HEAD").strip()


def branch(root) -> str:
    """The checked-out branch name; a detached HEAD is a GateError (never the string "HEAD")."""
    name = _git(root, "rev-parse", "--abbrev-ref", "HEAD").strip()
    if name == "HEAD":
        raise GateError("git rev-parse --abbrev-ref HEAD: detached HEAD")
    return name


def status_clean(root) -> bool:
    # status.showUntrackedFiles=no (a common user setting) would hide untracked files: pin it
    return _git(root, "-c", "status.showUntrackedFiles=all", "status", "--porcelain=v1", "-uall") == ""


def worktrees(root) -> list:
    """Paths from `git worktree list --porcelain` (the main checkout first)."""
    out = _git(root, "worktree", "list", "--porcelain")
    return [l[len("worktree "):] for l in out.splitlines() if l.startswith("worktree ")]


def changed_files(root, base: str, head: str = "HEAD") -> list:
    """Paths that differ between two commits (`git diff --name-only -z --no-renames`; -z means no C-quoting of non-ASCII paths)."""
    out = _git(root, "diff", "--name-only", "-z", "--no-renames", base, head)
    return [l for l in out.split("\0") if l]


def staged_files(root) -> list:
    out = _git(root, "diff", "--cached", "--name-only", "-z", "--no-renames")
    return [l for l in out.split("\0") if l]


def pr_for_branch(root):
    """`gh pr view` for the checked-out branch, or None when the branch has no PR."""
    cmd = ["gh", "pr", "view", "--json",
           "number,title,body,state,isDraft,headRefOid,statusCheckRollup"]
    try:
        p = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, check=False)
    except OSError as exc:
        raise GateError(f"{' '.join(cmd)}: {exc.strerror or exc}") from None
    if p.returncode != 0:
        if "no pull requests found" in p.stderr.lower():
            return None
        raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")
    try:
        return json.loads(p.stdout)
    except ValueError as exc:
        raise GateError(f"{' '.join(cmd)}: not JSON: {exc}") from None


def check_runs(pr: dict) -> list:
    """Every check on the PR, required or not, as {name, conclusion}. A CheckRun carries
    name/conclusion (conclusion is "" while it runs); a commit StatusContext carries context/state."""
    if not isinstance(pr, dict) or not isinstance(pr.get("statusCheckRollup"), list):
        raise GateError("check_runs: PR JSON has no statusCheckRollup list")
    runs = []
    for item in pr["statusCheckRollup"]:
        if not isinstance(item, dict):
            raise GateError(f"check_runs: statusCheckRollup item is not an object: {item!r}")
        name = item.get("name") or item.get("context") or ""
        conclusion = item.get("conclusion") or item.get("state") or ""
        runs.append({"name": name, "conclusion": conclusion})
    return runs


def commit_exists(root, sha) -> bool:
    """True when `sha` (full or abbreviated hex) names a commit in the repository at `root`
    (`git cat-file -e <sha>^{commit}`); False for an unknown, ambiguous or non-commit object;
    any other git failure (not a repository, no git) is a GateError."""
    if not re.fullmatch(r"[0-9a-f]{4,40}", sha):
        return False
    cmd = ["git", "cat-file", "-e", f"{sha}^{{commit}}"]
    _check_root(cmd, str(root))
    try:
        p = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, check=False, env=_env())
    except OSError as exc:
        raise GateError(f"{' '.join(cmd)}: {exc.strerror or exc}") from None
    if p.returncode == 0:
        return True
    if p.returncode == 128 and ("Not a valid object name" in p.stderr or "could not get object info" in p.stderr):
        return False
    raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")


def tags_at(root, sha) -> list:
    return [l for l in _git(root, "tag", "--points-at", sha).splitlines() if l]
