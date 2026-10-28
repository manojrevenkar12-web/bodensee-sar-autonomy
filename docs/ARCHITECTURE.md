# Architecture

```
                    coordinator (browser)
                           |
        console/  React 19, Vite, Tailwind v4, Motion (Framer Motion), TanStack Query
                           |  REST
        sar.api   FastAPI: missions, plans flown ahead in the belief, approvals (append-only),
                           |  uploads, exercise debrief (truth, after approval only)
       +-------------------+--------------------+-------------------------+
       |                   |                    |                         |
  sar.planners        sar.policy           sar.interop               sar.sim
  IAMSAR sweep,       ONNX Runtime,        MAVLink mission upload    scenarios -> C++ Scenario;
  greedy, lookahead   masked argmax        (UDP, retransmission);    draws the truth per seed
  (C++)                                    DIS Entity State bridge
       |                   |                    |                         |
       +-------------------+--------------------+-------------------------+
                           |  nanobind
        cpp/  C++20 core: lake raster, Monte Carlo belief (particles), truth drift, sensor
              (lateral range curves), shield, observations, planners, MAVLink v2 and DIS v7 codecs

   training (sar.train)                        evaluation (sar.evaluation)
   Ray RLlib PPO, new API stack, shared        every planner, same scenarios and seeds;
   masked policy; imitation start + DAgger;    stratified bootstrap CIs, paired differences;
   MLflow; ONNX export; KubeRay RayJob         local processes or Ray tasks (RayJob)

   perception (sar.perception)
   synthetic EO and thermal frames -> OpenCV detector -> lateral range curves and sweep widths
   -> config/sensor_calibration.yaml (the planner's W; the truth's curves)
```

## One step of the simulation

1. The **shield** turns each UAV's requested move into an allowed one: inside the grid, not into
   a no-fly zone, and with enough battery left to fly home at transit speed and land with the
   reserve. A UAV with no safe move returns to base.
2. Time passes (15 s): particles and the true object drift with leeway (downwind and crosswind,
   slope x U10 + intercept), wind-driven current and turbulent diffusion; anything reaching the
   shore stays there.
3. Every active UAV searches the cell it flew into. The **belief** multiplies the weight of each
   particle in that cell by 1 - POD, where POD = 1 - exp(-W / cell) and W is the calibrated
   sweep width for that object. The **truth** detects the object with the measured lateral range
   curve at the object's distance from the track. External searchers (a boat over DIS) scan too.
4. The team reward is the probability of success gained in the belief.

## Observations (683 numbers per UAV)

An 11 x 11 window around the UAV (belief, searchable water and shore, no-fly or outside, other
UAVs), an 8 x 8 summary of the whole grid (belief, self, others), and seven scalars (time, battery
margin, position, probability outside the grid, expected POD, active UAVs).

## Why these baselines

- **IAMSAR pattern**: what the IAMSAR Manual (Vol. II, ch. 5) tasks: a parallel sweep over the
  area holding 90% of the probability, split between the UAVs, with track spacing from the
  available effort (S = A / effort, at least one cell); an expanding square when the area is
  small. Planned once, it does not adapt.
- **Greedy**: each UAV takes the neighbouring cell with the most detectable probability, with a
  pull toward probability beyond its reach.
- **Lookahead**: a receding-horizon search over the next four moves of each UAV in turn,
  accounting for cells the others will search.
