"""Train the search policy with PPO on Ray RLlib, select the best checkpoint by the belief's
expected probability of success on validation scenarios, export it to ONNX, and track the run
in MLflow. Runs the same on a laptop or as a KubeRay RayJob (deploy/k8s/rayjob-train.yaml)."""

from __future__ import annotations

import hashlib
import json
import os
import subprocess
import time
from pathlib import Path
from typing import Any

import numpy as np
import torch

from sar import _sarsim as core
from sar import conf
from sar.planners import observe
from sar.sim.build import env_config, scenario
from sar.sim.generator import random_spec
from sar.sim.lake import lake


def belief_score(pi: torch.nn.Module, n: int, first_seed: int) -> float:
    """Mean expected probability of success (belief) of the greedy policy over n validation
    scenarios. Reads no truth."""
    cfg = env_config()
    total = 0.0
    with torch.no_grad():
        for i in range(n):
            spec = random_spec(first_seed + i)
            env = core.Env(lake(), cfg)
            env.reset(scenario(spec, first_seed + i))
            while not env.done:
                o = observe(env)
                logits = pi(torch.from_numpy(o.obs)).numpy()
                actions = np.where(o.mask.astype(bool), logits, -1e9).argmax(axis=1)
                env.step([int(a) for a in actions])
            total += env.pos_expected
    return total / max(1, n)


def _git_commit() -> str:
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=conf.ROOT, text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def export_onnx(pi: torch.nn.Module, obs_dim: int, path: Path) -> None:
    pi = pi.eval()
    dummy = torch.zeros(1, obs_dim, dtype=torch.float32)
    path.parent.mkdir(parents=True, exist_ok=True)
    torch.onnx.export(
        pi,
        (dummy,),
        str(path),
        input_names=["obs"],
        output_names=["logits"],
        dynamic_axes={"obs": {0: "batch"}, "logits": {0: "batch"}},
        opset_version=18,
        dynamo=False,
    )


def build_config(cfg: dict[str, Any], smoke: bool, init: dict[str, Any] | None = None) -> Any:
    from ray.rllib.algorithms.ppo import PPOConfig
    from ray.rllib.core.rl_module.multi_rl_module import MultiRLModuleSpec
    from ray.rllib.core.rl_module.rl_module import RLModuleSpec

    from sar.train.envs import SarMultiAgentEnv
    from sar.train.module import MaskedPolicyModule

    ppo = dict(cfg["ppo"])
    if init is not None:
        ppo.update(lr=cfg["finetune"]["lr"], entropy_coeff=cfg["finetune"]["entropy_coeff"])
    if smoke:
        for k in ("train_batch_size_per_learner", "minibatch_size", "num_epochs"):
            ppo[k] = cfg["smoke"][k]
    runners = cfg["runners"]
    return (
        PPOConfig()
        .environment(SarMultiAgentEnv, env_config={"seed": cfg["seed"], "reward_scale": cfg["reward_scale"]})
        .env_runners(
            num_env_runners=runners["num_env_runners"] if not smoke else 0,
            num_envs_per_env_runner=runners["num_envs_per_env_runner"] if not smoke else 1,
            rollout_fragment_length=runners["rollout_fragment_length"],
        )
        .multi_agent(policies={"shared"}, policy_mapping_fn=lambda agent_id, episode, **kw: "shared")
        .rl_module(
            rl_module_spec=MultiRLModuleSpec(
                rl_module_specs={
                    "shared": RLModuleSpec(
                        module_class=MaskedPolicyModule, model_config={"hidden": cfg["model"]["hidden"], "init": init}
                    )
                }
            )
        )
        .training(
            lr=ppo["lr"],
            gamma=ppo["gamma"],
            lambda_=ppo["lambda"],
            clip_param=ppo["clip_param"],
            entropy_coeff=ppo["entropy_coeff"],
            vf_loss_coeff=ppo["vf_loss_coeff"],
            num_epochs=ppo["num_epochs"],
            train_batch_size_per_learner=ppo["train_batch_size_per_learner"],
            minibatch_size=ppo["minibatch_size"],
        )
        .learners(num_learners=0)
        .debugging(seed=cfg["seed"])
    )


