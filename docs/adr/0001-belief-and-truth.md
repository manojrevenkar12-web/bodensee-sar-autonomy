# 0001: Two worlds in one simulator: the belief decides, the truth scores

## Context

A search planner must work from what a coordinator can know: a last known position, a wind
forecast, what was reported. A simulator also knows where the object really is. If that truth
leaks into planning, training or model selection, results look better than they can be in the
field.

## Decision

The C++ environment keeps both, apart:

- the **belief** is a Monte Carlo drift forecast (2048 particles over position, object type,
  leeway, wind and current errors), as in the US Coast Guard's SAROPS. Every unsuccessful look
  multiplies the particles in the scanned cell by (1 - POD). Observations, action masks, rewards
  and planners' views come from it alone;
- the **truth** drifts one object with the real wind (with gusts), its own leeway and turbulence,
  and decides detections with the lateral range curve measured from the detector.

The reinforcement learning reward is the probability of success the belief gains each step, so
training needs no truth at all. Model selection uses the same belief quantity on validation
scenarios. Truth is read only to score (`sar.runner`, `sar.evaluation`) and in the exercise
debrief, after an approval.

## Consequences

- A planner's expected success is its own forecast; the evaluation reports it next to the real
  success rate, and the forecast runs high (the world's errors are wider than assumed).
- Tests run every planner against a poisoned truth and require identical decisions.
