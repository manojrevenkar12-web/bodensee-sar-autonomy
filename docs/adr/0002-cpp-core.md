# 0002: The simulator core is C++20 with Python bindings

## Context

Reinforcement learning needs millions of steps; evaluation needs thousands of episodes per
planner; the same code must run in a coordinator's console.

## Decision

The lake, drift, belief, sensor, shield, observations, classical planners and the MAVLink and
DIS codecs are C++20 (`cpp/`), exposed to Python with nanobind and built by scikit-build-core.
Random numbers are our own (xoshiro256**, Box-Muller) so results do not depend on the standard
library. The core is tested with Catch2 under AddressSanitizer and UndefinedBehaviorSanitizer,
with warnings as errors. Type stubs are generated from the bindings and checked in CI.

## Consequences

- About 9,000 environment steps per second per core with 2048 particles and three UAVs.
- Python (`sar`) orchestrates: scenarios, training, evaluation, API, interfaces.
