"""Register loaders, schema validators and lifecycle rules (umbrella plan, "Registers";
spec §4). Every validator returns a list of "<field>: <problem>" strings, [] when the row is ok."""
from __future__ import annotations

import os
import re

from . import GateError, load_yaml

FINDING_STATES = ["open", "planned", "in-progress", "fixed-unpinned", "pinned", "deferred", "closed", "merged"]
FINDING_TRANSITIONS = {  # from -> allowed to
    "open": ["planned", "deferred", "closed"], "planned": ["in-progress", "open", "deferred"],
    "in-progress": ["fixed-unpinned", "pinned", "planned"], "fixed-unpinned": ["pinned"],
    "pinned": ["closed", "merged"], "deferred": ["open", "planned"], "closed": ["merged"], "merged": []}
CLAIM_STATES = ["unverified", "verified", "stale", "false"]
DECISION_CLASSES = ["triage", "spec-direction", "scope", "merge-grant", "hardware", "model-policy",
                    "issue-close", "finding-class"]
OWNER_KINDS = ["plan", "issue", "roadmap"]

SEVERITIES = ["CRITICAL", "HIGH", "MEDIUM", "LOW", "INFO"]
FINDING_ID = re.compile(r"^[A-Z0-9]+(-[A-Za-z0-9]+)+$")
CLAIM_ID = re.compile(r"^C-[A-Za-z0-9.§-]+$")
DECISION_ID = re.compile(r"^DEC-\d{4}$")
SHA = re.compile(r"^[0-9a-f]{7,40}$")
STATE_DIR = os.path.join("docs", "superpowers", "state")
POSTMERGE = "postmerge"   # the only `by` allowed on a history entry that sets `merged`

FINDING_KEYS = ["id", "source", "severity", "class", "siblings", "cites", "repro", "owner", "state",
                "pin", "ruling", "evidence", "history"]
CLAIM_KEYS = ["id", "doc", "anchor", "text", "direct", "contra", "cites", "verified_at", "by",
              "state", "finding"]
DECISION_KEYS = ["id", "class", "question", "options", "asked_at", "answer", "answered_at"]
OWNER_SHAPES = {"plan": {"kind", "plan", "task"}, "issue": {"kind", "number"}, "roadmap": {"kind", "plan"}}


def _load(root, name) -> list:
    path = os.path.join(root, STATE_DIR, name)
    data = load_yaml(path)
    if data is None:
        raise GateError(f"{path}: empty file (an empty register is the YAML list [])")
    if not isinstance(data, list):
        raise GateError(f"{path}: top level must be a YAML list")
    for i, row in enumerate(data):
        if not isinstance(row, dict):
            raise GateError(f"{path}: entry {i} is not a mapping")
    return data


def load_findings(root) -> list:
    return _load(root, "findings.yml")


def load_claims(root) -> list:
    return _load(root, "claims.yml")


def load_decisions(root) -> list:
    return _load(root, "decisions.yml")


def _text(v) -> bool:
    return isinstance(v, str) and v.strip() != ""


def _sha(v) -> bool:
    return isinstance(v, str) and SHA.match(v) is not None


def _sha_problem(field: str, v) -> list:
    if _sha(v):
        return []
    if isinstance(v, int) and not isinstance(v, bool):
        return [f"{field}: {v} parsed as a number; quote the sha (\"{v}\")"]
    return [f"{field}: must be a 7-40 char lowercase hex sha string, got {v!r}"]


def _keys(row: dict, keys: list) -> list:
    errs = []
    missing = [k for k in keys if k not in row]
    if missing:
        errs.append(f"keys: missing {', '.join(missing)}")
    extra = sorted(set(row) - set(keys))
    if extra:
        errs.append(f"keys: unknown {', '.join(map(str, extra))}")
    return errs


def _file_under(root, rel) -> bool:
    """True when `rel` names an existing file inside `root` (an absolute path or a `..` that
    leaves the root does not count)."""
    base = os.path.realpath(root)
    path = os.path.realpath(os.path.join(base, rel))
    return os.path.commonpath([base, path]) == base and os.path.isfile(path)


def owner_errors(owner) -> list:
    """`{kind: plan, plan, task}` | `{kind: issue, number}` | `{kind: roadmap, plan}`."""
    if owner is None:
        return ["owner: missing (every finding needs an owner: a plan task, an issue or a roadmap plan)"]
    if not isinstance(owner, dict) or owner.get("kind") not in OWNER_KINDS:
        return [f"owner: kind must be one of {OWNER_KINDS}, got {owner!r}"]
    kind = owner["kind"]
    if set(owner) != OWNER_SHAPES[kind]:
        return [f"owner: kind {kind} needs exactly the keys {sorted(OWNER_SHAPES[kind])}"]
    errs = []
    if kind == "issue":
        n = owner["number"]
        if isinstance(n, bool) or not isinstance(n, int) or n <= 0:
            errs.append(f"owner: issue number must be a positive integer, got {n!r}")
    else:
        p = owner["plan"]
        if not (_text(p) or (isinstance(p, int) and not isinstance(p, bool) and p > 0)):
            errs.append(f"owner: plan must be a plan name or number, got {p!r}")
    if kind == "plan":
        t = owner["task"]
        if not (_text(t) or (isinstance(t, int) and not isinstance(t, bool) and t > 0)):
            errs.append(f"owner: task must be a task number or id, got {t!r}")
    return errs


