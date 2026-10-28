// The search simulation. Two worlds live side by side and never mix:
//  - the belief: what the coordinator can know, a Monte Carlo drift forecast (as in SAROPS)
//    updated by every unsuccessful search; observations, rewards, masks and the planners' view
//    come from it alone;
//  - the truth: where the object really drifts and whether a camera really sees it, used only to
//    score a search afterwards (truth_state()).
#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "sar/lake.hpp"
#include "sar/model.hpp"
#include "sar/rng.hpp"

namespace sar {

// A searcher outside the UAV team, e.g. a rescue boat reported over DIS by another simulator.
struct ExternalScan {
  Vec2 pos;
  double sweep_width_m = 20.0;
};

// Everything a planner may look at.
struct View {
  int n = 0;
  double cell_m = 0.0, dt_s = 0.0;
  int t = 0, horizon = 0;
  std::vector<float> p;                 // conditional probability per cell, row-major [iy * n + ix]
  std::vector<float> pod;               // probability of detection of one pass, per cell
  std::vector<std::uint8_t> searchable; // water or shoreline
  std::vector<std::uint8_t> no_fly;
  std::vector<double> return_s;         // flight time from each cell back to the base
  float p_out = 0.0f;                  // probability the object is outside the grid
  double sweep_width_m = 0.0;           // prior-weighted, for pattern spacing
  std::vector<int> ax, ay;
  std::vector<std::uint8_t> active;
  std::vector<std::uint16_t> mask;      // allowed actions per agent (bit a)
  std::vector<double> margin_s;         // battery left beyond return and reserve
};

struct StepInfo {
  double reward = 0.0;        // probability of success gained this step (belief)
  double pos_expected = 0.0;  // cumulative probability of success so far (belief)
  int interventions = 0;      // actions the shield replaced
  int violations = 0;         // states outside the rules after the shield (must stay 0)
  int active = 0;
  bool done = false;
};

struct TruthState {
  Vec2 pos;
  bool beached = false;
  int found_step = -1;        // step at which a camera detected it, -1 if not yet
  int found_by = -1;          // agent index, or -2 for an external searcher
  double wind_speed = 0.0;
};

class Env {
 public:
  Env(std::shared_ptr<const Lake> lake, EnvConfig cfg);

  void reset(const Scenario& s);
  StepInfo step(const std::vector<int>& actions, const std::vector<ExternalScan>& external = {});

  // Observation of one agent: obs has cfg.obs_dim() floats; returns the allowed-action bits.
  std::uint16_t observe(int agent, float* obs) const;
  std::uint16_t action_mask(int agent) const;
  View view() const;

  // Geometry and state the coordinator sees.
  const EnvConfig& config() const { return cfg_; }
  const Lake& lake() const { return *lake_; }
  int n_agents() const { return static_cast<int>(agents_.size()); }
  int t() const { return t_; }
  int horizon() const { return horizon_; }
  bool done() const { return done_; }
  Vec2 grid_origin() const { return {gx0_, gy0_}; }
  Vec2 cell_center(int ix, int iy) const {
    return {gx0_ + (ix + 0.5) * cfg_.cell_m, gy0_ + (iy + 0.5) * cfg_.cell_m};
  }
  int cell_of(Vec2 p) const;
  const std::vector<float>& belief() const { return p_cell_; }
  float p_out() const { return p_out_; }
  double pos_expected() const { return 1.0 - total_w_; }
  std::vector<int> agent_cells() const;  // [ix0, iy0, ix1, iy1, ...]
  std::vector<std::uint8_t> agent_active() const;
  std::vector<double> agent_endurance() const;
  const std::vector<std::uint8_t>& cell_searchable() const { return searchable_; }
  const std::vector<std::uint8_t>& cell_no_fly() const { return no_fly_; }
  double return_time_s(int ix, int iy) const;
  const Scenario& scenario() const { return sc_; }
  // Particle positions and weights (belief), for plotting.
  void particles(std::vector<double>& xy, std::vector<float>& w) const;

  // Truth: for scoring and replays only.
  TruthState truth_state() const;

 private:
  struct AgentState {
    int ix = 0, iy = 0, pix = 0, piy = 0;
    bool active = true;
    double endurance_left = 0.0;
  };

  void drift_particles(double dt, bool random_walk);
  void drift_truth(double dt);
  void map_particles();
  void histogram();
  bool allowed(const AgentState& a, int action) const;
  int nearest_allowed(const AgentState& a, int requested, std::uint16_t mask) const;
  void scan(int ix, int iy, Vec2 from, Vec2 to, bool hover, const std::vector<double>& pod_obj,
            int who, double ext_sweep_width);
  double lrc(int object, double offset) const;

  std::shared_ptr<const Lake> lake_;
  EnvConfig cfg_;
  Scenario sc_;
  Rng rng_belief_, rng_truth_;

  // belief particles (structure of arrays)
  std::vector<double> px_, py_, pvx_, pvy_;
  std::vector<float> pw_;
  std::vector<std::uint8_t> pobj_, pbeached_;
  std::vector<int> pcell_;
  std::vector<double> pod_obj_;   // planner's one-pass probability of detection per object
  double total_w_ = 1.0;

  // grid
  double gx0_ = 0.0, gy0_ = 0.0;
  std::vector<std::uint8_t> searchable_, no_fly_;
  std::vector<float> p_cell_, pod_cell_;
  float p_out_ = 0.0f;
  std::vector<double> return_s_;

  std::vector<AgentState> agents_;
  int t_ = 0, horizon_ = 0;
  bool done_ = false;

  // truth
  Vec2 tpos_;
  bool tbeached_ = false;
  double gust_ = 0.0;
  double t_wind_now_ = 0.0;
  int found_step_ = -1, found_by_ = -1;
};

}  // namespace sar
