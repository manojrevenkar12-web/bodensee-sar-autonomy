// What a search is made of: the object adrift, the sensor, the scenario as the coordinator knows
// it, and the truth only the simulator knows.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sar/geo.hpp"

namespace sar {

// Leeway of a drifting object in the style of the IAMSAR/Allen leeway tables: downwind and
// crosswind speed = slope * U10 + intercept (m/s), each with a standard error.
struct ObjectType {
  std::string name;
  double dw_slope = 0.011, dw_icpt = 0.068, dw_sd = 0.03;
  double cw_slope = 0.004, cw_icpt = 0.04, cw_sd = 0.02;
};

// One pass of a UAV's camera over an object: the lateral range curve (probability of detecting
// it at a given lateral distance from the track), measured from the OpenCV detector, and its
// integral, the sweep width, which is all the planner uses.
struct SensorModel {
  std::vector<double> offsets_m;             // ascending, starting at 0
  std::vector<std::vector<double>> lrc;      // [object][offset]
  std::vector<double> sweep_width_m;         // [object]
};

// The simulator's realised world. Planners and policies never see it.
struct Truth {
  int object = 0;
  Vec2 start;                       // where the object entered the water
  double wind_speed = 5.0;          // mean actual wind, m/s
  double wind_from_deg = 240.0;
  double gust_sd = 1.0;             // AR(1) fluctuation of the wind speed, m/s
  double gust_tau_s = 600.0;
  double current_factor = 0.025;    // wind-driven surface current as a fraction of U10
  double dw_slope = 0.011, dw_icpt = 0.068;
  double cw_slope = 0.004, cw_icpt = 0.04;  // signed: which side of downwind it drifts to
  double rw_sigma = 0.7;            // turbulent random walk, m / sqrt(s)
};

// The search as the coordinator knows it at the start: last known position, time since, what
// may be in the water, the wind forecast and its expected error, the UAVs available.
struct Scenario {
  Vec2 lkp;
  double lkp_sigma_m = 200.0;
  double delay_s = 1800.0;          // incident to start of search
  std::vector<ObjectType> objects;
  std::vector<double> object_prior;  // the coordinator's belief over objects
  double wind_speed = 5.0, wind_from_deg = 240.0;
  double wind_speed_sd = 1.0, wind_dir_sd_deg = 20.0;
  double current_factor = 0.025, current_factor_sd = 0.008;
  double rw_sigma = 0.7;
  int n_uav = 3;
  double endurance_s = 1800.0;
  Vec2 base;
  int horizon_steps = 120;
  SensorModel sensor;
  Truth truth;
  std::uint64_t seed = 1;
};

struct EnvConfig {
  int grid_n = 32;
  double cell_m = 150.0;
  double dt_s = 15.0;               // one cell per step: 10 m/s search speed
  int n_particles = 2048;
  double transit_speed = 15.0;      // m/s, to and from the base
  double reserve_s = 180.0;         // battery left on landing
  int crop = 11;                    // agent-centred window, cells (odd)
  int coarse = 8;                   // global summary, blocks per side
  double pre_search_dt_s = 60.0;    // step used to drift from incident to start of search
  int max_agents = 4;

  double reach_sigma_cells = 3.0;   // spread of the 'reachable probability' channel

  int obs_dim() const { return 5 * crop * crop + 3 * coarse * coarse + 7; }
};

constexpr int kActions = 9;  // stay, N, NE, E, SE, S, SW, W, NW
constexpr int kDx[kActions] = {0, 0, 1, 1, 1, 0, -1, -1, -1};
constexpr int kDy[kActions] = {0, 1, 1, 0, -1, -1, -1, 0, 1};

}  // namespace sar
