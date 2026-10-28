"""The simulation as a multi-agent environment for RLlib (and PettingZoo, for its API checks).
Each UAV is an agent; all share the team reward, which is the probability of success the search
gained this step according to the belief. The truth is never read: training needs no answer key.
Episodes are random scenarios from config/sim.yaml, never the dev or held-out files."""

from __future__ import annotations

from typing import Any

import gymnasium as gym
import numpy as np
from ray.rllib.env.multi_agent_env import MultiAgentEnv

from sar import _sarsim as core
from sar.sim.build import env_config, scenario
from sar.sim.generator import random_spec
from sar.sim.lake import lake


def agent_id(k: int) -> str:
    return f"uav_{k}"


def spaces(cfg: core.EnvConfig) -> tuple[gym.spaces.Dict, gym.spaces.Discrete]:
    obs = gym.spaces.Dict(
        {
            "observations": gym.spaces.Box(-np.inf, np.inf, (cfg.obs_dim,), np.float32),
            "action_mask": gym.spaces.Box(0.0, 1.0, (core.N_ACTIONS,), np.float32),
        }
    )
    return obs, gym.spaces.Discrete(core.N_ACTIONS)


class SarMultiAgentEnv(MultiAgentEnv):
    def __init__(self, config: dict[str, Any] | None = None) -> None:
        super().__init__()
        ctx = config  # RLlib passes an EnvContext: a dict with worker_index and vector_index
        config = dict(config or {})
        self.cfg = env_config(**config.get("env", {}))
        self.reward_scale = float(config.get("reward_scale", 10.0))
        self.sim = core.Env(lake(), self.cfg)
        self.possible_agents: list[Any] = [agent_id(k) for k in range(self.cfg.max_agents)]
        obs_space, act_space = spaces(self.cfg)
        self.observation_spaces = dict.fromkeys(self.possible_agents, obs_space)
        self.action_spaces = dict.fromkeys(self.possible_agents, act_space)
        worker = int(getattr(ctx, "worker_index", 0) or 0)
        vector = int(getattr(ctx, "vector_index", 0) or 0)
        self.rng = np.random.default_rng([int(config.get("seed", 0)), worker, vector])
        self.agents: list[Any] = []

    def _obs(self, which: list[int]) -> dict[str, dict[str, np.ndarray]]:
        obs, mask, _active = self.sim.observe()
        return {
            agent_id(k): {"observations": obs[k].astype(np.float32), "action_mask": mask[k].astype(np.float32)}
            for k in which
        }

    def reset(self, *, seed: int | None = None, options: dict[str, Any] | None = None) -> tuple[dict, dict]:
        if seed is not None:
            self.rng = np.random.default_rng(seed)
        while True:
            spec = random_spec(int(self.rng.integers(2**31)))
            self.sim.reset(scenario(spec, int(self.rng.integers(2**31))))
            active = [k for k, a in enumerate(self.sim.agent_active()) if a]
            if active and not self.sim.done:
                break
        self.agents = [agent_id(k) for k in active]
        return self._obs(active), {}

    def step(self, action_dict: dict[Any, Any]) -> tuple[dict, dict, dict, dict, dict]:  # type: ignore[override]
        acting = [int(a.split("_")[1]) for a in self.agents]
        actions = [int(action_dict.get(agent_id(k), 0)) for k in range(self.sim.n_agents)]
        info = self.sim.step(actions)
        active = self.sim.agent_active()
        obs = self._obs(acting)
        reward = self.reward_scale * info.reward
        rewards = dict.fromkeys(obs, reward)
        terminateds = {agent_id(k): bool(info.done or not active[k]) for k in acting}
        terminateds["__all__"] = bool(info.done)
        truncateds = {"__all__": False}
        self.agents = [agent_id(k) for k in acting if not terminateds[agent_id(k)]]
        infos = {a: {"pos_expected": info.pos_expected, "interventions": info.interventions} for a in obs}
        return obs, rewards, terminateds, truncateds, infos
