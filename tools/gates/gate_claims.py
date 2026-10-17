#!/usr/bin/env python3
"""gate_claims -- the claim register (spec §4.2, §7).

  python3 tools/gates/gate_claims.py [--json] [--root <tree>] [--head <sha>]
  python3 tools/gates/gate_claims.py --mark-stale --base <sha> [--root <tree>]

A row is "verified at HEAD" when its state is `verified` and no file it cites differs between
`verified_at` and HEAD, nor is staged for the next commit. (`verified_at` cannot literally equal
the HEAD that contains the row: a commit cannot hold its own sha.) A cite ending in "/" covers
every file under that directory. --mark-stale rewrites every `verified` row whose cites
intersect changed_files(base, HEAD) + the staged files to `stale`, then checks the result. Every
verified_at names a commit of the gated repository (DEC-0003); a row that does not is a FAIL
claims.stale.<id>. Row ids print forgery-safe (entry-<i> when the id is malformed), an empty
--root is an error, and a category some row never reached prints no PASS line.
"""
from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import yaml  # noqa: E402

from tools.gates.lib import GateError, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import gitinfo, registers  # noqa: E402

CATEGORIES = ["schema", "duplicate", "false", "stale"]


def hits(cites: list, changed) -> list:
    """The changed paths a claim's cites cover (exact path, or under a cite ending in '/')."""
    return sorted(p for p in set(changed)
                  if any(p == c or (c.endswith("/") and p.startswith(c)) for c in cites))


def stale_reason(root, claim: dict, head: str, staged) -> str:
    """'' when a verified claim still holds at head, else why it does not."""
    try:
        changed = gitinfo.changed_files(root, claim["verified_at"], head)
    except GateError as exc:
        return f"verified_at {claim['verified_at']} is not comparable with {head}: {exc}"
    hit = hits(claim["cites"], list(changed) + list(staged))
    if hit:
        return (f"cited file(s) changed since verified_at {claim['verified_at']}: {', '.join(hit)}; "
                f"re-verify the row or run gate_claims.py --mark-stale --base {claim['verified_at']}")
    return ""


def verified_at_head(root, claim: dict, head: str) -> bool:
    """For gate_dispatch (S2): the row is well-formed, `verified`, and not stale at head."""
    if registers.validate_claim(claim) or claim["state"] != "verified":
        return False
    return stale_reason(root, claim, head, gitinfo.staged_files(root)) == ""


def _claims_path(root) -> str:
    return os.path.join(root, registers.STATE_DIR, "claims.yml")


def mark_stale(root, claims: list, base: str, head: str, r: Result) -> None:
    changed = list(gitinfo.changed_files(root, base, head)) + gitinfo.staged_files(root)
    marked = 0
    for c in claims:
        if c.get("state") == "verified" and not registers.validate_claim(c) and hits(c["cites"], changed):
            c["state"] = "stale"
            marked += 1
            r.passed(f"claims.marked-stale.{c.get('id')}")
    if marked:
        with open(_claims_path(root), "w", encoding="utf-8") as fh:
            yaml.safe_dump(claims, fh, sort_keys=False, allow_unicode=True, width=100000)
    else:
        r.passed("claims.marked-stale.none")


def _row_id(row, index) -> str:
    """The id to print in a line id: the row's own id only when it is well-formed, else entry-<index>."""
    rid = row.get("id")
    return rid if isinstance(rid, str) and registers.CLAIM_ID.fullmatch(rid) else f"entry-{index}"


def check(args: list) -> Result:
    ap = gate_args("gate_claims")
    ap.add_argument("--head", default=None, help="sha to check against (default: HEAD of --root)")
    ap.add_argument("--mark-stale", action="store_true")
    ap.add_argument("--base", default=None)
    ns = ap.parse_args(args)
    if ns.root is not None and not ns.root.strip():
        raise GateError("--root is empty")
    root = ns.root if ns.root is not None else repo_root()
    claims = registers.load_claims(root)
    findings = registers.load_findings(root)
    r = Result("claims")
    commits = {}

    def exists(root, sha):    # one git call per distinct sha
        if sha not in commits:
            commits[sha] = gitinfo.commit_exists(root, sha)
        return commits[sha]

    skipped = set()    # categories in which some row was not evaluated: they never print a PASS line
    head = ns.head
    if ns.mark_stale:
        if not ns.base:
            raise GateError("--mark-stale needs --base <sha>")
        head = head or gitinfo.head_sha(root)
        mark_stale(root, claims, ns.base, head, r)
    staged = None
    seen = set()
    for i, c in enumerate(claims):
        cid = _row_id(c, i)
        if cid in seen:
            r.fail(f"claims.duplicate.{cid}", "id appears more than once")
        seen.add(cid)
        errs = registers.validate_claim(c)
        for e in errs:
            r.fail(f"claims.schema.{cid}", e)
        if errs:
            skipped.update(["false", "stale"])    # the false and stale rules need a well-formed row
            continue
        bad_commit = registers.claim_commit_errors(c, root, exists)
        for e in bad_commit:
            r.fail(f"claims.stale.{cid}", e)
        if bad_commit:
            continue    # nothing to compare against
        if c["state"] == "false" and registers.finding_by_id(findings, c["finding"]) is None:
            r.fail(f"claims.false.{cid}", f"finding {c['finding']} is not in findings.yml")
        if c["state"] == "verified":
            if head is None:
                head = gitinfo.head_sha(root)
            if staged is None:
                staged = gitinfo.staged_files(root)
            why = stale_reason(root, c, head, staged)
            if why:
                r.fail(f"claims.stale.{cid}", why)
    failed = {l.id.split(".")[1] for l in r.lines if l.status == "FAIL"}
    for cat in CATEGORIES:
        if cat not in failed and cat not in skipped:
            r.passed(f"claims.{cat}")
    return r


def main(argv=None):
    return run_gate("claims", check, argv)


if __name__ == "__main__":
    sys.exit(main())
