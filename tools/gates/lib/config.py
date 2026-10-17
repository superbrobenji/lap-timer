"""tools/gates/config.yml loader (umbrella plan, "Config")."""
from __future__ import annotations

import os

from . import GateError, load_yaml

REQUIRED = ["owner", "triggers", "models", "model_rank", "fable_roles", "block", "rulings",
            "dispatch", "merge_markers", "comment_patterns", "exceptions", "tag_pattern"]


def load(root) -> dict:
    """Parse <root>/tools/gates/config.yml; a missing file, key or owner handle is a GateError."""
    path = os.path.join(root, "tools", "gates", "config.yml")
    data = load_yaml(path)
    if not isinstance(data, dict):
        raise GateError(f"{path}: top level must be a mapping")
    missing = [k for k in REQUIRED if k not in data]
    if missing:
        raise GateError(f"{path}: missing keys: {', '.join(missing)}")
    owner = data["owner"]
    handle = owner.get("handle") if isinstance(owner, dict) else None
    if not isinstance(handle, str) or not handle.strip():
        raise GateError(f"{path}: owner.handle must be a non-empty string")
    return data
