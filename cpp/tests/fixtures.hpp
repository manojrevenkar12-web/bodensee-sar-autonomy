// A square test lake and a simple scenario, so C++ tests do not depend on config files.
#pragma once

#include <memory>

#include "sar/env.hpp"
#include "sar/lake.hpp"

namespace sar::test {

// 20 km x 20 km of water centred on the origin, a no-fly square in the north-east corner, a site
// on the southern shore.
inline std::shared_ptr<Lake> square_lake(bool with_no_fly = true) {
  std::vector<Vec2> shore{{-10000, -10000}, {10000, -10000}, {10000, 10000}, {-10000, 10000}};
  std::vector<Zone> zones;
  if (with_no_fly) zones.push_back({"test zone", {{1500, 1500}, {3000, 1500}, {3000, 3000}, {1500, 3000}}});
  std::vector<Site> sites{{"south", {0, -2500}}};
  return std::make_shared<Lake>(Projection(47.6, 9.4), shore, zones, sites, 50.0, 3000.0);
}

inline SensorModel perfect_sensor(int objects = 1, double width = 60.0) {
  SensorModel s;
  s.offsets_m = {0, 30, 60, 75};
  for (int k = 0; k < objects; ++k) {
    s.lrc.push_back({1.0, 1.0, 0.0, 0.0});
    s.sweep_width_m.push_back(width);
  }
  return s;
}

inline Scenario simple_scenario(std::uint64_t seed = 7) {
  Scenario s;
  s.lkp = {0, 0};
  s.lkp_sigma_m = 150;
  s.delay_s = 600;
  ObjectType piw;
  piw.name = "piw";
  s.objects = {piw};
  s.object_prior = {1.0};
  s.wind_speed = 5;
  s.wind_from_deg = 270;  // from the west: drift east
  s.n_uav = 3;
  s.endurance_s = 1800;
  s.base = {0, -2500};
  s.horizon_steps = 60;
  s.sensor = perfect_sensor();
  s.truth.object = 0;
  s.truth.start = {0, 0};
  s.truth.wind_speed = 5;
  s.truth.wind_from_deg = 270;
  s.seed = seed;
  return s;
}

inline EnvConfig small_config() {
  EnvConfig c;
  c.n_particles = 1024;
  return c;
}

}  // namespace sar::test
