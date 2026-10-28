import json

import numpy as np
import pytest

from sar import conf
from sar.evaluation.evaluate import evaluate, markdown, set_digest
from sar.evaluation.stats import bootstrap_mean, restricted_mean_time


def test_bootstrap_interval_contains_the_mean_and_shrinks_with_data():
    rng = np.random.default_rng(0)
    small = rng.binomial(1, 0.6, 40).astype(float)
    big = rng.binomial(1, 0.6, 4000).astype(float)
    p1, lo1, hi1 = bootstrap_mean(small, ["a"] * 20 + ["b"] * 20, 500, 1, 0.95)
    p2, lo2, hi2 = bootstrap_mean(big, ["a"] * 2000 + ["b"] * 2000, 500, 1, 0.95)
    assert lo1 <= p1 <= hi1 and lo2 <= p2 <= hi2
    assert hi2 - lo2 < hi1 - lo1


def test_strata_weigh_scenarios_equally():
    vals = np.array([1.0] * 10 + [0.0] * 2)
    p, _, _ = bootstrap_mean(vals, ["easy"] * 10 + ["hard"] * 2, 10, 0, 0.95)
    assert p == pytest.approx(0.5)


def test_restricted_mean_time_counts_misses_as_the_horizon():
    assert restricted_mean_time([10.0, None], 30.0) == 20.0


def test_evaluation_is_reproducible_and_paired(tmp_path):
    src = conf.path("scenarios", "dev")
    for name in ("immenstaad-swimmer-calm.yaml", "lindau-dinghy-westerly.yaml"):
        (tmp_path / name).write_text((src / name).read_text())
    a = evaluate(tmp_path, planners=["random", "greedy"], seeds=3, workers=1)
    b = evaluate(tmp_path, planners=["random", "greedy"], seeds=3, workers=1)
    assert json.dumps(a, sort_keys=True) == json.dumps(b, sort_keys=True)
    assert a["planners"]["greedy"]["runs"] == 6
    assert a["planners"]["greedy"]["rule_violations"] == 0
    assert a["set_sha256"] == set_digest(tmp_path)[0]
    assert "| greedy |" in markdown(a)
