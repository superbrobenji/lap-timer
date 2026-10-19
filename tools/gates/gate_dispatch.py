#!/usr/bin/env python3
"""gate_dispatch -- a subagent dispatch prompt obeys the strict-delivery dispatch grammar.

Grammar and rules: docs/superpowers/plans/2026-10-17-strict-delivery.md, "Dispatch prompt grammar".
Usage: gate_dispatch.py [--json] --prompt P|- [--tool-model M] [--prompts-dir D] | [--json] --config-check
(--json is read by run_gate and only as the first argument, like every S1 gate.)
"""
from __future__ import annotations

import argparse
import os
import re
import sys

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)

from tools.gates.lib import CODE_ROOT, Result, gate_args, repo_root, run_gate  # noqa: E402
from tools.gates.lib import config as gconfig  # noqa: E402
from tools.gates.gate_claims import stale_reason, verified_at_head  # noqa: E402
from tools.gates.gate_decisions import REF as DEC_RE  # noqa: E402    # one scan pattern for both gates
from tools.gates.lib import gitinfo, registers  # noqa: E402

ROLES = ["controller", "implementer", "implementer_transcription", "task_reviewer", "final_reviewer",
         "final_verifier", "exception_reviewer", "audit_reviewer", "audit_verifier", "investigator"]
# The umbrella names ten roles but eight prompt files; this map is the only place they meet.
ROLE_PROMPT = {"controller": "controller", "implementer": "implementer",
               "implementer_transcription": "implementer", "task_reviewer": "reviewer",
               "final_reviewer": "reviewer", "final_verifier": "verifier",
               "exception_reviewer": "exception-reviewer", "audit_reviewer": "audit-reviewer",
               "audit_verifier": "audit-verifier", "investigator": "investigator"}
IMPLEMENTERS = {"implementer", "implementer_transcription"}
BRIEF_ROLES = IMPLEMENTERS | {"task_reviewer", "final_reviewer", "final_verifier", "exception_reviewer"}
FINDINGS_NONE_ROLES = {"investigator", "audit_reviewer", "audit_verifier"}
HEADERS = ["Role", "Brief", "Findings", "Claims", "Budget", "Model", "Report"]
STRICT, TASK = "## Strict block\n", "## Task\n"
BUDGET_RE = re.compile(r"^tokens=(\d+) minutes=(\d+)$")
DEFAULT_PROMPTS = os.path.expanduser("~/.claude/skills/strict-delivery/prompts")


class Grammar(Exception):
    """The prompt's sections are missing or out of order."""


def parse(text: str) -> tuple[dict, int]:
    """(header fields, offset of the first character after '## Strict block\\n')."""
    fields, pos = {}, 0
    for key in HEADERS:
        while text.startswith("\n", pos):
            pos += 1
        end = text.find("\n", pos)
        line = text[pos:end] if end >= 0 else text[pos:]
        prefix = f"## {key}: "
        if end < 0 or not line.startswith(prefix) or not line[len(prefix):].strip():
            raise Grammar(f"expected '{prefix}<value>' as the next section, got {line[:60]!r}")
        fields[key] = line[len(prefix):].strip()
        pos = end + 1
    while text.startswith("\n", pos):
        pos += 1
    if not text.startswith(STRICT, pos):
        raise Grammar(f"expected '## Strict block' after '## Report:', got {text[pos:pos + 60]!r}")
    return fields, pos + len(STRICT)


def id_list(value: str) -> list[str]:
    if value.strip().lower() == "none":
        return []
    return [v.strip() for v in value.split(",")]


def resolve(root: str, path: str) -> str:
    return path if os.path.isabs(path) else os.path.join(root, path)


def read_prompt(path: str) -> str:
    """'--prompt -' reads the prompt from stdin (umbrella "CLI flags and conventions")."""
    if path == "-":
        return sys.stdin.read()
    with open(path, encoding="utf-8", newline="") as f:
        return f.read()


def claim_fresh(root: str, claim: dict, head: str) -> str | None:
    """None when the row is verified at head -- S1's rule (gate_claims.verified_at_head): state verified
    and no cited path changed between verified_at and head or in the index; otherwise the reason."""
    if claim.get("state") != "verified":
        return f"state is {claim.get('state')!r}, not 'verified'"
    if verified_at_head(root, claim, head):
        return None
    return stale_reason(root, claim, head, gitinfo.staged_files(root)) or "row fails the claims schema"


