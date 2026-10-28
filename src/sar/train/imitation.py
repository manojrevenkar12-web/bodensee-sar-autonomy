"""Start PPO from a policy that already searches sensibly: clone the lookahead planner (with one
round of DAgger, so the clone also learns to recover from its own mistakes), and fit the value
network to the returns those searches earned. PPO then optimises the real objective from there.
Uses training-distribution scenarios only (generator seeds from 500000), never dev or held-out."""

from __future__ import annotations

from typing import Any

import numpy as np
import torch
from torch import nn

from sar import _sarsim as core
from sar import conf
from sar.planners import Lookahead, observe
from sar.sim.build import env_config, scenario
from sar.sim.generator import random_spec
from sar.sim.lake import lake
from sar.train.module import mlp


def rollouts(
    n_episodes: int, first_seed: int, reward_scale: float, actor: nn.Module | None = None
) -> dict[str, np.ndarray]:
    """Fly episodes and label every UAV's decision with the lookahead's choice. With an actor,
    the actor flies (DAgger) and the lookahead only labels."""
    teacher = Lookahead(int(conf.read("evaluation")["lookahead_depth"]))
    cfg = env_config()
    obs_l: list[np.ndarray] = []
    mask_l: list[np.ndarray] = []
    act_l: list[int] = []
    ret_l: list[float] = []
    for i in range(n_episodes):
        spec = random_spec(first_seed + i)
        env = core.Env(lake(), cfg)
        env.reset(scenario(spec, first_seed + i))
        start = len(obs_l)
        pos_at: list[float] = []
        while not env.done:
            o = observe(env)
            label = teacher.act(o)
            if actor is None:
                act = label
            else:
                with torch.no_grad():
                    logits = actor(torch.from_numpy(o.obs)).numpy()
                act = [int(a) for a in np.where(o.mask.astype(bool), logits, -1e9).argmax(axis=1)]
            for k in range(env.n_agents):
                if o.active[k] and o.mask[k].any():
                    obs_l.append(o.obs[k])
                    mask_l.append(o.mask[k])
                    act_l.append(label[k])
                    pos_at.append(env.pos_expected)
            env.step(act)
        final = env.pos_expected
        ret_l.extend(reward_scale * (final - p) for p in pos_at)
        assert len(ret_l) == len(obs_l), (start, len(obs_l))
    return {
        "obs": np.asarray(obs_l, np.float32),
        "mask": np.asarray(mask_l, np.float32),
        "action": np.asarray(act_l, np.int64),
        "ret": np.asarray(ret_l, np.float32),
    }


def fit(
    data: dict[str, np.ndarray],
    hidden: list[int],
    epochs: int,
    seed: int,
    pi: nn.Module | None = None,
    vf: nn.Module | None = None,
) -> tuple[nn.Module, nn.Module, dict[str, float]]:
    torch.manual_seed(seed)
    d = data["obs"].shape[1]
    pi = pi or mlp(d, hidden, core.N_ACTIONS)
    vf = vf or mlp(d, hidden, 1)
    opt = torch.optim.Adam([*pi.parameters(), *vf.parameters()], lr=1e-3)
    x = torch.from_numpy(data["obs"])
    m = torch.from_numpy(data["mask"])
    y = torch.from_numpy(data["action"])
    r = torch.from_numpy(data["ret"])
    n = len(x)
    gen = torch.Generator().manual_seed(seed)
    for _ in range(epochs):
        perm = torch.randperm(n, generator=gen)
        for b in range(0, n, 1024):
            idx = perm[b : b + 1024]
            logits = pi(x[idx]) + torch.clamp(torch.log(m[idx]), min=-1e9)
            loss = nn.functional.cross_entropy(logits, y[idx]) + 0.5 * nn.functional.mse_loss(
                vf(x[idx]).squeeze(-1), r[idx]
            )
            opt.zero_grad()
            loss.backward()
            opt.step()
    with torch.no_grad():
        logits = pi(x) + torch.clamp(torch.log(m), min=-1e9)
        acc = float((logits.argmax(dim=1) == y).float().mean())
        vloss = float(nn.functional.mse_loss(vf(x).squeeze(-1), r))
    return pi, vf, {"agreement_with_lookahead": round(acc, 4), "value_mse": round(vloss, 4), "samples": n}


def pretrain(smoke: bool = False) -> dict[str, Any]:
    cfg = conf.read("training")
    im = cfg["imitation"]
    episodes = 3 if smoke else im["episodes"]
    hidden = list(cfg["model"]["hidden"])
    data = rollouts(episodes, im["first_seed"], cfg["reward_scale"])
    pi, vf, stats = fit(data, hidden, 2 if smoke else im["epochs"], cfg["seed"])
    for r in range(0 if smoke else im["dagger_rounds"]):
        more = rollouts(episodes // 2, im["first_seed"] + 100_000 * (r + 1), cfg["reward_scale"], actor=pi)
        data = {k: np.concatenate([data[k], more[k]]) for k in data}
        pi, vf, stats = fit(data, hidden, im["epochs"], cfg["seed"] + r + 1, pi, vf)
    stats["episodes"] = episodes
    return {
        "pi": {k: v.numpy() for k, v in pi.state_dict().items()},
        "vf": {k: v.numpy() for k, v in vf.state_dict().items()},
        "stats": stats,
    }
