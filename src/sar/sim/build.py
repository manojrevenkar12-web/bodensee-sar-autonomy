"""Turn a scenario description into a running simulation. This is the simulator's side: it draws
what really happened (the truth) for a seed. Planners and policies receive the environment and
read only its belief (sar.policy, sar.planners never call truth_state)."""

from __future__ import annotations

import math
import zlib
from functools import lru_cache
from typing import Any

import numpy as np

from sar import _sarsim as core
from sar import conf
from sar.sim.lake import lake, projection, site_position
from sar.spec import ScenarioSpec


def env_config(**overrides: Any) -> core.EnvConfig:
    cfg = core.EnvConfig()
    for k, v in {**conf.read("sim")["env"], **overrides}.items():
        setattr(cfg, k, type(getattr(cfg, k))(v))
    return cfg


def object_type(name: str) -> core.ObjectType:
    o = conf.read("objects")["objects"][name]
    t = core.ObjectType()
    t.name = name
    for k in ("dw_slope", "dw_icpt", "dw_sd", "cw_slope", "cw_icpt", "cw_sd"):
        setattr(t, k, float(o[k]))
    return t


def wind_class(speed: float) -> str:
    for name, upper in conf.read("sim")["wind_classes"].items():
        if speed < upper:
            return str(name)
    return "fresh"


@lru_cache(maxsize=1)
def calibration() -> dict[str, Any]:
    path = conf.path("config", "sensor_calibration.yaml")
    if not path.exists():
        raise FileNotFoundError("config/sensor_calibration.yaml is missing: run `sar calibrate`")
    return conf.read("sensor_calibration")


def sensor_model(objects: list[str], time_of_day: str, forecast_wind: float, true_wind: float) -> core.SensorModel:
    """The planner's sweep widths for the forecast conditions; the truth's lateral range curves for
    the conditions that really hold."""
    cal = calibration()
    sensor = "thermal" if time_of_day == "night" else "eo"
    planned = cal["sensors"][sensor][wind_class(forecast_wind)]
    actual = cal["sensors"][sensor][wind_class(true_wind)]
    m = core.SensorModel()
    m.offsets_m = [float(x) for x in cal["offsets_m"]]
    m.lrc = [[float(x) for x in actual[o]["lrc"]] for o in objects]
    m.sweep_width_m = [float(planned[o]["sweep_width_m"]) for o in objects]
    return m


def _rng(spec: ScenarioSpec, seed: int) -> np.random.Generator:
    return np.random.default_rng([zlib.crc32(spec.id.encode()), int(seed)])


def scenario(spec: ScenarioSpec, seed: int) -> core.Scenario:
    sim = conf.read("sim")
    planner, world = sim["planner"], sim["world"]
    proj = projection()
    objects = list(spec.incident.reported_objects)
    if spec.truth.object is not None and spec.truth.object not in objects:
        objects.append(spec.truth.object)  # what was reported was wrong
    prior = [spec.incident.reported_objects.get(o, 0.0) for o in objects]
    rng = _rng(spec, seed)

    # --- the truth for this seed
    tr = core.Truth()
    tobj = spec.truth.object or str(rng.choice(objects, p=np.array(prior) / sum(prior)))
    tr.object = objects.index(tobj)
    lkp = proj.to_local(spec.incident.last_known.lat, spec.incident.last_known.lon)
    if spec.truth.entered_water is not None:
        tr.start = proj.to_local(spec.truth.entered_water.lat, spec.truth.entered_water.lon)
    else:
        sd = spec.incident.position_error_m
        start = core.Vec2(lkp.x, lkp.y)
        for _ in range(200):
            cand = core.Vec2(lkp.x + rng.normal(0, sd), lkp.y + rng.normal(0, sd))
            if lake().is_water(cand.x, cand.y):
                start = cand
                break
        tr.start = start
    tr.wind_speed = (
        spec.truth.wind_speed
        if spec.truth.wind_speed is not None
        else max(0.0, spec.forecast.wind_speed + rng.normal(0, world["wind_speed_err_sd"]))
    )
    tr.wind_from_deg = (
        spec.truth.wind_from_deg
        if spec.truth.wind_from_deg is not None
        else (spec.forecast.wind_from_deg + rng.normal(0, world["wind_dir_err_sd_deg"])) % 360.0
    )
    tr.gust_sd = float(world["gust_sd"])
    tr.gust_tau_s = float(world["gust_tau_s"])
    tr.current_factor = max(0.0, float(world["current_factor"]) + rng.normal(0, world["current_factor_sd"]))
    o = conf.read("objects")["objects"][tobj]
    side = 1.0 if rng.uniform() < 0.5 else -1.0
    tr.dw_slope = float(o["dw_slope"])
    tr.dw_icpt = float(o["dw_icpt"]) + rng.normal(0, o["dw_sd"])
    tr.cw_slope = side * float(o["cw_slope"])
    tr.cw_icpt = side * max(0.0, float(o["cw_icpt"]) + rng.normal(0, o["cw_sd"]))
    tr.rw_sigma = float(world["rw_sigma"])

    # --- what the coordinator knows
    s = core.Scenario()
    s.lkp = lkp
    s.lkp_sigma_m = spec.incident.position_error_m
    s.delay_s = spec.incident.minutes_since * 60.0
    s.objects = [object_type(n) for n in objects]
    s.object_prior = prior
    s.wind_speed = spec.forecast.wind_speed
    s.wind_from_deg = spec.forecast.wind_from_deg
    s.wind_speed_sd = float(planner["wind_speed_sd"])
    s.wind_dir_sd_deg = float(planner["wind_dir_sd_deg"])
    s.current_factor = float(planner["current_factor"])
    s.current_factor_sd = float(planner["current_factor_sd"])
    s.rw_sigma = float(planner["rw_sigma"])
    s.n_uav = spec.uavs.count
    s.endurance_s = spec.uavs.endurance_min * 60.0
    s.base = site_position(spec.uavs.base)
    s.horizon_steps = spec.horizon_steps
    s.sensor = sensor_model(objects, spec.incident.time_of_day, spec.forecast.wind_speed, tr.wind_speed)
    s.truth = tr
    s.seed = (zlib.crc32(spec.id.encode()) * 1_000_003 + int(seed)) % (2**63)
    return s


def make_env(spec: ScenarioSpec, seed: int, cfg: core.EnvConfig | None = None) -> core.Env:
    env = core.Env(lake(), cfg or env_config())
    env.reset(scenario(spec, seed))
    return env


def cell_geo(env: core.Env, ix: int, iy: int) -> tuple[float, float]:
    c = env.cell_center(ix, iy)
    return projection().to_geo(c.x, c.y)


def distance_m(a: core.Vec2, b: core.Vec2) -> float:
    return math.hypot(a.x - b.x, a.y - b.y)
