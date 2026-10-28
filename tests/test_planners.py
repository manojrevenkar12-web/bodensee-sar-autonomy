import numpy as np

from sar.planners import BASELINES, observe
from sar.runner import run
from sar.sim.build import make_env


def test_no_planner_ever_breaks_the_rules(dev_specs):
    for s in dev_specs:
        for name in ("random", "iamsar", "greedy", "lookahead"):
            p = BASELINES[name](0) if name == "random" else BASELINES[name]()
            o = run(make_env(s, 0), p, s.id, 0)
            assert o.violations == 0, (s.id, name)
            assert o.interventions == 0, (s.id, name)  # planners respect the mask themselves


def test_planners_are_ranked_as_search_theory_expects(dev_specs):
    """Expected success (belief): planning on the probability map beats a fixed pattern, which
    beats flying at random. Checked on the planners' own estimates, not on the truth."""
    exp = {k: [] for k in ("random", "iamsar", "greedy", "lookahead")}
    for s in dev_specs[:6]:
        for name in exp:
            p = BASELINES[name](1) if name == "random" else BASELINES[name]()
            exp[name].append(run(make_env(s, 1), p, s.id, 1).pos_expected)
    m = {k: float(np.mean(v)) for k, v in exp.items()}
    assert m["random"] < m["iamsar"] < m["greedy"] <= m["lookahead"] + 0.02


def test_shield_masks_are_what_the_planners_get(spec):
    env = make_env(spec, 0)
    o = observe(env)
    for k in range(env.n_agents):
        assert o.mask[k].tolist() == [(env.action_mask(k) >> a) & 1 for a in range(9)]
