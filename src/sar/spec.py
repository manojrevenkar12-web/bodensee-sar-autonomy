"""A search scenario as a coordinator would describe it (the file format of scenarios/ and
heldout/). The optional `truth` block pins parts of what really happened; anything left out is
drawn per seed by the simulator."""

from __future__ import annotations

from pathlib import Path
from typing import Literal

import yaml
from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator

from sar import conf


class LatLon(BaseModel):
    model_config = ConfigDict(extra="forbid")
    lat: float = Field(ge=47.3, le=47.95)
    lon: float = Field(ge=8.9, le=9.9)


class Incident(BaseModel):
    model_config = ConfigDict(extra="forbid")
    last_known: LatLon
    position_error_m: float = Field(200.0, gt=0, le=3000)
    minutes_since: float = Field(30.0, ge=0, le=180)
    reported_objects: dict[str, float]
    time_of_day: Literal["day", "night"] = "day"

    @field_validator("reported_objects")
    @classmethod
    def known_objects(cls, v: dict[str, float]) -> dict[str, float]:
        known = set(conf.read("objects")["objects"])
        unknown = set(v) - known
        if unknown:
            raise ValueError(f"unknown objects {sorted(unknown)}; known: {sorted(known)}")
        if not v or any(p < 0 for p in v.values()) or sum(v.values()) <= 0:
            raise ValueError("reported_objects needs positive weights")
        total = sum(v.values())
        return {k: p / total for k, p in v.items()}


class Forecast(BaseModel):
    model_config = ConfigDict(extra="forbid")
    wind_speed: float = Field(ge=0, le=25)
    wind_from_deg: float = Field(ge=0, le=360)


class Uavs(BaseModel):
    model_config = ConfigDict(extra="forbid")
    count: int = Field(ge=1, le=4)
    endurance_min: float = Field(ge=8, le=90)
    base: str

    @field_validator("base")
    @classmethod
    def known_site(cls, v: str) -> str:
        sites = conf.read("lake")["sites"]
        if v not in sites:
            raise ValueError(f"unknown base {v!r}; known: {sorted(sites)}")
        return v


class TruthOverride(BaseModel):
    model_config = ConfigDict(extra="forbid")
    object: str | None = None
    wind_speed: float | None = Field(None, ge=0, le=25)
    wind_from_deg: float | None = Field(None, ge=0, le=360)
    entered_water: LatLon | None = None


class ScenarioSpec(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: str = Field(pattern=r"^[a-z0-9][a-z0-9-]{1,63}$")
    description: str = ""
    incident: Incident
    forecast: Forecast
    uavs: Uavs
    search_minutes: float = Field(30.0, ge=5, le=60)
    truth: TruthOverride = TruthOverride()

    @model_validator(mode="after")
    def truth_object_known(self) -> ScenarioSpec:
        if self.truth.object is not None and self.truth.object not in conf.read("objects")["objects"]:
            raise ValueError(f"unknown truth.object {self.truth.object!r}")
        return self

    @property
    def horizon_steps(self) -> int:
        return round(self.search_minutes * 60.0 / float(conf.read("sim")["env"]["dt_s"]))


def load(path: str | Path) -> ScenarioSpec:
    with open(path, encoding="utf-8") as f:
        return ScenarioSpec.model_validate(yaml.safe_load(f))


def load_dir(path: str | Path) -> list[ScenarioSpec]:
    return [load(p) for p in sorted(Path(path).glob("*.yaml"))]
