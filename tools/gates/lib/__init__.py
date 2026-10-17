"""Gate framework -- the umbrella plan's "Gate contract" (docs/superpowers/plans/2026-10-17-strict-delivery.md).

Every gate module exposes `check(args: list) -> Result` and
`def main(argv=None): return run_gate("<name>", check, argv)`.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from dataclasses import asdict, dataclass, field
from typing import Callable, List, Optional

import yaml

# The checkout that holds this gate code (tools/gates/lib/__init__.py -> four dirnames up).
# Config is read from here, never from a gate's --root, so a fixture tree needs no config copy.
CODE_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))


@dataclass
class Line:
    status: str   # "PASS" | "FAIL"
    id: str       # e.g. "register.owner.A2-IO-N2"
    reason: str = ""


@dataclass
class Result:
    gate: str
    lines: List[Line] = field(default_factory=list)

    def ok(self) -> bool:
        return all(l.status == "PASS" for l in self.lines) and bool(self.lines)

    def fail(self, id: str, reason: str) -> None:
        self.lines.append(Line("FAIL", id, reason))

    def passed(self, id: str) -> None:
        self.lines.append(Line("PASS", id))

    def render(self, as_json: bool) -> str:
        if as_json:
            return json.dumps({"gate": self.gate, "ok": self.ok(),
                               "lines": [asdict(l) for l in self.lines]}, indent=1)
        return "\n".join(f"{l.status} {l.id}" + (f": {l.reason}" if l.reason else "")
                         for l in self.lines)


class GateError(Exception):
    """Raised for a missing/unreadable input; main() converts it to FAIL <gate>.input."""


def repo_root() -> str:
    """CLAUDE_PROJECT_DIR if set, else `git rev-parse --show-toplevel`."""
    env = os.environ.get("CLAUDE_PROJECT_DIR")
    if env:
        return env
    try:
        p = subprocess.run(["git", "rev-parse", "--show-toplevel"],
                           capture_output=True, text=True, check=False)
    except OSError as exc:
        raise GateError(f"git rev-parse --show-toplevel: {exc.strerror or exc}") from None
    if p.returncode != 0:
        raise GateError(f"git rev-parse --show-toplevel: {p.stderr.strip()}")
    return p.stdout.strip()


class _StrictLoader(yaml.SafeLoader):
    """SafeLoader that refuses a duplicate key (PyYAML silently keeps the last one) and merge keys."""


def _mapping_no_duplicates(loader, node, deep=False):
    seen = set()
    for key_node, _ in node.value:
        if key_node.tag == "tag:yaml.org,2002:merge":
            raise yaml.constructor.ConstructorError(
                None, None, "merge keys (<<) are not allowed: write every key out", key_node.start_mark)
        key = loader.construct_object(key_node, deep=deep)
        try:
            duplicate = key in seen
        except TypeError:
            raise yaml.constructor.ConstructorError(
                None, None, f"unhashable mapping key {key!r}", key_node.start_mark) from None
        if duplicate:
            raise yaml.constructor.ConstructorError(
                None, None, f"duplicate key {key!r}", key_node.start_mark)
        seen.add(key)
    return loader.construct_mapping(node, deep=deep)


_StrictLoader.add_constructor(yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG, _mapping_no_duplicates)


def load_yaml(path: str):
    """Parse one YAML file strictly. Missing, unreadable, invalid or duplicate-key -> GateError."""
    try:
        with open(path, encoding="utf-8") as fh:
            return yaml.load(fh, Loader=_StrictLoader)  # noqa: S506 -- _StrictLoader is a SafeLoader
    except OSError as exc:
        raise GateError(f"{path}: {exc.strerror or exc}") from None
    except UnicodeDecodeError:
        raise GateError(f"{path}: not valid UTF-8") from None
    except yaml.YAMLError as exc:
        raise GateError(f"{path}: invalid YAML: {exc}") from None


def gate_args(prog: str) -> argparse.ArgumentParser:
    """The arguments every gate shares: --root (the tree whose registers are checked)."""
    ap = argparse.ArgumentParser(prog=prog)
    ap.add_argument("--root", default=None,
                    help="tree holding docs/superpowers/state (default: repo_root())")
    return ap


def run_check(name: str, check: Callable[[list], Result], args: list) -> Result:
    """check(args), with every failure mode turned into a FAIL line (fail closed)."""
    try:
        result = check(list(args))
    except SystemExit as exc:          # argparse: bad arguments are a FAIL, never a skip
        result = Result(name)
        result.fail(f"{name}.input", f"bad arguments (argparse exit {exc.code})")
    except Exception as exc:           # GateError and anything unexpected
        result = Result(name)
        result.fail(f"{name}.input", str(exc) or type(exc).__name__)
    if not isinstance(result, Result):
        result = Result(name)
        result.fail(f"{name}.input", "check() did not return a Result")
    if not result.lines:
        result.fail(f"{name}.empty", "gate produced no verdict")
    return result


def run_gate(name: str, check: Callable[[list], Result], argv: Optional[list] = None) -> int:
    """Parses --json (recognised only as argv[0], where the CLI table puts it; anywhere else it is
    an ordinary token, e.g. another option's value) and gate-specific args, runs check(args) -> Result, prints, returns 0 if ok else 1.
    Any exception (incl. GateError) becomes a FAIL line `<name>.input: <message>` and rc 1.
    An empty Result (no lines) is a FAIL `<name>.empty: gate produced no verdict` (fail closed)."""
    argv = list(sys.argv[1:] if argv is None else argv)
    as_json = bool(argv) and argv[0] == "--json"
    result = run_check(name, check, argv[1:] if as_json else argv)
    print(result.render(as_json))
    return 0 if result.ok() else 1
