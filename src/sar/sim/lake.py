"""The lake from config/lake.yaml, built once and shared by every environment."""

from __future__ import annotations

import math
from functools import lru_cache

from sar import _sarsim as core
from sar import conf


def _circle(proj: core.Projection, lat: float, lon: float, radius_m: float, n: int = 48) -> list[core.Vec2]:
    c = proj.to_local(lat, lon)
    return [
        core.Vec2(c.x + radius_m * math.cos(2 * math.pi * k / n), c.y + radius_m * math.sin(2 * math.pi * k / n))
        for k in range(n)
    ]


@lru_cache(maxsize=1)
def lake() -> core.Lake:
    cfg = conf.read("lake")
    origin = cfg["projection_origin"]
    proj = core.Projection(origin["lat"], origin["lon"])
    shoreline = [proj.to_local(lat, lon) for lat, lon in cfg["shoreline"]]
    zones = []
    for z in cfg["no_fly"]:
        zone = core.Zone()
        zone.name = z["name"]
        zone.polygon = _circle(proj, z["lat"], z["lon"], z["radius_m"])
        zones.append(zone)
    sites = []
    for key, s in cfg["sites"].items():
        site = core.Site()
        site.name = key
        site.pos = proj.to_local(s["lat"], s["lon"])
        sites.append(site)
    return core.Lake(proj, shoreline, zones, sites, float(cfg.get("raster_m", 50)), 3000.0)


def site_position(key: str) -> core.Vec2:
    for s in lake().sites:
        if s.name == key:
            return s.pos
    raise KeyError(key)


def projection() -> core.Projection:
    return lake().projection
