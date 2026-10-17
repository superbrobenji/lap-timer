"""Throwaway git repositories for gate tests. Every git call pins identity, signing and hooks so
the developer's global config (gpgsign, core.hooksPath, templates) cannot change the result."""
from __future__ import annotations

import os
import shutil
import subprocess
import tempfile

GIT = ["git", "-c", "user.name=gate-test", "-c", "user.email=gate-test@example.invalid",
       "-c", "commit.gpgsign=false", "-c", "tag.gpgsign=false", "-c", "core.hooksPath=/dev/null",
       "-c", "init.defaultBranch=main"]
REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))
STATE = os.path.join("docs", "superpowers", "state")


def git(root, *args) -> str:
    p = subprocess.run(GIT + ["-C", root, *args], capture_output=True, text=True, check=False)
    if p.returncode != 0:
        raise AssertionError(f"git {' '.join(args)}: {p.stderr}")
    return p.stdout.strip()


def write(root, rel, text) -> str:
    path = os.path.join(root, rel)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(text)
    return path


def new_repo(registers=None) -> str:
    """A fresh repo with one commit. `registers` maps findings/claims/decisions to YAML text
    (default "[]\\n" each); pass a value None to leave that file out."""
    root = tempfile.mkdtemp(prefix="gate-test-")
    git(root, "init", "-q")
    regs = {"findings": "[]\n", "claims": "[]\n", "decisions": "[]\n"}
    regs.update(registers or {})
    for name, text in regs.items():
        if text is not None:
            write(root, os.path.join(STATE, f"{name}.yml"), text)
    write(root, "README.md", "fixture\n")
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", "fixture")
    return root


def commit_all(root, msg="change") -> str:
    git(root, "add", "-A")
    git(root, "commit", "-q", "-m", msg)
    return git(root, "rev-parse", "HEAD")


def remove(root) -> None:
    shutil.rmtree(root, ignore_errors=True)
