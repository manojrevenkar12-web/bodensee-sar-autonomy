# 0003: PPO on Ray RLlib, with one network shared by every UAV and the shield's mask

## Context

The team's tools are deep reinforcement learning, Ray and Kubernetes. The search has two to
four UAVs, a team objective and hard rules.

## Decision

- PPO from Ray RLlib's new API stack, multi-agent, one policy shared by all UAVs (parameter
  sharing), each acting on its own agent-centred observation. The team reward is the gain in
  the belief's probability of success.
- A custom RLModule adds log(mask) to the logits, so actions the shield forbids are never
  sampled; the shield still checks in the simulator.
- Two starts are compared: from scratch, and from an imitation of the lookahead planner (with
  one round of DAgger and a value network fitted to the returns), then fine-tuned by PPO.
- The best iteration is chosen by the belief's expected success on generated validation
  scenarios, exported to ONNX and served with ONNX Runtime (no Ray or PyTorch at run time).
- Runs are tracked in MLflow. On Kubernetes, KubeRay runs training and evaluation as RayJobs.

## Consequences

- On two CPU cores PPO from scratch learns slowly; the README reports both starts as measured.
- The learned policy is judged on the same dev and blind scenarios as the classical planners.
