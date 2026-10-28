"""Decision support for the coordinator: every planner flies the search ahead in the belief, so
the coordinator can compare their expected probability of success over time and see the
tracks before approving one. Reads no truth."""

from __future__ import annotations

from typing import Any

import numpy as np

from sar import _sarsim as core
from sar import conf
from sar.planners import BASELINES, Planner, observe
from sar.sim.build import env_config, make_env
from sar.sim.lake import lake, projection
from sar.spec import ScenarioSpec

FRAME_EVERY_STEPS = 4  # one frame per simulated minute


def available_planners() -> list[str]:
    names = ["iamsar", "greedy", "lookahead"]
    if conf.path("models", "policy.onnx").exists():
        names.append("rl")
    return names


def make(name: str) -> Planner:
    if name == "rl":
        from sar.policy import OnnxPolicy

        return OnnxPolicy()
    if name == "lookahead":
        return BASELINES["lookahead"](int(conf.read("evaluation")["lookahead_depth"]))
    return BASELINES[name]()


def heat(p: np.ndarray) -> list[list[int]]:
    """Belief as 0-255, scaled to its own maximum, rows from south to north."""
    m = float(p.max())
    if m <= 0:
        return np.zeros_like(p, dtype=int).tolist()
    return np.round(255.0 * np.sqrt(p / m)).astype(int).tolist()


def grid_geo(env: core.Env) -> dict[str, float]:
    proj = projection()
    o = env.grid_origin
    cell = env.config.cell_m
    lat0, lon0 = proj.to_geo(o.x, o.y)
    lat1, lon1 = proj.to_geo(o.x + cell, o.y + cell)
    return {"lat0": lat0, "lon0": lon0, "dlat": lat1 - lat0, "dlon": lon1 - lon0, "n": env.config.grid_n, "cell_m": cell}


def start_state(spec: ScenarioSpec, seed: int) -> dict[str, Any]:
    env = make_env(spec, seed)
    base = conf.read("lake")["sites"][spec.uavs.base]
    return {
        "grid": grid_geo(env),
        "belief": heat(env.belief()),
        "p_out": round(env.p_out, 4),
        "searchable": env.searchable().astype(int).tolist(),
        "no_fly": env.no_fly().astype(int).tolist(),
        "uavs": env.agent_cells().tolist(),
        "base": {"key": spec.uavs.base, "name": base["name"], "lat": base["lat"], "lon": base["lon"]},
        "lkp": spec.incident.last_known.model_dump(),
        "minutes": spec.search_minutes,
    }


def fly_ahead(spec: ScenarioSpec, seed: int, name: str) -> dict[str, Any]:
    env = make_env(spec, seed)
    planner = make(name)
    o = observe(env)
    planner.reset(o)
    curve: list[float] = []
    tracks: list[list[list[int]]] = [[[int(c[0]), int(c[1])] for c in env.agent_cells()]]
    frames = [{"t": 0, "belief": heat(env.belief()), "uavs": tracks[0], "active": env.agent_active().astype(int).tolist(), "pos": 0.0}]
    while not env.done:
        info = env.step(planner.act(o))
        curve.append(round(info.pos_expected, 5))
        cells = [[int(c[0]), int(c[1])] for c in env.agent_cells()]
        tracks.append(cells)
        if env.t % FRAME_EVERY_STEPS == 0 or env.done:
            frames.append({"t": env.t, "belief": heat(env.belief()), "uavs": cells, "active": env.agent_active().astype(int).tolist(), "pos": curve[-1]})
        o = observe(env)
    return {"planner": name, "expected_success": curve[-1] if curve else 0.0, "pos_curve": curve, "frames": frames, "tracks": tracks}


def lake_geometry() -> dict[str, Any]:
    cfg = conf.read("lake")
    proj = projection()
    zones = []
    for z in lake().no_fly:
        zones.append({"name": z.name, "polygon": [list(proj.to_geo(p.x, p.y)) for p in z.polygon]})
    return {
        "shoreline": cfg["shoreline"],
        "no_fly": zones,
        "sites": [{"key": k, **v} for k, v in cfg["sites"].items()],
        "dt_s": env_config().dt_s,
    }
