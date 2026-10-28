"""Decisions are made from what a coordinator can know. Planners, the policy, training and the
planning API never read the simulator's truth; and changing the truth changes none of their
decisions (only the score)."""

import ast
from pathlib import Path

import numpy as np
import pytest

from sar import conf
from sar.planners import BASELINES, observe
from sar.sim.build import make_env
from sar.spec import ScenarioSpec

DECISION_MODULES = [
    "src/sar/planners/__init__.py",
    "src/sar/policy.py",
    "src/sar/train/envs.py",
    "src/sar/train/module.py",
    "src/sar/train/train.py",
    "src/sar/api/planning.py",
]


@pytest.mark.parametrize("path", DECISION_MODULES)
def test_decision_code_never_touches_the_truth(path):
    tree = ast.parse(Path(conf.ROOT, path).read_text())
    for node in ast.walk(tree):
        if isinstance(node, ast.Attribute):
            assert node.attr not in {"truth_state", "truth"}, f"{path}:{node.lineno} reads the truth"
        if isinstance(node, ast.ImportFrom) and node.module:
            assert node.module not in {"sar.runner", "sar.api.debrief", "sar.evaluation.evaluate"}, (
                f"{path} imports a scoring module"
            )


def poisoned(spec: ScenarioSpec) -> ScenarioSpec:
    d = spec.model_dump(mode="json")
    d["truth"] = {
        "object": "dinghy_capsized",
        "wind_speed": 14.0,
        "wind_from_deg": 90.0,
        "entered_water": {"lat": 47.62, "lon": 9.45},
    }
    return ScenarioSpec.model_validate(d)


def planners():
    names = ["iamsar", "greedy", "lookahead"]
    if conf.path("models", "policy.onnx").exists():
        names.append("rl")
    return names


@pytest.mark.parametrize("name", planners())
def test_a_different_truth_changes_no_decision(spec, name):
    from sar.api.planning import make

    env_a, env_b = make_env(spec, 4), make_env(poisoned(spec), 4)
    pa, pb = make(name), make(name)
    oa, ob = observe(env_a), observe(env_b)
    pa.reset(oa)
    pb.reset(ob)
    while not env_a.done:
        assert np.array_equal(oa.obs, ob.obs) and np.array_equal(oa.mask, ob.mask)
        acts = pa.act(oa)
        assert acts == pb.act(ob)
        ia, ib = env_a.step(acts), env_b.step(acts)
        assert ia.reward == ib.reward
        oa, ob = observe(env_a), observe(env_b)
    assert env_b.done
    # the truths really were different
    assert (env_a.truth_state().pos.x, env_a.truth_state().pos.y) != (
        env_b.truth_state().pos.x,
        env_b.truth_state().pos.y,
    )


def test_baselines_registry():
    assert set(BASELINES) == {"random", "iamsar", "greedy", "lookahead"}
