"""Vehicles the API uploads approved plans to. In the demo, stand-in autopilots (pymavlink) on
local UDP ports; in the field, the autopilots' MAVLink endpoints from SAR_VEHICLES
("host:port,host:port")."""

from __future__ import annotations

import os
from typing import Any

from sar.interop.fake_vehicle import FakeVehicle
from sar.interop.mavlink_link import MavlinkLink, mission_from_track


class DemoFleet:
    def __init__(self, n: int, drop_rate: float = 0.1) -> None:
        self.vehicles = [FakeVehicle(drop_rate=drop_rate, seed=k, sysid=k + 1) for k in range(n)]

    def __enter__(self) -> DemoFleet:
        for v in self.vehicles:
            v.__enter__()
        return self

    def __exit__(self, *exc: object) -> None:
        for v in self.vehicles:
            v.__exit__(*exc)

    def endpoints(self) -> list[tuple[str, int, int]]:
        return [("127.0.0.1", v.port, k + 1) for k, v in enumerate(self.vehicles)]


class FieldFleet:
    def __init__(self, spec: str) -> None:
        self._eps = []
        for k, part in enumerate(x for x in spec.split(",") if x):
            host, port = part.rsplit(":", 1)
            self._eps.append((host, int(port), k + 1))

    def endpoints(self) -> list[tuple[str, int, int]]:
        return self._eps


def fleet_from_env() -> FieldFleet | None:
    spec = os.environ.get("SAR_VEHICLES", "")
    return FieldFleet(spec) if spec else None


def upload_plan(fleet: Any, start: dict[str, Any], tracks: list[list[list[int]]]) -> list[dict[str, Any]]:
    g = start["grid"]
    base = (start["base"]["lat"], start["base"]["lon"])
    n_uav = len(tracks[0]) if tracks else 0
    results = []
    for k, (host, port, sysid) in enumerate(fleet.endpoints()[:n_uav]):
        cells = [step[k] for step in tracks]
        geo = [(g["lat0"] + (iy + 0.5) * g["dlat"], g["lon0"] + (ix + 0.5) * g["dlon"]) for ix, iy in cells]
        link = MavlinkLink(host, port, target_system=sysid)
        try:
            r = link.upload(mission_from_track(geo, base))
        finally:
            link.close()
        results.append(
            {
                "uav": k,
                "vehicle": f"{host}:{port}",
                "ok": int(r.ok),
                "items": r.items,
                "retransmissions": r.retransmissions,
                "error": r.error,
            }
        )
    return results