def validate_finding(f: dict) -> list:
    errs = _keys(f, FINDING_KEYS)
    if any(e.startswith("keys: missing") for e in errs):
        return errs
    if not isinstance(f["id"], str) or not FINDING_ID.match(f["id"]):
        errs.append(f"id: {f['id']!r} does not match {FINDING_ID.pattern}")
    if not _text(f["source"]):
        errs.append("source: must be a non-empty string")
    if f["severity"] not in SEVERITIES:
        errs.append(f"severity: must be one of {SEVERITIES}, got {f['severity']!r}")
    if not _text(f["class"]):
        errs.append("class: the reviewer's class statement, verbatim, non-empty")
    if not isinstance(f["siblings"], list) or not all(_text(s) for s in f["siblings"]):
        errs.append("siblings: must be a list of non-empty strings (may be empty)")
    if not isinstance(f["cites"], list):
        errs.append("cites: must be a list of {file, lines, sha}")
    else:
        for i, c in enumerate(f["cites"]):
            if not isinstance(c, dict) or set(c) != {"file", "lines", "sha"}:
                errs.append(f"cites: entry {i} must be exactly {{file, lines, sha}}")
                continue
            if not _text(c["file"]) or not _text(c["lines"]):
                errs.append(f"cites: entry {i} file and lines must be non-empty strings (quote lines)")
            errs += _sha_problem(f"cites: entry {i} sha", c["sha"])
    if f["repro"] is not None and not _text(f["repro"]):
        errs.append("repro: must be a path string or null")
    errs += owner_errors(f["owner"])
    if f["state"] not in FINDING_STATES:
        errs.append(f"state: must be one of {FINDING_STATES}, got {f['state']!r}")
    pin = f["pin"]
    if pin is not None and (not isinstance(pin, dict) or set(pin) != {"test", "transcript"}
                            or not _text(pin["test"]) or not _text(pin["transcript"])):
        errs.append("pin: must be null or {test, transcript}, both non-empty strings")
    if f["ruling"] is not None and not (isinstance(f["ruling"], str) and DECISION_ID.match(f["ruling"])):
        errs.append(f"ruling: must be null or a decision id DEC-NNNN, got {f['ruling']!r}")
    if not isinstance(f["evidence"], list) or not all(_text(e) for e in f["evidence"]):
        errs.append("evidence: must be a list of non-empty strings (commit shas or paths)")
    hist = f["history"]
    if not isinstance(hist, list) or not hist:
        errs.append("history: must be a non-empty list of {state, sha, by}")
    else:
        for i, h in enumerate(hist):
            if not isinstance(h, dict) or set(h) != {"state", "sha", "by"}:
                errs.append(f"history: entry {i} must be exactly {{state, sha, by}}")
                continue
            if h["state"] not in FINDING_STATES:
                errs.append(f"history: entry {i} state {h['state']!r} is not a finding state")
            errs += _sha_problem(f"history: entry {i} sha", h["sha"])
            if not _text(h["by"]):
                errs.append(f"history: entry {i} by must be a non-empty string")
        last = hist[-1]
        if isinstance(last, dict) and last.get("state") != f["state"]:
            errs.append(f"history: last entry state {last.get('state')!r} != state {f['state']!r}")
    return errs


def transition_errors(f: dict) -> list:
    """History must start at `open` and every step must be legal. Call only on a valid row."""
    states = [h["state"] for h in f["history"]]
    errs = [] if states[0] == "open" else [f"transition: history starts at {states[0]!r}, not 'open'"]
    for a, b in zip(states, states[1:]):
        if b not in FINDING_TRANSITIONS[a]:
            errs.append(f"transition: {a} -> {b} is illegal (from {a}: {FINDING_TRANSITIONS[a]})")
    return errs


def evidence_errors(f: dict, root, decisions: list) -> list:
    """What each state must carry (spec §4.1). Call only on a valid row."""
    errs = []
    for e in f["evidence"]:
        if not _sha(e) and not _file_under(root, e):
            errs.append(f"evidence: {e!r} is neither a commit sha nor a file under the repo root")
    states = [h["state"] for h in f["history"]]
    if "pinned" in states:
        pin = f["pin"]
        if pin is None:
            errs.append("evidence: pinned needs pin {test, transcript}")
        elif not _file_under(root, pin["transcript"]):
            errs.append(f"evidence: pin transcript {pin['transcript']} does not exist")
        if not any(not _sha(e) and _file_under(root, e) for e in f["evidence"]):
            errs.append("evidence: pinned needs the reviewer's confirming report path in evidence")
    if f["state"] == "deferred":
        d = next((x for x in decisions if x.get("id") == f["ruling"]), None)
        if f["ruling"] is None:
            errs.append("evidence: deferred needs ruling = the owner's decision id")
        elif d is None:
            errs.append(f"evidence: ruling {f['ruling']} is not in decisions.yml")
        elif d.get("answer") is None:
            errs.append(f"evidence: ruling {f['ruling']} is not answered by the owner")
    if f["state"] == "closed" and not f["evidence"]:
        errs.append("evidence: closed needs evidence (commits or report paths)")
    for h in f["history"]:
        if h["state"] == "merged" and h["by"] != POSTMERGE:
            errs.append(f"evidence: merged is set only by the post-merge script (by: {POSTMERGE}), got {h['by']!r}")
    return errs


