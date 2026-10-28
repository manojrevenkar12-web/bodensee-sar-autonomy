"""Random scenarios for training, drawn from config/sim.yaml: training_distribution. The dev and
held-out scenario files are never used for training."""

from __future__ import annotations

import math

import numpy as np

from sar import conf
from sar.sim.lake import lake, projection
from sar.spec import ScenarioSpec


def _nearest_site(x: float, y: float) -> str:
    best, key = math.inf, ""
    for s in lake().sites:
        d = math.hypot(s.pos.x - x, s.pos.y - y)
        if d < best:
            best, key = d, s.name
    return key


def random_spec(seed: int) -> ScenarioSpec:
    rng = np.random.default_rng([7, int(seed)])
    d = conf.read("sim")["training_distribution"]
    objects = list(conf.read("objects")["objects"])
    shore = lake().shoreline
    xs, ys = [p.x for p in shore], [p.y for p in shore]
    while True:
        x, y = rng.uniform(min(xs), max(xs)), rng.uniform(min(ys), max(ys))
        if lake().is_water(x, y):
            break
    lat, lon = projection().to_geo(x, y)
    if rng.uniform() < d["mixed_object_report"]:
        pick = rng.choice(objects, size=2, replace=False)
        w = rng.uniform(0.3, 0.7)
        reported = {str(pick[0]): round(float(w), 3), str(pick[1]): round(float(1 - w), 3)}
    else:
        reported = {str(rng.choice(objects)): 1.0}
    return ScenarioSpec.model_validate(
        {
            "id": f"train-{seed}",
            "incident": {
                "last_known": {"lat": lat, "lon": lon},
                "position_error_m": float(rng.uniform(*d["position_error_m"])),
                "minutes_since": float(rng.uniform(*d["delay_min"])),
                "reported_objects": reported,
                "time_of_day": "night" if rng.uniform() < d["night_share"] else "day",
            },
            "forecast": {"wind_speed": float(rng.uniform(*d["wind_speed"])), "wind_from_deg": float(rng.uniform(0, 360))},
            "uavs": {
                "count": int(rng.integers(d["n_uav"][0], d["n_uav"][1] + 1)),
                "endurance_min": float(rng.uniform(*d["endurance_min"])),
                "base": _nearest_site(x, y),
            },
            "search_minutes": float(d["search_minutes"]),
        }
    )
