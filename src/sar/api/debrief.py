"""Exercise debrief: after a plan was approved, replay each planner against the simulator's truth
to show where the object really drifted and which plan would have found it, and when. This is
the only part of the API that reads the truth, and only after the decision."""

from __future__ import annotations

from typing import Any

from sar.api.planning import make
from sar.planners import observe
from sar.sim.build import make_env
from sar.sim.lake import projection
from sar.spec import ScenarioSpec


def debrief(spec: ScenarioSpec, seed: int, planners: list[str]) -> dict[str, Any]:
    proj = projection()
    out: dict[str, Any] = {"planners": {}}
    truth_track: list[list[float]] = []
    for i, name in enumerate(planners):
        env = make_env(spec, seed)
        planner = make(name)
        o = observe(env)
        planner.reset(o)
        while not env.done:
            env.step(planner.act(o))
            if i == 0:
                t = env.truth_state()
                truth_track.append(list(proj.to_geo(t.pos.x, t.pos.y)))
            o = observe(env)
        t = env.truth_state()
        out["planners"][name] = {
            "found": t.found_step >= 0,
            "found_minute": round((t.found_step + 1) * env.config.dt_s / 60.0, 2) if t.found_step >= 0 else None,
            "found_by": t.found_by,
        }
        out["beached"] = t.beached
    out["truth_track"] = truth_track[::4]
    return out
