"""Statistics for comparing planners: stratified bootstrap confidence intervals (resample seeds
within each scenario, so every scenario keeps its weight) and paired differences on the same
scenario and seed."""

from __future__ import annotations

from collections import defaultdict
from collections.abc import Sequence

import numpy as np


def strata(keys: Sequence[str]) -> dict[str, np.ndarray]:
    groups: dict[str, list[int]] = defaultdict(list)
    for i, k in enumerate(keys):
        groups[k].append(i)
    return {k: np.array(v) for k, v in groups.items()}


def bootstrap_mean(
    values: np.ndarray, keys: Sequence[str], resamples: int, seed: int, confidence: float
) -> tuple[float, float, float]:
    """Mean over scenarios of the per-scenario mean, with a stratified percentile interval."""
    values = np.asarray(values, dtype=float)
    groups = strata(keys)
    rng = np.random.default_rng(seed)
    point = float(np.mean([values[idx].mean() for idx in groups.values()]))
    draws = np.empty(resamples)
    for r in range(resamples):
        draws[r] = np.mean([values[rng.choice(idx, size=len(idx), replace=True)].mean() for idx in groups.values()])
    lo, hi = np.quantile(draws, [(1 - confidence) / 2, 1 - (1 - confidence) / 2])
    return point, float(lo), float(hi)


def restricted_mean_time(found_minute: Sequence[float | None], horizon_min: float) -> float:
    """Mean time to find, counting a search that never found the object as the full horizon (a
    restricted mean survival time: lower is better and it is defined even when nothing is found)."""
    return float(np.mean([horizon_min if m is None else min(m, horizon_min) for m in found_minute]))
