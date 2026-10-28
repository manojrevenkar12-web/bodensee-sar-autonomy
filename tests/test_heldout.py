"""The blind set and its first run are pinned: changing either fails here (see AGENTS.md)."""

import hashlib
import json

from sar import conf
from sar.evaluation.evaluate import set_digest

SET_SHA256 = "9a85a655008fb3ffe60a2028ff69384ed52a28cf854d7880ddc560f4012f5366"
FIRST_RUN_SHA256 = "1106ae83fd0b6c40e004c0a0dc3009678dfbac3aabb19e4139328b9ecf56f0c0"


def test_the_set_is_unchanged_and_still_held_out():
    directory = conf.path("heldout", "blind-1")
    assert set_digest(directory)[0] == SET_SHA256
    assert (directory / "STATUS").read_text().strip() == "heldout"


def test_the_first_run_is_never_rewritten():
    path = conf.path("reports", "heldout", "blind-1", "first_run.json")
    assert hashlib.sha256(path.read_bytes()).hexdigest() == FIRST_RUN_SHA256
    run = json.loads(path.read_text())
    assert run["set_sha256"] == SET_SHA256
    assert all(p["rule_violations"] == 0 for p in run["planners"].values())
