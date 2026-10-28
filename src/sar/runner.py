"""Fly one search from start to end with a planner and score it. The planner sees observations
only; the truth is read here, after each step, for scoring and replays."""

from __future__ import annotations

from dataclasses import asdict, dataclass, field
from typing import Any

from sar import _sarsim as core
from sar.planners import Observation, Planner, observe


@dataclass
class Outcome:
    scenario: str
    seed: int
    planner: str
    found: bool
    found_minute: float | None
    found_by: int
    pos_expected: float  # the planner's own estimate of success (belief)
    pos_curve: list[float]  # expected success after each step
    interventions: int
    violations: int
    steps: int
    truth_beached: bool
    tracks: list[list[list[int]]] = field(default_factory=list)  # per step, per agent [ix, iy]

    def as_dict(self) -> dict[str, Any]:
        return asdict(self)


def run(env: core.Env, planner: Planner, scenario_id: str, seed: int, keep_tracks: bool = False) -> Outcome:
    o: Observation = observe(env)
    planner.reset(o)
    interventions = violations = 0
    curve: list[float] = []
    tracks: list[list[list[int]]] = []
    while not env.done:
        actions = planner.act(o)
        info = env.step(actions)
        interventions += info.interventions
        violations += info.violations
        curve.append(round(info.pos_expected, 6))
        if keep_tracks:
            tracks.append([[int(c[0]), int(c[1])] for c in env.agent_cells()])
        o = observe(env)
    truth = env.truth_state()
    dt = env.config.dt_s
    found = truth.found_step >= 0
    return Outcome(
        scenario=scenario_id,
        seed=seed,
        planner=planner.name,
        found=found,
        found_minute=round((truth.found_step + 1) * dt / 60.0, 3) if found else None,
        found_by=truth.found_by,
        pos_expected=round(env.pos_expected, 6),
        pos_curve=curve,
        interventions=interventions,
        violations=violations,
        steps=env.t,
        truth_beached=truth.beached,
        tracks=tracks,
    )
