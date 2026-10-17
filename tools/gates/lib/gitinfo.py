"""git/gh helpers (umbrella plan, "Git/gh helpers"). Every call is
subprocess.run([...], capture_output=True, text=True, check=False); a non-zero rc raises
GateError(f"{cmd}: {stderr}") -- except `gh pr view` reporting that the branch has no PR,
which is the documented None of pr_for_branch()."""
from __future__ import annotations

import json
import subprocess

from . import GateError


def _run(cmd: list, cwd=None) -> str:
    p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise GateError(f"{' '.join(cmd)}: {p.stderr.strip()}")
    return p.stdout


def _git(root, *args) -> str:
    return _run(["git", "-C", str(root), *args])


def head_sha(root) -> str:
    return _git(root, "rev-parse", "HEAD").strip()


def branch(root) -> str:
    return _git(root, "rev-parse", "--abbrev-ref", "HEAD").strip()


def status_clean(root) -> bool:
    return _git(root, "status", "--porcelain") == ""


def worktrees(root) -> list:
    """Paths from `git worktree list --porcelain` (the main checkout first)."""
    out = _git(root, "worktree", "list", "--porcelain")
    return [l[len("worktree "):] for l in out.splitlines() if l.startswith("worktree ")]


def changed_files(root, base: str, head: str = "HEAD") -> list:
    """Paths that differ between two commits (`git diff --name-only base head`)."""
    return [l for l in _git(root, "diff", "--name-only", base, head).splitlines() if l]


def staged_files(root) -> list:
    return [l for l in _git(root, "diff", "--cached", "--name-only").splitlines() if l]


def pr_for_branch(root):
    """`gh pr view` for the checked-out branch, or None when the branch has no PR."""
    cmd = ["gh", "pr", "view", "--json",
           "number,title,body,state,isDraft,headRefOid,statusCheckRollup"]
    p = subprocess.run(cmd, cwd=str(root), capture_output=True, text=True, check=False)
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
        name = item.get("name") or item.get("context") or ""
        conclusion = item.get("conclusion") or item.get("state") or ""
        runs.append({"name": name, "conclusion": conclusion})
    return runs


def tags_at(root, sha) -> list:
    return [l for l in _git(root, "tag", "--points-at", sha).splitlines() if l]