def _model(conf, role, model, tool_model, res):
    rank, need, fable = conf["model_rank"], conf["models"][role], conf["fable_roles"]
    if model not in rank:
        res.fail("dispatch.model.known", f"model {model!r} not in model_rank {sorted(rank)}")
        return
    res.passed("dispatch.model.known")
    if rank[model] < rank[need]:
        res.fail("dispatch.model.rank", f"role {role} needs at least {need}, got {model}")
    else:
        res.passed("dispatch.model.rank")
    if model == "fable" and role not in conf["fable_roles"]:
        res.fail("dispatch.model.fable", f"fable is reserved for {', '.join(fable)}; role is {role}")
    else:
        res.passed("dispatch.model.fable")
    if tool_model is None:
        return
    if not tool_model:
        res.fail("dispatch.model.agree", "tool_input.model is not set; the subagent would inherit the session model")
    elif tool_model != model:
        res.fail("dispatch.model.agree", f"tool_input.model {tool_model!r} != '## Model: {model}'")
    else:
        res.passed("dispatch.model.agree")


def _brief(root, role, value, res):
    if value.lower() == "none" and role not in BRIEF_ROLES:
        res.passed("dispatch.brief")
    elif value.lower() != "none" and os.path.isfile(resolve(root, value)):
        res.passed("dispatch.brief")
    else:
        res.fail("dispatch.brief", f"role {role} needs an existing brief file; got {value!r}")


def _findings(root, role, value, res):
    ids = id_list(value)
    if not ids:
        if role in FINDINGS_NONE_ROLES:
            res.passed("dispatch.findings")
        else:
            res.fail("dispatch.findings", f"role {role} must name the register ids it works on")
        return
    findings = registers.load_findings(root)
    for fid in ids:
        f = registers.finding_by_id(findings, fid)
        if f is None:
            res.fail(f"dispatch.findings.{fid}", "not in docs/superpowers/state/findings.yml")
        elif role in IMPLEMENTERS and f["state"] not in ("planned", "in-progress"):
            res.fail(f"dispatch.findings.{fid}",
                     f"state {f['state']!r}; an implementer takes only planned or in-progress rows")
        else:
            res.passed(f"dispatch.findings.{fid}")


def _claims(root, value, res):
    ids = id_list(value)
    if not ids:
        res.passed("dispatch.claims")
        return
    by_id = {c["id"]: c for c in registers.load_claims(root)}
    head = gitinfo.head_sha(root)
    for cid in ids:
        c = by_id.get(cid)
        reason = "not in docs/superpowers/state/claims.yml" if c is None else claim_fresh(root, c, head)
        if reason:
            res.fail(f"dispatch.claims.{cid}", reason)
        else:
            res.passed(f"dispatch.claims.{cid}")


def _budget(conf, role, value, res):
    m = BUDGET_RE.match(value)
    if not m:
        res.fail("dispatch.budget", f"expected 'tokens=<n> minutes=<n>', got {value!r}")
        return
    tokens, minutes = int(m.group(1)), int(m.group(2))
    t = conf["triggers"]
    if role == "controller":
        cap_t, cap_m = t["controller"]["tokens"], t["controller"]["minutes"]
    elif role in IMPLEMENTERS:
        cap_t, cap_m = t["implementer"]["tokens"], None
    else:
        cap_t, cap_m = t["reviewer"]["tokens"], None
    if tokens <= 0 or minutes <= 0:
        res.fail("dispatch.budget", "tokens and minutes must be positive")
    elif tokens > cap_t or (cap_m is not None and minutes > cap_m):
        res.fail("dispatch.budget", f"budget {value!r} exceeds the {role} trigger (tokens {cap_t}"
                 + (f", minutes {cap_m})" if cap_m is not None else ")"))
    else:
        res.passed("dispatch.budget")


def _report(root, value, res):
    target = resolve(root, value)
    folder = os.path.dirname(target)
    if value.endswith("/") or os.path.isdir(target):
        res.fail("dispatch.report", f"report path {value!r} names a directory; it must name a file")
    elif os.path.isdir(folder):
        res.passed("dispatch.report")
    else:
        res.fail("dispatch.report", f"report directory {os.path.dirname(value) or '.'!r} does not exist")


def _decisions(root, text, res):
    ids = sorted(set(DEC_RE.findall(text)))
    if not ids:
        res.passed("dispatch.decisions")
        return
    decisions = registers.load_decisions(root)
    known = {d.get("id") for d in decisions}
    unanswered = {d.get("id") for d in registers.open_decisions(decisions, owner=registers.owner_handle())}
    for did in ids:
        if did not in known:
            res.fail(f"dispatch.decisions.{did}", "not in docs/superpowers/state/decisions.yml")
        elif did in unanswered:
            res.fail(f"dispatch.decisions.{did}", "unanswered owner decision; the path halts until the owner answers")
        else:
            res.passed(f"dispatch.decisions.{did}")


