"""The learned policy at run time: an ONNX export of the trained network, run with ONNX Runtime
(no Ray or PyTorch needed to serve it). Every UAV uses the same network on its own observation;
the shield's mask is applied before choosing, and the shield checks again in the simulator."""

from __future__ import annotations

import json
from pathlib import Path

import numpy as np

from sar import conf
from sar.planners import Observation

DEFAULT_MODEL = conf.path("models", "policy.onnx")


class OnnxPolicy:
    name = "rl"

    def __init__(self, path: str | Path = DEFAULT_MODEL, deterministic: bool = True, seed: int = 0) -> None:
        import onnxruntime as ort

        opts = ort.SessionOptions()
        opts.intra_op_num_threads = 1
        opts.inter_op_num_threads = 1
        self.session = ort.InferenceSession(str(path), opts, providers=["CPUExecutionProvider"])
        self.input = self.session.get_inputs()[0].name
        self.deterministic = deterministic
        self.rng = np.random.default_rng(seed)
        card = Path(path).with_suffix(".json")
        self.card = json.loads(card.read_text()) if card.exists() else {}

    def reset(self, o: Observation) -> None:
        pass

    def logits(self, obs: np.ndarray) -> np.ndarray:
        return np.asarray(self.session.run(None, {self.input: obs.astype(np.float32)})[0])

    def act(self, o: Observation) -> list[int]:
        logits = self.logits(o.obs)
        masked = np.where(o.mask.astype(bool), logits, -1e9)
        if self.deterministic:
            actions = masked.argmax(axis=1)
        else:
            z = masked - masked.max(axis=1, keepdims=True)
            p = np.exp(z) / np.exp(z).sum(axis=1, keepdims=True)
            actions = np.array([self.rng.choice(len(row), p=row) for row in p])
        return [int(a) for a in actions]
