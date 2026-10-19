"""S2 test repositories, built on S1's gitfixture. make_repo() commits the given files, then writes the
three registers from fixtures/rows.yml with every "@BASE@" value replaced by the sha of that commit, and
commits again. Config is never copied: gates read it from CODE_ROOT (S1). Tests point --root or
CLAUDE_PROJECT_DIR at the repo.
"""
from __future__ import annotations

import copy
import os
import re

import yaml

from tools.gates.tests import gitfixture as gf

REAL = gf.REPO
FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")
STATE = "docs/superpowers/state"
DISPATCH_FILES = {
    "docs/cited.md": "cited\n",
    "docs/moved.md": "moved once\n",
    "docs/brief.md": "# Brief\nfixture task 1\n",
    "reports/.gitkeep": "",
    "docs/superpowers/plans/fixture-plan.md": "# Fixture plan\n",
}
git, write, remove = gf.git, gf.write, gf.remove
FALLBACK_LINE = re.compile(r"^fable_fallback:.*\n", re.M)


def config_text(fallback: str | None = None) -> str:
    """The real tools/gates/config.yml with its one fable_fallback line removed, then
    'fable_fallback: <fallback>' appended (None: the key is absent, the DEC-0018 fallback is off)."""
    with open(os.path.join(REAL, "tools", "gates", "config.yml"), encoding="utf-8") as f:
        real = f.read()
    text, n = FALLBACK_LINE.subn("", real)
    if n != 1:
        raise AssertionError(f"tools/gates/config.yml holds {n} fable_fallback lines, expected 1")
    return text if fallback is None else text + f"fable_fallback: {fallback}\n"


def commit(root: str, message: str) -> str:
    return gf.commit_all(root, message)


def load_rows() -> dict:
    with open(os.path.join(FIX, "rows.yml"), encoding="utf-8") as f:
        return yaml.safe_load(f)


def _subst(obj, base: str):
    if isinstance(obj, dict):
        return {k: _subst(v, base) for k, v in obj.items()}
    if isinstance(obj, list):
        return [_subst(v, base) for v in obj]
    return base if obj == "@BASE@" else obj


def write_registers(root: str, rows: dict, base: str) -> None:
    rows = _subst(copy.deepcopy(rows), base)
    for key in ("findings", "claims", "decisions"):
        write(root, f"{STATE}/{key}.yml", yaml.safe_dump(rows.get(key, []), sort_keys=False, allow_unicode=True))


def make_repo(files: dict, rows: dict, after: dict | None = None) -> str:
    root = gf.new_repo()
    for rel, text in files.items():
        write(root, rel, text)
    base = commit(root, "fixture: base files")
    write_registers(root, rows, base)
    commit(root, "fixture: registers")
    if after:
        for rel, text in after.items():
            write(root, rel, text)
        commit(root, "fixture: after")
    return root
