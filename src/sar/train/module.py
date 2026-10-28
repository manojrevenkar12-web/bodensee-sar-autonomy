"""The policy network as an RLlib RLModule: one network shared by every UAV (each acts on its own
observation), a separate value network for PPO, and the shield's action mask applied to the
logits so forbidden moves are never sampled."""

from __future__ import annotations

from typing import Any

import torch
from ray.rllib.core.columns import Columns
from ray.rllib.core.rl_module.apis.value_function_api import ValueFunctionAPI
from ray.rllib.core.rl_module.torch.torch_rl_module import TorchRLModule
from torch import nn

FLOAT_MIN = -3.4e38


def mlp(n_in: int, hidden: list[int], n_out: int) -> nn.Sequential:
    layers: list[nn.Module] = []
    last = n_in
    for h in hidden:
        layers += [nn.Linear(last, h), nn.Tanh()]
        last = h
    out = nn.Linear(last, n_out)
    nn.init.orthogonal_(out.weight, gain=0.01)
    nn.init.zeros_(out.bias)
    return nn.Sequential(*layers, out)


class MaskedPolicyModule(TorchRLModule, ValueFunctionAPI):
    def setup(self) -> None:
        obs_space = self.observation_space["observations"]  # type: ignore[index]
        hidden = list(self.model_config.get("hidden", [256, 256]))  # type: ignore[union-attr]
        n_obs = int(obs_space.shape[0])
        n_actions = int(self.action_space.n)  # type: ignore[union-attr]
        self.pi = mlp(n_obs, hidden, n_actions)
        self.vf = mlp(n_obs, hidden, 1)

    def _logits(self, batch: dict[str, Any]) -> torch.Tensor:
        obs = batch[Columns.OBS]
        logits = self.pi(obs["observations"])
        return logits + torch.clamp(torch.log(obs["action_mask"]), min=FLOAT_MIN)

    def _forward(self, batch: dict[str, Any], **kwargs: Any) -> dict[str, Any]:
        return {Columns.ACTION_DIST_INPUTS: self._logits(batch)}

    def _forward_train(self, batch: dict[str, Any], **kwargs: Any) -> dict[str, Any]:
        return {Columns.ACTION_DIST_INPUTS: self._logits(batch)}

    def compute_values(self, batch: dict[str, Any], embeddings: Any = None) -> torch.Tensor:
        return self.vf(batch[Columns.OBS]["observations"]).squeeze(-1)