def _strict(text, start, role, prompts_dir, res):
    path = os.path.join(prompts_dir, ROLE_PROMPT[role] + ".md")
    if not os.path.isfile(path):
        res.fail("dispatch.strict", f"prompt block file {path} not found")
        return
    with open(path, encoding="utf-8", newline="") as f:
        block = f.read()
    if not block.endswith("\n"):
        res.fail("dispatch.strict", f"{path} must end with a newline")
        return
    got = text[start:start + len(block)]
    if got != block:
        i = next((k for k, (x, y) in enumerate(zip(got, block)) if x != y), min(len(got), len(block)))
        res.fail("dispatch.strict", f"strict block differs from {path} at character {i}")
        return
    rest = text[start + len(block):]
    if not rest.startswith(TASK):
        res.fail("dispatch.strict", "the strict block must be followed directly by '## Task'")
        return
    res.passed("dispatch.strict")
    if rest[len(TASK):].strip():
        res.passed("dispatch.task")
    else:
        res.fail("dispatch.task", "the '## Task' section is empty")


def check_prompt(root, conf, text, tool_model, prompts_dir, res) -> None:
    cap = conf["dispatch"]["max_prompt_chars"]
    if len(text) > cap:
        res.fail("dispatch.length", f"{len(text)} chars > dispatch.max_prompt_chars {cap}")
    else:
        res.passed("dispatch.length")
    try:
        fields, start = parse(text)
    except Grammar as e:
        res.fail("dispatch.grammar", str(e))
        return
    res.passed("dispatch.grammar")
    role = fields["Role"]
    if role not in ROLES or role not in conf["models"]:
        res.fail("dispatch.role", f"unknown role {role!r}; roles: {', '.join(ROLES)}")
        return
    res.passed("dispatch.role")
    _model(conf, role, fields["Model"], tool_model, res)
    _brief(root, role, fields["Brief"], res)
    _findings(root, role, fields["Findings"], res)
    _claims(root, fields["Claims"], res)
    _budget(conf, role, fields["Budget"], res)
    _report(root, fields["Report"], res)
    _decisions(root, text, res)
    _strict(text, start, role, prompts_dir, res)


def check_config(conf: dict, res: Result) -> None:
    """CI mode: the model map, fable_roles and the prompt cap agree with the dispatch grammar."""
    models, rank, fable = conf["models"], conf["model_rank"], set(conf["fable_roles"])
    for role in ROLES:
        need = models.get(role)
        if need not in rank:
            res.fail(f"dispatch.config.{role}", f"minimum model {need!r} not in model_rank")
        elif need == "fable" and role not in fable:
            res.fail(f"dispatch.config.{role}", "minimum model fable but the role is not in fable_roles")
        elif ROLE_PROMPT.get(role) is None:
            res.fail(f"dispatch.config.{role}", "no prompt file mapped for the role")
        else:
            res.passed(f"dispatch.config.{role}")
    extra = sorted(set(models) - set(ROLES))
    if extra:
        res.fail("dispatch.config.models", f"roles outside the dispatch grammar: {', '.join(extra)}")
    else:
        res.passed("dispatch.config.models")
    stray = sorted(fable - set(ROLES))
    if stray:
        res.fail("dispatch.config.fable_roles", f"unknown roles: {', '.join(stray)}")
    else:
        res.passed("dispatch.config.fable_roles")
    cap = conf["dispatch"]["max_prompt_chars"]
    if isinstance(cap, int) and not isinstance(cap, bool) and cap > 0:
        res.passed("dispatch.config.max_prompt_chars")
    else:
        res.fail("dispatch.config.max_prompt_chars", f"must be a positive integer, got {cap!r}")


def check(args: list) -> Result:
    p = gate_args("gate_dispatch")
    mode = p.add_mutually_exclusive_group(required=True)
    mode.add_argument("--prompt")
    mode.add_argument("--config-check", action="store_true")
    p.add_argument("--tool-model")
    p.add_argument("--prompts-dir", default=DEFAULT_PROMPTS)
    a = p.parse_args(args)
    root = a.root or repo_root()
    conf = gconfig.load(CODE_ROOT)
    res = Result("dispatch")
    if a.config_check:
        check_config(conf, res)
        return res
    check_prompt(root, conf, read_prompt(a.prompt), a.tool_model, a.prompts_dir, res)
    return res


def main(argv=None):
    return run_gate("dispatch", check, argv)


if __name__ == "__main__":
    sys.exit(main())