def train(
    out: Path | None = None, smoke: bool = False, iterations: int | None = None, start: str = "scratch"
) -> dict[str, Any]:
    """start: "scratch" (random weights) or "imitation" (clone the lookahead planner first)."""
    import mlflow
    import ray

    cfg = conf.read("training")
    out = out or conf.path("models")
    out.mkdir(parents=True, exist_ok=True)
    default_iters = cfg["finetune"]["iterations"] if start == "imitation" else cfg["iterations"]
    iters = iterations or (cfg["smoke"]["iterations"] if smoke else default_iters)
    n_val = cfg["smoke"]["validation_scenarios"] if smoke else cfg["validation"]["scenarios"]
    every = 1 if smoke else cfg["validation"]["every"]
    torch.manual_seed(cfg["seed"])
    if not ray.is_initialized():
        ray.init(
            address=os.environ.get("RAY_ADDRESS"),
            include_dashboard=False,
            log_to_driver=False,
            ignore_reinit_error=True,
        )
    mlflow.set_tracking_uri(os.environ.get("MLFLOW_TRACKING_URI", f"sqlite:///{conf.path('mlflow.db')}"))
    mlflow.set_experiment("bodensee-sar-ppo")
    t0 = time.time()
    init = None
    imitation_stats: dict[str, Any] = {}
    if start == "imitation":
        from sar.train.imitation import pretrain

        pre = pretrain(smoke)
        init = {"pi": pre["pi"], "vf": pre["vf"]}
        imitation_stats = pre["stats"]
    algo = build_config(cfg, smoke, init).build_algo()
    obs_dim = env_config().obs_dim
    best: dict[str, Any] = {"score": -1.0, "iteration": -1, "state": None}
    history: list[dict[str, Any]] = []
    with mlflow.start_run(run_name=("smoke-" if smoke else "") + f"ppo-{start}"):
        if init is not None:
            pi0 = algo.get_module("shared").pi
            score0 = belief_score(pi0, n_val, cfg["validation"]["first_seed"])
            best = {"score": score0, "iteration": 0, "state": {k: v.clone() for k, v in pi0.state_dict().items()}}
            history.append(
                {"iteration": 0, "env_steps": 0, "validation_pos_expected": round(score0, 5), **imitation_stats}
            )
            print(json.dumps(history[-1]), flush=True)
        mlflow.log_params(
            {
                "start": start,
                "iterations": iters,
                "smoke": smoke,
                **{f"ppo.{k}": v for k, v in cfg["ppo"].items()},
                "hidden": str(cfg["model"]["hidden"]),
            }
        )
        for it in range(1, iters + 1):
            result = algo.train()
            runners = result.get("env_runners", {})
            row = {
                "iteration": it,
                "env_steps": int(
                    result.get("num_env_steps_sampled_lifetime", runners.get("num_env_steps_sampled_lifetime", 0)) or 0
                ),
                "episode_return_mean": float(runners.get("episode_return_mean", float("nan"))),
            }
            if it % every == 0 or it == iters:
                pi = algo.get_module("shared").pi
                row["validation_pos_expected"] = round(belief_score(pi, n_val, cfg["validation"]["first_seed"]), 5)
                if row["validation_pos_expected"] > best["score"]:
                    best = {
                        "score": row["validation_pos_expected"],
                        "iteration": it,
                        "state": {k: v.clone() for k, v in pi.state_dict().items()},
                    }
            history.append(row)
            mlflow.log_metrics({k: v for k, v in row.items() if k != "iteration" and v == v}, step=it)
            print(json.dumps(row), flush=True)
        pi = algo.get_module("shared").pi
        if best["state"] is not None:
            pi.load_state_dict(best["state"])
        onnx_path = out / ("policy-smoke.onnx" if smoke else "policy.onnx")
        export_onnx(pi, obs_dim, onnx_path)
        card = {
            "model": onnx_path.name,
            "algorithm": "PPO (Ray RLlib, new API stack), parameters shared by all UAVs, action mask from the safety shield",
            "start": start,
            "imitation": imitation_stats or None,
            "obs_dim": obs_dim,
            "actions": core.N_ACTIONS,
            "iterations": iters,
            "selected_iteration": best["iteration"],
            "validation_pos_expected": best["score"],
            "validation": f"{n_val} generated scenarios from seed {cfg['validation']['first_seed']} (belief only, no truth)",
            "env_steps": history[-1]["env_steps"] if history else 0,
            "train_seconds": round(time.time() - t0),
            "code_commit": _git_commit(),
            "config_sha256": hashlib.sha256(json.dumps(cfg, sort_keys=True).encode()).hexdigest(),
            "sha256": hashlib.sha256(onnx_path.read_bytes()).hexdigest(),
        }
        onnx_path.with_suffix(".json").write_text(json.dumps(card, indent=1) + "\n")
        (out / ("history-smoke.json" if smoke else "history.json")).write_text(json.dumps(history, indent=1) + "\n")
        mlflow.log_artifact(str(onnx_path))
        mlflow.log_dict(card, "model_card.json")
    algo.stop()
    return card
