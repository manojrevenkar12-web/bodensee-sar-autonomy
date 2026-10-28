import numpy as np
import pytest
import torch

from sar import conf
from sar.train.envs import SarMultiAgentEnv


def test_multi_agent_env_follows_the_rllib_contract():
    env = SarMultiAgentEnv({"seed": 3, "reward_scale": 10.0})
    obs, _ = env.reset(seed=3)
    assert obs and set(obs) == set(env.agents)
    space = env.observation_spaces[env.agents[0]]
    for o in obs.values():
        assert space.contains(o)
        assert o["action_mask"].sum() >= 1
    total, steps = 0.0, 0
    done = False
    while not done:
        acts = {a: int(np.flatnonzero(obs[a]["action_mask"])[0]) for a in env.agents}
        obs, rew, term, trunc, _info = env.step(acts)
        assert set(rew) == set(obs)
        total += next(iter(rew.values()), 0.0)
        steps += 1
        done = term["__all__"]
        assert not trunc["__all__"]
    assert steps <= 120
    # the team reward adds up to the belief's probability of success, scaled
    assert total == pytest.approx(10.0 * env.sim.pos_expected, rel=1e-6, abs=1e-6)


def test_policy_module_masks_forbidden_actions():
    import gymnasium as gym
    from ray.rllib.core.columns import Columns

    from sar.sim.build import env_config
    from sar.train.envs import spaces
    from sar.train.module import MaskedPolicyModule

    obs_space, act_space = spaces(env_config())
    mod = MaskedPolicyModule(observation_space=obs_space, action_space=act_space, model_config={"hidden": [32]})
    obs = torch.randn(4, obs_space["observations"].shape[0])
    mask = torch.zeros(4, 9)
    mask[:, 3] = 1.0
    out = mod.forward_inference({Columns.OBS: {"observations": obs, "action_mask": mask}})
    assert out[Columns.ACTION_DIST_INPUTS].argmax(dim=1).tolist() == [3, 3, 3, 3]
    v = mod.compute_values({Columns.OBS: {"observations": obs, "action_mask": mask}})
    assert v.shape == (4,)
    assert isinstance(act_space, gym.spaces.Discrete)


def test_onnx_export_matches_torch(tmp_path):
    import onnxruntime as ort

    from sar.train.module import mlp
    from sar.train.train import export_onnx

    torch.manual_seed(0)
    pi = mlp(683, [64, 64], 9)
    path = tmp_path / "p.onnx"
    export_onnx(pi, 683, path)
    x = np.random.default_rng(0).normal(size=(5, 683)).astype(np.float32)
    want = pi(torch.from_numpy(x)).detach().numpy()
    got = ort.InferenceSession(str(path), providers=["CPUExecutionProvider"]).run(None, {"obs": x})[0]
    assert np.allclose(want, got, atol=1e-5)


def test_trained_policy_card_matches_the_model():
    import hashlib
    import json

    model = conf.path("models", "policy.onnx")
    if not model.exists():
        pytest.skip("no trained model")
    card = json.loads(model.with_suffix(".json").read_text())
    assert card["sha256"] == hashlib.sha256(model.read_bytes()).hexdigest()
    from sar.sim.build import env_config

    assert card["obs_dim"] == env_config().obs_dim and card["actions"] == 9


@pytest.mark.slow
@pytest.mark.ray
def test_rllib_training_smoke(tmp_path, monkeypatch):
    monkeypatch.setenv("MLFLOW_TRACKING_URI", f"sqlite:///{tmp_path}/mlflow.db")
    from sar.train.train import train

    card = train(out=tmp_path, smoke=True)
    assert (tmp_path / "policy-smoke.onnx").exists()
    assert card["iterations"] == conf.read("training")["smoke"]["iterations"]
