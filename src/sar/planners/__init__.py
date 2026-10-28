"""Planners decide each UAV's next move from what the coordinator can know: an Observation built
from the environment's belief (view, per-agent observation vectors, the shield's masks). They are
never handed the environment itself, so they cannot read the truth."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol

import numpy as np

from sar import _sarsim as core


@dataclass(frozen=True)
class Observation:
    view: core.View
    obs: np.ndarray  # [agents, obs_dim] float32
    mask: np.ndarray  # [agents, 9] uint8
    active: np.ndarray  # [agents] uint8


def observe(env: core.Env) -> Observation:
    obs, mask, active = env.observe()
    return Observation(env.view(), obs, mask, active)


class Planner(Protocol):
    name: str

    def reset(self, o: Observation) -> None: ...

    def act(self, o: Observation) -> list[int]: ...


class Random:
    name = "random"

    def __init__(self, seed: int = 0) -> None:
        self.rng = core.Rng(seed)

    def reset(self, o: Observation) -> None:
        pass

    def act(self, o: Observation) -> list[int]:
        return list(core.random_actions(o.view, self.rng))


class Sweep:
    """IAMSAR parallel sweep / expanding square, planned once at the start."""

    name = "iamsar"

    def __init__(self) -> None:
        self.p = core.SweepPlanner()

    def reset(self, o: Observation) -> None:
        self.p = core.SweepPlanner()
        self.p.reset(o.view)

    def act(self, o: Observation) -> list[int]:
        return list(self.p.act(o.view))


class Greedy:
    name = "greedy"

    def reset(self, o: Observation) -> None:
        pass

    def act(self, o: Observation) -> list[int]:
        return list(core.greedy_actions(o.view))


class Lookahead:
    name = "lookahead"

    def __init__(self, depth: int = 4) -> None:
        self.depth = depth

    def reset(self, o: Observation) -> None:
        pass

    def act(self, o: Observation) -> list[int]:
        return list(core.lookahead_actions(o.view, self.depth))


BASELINES = {"random": Random, "iamsar": Sweep, "greedy": Greedy, "lookahead": Lookahead}
