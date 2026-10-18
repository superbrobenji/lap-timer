#!/usr/bin/env python3
"""Claude Code UserPromptSubmit hook (spec §8): a one-line reminder on stdout, nothing else.
Stdlib only, so it cannot fail for a missing package; a malformed event still fails closed."""
from __future__ import annotations

import json
import sys

REMINDER = ("strict-delivery: every change goes through the pipeline (CLAUDE.md, tools/gates/); verify "
            "every claim directly and by contradiction; owner-only decisions go to "
            "docs/superpowers/state/decisions.yml as questions, then stop.")


def main() -> int:
    event = json.load(sys.stdin)
    if not isinstance(event, dict):
        raise ValueError("stdin is not a JSON object")
    print(REMINDER)
    return 0


if __name__ == "__main__":
    try:
        rc = main()
    except Exception as exc:  # fail closed
        print(f"FAIL hook.internal: {type(exc).__name__}: {exc}", file=sys.stderr)
        rc = 2
    sys.exit(rc)
