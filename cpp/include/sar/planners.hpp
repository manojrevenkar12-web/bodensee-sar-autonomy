// Classical planners. They see a View (the belief and the rules), never the truth.
//  - SweepPlanner: what a SAR mission coordinator would task by the IAMSAR manual: a parallel
//    sweep over the area holding 90% of the probability, split between the UAVs, track spacing
//    from the effort available; an expanding square when the area is small.
//  - greedy: each UAV flies to the neighbouring cell with the most probability it can detect.
//  - lookahead: receding-horizon search over the next few moves of every UAV in turn, with a
//    potential toward probability beyond the horizon.
#pragma once

#include <cstdint>
#include <vector>

#include "sar/env.hpp"

namespace sar {

std::vector<int> random_actions(const View& v, Rng& rng);
std::vector<int> greedy_actions(const View& v);
std::vector<int> lookahead_actions(const View& v, int depth = 4, double discount = 0.97);

class SweepPlanner {
 public:
  void reset(const View& v);
  std::vector<int> act(const View& v);
  // Waypoints per agent, as cell pairs, for display.
  const std::vector<std::vector<std::pair<int, int>>>& waypoints() const { return wps_; }
  bool expanding_square() const { return expanding_; }
  int track_spacing_cells() const { return spacing_; }

 private:
  std::vector<std::vector<std::pair<int, int>>> wps_;
  std::vector<std::size_t> next_;
  bool expanding_ = false;
  int spacing_ = 1;
};

// Move from (ix, iy) toward (tx, ty) using only allowed actions; returns the action.
int step_toward(const View& v, int agent, int tx, int ty);

// Probability within reach: potential(c) = sum_j p_j * pod_j * exp(-d(c, j) / length_cells).
std::vector<double> potential(const View& v, double length_cells);

}  // namespace sar