def validate_claim(c: dict) -> list:
    errs = _keys(c, CLAIM_KEYS)
    if any(e.startswith("keys: missing") for e in errs):
        return errs
    if not isinstance(c["id"], str) or not CLAIM_ID.match(c["id"]):
        errs.append(f"id: {c['id']!r} does not match {CLAIM_ID.pattern}")
    for k in ("doc", "anchor", "text", "direct", "contra"):
        if not _text(c[k]):
            errs.append(f"{k}: must be a non-empty string")
    if not isinstance(c["cites"], list) or not c["cites"] or not all(_text(p) for p in c["cites"]):
        errs.append("cites: must be a non-empty list of paths (a claim citing nothing can never go stale)")
    if c["state"] is False:
        errs.append('state: YAML read false as a boolean; quote it: state: "false"')
    elif c["state"] not in CLAIM_STATES:
        errs.append(f"state: must be one of {CLAIM_STATES}, got {c['state']!r}")
    if c["verified_at"] is not None:
        errs += _sha_problem("verified_at", c["verified_at"])
    if c["by"] is not None and not _text(c["by"]):
        errs.append("by: must be null or a non-empty string")
    if c["state"] == "verified" and (c["verified_at"] is None or c["by"] is None):
        errs.append("state: verified needs verified_at and by")
    if c["state"] == "false":
        if not (isinstance(c["finding"], str) and FINDING_ID.match(c["finding"])):
            errs.append("finding: a false claim must name the finding id that records it")
    elif c["finding"] is not None:
        errs.append("finding: only a false claim carries a finding (set it to null)")
    return errs


def validate_decision(d: dict) -> list:
    errs = _keys(d, DECISION_KEYS)
    if any(e.startswith("keys: missing") for e in errs):
        return errs
    if not isinstance(d["id"], str) or not DECISION_ID.match(d["id"]):
        errs.append(f"id: {d['id']!r} does not match {DECISION_ID.pattern}")
    if d["class"] not in DECISION_CLASSES:
        errs.append(f"class: must be one of {DECISION_CLASSES}, got {d['class']!r}")
    if not _text(d["question"]):
        errs.append("question: must be a non-empty string")
    elif d["class"] == "merge-grant" and not d["question"].startswith("grant:"):
        errs.append("question: a merge-grant question is 'grant:<plan>'")
    opts = d["options"]
    if not isinstance(opts, list) or not all(
            isinstance(o, dict) and set(o) == {"label", "cost"} and _text(o["label"]) and _text(o["cost"])
            for o in opts):
        errs.append("options: must be a list of {label, cost}, both non-empty strings")
    elif len(opts) < 2 and d["class"] != "merge-grant":
        errs.append("options: an owner question needs at least two options with their cost")
    asked = d["asked_at"]
    if not isinstance(asked, dict) or set(asked) != {"sha", "by"} or not _text(asked.get("by")):
        errs.append("asked_at: must be {sha, by}")
    else:
        errs += _sha_problem("asked_at.sha", asked["sha"])
    ans = d["answer"]
    if ans is not None and not _text(ans):
        hint = " (YAML read yes/no/on/off as a boolean: quote it, e.g. answer: \"yes\")" \
            if isinstance(ans, bool) else ""
        errs.append(f"answer: must be null or a non-empty string, got {ans!r}{hint}")
    at = d["answered_at"]
    if at is not None:
        if not isinstance(at, dict) or set(at) != {"sha", "by"} or not _text(at.get("by")):
            errs.append("answered_at: must be null or {sha, by}")
        else:
            errs += _sha_problem("answered_at.sha", at["sha"])
    if (ans is None) != (at is None):
        errs.append("answered_at: answer and answered_at are both null or both set")
    return errs


def finding_by_id(findings, id):
    return next((f for f in findings if f.get("id") == id), None)


def open_decisions(decisions) -> list:
    """Decisions whose answer is None (the owner has not answered)."""
    return [d for d in decisions if d.get("answer") is None]


def grant_for(decisions, plan: str):
    """The merge grant for <plan>: class merge-grant, question 'grant:<plan>' (optionally followed
    by a space and free text), answered "yes" (case-insensitive). Any other answer is no grant."""
    for d in decisions:
        q = d.get("question")
        if d.get("class") != "merge-grant" or not isinstance(q, str):
            continue
        if q != f"grant:{plan}" and not q.startswith(f"grant:{plan} "):
            continue
        ans = d.get("answer")
        if isinstance(ans, str) and ans.strip().lower() == "yes" and d.get("answered_at") is not None:
            return d
    return None
